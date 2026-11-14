import com.s10caros.launcher.core.*;
import com.s10caros.launcher.core.ConnectionPolicy.Action;
import com.s10caros.launcher.core.ConnectionPolicy.Event;
import com.s10caros.launcher.core.ConnectionPolicy.State;
import java.io.ByteArrayInputStream;
import java.util.*;

public class CoreTests {
    static int fails = 0;
    static void check(boolean c, String what) { if (!c) { System.out.println("FAIL " + what); fails++; } }

    public static void main(String[] a) throws Exception {
        // ---- ConnectionPolicy ----
        ConnectionPolicy p = new ConnectionPolicy(true, 500, 4000, 10_000);
        check(p.state() == State.WAIT_USB, "starts waiting for USB");
        check(p.on(Event.TICK, 0) == Action.NONE, "tick without usb does nothing");
        check(p.on(Event.USB_ATTACHED, 100) == Action.START_SESSION && p.state() == State.CONNECTING, "attach starts session");
        check(p.on(Event.SESSION_LOST, 200) == Action.STOP_SESSION && p.state() == State.BACKOFF, "early loss -> backoff");
        check(p.retryInMs(200) == 500, "first backoff 500");
        check(p.on(Event.TICK, 600) == Action.NONE, "not due yet");
        check(p.on(Event.TICK, 700) == Action.START_SESSION, "retry when due");
        check(p.on(Event.SESSION_LOST, 800) == Action.STOP_SESSION && p.retryInMs(800) == 1000, "backoff doubles");
        check(p.on(Event.TICK, 1800) == Action.START_SESSION, "second retry");
        p.on(Event.SESSION_LOST, 1900); check(p.retryInMs(1900) == 2000, "doubles again");
        p.on(Event.TICK, 3900); p.on(Event.SESSION_LOST, 4000);
        check(p.retryInMs(4000) == 4000, "capped at max");
        p.on(Event.TICK, 8000); p.on(Event.SESSION_LOST, 8100);
        check(p.retryInMs(8100) == 4000, "stays at cap");
        // stable streaming resets backoff
        p.on(Event.TICK, 12100); p.on(Event.SESSION_READY, 12200);
        check(p.state() == State.STREAMING, "streaming");
        check(p.on(Event.SESSION_LOST, 30_000) == Action.STOP_SESSION && p.retryInMs(30_000) == 500, "stable stream resets backoff");
        // unstable streaming does not reset
        p.on(Event.TICK, 30_500); p.on(Event.SESSION_READY, 30_600);
        p.on(Event.SESSION_LOST, 31_000);
        check(p.retryInMs(31_000) == 1000, "short stream keeps growing backoff");
        // cable pulled while active: stop and wait; replug starts fresh
        p.on(Event.TICK, 32_000);
        check(p.on(Event.USB_DETACHED, 32_100) == Action.STOP_SESSION && p.state() == State.WAIT_USB, "detach stops");
        check(p.on(Event.SESSION_LOST, 32_200) == Action.NONE, "late loss event ignored");
        check(p.on(Event.USB_ATTACHED, 40_000) == Action.START_SESSION && p.attempts() == 1, "replug restarts with reset attempts");
        // loss after cable gone must not schedule a retry
        p.on(Event.SESSION_READY, 40_100);
        p.on(Event.USB_DETACHED, 40_200);
        check(p.state() == State.WAIT_USB && p.retryInMs(40_300) == -1, "no retry without usb");
        // config mode never auto-connects
        ConnectionPolicy c = new ConnectionPolicy(false, 500, 4000, 0);
        check(c.on(Event.USB_ATTACHED, 0) == Action.NONE && c.state() == State.IDLE, "config mode is inert");
        boolean threw = false; try { new ConnectionPolicy(true, 0, 10, 0); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "rejects bad parameters");

        // ---- ModeConfig ----
        ModeConfig d = ModeConfig.defaults();
        check(d.valid() && d.mode == ModeConfig.Mode.CONFIG && d.reconnectInitialMs == 500 && d.reconnectMaxMs == 30_000, "safe defaults");
        ModeConfig v = ModeConfig.load(new ByteArrayInputStream("mode=vehicle\nreconnect.initial_ms=250\napps.pinned=a.b, c.d ,a.b\n".getBytes()));
        check(v.valid() && v.mode == ModeConfig.Mode.VEHICLE && v.reconnectInitialMs == 250 && v.pinned.equals(Arrays.asList("a.b", "c.d")), "parses vehicle config");
        ModeConfig bad = ModeConfig.load(new ByteArrayInputStream("mode=turbo\nreconnect.initial_ms=5\nstable_ms=x\nwat=1\nreconnect.max_ms=100\nreconnect.initial_ms=200\n".getBytes()));
        check(!bad.valid() && bad.errors.size() >= 3, "reports invalid values: " + bad.errors);
        check(bad.mode == ModeConfig.Mode.CONFIG, "invalid mode falls back to config (safe)");
        boolean immutable = false; try { d.pinned.add("x"); } catch (UnsupportedOperationException e) { immutable = true; }
        check(immutable, "lists immutable");

        // ---- AppFilter ----
        List<AppFilter.Entry> apps = Arrays.asList(
            new AppFilter.Entry("org.maps", "Maps"), new AppFilter.Entry("org.music", "music"),
            new AppFilter.Entry("com.s10caros.launcher", "Launcher"), new AppFilter.Entry("org.zed", "Zed"),
            new AppFilter.Entry("org.maps", "Maps dup"), new AppFilter.Entry("org.secret", "Secret"));
        List<AppFilter.Entry> out = AppFilter.arrange(apps, Arrays.asList("org.zed", "org.absent", "org.maps"), Arrays.asList("org.secret"), "com.s10caros.launcher");
        List<String> pk = new ArrayList<>(); for (AppFilter.Entry e : out) pk.add(e.pkg);
        check(pk.equals(Arrays.asList("org.zed", "org.maps", "org.music")), "pinned first, rest sorted, hidden/self/dup removed: " + pk);

        if (fails > 0) { System.out.println(fails + " FAILED"); System.exit(1); }
        System.out.println("ALL PASSED");
    }
}
