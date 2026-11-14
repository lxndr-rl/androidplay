package com.s10caros.input.core;

/**
 * Single-pointer touch state machine: CarPlay touch edges -> Android MotionEvent actions.
 * Pure Java (no Android). Rejects impossible sequences instead of forwarding them: a MOVE or UP without a prior
 * DOWN is dropped, a second DOWN without UP is turned into a MOVE, coordinates are clamped to the display.
 */
public final class TouchMapper {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2;   // = android.view.MotionEvent values

    public static final class Out {
        public final int action;
        public final float x, y;
        public final long downTimeMs, eventTimeMs;
        Out(int action, float x, float y, long downTimeMs, long eventTimeMs) {
            this.action = action; this.x = x; this.y = y; this.downTimeMs = downTimeMs; this.eventTimeMs = eventTimeMs;
        }
    }

    private final int width, height;
    private boolean down;
    private long downTimeMs;
    private float lastX, lastY;

    public TouchMapper(int width, int height) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("display size");
        this.width = width; this.height = height;
    }

    public boolean isDown() { return down; }

    /** kind: 'D' down, 'M' move, 'U' up. Returns null when the event is dropped. */
    public Out map(char kind, int x, int y, long nowMs) {
        float cx = Math.max(0, Math.min(width - 1, x)), cy = Math.max(0, Math.min(height - 1, y));
        switch (kind) {
            case 'D':
                if (down) return map('M', x, y, nowMs);
                down = true; downTimeMs = nowMs; lastX = cx; lastY = cy;
                return new Out(ACTION_DOWN, cx, cy, downTimeMs, nowMs);
            case 'M':
                if (!down) return null;
                if (cx == lastX && cy == lastY) return null;     // no-op move
                lastX = cx; lastY = cy;
                return new Out(ACTION_MOVE, cx, cy, downTimeMs, nowMs);
            case 'U':
                if (!down) return null;
                down = false;
                return new Out(ACTION_UP, cx, cy, downTimeMs, nowMs);
            default:
                return null;
        }
    }

    /** Cancel an in-flight touch (cable pulled, screen handed to the car): emits UP at the last position. */
    public Out release(long nowMs) {
        if (!down) return null;
        down = false;
        return new Out(ACTION_UP, lastX, lastY, downTimeMs, nowMs);
    }
}
