package ro.paludariu.app;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.widget.RemoteViews;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Widgetul de pe ecranul telefonului (Inel + Sticla): confortul intr-un inel, temperatura si umiditatea,
 * linia temperaturii din ultimele 24 de ore si 4 butoane (pompa, lumina, ceata, ventilator).
 * Arata ultima placa deschisa (sau prima salvata). Se actualizeaza cand e deschisa aplicatia,
 * la verificarea notificarilor (cam la 15 min) si singur, la jumatate de ora.
 */
public class Widget extends AppWidgetProvider {
    static final String ACT_LIGHT = "ro.paludariu.app.WIDGET_LUMINA";        // widgeturile vechi: tot lumina
    static final String ACT_TOGGLE = "ro.paludariu.app.WIDGET_COMUTA_";      // + numarul aparatului
    static final String ACT_REFRESH = "ro.paludariu.app.WIDGET_ACTUALIZEAZA";

    static final int[] BTN = {R.id.w_b0, R.id.w_b1, R.id.w_b2, R.id.w_b3};
    static final int[] BG_ON = {R.drawable.widget_ob_cy, R.drawable.widget_ob_am, R.drawable.widget_ob_cy, R.drawable.widget_ob_gr};
    static final int[] COL_ON = {0xFF5CC6C0, 0xFFF2B45A, 0xFF5CC6C0, 0xFF86CF8E};
    static final int MUTED = 0xFF93A99A;

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
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < 4; i++) s.append(st != null && st.optBoolean(i) ? '1' : '0');
        Boards.prefs(c).edit()
                .putString("w_url", b.url)
                .putString("w_t", d.isNull("t") ? "" : AlertRules.fmt((float) d.optDouble("t")))
                .putString("w_h", d.isNull("h") ? "" : String.valueOf(Math.round(d.optDouble("h"))))
                .putBoolean("w_l", st != null && st.optBoolean(1))
                .putString("w_st", s.toString())
                .putInt("w_sc", score(d))
                .putLong("w_time", System.currentTimeMillis())
                .putBoolean("w_ok", true)
                .apply();
    }

    /** Temperatura din ultimele 24 de ore (de la /h), redusa la cel mult 72 de puncte, ca text "24.1,24.3,n,..." */
    static void storeHist(Context c, JSONObject h) {
        JSONArray t = h.optJSONArray("t");
        if (t == null || t.length() < 2) return;
        int n = t.length(), k = Math.min(72, n);
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < k; i++) {
            int a = i * n / k, z = Math.max(a + 1, (i + 1) * n / k);
            double sum = 0; int cnt = 0;
            for (int j = a; j < z && j < n; j++) if (!t.isNull(j)) { sum += t.optDouble(j); cnt++; }
            if (i > 0) s.append(',');
            s.append(cnt == 0 ? "n" : String.format(Locale.US, "%.1f", sum / cnt));
        }
        Boards.prefs(c).edit().putString("w_spark", s.toString()).putLong("w_spark_t", System.currentTimeMillis()).apply();
    }

    /** Scorul de confort, la fel ca pe pagina placii (fara racirea din ultimele ore). Lampa LED nu intra in confort. */
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
        if (d.has("apa") && !d.isNull("apa") && !d.optBoolean("apa", true)) sig++;
        double sv = 100, ss = sig > 0 ? Math.max(0, 100 - 50 * sig) : 100;
        double w = Math.min(Math.min(st, sh), Math.min(sv, ss));
        return (int) Math.round((.4 * st + .4 * sh + .1 * sv + .1 * ss) * (.5 + .5 * w / 100));
    }

    // ---------------- desenele (inelul si linia) ----------------

    private static float dens(Context c) { return Math.min(2.5f, c.getResources().getDisplayMetrics().density); }

    /** Inelul de confort: 3/4 de cerc, colorat dupa scor. */
    static Bitmap ring(Context c, int sc) {
        float d = dens(c);
        int px = Math.round(84 * d);
        Bitmap b = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(b);
        float sw = 7.5f * d;
        RectF r = new RectF(sw / 2 + d, sw / 2 + d, px - sw / 2 - d, px - sw / 2 - d);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(sw);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setColor(0x1FFFFFFF);
        cv.drawArc(r, 135, 270, false, p);
        if (sc > 0) {
            p.setColor(sc >= 80 ? 0xFF86CF8E : sc >= 60 ? 0xFFF2B45A : 0xFFEF6F5E);
            if (sc >= 80) p.setShader(new LinearGradient(0, 0, px, px, 0xFF86CF8E, 0xFF5CC6C0, Shader.TileMode.CLAMP));
            cv.drawArc(r, 135, 270f * Math.min(100, sc) / 100f, false, p);
        }
        return b;
    }

    /** Linia temperaturii pe 24 de ore, cu o umbra usoara dedesubt. */
    static Bitmap spark(Context c, String data) {
        float d = dens(c);
        int w = Math.round(200 * d), h = Math.round(22 * d);
        Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        if (data == null || data.isEmpty()) return b;
        String[] v = data.split(",");
        float mn = Float.MAX_VALUE, mx = -Float.MAX_VALUE;
        float[] f = new float[v.length];
        boolean[] ok = new boolean[v.length];
        for (int i = 0; i < v.length; i++) {
            try { f[i] = Float.parseFloat(v[i]); ok[i] = true; mn = Math.min(mn, f[i]); mx = Math.max(mx, f[i]); } catch (NumberFormatException e) { ok[i] = false; }
        }
        if (mn > mx) return b;
        if (mx - mn < 1.5f) { float m = (mx + mn) / 2; mn = m - .75f; mx = m + .75f; }
        float pad = 2.5f * d;
        Path line = new Path(), fill = new Path();
        boolean started = false;
        float x0 = 0, lastX = 0;
        for (int i = 0; i < v.length; i++) {
            if (!ok[i]) continue;
            float x = v.length == 1 ? 0 : i * (w - 1f) / (v.length - 1);
            float y = pad + (h - 2 * pad) * (1 - (f[i] - mn) / (mx - mn));
            if (!started) { line.moveTo(x, y); fill.moveTo(x, h); fill.lineTo(x, y); x0 = x; started = true; }
            else { line.lineTo(x, y); fill.lineTo(x, y); }
            lastX = x;
        }
        fill.lineTo(lastX, h);
        fill.lineTo(x0, h);
        fill.close();
        Canvas cv = new Canvas(b);
        Paint pf = new Paint(Paint.ANTI_ALIAS_FLAG);
        pf.setShader(new LinearGradient(0, 0, 0, h, 0x55F2B45A, 0x00F2B45A, Shader.TileMode.CLAMP));
        cv.drawPath(fill, pf);
        Paint pl = new Paint(Paint.ANTI_ALIAS_FLAG);
        pl.setStyle(Paint.Style.STROKE);
        pl.setStrokeWidth(1.7f * d);
        pl.setStrokeJoin(Paint.Join.ROUND);
        pl.setStrokeCap(Paint.Cap.ROUND);
        pl.setColor(0xD9F2B45A);
        cv.drawPath(line, pl);
        return b;
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
        v.setOnClickPendingIntent(R.id.w_time, broadcast(c, ACT_REFRESH, 2));
        for (int i = 0; i < 4; i++) v.setOnClickPendingIntent(BTN[i], broadcast(c, ACT_TOGGLE + i, 10 + i));

        boolean same = b != null && b.url.equalsIgnoreCase(p.getString("w_url", ""));
        long tm = same ? p.getLong("w_time", 0) : 0;
        boolean ok = same && p.getBoolean("w_ok", false);
        int busy = p.getBoolean("w_busy", false) ? p.getInt("w_busy_i", -1) : -2;
        String st = same ? p.getString("w_st", "0000") : "0000";
        String t = same ? p.getString("w_t", "") : "", h = same ? p.getString("w_h", "") : "";
        int sc = same ? p.getInt("w_sc", -1) : -1;

        v.setTextViewText(R.id.w_name, b == null ? "Deschide aplicația și adaugă placa" : b.name);
        if (b == null) v.setTextViewText(R.id.w_time, "");
        else if (busy >= -1) v.setTextViewText(R.id.w_time, "Se trimite…");
        else if (tm != 0 && !ok) v.setTextViewText(R.id.w_time, "nu răspunde ↻");
        else v.setTextViewText(R.id.w_time, tm == 0 ? "↻" : when(tm) + " ↻");
        v.setTextColor(R.id.w_time, tm != 0 && !ok && busy < -1 ? 0xFFEF6F5E : MUTED);
        v.setTextViewText(R.id.w_t, t.isEmpty() ? "--" : t + "°");
        v.setTextViewText(R.id.w_h, h.isEmpty() ? "--" : h + "%");
        v.setTextViewText(R.id.w_sc, sc < 0 ? "--" : String.valueOf(sc));
        try {
            v.setImageViewBitmap(R.id.w_ring, ring(c, sc));
            v.setImageViewBitmap(R.id.w_spark, spark(c, same ? p.getString("w_spark", "") : ""));
        } catch (Throwable ignored) {   // fara desene daca telefonul n-are memorie: cifrele raman
        }
        for (int i = 0; i < 4; i++) {
            boolean on = ok && st.length() > i && st.charAt(i) == '1';
            v.setInt(BTN[i], "setBackgroundResource", on ? BG_ON[i] : R.drawable.widget_ob_off);
            v.setInt(BTN[i], "setColorFilter", on ? COL_ON[i] : MUTED);
            v.setInt(BTN[i], "setImageAlpha", busy == i ? 110 : 255);
        }
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
        refresh(c, goAsync(), -1);
    }

    @Override
    public void onReceive(Context c, Intent i) {
        super.onReceive(c, i);
        String a = i.getAction();
        if (a == null) return;
        int k = -1;
        if (ACT_LIGHT.equals(a)) k = 1;
        else if (a.startsWith(ACT_TOGGLE)) {
            try { k = Integer.parseInt(a.substring(ACT_TOGGLE.length())); } catch (NumberFormatException e) { k = -1; }
            if (k < 0 || k > 3) return;
        } else if (!ACT_REFRESH.equals(a)) return;
        // daca nu stim starea (placa n-a raspuns), intai o citim; nu apasam pe ghicite
        boolean ok = Boards.prefs(c).getBoolean("w_ok", false);
        refresh(c, goAsync(), ok ? k : -1);
    }

    /** Citeste placa (si, la cerere, porneste/opreste aparatul k) pe un fir separat. */
    static void refresh(Context c0, PendingResult pr, int k) {
        final Context c = c0.getApplicationContext();
        final Boards.Board b = board(c);
        if (b == null) { updateAll(c); if (pr != null) pr.finish(); return; }
        Boards.prefs(c).edit().putBoolean("w_busy", true).putInt("w_busy_i", k).apply();
        updateAll(c);
        new Thread(() -> {
            boolean ok = false;
            try {
                SharedPreferences p = Boards.prefs(c);
                if (k >= 0) {
                    String st = p.getString("w_st", "0000");
                    boolean on = st.length() > k && st.charAt(k) == '1';
                    AlertChecker.getAny(c, b, "/m?i=" + k + "&v=" + (on ? 2 : 1));   // ca pe pagina: comanda manuala
                    Thread.sleep(400);
                }
                String t = AlertChecker.getAny(c, b, "/s");   // acasa direct; altfel prin internet, daca e configurat
                if (t != null) { store(c, b.url, b.name, new JSONObject(t)); ok = true; }
                // linia de 24 de ore: cel mult o data la 10 minute
                if (ok && System.currentTimeMillis() - p.getLong("w_spark_t", 0) > 600_000L) {
                    String h = AlertChecker.getAny(c, b, "/h");
                    if (h != null) storeHist(c, new JSONObject(h));
                }
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
