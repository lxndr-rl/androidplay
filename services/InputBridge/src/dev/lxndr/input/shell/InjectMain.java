package dev.lxndr.input.shell;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import dev.lxndr.input.core.GestureFilter;
import dev.lxndr.input.core.TouchMapper;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;

/**
 * Touch injector run as a privileged process (adb shell uid in tests; a platform/system service in the ROM).
 * Needs android.permission.INJECT_EVENTS, which ordinary apps cannot hold.
 *
 *   CLASSPATH=inject.dex app_process / dev.lxndr.input.shell.InjectMain --display ID --size W H [--listen PORT] < events
 *
 * --listen PORT also accepts lines on 127.0.0.1:PORT (one short connection per line): "K <keycode>" injects a key
 * press (down+up) on the display, e.g. "K 4" = BACK; touch lines are accepted too. Used by the launcher's Back button.
 *
 * stdin lines: "D x y" | "M x y" | "U x y" | "S ms" (sleep) . Hidden-API reflection: may break on other Android versions.
 */
public final class InjectMain {
    public static void main(String[] args) throws Exception {
        int display = 0, w = 0, h = 0, listen = 0;
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("--display")) display = Integer.parseInt(args[++i]);
            else if (args[i].equals("--size")) { w = Integer.parseInt(args[++i]); h = Integer.parseInt(args[++i]); }
            else if (args[i].equals("--listen")) listen = Integer.parseInt(args[++i]);
        }
        if (w <= 0 || h <= 0) { System.err.println("usage: --display ID --size W H"); System.exit(2); }
        final Injector inj = new Injector();
        final int disp = display;
        if (listen > 0) startListener(inj, listen, disp);
        TouchMapper mapper = new TouchMapper(w, h);
        GestureFilter gestures = new GestureFilter(w, h);
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        String line;
        int sent = 0, dropped = 0;
        while ((line = in.readLine()) != null) {
            String[] p = line.trim().split("\\s+");
            if (p.length == 0 || p[0].isEmpty()) continue;
            if (p[0].equals("S")) { Thread.sleep(Long.parseLong(p[1])); continue; }
            if (p.length < 3) { dropped++; continue; }
            for (GestureFilter.Ev ev : gestures.feed(p[0].charAt(0), Integer.parseInt(p[1]), Integer.parseInt(p[2]))) {
                if (ev.kind == 'G') { if (inj.key(ev.x, display)) sent++; else dropped++; continue; }
                TouchMapper.Out o = mapper.map(ev.kind, ev.x, ev.y, SystemClock.uptimeMillis());
                if (o == null) { dropped++; continue; }
                if (inj.inject(o, display)) sent++; else dropped++;
            }
        }
        gestures.reset();
        TouchMapper.Out r = mapper.release(SystemClock.uptimeMillis());
        if (r != null && inj.inject(r, display)) sent++;
        System.out.println("INJECT sent=" + sent + " dropped=" + dropped + " display=" + display);
    }

    private static void startListener(final Injector inj, final int port, final int display) {
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                try (java.net.ServerSocket ss = new java.net.ServerSocket(port, 8, java.net.InetAddress.getByName("127.0.0.1"))) {
                    while (true) {
                        try (java.net.Socket c = ss.accept()) {
                            c.setSoTimeout(1000);
                            BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream()));
                            String line;
                            while ((line = r.readLine()) != null) {
                                String[] p = line.trim().split("\\s+");
                                if (p.length == 2 && p[0].equals("K")) inj.key(Integer.parseInt(p[1]), display);
                            }
                        } catch (java.io.IOException e) { /* client went away or timed out: next connection */ }
                    }
                } catch (Exception e) { System.err.println("listen failed: " + e); }
            }
        }, "inject-listen");
        t.setDaemon(true);
        t.start();
    }

    private static final class Injector {
        private final Object manager;
        private final Method inject, setDisplayId;

        Injector() throws Exception {
            Class<?> c;
            Object m;
            try {   // Android 14+
                c = Class.forName("android.hardware.input.InputManagerGlobal");
                m = c.getMethod("getInstance").invoke(null);
            } catch (ReflectiveOperationException e) {
                c = Class.forName("android.hardware.input.InputManager");
                m = c.getMethod("getInstance").invoke(null);
            }
            manager = m;
            inject = c.getMethod("injectInputEvent", android.view.InputEvent.class, int.class);
            setDisplayId = android.view.InputEvent.class.getMethod("setDisplayId", int.class);
        }

        boolean key(int code, int display) {
            long now = SystemClock.uptimeMillis();
            boolean ok = true;
            for (int action = android.view.KeyEvent.ACTION_DOWN; action <= android.view.KeyEvent.ACTION_UP; action++) {
                android.view.KeyEvent k = new android.view.KeyEvent(now, now, action, code, 0);
                k.setSource(InputDevice.SOURCE_KEYBOARD);
                try { setDisplayId.invoke(k, display); ok &= (Boolean) inject.invoke(manager, k, 0); }
                catch (ReflectiveOperationException ex) { System.err.println("key failed: " + ex.getCause()); ok = false; }
            }
            return ok;
        }

        boolean inject(TouchMapper.Out o, int display) {
            MotionEvent e = MotionEvent.obtain(o.downTimeMs, o.eventTimeMs, o.action, o.x, o.y, 0);
            e.setSource(InputDevice.SOURCE_TOUCHSCREEN);
            try {
                setDisplayId.invoke(e, display);
                return (Boolean) inject.invoke(manager, e, 0);   // 0 = INJECT_INPUT_EVENT_MODE_ASYNC
            } catch (ReflectiveOperationException ex) {
                System.err.println("inject failed: " + ex.getCause());
                return false;
            } finally {
                e.recycle();
            }
        }
    }
}
