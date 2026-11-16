package dev.lxndr.launcher.core;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Orders launcher tiles: pinned first (in configured order), then the rest alphabetically; hidden and self excluded. */
public final class AppFilter {
    public static final class Entry {
        public final String pkg, label;
        public Entry(String pkg, String label) { this.pkg = pkg; this.label = label; }
    }

    private AppFilter() {}

    public static List<Entry> arrange(List<Entry> all, List<String> pinned, List<String> hidden, String selfPkg) {
        Set<String> hide = new HashSet<>(hidden);
        hide.add(selfPkg);
        List<Entry> visible = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (Entry e : all) if (!hide.contains(e.pkg) && seen.add(e.pkg)) visible.add(e);   // de-duplicate by package
        List<Entry> out = new ArrayList<>();
        for (String p : pinned) for (Entry e : visible) if (e.pkg.equals(p)) { out.add(e); break; }
        List<Entry> rest = new ArrayList<>();
        for (Entry e : visible) if (!out.contains(e)) rest.add(e);
        rest.sort(Comparator.comparing((Entry e) -> e.label.toLowerCase()).thenComparing(e -> e.pkg));
        out.addAll(rest);
        return out;
    }
}
