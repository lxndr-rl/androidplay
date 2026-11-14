package com.s10caros.projection;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Bundle;
import android.view.Surface;
import java.io.ByteArrayOutputStream;
import java.io.IOException;

/**
 * Surface-input H.264 baseline encoder. Emits one Annex-B access unit per callback; key frames always carry SPS+PPS
 * (prepended from the codec-config buffer when the encoder does not repeat them), as the CarPlay sender needs.
 */
public final class H264Encoder {
    public interface Sink { void onAccessUnit(byte[] annexB, boolean keyFrame, long ptsUs); }

    private final int width, height, fps, bitrate;
    private final Sink sink;
    private MediaCodec codec;
    private Thread thread;
    private volatile boolean running;
    private byte[] config = new byte[0];

    public H264Encoder(int width, int height, int fps, int bitrateBps, Sink sink) {
        this.width = width; this.height = height; this.fps = fps; this.bitrate = bitrateBps; this.sink = sink;
    }

    /** Creates the codec and returns the input surface to hand to a VirtualDisplay. */
    public Surface start() throws IOException {
        MediaFormat f = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        f.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
        f.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
        f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
        f.setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline);
        f.setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0);
        f.setInteger(MediaFormat.KEY_LATENCY, 1);
        f.setInteger(MediaFormat.KEY_PRIORITY, 0);                       // realtime
        f.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 100_000);  // idle screen still produces frames
        codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
        codec.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        Surface s = codec.createInputSurface();
        codec.start();
        running = true;
        thread = new Thread(this::drain, "s10-h264-drain");
        thread.start();
        return s;
    }

    /** Ask for an IDR (car sent forceKeyFrame/requestUI). */
    public void requestKeyFrame() {
        Bundle b = new Bundle();
        b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
        try { codec.setParameters(b); } catch (IllegalStateException ignored) { /* stopping */ }
    }

    public void stop() {
        running = false;
        try { if (thread != null) thread.join(2000); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        try { codec.stop(); } catch (RuntimeException ignored) { }
        codec.release();
    }

    private void drain() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (running) {
            int i;
            try { i = codec.dequeueOutputBuffer(info, 100_000); } catch (IllegalStateException e) { return; }
            if (i < 0) continue;   // timeout / format change / buffers changed
            byte[] data = new byte[info.size];
            codec.getOutputBuffer(i).position(info.offset);
            codec.getOutputBuffer(i).get(data, 0, info.size);
            codec.releaseOutputBuffer(i, false);
            if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) { config = data; continue; }
            boolean key = (info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
            if (key && !hasNal(data, 7) && config.length > 0) {
                ByteArrayOutputStream o = new ByteArrayOutputStream(config.length + data.length);
                o.write(config, 0, config.length);
                o.write(data, 0, data.length);
                data = o.toByteArray();
            }
            sink.onAccessUnit(data, key, info.presentationTimeUs);
        }
    }

    /** True if the Annex-B buffer contains a NAL unit of the given type. */
    static boolean hasNal(byte[] d, int type) {
        for (int i = 0; i + 3 < d.length; i++)
            if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == type) return true;
        return false;
    }
}
