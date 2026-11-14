package com.s10caros.launcher.app;

/** Process-wide status shown on the launcher banner. Written by ConnectionService, read by the UI thread. */
final class Status {
    static volatile String line = "service not started";
    private Status() {}
}
