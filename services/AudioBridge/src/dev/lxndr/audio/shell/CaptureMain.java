package dev.lxndr.audio.shell;

import android.content.AttributionSource;
import android.content.Context;
import android.content.ContextWrapper;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Looper;
import android.os.Process;
import java.io.BufferedOutputStream;
import java.io.OutputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Privileged system-audio capture probe, run with app_process as the adb shell uid (a platform service in the ROM).
 *   CLASSPATH=capture.dex app_process / dev.lxndr.audio.shell.CaptureMain SECONDS > out.pcm
 * Writes 48 kHz 16-bit stereo little-endian PCM to stdout. Source: REMOTE_SUBMIX (needs CAPTURE_AUDIO_OUTPUT).
 * Uses hidden APIs (fake system Context, AudioRecord.Builder.setContext); may break on other Android versions.
 */
public final class CaptureMain {
    static final int RATE = 48000;

    public static void main(String[] args) throws Exception {
        int seconds = args.length > 0 ? Integer.parseInt(args[0]) : 5;
        final Context ctx = fakeContext();
        AudioFormat fmt = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build();
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord.Builder b = new AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.REMOTE_SUBMIX)
                .setAudioFormat(fmt).setBufferSizeInBytes(Math.max(min, RATE / 5 * 4));
        Method setContext = AudioRecord.Builder.class.getMethod("setContext", Context.class);
        setContext.invoke(b, ctx);
        AudioRecord rec = b.build();
        if (rec.getState() != AudioRecord.STATE_INITIALIZED) { System.err.println("AudioRecord not initialized"); System.exit(3); }
        rec.startRecording();
        OutputStream out = new BufferedOutputStream(System.out, 1 << 16);
        byte[] buf = new byte[RATE / 100 * 4];   // 10 ms
        long end = System.nanoTime() + seconds * 1_000_000_000L, total = 0;
        while (System.nanoTime() < end) {
            int n = rec.read(buf, 0, buf.length);
            if (n < 0) { System.err.println("read error " + n); break; }
            out.write(buf, 0, n);
            total += n;
        }
        out.flush();
        rec.stop(); rec.release();
        System.err.println("CAPTURED bytes=" + total + " ms=" + total / 4 * 1000 / RATE);
    }

    /** System context posing as com.android.shell (same trick scrcpy uses). */
    private static Context fakeContext() throws Exception {
        if (Looper.getMainLooper() == null) Looper.prepareMainLooper();
        Class<?> at = Class.forName("android.app.ActivityThread");
        Constructor<?> c = at.getDeclaredConstructor();
        c.setAccessible(true);
        Object thread = c.newInstance();
        Field cur = at.getDeclaredField("sCurrentActivityThread"); cur.setAccessible(true); cur.set(null, thread);
        Field sys = at.getDeclaredField("mSystemThread"); sys.setAccessible(true); sys.setBoolean(thread, true);
        final Context system = (Context) at.getMethod("getSystemContext").invoke(thread);
        return new ContextWrapper(system) {
            @Override public String getPackageName() { return "com.android.shell"; }
            @Override public String getOpPackageName() { return "com.android.shell"; }
            @Override public AttributionSource getAttributionSource() {
                return new AttributionSource.Builder(Process.SHELL_UID).setPackageName("com.android.shell").build();
            }
            @Override public Context getApplicationContext() { return this; }
        };
    }
}
