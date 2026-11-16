package dev.lxndr.launcher.app;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.SweepGradient;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.LinearInterpolator;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Boot splash shown on the projected display while Android and the CarPlay session come up (the supervisor starts it
 * as soon as the display exists and starts the launcher once the session is streaming). Dark gradient, a logo ring that
 * keeps turning, the product name and a quiet status line. No text input, no way to leave it: nothing to touch.
 */
public final class SplashActivity extends Activity {
    private static final int ACCENT = Color.parseColor("#4FD1C5"), ACCENT_2 = Color.parseColor("#5B8DEF");
    private ValueAnimator spin;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        final float d = getResources().getDisplayMetrics().density;

        FrameLayout root = new FrameLayout(this);
        root.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {Color.parseColor("#0B1322"), Color.parseColor("#02050A")}));

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER);

        final Logo logo = new Logo(this, d);
        col.addView(logo, new LinearLayout.LayoutParams(Math.round(190 * d), Math.round(190 * d)));

        TextView name = new TextView(this);
        name.setText("AndroidPlay");
        name.setTextColor(Color.parseColor("#F2F6FF"));
        name.setTextSize(44);
        name.setLetterSpacing(0.08f);
        name.setTypeface(Typeface.create("sans-serif-light", Typeface.NORMAL));
        name.setPadding(0, Math.round(26 * d), 0, 0);
        col.addView(name);

        TextView sub = new TextView(this);
        sub.setText("Iniciando");
        sub.setTextColor(Color.parseColor("#8FA3C7"));
        sub.setTextSize(17);
        sub.setLetterSpacing(0.3f);
        sub.setPadding(0, Math.round(10 * d), 0, 0);
        col.addView(sub);

        root.addView(col, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(root);

        col.setAlpha(0f);
        col.animate().alpha(1f).setDuration(700).start();
        spin = ValueAnimator.ofFloat(0f, 360f);
        spin.setDuration(1600);
        spin.setRepeatCount(ValueAnimator.INFINITE);
        spin.setInterpolator(new LinearInterpolator());
        spin.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator a) { logo.angle = (Float) a.getAnimatedValue(); logo.invalidate(); }
        });
        spin.start();
    }

    @Override protected void onDestroy() { if (spin != null) spin.cancel(); super.onDestroy(); }
    @Override public void onBackPressed() { /* nothing to go back to */ }

    /** Static ring + rotating gradient arc + "S10" mark. */
    private static final class Logo extends View {
        float angle;
        private final float d;
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG), arc = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG), glow = new Paint(Paint.ANTI_ALIAS_FLAG);

        Logo(Context c, float d) {
            super(c); this.d = d;
            ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(5 * d); ring.setColor(Color.parseColor("#1C2A44"));
            arc.setStyle(Paint.Style.STROKE); arc.setStrokeWidth(8 * d); arc.setStrokeCap(Paint.Cap.ROUND);
            text.setColor(Color.parseColor("#F2F6FF")); text.setTextAlign(Paint.Align.CENTER); text.setTextSize(64 * d);
            text.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            glow.setStyle(Paint.Style.FILL);
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight(), cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 12 * d;
            glow.setShader(new LinearGradient(0, 0, w, h, new int[] {Color.parseColor("#14243D"), Color.parseColor("#0A1220")}, null, Shader.TileMode.CLAMP));
            cv.drawCircle(cx, cy, r - 6 * d, glow);
            cv.drawCircle(cx, cy, r, ring);
            arc.setShader(new SweepGradient(cx, cy, new int[] {Color.TRANSPARENT, ACCENT_2, ACCENT, Color.TRANSPARENT}, new float[] {0f, 0.35f, 0.7f, 0.72f}));
            cv.save();
            cv.rotate(angle, cx, cy);
            cv.drawArc(new RectF(cx - r, cy - r, cx + r, cy + r), 0, 252, false, arc);
            cv.restore();
            cv.drawText("S10", cx, cy + 22 * d, text);
        }
    }
}
