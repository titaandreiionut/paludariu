package ro.paludariu.app;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.widget.RemoteViews;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Widgetul de pe ecranul telefonului: temperatura, umiditatea, scorul de confort si un buton pentru lumina.
 * Arata ultima placa deschisa (sau prima salvata). Se actualizeaza cand e deschisa aplicatia,
 * la verificarea notificarilor (cam la 15 min) si singur, la jumatate de ora.
 */
public class Widget extends AppWidgetProvider {
    static final String ACT_LIGHT = "ro.paludariu.app.WIDGET_LUMINA";
    static final String ACT_REFRESH = "ro.paludariu.app.WIDGET_ACTUALIZEAZA";

    /** Placa din widget: ultima deschisa, altfel prima salvata. */
    static Boards.Board board(Context c) {
        Boards.Board b = Boards.last(c);
        if (b != null) return b;
        List<Boards.Board> l = Boards.load(c);
        return l.isEmpty() ? null : l.get(0);
    }

    // ---------------- datele ----------------

    /** Pastreaza starea primita de la placa, daca e placa din widget. */
    static void store(Context c, String url, String name, JSONObject d) {
        Boards.Board b = board(c);
        if (b == null || url == null || !b.url.equalsIgnoreCase(Boards.normalize(url))) return;
        JSONArray st = d.optJSONArray("st");
        Boards.prefs(c).edit()
                .putString("w_url", b.url)
                .putString("w_t", d.isNull("t") ? "" : AlertRules.fmt((float) d.optDouble("t")))
                .putString("w_h", d.isNull("h") ? "" : String.valueOf(Math.round(d.optDouble("h"))))
                .putBoolean("w_l", st != null && st.optBoolean(1))
                .putInt("w_sc", score(d))
                .putLong("w_time", System.currentTimeMillis())
                .putBoolean("w_ok", true)
                .apply();
    }

    /** Scorul de confort, la fel ca pe pagina placii (fara variatia din ultimele ore). */
    static int score(JSONObject d) {
        JSONObject s = d.optJSONObject("s");
        if (s == null) return -1;
        Double t = d.isNull("t") ? null : d.optDouble("t");
        Double h = d.isNull("h") ? null : d.optDouble("h");
        int lo = s.optInt("lumOra", 480), hi = s.optInt("lumStop", 1200);
        Calendar now = Calendar.getInstance();
        int m = now.get(Calendar.HOUR_OF_DAY) * 60 + now.get(Calendar.MINUTE);
        boolean zi = lo <= hi ? (m >= lo && m < hi) : (m >= lo || m < hi);
        double r0 = zi ? 24 : 21, r1 = zi ? 27 : 24;
        double st = 0, sh = 0;
        if (t != null) {
            double dd = t < r0 ? r0 - t : t > r1 ? t - r1 : 0;
            st = t >= 30 ? 0 : Math.max(0, 100 - 25 * dd);
        }
        if (h != null) {
            double dd = h < 75 ? 75 - h : h > 90 ? h - 90 : 0;
            sh = Math.max(0, 100 - 5 * dd);
            if (h < 65) sh = Math.min(sh, 30);
        }
        int sig = 0;
        if (t == null) sig++;
        JSONObject L = d.optJSONObject("led");
        if (L != null) {
            if (L.optInt("oh", 0) == 1) sig++;
            if (L.optInt("ff", 0) == 1) sig++;
            if (L.isNull("t")) sig++;
        }
        if (d.has("apa") && !d.isNull("apa") && !d.optBoolean("apa", true)) sig++;
        double sv = 100, ss = sig > 0 ? Math.max(0, 100 - 50 * sig) : 100;
        double w = Math.min(Math.min(st, sh), Math.min(sv, ss));
        return (int) Math.round((.4 * st + .4 * sh + .1 * sv + .1 * ss) * (.5 + .5 * w / 100));
    }

    // ---------------- afisarea ----------------

    static RemoteViews views(Context c) {
        RemoteViews v = new RemoteViews(c.getPackageName(), R.layout.widget);
        SharedPreferences p = Boards.prefs(c);
        Boards.Board b = board(c);

        Intent open = new Intent(c, MainActivity.class);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        v.setOnClickPendingIntent(R.id.w_root, PendingIntent.getActivity(c, 0, open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        v.setOnClickPendingIntent(R.id.w_light, broadcast(c, ACT_LIGHT, 1));
        v.setOnClickPendingIntent(R.id.w_time, broadcast(c, ACT_REFRESH, 2));

        if (b == null) {
            v.setTextViewText(R.id.w_name, "Paludariu");
            v.setTextViewText(R.id.w_time, "");
            v.setTextViewText(R.id.w_t, "--");
            v.setTextViewText(R.id.w_h, "");
            v.setTextViewText(R.id.w_sc, "--");
            v.setTextViewText(R.id.w_light, "Deschide aplicația și adaugă placa");
            v.setInt(R.id.w_light, "setBackgroundResource", R.drawable.widget_btn_off);
            v.setTextColor(R.id.w_light, Ui.TXT);
            return v;
        }
        boolean same = b.url.equalsIgnoreCase(p.getString("w_url", ""));
        long tm = same ? p.getLong("w_time", 0) : 0;
        boolean ok = same && p.getBoolean("w_ok", false);
        boolean busy = p.getBoolean("w_busy", false);
        boolean on = same && p.getBoolean("w_l", false);
        String t = same ? p.getString("w_t", "") : "", h = same ? p.getString("w_h", "") : "";
        int sc = same ? p.getInt("w_sc", -1) : -1;

        v.setTextViewText(R.id.w_name, b.name);
        v.setTextViewText(R.id.w_time, tm == 0 ? "↻" : when(tm) + " ↻");
        v.setTextViewText(R.id.w_t, t.isEmpty() ? "--" : t + "°");
        v.setTextViewText(R.id.w_h, h.isEmpty() ? "" : h + "%");
        v.setTextViewText(R.id.w_sc, sc < 0 ? "--" : String.valueOf(sc));
        v.setTextColor(R.id.w_sc, sc < 0 ? Ui.MUTED : sc >= 80 ? Ui.GREEN : sc >= 60 ? 0xFFF2B45A : Ui.BAD);

        String txt;
        if (busy) txt = "Se trimite…";
        else if (!ok) txt = "Placa nu răspunde · atinge ca să încerci iar";
        else txt = on ? "Lumina e aprinsă · atinge ca s-o stingi" : "Lumina e stinsă · atinge ca s-o aprinzi";
        boolean green = on && ok && !busy;
        v.setTextViewText(R.id.w_light, txt);
        v.setInt(R.id.w_light, "setBackgroundResource", green ? R.drawable.widget_btn_on : R.drawable.widget_btn_off);
        v.setTextColor(R.id.w_light, green ? Ui.ON_GREEN : Ui.TXT);
        return v;
    }

    private static String when(long t) {
        long age = System.currentTimeMillis() - t;
        if (age < 0) age = 0;
        Calendar a = Calendar.getInstance(), n = Calendar.getInstance();
        a.setTimeInMillis(t);
        boolean azi = a.get(Calendar.YEAR) == n.get(Calendar.YEAR) && a.get(Calendar.DAY_OF_YEAR) == n.get(Calendar.DAY_OF_YEAR);
        if (age < 2 * 3600_000L || azi) return new SimpleDateFormat("HH:mm", Locale.getDefault()).format(new Date(t));
        return new SimpleDateFormat("dd.MM HH:mm", Locale.getDefault()).format(new Date(t));
    }

    private static PendingIntent broadcast(Context c, String action, int code) {
        Intent i = new Intent(c, Widget.class);
        i.setAction(action);
        return PendingIntent.getBroadcast(c, code, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    static void updateAll(Context c) {
        AppWidgetManager m = AppWidgetManager.getInstance(c);
        if (m == null) return;
        int[] ids = m.getAppWidgetIds(new ComponentName(c, Widget.class));
        if (ids == null || ids.length == 0) return;
        m.updateAppWidget(ids, views(c));
    }

    // ---------------- evenimente ----------------

    @Override
    public void onUpdate(Context c, AppWidgetManager m, int[] ids) {
        m.updateAppWidget(ids, views(c));
        refresh(c, goAsync(), false);
    }

    @Override
    public void onReceive(Context c, Intent i) {
        super.onReceive(c, i);
        String a = i.getAction();
        if (ACT_LIGHT.equals(a)) {
            // daca nu stim starea (placa n-a raspuns), intai o citim; nu apasam pe ghicite
            boolean ok = Boards.prefs(c).getBoolean("w_ok", false);
            refresh(c, goAsync(), ok);
        } else if (ACT_REFRESH.equals(a)) {
            refresh(c, goAsync(), false);
        }
    }

    /** Citeste placa (si, la cerere, aprinde/stinge lumina) pe un fir separat. */
    static void refresh(Context c0, PendingResult pr, boolean toggle) {
        final Context c = c0.getApplicationContext();
        final Boards.Board b = board(c);
        if (b == null) { updateAll(c); if (pr != null) pr.finish(); return; }
        Boards.prefs(c).edit().putBoolean("w_busy", true).apply();
        updateAll(c);
        new Thread(() -> {
            boolean ok = false;
            try {
                SharedPreferences p = Boards.prefs(c);
                if (toggle) {
                    boolean on = p.getBoolean("w_l", false);
                    AlertChecker.getAny(c, b, "/m?i=1&v=" + (on ? 2 : 1));   // ca pe pagina: comanda manuala
                    Thread.sleep(400);
                }
                String t = AlertChecker.getAny(c, b, "/s");   // acasa direct; altfel prin internet, daca e configurat
                if (t != null) { store(c, b.url, b.name, new JSONObject(t)); ok = true; }
            } catch (Exception ignored) {
            } finally {
                SharedPreferences.Editor e = Boards.prefs(c).edit().putBoolean("w_busy", false);
                if (!ok) e.putBoolean("w_ok", false);
                e.apply();
                updateAll(c);
                if (pr != null) pr.finish();
            }
        }).start();
    }
}
