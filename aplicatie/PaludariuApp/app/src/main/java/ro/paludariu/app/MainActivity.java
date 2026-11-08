package ro.paludariu.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.os.Build;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Ecranul de start: placile gasite automat in WiFi si cele salvate. */
public class MainActivity extends Activity {
    private static final String SERVICE_TYPE = "_paludariu._tcp.";

    private NsdManager nsd;
    private NsdManager.DiscoveryListener discovery;
    private final Map<String, Boards.Board> found = new LinkedHashMap<>();
    private final ArrayDeque<NsdServiceInfo> resolveQueue = new ArrayDeque<>();
    private boolean resolving = false;

    private LinearLayout foundBox, savedBox;
    private TextView foundHint;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Ui.darkBars(this);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Ui.BG);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = Ui.dp(this, 20);
        root.setPadding(pad, Ui.dp(this, 28), pad, Ui.dp(this, 32));
        scroll.addView(root);

        // Antet: crabul pe piatra + "Paludariu"
        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.logo_header);   // aceeasi iconita, cu colturi rotunde
        int ls = Ui.dp(this, 56);
        head.addView(logo, new LinearLayout.LayoutParams(ls, ls));
        LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        names.setPadding(Ui.dp(this, 14), 0, 0, 0);
        names.addView(Ui.title(this, "Paludariu", 30));
        names.addView(Ui.text(this, "Alege placa pe care vrei s-o deschizi", 14, Ui.MUTED, false));
        head.addView(names);

        // Semnatura, sus in dreapta (subtire si aerisita, ca Josefin Sans de pe pagina placii)
        TextView sig = Ui.text(this, "by Andrei T", 13, Ui.MUTED, false);
        sig.setTypeface(Typeface.create("sans-serif-light", Typeface.NORMAL));
        sig.setLetterSpacing(0.18f);
        sig.setGravity(Gravity.END);
        root.addView(sig, Ui.wide(this, 0));
        root.addView(head, Ui.wide(this, 6));

        // Placi gasite automat
        root.addView(section("În rețea", 30));
        foundHint = Ui.text(this, "", 14, Ui.MUTED, false);
        root.addView(foundHint, Ui.wide(this, 4));
        foundBox = new LinearLayout(this);
        foundBox.setOrientation(LinearLayout.VERTICAL);
        root.addView(foundBox, Ui.wide(this, 4));

        // Placi salvate
        root.addView(section("Salvate", 26));
        savedBox = new LinearLayout(this);
        savedBox.setOrientation(LinearLayout.VERTICAL);
        root.addView(savedBox, Ui.wide(this, 4));

        Button add = Ui.primary(this, "Adaugă o placă");
        add.setOnClickListener(v -> new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Adaugă o placă")
                .setItems(new String[]{"Placă nouă: o conectez la WiFi", "Am deja placa în rețea (după adresă)"}, (d, w) -> {
                    if (w == 0) startActivity(new Intent(this, SetupActivity.class));
                    else showAddDialog();
                })
                .show());
        root.addView(add, Ui.wide(this, 18));

        TextView hint = Ui.text(this,
                "Telefonul trebuie să fie pe același WiFi cu placa. Ține apăsat pe o placă salvată ca s-o ștergi.",
                13, Ui.MUTED, false);
        root.addView(hint, Ui.wide(this, 14));

        // Notificari
        root.addView(section("Notificări", 30));
        notifStatus = Ui.text(this, "", 14, Ui.MUTED, false);
        root.addView(notifStatus, Ui.wide(this, 4));
        notifBtn = Ui.primary(this, "");
        notifBtn.setOnClickListener(v -> toggleNotifications());
        root.addView(notifBtn, Ui.wide(this, 12));
        testBtn = Ui.secondary(this, "Trimite o notificare de test");
        testBtn.setOnClickListener(v -> sendTest());
        root.addView(testBtn, Ui.wide(this, 10));

        // Sunete
        root.addView(section("Sunete", 30));
        Sounds snd = Sounds.get(this);
        root.addView(switchRow("Sunete la butoane", "La pornire, la comenzi, la salvare și la glisarea paginilor",
                snd.buttonsOn(), on -> { snd.setButtons(on); if (on) snd.ui("click"); }), Ui.wide(this, 6));
        TextView volLbl = Ui.text(this, "", 14, Ui.MUTED, false);
        SeekBar vol = new SeekBar(this);
        vol.setMax(100);
        vol.setProgress(snd.volume());
        volLbl.setText("Volumul păsărilor: " + snd.volume() + "%");
        root.addView(switchRow("Păsări în fundal", "Ciripesc încet, din când în când, cât timp ești în aplicație",
                snd.birdsOn(), on -> {
                    snd.setBirds(on);
                    vol.setEnabled(on);
                    volLbl.setAlpha(on ? 1f : 0.5f);
                }), Ui.wide(this, 10));
        vol.setEnabled(snd.birdsOn());
        volLbl.setAlpha(snd.birdsOn() ? 1f : 0.5f);
        vol.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar b, int v, boolean user) {
                volLbl.setText("Volumul păsărilor: " + v + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar b) {}
            @Override public void onStopTrackingTouch(SeekBar b) { snd.setVolume(b.getProgress()); }
        });
        LinearLayout.LayoutParams vl = Ui.wide(this, 14);
        vl.leftMargin = Ui.dp(this, 4);
        root.addView(volLbl, vl);
        root.addView(vol, Ui.wide(this, 4));

        // Versiune noua a aplicatiei pe GitHub (o verificam in fundal, cel mult de doua ori pe zi)
        updBtn = Ui.primary(this, "");
        updBtn.setVisibility(View.GONE);
        updBtn.setOnClickListener(v -> {
            String u = Boards.prefs(this).getString("app_upd_url", "");
            if (!u.isEmpty()) try { startActivity(new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(u))); } catch (Exception ignored) { }
        });
        root.addView(updBtn, Ui.wide(this, 26));

        // Subsol: versiunea aplicatiei si anul, ca pe pagina placii
        String ver = "";
        try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception ignored) { }
        TextView ft = Ui.text(this, "Paludariu v" + ver + " © 2026 Andrei T", 12, Ui.MUTED, false);
        ft.setTypeface(android.graphics.Typeface.create("sans-serif-light", android.graphics.Typeface.NORMAL));
        ft.setLetterSpacing(0.12f);
        ft.setGravity(android.view.Gravity.CENTER);
        ft.setAlpha(0.75f);
        LinearLayout.LayoutParams fl = Ui.wide(this, 32);
        fl.bottomMargin = Ui.dp(this, 8);
        root.addView(ft, fl);

        // Animatia de pornire: doar la prima deschidere a aplicatiei, nu la fiecare intoarcere pe ecranul asta
        android.widget.FrameLayout frame = new android.widget.FrameLayout(this);
        frame.setBackgroundColor(Ui.BG);
        frame.addView(scroll);
        setContentView(frame);
        if (state == null && !splashShown) {
            splashShown = true;
            snd.appStart();
            // Daca ai mai deschis o placa, dupa animatie mergem direct la ea. Inapoi aduce aici, la lista.
            Boards.Board last = Boards.last(this);
            showSplash(frame, last);
        }
    }

    private interface OnToggle { void set(boolean on); }

    /** Un rand cu titlu, explicatie si comutator, pe card in forma de frunza. */
    private View switchRow(String title, String sub, boolean on, OnToggle t) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.HORIZONTAL);
        c.setGravity(Gravity.CENTER_VERTICAL);
        c.setBackground(Ui.leaf(this));
        int p = Ui.dp(this, 16);
        c.setPadding(p, Ui.dp(this, 12), Ui.dp(this, 10), Ui.dp(this, 12));
        LinearLayout txt = new LinearLayout(this);
        txt.setOrientation(LinearLayout.VERTICAL);
        txt.addView(Ui.text(this, title, 16, Ui.TXT, true));
        txt.addView(Ui.text(this, sub, 13, Ui.MUTED, false));
        c.addView(txt, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        Switch sw = new Switch(this);
        sw.setChecked(on);
        sw.setOnCheckedChangeListener((b, v) -> t.set(v));
        c.addView(sw);
        c.setClickable(true);
        c.setOnClickListener(v -> sw.toggle());
        return c;
    }

    private Button updBtn;

    private void renderUpdate() {
        String tag = Boards.prefs(this).getString("app_upd_tag", "");
        updBtn.setText("Versiune nouă a aplicației: " + tag + " — descarc-o");
        updBtn.setVisibility(tag.isEmpty() ? View.GONE : View.VISIBLE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        renderUpdate();
        new Thread(() -> {
            AlertChecker.checkAppUpdate(getApplicationContext(), 10 * 60_000L);
            runOnUiThread(this::renderUpdate);
        }).start();
        Sounds.get(this).resumed();
        renderSaved();
        renderNotif();
        startDiscovery();
    }

    // ---------------- notificari ----------------

    private static final int REQ_NOTIF = 7;
    private static final String PERM_NOTIF = "android.permission.POST_NOTIFICATIONS";
    private TextView notifStatus;
    private Button notifBtn, testBtn;

    private boolean canNotify() {
        return Build.VERSION.SDK_INT < 33 || checkSelfPermission(PERM_NOTIF) == PackageManager.PERMISSION_GRANTED;
    }

    private void renderNotif() {
        boolean on = AlertChecker.enabled(this) && canNotify();
        notifStatus.setText(on
                ? "Pornite. Telefonul verifică plăcile salvate cam la fiecare 15 minute, când e pe WiFi, și te anunță dacă "
                  + "umiditatea sau temperatura ies din limite, dacă un senzor nu răspunde, dacă radiatorul LED se supraîncălzește "
                  + "sau dacă ventilatorul lui se oprește. Tot aici primești mementourile de întreținere."
                : "Oprite. Pornește-le ca să afli de probleme fără să deschizi aplicația.");
        notifBtn.setText(on ? "Oprește notificările" : "Pornește notificările");
        testBtn.setVisibility(on ? View.VISIBLE : View.GONE);
    }

    private void toggleNotifications() {
        if (AlertChecker.enabled(this) && canNotify()) {
            AlertChecker.setEnabled(this, false);
            renderNotif();
            return;
        }
        if (!canNotify()) {
            requestPermissions(new String[]{PERM_NOTIF}, REQ_NOTIF);   // raspunsul vine in onRequestPermissionsResult
            return;
        }
        AlertChecker.setEnabled(this, true);
        renderNotif();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code != REQ_NOTIF) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
            AlertChecker.setEnabled(this, true);
        } else {
            notifStatus.setText("Telefonul nu a permis notificările. Le poți porni din Setări > Aplicații > Paludariu > Notificări.");
            return;
        }
        renderNotif();
    }

    /** Verifica placile acum si trimite o notificare cu rezultatul. */
    private void sendTest() {
        testBtn.setEnabled(false);
        testBtn.setText("Verific plăcile…");
        new Thread(() -> {
            int ok = AlertChecker.checkAll(getApplicationContext());
            int total = Boards.load(this).size();
            String msg = ok > 0
                    ? "Notificările funcționează. Au răspuns " + ok + " din " + total + " plăci salvate."
                    : "Notificările funcționează, dar nicio placă salvată n-a răspuns. Deschide o dată fiecare placă în aplicație, ca să-i salvezi parola.";
            AlertChecker.show(getApplicationContext(), 1, "Paludariu", msg, null);
            runOnUiThread(() -> {
                testBtn.setEnabled(true);
                testBtn.setText("Trimite o notificare de test");
            });
        }).start();
    }

    @Override
    protected void onPause() {
        stopDiscovery();
        Sounds.get(this).paused();
        super.onPause();
    }

    // ---------------- descoperire automata (mDNS / NSD) ----------------

    private void startDiscovery() {
        found.clear();
        synchronized (resolveQueue) {
            resolveQueue.clear();
            resolving = false;
        }
        foundHint.setText("Caut plăci în rețea…");
        renderFound();
        nsd = (NsdManager) getSystemService(NSD_SERVICE);
        if (nsd == null) {
            foundHint.setText("Căutarea automată nu e disponibilă pe acest telefon. Adaugă placa după adresă.");
            return;
        }
        discovery = new NsdManager.DiscoveryListener() {
            @Override public void onDiscoveryStarted(String type) {}
            @Override public void onDiscoveryStopped(String type) {}

            @Override public void onServiceFound(NsdServiceInfo info) {
                if (info.getServiceType() != null && info.getServiceType().contains("_paludariu")) enqueue(info);
            }

            @Override public void onServiceLost(NsdServiceInfo info) {
                runOnUiThread(() -> {
                    found.remove(info.getServiceName());
                    renderFound();
                });
            }

            @Override public void onStartDiscoveryFailed(String type, int error) {
                runOnUiThread(() -> foundHint.setText(
                        "Căutarea automată nu a pornit (cod " + error + "). Adaugă placa după adresă."));
            }

            @Override public void onStopDiscoveryFailed(String type, int error) {}
        };
        try {
            nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discovery);
        } catch (RuntimeException e) {
            foundHint.setText("Căutarea automată nu a pornit. Adaugă placa după adresă.");
            discovery = null;
        }
    }

    private void stopDiscovery() {
        if (nsd != null && discovery != null) {
            try {
                nsd.stopServiceDiscovery(discovery);
            } catch (RuntimeException ignored) {
            }
        }
        discovery = null;
    }

    private void enqueue(NsdServiceInfo info) {
        synchronized (resolveQueue) {
            resolveQueue.add(info);
        }
        resolveNext();
    }

    /** NsdManager rezolva un singur serviciu odata, asa ca le luam pe rand. */
    @SuppressWarnings("deprecation")
    private void resolveNext() {
        NsdServiceInfo next;
        synchronized (resolveQueue) {
            if (resolving || resolveQueue.isEmpty() || nsd == null) return;
            resolving = true;
            next = resolveQueue.poll();
        }
        try {
            nsd.resolveService(next, new NsdManager.ResolveListener() {
                @Override public void onResolveFailed(NsdServiceInfo info, int error) {
                    resolved();
                }

                @Override public void onServiceResolved(NsdServiceInfo info) {
                    InetAddress host = info.getHost();
                    if (host instanceof Inet4Address) {
                        String url = "http://" + host.getHostAddress() + (info.getPort() == 80 ? "" : ":" + info.getPort());
                        Boards.Board b = new Boards.Board(info.getServiceName(), url);
                        runOnUiThread(() -> {
                            found.put(b.name, b);
                            renderFound();
                        });
                    }
                    resolved();
                }
            });
        } catch (RuntimeException e) {
            resolved();
        }
    }

    private void resolved() {
        synchronized (resolveQueue) {
            resolving = false;
        }
        resolveNext();
    }

    // ---------------- afisare ----------------

    private TextView section(String s, int topDp) {
        TextView t = Ui.text(this, s, 15, Ui.MUTED, true);
        t.setLayoutParams(Ui.wide(this, topDp));
        return t;
    }

    private void renderFound() {
        foundBox.removeAllViews();
        if (found.isEmpty()) {
            foundHint.setVisibility(View.VISIBLE);
        } else {
            foundHint.setVisibility(View.GONE);
            for (Boards.Board b : found.values()) foundBox.addView(card(b, false), Ui.wide(this, 10));
        }
    }

    private void renderSaved() {
        savedBox.removeAllViews();
        List<Boards.Board> list = Boards.load(this);
        if (list.isEmpty()) {
            savedBox.addView(Ui.text(this, "Nicio placă salvată încă. Placa pe care o deschizi se salvează singură.",
                    14, Ui.MUTED, false), Ui.wide(this, 4));
            return;
        }
        for (Boards.Board b : list) savedBox.addView(card(b, true), Ui.wide(this, 10));
    }

    private View card(Boards.Board b, boolean saved) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(Ui.leaf(this));
        int p = Ui.dp(this, 16);
        c.setPadding(p, p, p, p);
        c.addView(Ui.text(this, b.name, 18, Ui.TXT, true));
        c.addView(Ui.text(this, Boards.shortUrl(b.url), 14, Ui.MUTED, false));
        c.setClickable(true);
        c.setFocusable(true);
        c.setOnClickListener(v -> open(b));
        if (saved) {
            c.setOnLongClickListener(v -> {
                confirmDelete(b);
                return true;
            });
        }
        return c;
    }

    private void open(Boards.Board b) {
        Boards.save(this, b.name, b.url);
        Intent i = new Intent(this, BoardActivity.class);
        i.putExtra(BoardActivity.EXTRA_URL, b.url);
        i.putExtra(BoardActivity.EXTRA_NAME, b.name);
        startActivity(i);
    }

    private void confirmDelete(Boards.Board b) {
        new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Ștergi „" + b.name + "”?")
                .setMessage("Placa dispare doar din listă. Dacă e în rețea, o găsești din nou automat.")
                .setPositiveButton("Șterge", (d, w) -> {
                    Boards.remove(this, b.url);
                    renderSaved();
                })
                .setNegativeButton("Renunță", null)
                .show();
    }

    private static boolean splashShown = false;

    @SuppressWarnings("deprecation")
    private void showSplash(android.widget.FrameLayout frame, Boards.Board last) {
        android.webkit.WebView sp = new android.webkit.WebView(this);
        sp.setBackgroundColor(Ui.BG);
        sp.getSettings().setJavaScriptEnabled(false);
        frame.addView(sp, new android.widget.FrameLayout.LayoutParams(-1, -1));
        sp.loadDataWithBaseURL(null, SPLASH, "text/html", "utf-8", null);
        if (last != null) {
            // ultima placa se deschide peste animatie, cu o trecere lina; lista ramane dedesubt pentru butonul Inapoi
            sp.postDelayed(() -> {
                if (isFinishing()) return;
                open(last);
                overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
            }, 2050);
            sp.postDelayed(() -> { frame.removeView(sp); sp.destroy(); }, 2900);
            return;
        }
        sp.postDelayed(() -> sp.animate().alpha(0f).scaleX(1.05f).scaleY(1.05f).setDuration(350)
                .withEndAction(() -> { frame.removeView(sp); sp.destroy(); }).start(), 2100);
    }

    private static final String SPLASH = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><link href=\"https://fonts.googleapis.com/css2?family=Fraunces:opsz,wght@9..144,600&family=Josefin+Sans:wght@300&display=swap\" rel=\"stylesheet\"><style> :root{--crab:#d98a5f;--rock:#5d7566;--leaf:#86cf8e;--b1:#1f3d2c;--b2:#0b1610;--t:#e8f1e7;--m:#93a99a;--cy:#5cc6c0} *{box-sizing:border-box} html,body{margin:0;height:100%;background:var(--b2);overflow:hidden;font-family:Figtree,system-ui,sans-serif} /* ecranul de pornire */ #sp{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;background:radial-gradient(120% 70% at 50% 40%,#244a35,var(--b2) 70%);z-index:5} #sp.out{animation:spOut .45s ease-in forwards} @keyframes spOut{to{opacity:0;transform:scale(1.06)}} .lg{width:150px;height:122px;overflow:visible} .rock{transform-box:fill-box;transform-origin:50% 100%;animation:rock .5s cubic-bezier(.2,1.4,.4,1) both} @keyframes rock{from{transform:scaleY(0)}to{transform:scaleY(1)}} .leaf{transform-box:fill-box;transform-origin:0% 100%;animation:leaf .45s .35s cubic-bezier(.2,1.5,.4,1) both} @keyframes leaf{from{transform:scale(0) rotate(-40deg)}to{transform:scale(1) rotate(0)}} .crab{animation:pop .55s .45s cubic-bezier(.2,1.6,.4,1) both} @keyframes pop{0%{transform:translateY(26px);opacity:0}60%{opacity:1}100%{transform:translateY(0);opacity:1}} .claw{transform-box:fill-box;animation:snap .32s 1.05s ease-in-out 2}.cl{transform-origin:100% 100%}.cr{transform-origin:0 100%;animation-delay:1.12s} @keyframes snap{50%{transform:rotate(-18deg)}} .eye{transform-box:fill-box;transform-origin:center;animation:blink .25s 1.35s ease-in-out} @keyframes blink{50%{transform:scaleY(.1)}} .bub{fill:none;stroke:var(--cy);stroke-width:1.4;opacity:0;animation:bub 1.3s ease-out both} @keyframes bub{0%{transform:translateY(0);opacity:0}20%{opacity:.9}100%{transform:translateY(-46px);opacity:0}} h1{margin:18px 0 0;font:600 34px Fraunces,Georgia,serif;color:var(--t);letter-spacing:-.3px;animation:up .5s .75s ease-out both} .by{margin-top:8px;font:300 15px \"Josefin Sans\",system-ui,sans-serif;letter-spacing:3px;color:var(--m);animation:up .5s .95s ease-out both} @keyframes up{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}} .ld{margin-top:26px;width:120px;height:3px;border-radius:2px;background:rgba(190,230,200,.14);overflow:hidden;animation:up .4s .9s ease-out both} .ld i{display:block;height:100%;width:0;background:var(--leaf);border-radius:2px;animation:ld 1.1s .9s cubic-bezier(.4,0,.2,1) forwards} @keyframes ld{to{width:100%}} </style></head><body><div id=\"sp\"> <svg class=\"lg\" viewBox=\"0 0 64 52\" aria-hidden=\"true\"> <g transform=\"translate(0 0)\"> <circle class=\"bub\" cx=\"14\" cy=\"34\" r=\"2.2\" style=\"animation-delay:.6s\"/><circle class=\"bub\" cx=\"50\" cy=\"30\" r=\"1.6\" style=\"animation-delay:.85s\"/><circle class=\"bub\" cx=\"20\" cy=\"26\" r=\"1.3\" style=\"animation-delay:1.1s\"/> <path class=\"rock\" d=\"M5 49C4 39 11 32 21 32C26 25 39 25 45 31C53 32 58 40 56 49Z\" fill=\"var(--rock)\" opacity=\".9\"/> <path class=\"rock\" d=\"M17 38C21 36 25 37 27 39M38 36C42 35 46 37 47 40\" fill=\"none\" stroke=\"#0b1610\" stroke-width=\"1.6\" stroke-linecap=\"round\" opacity=\".45\"/> <g class=\"leaf\"><path d=\"M50 33C49 25 53 19 59 17C60 25 56 31 50 33Z\" fill=\"var(--leaf)\"/><path d=\"M50 33C53 28 56 23 59 17\" fill=\"none\" stroke=\"#0b1610\" stroke-width=\"1\" opacity=\".45\"/></g> <g class=\"crab\"><g transform=\"translate(9 3) scale(.72)\"> <path d=\"M21 24L11 29L7 35M22 26L15 32L13 38M25 28L20 34L19 39M43 24L53 29L57 35M42 26L49 32L51 38M39 28L44 34L45 39\" fill=\"none\" stroke=\"var(--crab)\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/> <g class=\"claw cl\"><path d=\"M23 20L15 13\" fill=\"none\" stroke=\"var(--crab)\" stroke-width=\"2.8\" stroke-linecap=\"round\"/><path d=\"M15 13C9 11 7 5 10 2C12 5 14 6 17 6C16 8 16 11 15 13Z\" fill=\"var(--crab)\"/></g> <g class=\"claw cr\"><path d=\"M41 20L49 13\" fill=\"none\" stroke=\"var(--crab)\" stroke-width=\"2.8\" stroke-linecap=\"round\"/><path d=\"M49 13C55 11 57 5 54 2C52 5 50 6 47 6C48 8 48 11 49 13Z\" fill=\"var(--crab)\"/></g> <path d=\"M28 15L27 9M36 15L37 9\" fill=\"none\" stroke=\"var(--crab)\" stroke-width=\"2.6\" stroke-linecap=\"round\"/> <ellipse cx=\"32\" cy=\"22\" rx=\"13\" ry=\"9\" fill=\"var(--crab)\"/> <circle cx=\"27\" cy=\"8\" r=\"2.6\" fill=\"var(--crab)\"/><circle cx=\"37\" cy=\"8\" r=\"2.6\" fill=\"var(--crab)\"/> <g class=\"eye\"><circle cx=\"27\" cy=\"7.6\" r=\"1.2\" fill=\"#0b1610\"/><circle cx=\"37\" cy=\"7.6\" r=\"1.2\" fill=\"#0b1610\"/></g> </g></g> </g> </svg> <h1>Paludariu</h1> <div class=\"by\">by Andrei T</div> <div class=\"ld\"><i></i></div> </div></body></html>";

    private void showAddDialog() {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 20);
        form.setPadding(p, Ui.dp(this, 8), p, 0);
        EditText name = Ui.input(this, "Nume, de exemplu Paludariu 2");
        EditText addr = Ui.input(this, "Adresă, de exemplu 192.168.1.51");
        TextView err = Ui.text(this, "", 13, Ui.BAD, false);
        form.addView(name, Ui.wide(this, 0));
        form.addView(addr, Ui.wide(this, 10));
        form.addView(err, Ui.wide(this, 6));

        AlertDialog d = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Adaugă o placă")
                .setView(form)
                .setPositiveButton("Adaugă", null)
                .setNegativeButton("Renunță", null)
                .create();
        d.setOnShowListener(x -> d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String url = Boards.normalize(addr.getText().toString());
            if (url.isEmpty()) {
                err.setText("Scrie adresa plăcii. O găsești în Serial Monitor când pornește placa.");
                addr.requestFocus();
                return;
            }
            Boards.save(this, name.getText().toString(), url);
            renderSaved();
            d.dismiss();
        }));
        d.show();
    }
}
