package com.s10caros.launcher.app;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.Outline;
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
import android.view.ViewOutlineProvider;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextClock;
import android.widget.TextView;
import com.s10caros.launcher.core.AppFilter;
import com.s10caros.launcher.core.ModeConfig;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Car home screen modelled on the CarPlay layout: a slim sidebar (clock, date, link status) and a paged grid of large
 * rounded app icons with the name underneath, page dots at the bottom. Only apps worth having in a car are listed:
 * everything the user installed, plus a short allow-list of system apps (config.properties apps.pinned goes first,
 * apps.hidden removes). Programmatic UI, dp-based, dark theme, one accent colour, touch targets >= 120 dp.
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
            "org.smarttube.stable", "com.xuper.netxxus", "org.lineageos.twelve", "org.lineageos.jelly", "com.android.settings");
    private static final int COLS = 4, ROWS = 2;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private LinearLayout dots;
    private HorizontalScrollView pager;
    private int pageCount = 1, pageWidth = 1;
    private float d;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { status.setText(Status.line); handler.postDelayed(this, 1500); }
    };

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
        root.addView(sidebar(), new LinearLayout.LayoutParams(dp(176), ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout main = new LinearLayout(this);
        main.setOrientation(LinearLayout.VERTICAL);
        main.setPadding(dp(8), dp(18), dp(24), dp(40));   // the Back/Home capsule floats over the (empty) middle of the sidebar
        pager = new HorizontalScrollView(this);
        pager.setHorizontalScrollBarEnabled(false);
        pager.setOverScrollMode(View.OVER_SCROLL_NEVER);
        LinearLayout pages = new LinearLayout(this);
        pages.setOrientation(LinearLayout.HORIZONTAL);
        pager.addView(pages, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT));
        main.addView(pager, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        dots = new LinearLayout(this);
        dots.setGravity(Gravity.CENTER);
        main.addView(dots, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(22)));
        root.addView(main, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        setContentView(root);

        final LinearLayout pagesRef = pages;
        main.post(new Runnable() { @Override public void run() { build(pagesRef); } });
        // snap to the nearest page when the finger lifts
        pager.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent ev) {
                if (ev.getActionMasked() == MotionEvent.ACTION_UP || ev.getActionMasked() == MotionEvent.ACTION_CANCEL)
                    v.postDelayed(new Runnable() { @Override public void run() { snap(); } }, 40);
                return false;
            }
        });
    }

    private void snap() {
        if (pageWidth <= 0) return;
        int page = Math.max(0, Math.min(pageCount - 1, Math.round(pager.getScrollX() / (float) pageWidth)));
        pager.smoothScrollTo(page * pageWidth, 0);
        paintDots(page);
    }

    private View sidebar() {
        LinearLayout s = new LinearLayout(this);
        s.setOrientation(LinearLayout.VERTICAL);
        s.setBackgroundColor(SIDEBAR);
        s.setPadding(dp(20), dp(26), dp(14), dp(20));
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
        View grow = new View(this);
        s.addView(grow, new LinearLayout.LayoutParams(1, 0, 1f));
        status = new TextView(this);   // kept for the refresh loop; deliberately not shown (debug text is not for the driver)
        status.setVisibility(View.GONE);
        s.addView(status);
        return s;
    }

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
        int perPage = COLS * ROWS;
        pageCount = Math.max(1, (apps.size() + perPage - 1) / perPage);
        for (int p = 0; p < pageCount; p++) {
            LinearLayout page = new LinearLayout(this);
            page.setOrientation(LinearLayout.VERTICAL);
            page.setGravity(Gravity.CENTER_VERTICAL);
            for (int r = 0; r < ROWS; r++) {
                LinearLayout row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                for (int col = 0; col < COLS; col++) {
                    int i = p * perPage + r * COLS + col;
                    View cell = i < apps.size() ? cell(apps.get(i), icons.get(apps.get(i).pkg)) : new View(this);
                    row.addView(cell, new LinearLayout.LayoutParams(pageWidth / COLS, pageHeight / ROWS));
                }
                page.addView(row);
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
        box.addView(iv, new FrameLayout.LayoutParams(dp(84), dp(84), Gravity.CENTER));
        cell.addView(box, new LinearLayout.LayoutParams(dp(132), dp(132)));

        TextView label = new TextView(this);
        label.setText(e.label);
        label.setTextColor(TEXT);
        label.setTextSize(19);
        label.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        label.setGravity(Gravity.CENTER);
        label.setSingleLine(true);
        label.setEllipsize(android.text.TextUtils.TruncateAt.END);
        label.setPadding(dp(6), dp(10), dp(6), 0);
        cell.addView(label, new LinearLayout.LayoutParams(dp(220), ViewGroup.LayoutParams.WRAP_CONTENT));

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

    @Override protected void onResume() { super.onResume(); handler.post(refresh); }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
    @Override public void onBackPressed() { /* home screen: nowhere to go back to */ }
}
