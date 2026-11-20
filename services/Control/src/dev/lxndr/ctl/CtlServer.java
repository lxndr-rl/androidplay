package dev.lxndr.ctl;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

/**
 * TEST AID, off unless /data/local/tmp/androidplay.ctl exists: lets the AndroidPlay Control screen on the phone read the manager's state and
 * change its mode without adb. Runs as root (started by androidplayd.sh), listens on 127.0.0.1 only and wants the shared token (file
 * androidplay.token, also packaged in the app) on the first line of every request. Any app that can read that token could switch USB modes, which
 * is acceptable on a test phone and is why this is not part of a release image.
 *
 * Request (one per connection):  line 1 = token, line 2 = command
 *   STATUS | SET <car|mic|secure|learn|home> <on|off> | MODE <arm|adb> | RADIO <name>
 * Reply: text, key=value lines for STATUS, "ok ..." or "error ..." otherwise.
 */
public final class CtlServer {
    private static final String D = System.getProperty("ctl.dir", "/data/local/tmp");   // the property is for the host test

    public static void main(String[] a) throws Exception {
        final int port = a.length > 0 ? Integer.parseInt(a[0]) : 47650;
        final String token = a.length > 1 ? read(new File(a[1])).trim() : "";
        if (token.length() < 8) { System.err.println("no token: refusing to serve"); System.exit(2); }
        try (ServerSocket srv = new ServerSocket(port, 4, InetAddress.getByName("127.0.0.1"))) {
            for (;;) {
                final Socket s = srv.accept();
                new Thread(new Runnable() { @Override public void run() { handle(s, token); } }).start();
            }
        }
    }

    private static void handle(Socket s, String token) {
        try (Socket sk = s) {
            sk.setSoTimeout(3000);
            BufferedReader in = new BufferedReader(new InputStreamReader(sk.getInputStream(), StandardCharsets.UTF_8));
            String given = in.readLine(), cmd = in.readLine();
            OutputStream out = sk.getOutputStream();
            String reply;
            if (given == null || cmd == null || !same(token, given.trim())) reply = "error unauthorized";
            else reply = run(cmd.trim());
            out.write((reply + "\n").getBytes(StandardCharsets.UTF_8));
            out.flush();
        } catch (Exception ignored) { }
    }

    private static boolean same(String a, String b) {
        return MessageDigest.isEqual(a.getBytes(StandardCharsets.UTF_8), b.getBytes(StandardCharsets.UTF_8));
    }

    static String run(String cmd) {
        String[] p = cmd.split("\\s+");
        switch (p[0]) {
            case "STATUS": return status();
            case "SET":
                if (p.length != 3) return "error usage: SET <car|mic|secure|learn|home> <on|off>";
                return set(p[1], p[2].equals("on"));
            case "MODE":
                if (p.length != 2 || !(p[1].equals("arm") || p[1].equals("adb"))) return "error usage: MODE <arm|adb>";
                write("androidplay.cmd", p[1]);
                return "ok queued " + p[1] + " (the manager acts within a second)";
            case "RADIO":
                if (p.length != 2 || !p[1].matches("[A-Za-z0-9_-]{1,24}")) return "error usage: RADIO <name>";
                write("androidplay.radio", p[1]);
                return "ok radio " + p[1];
            default: return "error unknown command";
        }
    }

    private static String set(String what, boolean on) {
        switch (what) {
            case "car": return flag("androidplay.car", on);
            case "mic": return flag("androidplay.mic", on);
            case "learn": return flag("androidplay.learn", on);
            case "home": return flag("androidplay.home", on);
            case "secure": return flag("androidplay.nosecure", !on);   // secure windows are on unless the opt-out file exists
            default: return "error unknown flag " + what;
        }
    }

    private static String flag(String name, boolean present) {
        File f = new File(D, name);
        try {
            if (present) { if (!f.exists()) new FileOutputStream(f).close(); f.setReadable(true, false); f.setWritable(true, false); }
            else if (f.exists() && !f.delete()) return "error cannot remove " + name;
            return "ok " + name + (present ? " set" : " cleared");
        } catch (Exception e) { return "error " + e; }
    }

    private static void write(String name, String text) {
        try (FileOutputStream o = new FileOutputStream(new File(D, name))) { o.write((text + "\n").getBytes(StandardCharsets.UTF_8)); }
        catch (Exception ignored) { }
    }

    private static boolean has(String name) { return new File(D, name).exists(); }

    private static String read(File f) {
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            StringBuilder b = new StringBuilder(); String l;
            while ((l = r.readLine()) != null) b.append(l).append('\n');
            return b.toString();
        } catch (Exception e) { return ""; }
    }

    private static String status() {
        StringBuilder b = new StringBuilder();
        b.append("state=").append(read(new File(D, "androidplayd.state")).trim()).append('\n');
        b.append("status=").append(read(new File(D, "androidplayd.status")).trim()).append('\n');
        b.append("car=").append(has("androidplay.car")).append('\n');
        b.append("mic=").append(has("androidplay.mic")).append('\n');
        b.append("secure=").append(!has("androidplay.nosecure")).append('\n');
        b.append("learn=").append(has("androidplay.learn")).append('\n');
        b.append("home=").append(has("androidplay.home")).append('\n');
        b.append("off=").append(has("androidplay.off")).append('\n');
        b.append("radio=").append(read(new File(D, "androidplay.radio")).trim()).append('\n');
        b.append("reply=").append(read(new File(D, "androidplay.reply")).trim()).append('\n');
        b.append("learn_idx=").append(read(new File(D, "androidplay.learn.idx")).trim()).append('\n');
        // the last link line and the last few interesting manager lines
        List<String> keep = new ArrayList<String>(); String link = "";
        List<String> all = tail(new File(D, "androidplayd.log"), 600);
        for (String l : all) {
            if (l.contains("link:")) link = l;
            if (l.contains("alive:") || l.contains("trace reader") || l.startsWith("mkdir") || l.startsWith("write ") || l.startsWith("symlink") || l.startsWith("unlink") || l.startsWith("rmdir")) continue;
            if (l.contains("link:")) continue;
            keep.add(l);
        }
        b.append("link=").append(link).append('\n');
        for (int i = Math.max(0, keep.size() - 8); i < keep.size(); i++) b.append("log=").append(keep.get(i)).append('\n');
        return b.toString().trim();
    }

    private static List<String> tail(File f, int n) {
        List<String> ring = new ArrayList<String>();
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String l;
            while ((l = r.readLine()) != null) { ring.add(l); if (ring.size() > n) ring.remove(0); }
        } catch (Exception ignored) { }
        return ring;
    }
}
