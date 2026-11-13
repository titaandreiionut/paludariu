package ro.paludariu.app;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.List;

/**
 * Copia de rezerva automata a setarilor (1.8.9): in fiecare duminica telefonul ia setarile placii (/cfg)
 * si le pastreaza; tine ultimele 8. Copia nu are parolele WiFi si Telegram (placa nu le pune in /cfg).
 * Fisierele stau in memoria aplicatiei: copii_<placa>/<cand>_<a|m>.json (a = automata, m = de mana).
 */
final class Copii {
    static final int PASTREAZA = 8;
    private Copii() {}

    static String safe(String board) { return board == null ? "placa" : board.replaceAll("[^A-Za-z0-9.-]", "_"); }

    static File dir(Context c, String board) {
        File d = new File(c.getFilesDir(), "copii_" + safe(board));
        if (!d.exists()) d.mkdirs();
        return d;
    }

    static boolean on(Context c, String board) { return Boards.prefs(c).getBoolean("bk_on_" + board, true); }
    static void setOn(Context c, String board, boolean v) { Boards.prefs(c).edit().putBoolean("bk_on_" + board, v).apply(); }

    static File[] list(Context c, String board) {
        File[] f = dir(c, board).listFiles((d, n) -> n.endsWith(".json"));
        if (f == null) return new File[0];
        Arrays.sort(f, (a, b) -> b.getName().compareTo(a.getName()));   // cele mai noi primele (numele incep cu ora)
        return f;
    }

    static long time(File f) {
        try { return Long.parseLong(f.getName().substring(0, f.getName().indexOf('_'))); } catch (Exception e) { return f.lastModified(); }
    }

    static boolean save(Context c, String board, String json, boolean auto) {
        try {
            JSONObject j = new JSONObject(json);
            if (!"paludariu-setari".equals(j.optString("tip"))) return false;
        } catch (Exception e) {
            return false;
        }
        File f = new File(dir(c, board), String.format("%013d_%s.json", System.currentTimeMillis(), auto ? "a" : "m"));
        try (FileOutputStream o = new FileOutputStream(f)) {
            o.write(json.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            return false;
        }
        File[] all = list(c, board);
        for (int i = PASTREAZA; i < all.length; i++) all[i].delete();
        return true;
    }

    static String read(Context c, String board, String name) {
        if (name == null || name.contains("/") || name.contains("..")) return null;
        File f = new File(dir(c, board), name);
        if (!f.exists()) return null;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int n = 0;
            while (n < b.length) { int r = in.read(b, n, b.length - n); if (r < 0) break; n += r; }
            return new String(b, 0, n, StandardCharsets.UTF_8);
        } catch (Exception e) {
            return null;
        }
    }

    /** Pentru pagina: {"on":1,"l":[{"t":ms,"v":"1.8.9","f":"nume","a":1}, ...]} */
    static String info(Context c, String board) {
        JSONObject o = new JSONObject();
        try {
            o.put("on", on(c, board) ? 1 : 0);
            JSONArray l = new JSONArray();
            for (File f : list(c, board)) {
                JSONObject e = new JSONObject();
                e.put("t", time(f));
                e.put("f", f.getName());
                e.put("a", f.getName().endsWith("_a.json") ? 1 : 0);
                String v = "";
                try { v = new JSONObject(read(c, board, f.getName())).optString("v", ""); } catch (Exception ignored) { }
                e.put("v", v);
                l.put(e);
            }
            o.put("l", l);
        } catch (Exception ignored) {
        }
        return o.toString();
    }

    /** Din verificarea de la 15 minute: duminica (sau daca duminica a fost ratata, dupa 7 zile) ia o copie. */
    static void auto(Context c) {
        Calendar now = Calendar.getInstance();
        boolean duminica = now.get(Calendar.DAY_OF_WEEK) == Calendar.SUNDAY;
        Calendar zi = (Calendar) now.clone();
        zi.set(Calendar.HOUR_OF_DAY, 0); zi.set(Calendar.MINUTE, 0); zi.set(Calendar.SECOND, 0); zi.set(Calendar.MILLISECOND, 0);
        for (Boards.Board b : Boards.load(c)) {
            if (!on(c, b.name)) continue;
            long last = 0;
            for (File f : list(c, b.name)) if (f.getName().endsWith("_a.json")) { last = time(f); break; }
            boolean deFacut = duminica ? last < zi.getTimeInMillis() : System.currentTimeMillis() - last > 8 * 86400_000L;
            if (!deFacut) continue;
            String t = AlertChecker.getAny(c, b, "/cfg");
            if (t != null) save(c, b.name, t, true);
        }
    }

    static List<String> names(Context c, String board) {
        List<String> out = new ArrayList<>();
        for (File f : list(c, board)) out.add(f.getName());
        return out;
    }
}
