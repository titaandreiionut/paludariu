package ro.paludariu.app;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.List;

/**
 * "Placa noua": aplicatia se conecteaza la reteaua Paludariu-Setup a placii, ii trimite WiFi-ul de acasa,
 * asteapta sa se conecteze si o salveaza in lista.
 */
public class SetupActivity extends Activity {
    static final String AP = "Paludariu-Setup";
    static final String AP_PASS_IMPLICIT = "paludariu";
    /** Parola retelei Paludariu-Setup: cea obisnuita, sau alta daca ai schimbat-o in config.h (SETUP_PAROLA). */
    private String apPass() { return Boards.prefs(this).getString("setup_pass", AP_PASS_IMPLICIT); }
    static final String BASE = "http://192.168.4.1";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LinearLayout box;
    private ConnectivityManager cm;
    private volatile Network net;
    private ConnectivityManager.NetworkCallback cb;
    private volatile boolean alive = true;
    private String ssid = "";
    private boolean secured = true;
    private String boardName = "Paludariu";
    private TextView[] stepIcon = new TextView[4];
    private TextView[] stepText = new TextView[4];
    private LinearLayout finBox;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Ui.darkBars(this);
        cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Ui.BG);
        box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = Ui.dp(this, 20);
        box.setPadding(pad, Ui.dp(this, 22), pad, Ui.dp(this, 28));
        scroll.addView(box);
        setContentView(scroll);
        boardName = "Paludariu " + (Boards.load(this).size() + 1);
        showIntro();
    }

    @Override
    protected void onDestroy() {
        alive = false;
        release();
        super.onDestroy();
    }

    // ---------------- bucati de interfata ----------------

    private void page(int step) {
        box.removeAllViews();
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        Button back = Ui.secondary(this, "‹");
        back.setMinHeight(0);
        back.setMinWidth(0);
        back.setPadding(0, 0, 0, Ui.dp(this, 2));
        back.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        back.setOnClickListener(v -> finish());
        int b = Ui.dp(this, 40);
        bar.addView(back, new LinearLayout.LayoutParams(b, b));
        TextView t = Ui.title(this, "Placă nouă", 22);
        LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tl.leftMargin = Ui.dp(this, 12);
        bar.addView(t, tl);
        box.addView(bar);
        LinearLayout steps = new LinearLayout(this);
        for (int i = 0; i < 4; i++) {
            View s = new View(this);
            s.setBackground(Ui.round(i < step ? Ui.GREEN : 0x24BEE6C8, 0, 2, this));
            LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(0, Ui.dp(this, 4), 1f);
            if (i > 0) sl.leftMargin = Ui.dp(this, 6);
            steps.addView(s, sl);
        }
        box.addView(steps, Ui.wide(this, 16));
    }

    private TextView h2(String s) {
        TextView t = Ui.title(this, s, 26);
        box.addView(t, Ui.wide(this, 18));
        return t;
    }

    private TextView para(String s) {
        TextView t = Ui.text(this, s, 15, Ui.MUTED, false);
        t.setLineSpacing(0, 1.15f);
        box.addView(t, Ui.wide(this, 6));
        return t;
    }

    private Button primary(String s, View.OnClickListener l, int top) {
        Button b = Ui.primary(this, s);
        b.setOnClickListener(l);
        box.addView(b, Ui.wide(this, top));
        return b;
    }

    private Button secondary(String s, View.OnClickListener l, int top) {
        Button b = Ui.secondary(this, s);
        b.setOnClickListener(l);
        box.addView(b, Ui.wide(this, top));
        return b;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(Ui.leaf(this));
        int p = Ui.dp(this, 16);
        c.setPadding(p, Ui.dp(this, 10), p, Ui.dp(this, 10));
        box.addView(c, Ui.wide(this, 14));
        return c;
    }

    private void wait(String title, String text) {
        page(2);
        TextView spin = Ui.text(this, "◌", 44, Ui.GREEN, false);
        spin.setGravity(Gravity.CENTER);
        spin.animate().rotationBy(36000).setDuration(100000).setInterpolator(new android.view.animation.LinearInterpolator()).start();
        box.addView(spin, Ui.wide(this, 48));
        TextView t = Ui.title(this, title, 24);
        t.setGravity(Gravity.CENTER);
        box.addView(t, Ui.wide(this, 18));
        TextView p = Ui.text(this, text, 15, Ui.MUTED, false);
        p.setGravity(Gravity.CENTER);
        box.addView(p, Ui.wide(this, 6));
    }

    private void showError(String title, String text) {
        release();
        page(2);
        h2(title);
        para(text);
        primary("Încearcă din nou", v -> askAndConnect(), 24);
        secondary("Am schimbat parola rețelei „" + AP + "”", v -> askApPass(), 10);
        secondary("Înapoi", v -> finish(), 10);
    }

    /** Daca in config.h ai pus SETUP_PAROLA, aplicatia trebuie sa stie parola noua. */
    private void askApPass() {
        android.widget.EditText e = Ui.input(this, "Parola din SETUP_PAROLA");
        e.setText(apPass());
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        int pd = Ui.dp(this, 20);
        f.setPadding(pd, Ui.dp(this, 8), pd, 0);
        f.addView(e, Ui.wide(this, 0));
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Parola rețelei „" + AP + "”")
                .setMessage("Implicit e „" + AP_PASS_IMPLICIT + "”. Dacă ai pus alta în config.h, la SETUP_PAROLA, scrie-o aici.")
                .setView(f)
                .setPositiveButton("Salvează", (d, w) -> {
                    String v = e.getText().toString();
                    Boards.prefs(this).edit().putString("setup_pass", v.length() >= 8 ? v : AP_PASS_IMPLICIT).apply();
                    askAndConnect();
                })
                .setNegativeButton("Renunță", null)
                .show();
    }

    // ---------------- pasul 1: explicatii ----------------

    private void showIntro() {
        page(1);
        h2("Conectăm placa la WiFi");
        para("Placa nouă nu știe încă rețeaua ta. O configurăm de aici, fără să umbli în cod.");
        LinearLayout c = card();
        String[] st = {
                "1.  Pornește placa din priză. Dacă n-are WiFi, își face singură rețeaua „" + AP + "”.",
                "2.  Ține telefonul lângă placă.",
                "3.  Apasă butonul de jos. Aplicația se conectează singură la placă."};
        for (String s : st) {
            TextView t = Ui.text(this, s, 15, Ui.TXT, false);
            t.setLineSpacing(0, 1.15f);
            c.addView(t, Ui.wide(this, 6));
        }
        para("Ca s-o configurezi din nou mai târziu (alt router): Setări → „Schimbă rețeaua WiFi”, sau ține apăsat 5 secunde butonul BOOT de pe placă.");
        primary("Caută placa", v -> askAndConnect(), 24);
    }

    // ---------------- pasul 2: conectarea la Paludariu-Setup ----------------

    private void askAndConnect() {
        String perm = Build.VERSION.SDK_INT >= 33 ? "android.permission.NEARBY_WIFI_DEVICES"
                : Build.VERSION.SDK_INT >= 29 ? "android.permission.ACCESS_FINE_LOCATION" : null;
        if (perm != null && checkSelfPermission(perm) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{perm}, 7);
            return;
        }
        connect();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] p, int[] r) {
        if (code == 7) connect();   // incercam oricum; Android spune singur daca refuza
    }

    private void connect() {
        release();
        if (Build.VERSION.SDK_INT >= 29) {
            wait("Caut placa…", "Mă conectez la rețeaua „" + AP + "”. Android te întreabă o dată dacă permiți.");
            WifiNetworkSpecifier spec = new WifiNetworkSpecifier.Builder().setSsid(AP).setWpa2Passphrase(apPass()).build();
            NetworkRequest req = new NetworkRequest.Builder()
                    .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                    .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .setNetworkSpecifier(spec)
                    .build();
            cb = new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network n) {
                    net = n;
                    ui.post(() -> scan());
                }

                @Override
                public void onUnavailable() {
                    ui.post(() -> showError("Nu am găsit placa",
                            "Verifică dacă placa e pornită și aproape de telefon. Dacă e deja conectată la alt WiFi, n-are rețeaua „" + AP + "”: ține apăsat 5 secunde butonul BOOT."));
                }

                @Override
                public void onLost(Network n) {
                    if (n.equals(net)) net = null;
                }
            };
            try {
                cm.requestNetwork(req, cb, 40000);
            } catch (Exception e) {
                showError("Nu pot porni căutarea", "Android a refuzat cererea: " + e.getMessage());
            }
        } else {
            page(2);
            h2("Conectează-te la placă");
            para("Deschide Setări → WiFi pe telefon și alege rețeaua „" + AP + "”, cu parola „" + apPass() + "”. Apoi revino aici.");
            primary("M-am conectat", v -> {
                net = findWifi();
                if (net == null) showError("Nu văd rețeaua plăcii", "Telefonul nu pare conectat la „" + AP + "”.");
                else scan();
            }, 24);
        }
    }

    @SuppressWarnings("deprecation")
    private Network findWifi() {
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities c = cm.getNetworkCapabilities(n);
            if (c != null && c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return n;
        }
        return null;
    }

    private void release() {
        if (cb != null) {
            try { cm.unregisterNetworkCallback(cb); } catch (Exception ignored) { }
            cb = null;
        }
        net = null;
    }

    private String http(Network via, String url, int readMs) throws IOException {
        URL u = new URL(url);
        HttpURLConnection c = (HttpURLConnection) (via != null ? via.openConnection(u) : u.openConnection());
        c.setConnectTimeout(4000);
        c.setReadTimeout(readMs);
        try {
            if (c.getResponseCode() != 200) throw new IOException("HTTP " + c.getResponseCode());
            InputStream in = c.getInputStream();
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] b = new byte[2048];
            int r;
            while ((r = in.read(b)) > 0) o.write(b, 0, r);
            return o.toString("UTF-8");
        } finally {
            c.disconnect();
        }
    }

    private static String enc(String s) {
        try { return URLEncoder.encode(s, "UTF-8"); } catch (Exception e) { return s; }
    }

    // ---------------- pasul 3: retelele vazute de placa ----------------

    private static class Wifi { String s; int r; boolean l; }

    private void scan() {
        wait("Conectat la placă", "Placa caută rețelele din jur…");
        new Thread(() -> {
            List<Wifi> list = new ArrayList<>();
            String err = null;
            for (int k = 0; k < 2 && list.isEmpty(); k++) {
                try {
                    JSONArray a = new JSONObject(http(net, BASE + "/setup/scan", 12000)).getJSONArray("n");
                    for (int i = 0; i < a.length(); i++) {
                        JSONObject o = a.getJSONObject(i);
                        Wifi w = new Wifi();
                        w.s = o.getString("s");
                        w.r = o.getInt("r");
                        w.l = o.getInt("l") != 0;
                        list.add(w);
                    }
                    err = null;
                } catch (Exception e) {
                    err = e.getMessage();
                    try { Thread.sleep(1500); } catch (InterruptedException ignored) { }
                }
            }
            final String fe = err;
            ui.post(() -> {
                if (!alive) return;
                if (fe != null && list.isEmpty()) showError("Placa nu răspunde", "Conexiunea cu placa s-a întrerupt. Încearcă din nou.");
                else showNets(list);
            });
        }).start();
    }

    private static String bars(int rssi) {
        return rssi > -60 ? "▂▄▆█" : rssi > -70 ? "▂▄▆" : rssi > -80 ? "▂▄" : "▂";
    }

    private void showNets(List<Wifi> list) {
        page(3);
        h2("Alege WiFi-ul de acasă");
        para("Rețelele pe care le vede placa. Placa merge doar pe 2,4 GHz.");
        LinearLayout c = card();
        if (list.isEmpty()) c.addView(Ui.text(this, "Placa nu vede nicio rețea. Apropie-o de router și caută din nou.", 15, Ui.MUTED, false));
        for (Wifi w : list) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, Ui.dp(this, 12), 0, Ui.dp(this, 12));
            TextView sig = Ui.text(this, bars(w.r), 14, Ui.GREEN, true);
            row.addView(sig, new LinearLayout.LayoutParams(Ui.dp(this, 44), LinearLayout.LayoutParams.WRAP_CONTENT));
            TextView name = Ui.text(this, w.s, 16, Ui.TXT, true);
            row.addView(name, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            row.addView(Ui.text(this, w.l ? "🔒" : "deschisă", 13, Ui.MUTED, false));
            row.setOnClickListener(v -> showPass(w.s, w.l));
            c.addView(row);
        }
        TextView other = Ui.text(this, "Altă rețea (ascunsă)…", 15, Ui.GREEN, false);
        other.setPadding(0, Ui.dp(this, 12), 0, Ui.dp(this, 6));
        other.setOnClickListener(v -> showPass("", true));
        c.addView(other);
        secondary("Caută din nou", v -> scan(), 14);
    }

    // ---------------- parola si numele ----------------

    private void showPass(String s, boolean lock) {
        ssid = s;
        secured = lock;
        page(3);
        h2("Parola WiFi-ului");
        EditText ss = null;
        if (s.isEmpty()) {
            box.addView(Ui.text(this, "Numele rețelei", 14, Ui.MUTED, false), Ui.wide(this, 12));
            ss = Ui.input(this, "Numele exact al rețelei");
            box.addView(ss, Ui.wide(this, 4));
        } else {
            LinearLayout c = card();
            c.addView(Ui.text(this, "Rețea: " + s, 16, Ui.TXT, true));
        }
        box.addView(Ui.text(this, "Parola", 14, Ui.MUTED, false), Ui.wide(this, 12));
        EditText pw = Ui.input(this, lock ? "Parola rețelei" : "Rețea deschisă, fără parolă");
        pw.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        pw.setEnabled(lock);
        box.addView(pw, Ui.wide(this, 4));
        TextView show = Ui.text(this, "Arată parola", 14, Ui.GREEN, false);
        show.setPadding(0, Ui.dp(this, 8), 0, 0);
        show.setOnClickListener(v -> {
            boolean hidden = (pw.getInputType() & InputType.TYPE_TEXT_VARIATION_PASSWORD) == InputType.TYPE_TEXT_VARIATION_PASSWORD;
            pw.setInputType(InputType.TYPE_CLASS_TEXT | (hidden ? InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD : InputType.TYPE_TEXT_VARIATION_PASSWORD));
            pw.setTypeface(Typeface.DEFAULT);
            pw.setSelection(pw.getText().length());
            show.setText(hidden ? "Ascunde parola" : "Arată parola");
        });
        box.addView(show);
        box.addView(Ui.text(this, "Numele plăcii", 14, Ui.MUTED, false), Ui.wide(this, 14));
        EditText nm = Ui.input(this, "Paludariu");
        nm.setText(boardName);
        box.addView(nm, Ui.wide(this, 4));
        TextView err = Ui.text(this, "", 14, Ui.BAD, false);
        box.addView(err, Ui.wide(this, 8));
        final EditText fss = ss;
        primary("Conectează", v -> {
            String id = fss != null ? fss.getText().toString().trim() : ssid;
            String p = pw.getText().toString();
            if (id.isEmpty()) { err.setText("Scrie numele rețelei."); return; }
            if (secured && p.length() < 8) { err.setText("Parola WiFi are cel puțin 8 caractere."); return; }
            ssid = id;
            String n = nm.getText().toString().trim();
            boardName = n.isEmpty() ? "Paludariu" : n.replace("\"", "").replace("\\", "");
            send(p);
        }, 22);
    }

    // ---------------- pasul 4: trimiterea si asteptarea ----------------

    private void progress() {
        page(4);
        h2("Conectez placa…");
        String[] s = {"Trimit datele către placă", "Placa se conectează la " + ssid, "Caut placa în rețeaua de acasă", "Salvez placa în aplicație"};
        for (int i = 0; i < 4; i++) {
            LinearLayout row = new LinearLayout(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView ic = Ui.text(this, "", 13, Ui.MUTED, true);
            ic.setGravity(Gravity.CENTER);
            ic.setBackground(Ui.round(0, 0x33BEE6C8, 99, this));
            int d = Ui.dp(this, 26);
            row.addView(ic, new LinearLayout.LayoutParams(d, d));
            TextView t = Ui.text(this, s[i], 15, Ui.MUTED, false);
            LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            tl.leftMargin = Ui.dp(this, 12);
            row.addView(t, tl);
            box.addView(row, Ui.wide(this, i == 0 ? 18 : 12));
            stepIcon[i] = ic;
            stepText[i] = t;
        }
        finBox = new LinearLayout(this);
        finBox.setOrientation(LinearLayout.VERTICAL);
        box.addView(finBox, Ui.wide(this, 18));
    }

    /** 0 = in asteptare, 1 = acum, 2 = gata, 3 = n-a mers */
    private void step(int i, int st) {
        ui.post(() -> {
            if (!alive || stepIcon[i] == null) return;
            stepIcon[i].setText(st == 2 ? "✓" : st == 3 ? "!" : st == 1 ? "…" : "");
            stepIcon[i].setTextColor(st >= 2 ? Ui.ON_GREEN : Ui.TXT);
            stepIcon[i].setBackground(Ui.round(st == 2 ? Ui.GREEN : st == 3 ? Ui.BAD : 0, st == 1 ? Ui.GREEN : st == 0 ? 0x33BEE6C8 : 0, 99, this));
            stepText[i].setTextColor(st == 3 ? Ui.BAD : st == 0 ? Ui.MUTED : Ui.TXT);
        });
    }

    private void send(String pass) {
        progress();
        final String name = boardName;
        new Thread(() -> {
            step(0, 1);
            try {
                http(net, BASE + "/setup/save?ssid=" + enc(ssid) + "&pass=" + enc(pass) + "&nume=" + enc(name), 6000);
            } catch (Exception e) {
                step(0, 3);
                fail("Placa nu a primit datele", "Conexiunea cu placa s-a întrerupt. Încearcă din nou.", true);
                return;
            }
            step(0, 2);
            step(1, 1);
            String ip = null;
            int st = 1, errs = 0;
            long t0 = System.currentTimeMillis();
            while (alive && System.currentTimeMillis() - t0 < 32000) {
                try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
                try {
                    JSONObject o = new JSONObject(http(net, BASE + "/setup/status", 4000));
                    st = o.getInt("st");
                    errs = 0;
                    if (st == 2) { ip = o.optString("ip", null); break; }
                    if (st == 3) break;
                } catch (Exception e) {
                    if (++errs >= 6) break;   // telefonul a pierdut reteaua placii (se intampla cand placa schimba canalul)
                }
            }
            if (st == 3) {
                step(1, 3);
                fail("Parola nu s-a potrivit", "Sau rețeaua e prea departe de placă. Placa a rămas pe „" + AP + "”: verifică parola și încearcă din nou.", true);
                return;
            }
            if (ip != null) {
                try { http(net, BASE + "/setup/done", 3000); } catch (Exception ignored) { }
            }
            release();   // telefonul revine pe WiFi-ul de acasa
            step(1, 2);
            step(2, 1);
            String url = ip != null ? waitForBoard("http://" + ip, 45000) : findByName(name, 45000);
            if (url == null) {
                step(2, 3);
                fail("Nu am găsit placa în rețea", "Poate a mers, dar n-o văd încă. Verifică lista de plăci. Dacă nu apare, parola poate fi greșită: placa revine singură la „" + AP + "”.", false);
                return;
            }
            step(2, 2);
            step(3, 1);
            Boards.save(this, name, url);
            step(3, 2);
            final String fu = url;
            ui.post(() -> done(name, fu));
        }).start();
    }

    /** Placa reporneste dupa configurare: asteptam sa raspunda la noua adresa. */
    private String waitForBoard(String url, long ms) {
        long t0 = System.currentTimeMillis();
        while (alive && System.currentTimeMillis() - t0 < ms) {
            try {
                Thread.sleep(2000);
                http(null, url + "/manifest.json", 3000);
                return url;
            } catch (Exception ignored) { }
        }
        return null;
    }

    /** Daca n-am apucat sa aflam adresa, cautam placa dupa nume in reteaua de acasa. */
    private String findByName(String name, long ms) {
        final String[] found = {null};
        NsdManager nsd = (NsdManager) getSystemService(Context.NSD_SERVICE);
        NsdManager.DiscoveryListener dl = new NsdManager.DiscoveryListener() {
            public void onStartDiscoveryFailed(String t, int e) { }
            public void onStopDiscoveryFailed(String t, int e) { }
            public void onDiscoveryStarted(String t) { }
            public void onDiscoveryStopped(String t) { }
            public void onServiceLost(NsdServiceInfo i) { }
            public void onServiceFound(NsdServiceInfo info) {
                if (!name.equals(info.getServiceName())) return;
                nsd.resolveService(info, new NsdManager.ResolveListener() {
                    public void onResolveFailed(NsdServiceInfo i, int e) { }
                    public void onServiceResolved(NsdServiceInfo i) {
                        InetAddress h = i.getHost();
                        if (h != null) found[0] = "http://" + h.getHostAddress() + (i.getPort() == 80 ? "" : ":" + i.getPort());
                    }
                });
            }
        };
        try { nsd.discoverServices("_paludariu._tcp", NsdManager.PROTOCOL_DNS_SD, dl); } catch (Exception e) { return null; }
        long t0 = System.currentTimeMillis();
        while (alive && found[0] == null && System.currentTimeMillis() - t0 < ms) {
            try { Thread.sleep(500); } catch (InterruptedException e) { break; }
        }
        try { nsd.stopServiceDiscovery(dl); } catch (Exception ignored) { }
        return found[0];
    }

    private void fail(String title, String text, boolean retry) {
        ui.post(() -> {
            if (!alive) return;
            finBox.removeAllViews();
            LinearLayout c = new LinearLayout(this);
            c.setOrientation(LinearLayout.VERTICAL);
            c.setBackground(Ui.leaf(this));
            int p = Ui.dp(this, 16);
            c.setPadding(p, p, p, p);
            c.addView(Ui.text(this, title, 17, Ui.TXT, true));
            TextView t = Ui.text(this, text, 15, Ui.MUTED, false);
            c.addView(t, Ui.wide(this, 6));
            finBox.addView(c);
            Button b = Ui.primary(this, retry ? "Încearcă din nou" : "Înapoi la listă");
            b.setOnClickListener(v -> {
                if (!retry) finish();
                else if (net != null) showPass(ssid, secured);
                else askAndConnect();
            });
            finBox.addView(b, Ui.wide(this, 16));
        });
    }

    private void done(String name, String url) {
        if (!alive) return;
        finBox.removeAllViews();
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.logo_header);
        int s = Ui.dp(this, 84);
        LinearLayout.LayoutParams ll = new LinearLayout.LayoutParams(s, s);
        ll.gravity = Gravity.CENTER_HORIZONTAL;
        finBox.addView(logo, ll);
        logo.setScaleX(.3f);
        logo.setScaleY(.3f);
        logo.animate().scaleX(1f).scaleY(1f).setDuration(450).setInterpolator(new android.view.animation.OvershootInterpolator()).start();
        TextView t = Ui.title(this, "Gata!", 26);
        t.setGravity(Gravity.CENTER);
        finBox.addView(t, Ui.wide(this, 10));
        TextView p = Ui.text(this, "„" + name + "” e conectată la " + ssid + " și a fost salvată în listă.", 15, Ui.MUTED, false);
        p.setGravity(Gravity.CENTER);
        finBox.addView(p, Ui.wide(this, 6));
        Button b = Ui.primary(this, "Deschide placa");
        b.setOnClickListener(v -> {
            Intent i = new Intent(this, BoardActivity.class);
            i.putExtra(BoardActivity.EXTRA_URL, url);
            i.putExtra(BoardActivity.EXTRA_NAME, name);
            startActivity(i);
            finish();
        });
        finBox.addView(b, Ui.wide(this, 20));
    }
}
