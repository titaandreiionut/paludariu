package ro.paludariu.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Base64;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Citeste starea placilor si trimite notificari cand apare o problema. */
final class AlertChecker {
    static final String CHANNEL = "alerte";
    static final String PREF_ON = "notif_on";
    private static final int JOB_ID = 4201;
    private static final long PERIOD_MS = 15 * 60 * 1000L;   // minimul permis de Android

    private AlertChecker() {}

    // ---------------- pornire / oprire ----------------

    static boolean enabled(Context c) {
        return Boards.prefs(c).getBoolean(PREF_ON, false);
    }

    static void setEnabled(Context c, boolean on) {
        Boards.prefs(c).edit().putBoolean(PREF_ON, on).apply();
        JobScheduler js = (JobScheduler) c.getSystemService(Context.JOB_SCHEDULER_SERVICE);
        if (js == null) return;
        if (on) {
            ensureChannel(c);
            JobInfo job = new JobInfo.Builder(JOB_ID, new ComponentName(c, AlertJob.class))
                    .setPeriodic(PERIOD_MS)
                    // doar pe WiFi; daca ai acces de la distanta, pe orice retea
                    .setRequiredNetworkType(Remote.anyConfigured(c) ? JobInfo.NETWORK_TYPE_ANY : JobInfo.NETWORK_TYPE_UNMETERED)
                    .setPersisted(true)                                       // si dupa repornirea telefonului
                    .build();
            js.schedule(job);
        } else {
            js.cancel(JOB_ID);
        }
    }

    /** Dupa ce s-a pornit sau oprit accesul de la distanta, reprogramam verificarea (alt tip de retea). */
    static void reschedule(Context c) {
        if (enabled(c)) setEnabled(c, true);
    }

    // ---------------- verificarea ----------------

    /** Ruleaza pe un fir separat (nu pe ecran). Intoarce cate placi au raspuns. */
    static int checkAll(Context c) {
        int ok = 0;
        for (Boards.Board b : Boards.load(c)) {
            try {
                String t = getAny(c, b, "/s");
                if (t == null) continue;
                JSONObject d = new JSONObject(t);
                ok++;
                JSONObject s = d.getJSONObject("s");
                JSONObject led = d.optJSONObject("led");
                Map<String, String> alerts = AlertRules.evaluate(
                        d.isNull("t") ? null : (float) d.getDouble("t"),
                        d.isNull("h") ? null : (float) d.getDouble("h"),
                        (float) s.getDouble("mistOn"),
                        (float) s.getDouble("fanOn"),
                        led == null || led.isNull("t") ? null : (float) led.getDouble("t"),
                        led != null && led.optInt("oh", 0) == 1,
                        led != null && led.optInt("ff", 0) == 1);
                // mementourile de intretinere (placa 1.7+): o notificare cand le vine termenul
                org.json.JSONArray mem = d.optJSONArray("mem");
                if (mem != null) {
                    for (int k = 0; k < mem.length(); k++) {
                        JSONObject m = mem.optJSONObject(k);
                        if (k == 1 || k == 4) continue;   // 1.8.9: hranirea are calendarul ei, pompa merge dupa ore
                        if (m == null || m.isNull("r") || m.optInt("z", 0) <= 0) continue;
                        long r = m.optLong("r", 1);
                        if (r <= 0) alerts.put("mem" + k, AlertRules.reminder(m.optString("n", "întreținere"), r));
                    }
                }
                // intretinerea dupa ore de mers (placa 1.8.9): [minute, limita in ore, secunde]
                org.json.JSONArray ore = d.optJSONArray("ore");
                String[] oreN = {"Curăță membrana de ceață", "Curăță pompa și sita", "Șterge praful de pe ventilatorul lămpii"};
                if (ore != null) {
                    for (int k = 0; k < ore.length() && k < oreN.length; k++) {
                        org.json.JSONArray o = ore.optJSONArray(k);
                        if (o == null || o.optInt(1, 0) <= 0) continue;
                        if (o.optLong(0, 0) >= o.optLong(1, 0) * 60L)
                            alerts.put("ore" + k, oreN[k] + ": a mers " + (o.optLong(0, 0) / 60) + " de ore (limita: " + o.optInt(1) + ").");
                    }
                }
                notifyNew(c, b, alerts);
                Widget.store(c, b.url, b.name, d);
            } catch (Exception ignored) {
                // placa oprita, alta retea WiFi sau parola gresita: nu trimitem nimic
            }
        }
        Widget.updateAll(c);
        try { Rezumat.check(c); } catch (Exception ignored) { }      // 1.8.9: rezumatul de dimineata
        try { Copii.auto(c); } catch (Exception ignored) { }         // 1.8.9: copia de duminica
        checkAppUpdate(c, 12 * 3600_000L);
        return ok;
    }

    /** Intai direct, in reteaua de acasa; daca nu merge si placa are acces de la distanta, prin internet. */
    static String getAny(Context c, Boards.Board b, String path) {
        try {
            String t = get(c, b.url, path);
            if (t != null) return t;
        } catch (Exception ignored) {
        }
        Remote.Cfg rc = Remote.load(c, b.name);
        if (!rc.ok()) return null;
        try {
            Remote.Resp r = Remote.once(rc, path);
            return r.code == 200 ? r.body : null;
        } catch (Exception e) {
            return null;
        }
    }

    // ---------------- versiune noua a aplicatiei, pe GitHub ----------------

    /** Daca pe GitHub e o versiune mai noua a aplicatiei, o tinem minte si anuntam.
     *  In fundal cel mult o data la 12 ore; cand deschizi aplicatia, cel mult o data la 10 minute. */
    static void checkAppUpdate(Context c, long minMs) {
        SharedPreferences p = Boards.prefs(c);
        String repo = p.getString("gh_repo", "");
        long now = System.currentTimeMillis();
        if (repo.indexOf('/') <= 0 || now - p.getLong("app_upd_t", 0) < minMs) return;
        p.edit().putLong("app_upd_t", now).apply();
        HttpURLConnection con = null;
        try {
            con = (HttpURLConnection) new URL("https://api.github.com/repos/" + repo + "/releases/latest").openConnection();
            con.setConnectTimeout(8000);
            con.setReadTimeout(8000);
            con.setRequestProperty("Accept", "application/vnd.github+json");
            if (con.getResponseCode() != 200) return;
            InputStream in = con.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            JSONObject j = new JSONObject(out.toString("UTF-8"));
            String tag = j.optString("tag_name", "");
            String page = j.optString("html_url", "https://github.com/" + repo + "/releases");
            boolean apk = out.toString("UTF-8").contains(".apk\"");
            String mine = "";
            try { mine = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName; } catch (Exception ignored) { }
            if (!apk || compare(tag, mine) <= 0) { p.edit().remove("app_upd_tag").apply(); return; }
            boolean nou = !tag.equals(p.getString("app_upd_tag", ""));
            p.edit().putString("app_upd_tag", tag).putString("app_upd_url", page).apply();
            if (nou && enabled(c)) showUrl(c, 9, "Paludariu " + tag + " e gata",
                    "Atinge ca s-o descarci de pe GitHub, sau actualizeaz-o din Obtainium.", page);
        } catch (Exception ignored) {
        } finally {
            if (con != null) con.disconnect();
        }
    }

    /** "v1.8.2" fata de "1.8": >0 daca a e mai noua */
    static int compare(String a, String b) {
        String[] x = a.replaceAll("[^0-9.]", "").split("\\."), y = b.replaceAll("[^0-9.]", "").split("\\.");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int u = 0, v = 0;
            try { if (i < x.length) u = Integer.parseInt(x[i]); } catch (NumberFormatException ignored) { }
            try { if (i < y.length) v = Integer.parseInt(y[i]); } catch (NumberFormatException ignored) { }
            if (u != v) return u > v ? 1 : -1;
        }
        return 0;
    }

    static JSONObject fetchStatus(Context c, String base) throws Exception {
        String t = get(c, base, "/s");
        return t == null ? null : new JSONObject(t);
    }

    /** Cerere GET catre placa, cu parola salvata pentru ea. Intoarce textul sau null. */
    static String get(Context c, String base, String path) throws Exception {
        URL url = new URL(base + path);
        HttpURLConnection con = (HttpURLConnection) url.openConnection();
        con.setConnectTimeout(4000);
        con.setReadTimeout(4000);
        SharedPreferences p = Boards.prefs(c);
        String user = p.getString("user_" + url.getHost(), null);
        String pass = p.getString("pass_" + url.getHost(), null);
        if (user != null && pass != null) {
            String tok = Base64.encodeToString((user + ":" + pass).getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
            con.setRequestProperty("Authorization", "Basic " + tok);
        }
        try {
            if (con.getResponseCode() != 200) return null;
            InputStream in = con.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[2048];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toString("UTF-8");
        } finally {
            con.disconnect();
        }
    }

    /** Notifica doar problemele noi; o problema rezolvata poate fi anuntata din nou data viitoare. */
    private static void notifyNew(Context c, Boards.Board b, Map<String, String> alerts) {
        SharedPreferences p = Boards.prefs(c);
        String key = "alerte_" + b.url;
        Set<String> before = new HashSet<>(Arrays.asList(p.getString(key, "").split(",")));
        for (Map.Entry<String, String> e : alerts.entrySet()) {
            if (!before.contains(e.getKey())) show(c, (b.url + e.getKey()).hashCode(), b.name, e.getValue(), b);
        }
        p.edit().putString(key, String.join(",", alerts.keySet())).apply();
    }

    // ---------------- notificari ----------------

    static void ensureChannel(Context c) {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Alerte paludariu", NotificationManager.IMPORTANCE_HIGH);
            ch.setDescription("Umiditate, temperatură, senzori și radiatorul LED");
            nm.createNotificationChannel(ch);
        }
    }

    /** Notificare care deschide o pagina web (versiunea noua de pe GitHub). */
    @SuppressWarnings("deprecation")
    static void showUrl(Context c, int id, String title, String text, String url) {
        ensureChannel(c);
        Intent i = new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(url));
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent pi = PendingIntent.getActivity(c, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder nb = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CHANNEL) : new Notification.Builder(c);
        nb.setSmallIcon(R.drawable.ic_stat_crab).setColor(Ui.GREEN).setContentTitle(title).setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text)).setContentIntent(pi).setAutoCancel(true);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.notify(id, nb.build());
    }

    @SuppressWarnings("deprecation")
    static void show(Context c, int id, String title, String text, Boards.Board open) {
        ensureChannel(c);
        Intent i;
        if (open != null) {
            i = new Intent(c, BoardActivity.class);
            i.putExtra(BoardActivity.EXTRA_URL, open.url);
            i.putExtra(BoardActivity.EXTRA_NAME, open.name);
        } else {
            i = new Intent(c, MainActivity.class);
        }
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent pi = PendingIntent.getActivity(c, id, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder nb = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(c, CHANNEL) : new Notification.Builder(c);
        nb.setSmallIcon(R.drawable.ic_stat_crab)
                .setColor(Ui.GREEN)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setContentIntent(pi)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT < 26) nb.setPriority(Notification.PRIORITY_HIGH);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.notify(id, nb.build());
    }
}
