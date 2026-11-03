package ro.paludariu.app;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Map;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * Accesul de la distanta: cererile paginii trec printr-un server MQTT (ex. EMQX), criptate cap-coada.
 * Mesaj = nonce (12 octeti) | text criptat | eticheta (16), AES-256-GCM, cu numele canalului ca date asociate.
 * Cheia o stiu doar placa si aplicatia; serverul nu poate citi sau modifica nimic.
 */
final class Remote {
    private Remote() {}

    static final SecureRandom RND = new SecureRandom();

    /** Setarile unei placi, tinute in aplicatie (nu in backup-ul Google). */
    static final class Cfg {
        String host = "", user = "", pass = "", keyHex = "", id = "";
        boolean ok() { return !host.isEmpty() && !user.isEmpty() && keyHex.length() == 64 && !id.isEmpty(); }
    }

    private static String k(String board, String f) { return "rm_" + board + "_" + f; }

    static Cfg load(Context c, String board) {
        Cfg g = new Cfg();
        if (board == null) return g;
        SharedPreferences p = Boards.prefs(c);
        g.host = p.getString(k(board, "host"), "");
        g.user = p.getString(k(board, "user"), "");
        g.pass = p.getString(k(board, "pass"), "");
        g.keyHex = p.getString(k(board, "key"), "");
        g.id = p.getString(k(board, "id"), "");
        return g;
    }

    static void save(Context c, String board, Cfg g) {
        Boards.prefs(c).edit().putString(k(board, "host"), g.host).putString(k(board, "user"), g.user)
                .putString(k(board, "pass"), g.pass).putString(k(board, "key"), g.keyHex).putString(k(board, "id"), g.id).apply();
    }

    static void remove(Context c, String board) {
        Boards.prefs(c).edit().remove(k(board, "host")).remove(k(board, "user")).remove(k(board, "pass"))
                .remove(k(board, "key")).remove(k(board, "id")).apply();
    }

    static boolean anyConfigured(Context c) {
        for (Boards.Board b : Boards.load(c)) if (load(c, b.name).ok()) return true;
        return false;
    }

    static String newKeyHex() {
        byte[] k = new byte[32];
        RND.nextBytes(k);
        return hex(k);
    }

    static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format("%02x", x & 0xff));
        return s.toString();
    }

    static byte[] unhex(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
        return b;
    }

    // ---------- criptarea ----------

    static byte[] encrypt(byte[] key, byte[] plain, String aad) throws Exception {
        byte[] nonce = new byte[12];
        RND.nextBytes(nonce);
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, nonce));
        c.updateAAD(aad.getBytes(StandardCharsets.UTF_8));
        byte[] ct = c.doFinal(plain);   // text criptat + eticheta
        byte[] out = new byte[12 + ct.length];
        System.arraycopy(nonce, 0, out, 0, 12);
        System.arraycopy(ct, 0, out, 12, ct.length);
        return out;
    }

    static byte[] decrypt(byte[] key, byte[] msg, String aad) throws Exception {
        if (msg.length < 12 + 16) throw new IOException("mesaj prea scurt");
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, msg, 0, 12));
        c.updateAAD(aad.getBytes(StandardCharsets.UTF_8));
        return c.doFinal(msg, 12, msg.length - 12);
    }

    // ---------- raspunsul placii ----------

    static final class Resp {
        int code;
        String type;
        String body;
    }

    /** O legatura deschisa cu serverul MQTT, pentru o placa. Se pot trimite mai multe cereri in paralel. */
    static final class Session implements MiniMqtt.Listener {
        private final Cfg cfg;
        private final byte[] key;
        private final String tCer, tRasp, tOn;
        private final MiniMqtt m = new MiniMqtt();
        private final Map<String, Resp[]> waiting = new HashMap<>();
        volatile Boolean boardOn = null;   // ce a anuntat placa la server: pornita / disparuta
        volatile boolean closed = false;

        Session(Cfg cfg) {
            this.cfg = cfg;
            this.key = unhex(cfg.keyHex);
            tCer = "pal/" + cfg.id + "/cer";
            tRasp = "pal/" + cfg.id + "/rasp";
            tOn = "pal/" + cfg.id + "/on";
        }

        void open() throws IOException {
            byte[] r = new byte[4];
            RND.nextBytes(r);
            m.connect(cfg.host, "app-" + hex(r), cfg.user, cfg.pass, 60, this);
            m.subscribe(tRasp);
            m.subscribe(tOn);
        }

        boolean isOpen() { return m.isOpen() && !closed; }

        Resp request(String path, long timeoutMs) throws IOException {
            byte[] r = new byte[8];
            RND.nextBytes(r);
            String n = hex(r);
            String plain = "{\"n\":\"" + n + "\",\"t\":" + (System.currentTimeMillis() / 1000) + ",\"p\":\""
                    + path.replace("\\", "\\\\").replace("\"", "\\\"") + "\"}";
            Resp[] slot = new Resp[1];
            synchronized (waiting) { waiting.put(n, slot); }
            try {
                m.publish(tCer, encrypt(key, plain.getBytes(StandardCharsets.UTF_8), tCer), false);
                long end = System.currentTimeMillis() + timeoutMs;
                synchronized (slot) {
                    while (slot[0] == null && !closed) {
                        long left = end - System.currentTimeMillis();
                        if (left <= 0) break;
                        slot.wait(left);
                    }
                }
            } catch (IOException e) {
                throw e;
            } catch (Exception e) {
                throw new IOException(e);
            } finally {
                synchronized (waiting) { waiting.remove(n); }
            }
            if (slot[0] == null) throw new IOException(Boolean.FALSE.equals(boardOn)
                    ? "Placa nu e conectată la internet." : "Placa nu a răspuns prin internet.");
            return slot[0];
        }

        @Override
        public void onMessage(String topic, byte[] payload) {
            if (topic.equals(tOn)) {
                boardOn = payload.length > 0 && payload[0] == '1';
                return;
            }
            if (!topic.equals(tRasp)) return;
            try {
                String s = new String(decrypt(key, payload, tRasp), StandardCharsets.UTF_8);
                int nl = s.indexOf('\n');
                JSONObject h = new JSONObject(nl < 0 ? s : s.substring(0, nl));
                Resp r = new Resp();
                r.code = h.optInt("c", 200);
                r.type = h.optString("y", "text/plain");
                r.body = nl < 0 ? "" : s.substring(nl + 1);
                Resp[] slot;
                synchronized (waiting) { slot = waiting.get(h.optString("n", "")); }
                if (slot == null) return;
                synchronized (slot) { slot[0] = r; slot.notifyAll(); }
            } catch (Exception ignored) {
                // nu e pentru noi sau a fost modificat pe drum: il ignoram
            }
        }

        @Override
        public void onClosed(Exception why) {
            closed = true;
            synchronized (waiting) {
                for (Resp[] slot : waiting.values()) synchronized (slot) { slot.notifyAll(); }
            }
        }

        void close() {
            closed = true;
            m.disconnect();
        }
    }

    /** O singura cerere: deschide legatura, intreaba, inchide. Pentru widget si notificari. */
    static Resp once(Cfg cfg, String path) throws IOException {
        Session s = new Session(cfg);
        try {
            s.open();
            return s.request(path, 12000);
        } finally {
            s.close();
        }
    }
}
