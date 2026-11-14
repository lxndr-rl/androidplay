package com.s10caros.projection;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Handler;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;
import android.view.Surface;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Probe service: creates an app-owned public VirtualDisplay backed by an H.264 encoder, records for N seconds to
 * files/out.h264 and writes files/metrics.json. Started from adb only (service is protected by DUMP).
 *
 * Extras: w h fps kbps seconds (ints). Logs "S10CarOS-PROJ display=<id>" so a test can launch apps onto it.
 *
 * NOT the production capture path: a plain app can only render its own content onto this display. Mirroring the
 * phone screen without user consent needs platform privileges in the ROM (see docs/validation.md, MEDIA-001).
 */
public final class ProjectionService extends Service {
    private static final String TAG = "S10CarOS-PROJ";
    private final Handler handler = new Handler(android.os.Looper.getMainLooper());
    private VirtualDisplay display;
    private H264Encoder encoder;
    private BufferedOutputStream out;
    private long frames, keyFrames, bytes, firstKeyMs = -1, startMs, lastPtsUs;
    private long maxAuBytes;

    @Override public int onStartCommand(Intent i, int flags, int id) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("proj", "Projection", NotificationManager.IMPORTANCE_LOW));
        startForeground(2, new Notification.Builder(this, "proj").setContentTitle("S10CarOS projection")
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).build());
        final int w = i.getIntExtra("w", 1200), h = i.getIntExtra("h", 512), fps = i.getIntExtra("fps", 30);
        final int kbps = i.getIntExtra("kbps", 3000), seconds = i.getIntExtra("seconds", 10);
        try {
            out = new BufferedOutputStream(new FileOutputStream(new File(getFilesDir(), "out.h264")));
            startMs = SystemClock.elapsedRealtime();
            encoder = new H264Encoder(w, h, fps, kbps * 1000, this::onAu);
            Surface s = encoder.start();
            display = getSystemService(DisplayManager.class).createVirtualDisplay("s10caros", w, h, 160, s,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC | DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY);
            if (display == null) throw new IOException("createVirtualDisplay returned null");
            Log.i(TAG, "display=" + display.getDisplay().getDisplayId() + " size=" + w + "x" + h);
            handler.postDelayed(() -> finish(w, h, fps, kbps, seconds), seconds * 1000L);
        } catch (Exception e) {
            Log.e(TAG, "start failed", e);
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    private synchronized void onAu(byte[] au, boolean key, long ptsUs) {
        frames++; bytes += au.length; lastPtsUs = ptsUs;
        if (au.length > maxAuBytes) maxAuBytes = au.length;
        if (key) { keyFrames++; if (firstKeyMs < 0) firstKeyMs = SystemClock.elapsedRealtime() - startMs; }
        try { out.write(au); } catch (IOException e) { Log.e(TAG, "write", e); }
    }

    private void finish(int w, int h, int fps, int kbps, int seconds) {
        if (display != null) display.release();
        encoder.stop();
        synchronized (this) {
            try { out.close(); } catch (IOException ignored) { }
            String json = String.format("{\"width\":%d,\"height\":%d,\"fps_target\":%d,\"kbps_target\":%d,\"seconds\":%d,"
                    + "\"frames\":%d,\"key_frames\":%d,\"bytes\":%d,\"max_au_bytes\":%d,\"first_key_ms\":%d,\"last_pts_us\":%d}",
                    w, h, fps, kbps, seconds, frames, keyFrames, bytes, maxAuBytes, firstKeyMs, lastPtsUs);
            try (FileOutputStream f = new FileOutputStream(new File(getFilesDir(), "metrics.json"))) { f.write(json.getBytes()); }
            catch (IOException e) { Log.e(TAG, "metrics", e); }
            Log.i(TAG, "done " + json);
        }
        stopSelf();
    }

    @Override public IBinder onBind(Intent i) { return null; }
}
