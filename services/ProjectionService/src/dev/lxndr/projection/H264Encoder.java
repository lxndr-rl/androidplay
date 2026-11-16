package dev.lxndr.projection;

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
    private volatile MediaCodec codec;
    private Thread thread;
    private volatile boolean running;
    private byte[] config = new byte[0];

    public H264Encoder(int width, int height, int fps, int bitrateBps, Sink sink) {
        this.width = width; this.height = height; this.fps = fps; this.bitrate = bitrateBps; this.sink = sink;
    }

    /** Creates the codec and returns the input surface to hand to a VirtualDisplay.
     *  Vendor encoders (Exynos on the S10+) reject some optional keys: try progressively plainer configurations. */
    public Surface start() throws IOException {
        IOException last = null;
        for (int level = 0; level < 4; level++) {
            MediaFormat f = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
            f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            f.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
            f.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
            f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            // 0: baseline + repeat-previous-frame, 1: repeat only, 2: baseline only, 3: plain. (LATENCY/PRIORITY/MAX_B_FRAMES
            // are rejected by the Exynos encoder on the S10+: configure error -2147479551 with all of them set.)
            if (level == 0 || level == 2) f.setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline);
            if (level == 0 || level == 1) f.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 100_000);   // idle screen still produces frames
            MediaCodec c = null;
            try {
                c = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
                c.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
                Surface s = c.createInputSurface();
                c.start();
                codec = c;
                running = true;
                thread = new Thread(this::drain, "s10-h264-drain");
                thread.start();
                android.util.Log.i("AndroidPlay-PROJ", "encoder " + c.getName() + " configured at level " + level + " " + width + "x" + height);
                return s;
            } catch (RuntimeException e) {
                String d = (e instanceof MediaCodec.CodecException) ? ((MediaCodec.CodecException) e).getDiagnosticInfo() : "";
                android.util.Log.w("AndroidPlay-PROJ", "encoder level " + level + " rejected: " + e + " " + d);
                last = new IOException("encoder rejected level " + level, e);
                if (c != null) { try { c.release(); } catch (RuntimeException ignored) { } }
            }
        }
        throw last;
    }

    /** Ask for an IDR (car sent forceKeyFrame/requestUI). */
    public void requestKeyFrame() {
        MediaCodec c = codec;
        if (c == null) return;   // not started yet: the first frame is an IDR anyway
        Bundle b = new Bundle();
        b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
        try { c.setParameters(b); } catch (IllegalStateException ignored) { /* stopping */ }
    }

    public void stop() {
        running = false;
        try { if (thread != null) thread.join(2000); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        try { codec.stop(); } catch (RuntimeException ignored) { }
        codec.release();
    }

    private void drain() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        long lastLog = System.currentTimeMillis(); int tryAgain = 0, fmt = 0, outs = 0;
        while (running) {
            int i;
            try { i = codec.dequeueOutputBuffer(info, 100_000); }
            catch (RuntimeException e) { android.util.Log.e("AndroidPlay-PROJ", "dequeueOutputBuffer failed: " + e); return; }
            if (i == MediaCodec.INFO_TRY_AGAIN_LATER) tryAgain++; else if (i < 0) fmt++; else outs++;
            if (System.currentTimeMillis() - lastLog > 2000) {
                android.util.Log.i("AndroidPlay-PROJ", "encoder drain: outputs=" + outs + " tryAgain=" + tryAgain + " other=" + fmt);
                lastLog = System.currentTimeMillis();
            }
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
