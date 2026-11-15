package com.s10caros.launcher.app;

import android.app.ActivityOptions;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.os.IBinder;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * Floating Back / Home pill on the projected display. The head unit cannot send Back/Home, and a normal app cannot
 * inject key events, so Back is sent to the privileged injector (InputBridge InjectMain --listen) over loopback.
 * Needs SYSTEM_ALERT_WINDOW (granted with `appops set ... SYSTEM_ALERT_WINDOW allow` in this bring-up build).
 * Extras: "display" (int id, optional; else the display named "s10caros"), "port" (injector port, default 47600).
 */
public final class NavOverlayService extends Service {
    private static final String TAG = "S10CarOS-NAV";
    private WindowManager wm;
    private View bar;

    @Override public int onStartCommand(Intent i, int flags, int id) {
        int want = i == null ? -1 : i.getIntExtra("display", -1);
        final int port = i == null ? 47600 : i.getIntExtra("port", 47600);
        DisplayManager dm = getSystemService(DisplayManager.class);
        Display target = null;
        for (Display dsp : dm.getDisplays()) if (dsp.getDisplayId() == want || (want < 0 && "s10caros".equals(dsp.getName()))) target = dsp;
        if (target == null) { Log.w(TAG, "no projected display, nothing to overlay"); stopSelf(); return START_NOT_STICKY; }
        final int displayId = target.getDisplayId();
        Context ctx = createDisplayContext(target).createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
        wm = ctx.getSystemService(WindowManager.class);
        if (bar != null) { try { wm.removeView(bar); } catch (RuntimeException ignored) { } }
        final float d = ctx.getResources().getDisplayMetrics().density;

        LinearLayout pill = new LinearLayout(ctx);
        pill.setOrientation(LinearLayout.HORIZONTAL);
        pill.setGravity(Gravity.CENTER);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(225, 14, 22, 38)); bg.setCornerRadius(34 * d); bg.setStroke(Math.round(1.5f * d), Color.argb(255, 38, 50, 74));
        pill.setBackground(bg);
        pill.setPadding(Math.round(10 * d), Math.round(6 * d), Math.round(10 * d), Math.round(6 * d));
        pill.addView(button(ctx, d, "◀", "ATRÁS", new Runnable() { @Override public void run() { sendKey(port, 4); } }));
        pill.addView(button(ctx, d, "●", "INICIO", new Runnable() { @Override public void run() { goHome(displayId); } }));
        bar = pill;

        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        lp.y = Math.round(12 * d);
        try { wm.addView(bar, lp); Log.i(TAG, "nav bar on display " + displayId); }
        catch (RuntimeException e) { Log.e(TAG, "addView failed: " + e); stopSelf(); }
        return START_STICKY;
    }

    private View button(Context ctx, float d, String glyph, String label, final Runnable action) {
        LinearLayout b = new LinearLayout(ctx);
        b.setOrientation(LinearLayout.HORIZONTAL);
        b.setGravity(Gravity.CENTER);
        b.setPadding(Math.round(22 * d), 0, Math.round(26 * d), 0);
        GradientDrawable n = new GradientDrawable(); n.setColor(Color.TRANSPARENT); n.setCornerRadius(28 * d);
        GradientDrawable p = new GradientDrawable(); p.setColor(Color.argb(255, 38, 56, 90)); p.setCornerRadius(28 * d);
        android.graphics.drawable.StateListDrawable s = new android.graphics.drawable.StateListDrawable();
        s.addState(new int[] {android.R.attr.state_pressed}, p); s.addState(new int[] {}, n);
        b.setBackground(s);
        b.setClickable(true);
        TextView g = new TextView(ctx); g.setText(glyph); g.setTextColor(Color.parseColor("#4FD1C5")); g.setTextSize(24);
        TextView t = new TextView(ctx); t.setText(label); t.setTextColor(Color.parseColor("#F2F6FF")); t.setTextSize(15);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL)); t.setLetterSpacing(0.12f); t.setPadding(Math.round(10 * d), 0, 0, 0);
        b.addView(g); b.addView(t);
        b.setMinimumHeight(Math.round(60 * d));
        b.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { action.run(); } });
        return b;
    }

    private void goHome(int displayId) {
        Intent h = new Intent(this, LauncherActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(h, ActivityOptions.makeBasic().setLaunchDisplayId(displayId).toBundle());
    }

    private void sendKey(final int port, final int code) {
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                try (Socket s = new Socket()) {
                    s.connect(new InetSocketAddress("127.0.0.1", port), 500);
                    OutputStream o = s.getOutputStream();
                    o.write(("K " + code + "\n").getBytes());
                    o.flush();
                } catch (Exception e) { Log.w(TAG, "key " + code + " not sent: " + e); }
            }
        }, "s10-nav-key");
        t.start();
    }

    @Override public void onDestroy() { if (bar != null && wm != null) { try { wm.removeView(bar); } catch (RuntimeException ignored) { } } super.onDestroy(); }
    @Override public IBinder onBind(Intent i) { return null; }
}
