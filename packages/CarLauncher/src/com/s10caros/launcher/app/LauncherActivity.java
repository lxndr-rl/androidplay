package com.s10caros.launcher.app;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.s10caros.launcher.core.AppFilter;
import com.s10caros.launcher.core.ModeConfig;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;

/** Landscape home screen with large touch targets. Programmatic UI only (no resources). */
public final class LauncherActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView banner;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { banner.setText(Status.line); handler.postDelayed(this, 1000); }
    };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        ConnectionService.start(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);
        banner = new TextView(this);
        banner.setTextColor(Color.LTGRAY);
        banner.setTextSize(16);
        banner.setPadding(24, 12, 24, 12);
        root.addView(banner);
        ScrollView sv = new ScrollView(this);
        GridLayout grid = new GridLayout(this);
        grid.setColumnCount(4);
        sv.addView(grid);
        root.addView(sv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
        fill(grid);
    }

    private ModeConfig config() {
        File f = new File(getFilesDir(), "config.properties");
        if (!f.exists()) return ModeConfig.defaults();
        try (FileInputStream in = new FileInputStream(f)) { return ModeConfig.load(in); } catch (Exception e) { return ModeConfig.defaults(); }
    }

    private void fill(GridLayout grid) {
        PackageManager pm = getPackageManager();
        Intent q = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<AppFilter.Entry> all = new ArrayList<>();
        for (ResolveInfo ri : pm.queryIntentActivities(q, 0))
            all.add(new AppFilter.Entry(ri.activityInfo.packageName, String.valueOf(ri.loadLabel(pm))));
        ModeConfig c = config();
        int tileW = getResources().getDisplayMetrics().widthPixels / 4;
        for (final AppFilter.Entry e : AppFilter.arrange(all, c.pinned, c.hidden, getPackageName())) {
            TextView t = new TextView(this);
            t.setText(e.label);
            t.setGravity(Gravity.CENTER);
            t.setTextSize(22);
            t.setTypeface(Typeface.DEFAULT_BOLD);
            t.setTextColor(Color.WHITE);
            t.setBackgroundColor(Color.rgb(32, 44, 60));
            t.setMinHeight(160);   // large targets for the head unit touch screen
            GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
            lp.width = tileW - 16; lp.height = ViewGroup.LayoutParams.WRAP_CONTENT; lp.setMargins(8, 8, 8, 8);
            t.setLayoutParams(lp);
            t.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    Intent i = getPackageManager().getLaunchIntentForPackage(e.pkg);
                    if (i != null) startActivity(i);
                }
            });
            grid.addView(t);
        }
    }

    @Override protected void onResume() { super.onResume(); handler.post(refresh); }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
    @Override public void onBackPressed() { /* home screen: nowhere to go back to */ }
}
