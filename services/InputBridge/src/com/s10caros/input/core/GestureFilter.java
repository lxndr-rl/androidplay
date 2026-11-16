package com.s10caros.input.core;

import java.util.ArrayList;
import java.util.List;

/**
 * System gestures on the projected screen, independent of what the head unit offers (no buttons needed):
 *   swipe in from the left or right edge  -> BACK
 *   swipe up from the bottom edge         -> HOME   (a long swipe, over 40 % of the height -> RECENTS)
 * Pure Java. Touches that start in an edge zone are held back until they are classified: a gesture swallows the whole
 * touch, anything else is replayed unchanged (down + buffered moves), so taps and drags at the edge still work, only
 * delayed until the finger moves or lifts. Touches that start elsewhere pass straight through.
 */
public final class GestureFilter {
    public static final int KEY_HOME = 3, KEY_BACK = 4, KEY_APP_SWITCH = 187;

    /** One output step: kind 'D'/'M'/'U' = forward this touch event, 'G' = gesture, x holds the Android keycode. */
    public static final class Ev {
        public final char kind; public final int x, y;
        Ev(char kind, int x, int y) { this.kind = kind; this.x = x; this.y = y; }
    }

    private enum Edge { NONE, LEFT, RIGHT, BOTTOM }
    private enum Phase { IDLE, PASS, HOLD, SWALLOW }

    private final int w, h, sideZone, bottomZone, sideMin, bottomHome, bottomRecents, drift;
    private Phase phase = Phase.IDLE;
    private Edge edge = Edge.NONE;
    private int x0, y0, lastX, lastY;
    private final List<Ev> held = new ArrayList<>();

    public GestureFilter(int width, int height) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("display size");
        w = width; h = height;
        sideZone = Math.max(24, w * 4 / 100);
        bottomZone = Math.max(24, h * 6 / 100);
        sideMin = Math.max(60, w * 12 / 100);
        bottomHome = Math.max(60, h * 12 / 100);
        bottomRecents = h * 40 / 100;
        drift = Math.max(40, h * 8 / 100);      // sideways wander that turns the touch into an ordinary drag
    }

    public List<Ev> feed(char kind, int x, int y) {
        List<Ev> out = new ArrayList<>();
        switch (kind) {
            case 'D': down(x, y, out); break;
            case 'M': move(x, y, out); break;
            case 'U': up(x, y, out); break;
            default: break;
        }
        return out;
    }

    /** Cable pulled mid-touch: forget the state, nothing is forwarded. */
    public void reset() { phase = Phase.IDLE; edge = Edge.NONE; held.clear(); }

    private void down(int x, int y, List<Ev> out) {
        if (phase != Phase.IDLE) { move(x, y, out); return; }       // second DOWN without UP behaves as a move
        x0 = lastX = x; y0 = lastY = y;
        edge = x < sideZone ? Edge.LEFT : x >= w - sideZone ? Edge.RIGHT : y >= h - bottomZone ? Edge.BOTTOM : Edge.NONE;
        if (edge == Edge.NONE) { phase = Phase.PASS; out.add(new Ev('D', x, y)); return; }
        phase = Phase.HOLD; held.clear(); held.add(new Ev('D', x, y));
    }

    private void move(int x, int y, List<Ev> out) {
        lastX = x; lastY = y;
        switch (phase) {
            case PASS: out.add(new Ev('M', x, y)); break;
            case HOLD:
                held.add(new Ev('M', x, y));
                int along = along(x, y), perp = perp(x, y);
                if (perp > drift || (along < -drift)) { flush(out); phase = Phase.PASS; break; }
                if (edge != Edge.BOTTOM && along >= sideMin) { held.clear(); phase = Phase.SWALLOW; out.add(new Ev('G', KEY_BACK, 0)); }
                break;
            default: break;      // IDLE (move without down) and SWALLOW: dropped
        }
    }

    private void up(int x, int y, List<Ev> out) {
        lastX = x; lastY = y;
        switch (phase) {
            case PASS: out.add(new Ev('U', x, y)); break;
            case HOLD:
                if (edge == Edge.BOTTOM) {
                    int along = along(x, y), perp = perp(x, y);
                    if (perp <= drift && along >= bottomRecents) { held.clear(); out.add(new Ev('G', KEY_APP_SWITCH, 0)); break; }
                    if (perp <= drift && along >= bottomHome) { held.clear(); out.add(new Ev('G', KEY_HOME, 0)); break; }
                }
                flush(out); out.add(new Ev('U', x, y));
                break;
            default: break;
        }
        phase = Phase.IDLE; edge = Edge.NONE; held.clear();
    }

    private void flush(List<Ev> out) { out.addAll(held); held.clear(); }

    /** Distance travelled in the gesture direction (inward for the sides, upward for the bottom). */
    private int along(int x, int y) {
        switch (edge) {
            case LEFT: return x - x0;
            case RIGHT: return x0 - x;
            default: return y0 - y;
        }
    }
    private int perp(int x, int y) { return edge == Edge.BOTTOM ? Math.abs(x - x0) : Math.abs(y - y0); }
}
