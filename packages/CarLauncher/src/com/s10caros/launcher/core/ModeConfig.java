package com.s10caros.launcher.core;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Properties;

/**
 * Device configuration (java.util.Properties format). Defaults are the safe ones: config mode, adb not exposed
 * over the network. Unknown keys and out-of-range values are reported, never silently accepted.
 */
public final class ModeConfig {
    public enum Mode { CONFIG, VEHICLE }

    public final Mode mode;
    public final long reconnectInitialMs, reconnectMaxMs, stableMs;
    public final List<String> pinned, hidden;
    public final List<String> errors;

    private ModeConfig(Mode m, long i, long x, long s, List<String> p, List<String> h, List<String> e) {
        mode = m; reconnectInitialMs = i; reconnectMaxMs = x; stableMs = s;
        pinned = Collections.unmodifiableList(p); hidden = Collections.unmodifiableList(h); errors = Collections.unmodifiableList(e);
    }

    public boolean valid() { return errors.isEmpty(); }
    public static ModeConfig defaults() { return parse(new Properties()); }

    public static ModeConfig load(InputStream in) throws IOException {
        Properties p = new Properties();
        p.load(in);
        return parse(p);
    }

    private static final List<String> KNOWN = Arrays.asList("mode", "reconnect.initial_ms", "reconnect.max_ms", "stable_ms", "apps.pinned", "apps.hidden");

    public static ModeConfig parse(Properties p) {
        List<String> err = new ArrayList<>();
        for (String k : p.stringPropertyNames()) if (!KNOWN.contains(k)) err.add("unknown key: " + k);
        Mode mode = Mode.CONFIG;
        String m = p.getProperty("mode", "config").trim();
        if (m.equals("vehicle")) mode = Mode.VEHICLE; else if (!m.equals("config")) err.add("mode must be 'config' or 'vehicle': " + m);
        long init = num(p, "reconnect.initial_ms", 500, 100, 60_000, err);
        long max = num(p, "reconnect.max_ms", 30_000, 100, 600_000, err);
        long stable = num(p, "stable_ms", 10_000, 0, 600_000, err);
        if (max < init) { err.add("reconnect.max_ms < reconnect.initial_ms"); max = init; }
        return new ModeConfig(mode, init, max, stable, list(p.getProperty("apps.pinned", "")), list(p.getProperty("apps.hidden", "")), err);
    }

    private static long num(Properties p, String k, long def, long lo, long hi, List<String> err) {
        String v = p.getProperty(k);
        if (v == null) return def;
        try {
            long x = Long.parseLong(v.trim());
            if (x < lo || x > hi) { err.add(k + " out of range [" + lo + "," + hi + "]: " + x); return def; }
            return x;
        } catch (NumberFormatException e) { err.add(k + " not a number: " + v); return def; }
    }

    private static List<String> list(String s) {
        List<String> out = new ArrayList<>();
        for (String t : s.split(",")) { t = t.trim(); if (!t.isEmpty() && !out.contains(t)) out.add(t); }
        return out;
    }
}
