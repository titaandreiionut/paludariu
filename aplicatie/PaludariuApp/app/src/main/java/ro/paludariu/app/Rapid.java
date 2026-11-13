package ro.paludariu.app;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

/**
 * Butoanele rapide (1.8.9): din widgetul cu 4 butoane sau din meniul iconitei (tii apasat pe Paludariu).
 * Nu deschide nimic pe ecran: trimite comanda la placa (acasa direct, altfel prin internet) si arata un mesaj scurt.
 * Ploaie acum · Ceata 5 min · Lumina 1 ora · Vant 10 min; dupa timpul ales, aparatul revine singur pe automat.
 */
public class Rapid extends Activity {
    static final String EXTRA_A = "a";

    static final String[][] ACT = {
            // cheie, cererea catre placa, mesajul cand merge
            {"ploaie", "/pl?c=start", "🌧️ Plouă acum"},
            {"ceata", "/m?i=2&v=1&min=5", "☁️ Ceața merge 5 minute"},
            {"lumina", "/m?i=1&v=1&min=60", "💡 Lumina e aprinsă o oră"},
            {"vant", "/m?i=3&v=1&min=10", "🌀 Ventilatorul merge 10 minute"}};

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        String a = getIntent().getStringExtra(EXTRA_A);
        run(getApplicationContext(), a);
        finish();
        overridePendingTransition(0, 0);
    }

    static void run(Context c, String a) {
        String[] act = null;
        for (String[] x : ACT) if (x[0].equals(a)) act = x;
        Boards.Board b = Boards.last(c);
        if (b == null && !Boards.load(c).isEmpty()) b = Boards.load(c).get(0);
        Handler ui = new Handler(Looper.getMainLooper());
        if (act == null) return;
        if (b == null) { Toast.makeText(c, "Adaugă întâi placa în aplicația Paludariu.", Toast.LENGTH_LONG).show(); return; }
        final String[] fa = act;
        final Boards.Board fb = b;
        Toast.makeText(c, "Trimit la " + b.name + "…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String r = AlertChecker.getAny(c, fb, fa[1]);
            String msg = r != null ? fa[2] + " · " + fb.name
                    : "Placa nu a răspuns (sau ai acces doar să privești). Încearcă din aplicație.";
            ui.post(() -> Toast.makeText(c, msg, Toast.LENGTH_LONG).show());
            if (r != null) {
                // starea noua si in widgetul mare
                try {
                    String s = AlertChecker.getAny(c, fb, "/s");
                    if (s != null) { Widget.store(c, fb.url, fb.name, new org.json.JSONObject(s)); Widget.updateAll(c); }
                } catch (Exception ignored) { }
            }
        }).start();
    }
}
