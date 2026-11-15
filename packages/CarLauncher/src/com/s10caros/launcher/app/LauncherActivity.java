package com.s10caros.launcher.app;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextClock;
import android.widget.TextView;
import com.s10caros.launcher.core.AppFilter;
import com.s10caros.launcher.core.ModeConfig;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Car home screen for a 1280x720 head unit: big clock, a row of favourites, every other app in a grid.
 * Programmatic UI (no resource files). All sizes are dp so it also looks right on the phone's own display.
 * Dark theme, one accent colour, large touch targets (>= 76 dp) and press feedback; no hover/long-press UI.
 */
public final class LauncherActivity extends Activity {
    // palette
    private static final int BG_TOP = Color.parseColor("#0C1424"), BG_BOTTOM = Color.parseColor("#04070D");
    private static final int SURFACE = Color.parseColor("#172033"), SURFACE_PRESSED = Color.parseColor("#26385A");
    private static final int STROKE = Color.parseColor("#26324A"), ACCENT = Color.parseColor("#4FD1C5");
    private static final int TEXT = Color.parseColor("#F2F6FF"), MUTED = Color.parseColor("#8FA3C7");
    /** Shown first when installed and nothing is pinned in config.properties. */
    private static final List<String> PRESET_FAVOURITES = Arrays.asList(
            "org.smarttube.stable", "com.xuper.netxxus", "com.google.android.apps.youtube.music", "com.android.settings");

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private float d;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { status.setText(Status.line); handler.postDelayed(this, 1500); }
    };

    private int dp(float v) { return Math.round(v * d); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        d = getResources().getDisplayMetrics().density;
        ConnectionService.start(this);
        // On the projected display the head unit has no Back/Home: show the floating bar (the activity is in the
        // foreground here, which is what lets an app start the overlay service).
        android.view.Display shown = getDisplay();
        if (shown != null && "s10caros".equals(shown.getName()))
            startService(new Intent(this, NavOverlayService.class).putExtra("display", shown.getDisplayId()));
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {BG_TOP, BG_BOTTOM});
        root.setBackground(bg);
        root.setPadding(dp(28), dp(14), dp(28), dp(86));   // bottom room for the floating Back/Home bar

        root.addView(header());

        ScrollView sv = new ScrollView(this);
        sv.setVerticalScrollBarEnabled(false);
        sv.setOverScrollMode(View.OVER_SCROLL_NEVER);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        sv.addView(body, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(sv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
        fill(body);
    }

    private View header() {
        LinearLayout h = new LinearLayout(this);
        h.setOrientation(LinearLayout.HORIZONTAL);
        h.setGravity(Gravity.CENTER_VERTICAL);
        h.setPadding(0, 0, 0, dp(10));

        TextClock clock = new TextClock(this);
        clock.setFormat24Hour("HH:mm"); clock.setFormat12Hour("HH:mm");   // always 24 h
        clock.setTextSize(52); clock.setTextColor(TEXT);
        clock.setTypeface(Typeface.create("sans-serif-light", Typeface.NORMAL));
        h.addView(clock);

        LinearLayout dateBox = new LinearLayout(this);
        dateBox.setOrientation(LinearLayout.VERTICAL);
        dateBox.setPadding(dp(18), dp(8), 0, 0);
        TextClock day = new TextClock(this);
        day.setFormat24Hour("EEEE"); day.setFormat12Hour("EEEE");
        day.setTextSize(20); day.setTextColor(TEXT);
        day.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        TextClock date = new TextClock(this);
        date.setFormat24Hour("d 'de' MMMM"); date.setFormat12Hour("d 'de' MMMM");
        date.setTextSize(16); date.setTextColor(MUTED);
        dateBox.addView(day); dateBox.addView(date);
        h.addView(dateBox);

        View spacer = new View(this);
        h.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1f));

        status = new TextView(this);
        status.setTextColor(MUTED); status.setTextSize(14); status.setGravity(Gravity.END);
        status.setMaxWidth(dp(420));
        h.addView(status);
        return h;
    }

    private ModeConfig config() {
        File f = new File(getFilesDir(), "config.properties");
        if (!f.exists()) return ModeConfig.defaults();
        try (FileInputStream in = new FileInputStream(f)) { return ModeConfig.load(in); } catch (Exception e) { return ModeConfig.defaults(); }
    }

    private void fill(LinearLayout body) {
        PackageManager pm = getPackageManager();
        Intent q = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<AppFilter.Entry> all = new ArrayList<>();
        final java.util.Map<String, Drawable> icons = new java.util.HashMap<>();
        for (ResolveInfo ri : pm.queryIntentActivities(q, 0)) {
            all.add(new AppFilter.Entry(ri.activityInfo.packageName, String.valueOf(ri.loadLabel(pm))));
            if (!icons.containsKey(ri.activityInfo.packageName)) icons.put(ri.activityInfo.packageName, ri.loadIcon(pm));
        }
        ModeConfig c = config();
        List<String> pinned = c.pinned.isEmpty() ? PRESET_FAVOURITES : c.pinned;
        List<AppFilter.Entry> arranged = AppFilter.arrange(all, pinned, c.hidden, getPackageName());
        List<AppFilter.Entry> fav = new ArrayList<>(), rest = new ArrayList<>();
        for (AppFilter.Entry e : arranged) (pinned.contains(e.pkg) && fav.size() < 4 ? fav : rest).add(e);

        if (!fav.isEmpty()) {
            body.addView(sectionTitle("FAVORITOS"));
            GridLayout g = new GridLayout(this);
            g.setColumnCount(4);
            for (AppFilter.Entry e : fav) g.addView(tile(e, icons.get(e.pkg), true));
            body.addView(g);
        }
        if (!rest.isEmpty()) {
            body.addView(sectionTitle("TODAS LAS APPS"));
            GridLayout g = new GridLayout(this);
            g.setColumnCount(5);
            for (AppFilter.Entry e : rest) g.addView(tile(e, icons.get(e.pkg), false));
            body.addView(g);
        }
    }

    private TextView sectionTitle(String s) {
        TextView t = new TextView(this);
        t.setText(s); t.setTextColor(MUTED); t.setTextSize(13); t.setLetterSpacing(0.18f);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        t.setPadding(dp(6), dp(14), 0, dp(8));
        return t;
    }

    private View tile(final AppFilter.Entry e, Drawable icon, boolean big) {
        LinearLayout t = new LinearLayout(this);
        t.setOrientation(LinearLayout.VERTICAL);
        t.setGravity(Gravity.CENTER);
        int pad = dp(big ? 14 : 8);
        t.setPadding(pad, pad, pad, pad);
        t.setBackground(tileBackground(big));
        t.setClickable(true);
        ImageView iv = new ImageView(this);
        iv.setImageDrawable(icon);
        int sz = dp(big ? 84 : 60);
        t.addView(iv, new LinearLayout.LayoutParams(sz, sz));
        TextView label = new TextView(this);
        label.setText(e.label);
        label.setTextColor(TEXT);
        label.setTextSize(big ? 21 : 16);
        label.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setGravity(Gravity.CENTER);
        label.setSingleLine(true);
        label.setEllipsize(android.text.TextUtils.TruncateAt.END);
        label.setPadding(0, dp(big ? 10 : 6), 0, 0);
        t.addView(label, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        int screenW = getResources().getDisplayMetrics().widthPixels - dp(56);
        int cols = big ? 4 : 5, margin = dp(6);
        GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
        lp.width = screenW / cols - 2 * margin;
        lp.height = dp(big ? 164 : 112);
        lp.setMargins(margin, margin, margin, margin);
        t.setLayoutParams(lp);

        // press feedback: slight shrink; the drawable state list handles the colour
        t.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent ev) {
                int a = ev.getActionMasked();
                if (a == MotionEvent.ACTION_DOWN) v.animate().scaleX(0.96f).scaleY(0.96f).setDuration(80).start();
                else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) v.animate().scaleX(1f).scaleY(1f).setDuration(120).start();
                return false;
            }
        });
        t.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                Intent i = getPackageManager().getLaunchIntentForPackage(e.pkg);
                if (i != null) startActivity(i);
            }
        });
        return t;
    }

    private Drawable tileBackground(boolean big) {
        float r = dp(big ? 24 : 20);
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(SURFACE); normal.setCornerRadius(r); normal.setStroke(dp(1), STROKE);
        GradientDrawable pressed = new GradientDrawable();
        pressed.setColor(SURFACE_PRESSED); pressed.setCornerRadius(r); pressed.setStroke(dp(2), ACCENT);
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] {android.R.attr.state_pressed}, pressed);
        s.addState(new int[] {}, normal);
        return s;
    }

    @Override protected void onResume() { super.onResume(); handler.post(refresh); }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
    @Override public void onBackPressed() { /* home screen: nowhere to go back to */ }
}
