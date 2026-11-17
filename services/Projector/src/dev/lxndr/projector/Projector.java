package dev.lxndr.projector;

import android.content.Context;
import android.content.ContextWrapper;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import dev.lxndr.projection.H264Encoder;
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
 *   CLASSPATH=projector.dex app_process / dev.lxndr.projector.Projector W H DPI FPS KBPS SINK_PORT [SECONDS [AUDIO_PORT]]
 * Logs "AndroidPlay-PRJ display=<id>" (logcat) and prints it on stdout.
 */
public final class Projector {
    private static final String TAG = "AndroidPlay-PRJ";
    // DisplayManager.VIRTUAL_DISPLAY_FLAG_* values (hidden ones are plain ints on purpose)
    private static final int PUBLIC = 1 << 0, PRESENTATION = 1 << 1, OWN_CONTENT_ONLY = 1 << 3, SUPPORTS_TOUCH = 1 << 6,
            ROTATES_WITH_CONTENT = 1 << 7, DESTROY_CONTENT_ON_REMOVAL = 1 << 8, TRUSTED = 1 << 10, OWN_DISPLAY_GROUP = 1 << 11,
            ALWAYS_UNLOCKED = 1 << 12, SHOW_SYSTEM_DECORATIONS = 1 << 9, TOUCH_FEEDBACK_DISABLED = 1 << 13, OWN_FOCUS = 1 << 14;

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
        if (a.length < 6) { System.err.println("usage: W H DPI FPS KBPS SINK_PORT [SECONDS [AUDIO_PORT [decor|- [MIC_PORT]]]]"); System.exit(2); }
        final int w = Integer.parseInt(a[0]), h = Integer.parseInt(a[1]), dpi = Integer.parseInt(a[2]);
        final int fps = Integer.parseInt(a[3]), kbps = Integer.parseInt(a[4]), port = Integer.parseInt(a[5]);
        final int seconds = a.length > 6 ? Integer.parseInt(a[6]) : 3600;
        final int audioPort = a.length > 7 ? Integer.parseInt(a[7]) : 0;
        final boolean decor = a.length > 8 && a[8].equals("decor");   // experiment: let the IME / system decorations live on this display
        if (Looper.getMainLooper() == null) Looper.prepareMainLooper();
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("systemMain").invoke(null);
        Method gsc = at.getDeclaredMethod("getSystemContext");
        Context ctx = new ShellContext((Context) gsc.invoke(thread));
        DisplayManager dm = (DisplayManager) ctx.getSystemService(Context.DISPLAY_SERVICE);

        connectSink(port);
        if (audioPort > 0) AudioTap.start(ctx, audioPort);
        final int micPort = a.length > 9 ? Integer.parseInt(a[9]) : 0;   // radio microphone -> phone microphone (16 kHz mono PCM in)
        if (micPort > 0) MicInjector.start(ctx, micPort);
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
        if (decor) flags |= SHOW_SYSTEM_DECORATIONS;
        VirtualDisplay vd = dm.createVirtualDisplay("androidplay", w, h, dpi, s, flags);
        if (vd == null) { System.err.println("createVirtualDisplay returned null"); System.exit(1); }
        int id = vd.getDisplay().getDisplayId();
        setImeLocal(id);
        keepAwake(ctx, id);
        startPanelSupervisor();
        Log.i(TAG, "display=" + id + " size=" + w + "x" + h + " flags=0x" + Integer.toHexString(flags));
        System.out.println("display=" + id);
        System.out.flush();
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() { @Override public void run() { panelOn(); } }));
        Thread.sleep(seconds * 1000L);
        panelOn();
        vd.release();
        enc.stop();
        System.exit(0);
    }

    /** IME policy LOCAL (0): the soft keyboard is shown on this display instead of falling back to the phone's screen. */
    private static void setImeLocal(int displayId) {
        try {
            Object sm = Class.forName("android.os.ServiceManager").getMethod("getService", String.class).invoke(null, "window");
            Object wm = Class.forName("android.view.IWindowManager$Stub").getMethod("asInterface", android.os.IBinder.class).invoke(null, sm);
            wm.getClass().getMethod("setDisplayImePolicy", int.class, int.class).invoke(wm, displayId, 0);
            Log.i(TAG, "IME policy LOCAL set on display " + displayId);
        } catch (Throwable t) { Log.w(TAG, "setDisplayImePolicy failed: " + t + " cause=" + t.getCause()); }
    }

    /**
     * The phone's own screen is useless in the car (and broken here): let it sleep. A sleeping phone also puts the
     * virtual display's group to sleep unless that group is woken again, so wake THIS display's group once a second
     * (PowerManager.wakeUp(time, reason, details, displayId) is hidden) and hold a partial wake lock for the CPU.
     */
    private static void keepAwake(final Context ctx, final int displayId) {
        final android.os.PowerManager pm = (android.os.PowerManager) ctx.getSystemService(Context.POWER_SERVICE);
        try { android.os.PowerManager.WakeLock wl = pm.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "androidplay:projector"); wl.setReferenceCounted(false); wl.acquire(); }
        catch (Throwable t) { Log.w(TAG, "partial wake lock failed: " + t); }
        Thread th = new Thread(new Runnable() {
            @Override public void run() {
                Method wake;
                try { wake = android.os.PowerManager.class.getMethod("wakeUp", long.class, int.class, String.class, int.class); }
                catch (Throwable t) { Log.w(TAG, "PowerManager.wakeUp(displayId) unavailable: " + t); return; }
                boolean logged = false;
                for (;;) {
                    try { wake.invoke(pm, android.os.SystemClock.uptimeMillis(), 2 /* WAKE_REASON_APPLICATION */, "androidplay", displayId); if (!logged) { Log.i(TAG, "keeping display group of " + displayId + " awake"); logged = true; } }
                    catch (Throwable t) { if (!logged) { Log.w(TAG, "wakeUp failed: " + t + " cause=" + t.getCause()); logged = true; } }
                    try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
                }
            }
        }, "s10-keepawake");
        th.setDaemon(true);
        th.start();
    }

    private static final int POWER_MODE_OFF = 0, POWER_MODE_NORMAL = 2;
    private static volatile Object panelToken;
    private static volatile boolean panelLogged;

    /**
     * Turns the phone's physical panel off WITHOUT putting the device (or the projected display) to sleep, like
     * scrcpy --turn-screen-off: SurfaceControl.setDisplayPowerMode on the physical display token. The token API
     * moved between Android versions, so several lookups are tried; failure is logged, never fatal.
     */
    private static void panelOff() {
        try {
            Object token = panelToken;
            Class<?> sc = Class.forName("android.view.SurfaceControl");
            if (token == null) {
            try {
                long[] ids = (long[]) sc.getMethod("getPhysicalDisplayIds").invoke(null);
                token = sc.getMethod("getPhysicalDisplayToken", long.class).invoke(null, ids[0]);
                Log.i(TAG, "panel token via SurfaceControl");
            } catch (ReflectiveOperationException first) {
                Log.i(TAG, "SurfaceControl token API missing (" + first + "), trying DisplayControl");
                // DisplayControl lives in services.jar (not on the boot classpath); its natives come from libandroid_servers.
                ClassLoader cl = new dalvik.system.DexClassLoader("/system/framework/services.jar", null, null, ClassLoader.getSystemClassLoader());
                Class<?> dc = cl.loadClass("com.android.server.display.DisplayControl");
                Method load = Runtime.class.getDeclaredMethod("loadLibrary0", Class.class, String.class);
                load.setAccessible(true);
                load.invoke(Runtime.getRuntime(), dc, "android_servers");
                long[] ids = (long[]) dc.getMethod("getPhysicalDisplayIds").invoke(null);
                token = dc.getMethod("getPhysicalDisplayToken", long.class).invoke(null, ids[0]);
                Log.i(TAG, "panel token via DisplayControl");
            }
            panelToken = token;
            }
            sc.getMethod("setDisplayPowerMode", android.os.IBinder.class, int.class).invoke(null, token, POWER_MODE_OFF);
            if (!panelLogged) { Log.i(TAG, "physical panel powered off"); panelLogged = true; }
        } catch (Throwable t) { Log.w(TAG, "panelOff failed: " + t + " cause=" + t.getCause()); }
    }

    // 'S' bytes arrive from the sender once a second while the CarPlay session is streaming.
    private static volatile long lastStreamingMs = 0;

    /**
     * Panel policy: OFF while the sender says it is streaming (re-asserted every 300 ms so the power button cannot
     * bring it back), ON again 3 s after the heartbeat stops (cable pulled, radio off, session failed) so the phone is
     * usable when it is not projecting.
     */
    private static void startPanelSupervisor() {
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                boolean off = false;
                for (;;) {
                    boolean streaming = System.currentTimeMillis() - lastStreamingMs < 3000;
                    if (streaming) { panelOff(); off = true; }
                    else if (off) { panelOn(); off = false; Log.i(TAG, "no streaming heartbeat: panel back on"); }
                    try { Thread.sleep(300); } catch (InterruptedException e) { return; }
                }
            }
        }, "s10-panel");
        t.setDaemon(true);
        t.start();
    }

    private static void panelOn() {
        try {
            Object tk = panelToken;
            if (tk != null) Class.forName("android.view.SurfaceControl").getMethod("setDisplayPowerMode", android.os.IBinder.class, int.class).invoke(null, tk, POWER_MODE_NORMAL);
        } catch (Throwable ignored) { }
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
                            else if (b == 'S') lastStreamingMs = System.currentTimeMillis();
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
