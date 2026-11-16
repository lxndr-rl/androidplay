import com.s10caros.input.core.GestureFilter;
import com.s10caros.input.core.GestureFilter.Ev;
import java.util.ArrayList;
import java.util.List;

public class GestureTests {
    static int fails = 0;
    static void check(boolean c, String w) { if (!c) { System.out.println("FAIL " + w); fails++; } }
    static String s(List<Ev> l) { StringBuilder b = new StringBuilder(); for (Ev e : l) b.append(e.kind).append(e.kind == 'G' ? "" + e.x : "").append(' '); return b.toString().trim(); }
    static String run(GestureFilter f, Object... steps) {
        List<Ev> all = new ArrayList<>();
        for (int i = 0; i < steps.length; i += 3) all.addAll(f.feed((Character) steps[i], (Integer) steps[i + 1], (Integer) steps[i + 2]));
        return s(all);
    }
    public static void main(String[] a) {
        GestureFilter f = new GestureFilter(1280, 720);
        check(run(f, 'D', 600, 300, 'M', 610, 300, 'U', 610, 300).equals("D M U"), "touch in the middle passes through");
        check(run(f, 'D', 5, 300, 'M', 60, 300, 'M', 200, 305, 'M', 260, 305, 'U', 260, 305).equals("G4"), "left edge swipe = back, rest swallowed");
        check(run(f, 'D', 1275, 300, 'M', 1200, 300, 'M', 1100, 300, 'U', 1100, 300).equals("G4"), "right edge swipe = back");
        check(run(f, 'D', 5, 300, 'U', 5, 300).equals("D U"), "tap on the edge is replayed");
        check(run(f, 'D', 5, 300, 'M', 10, 500, 'M', 12, 600, 'U', 12, 600).equals("D M M U"), "vertical drag along the edge stays a drag");
        check(run(f, 'D', 5, 300, 'M', 20, 300, 'U', 20, 300).equals("D M U"), "short edge move is a normal drag");
        check(run(f, 'D', 640, 715, 'M', 640, 600, 'U', 640, 600).equals("G3"), "bottom swipe up = home");
        check(run(f, 'D', 640, 715, 'M', 640, 500, 'M', 640, 300, 'U', 640, 300).equals("G187"), "long bottom swipe = recents");
        check(run(f, 'D', 640, 715, 'M', 640, 700, 'U', 640, 700).equals("D M U"), "tiny bottom move is not a gesture");
        check(run(f, 'D', 100, 715, 'M', 400, 690, 'U', 500, 690).equals("D M U"), "sideways drag at the bottom is not home");
        check(run(f, 'D', 640, 715, 'M', 640, 600, 'M', 640, 715, 'U', 640, 715).equals("D M M U"), "bottom swipe going back down is not home");
        check(run(f, 'M', 1, 1, 'U', 1, 1).equals(""), "move/up without down dropped");
        f.feed('D', 5, 300); f.reset();
        check(run(f, 'D', 600, 300, 'U', 600, 300).equals("D U"), "reset clears a held touch");
        boolean threw = false; try { new GestureFilter(0, 5); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "bad size rejected");
        if (fails > 0) { System.out.println(fails + " FAILED"); System.exit(1); }
        System.out.println("ALL PASSED");
    }
}
