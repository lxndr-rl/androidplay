package com.s10caros.launcher.core;

/**
 * Reconnect policy for the car session. Pure logic: no Android, no clock, no threads. The caller feeds events with
 * a monotonic timestamp and executes the returned Action.
 *
 * Vehicle mode retries forever with capped exponential backoff and never gives up; config mode never auto-connects.
 */
public final class ConnectionPolicy {
    public enum State { IDLE, WAIT_USB, CONNECTING, STREAMING, BACKOFF }
    public enum Event { USB_ATTACHED, USB_DETACHED, SESSION_READY, SESSION_LOST, TICK }
    public enum Action { NONE, START_SESSION, STOP_SESSION }

    private final long initialMs, maxMs, stableMs;
    private final boolean autoConnect;
    private State state = State.IDLE;
    private boolean usbPresent;
    private long retryAtMs;
    private long nextDelayMs;
    private long streamingSinceMs;
    private int attempts;

    public ConnectionPolicy(boolean autoConnect, long initialMs, long maxMs, long stableMs) {
        if (initialMs <= 0 || maxMs < initialMs || stableMs < 0) throw new IllegalArgumentException("bad backoff parameters");
        this.autoConnect = autoConnect;
        this.initialMs = initialMs;
        this.maxMs = maxMs;
        this.stableMs = stableMs;
        this.nextDelayMs = initialMs;
        this.state = autoConnect ? State.WAIT_USB : State.IDLE;
    }

    public State state() { return state; }
    public int attempts() { return attempts; }
    /** Milliseconds until the next retry is due, or -1 when no retry is pending. */
    public long retryInMs(long nowMs) { return state == State.BACKOFF ? Math.max(0, retryAtMs - nowMs) : -1; }

    public Action on(Event e, long nowMs) {
        if (!autoConnect) return Action.NONE;
        switch (e) {
            case USB_ATTACHED:
                usbPresent = true;
                if (state == State.WAIT_USB || state == State.BACKOFF) return begin();
                return Action.NONE;
            case USB_DETACHED:
                usbPresent = false;
                boolean wasActive = state == State.CONNECTING || state == State.STREAMING;
                state = State.WAIT_USB;
                if (wasActive) { nextDelayMs = initialMs; attempts = 0; return Action.STOP_SESSION; }
                return Action.NONE;
            case SESSION_READY:
                if (state == State.CONNECTING) { state = State.STREAMING; streamingSinceMs = nowMs; }
                return Action.NONE;
            case SESSION_LOST:
                if (state != State.CONNECTING && state != State.STREAMING) return Action.NONE;
                if (state == State.STREAMING && nowMs - streamingSinceMs >= stableMs) { nextDelayMs = initialMs; attempts = 0; }
                if (!usbPresent) { state = State.WAIT_USB; return Action.STOP_SESSION; }
                state = State.BACKOFF;
                retryAtMs = nowMs + nextDelayMs;
                nextDelayMs = Math.min(maxMs, nextDelayMs * 2);
                return Action.STOP_SESSION;
            case TICK:
                if (state == State.BACKOFF && usbPresent && nowMs >= retryAtMs) return begin();
                return Action.NONE;
            default:
                return Action.NONE;
        }
    }

    private Action begin() {
        state = State.CONNECTING;
        attempts++;
        return Action.START_SESSION;
    }
}
