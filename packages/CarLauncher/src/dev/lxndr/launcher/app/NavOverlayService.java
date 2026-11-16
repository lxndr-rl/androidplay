package dev.lxndr.launcher.app;

import android.app.ActivityOptions;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * Back / Home controls on the projected display. The head unit cannot send Back/Home, and a normal app cannot inject
 * key events, so Back goes to the privileged injector (InputBridge InjectMain --listen) over loopback.
 *
 * Design: a slim vertical capsule on the LEFT edge, vertically centred, so it never covers the soft keyboard (bottom)
 * or the content centre. Two big round buttons with drawn icons (no text), dimmed when idle and fully visible for a
 * moment after any touch. Needs SYSTEM_ALERT_WINDOW (granted with appops in this bring-up build).
 * Extras: "display" (int id, optional; else the display named "androidplay"), "port" (injector port, default 47600).
 */
public final class NavOverlayService extends Service {
    private static final String TAG = "AndroidPlay-NAV";
    private static final float IDLE_ALPHA = 0.55f;
    private WindowManager wm;
    private View bar;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override public int onStartCommand(Intent i, int flags, int id) {
        int want = i == null ? -1 : i.getIntExtra("display", -1);
        final int port = i == null ? 47600 : i.getIntExtra("port", 47600);
        DisplayManager dm = getSystemService(DisplayManager.class);
        Display target = null;
        for (Display dsp : dm.getDisplays()) if (dsp.getDisplayId() == want || (want < 0 && "androidplay".equals(dsp.getName()))) target = dsp;
        if (target == null) { Log.w(TAG, "no projected display, nothing to overlay"); stopSelf(); return START_NOT_STICKY; }
        final int displayId = target.getDisplayId();
        Context ctx = createDisplayContext(target).createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
        wm = ctx.getSystemService(WindowManager.class);
        if (bar != null) { try { wm.removeView(bar); } catch (RuntimeException ignored) { } }
        final float d = ctx.getResources().getDisplayMetrics().density;

        final LinearLayout capsule = new LinearLayout(ctx);
        capsule.setOrientation(LinearLayout.VERTICAL);
        capsule.setGravity(Gravity.CENTER);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(230, 12, 20, 36)); bg.setCornerRadius(46 * d); bg.setStroke(Math.round(1.5f * d), Color.argb(255, 40, 54, 80));
        capsule.setBackground(bg);
        int pad = Math.round(8 * d);
        capsule.setPadding(pad, pad, pad, pad);
        capsule.addView(new IconButton(ctx, d, IconButton.BACK, new Runnable() { @Override public void run() { sendKey(port, 4); } }), lp(d));
        capsule.addView(new IconButton(ctx, d, IconButton.HOME, new Runnable() { @Override public void run() { goHome(displayId); } }), lp(d));
        capsule.setAlpha(IDLE_ALPHA);
        bar = capsule;

        // any touch on the capsule brings it to full opacity for 2.5 s
        final Runnable dim = new Runnable() { @Override public void run() { capsule.animate().alpha(IDLE_ALPHA).setDuration(400).start(); } };
        View.OnTouchListener wake = new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent e) {
                capsule.animate().cancel(); capsule.setAlpha(1f);
                ui.removeCallbacks(dim); ui.postDelayed(dim, 2500);
                return false;
            }
        };
        for (int k = 0; k < capsule.getChildCount(); k++) capsule.getChildAt(k).setOnTouchListener(wake);

        WindowManager.LayoutParams p = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        p.gravity = Gravity.START | Gravity.CENTER_VERTICAL;
        p.x = Math.round(10 * d);
        try { wm.addView(bar, p); Log.i(TAG, "nav capsule on display " + displayId); }
        catch (RuntimeException e) { Log.e(TAG, "addView failed: " + e); stopSelf(); }
        return START_STICKY;
    }

    private static LinearLayout.LayoutParams lp(float d) {
        LinearLayout.LayoutParams l = new LinearLayout.LayoutParams(Math.round(76 * d), Math.round(76 * d));
        l.setMargins(0, Math.round(5 * d), 0, Math.round(5 * d));
        return l;
    }

    /** Round button with a drawn icon: accent ring while pressed. */
    private static final class IconButton extends View {
        static final int BACK = 0, HOME = 1;
        private final int kind; private final float d; private final Runnable action;
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), ring = new Paint(Paint.ANTI_ALIAS_FLAG), ink = new Paint(Paint.ANTI_ALIAS_FLAG);

        IconButton(Context c, float d, int kind, Runnable action) {
            super(c); this.kind = kind; this.d = d; this.action = action;
            setClickable(true);
            ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(2.5f * d); ring.setColor(Color.parseColor("#4FD1C5"));
            ink.setStyle(Paint.Style.STROKE); ink.setStrokeWidth(5f * d); ink.setStrokeCap(Paint.Cap.ROUND); ink.setStrokeJoin(Paint.Join.ROUND); ink.setColor(Color.parseColor("#F2F6FF"));
            setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { IconButton.this.action.run(); } });
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight(), cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 3 * d;
            fill.setColor(isPressed() ? Color.parseColor("#2A4170") : Color.parseColor("#182236"));
            cv.drawCircle(cx, cy, r, fill);
            if (isPressed()) cv.drawCircle(cx, cy, r, ring);
            Path p = new Path();
            if (kind == BACK) {   // chevron pointing left
                p.moveTo(cx + 8 * d, cy - 16 * d); p.lineTo(cx - 8 * d, cy); p.lineTo(cx + 8 * d, cy + 16 * d);
            } else {              // house outline
                p.moveTo(cx - 17 * d, cy - 1 * d); p.lineTo(cx, cy - 16 * d); p.lineTo(cx + 17 * d, cy - 1 * d);
                p.moveTo(cx - 12 * d, cy - 4 * d); p.lineTo(cx - 12 * d, cy + 15 * d); p.lineTo(cx + 12 * d, cy + 15 * d); p.lineTo(cx + 12 * d, cy - 4 * d);
            }
            cv.drawPath(p, ink);
        }

        @Override public void setPressed(boolean pressed) { super.setPressed(pressed); invalidate(); }
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
