package com.s10caros.launcher.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import com.s10caros.launcher.core.ConnectionPolicy;
import com.s10caros.launcher.core.ConnectionPolicy.Action;
import com.s10caros.launcher.core.ConnectionPolicy.Event;
import com.s10caros.launcher.core.ModeConfig;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * Foreground service owning the ConnectionPolicy. It reacts to USB cable events and runs the policy's actions.
 *
 * NOT IMPLEMENTED: START_SESSION/STOP_SESSION do not yet drive the native CarPlayService (task CP-008); they only
 * update the status line. Nothing here has been run on a device.
 */
public final class ConnectionService extends Service {
    private static final String TAG = "S10CarOS";
    private static final String CHANNEL = "s10caros";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private ConnectionPolicy policy;
    private ModeConfig config;

    public static void start(Context c) { c.startForegroundService(new Intent(c, ConnectionService.class)); }

    private final BroadcastReceiver usb = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            String a = i.getAction();
            if (UsbManager.ACTION_USB_ACCESSORY_ATTACHED.equals(a) || "android.hardware.usb.action.USB_STATE".equals(a) && i.getBooleanExtra("connected", false))
                apply(Event.USB_ATTACHED);
            else if (UsbManager.ACTION_USB_ACCESSORY_DETACHED.equals(a) || "android.hardware.usb.action.USB_STATE".equals(a))
                apply(Event.USB_DETACHED);
        }
    };

    private final Runnable tick = new Runnable() {
        @Override public void run() { apply(Event.TICK); handler.postDelayed(this, 500); }
    };

    @Override public void onCreate() {
        super.onCreate();
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "S10CarOS", NotificationManager.IMPORTANCE_LOW));
        Notification n = new Notification.Builder(this, CHANNEL).setContentTitle("S10CarOS").setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).build();
        startForeground(1, n);
        config = loadConfig();
        policy = new ConnectionPolicy(config.mode == ModeConfig.Mode.VEHICLE, config.reconnectInitialMs, config.reconnectMaxMs, config.stableMs);
        IntentFilter f = new IntentFilter();
        f.addAction(UsbManager.ACTION_USB_ACCESSORY_ATTACHED);
        f.addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED);
        f.addAction("android.hardware.usb.action.USB_STATE");
        registerReceiver(usb, f, Context.RECEIVER_NOT_EXPORTED);
        publish();
        handler.postDelayed(tick, 500);
    }

    private ModeConfig loadConfig() {
        File f = new File(getFilesDir(), "config.properties");
        if (!f.exists()) return ModeConfig.defaults();
        try (FileInputStream in = new FileInputStream(f)) {
            ModeConfig c = ModeConfig.load(in);
            if (!c.valid()) Log.w(TAG, "config problems: " + c.errors);
            return c;
        } catch (IOException e) {
            Log.w(TAG, "config unreadable, using defaults", e);
            return ModeConfig.defaults();
        }
    }

    private void apply(Event e) {
        Action a = policy.on(e, SystemClock.elapsedRealtime());
        if (a == Action.START_SESSION) Log.i(TAG, "START_SESSION requested (native session not wired: CP-008)");
        if (a == Action.STOP_SESSION) Log.i(TAG, "STOP_SESSION requested");
        publish();
    }

    private void publish() {
        Status.line = config.mode + " mode / " + policy.state() + (config.valid() ? "" : " / config errors: " + config.errors.size());
    }

    @Override public int onStartCommand(Intent i, int flags, int id) { return START_STICKY; }
    @Override public IBinder onBind(Intent i) { return null; }
    @Override public void onDestroy() { handler.removeCallbacks(tick); unregisterReceiver(usb); super.onDestroy(); }
}
