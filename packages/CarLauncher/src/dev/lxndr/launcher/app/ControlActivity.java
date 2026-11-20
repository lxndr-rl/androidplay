package dev.lxndr.launcher.app;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * TEST AID: the phone's own screen for the USB mode manager (state, arm / ADB, test flags). Talks to the manager's control server on 127.0.0.1:47650
 * (services/Control, enabled by `scripts/deploy-phone.sh --ctl`) with the token packaged in the APK's assets. It is not part of what ships.
 */
public final class ControlActivity extends Activity {
    private static final int PORT = 47650;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService net = Executors.newSingleThreadExecutor();
    private String token = "";
    private boolean updating, resumed;
    private TextView stateView, statusView, linkView, logView, answer;
    private Switch car, mic, secure, learn, home;
    private EditText radio;
    private float d;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        d = getResources().getDisplayMetrics().density;
        try (InputStream in = getAssets().open("ctl.token")) { token = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8)).readLine(); }
        catch (Exception e) { token = ""; }
        setContentView(build());
    }

    private int dp(float v) { return Math.round(v * d); }

    private TextView text(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this); t.setText(s); t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp); t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private Switch toggle(String label, final String flag) {
        Switch s = new Switch(this); s.setText(label); s.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16); s.setTextColor(Color.WHITE);
        s.setPadding(0, dp(10), 0, dp(10));
        s.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton v, boolean on) { if (!updating) send("SET " + flag + " " + (on ? "on" : "off")); }
        });
        return s;
    }

    private Button button(String label, final String cmd) {
        Button x = new Button(this); x.setText(label); x.setAllCaps(false);
        x.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { send(cmd); } });
        return x;
    }

    private View build() {
        ScrollView sv = new ScrollView(this); sv.setBackgroundColor(Color.rgb(16, 16, 16));
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(dp(18), dp(24), dp(18), dp(24));
        sv.addView(root);
        root.addView(text("AndroidPlay Control", 22, Color.WHITE, true));
        root.addView(text("Pantalla de pruebas del gestor USB (no forma parte de lo que se entrega).", 12, Color.GRAY, false));
        stateView = text("...", 28, Color.GRAY, true); stateView.setPadding(0, dp(18), 0, dp(4)); root.addView(stateView);
        statusView = text("", 14, Color.LTGRAY, false); root.addView(statusView);
        linkView = text("", 12, Color.GRAY, false); linkView.setTypeface(Typeface.MONOSPACE); root.addView(linkView);

        LinearLayout row = new LinearLayout(this); row.setOrientation(LinearLayout.HORIZONTAL); row.setPadding(0, dp(14), 0, dp(6));
        Button arm = button("Armar ahora", "MODE arm"), adb = button("Modo ADB", "MODE adb");
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        row.addView(arm, lp); row.addView(adb, lp); root.addView(row);
        answer = text("", 12, Color.rgb(120, 200, 255), false); root.addView(answer);

        car = toggle("Armado siempre, aunque haya Wi-Fi de casa", "car"); root.addView(car);
        secure = toggle("Ventanas seguras (Spotify, bancos)", "secure"); root.addView(secure);
        mic = toggle("Pedir el micrófono de la radio", "mic"); root.addView(mic);
        learn = toggle("Modo aprendizaje de la radio", "learn"); root.addView(learn);
        home = toggle("Forzar ADB (como en casa)", "home"); root.addView(home);

        LinearLayout r2 = new LinearLayout(this); r2.setOrientation(LinearLayout.HORIZONTAL); r2.setGravity(Gravity.CENTER_VERTICAL); r2.setPadding(0, dp(8), 0, dp(8));
        radio = new EditText(this); radio.setHint("radio (p. ej. chevrolet)"); radio.setTextColor(Color.WHITE); radio.setHintTextColor(Color.GRAY); radio.setSingleLine(true);
        Button save = new Button(this); save.setText("Guardar"); save.setAllCaps(false);
        save.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) {
            String n = radio.getText().toString().trim(); if (!n.isEmpty()) send("RADIO " + n); } });
        r2.addView(radio, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1)); r2.addView(save); root.addView(r2);

        root.addView(text("Últimos eventos del gestor", 13, Color.GRAY, true));
        logView = text("", 11, Color.LTGRAY, false); logView.setTypeface(Typeface.MONOSPACE); root.addView(logView);
        return sv;
    }

    @Override protected void onResume() { super.onResume(); resumed = true; tick.run(); }
    @Override protected void onPause() { resumed = false; ui.removeCallbacks(tick); super.onPause(); }
    @Override protected void onDestroy() { net.shutdownNow(); super.onDestroy(); }

    private final Runnable tick = new Runnable() { @Override public void run() { if (!resumed) return; refresh(); ui.postDelayed(this, 2000); } };

    /** One request per connection, off the main thread. */
    private String request(String cmd) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1", PORT), 1500); s.setSoTimeout(3000);
            s.getOutputStream().write((token + "\n" + cmd + "\n").getBytes(StandardCharsets.UTF_8)); s.getOutputStream().flush();
            StringBuilder b = new StringBuilder(); BufferedReader r = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8)); String l;
            while ((l = r.readLine()) != null) b.append(l).append('\n');
            return b.toString().trim();
        } catch (Exception e) { return null; }
    }

    private void send(final String cmd) {
        net.execute(new Runnable() { @Override public void run() {
            final String r = request(cmd);
            ui.post(new Runnable() { @Override public void run() { answer.setText(r == null ? "sin respuesta del gestor" : r); } });
            ui.postDelayed(new Runnable() { @Override public void run() { refresh(); } }, 1500);
        } });
    }

    private void refresh() {
        net.execute(new Runnable() { @Override public void run() {
            final String r = request("STATUS");
            ui.post(new Runnable() { @Override public void run() { show(r); } });
        } });
    }

    private void show(String r) {
        if (r == null || r.startsWith("error")) {
            stateView.setText(r == null ? "Sin conexión con el gestor" : "Clave rechazada"); stateView.setTextColor(Color.rgb(230, 80, 80));
            statusView.setText(r == null ? "¿Está activo el servidor de pruebas? scripts/deploy-phone.sh --ctl" : r); return;
        }
        Map<String, String> m = new HashMap<String, String>(); StringBuilder log = new StringBuilder();
        for (String l : r.split("\n")) {
            int i = l.indexOf('='); if (i < 0) continue;
            String k = l.substring(0, i), v = l.substring(i + 1);
            if (k.equals("log")) log.append(v).append('\n'); else m.put(k, v);
        }
        String st = m.get("state");
        if ("carplay".equals(st)) { stateView.setText("ARMADO: esperando la radio"); stateView.setTextColor(Color.rgb(60, 200, 110)); }
        else if ("adb".equals(st)) { stateView.setText("ADB (no armado)"); stateView.setTextColor(Color.rgb(240, 170, 50)); }
        else { stateView.setText("Estado: " + st); stateView.setTextColor(Color.LTGRAY); }
        statusView.setText(nz(m.get("status")));
        linkView.setText(nz(m.get("link")));
        logView.setText(log.toString().trim());
        updating = true;
        car.setChecked(b(m, "car")); secure.setChecked(b(m, "secure")); mic.setChecked(b(m, "mic")); learn.setChecked(b(m, "learn")); home.setChecked(b(m, "home"));
        if (!radio.hasFocus() && !nz(m.get("radio")).isEmpty()) radio.setText(m.get("radio"));
        updating = false;
    }

    private static boolean b(Map<String, String> m, String k) { return "true".equals(m.get(k)); }
    private static String nz(String s) { return s == null ? "" : s; }
}
