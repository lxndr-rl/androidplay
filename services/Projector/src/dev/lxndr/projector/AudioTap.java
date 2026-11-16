package dev.lxndr.projector;

import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.util.Log;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * Captures the phone's audio OUTPUT (AudioSource.REMOTE_SUBMIX, needs CAPTURE_AUDIO_OUTPUT: held by the shell/root)
 * and streams raw 48 kHz stereo 16-bit LE PCM to the sender on 127.0.0.1:port (reconnects forever).
 * With the submix active the speaker is expected to stay silent; that is checked by ear, not by this code.
 */
final class AudioTap {
    private static final String TAG = "AndroidPlay-AUD";
    private static final int RATE = 48000, CHUNK = 480 * 4;   // 10 ms

    static void start(final Context ctx, final int port) {
        Thread t = new Thread(new Runnable() { @Override public void run() { loop(ctx, port); } }, "s10-audio");
        t.setDaemon(true);
        t.start();
    }

    private static AudioRecord open(Context ctx) throws Exception {
        AudioFormat fmt = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build();
        AudioRecord.Builder b = new AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.REMOTE_SUBMIX)
                .setAudioFormat(fmt).setBufferSizeInBytes(CHUNK * 20);
        try {   // hidden: attributes the capture to the shell/root package instead of a missing application context
            Method m = AudioRecord.Builder.class.getMethod("setContext", Context.class);
            m.invoke(b, ctx);
        } catch (ReflectiveOperationException e) { Log.w(TAG, "setContext unavailable: " + e); }
        return b.build();
    }

    private static void loop(Context ctx, int port) {
        for (;;) {
            AudioRecord rec = null;
            try (Socket sk = new Socket()) {
                sk.connect(new InetSocketAddress("127.0.0.1", port), 1000);
                sk.setTcpNoDelay(true);
                OutputStream out = sk.getOutputStream();
                rec = open(ctx);
                if (rec.getState() != AudioRecord.STATE_INITIALIZED) throw new IllegalStateException("AudioRecord not initialised");
                rec.startRecording();
                Log.i(TAG, "capturing REMOTE_SUBMIX -> :" + port);
                // The submix device keeps its OWN music volume (observed: default level ~ -33 dB). It only applies while the
                // submix is the active output, so keep asking for full scale; the radio's volume knob is the real control.
                final AudioManager am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
                long lastVol = 0;
                byte[] buf = new byte[CHUNK];
                long total = 0, lastLog = System.currentTimeMillis(); double sumSq = 0; long cnt = 0;
                while (true) {
                    int n = rec.read(buf, 0, buf.length);
                    if (n <= 0) { Thread.sleep(5); if (n < 0) throw new IllegalStateException("read error " + n); continue; }
                    if (am != null && System.currentTimeMillis() - lastVol > 1000) {
                        lastVol = System.currentTimeMillis();
                        try { am.setStreamVolume(AudioManager.STREAM_MUSIC, am.getStreamMaxVolume(AudioManager.STREAM_MUSIC), 0); }
                        catch (RuntimeException e) { Log.w(TAG, "setStreamVolume: " + e); }
                    }
                    out.write(buf, 0, n);
                    total += n;
                    for (int i = 0; i + 1 < n; i += 2) { int v = (short) ((buf[i] & 0xFF) | (buf[i + 1] << 8)); sumSq += (double) v * v; cnt++; }
                    if (System.currentTimeMillis() - lastLog > 5000) {
                        Log.i(TAG, "audio bytes=" + total + " rms=" + (int) Math.sqrt(sumSq / Math.max(1, cnt)));
                        sumSq = 0; cnt = 0; lastLog = System.currentTimeMillis();
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "audio tap: " + e);
                try { Thread.sleep(1000); } catch (InterruptedException ie) { return; }
            } finally {
                if (rec != null) { try { rec.stop(); } catch (RuntimeException ignored) { } rec.release(); }
            }
        }
    }
}
