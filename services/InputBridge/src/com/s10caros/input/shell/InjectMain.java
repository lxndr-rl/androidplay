package com.s10caros.input.shell;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import com.s10caros.input.core.TouchMapper;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;

/**
 * Touch injector run as a privileged process (adb shell uid in tests; a platform/system service in the ROM).
 * Needs android.permission.INJECT_EVENTS, which ordinary apps cannot hold.
 *
 *   CLASSPATH=inject.dex app_process / com.s10caros.input.shell.InjectMain --display ID --size W H < events
 *
 * stdin lines: "D x y" | "M x y" | "U x y" | "S ms" (sleep) . Hidden-API reflection: may break on other Android versions.
 */
public final class InjectMain {
    public static void main(String[] args) throws Exception {
        int display = 0, w = 0, h = 0;
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("--display")) display = Integer.parseInt(args[++i]);
            else if (args[i].equals("--size")) { w = Integer.parseInt(args[++i]); h = Integer.parseInt(args[++i]); }
        }
        if (w <= 0 || h <= 0) { System.err.println("usage: --display ID --size W H"); System.exit(2); }
        Injector inj = new Injector();
        TouchMapper mapper = new TouchMapper(w, h);
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        String line;
        int sent = 0, dropped = 0;
        while ((line = in.readLine()) != null) {
            String[] p = line.trim().split("\\s+");
            if (p.length == 0 || p[0].isEmpty()) continue;
            if (p[0].equals("S")) { Thread.sleep(Long.parseLong(p[1])); continue; }
            if (p.length < 3) { dropped++; continue; }
            TouchMapper.Out o = mapper.map(p[0].charAt(0), Integer.parseInt(p[1]), Integer.parseInt(p[2]), SystemClock.uptimeMillis());
            if (o == null) { dropped++; continue; }
            boolean ok = inj.inject(o, display);
            if (ok) sent++; else dropped++;
        }
        TouchMapper.Out r = mapper.release(SystemClock.uptimeMillis());
        if (r != null && inj.inject(r, display)) sent++;
        System.out.println("INJECT sent=" + sent + " dropped=" + dropped + " display=" + display);
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
