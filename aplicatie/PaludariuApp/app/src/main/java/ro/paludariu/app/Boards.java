package ro.paludariu.app;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

/** Placile salvate pe telefon (nume + adresa), pastrate intre porniri. */
final class Boards {
    static final class Board {
        final String name;
        final String url;

        Board(String name, String url) {
            this.name = name;
            this.url = url;
        }
    }

    private static final String PREFS = "paludariu";
    private static final String KEY = "boards";

    private Boards() {}

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** Adauga http:// daca lipseste si scoate "/" de la final. Intoarce "" daca adresa e goala. */
    static String normalize(String u) {
        if (u == null) return "";
        u = u.trim();
        if (u.isEmpty()) return "";
        if (!u.toLowerCase().startsWith("http://") && !u.toLowerCase().startsWith("https://")) u = "http://" + u;
        while (u.endsWith("/")) u = u.substring(0, u.length() - 1);
        return u;
    }

    static String shortUrl(String u) {
        return u.replaceFirst("^https?://", "");
    }

    static List<Board> load(Context c) {
        List<Board> out = new ArrayList<>();
        String s = prefs(c).getString(KEY, "");
        for (String line : s.split("\n")) {
            int t = line.indexOf('\t');
            if (t > 0 && t < line.length() - 1) out.add(new Board(line.substring(0, t), line.substring(t + 1)));
        }
        return out;
    }

    private static void store(Context c, List<Board> list) {
        StringBuilder sb = new StringBuilder();
        for (Board b : list) {
            if (sb.length() > 0) sb.append('\n');
            sb.append(b.name.replace('\t', ' ').replace('\n', ' ')).append('\t').append(b.url);
        }
        prefs(c).edit().putString(KEY, sb.toString()).apply();
    }

    /**
     * Salveaza sau actualizeaza o placa. Daca exista deja una cu acelasi nume (de ex. si-a schimbat IP-ul)
     * sau cu aceeasi adresa, o actualizeaza in loc sa o dubleze.
     */
    static void save(Context c, String name, String url) {
        url = normalize(url);
        if (url.isEmpty()) return;
        if (name == null || name.trim().isEmpty()) name = shortUrl(url);
        name = name.trim();
        List<Board> list = load(c);
        for (int i = 0; i < list.size(); i++) {
            Board b = list.get(i);
            if (b.name.equals(name) || b.url.equalsIgnoreCase(url)) {
                list.set(i, new Board(name, url));
                store(c, list);
                return;
            }
        }
        list.add(new Board(name, url));
        store(c, list);
    }

    /** Ultima placa deschisa: aplicatia o deschide direct dupa animatia de pornire. */
    static void setLast(Context c, String url) {
        prefs(c).edit().putString("last", normalize(url)).apply();
    }

    /** Ultima placa deschisa, daca e inca in lista celor salvate; altfel null. */
    static Board last(Context c) {
        String u = prefs(c).getString("last", "");
        if (u.isEmpty()) return null;
        for (Board b : load(c)) if (b.url.equalsIgnoreCase(u)) return b;
        return null;
    }

    static void remove(Context c, String url) {
        if (normalize(url).equalsIgnoreCase(prefs(c).getString("last", ""))) prefs(c).edit().remove("last").apply();
        List<Board> list = load(c);
        for (int i = list.size() - 1; i >= 0; i--) {
            if (list.get(i).url.equalsIgnoreCase(url)) list.remove(i);
        }
        store(c, list);
    }
}
