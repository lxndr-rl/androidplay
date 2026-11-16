import dev.lxndr.input.core.TouchMapper;
import dev.lxndr.input.core.TouchMapper.Out;

public class MapperTests {
    static int fails = 0;
    static void check(boolean c, String w) { if (!c) { System.out.println("FAIL " + w); fails++; } }
    public static void main(String[] a) {
        TouchMapper m = new TouchMapper(1200, 512);
        check(m.map('M', 5, 5, 0) == null, "move before down dropped");
        check(m.map('U', 5, 5, 0) == null, "up before down dropped");
        Out d = m.map('D', 100, 200, 1000);
        check(d != null && d.action == 0 && d.x == 100 && d.y == 200 && d.downTimeMs == 1000, "down");
        Out d2 = m.map('D', 110, 200, 1010);
        check(d2 != null && d2.action == 2 && d2.downTimeMs == 1000, "second down becomes move, keeps downTime");
        check(m.map('M', 110, 200, 1020) == null, "identical move dropped");
        Out mv = m.map('M', 5000, -20, 1030);
        check(mv != null && mv.x == 1199 && mv.y == 0, "clamped to display");
        Out u = m.map('U', 5000, -20, 1040);
        check(u != null && u.action == 1 && u.eventTimeMs == 1040 && u.downTimeMs == 1000 && !m.isDown(), "up");
        check(m.map('U', 1, 1, 1050) == null, "double up dropped");
        m.map('D', 10, 10, 2000);
        Out r = m.release(2100);
        check(r != null && r.action == 1 && r.x == 10 && !m.isDown() && m.release(2200) == null, "release cancels in-flight touch once");
        check(m.map('X', 1, 1, 3000) == null, "unknown kind dropped");
        boolean threw = false; try { new TouchMapper(0, 5); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "bad size rejected");
        if (fails > 0) { System.out.println(fails + " FAILED"); System.exit(1); }
        System.out.println("ALL PASSED");
    }
}
