package dev.lxndr.projector;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Looper;
import android.util.Log;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;

/**
 * Makes the radio's microphone the phone's microphone for every app: while the sender holds a connection on
 * 127.0.0.1:port, raw 16 kHz mono s16le PCM read from it is injected through an AudioPolicy "injector" mix that matches
 * every capture preset an app can ask for. When the connection closes the mix is detached and the real microphone is back.
 * Needs MODIFY_AUDIO_ROUTING (held by the shell/root) and no partition change. Verified on the S10+ with a tone: a
 * separate AudioRecord(MIC) read the injected level and returned to the real microphone afterwards.
 * Hidden APIs are reached by reflection, like AudioTap.
 */
final class MicInjector {
    private static final String TAG = "AndroidPlay-MIC";
    static final int RATE = 16000;
    private static final int MIX_ROLE_INJECTOR = 1, RULE_MATCH_ATTRIBUTE_CAPTURE_PRESET = 2, ROUTE_FLAG_LOOP_BACK = 2;
    private static final int[] PRESETS = {MediaRecorder.AudioSource.MIC, MediaRecorder.AudioSource.VOICE_RECOGNITION,
            MediaRecorder.AudioSource.VOICE_COMMUNICATION, MediaRecorder.AudioSource.CAMCORDER, MediaRecorder.AudioSource.UNPROCESSED,
            MediaRecorder.AudioSource.DEFAULT};

    /** Everything one injection needs; null track means the platform refused. */
    private static final class Session {
        Object policy, mix; Class<?> policyClass; AudioManager am; AudioTrack track;
    }

    /** The policy's callbacks need a running looper; the Projector's main thread only sleeps. */
    private static volatile Looper policyLooper;

    static void start(final Context ctx, final int port) {
        android.os.HandlerThread ht = new android.os.HandlerThread("s10-mic-policy");
        ht.start();
        policyLooper = ht.getLooper();
        Thread t = new Thread(new Runnable() { @Override public void run() { serve(ctx, port); } }, "s10-mic");
        t.setDaemon(true);
        t.start();
    }

    private static void serve(Context ctx, int port) {
        for (;;) {
            try (ServerSocket srv = new ServerSocket(port, 1, InetAddress.getByName("127.0.0.1"))) {
                Log.i(TAG, "listening :" + port);
                for (;;) {
                    try (Socket sk = srv.accept()) {
                        sk.setSoTimeout(5000);
                        inject(ctx, sk.getInputStream());
                    } catch (Exception e) {
                        Log.w(TAG, "mic session: " + e);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "mic server: " + e);
                try { Thread.sleep(1000); } catch (InterruptedException ie) { return; }
            }
        }
    }

    private static void inject(Context ctx, InputStream in) throws Exception {
        Session s = attach(ctx);
        long total = 0, lastLog = System.currentTimeMillis(); double sumSq = 0; long cnt = 0;
        try {
            Log.i(TAG, "radio microphone attached");
            byte[] buf = new byte[RATE / 100 * 2];   // 10 ms
            for (;;) {
                int n = in.read(buf, 0, buf.length);
                if (n < 0) break;   // sender closed: back to the real microphone
                if (n == 0) continue;
                s.track.write(buf, 0, n);
                total += n;
                for (int i = 0; i + 1 < n; i += 2) { int v = (short) ((buf[i] & 0xFF) | (buf[i + 1] << 8)); sumSq += (double) v * v; cnt++; }
                if (System.currentTimeMillis() - lastLog > 5000) {
                    Log.i(TAG, "mic bytes=" + total + " rms=" + (int) Math.sqrt(sumSq / Math.max(1, cnt)));
                    sumSq = 0; cnt = 0; lastLog = System.currentTimeMillis();
                }
            }
        } finally {
            detach(s);
            Log.i(TAG, "radio microphone detached, bytes=" + total);
        }
    }

    private static Session attach(Context ctx) throws Exception {
        Session s = new Session();
        Class<?> ruleB = Class.forName("android.media.audiopolicy.AudioMixingRule$Builder");
        Object rb = ruleB.getConstructor().newInstance();
        ruleB.getMethod("setTargetMixRole", int.class).invoke(rb, MIX_ROLE_INJECTOR);
        Method addRule = ruleB.getMethod("addMixRule", int.class, Object.class);
        Method preset = AudioAttributes.Builder.class.getMethod("setInternalCapturePreset", int.class);
        for (int p : PRESETS) {
            AudioAttributes.Builder ab = new AudioAttributes.Builder();
            preset.invoke(ab, p);
            addRule.invoke(rb, RULE_MATCH_ATTRIBUTE_CAPTURE_PRESET, ab.build());
        }
        Object rule = ruleB.getMethod("build").invoke(rb);
        Class<?> mixB = Class.forName("android.media.audiopolicy.AudioMix$Builder");
        Class<?> ruleC = Class.forName("android.media.audiopolicy.AudioMixingRule");
        Object mbo = mixB.getConstructor(ruleC).newInstance(rule);
        AudioFormat f = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_MONO).build();
        mixB.getMethod("setFormat", AudioFormat.class).invoke(mbo, f);
        mixB.getMethod("setRouteFlags", int.class).invoke(mbo, ROUTE_FLAG_LOOP_BACK);
        s.mix = mixB.getMethod("build").invoke(mbo);
        Class<?> mixC = Class.forName("android.media.audiopolicy.AudioMix");
        Class<?> polB = Class.forName("android.media.audiopolicy.AudioPolicy$Builder");
        Object pbo = polB.getConstructor(Context.class).newInstance(ctx);
        polB.getMethod("addMix", mixC).invoke(pbo, s.mix);
        polB.getMethod("setLooper", Looper.class).invoke(pbo, policyLooper);
        s.policy = polB.getMethod("build").invoke(pbo);
        s.policyClass = Class.forName("android.media.audiopolicy.AudioPolicy");
        s.am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        int rr = (Integer) AudioManager.class.getMethod("registerAudioPolicy", s.policyClass).invoke(s.am, s.policy);
        if (rr != 0) throw new IllegalStateException("registerAudioPolicy=" + rr);
        try {
            s.track = (AudioTrack) s.policyClass.getMethod("createAudioTrackSource", mixC).invoke(s.policy, s.mix);
            s.track.play();
        } catch (Exception e) { detach(s); throw e; }
        return s;
    }

    private static void detach(Session s) {
        try { if (s.track != null) { s.track.stop(); s.track.release(); } } catch (RuntimeException ignored) { }
        try { AudioManager.class.getMethod("unregisterAudioPolicy", s.policyClass).invoke(s.am, s.policy); }
        catch (ReflectiveOperationException | RuntimeException e) { Log.w(TAG, "unregister: " + e); }
    }
}
