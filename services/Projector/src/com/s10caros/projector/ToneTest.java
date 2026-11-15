package com.s10caros.projector;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

/** Test tool: plays a 440 Hz tone (amplitude 8000, USAGE_MEDIA) for N seconds, to check the audio capture end to end. */
public final class ToneTest {
    public static void main(String[] a) throws Exception {
        int secs = a.length > 0 ? Integer.parseInt(a[0]) : 8;
        final int rate = 48000;
        AudioTrack t = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setBufferSizeInBytes(rate * 4 / 5).build();
        t.play();
        short[] buf = new short[rate / 10 * 2];
        long n = 0;
        for (int blk = 0; blk < secs * 10; blk++) {
            for (int i = 0; i < buf.length; i += 2, n++) buf[i] = buf[i + 1] = (short) (8000 * Math.sin(2 * Math.PI * 440 * n / rate));
            t.write(buf, 0, buf.length);
        }
        t.stop();
        t.release();
        System.out.println("tone done");
    }
}
