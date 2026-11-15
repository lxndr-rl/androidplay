package com.s10caros.projector;

import android.content.Context;
import android.content.ContextWrapper;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import com.s10caros.projection.H264Encoder;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * Privileged projector (run as the adb shell uid with app_process; in the ROM this becomes a system service).
 * Creates a TRUSTED virtual display with its own display group (so it stays on while the phone screen sleeps,
 * is never locked, and third-party apps may open new tasks on it: an app-owned untrusted display cannot do that),
 * encodes it to H.264 and sends length-prefixed Annex-B access units to the sender on 127.0.0.1.
 *
 *   CLASSPATH=projector.dex app_process / com.s10caros.projector.Projector W H DPI FPS KBPS SINK_PORT [SECONDS]
 * Logs "S10CarOS-PRJ display=<id>" (logcat) and prints it on stdout.
 */
public final class Projector {
    private static final String TAG = "S10CarOS-PRJ";
    // DisplayManager.VIRTUAL_DISPLAY_FLAG_* values (hidden ones are plain ints on purpose)
    private static final int PUBLIC = 1 << 0, PRESENTATION = 1 << 1, OWN_CONTENT_ONLY = 1 << 3, SUPPORTS_TOUCH = 1 << 6,
            ROTATES_WITH_CONTENT = 1 << 7, DESTROY_CONTENT_ON_REMOVAL = 1 << 8, TRUSTED = 1 << 10, OWN_DISPLAY_GROUP = 1 << 11,
            ALWAYS_UNLOCKED = 1 << 12, TOUCH_FEEDBACK_DISABLED = 1 << 13, OWN_FOCUS = 1 << 14;

    private static volatile DataOutputStream out;
    private static volatile H264Encoder encoder;

    /** The package name must belong to the calling uid: "com.android.shell" for uid 2000, "root" for uid 0. */
    private static final class ShellContext extends ContextWrapper {
        ShellContext(Context base) { super(base); }
        private static String pkg() { return android.os.Process.myUid() == 0 ? "root" : "com.android.shell"; }
        @Override public String getPackageName() { return pkg(); }
        @Override public String getOpPackageName() { return pkg(); }
        @Override public String getAttributionTag() { return null; }
        @Override public Context getApplicationContext() { return this; }
    }

    public static void main(String[] a) throws Exception {
        if (a.length < 6) { System.err.println("usage: W H DPI FPS KBPS SINK_PORT [SECONDS]"); System.exit(2); }
        final int w = Integer.parseInt(a[0]), h = Integer.parseInt(a[1]), dpi = Integer.parseInt(a[2]);
        final int fps = Integer.parseInt(a[3]), kbps = Integer.parseInt(a[4]), port = Integer.parseInt(a[5]);
        final int seconds = a.length > 6 ? Integer.parseInt(a[6]) : 3600;
        if (Looper.getMainLooper() == null) Looper.prepareMainLooper();
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("systemMain").invoke(null);
        Method gsc = at.getDeclaredMethod("getSystemContext");
        Context ctx = new ShellContext((Context) gsc.invoke(thread));
        DisplayManager dm = (DisplayManager) ctx.getSystemService(Context.DISPLAY_SERVICE);

        connectSink(port);
        H264Encoder enc = new H264Encoder(w, h, fps, kbps * 1000, new H264Encoder.Sink() {
            @Override public void onAccessUnit(byte[] au, boolean key, long pts) {
                DataOutputStream o = out;
                if (o == null) return;
                try { o.writeInt(au.length); o.write(au); o.flush(); }
                catch (IOException e) { Log.w(TAG, "sink write failed: " + e); out = null; }
            }
        });
        encoder = enc;
        Surface s = enc.start();
        int flags = PUBLIC | PRESENTATION | OWN_CONTENT_ONLY | SUPPORTS_TOUCH | ROTATES_WITH_CONTENT | DESTROY_CONTENT_ON_REMOVAL
                | TRUSTED | OWN_DISPLAY_GROUP | ALWAYS_UNLOCKED | TOUCH_FEEDBACK_DISABLED | OWN_FOCUS;
        VirtualDisplay vd = dm.createVirtualDisplay("s10caros", w, h, dpi, s, flags);
        if (vd == null) { System.err.println("createVirtualDisplay returned null"); System.exit(1); }
        int id = vd.getDisplay().getDisplayId();
        Log.i(TAG, "display=" + id + " size=" + w + "x" + h + " flags=0x" + Integer.toHexString(flags));
        System.out.println("display=" + id);
        System.out.flush();
        Thread.sleep(seconds * 1000L);
        vd.release();
        enc.stop();
        System.exit(0);
    }

    private static void connectSink(final int port) {
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                for (;;) {
                    try {
                        Socket sk = new Socket();
                        sk.connect(new InetSocketAddress("127.0.0.1", port), 1000);
                        sk.setTcpNoDelay(true);
                        out = new DataOutputStream(new java.io.BufferedOutputStream(sk.getOutputStream(), 1 << 16));
                        Log.i(TAG, "sink connected");
                        H264Encoder e = encoder;
                        if (e != null) e.requestKeyFrame();
                        InputStream in = sk.getInputStream();
                        int b;
                        while ((b = in.read()) >= 0) {
                            if (b == 'K') { H264Encoder en = encoder; if (en != null) en.requestKeyFrame(); }
                        }
                        out = null;
                    } catch (IOException e) {
                        try { Thread.sleep(500); } catch (InterruptedException ignored) { return; }
                    }
                }
            }
        }, "s10-sink");
        t.setDaemon(true);
        t.start();
    }
}
