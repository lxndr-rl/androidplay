package com.s10caros.tonetest;

import android.app.Activity;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Bundle;

/** Plays a 440 Hz sine (48 kHz stereo) for `seconds` (default 8) so the capture probe has something to record. */
public final class ToneActivity extends Activity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        final int seconds = getIntent().getIntExtra("seconds", 8);
        new Thread(() -> {
            int rate = 48000;
            AudioTrack t = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                    .setBufferSizeInBytes(rate / 5 * 4).build();
            t.play();
            short[] buf = new short[rate / 50 * 2];   // 20 ms
            long n = 0;
            for (int blk = 0; blk < seconds * 50; blk++) {
                for (int i = 0; i < buf.length; i += 2, n++) buf[i] = buf[i + 1] = (short) (12000 * Math.sin(2 * Math.PI * 440 * n / rate));
                t.write(buf, 0, buf.length);
            }
            t.stop(); t.release();
            finish();
        }).start();
    }
}
