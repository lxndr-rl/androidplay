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
import android.media.AudioFormat;
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
    private CarPlayNative car;
    private volatile boolean audioRun;
    private Thread audioThread;

    @Override public int onStartCommand(Intent i, int flags, int id) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("proj", "Projection", NotificationManager.IMPORTANCE_LOW));
        startForeground(2, new Notification.Builder(this, "proj").setContentTitle("S10CarOS projection")
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).build());
        final int w = i.getIntExtra("w", 1200), h = i.getIntExtra("h", 512), fps = i.getIntExtra("fps", 30);
        final int kbps = i.getIntExtra("kbps", 3000), seconds = i.getIntExtra("seconds", 10);
        final String carHost = i.getStringExtra("host");
        final int carPort = i.getIntExtra("port", 0);
        try {
            out = new BufferedOutputStream(new FileOutputStream(new File(getFilesDir(), "out.h264")));
            startMs = SystemClock.elapsedRealtime();
            if (carHost != null && carPort > 0) {
                // CarPlay mode: access units go to the native sender instead of a file.
                car = new CarPlayNative(new CarPlayNative.Listener() {
                    @Override public void onTouch(int kind, int x, int y) { Log.i(TAG, "touch kind=" + kind + " x=" + x + " y=" + y); }
                    @Override public void onKeyFrameRequested() { Log.i(TAG, "keyframe requested"); if (encoder != null) encoder.requestKeyFrame(); }
                });
            }
            encoder = new H264Encoder(w, h, fps, kbps * 1000, this::onAu);
            Surface s = encoder.start();
            display = getSystemService(DisplayManager.class).createVirtualDisplay("s10caros", w, h, 160, s,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC | DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY);
            if (display == null) throw new IOException("createVirtualDisplay returned null");
            Log.i(TAG, "display=" + display.getDisplay().getDisplayId() + " size=" + w + "x" + h);
            if (car != null) {   // pipeline is up: only now let the car session begin
                car.start(carHost, carPort);
                Log.i(TAG, "carplay target " + carHost + ":" + carPort);
                startSyntheticAudio();
            }
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
        if (car != null) car.pushVideo(au);
        try { out.write(au); } catch (IOException e) { Log.e(TAG, "write", e); }
    }

    /** TEST FIXTURE: 440 Hz synthetic tone as 10 ms chunks, paced in real time. This is NOT system-audio capture. */
    private void startSyntheticAudio() {
        audioRun = true;
        audioThread = new Thread(() -> {
            final int rate = 48000, frames = rate / 100;
            byte[] chunk = new byte[frames * 4];
            long n = 0, next = SystemClock.elapsedRealtime();
            while (audioRun) {
                for (int i = 0; i < frames; i++, n++) {
                    short v = (short) (8000 * Math.sin(2 * Math.PI * 440 * n / rate));
                    chunk[4 * i] = chunk[4 * i + 2] = (byte) v;
                    chunk[4 * i + 1] = chunk[4 * i + 3] = (byte) (v >> 8);
                }
                car.pushAudio(chunk, chunk.length);
                next += 10;
                long d = next - SystemClock.elapsedRealtime();
                if (d > 0) SystemClock.sleep(d);
            }
        }, "s10-synth-audio");
        audioThread.start();
    }

    private void finish(int w, int h, int fps, int kbps, int seconds) {
        audioRun = false;
        if (audioThread != null) try { audioThread.join(500); } catch (InterruptedException ignored) { }
        if (car != null) { Log.i(TAG, "carplay status: " + car.status()); }
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
        if (car != null) { car.close(); car = null; }
        stopSelf();
    }

    @Override public IBinder onBind(Intent i) { return null; }
}
