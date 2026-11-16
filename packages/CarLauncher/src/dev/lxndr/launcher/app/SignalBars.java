package dev.lxndr.launcher.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

/** Four rising bars, the first `level` filled. Drawn, not text, so it stays crisp at any density. */
final class SignalBars extends View {
    private final Paint on = new Paint(Paint.ANTI_ALIAS_FLAG), off = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private int level;

    SignalBars(Context c, int onColor, int offColor) {
        super(c);
        on.setColor(onColor); off.setColor(offColor);
    }

    void setLevel(int l) { l = Math.max(0, Math.min(4, l)); if (l != level) { level = l; invalidate(); } }

    @Override protected void onDraw(Canvas c) {
        float w = getWidth(), h = getHeight(), gap = w * 0.08f, bw = (w - 3 * gap) / 4f, rad = bw / 3f;
        for (int i = 0; i < 4; i++) {
            float bh = h * (0.34f + 0.22f * i), left = i * (bw + gap);
            r.set(left, h - bh, left + bw, h);
            c.drawRoundRect(r, rad, rad, i < level ? on : off);
        }
    }
}
