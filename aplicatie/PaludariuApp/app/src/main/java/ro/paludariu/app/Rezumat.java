package ro.paludariu.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.Locale;

/**
 * Rezumatul de dimineata (1.8.9): la ora aleasa (implicit 8:00), o notificare cu noaptea trecuta:
 * temperatura si umiditatea minime/maxime, confortul si de cate ori a pornit ceata.
 * Daca noaptea a fost o problema, vine si o a doua notificare, galbena.
 * Datele le aduna placa intre 22:00 si 08:00 (/noapte).
 */
final class Rezumat {
    static final String CHANNEL = "rezumat";
    static final String PREF_ON = "rez_on", PREF_ORA = "rez_ora";
    static final int AMBER = 0xFFF2B45A;

    private Rezumat() {}

    static boolean on(Context c) { return Boards.prefs(c).getBoolean(PREF_ON, true); }
    static int ora(Context c) { return Boards.prefs(c).getInt(PREF_ORA, 8); }

    private static String azi() { return new SimpleDateFormat("yyyy-MM-dd", Locale.US).format(new Date()); }

    /** Din verificarea de la 15 minute: trimite rezumatul o data pe zi, dupa ora aleasa (pana la 13:00). */
    static void check(Context c) {
        if (!on(c)) return;
        Calendar now = Calendar.getInstance();
        int h = now.get(Calendar.HOUR_OF_DAY);
        if (h < ora(c) || h >= 13) return;
        SharedPreferences p = Boards.prefs(c);
        String zi = azi();
        for (Boards.Board b : Boards.load(c)) {
            String k = "rez_zi_" + b.url;
            if (zi.equals(p.getString(k, ""))) continue;
            try {
                String t = AlertChecker.getAny(c, b, "/noapte");
                if (t == null) continue;          // placa nu raspunde acum: incercam la urmatoarea verificare
                JSONObject j = new JSONObject(t);
                p.edit().putString(k, zi).apply();
                if (j.optInt("ok", 0) != 1) continue;
                long la = j.optLong("la", 0) * 1000L;
                if (la > 0 && System.currentTimeMillis() - la > 20 * 3600_000L) continue;   // noapte veche (placa a fost oprita)
                send(c, b, j);
            } catch (Exception ignored) {
            }
        }
    }

    static String nr(double v, int d) { return String.format(Locale.US, "%." + d + "f", v).replace('.', ','); }

    /** Textul rezumatului, ex. "22,1–24,8 °C · umiditate 82–93% · confort 91 · ceața a pornit de 6 ori". */
    static String text(JSONObject j) {
        StringBuilder s = new StringBuilder();
        if (!j.isNull("tMin") && !j.isNull("tMax")) s.append(nr(j.optDouble("tMin"), 1)).append("–").append(nr(j.optDouble("tMax"), 1)).append(" °C");
        if (!j.isNull("hMin") && !j.isNull("hMax")) {
            if (s.length() > 0) s.append(" · ");
            s.append("umiditate ").append(nr(j.optDouble("hMin"), 0)).append("–").append(nr(j.optDouble("hMax"), 0)).append("%");
        }
        if (!j.isNull("cf")) {
            int cf = j.optInt("cf");
            s.append(" · confort ").append(cf).append(cf >= 85 ? " (excelent)" : cf >= 70 ? " (bun)" : cf >= 50 ? " (acceptabil)" : " (slab)");
        }
        int n = j.optInt("ceata", 0);
        s.append(" · ceața ").append(n == 0 ? "n-a pornit" : n == 1 ? "a pornit o dată" : "a pornit de " + n + " ori");
        long tmo = j.optLong("tMinO", 0);
        if (tmo > 0 && !j.isNull("tMin"))
            s.append(". Cel mai frig a fost la ").append(new SimpleDateFormat("HH:mm", Locale.US).format(new Date(tmo * 1000L)));
        return s.append(".").toString();
    }

    static void send(Context c, Boards.Board b, JSONObject j) {
        ensureChannel(c);
        post(c, ("rez" + b.url).hashCode(), "☀️ Noaptea la " + b.name, text(j), b, Ui.GREEN, CHANNEL);
        JSONArray pb = j.optJSONArray("pb");
        if (pb != null && pb.length() > 0) {
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < pb.length(); i++) {
                String x = pb.optString(i);
                if (x.isEmpty()) continue;
                if (s.length() > 0) s.append("; ");
                s.append(x);
            }
            String t = s.length() > 0 ? Character.toUpperCase(s.charAt(0)) + s.substring(1) + "." : "";
            AlertChecker.ensureChannel(c);
            post(c, ("rezpb" + b.url).hashCode(), "⚠️ Noaptea, la " + b.name, t + " Atinge ca să vezi istoricul.", b, AMBER, AlertChecker.CHANNEL);
        }
    }

    static void ensureChannel(Context c) {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Rezumatul de dimineață", NotificationManager.IMPORTANCE_DEFAULT);
            ch.setDescription("Cum a fost noaptea în paludariu");
            if (nm != null) nm.createNotificationChannel(ch);
        }
    }

    @SuppressWarnings("deprecation")
    private static void post(Context c, int id, String title, String text, Boards.Board open, int color, String channel) {
        Intent i = new Intent(c, BoardActivity.class);
        i.putExtra(BoardActivity.EXTRA_URL, open.url);
        i.putExtra(BoardActivity.EXTRA_NAME, open.name);
        i.putExtra(BoardActivity.EXTRA_HASH, "istoric");
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent pi = PendingIntent.getActivity(c, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder nb = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, channel) : new Notification.Builder(c);
        nb.setSmallIcon(R.drawable.ic_stat_crab).setColor(color).setContentTitle(title).setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text)).setContentIntent(pi).setAutoCancel(true);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.notify(id, nb.build());
    }

    /** Pentru butonul "Trimite rezumatul acum" din aplicatie. Ruleaza pe un fir separat. Intoarce cate placi au raspuns. */
    static int sendNow(Context c) {
        int n = 0;
        for (Boards.Board b : Boards.load(c)) {
            try {
                String t = AlertChecker.getAny(c, b, "/noapte");
                if (t == null) continue;
                JSONObject j = new JSONObject(t);
                if (j.optInt("ok", 0) != 1) {   // placa n-a prins inca o noapte intreaga: aratam noaptea in curs, daca e
                    String t2 = AlertChecker.getAny(c, b, "/noapte?acum=1");
                    if (t2 != null) j = new JSONObject(t2);
                }
                n++;
                if (j.optInt("ok", 0) == 1) send(c, b, j);
                else {
                    ensureChannel(c);
                    post(c, ("rez" + b.url).hashCode(), "☀️ Noaptea la " + b.name,
                            "Placa nu are încă o noapte întreagă (22:00–08:00). Primul rezumat vine mâine dimineață.", b, Ui.GREEN, CHANNEL);
                }
            } catch (Exception ignored) {
            }
        }
        return n;
    }
}
