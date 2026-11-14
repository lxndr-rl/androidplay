package com.s10caros.projection;

/** JNI bridge to libs10carplay.so (services/CarPlayService/android). Thread-safe: calls only enqueue work. */
public final class CarPlayNative {
    public interface Listener {
        /** kind: 0 down, 1 move, 2 up (TouchEvent::Kind order); x,y in capture pixels. Called on the native runner thread. */
        void onTouch(int kind, int x, int y);
        /** The car asked for an IDR (forceKeyFrame / requestUI / changeModes). Called on the native runner thread. */
        void onKeyFrameRequested();
    }

    static { System.loadLibrary("s10carplay"); }

    private long handle;

    public CarPlayNative(Listener l) { handle = create(l); }
    public void start(String host, int port) { start(handle, host, port); }
    public void pushVideo(byte[] annexB) { pushVideo(handle, annexB, annexB.length); }
    public void pushAudio(byte[] pcmLe, int len) { pushAudio(handle, pcmLe, len); }
    public String status() { return status(handle); }
    public void close() { if (handle != 0) { destroy(handle); handle = 0; } }

    private static native long create(Listener l);
    private static native void start(long h, String host, int port);
    private static native void pushVideo(long h, byte[] d, int len);
    private static native void pushAudio(long h, byte[] d, int len);
    private static native String status(long h);
    private static native void destroy(long h);
}
