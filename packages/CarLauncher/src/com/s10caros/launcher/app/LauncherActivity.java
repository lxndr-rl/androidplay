package com.s10caros.launcher.app;

import android.Manifest;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.Outline;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.telephony.TelephonyManager;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextClock;
import android.widget.TextView;
import com.s10caros.launcher.core.AppFilter;
import com.s10caros.launcher.core.CardText;
import com.s10caros.launcher.core.ModeConfig;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Car home screen. Left rail: clock, date, link status (signal / Wi-Fi / battery); the Back/Home capsule floats over
 * its middle. Right: three information cards on top (weather, speed from the phone's GPS, now playing) and one row of
 * large app tiles below it (paged). Every card has a calm placeholder when its data is missing (no internet, no GPS
 * fix, nothing playing). Programmatic UI, dp-based, dark theme, one accent colour, touch targets >= 120 dp.
 * Runtime permissions (location, phone state) and the notification-listener access for media are granted over adb:
 * there is no dialog to answer on the car screen.
 */
public final class LauncherActivity extends Activity {
    private static final int BG_TOP = Color.parseColor("#0B1322"), BG_BOTTOM = Color.parseColor("#03060C");
    private static final int SIDEBAR = Color.parseColor("#0A101C"), TILE = Color.parseColor("#182236"), TILE_PRESSED = Color.parseColor("#2A3D63");
    private static final int STROKE = Color.parseColor("#26324A"), ACCENT = Color.parseColor("#4FD1C5");
    private static final int TEXT = Color.parseColor("#F2F6FF"), MUTED = Color.parseColor("#8FA3C7");
    /** System apps that make sense in a car; user-installed apps are always shown. */
    private static final Set<String> SYSTEM_ALLOW = new HashSet<>(Arrays.asList(
            "com.android.settings", "org.lineageos.jelly", "org.lineageos.twelve", "com.android.chrome",
            "com.google.android.apps.maps", "com.google.android.youtube", "com.google.android.apps.youtube.music"));
    private static final List<String> DEFAULT_ORDER = Arrays.asList(
            "com.waze", "com.google.android.apps.maps", "com.spotify.music", "org.smarttube.stable", "com.xuper.netxxus",
            "org.lineageos.twelve", "org.lineageos.jelly", "com.android.settings");
    private static final int PER_PAGE = 5;
    private static final String PREFS = "cards";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private LinearLayout dots;
    private HorizontalScrollView pager;
    private int pageCount = 1, pageWidth = 1;
    private float d;

    private TextView railSignal, railNet, railBattery;
    private SignalBars cellBars, wifiBars;
    private TextView wxIcon, wxTemp, wxText, wxSub;
    private TextView speedNum, speedSub;
    private TextView npTitle, npArtist, npPlay;
    private View npControls;
    private MediaController media;
    private Location lastLoc;
    private long lastWxTry;

    private int dp(float v) { return Math.round(v * d); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        d = getResources().getDisplayMetrics().density;
        ConnectionService.start(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        // On the projected display the head unit has no Back/Home: show the floating bar (this activity is in the
        // foreground, which is what lets an app start the overlay service).
        android.view.Display shown = getDisplay();
        if (shown != null && "s10caros".equals(shown.getName()))
            startService(new Intent(this, NavOverlayService.class).putExtra("display", shown.getDisplayId()));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {BG_TOP, BG_BOTTOM}));
        root.addView(rail(), new LinearLayout.LayoutParams(dp(176), ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout main = new LinearLayout(this);
        main.setOrientation(LinearLayout.VERTICAL);
        main.setPadding(dp(8), dp(22), dp(24), dp(14));
        LinearLayout cards = new LinearLayout(this);
        cards.setOrientation(LinearLayout.HORIZONTAL);
        cards.addView(weatherCard(), cardLp(1.15f, 0));
        cards.addView(speedCard(), cardLp(1f, dp(16)));
        cards.addView(nowPlayingCard(), cardLp(1.45f, dp(16)));
        main.addView(cards, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 0.43f));

        pager = new HorizontalScrollView(this);
        pager.setHorizontalScrollBarEnabled(false);
        pager.setOverScrollMode(View.OVER_SCROLL_NEVER);
        LinearLayout pages = new LinearLayout(this);
        pages.setOrientation(LinearLayout.HORIZONTAL);
        pager.addView(pages, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT));
        LinearLayout.LayoutParams plp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 0.57f);
        plp.topMargin = dp(8);
        main.addView(pager, plp);
        dots = new LinearLayout(this);
        dots.setGravity(Gravity.CENTER);
        main.addView(dots, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(18)));
        root.addView(main, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        setContentView(root);

        final LinearLayout pagesRef = pages;
        main.post(new Runnable() { @Override public void run() { build(pagesRef); } });
        pager.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent ev) {
                if (ev.getActionMasked() == MotionEvent.ACTION_UP || ev.getActionMasked() == MotionEvent.ACTION_CANCEL)
                    v.postDelayed(new Runnable() { @Override public void run() { snap(); } }, 40);
                return false;
            }
        });
        showCachedWeather();
        showNoSpeed();
        showNoMedia();
    }

    // ---------------------------------------------------------------- layout helpers

    private LinearLayout.LayoutParams cardLp(float weight, int leftMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, weight);
        lp.leftMargin = leftMargin;
        return lp;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(22), dp(16), dp(22), dp(16));
        GradientDrawable g = new GradientDrawable();
        g.setColor(TILE); g.setStroke(dp(1), STROKE); g.setCornerRadius(dp(26));
        c.setBackground(g);
        return c;
    }

    private TextView tv(String t, float sp, int color, String family) {
        TextView v = new TextView(this);
        v.setText(t); v.setTextSize(sp); v.setTextColor(color);
        v.setTypeface(Typeface.create(family, Typeface.NORMAL));
        v.setSingleLine(true);
        v.setEllipsize(android.text.TextUtils.TruncateAt.END);
        return v;
    }

    private View grow() { return new View(this); }

    private void snap() {
        if (pageWidth <= 0) return;
        int page = Math.max(0, Math.min(pageCount - 1, Math.round(pager.getScrollX() / (float) pageWidth)));
        pager.smoothScrollTo(page * pageWidth, 0);
        paintDots(page);
    }

    // ---------------------------------------------------------------- left rail

    private View rail() {
        LinearLayout s = new LinearLayout(this);
        s.setOrientation(LinearLayout.VERTICAL);
        s.setBackgroundColor(SIDEBAR);
        s.setPadding(dp(20), dp(26), dp(14), dp(18));
        TextClock clock = new TextClock(this);
        clock.setFormat24Hour("HH:mm"); clock.setFormat12Hour("HH:mm");   // always 24 h
        clock.setTextSize(44); clock.setTextColor(TEXT);
        clock.setTypeface(Typeface.create("sans-serif-light", Typeface.NORMAL));
        s.addView(clock);
        TextClock day = new TextClock(this);
        day.setFormat24Hour("EEEE"); day.setFormat12Hour("EEEE");
        day.setTextSize(18); day.setTextColor(TEXT);
        day.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        s.addView(day);
        TextClock date = new TextClock(this);
        date.setFormat24Hour("d 'de' MMMM"); date.setFormat12Hour("d 'de' MMMM");
        date.setTextSize(15); date.setTextColor(MUTED);
        s.addView(date);
        s.addView(grow(), new LinearLayout.LayoutParams(1, 0, 1f));   // the Back/Home capsule floats here
        railSignal = tv("", 15, TEXT, "sans-serif-medium");
        railNet = tv("", 15, TEXT, "sans-serif-medium");
        railBattery = tv("", 14, MUTED, "sans-serif");
        cellBars = new SignalBars(this, TEXT, STROKE);
        wifiBars = new SignalBars(this, TEXT, STROKE);
        s.addView(railRow(cellBars, railSignal));
        s.addView(railRow(wifiBars, railNet));
        s.addView(railBattery);
        return s;
    }

    private View railRow(SignalBars b, TextView label) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(3), 0, dp(3));
        row.addView(b, new LinearLayout.LayoutParams(dp(26), dp(18)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = dp(10);
        row.addView(label, lp);
        return row;
    }

    private static String bars(int level) {      // 0..4
        String[] b = {"▁", "▃", "▅", "▇"};
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 4; i++) sb.append(i < level ? b[i] : "·");
        return sb.toString();
    }

    private void updateRail() {
        ConnectivityManager cm = getSystemService(ConnectivityManager.class);

        
        // The car link (eth0 to the radio) is never the internet: look for the best real uplink
        boolean wifi = false, cell = false;
        for (Network x : cm.getAllNetworks()) {
            NetworkCapabilities c = cm.getNetworkCapabilities(x);
            if (c == null || !c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) continue;
            if (c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) wifi = true;
            if (c.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) cell = true;
        }
        boolean hasPhone = checkSelfPermission(Manifest.permission.READ_PHONE_STATE) == PackageManager.PERMISSION_GRANTED;
        String cellText = null;
        int cellLvl = 0, wifiLvl = 0;
        if (hasPhone) {
            try {
                TelephonyManager tm = getSystemService(TelephonyManager.class);
                cellLvl = tm.getSignalStrength() == null ? 0 : tm.getSignalStrength().getLevel();
                String type = netType(tm.getDataNetworkType());
                if (tm.getSimState() == TelephonyManager.SIM_STATE_READY || cell) cellText = type.isEmpty() ? "M\u00F3vil" : type;
            } catch (SecurityException e) { /* not granted yet */ }
        }
        String wifiText = null;
        if (wifi) {
            WifiInfo wi = getSystemService(WifiManager.class).getConnectionInfo();
            wifiLvl = wi == null ? 0 : WifiManager.calculateSignalLevel(wi.getRssi(), 5);
            String ssid = wi == null ? null : wi.getSSID();     // needs location permission + location on, else "<unknown ssid>"
            if (ssid != null && ssid.length() > 1 && ssid.startsWith("\"") && ssid.endsWith("\"")) ssid = ssid.substring(1, ssid.length() - 1);
            wifiText = ssid == null || ssid.isEmpty() || ssid.startsWith("<unknown") ? "Wi\u2011Fi" : ssid;
        }
        cellBars.setLevel(cellText != null ? cellLvl : 0);
        wifiBars.setLevel(wifiText != null ? wifiLvl : 0);
        railSignal.setText(cellText != null ? cellText : "Sin red m\u00F3vil");
        railNet.setText(wifiText != null ? wifiText : (cell ? "Datos m\u00F3viles" : "Sin Wi\u2011Fi"));
        Intent bat = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (bat != null) {
            int lvl = bat.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = bat.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            int st = bat.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            boolean chg = st == BatteryManager.BATTERY_STATUS_CHARGING || st == BatteryManager.BATTERY_STATUS_FULL;
            railBattery.setText("Teléfono " + (lvl * 100 / Math.max(1, scale)) + " %" + (chg ? " ⚡" : ""));
        }
    }

    private static String netType(int t) {
        switch (t) {
            case TelephonyManager.NETWORK_TYPE_NR: return "5G";
            case TelephonyManager.NETWORK_TYPE_LTE: return "4G";
            case TelephonyManager.NETWORK_TYPE_HSPAP: case TelephonyManager.NETWORK_TYPE_HSPA:
            case TelephonyManager.NETWORK_TYPE_HSDPA: case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_UMTS: return "3G";
            case TelephonyManager.NETWORK_TYPE_EDGE: case TelephonyManager.NETWORK_TYPE_GPRS: return "2G";
            default: return "";
        }
    }

    // ---------------------------------------------------------------- weather card

    private View weatherCard() {
        LinearLayout c = card();
        c.addView(tv("CLIMA", 13, MUTED, "sans-serif-medium"));
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        wxIcon = tv("☁️", 52, TEXT, "sans-serif");
        wxTemp = tv("--", 58, TEXT, "sans-serif-light");
        row.addView(wxIcon);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = dp(14);
        row.addView(wxTemp, lp);
        c.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        wxText = tv("", 20, TEXT, "sans-serif-medium");
        wxSub = tv("", 14, MUTED, "sans-serif");
        c.addView(wxText); c.addView(wxSub);
        return c;
    }

    private void showWeather(int code, double t, double hi, double lo, long when) {
        wxIcon.setText(CardText.weatherIcon(code));
        wxTemp.setText(CardText.temp(t));
        wxText.setText(CardText.weatherText(code));
        wxSub.setText("Máx " + CardText.temp(hi) + "  Mín " + CardText.temp(lo) + "  ·  " + CardText.age(System.currentTimeMillis() - when));
    }

    private void showCachedWeather() {
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        long when = p.getLong("wx_when", 0);
        if (when > 0 && System.currentTimeMillis() - when < 6 * 3600_000L)
            showWeather(p.getInt("wx_code", 3), p.getFloat("wx_t", 0), p.getFloat("wx_hi", 0), p.getFloat("wx_lo", 0), when);
        else {
            wxIcon.setText("☁️"); wxTemp.setText("--");
            wxText.setText("Sin conexión");
            wxSub.setText("El clima aparece cuando haya internet");
        }
    }

    /** One short HTTPS request to Open-Meteo (no key); position = last GPS/network fix. Off the UI thread, at most every 10 min. */
    private void maybeFetchWeather() {
        final Location l = lastLoc;
        long now = System.currentTimeMillis();
        if (l == null || now - lastWxTry < 10 * 60_000L) return;
        lastWxTry = now;
        new Thread(new Runnable() {
            @Override public void run() {
                HttpURLConnection h = null;
                try {
                    String u = String.format(Locale.US, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
                            + "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
                            l.getLatitude(), l.getLongitude());
                    h = (HttpURLConnection) new URL(u).openConnection();
                    h.setConnectTimeout(6000); h.setReadTimeout(8000);
                    if (h.getResponseCode() != 200) { lastWxTry = System.currentTimeMillis() - 9 * 60_000L; return; }   // retry in a minute
                    StringBuilder sb = new StringBuilder();
                    try (BufferedReader r = new BufferedReader(new InputStreamReader(h.getInputStream()))) { String ln; while ((ln = r.readLine()) != null) sb.append(ln); }
                    org.json.JSONObject o = new org.json.JSONObject(sb.toString());
                    final org.json.JSONObject cur = o.getJSONObject("current"), day = o.getJSONObject("daily");
                    final int code = cur.getInt("weather_code");
                    final double t = cur.getDouble("temperature_2m"), hi = day.getJSONArray("temperature_2m_max").getDouble(0), lo = day.getJSONArray("temperature_2m_min").getDouble(0);
                    final long when = System.currentTimeMillis();
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putLong("wx_when", when).putInt("wx_code", code)
                            .putFloat("wx_t", (float) t).putFloat("wx_hi", (float) hi).putFloat("wx_lo", (float) lo).apply();
                    handler.post(new Runnable() { @Override public void run() { showWeather(code, t, hi, lo, when); } });
                } catch (Exception e) {
                    lastWxTry = System.currentTimeMillis() - 9 * 60_000L;       // offline: try again in about a minute
                } finally { if (h != null) h.disconnect(); }
            }
        }, "weather").start();
    }

    // ---------------------------------------------------------------- speed card

    private View speedCard() {
        LinearLayout c = card();
        c.addView(tv("VELOCIDAD", 13, MUTED, "sans-serif-medium"));
        speedNum = tv("--", 84, TEXT, "sans-serif-light");
        speedNum.setGravity(Gravity.CENTER);
        c.addView(speedNum, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        speedSub = tv("km/h", 16, MUTED, "sans-serif-medium");
        speedSub.setGravity(Gravity.CENTER);
        c.addView(speedSub, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return c;
    }

    private void showNoSpeed() { speedNum.setText("--"); speedSub.setText("Buscando GPS…"); }

    private final LocationListener gps = new LocationListener() {
        @Override public void onLocationChanged(Location l) {
            lastLoc = l;
            if (l.hasSpeed()) { speedNum.setText(String.valueOf(CardText.kmh(l.getSpeed()))); speedSub.setText("km/h"); }
            else showNoSpeed();
            maybeFetchWeather();
        }
        @Override public void onProviderEnabled(String p) {}
        @Override public void onProviderDisabled(String p) { showNoSpeed(); }
    };

    private void startGps() {
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            speedSub.setText("Sin permiso de ubicación");
            return;
        }
        try {
            LocationManager lm = getSystemService(LocationManager.class);
            lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, gps, Looper.getMainLooper());
            Location last = lm.getLastKnownLocation(LocationManager.GPS_PROVIDER);
            if (last == null) last = lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
            if (last != null && lastLoc == null) { lastLoc = last; maybeFetchWeather(); }
        } catch (SecurityException | IllegalArgumentException e) { speedSub.setText("GPS no disponible"); }
    }

    // ---------------------------------------------------------------- now playing card

    private View nowPlayingCard() {
        LinearLayout c = card();
        c.addView(tv("AHORA SUENA", 13, MUTED, "sans-serif-medium"));
        npTitle = tv("", 24, TEXT, "sans-serif-medium");
        npArtist = tv("", 17, MUTED, "sans-serif");
        LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        LinearLayout text = new LinearLayout(this);
        text.setOrientation(LinearLayout.VERTICAL);
        text.setGravity(Gravity.CENTER_VERTICAL);
        text.addView(npTitle); text.addView(npArtist);
        c.addView(text, tl);
        LinearLayout ctl = new LinearLayout(this);
        ctl.setOrientation(LinearLayout.HORIZONTAL);
        ctl.setGravity(Gravity.CENTER);
        ctl.addView(ctlButton("⏮", new Runnable() { @Override public void run() { if (media != null) media.getTransportControls().skipToPrevious(); } }));
        npPlay = ctlButton("▶", new Runnable() { @Override public void run() {
            if (media == null) return;
            PlaybackState ps = media.getPlaybackState();
            if (ps != null && ps.getState() == PlaybackState.STATE_PLAYING) media.getTransportControls().pause(); else media.getTransportControls().play();
        } });
        ctl.addView(npPlay);
        ctl.addView(ctlButton("⏭", new Runnable() { @Override public void run() { if (media != null) media.getTransportControls().skipToNext(); } }));
        npControls = ctl;
        c.addView(ctl, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(68)));
        return c;
    }

    private TextView ctlButton(String glyph, final Runnable action) {
        TextView t = new TextView(this);
        t.setText(glyph); t.setTextSize(30); t.setTextColor(TEXT); t.setGravity(Gravity.CENTER);
        GradientDrawable g = new GradientDrawable();
        g.setShape(GradientDrawable.OVAL); g.setColor(TILE_PRESSED);
        t.setBackground(g);
        t.setClickable(true);
        t.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { action.run(); } });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(64), dp(64));
        lp.leftMargin = dp(14); lp.rightMargin = dp(14);
        t.setLayoutParams(lp);
        return t;
    }

    private void showNoMedia() {
        npTitle.setText("Nada reproduciéndose");
        npArtist.setText("Abre Spotify o tu música");
        npControls.setVisibility(View.INVISIBLE);
    }

    private final MediaController.Callback mediaCb = new MediaController.Callback() {
        @Override public void onMetadataChanged(MediaMetadata m) { showMedia(); }
        @Override public void onPlaybackStateChanged(PlaybackState s) { showMedia(); }
    };

    private void showMedia() {
        if (media == null) { showNoMedia(); return; }
        MediaMetadata m = media.getMetadata();
        String title = m == null ? null : m.getString(MediaMetadata.METADATA_KEY_TITLE);
        String artist = m == null ? null : m.getString(MediaMetadata.METADATA_KEY_ARTIST);
        if (title == null || title.isEmpty()) { showNoMedia(); return; }
        npTitle.setText(title);
        npArtist.setText(artist == null ? "" : artist);
        npControls.setVisibility(View.VISIBLE);
        PlaybackState ps = media.getPlaybackState();
        npPlay.setText(ps != null && ps.getState() == PlaybackState.STATE_PLAYING ? "⏸" : "▶");
    }

    private final MediaSessionManager.OnActiveSessionsChangedListener sessions = new MediaSessionManager.OnActiveSessionsChangedListener() {
        @Override public void onActiveSessionsChanged(List<MediaController> list) { pickSession(list); }
    };

    private void pickSession(List<MediaController> list) {
        if (media != null) media.unregisterCallback(mediaCb);
        media = null;
        if (list != null) for (MediaController c : list) { media = c; break; }   // the first one is the most recently active
        if (media != null) media.registerCallback(mediaCb);
        showMedia();
    }

    private void startMedia() {
        try {
            MediaSessionManager msm = getSystemService(MediaSessionManager.class);
            ComponentName cn = new ComponentName(this, MediaListener.class);
            msm.addOnActiveSessionsChangedListener(sessions, cn);
            pickSession(msm.getActiveSessions(cn));
        } catch (SecurityException e) { showNoMedia(); }   // notification-listener access not granted
    }

    // ---------------------------------------------------------------- app row

    private ModeConfig config() {
        File f = new File(getFilesDir(), "config.properties");
        if (!f.exists()) return ModeConfig.defaults();
        try (FileInputStream in = new FileInputStream(f)) { return ModeConfig.load(in); } catch (Exception e) { return ModeConfig.defaults(); }
    }

    private void build(LinearLayout pages) {
        PackageManager pm = getPackageManager();
        Intent q = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<AppFilter.Entry> all = new ArrayList<>();
        final java.util.Map<String, Drawable> icons = new java.util.HashMap<>();
        for (ResolveInfo ri : pm.queryIntentActivities(q, 0)) {
            ApplicationInfo ai = ri.activityInfo.applicationInfo;
            boolean system = (ai.flags & (ApplicationInfo.FLAG_SYSTEM | ApplicationInfo.FLAG_UPDATED_SYSTEM_APP)) != 0;
            if (system && !SYSTEM_ALLOW.contains(ri.activityInfo.packageName)) continue;   // recorder, camera, contacts... not for a car
            all.add(new AppFilter.Entry(ri.activityInfo.packageName, String.valueOf(ri.loadLabel(pm))));
            if (!icons.containsKey(ri.activityInfo.packageName)) icons.put(ri.activityInfo.packageName, ri.loadIcon(pm));
        }
        ModeConfig c = config();
        List<String> order = c.pinned.isEmpty() ? DEFAULT_ORDER : c.pinned;
        List<AppFilter.Entry> apps = AppFilter.arrange(all, order, c.hidden, getPackageName());

        pageWidth = pager.getWidth();
        int pageHeight = pager.getHeight();
        pageCount = Math.max(1, (apps.size() + PER_PAGE - 1) / PER_PAGE);
        for (int p = 0; p < pageCount; p++) {
            LinearLayout page = new LinearLayout(this);
            page.setOrientation(LinearLayout.HORIZONTAL);
            page.setGravity(Gravity.CENTER_VERTICAL);
            for (int col = 0; col < PER_PAGE; col++) {
                int i = p * PER_PAGE + col;
                View cell = i < apps.size() ? cell(apps.get(i), icons.get(apps.get(i).pkg)) : new View(this);
                page.addView(cell, new LinearLayout.LayoutParams(pageWidth / PER_PAGE, ViewGroup.LayoutParams.MATCH_PARENT));
            }
            pages.addView(page, new LinearLayout.LayoutParams(pageWidth, pageHeight));
        }
        paintDots(0);
    }

    private void paintDots(int active) {
        dots.removeAllViews();
        if (pageCount <= 1) return;
        for (int i = 0; i < pageCount; i++) {
            View dot = new View(this);
            GradientDrawable g = new GradientDrawable();
            g.setShape(GradientDrawable.OVAL);
            g.setColor(i == active ? TEXT : STROKE);
            dot.setBackground(g);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(9), dp(9));
            lp.setMargins(dp(6), 0, dp(6), 0);
            dots.addView(dot, lp);
        }
    }

    private View cell(final AppFilter.Entry e, Drawable icon) {
        LinearLayout cell = new LinearLayout(this);
        cell.setOrientation(LinearLayout.VERTICAL);
        cell.setGravity(Gravity.CENTER);
        cell.setClickable(true);

        FrameLayout box = new FrameLayout(this);
        box.setBackground(tileBackground());
        box.setClipToOutline(true);
        final float r = dp(30);
        box.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View v, Outline o) { o.setRoundRect(0, 0, v.getWidth(), v.getHeight(), r); }
        });
        ImageView iv = new ImageView(this);
        iv.setImageDrawable(icon);
        box.addView(iv, new FrameLayout.LayoutParams(dp(88), dp(88), Gravity.CENTER));
        cell.addView(box, new LinearLayout.LayoutParams(dp(136), dp(136)));

        TextView label = new TextView(this);
        label.setText(e.label);
        label.setTextColor(TEXT);
        label.setTextSize(19);
        label.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setGravity(Gravity.CENTER);
        label.setSingleLine(true);
        label.setEllipsize(android.text.TextUtils.TruncateAt.END);
        label.setPadding(dp(4), dp(10), dp(4), 0);
        cell.addView(label, new LinearLayout.LayoutParams(dp(170), ViewGroup.LayoutParams.WRAP_CONTENT));

        cell.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent ev) {
                int a = ev.getActionMasked();
                if (a == MotionEvent.ACTION_DOWN) v.animate().scaleX(0.94f).scaleY(0.94f).setDuration(80).start();
                else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) v.animate().scaleX(1f).scaleY(1f).setDuration(120).start();
                return false;
            }
        });
        cell.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                Intent i = getPackageManager().getLaunchIntentForPackage(e.pkg);
                if (i != null) startActivity(i);
            }
        });
        return cell;
    }

    private Drawable tileBackground() {
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(TILE); normal.setStroke(dp(1), STROKE);
        GradientDrawable pressed = new GradientDrawable();
        pressed.setColor(TILE_PRESSED); pressed.setStroke(dp(2), ACCENT);
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] {android.R.attr.state_pressed}, pressed);
        s.addState(new int[] {}, normal);
        return s;
    }

    // ---------------------------------------------------------------- lifecycle

    private final Runnable tick = new Runnable() {
        @Override public void run() { updateRail(); maybeFetchWeather(); handler.postDelayed(this, 3000); }
    };

    @Override protected void onResume() {
        super.onResume();
        startGps();
        startMedia();
        handler.post(tick);
    }

    @Override protected void onPause() {
        handler.removeCallbacks(tick);
        try { getSystemService(LocationManager.class).removeUpdates(gps); } catch (RuntimeException e) { /* nothing registered */ }
        try { getSystemService(MediaSessionManager.class).removeOnActiveSessionsChangedListener(sessions); } catch (RuntimeException e) { /* idem */ }
        if (media != null) { media.unregisterCallback(mediaCb); media = null; }
        super.onPause();
    }

    @Override public void onBackPressed() { /* home screen: nowhere to go back to */ }
}
