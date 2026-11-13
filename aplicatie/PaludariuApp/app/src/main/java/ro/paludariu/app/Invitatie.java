package ro.paludariu.app;

import android.content.Context;
import android.util.Base64;

import org.json.JSONObject;

import java.nio.charset.StandardCharsets;

/**
 * Codul de invitatie pentru "doar privit" (1.8.9).
 * Proprietarul il face din pagina placii (Setari > Acces de la distanta > Doar privit > Trimite invitatia);
 * familia il lipeste in aplicatia lor, la "Am un cod de invitatie".
 * Codul = "PALV1:" + JSON in base64: numele si adresa placii, parola "doar privit" si, daca exista acces
 * de la distanta, serverul, contul telefonului si a doua cheie (cu ea placa raspunde doar la citire).
 */
final class Invitatie {
    static final String PREFIX = "PALV1:";
    private Invitatie() {}

    static String encode(String name, String url, String viewPass, Remote.Cfg rc, String key2) throws Exception {
        JSONObject j = new JSONObject();
        j.put("n", name);
        j.put("u", url);
        j.put("p", viewPass);
        if (rc != null && rc.ok() && key2 != null && key2.length() == 64) {
            j.put("h", rc.host);
            j.put("mu", rc.user);
            j.put("mp", rc.pass);
            j.put("i", rc.id);
            j.put("k", key2);
        }
        return PREFIX + Base64.encodeToString(j.toString().getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP | Base64.URL_SAFE);
    }

    /** Gaseste codul intr-un text lipit (poate fi tot mesajul primit) si il citeste. Intoarce null daca nu e bun. */
    static JSONObject decode(String text) {
        if (text == null) return null;
        int a = text.indexOf(PREFIX);
        if (a < 0) return null;
        int b = a + PREFIX.length();
        int e = b;
        while (e < text.length() && "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=".indexOf(text.charAt(e)) >= 0) e++;
        try {
            JSONObject j = new JSONObject(new String(Base64.decode(text.substring(b, e), Base64.URL_SAFE), StandardCharsets.UTF_8));
            if (j.optString("u").isEmpty() || j.optString("p").isEmpty()) return null;
            return j;
        } catch (Exception ex) {
            return null;
        }
    }

    /** Salveaza placa din invitatie: adresa, utilizatorul "privit" cu parola si, daca e, accesul prin internet. */
    static String apply(Context c, JSONObject j) {
        String name = j.optString("n", "Paludariu");
        String url = Boards.normalize(j.optString("u"));
        Boards.save(c, name, url);
        String host = android.net.Uri.parse(url).getHost();
        if (host != null) Boards.prefs(c).edit().putString("user_" + host, "privit").putString("pass_" + host, j.optString("p")).apply();
        if (!j.optString("k").isEmpty()) {
            Remote.Cfg rc = new Remote.Cfg();
            rc.host = j.optString("h"); rc.user = j.optString("mu"); rc.pass = j.optString("mp");
            rc.keyHex = j.optString("k"); rc.id = j.optString("i");
            Remote.save(c, name, rc);
            AlertChecker.reschedule(c);
        }
        return name;
    }
}
