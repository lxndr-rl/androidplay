package dev.lxndr.launcher.core;

/** Text for the launcher cards (pure functions, host-testable): WMO weather codes, speed, ages. */
public final class CardText {
    private CardText() {}

    /** WMO weather interpretation code (Open-Meteo `weather_code`) -> Spanish description. */
    public static String weatherText(int code) {
        if (code == 0) return "Despejado";
        if (code == 1) return "Mayormente despejado";
        if (code == 2) return "Parcialmente nublado";
        if (code == 3) return "Nublado";
        if (code == 45 || code == 48) return "Niebla";
        if (code >= 51 && code <= 57) return "Llovizna";
        if (code >= 61 && code <= 67) return "Lluvia";
        if (code >= 71 && code <= 77) return "Nieve";
        if (code >= 80 && code <= 82) return "Chubascos";
        if (code == 85 || code == 86) return "Chubascos de nieve";
        if (code >= 95 && code <= 99) return "Tormenta";
        return "Sin datos";
    }

    public static String weatherIcon(int code) {
        if (code == 0 || code == 1) return "☀️";
        if (code == 2) return "⛅";
        if (code == 3) return "☁️";
        if (code == 45 || code == 48) return "🌫️";
        if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "🌧️";
        if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "❄️";
        if (code >= 95 && code <= 99) return "⛈️";
        return "☁️";
    }

    /** m/s from the GPS -> whole km/h; below 2 km/h it is GPS noise at standstill and shows 0. */
    public static int kmh(float metersPerSecond) {
        if (metersPerSecond < 0 || Float.isNaN(metersPerSecond)) return 0;
        int v = Math.round(metersPerSecond * 3.6f);
        return v < 2 ? 0 : v;
    }

    /** "ahora", "hace 5 min", "hace 2 h". */
    public static String age(long ms) {
        long m = ms / 60_000;
        if (m < 1) return "ahora";
        if (m < 60) return "hace " + m + " min";
        return "hace " + (m / 60) + " h";
    }

    public static String temp(double c) { return Math.round(c) + "°"; }
}
