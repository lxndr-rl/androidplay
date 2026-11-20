import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

/** Host test of the control protocol: token, status keys, flags, mode queue, input validation. No phone. */
public class CtlTests {
    static int fails = 0;
    static void check(String name, boolean ok) { if (!ok) { System.out.println("FAIL " + name); fails++; } }
    static String ask(int port, String token, String cmd) throws Exception {
        try (Socket s = new Socket("127.0.0.1", port)) {
            s.getOutputStream().write((token + "\n" + cmd + "\n").getBytes(StandardCharsets.UTF_8)); s.getOutputStream().flush();
            return new String(s.getInputStream().readAllBytes(), StandardCharsets.UTF_8).trim();
        }
    }
    public static void main(String[] a) throws Exception {
        final Path dir = Files.createTempDirectory("ctl");
        System.setProperty("ctl.dir", dir.toString());
        Path tok = dir.resolve("androidplay.token"); Files.writeString(tok, "0123456789abcdef\n");
        Files.writeString(dir.resolve("androidplayd.state"), "carplay\n");
        Files.writeString(dir.resolve("androidplayd.status"), "12:00:00 ARMED: waiting for the radio\n");
        Files.writeString(dir.resolve("androidplayd.log"), "1.0 start\n2.0 alive: x\n3.0 link: partner=y udc=default\n4.0 state CARPLAY (armed at start)\n");
        final int port; try (ServerSocket t = new ServerSocket(0)) { port = t.getLocalPort(); }
        Thread th = new Thread(() -> { try { dev.lxndr.ctl.CtlServer.main(new String[]{String.valueOf(port), tok.toString()}); } catch (Exception e) { } });
        th.setDaemon(true); th.start();
        for (int i = 0; i < 50; i++) { try (Socket s = new Socket("127.0.0.1", port)) { break; } catch (IOException e) { Thread.sleep(40); } }
        String K = "0123456789abcdef";

        check("wrong token refused", ask(port, "wrong", "STATUS").startsWith("error unauthorized"));
        check("empty token refused", ask(port, "", "STATUS").startsWith("error unauthorized"));
        String st = ask(port, K, "STATUS");
        check("status state", st.contains("state=carplay"));
        check("status text", st.contains("status=12:00:00 ARMED"));
        check("status link", st.contains("link=3.0 link: partner=y udc=default"));
        check("status log hides alive and link lines", st.contains("log=4.0 state CARPLAY") && !st.contains("log=2.0 alive") && !st.contains("log=3.0 link"));
        check("secure defaults to on", st.contains("secure=true"));
        check("car off initially", st.contains("car=false"));

        check("set car on", ask(port, K, "SET car on").startsWith("ok") && Files.exists(dir.resolve("androidplay.car")));
        check("set car off", ask(port, K, "SET car off").startsWith("ok") && !Files.exists(dir.resolve("androidplay.car")));
        check("set mic on", ask(port, K, "SET mic on").startsWith("ok") && Files.exists(dir.resolve("androidplay.mic")));
        check("secure off creates the opt-out file", ask(port, K, "SET secure off").startsWith("ok") && Files.exists(dir.resolve("androidplay.nosecure")));
        check("status secure=false", ask(port, K, "STATUS").contains("secure=false"));
        check("secure on removes it", ask(port, K, "SET secure on").startsWith("ok") && !Files.exists(dir.resolve("androidplay.nosecure")));
        check("kill switch cannot be set from the UI", ask(port, K, "SET off on").startsWith("error") && !Files.exists(dir.resolve("androidplay.off")));
        check("learn on", ask(port, K, "SET learn on").startsWith("ok") && Files.exists(dir.resolve("androidplay.learn")));

        check("mode arm queued", ask(port, K, "MODE arm").startsWith("ok") && Files.readString(dir.resolve("androidplay.cmd")).trim().equals("arm"));
        check("mode adb queued", ask(port, K, "MODE adb").startsWith("ok") && Files.readString(dir.resolve("androidplay.cmd")).trim().equals("adb"));
        check("bad mode refused", ask(port, K, "MODE reboot").startsWith("error"));

        check("radio name saved", ask(port, K, "RADIO chevrolet").startsWith("ok") && Files.readString(dir.resolve("androidplay.radio")).trim().equals("chevrolet"));
        check("radio name with shell characters refused", ask(port, K, "RADIO a;rm").startsWith("error") && Files.readString(dir.resolve("androidplay.radio")).trim().equals("chevrolet"));
        check("unknown command", ask(port, K, "REBOOT").startsWith("error"));
        check("empty command", ask(port, K, "").startsWith("error"));

        System.out.println(fails == 0 ? "CtlTests: all passed" : "CtlTests: " + fails + " failed");
        System.exit(fails == 0 ? 0 : 1);
    }
}
