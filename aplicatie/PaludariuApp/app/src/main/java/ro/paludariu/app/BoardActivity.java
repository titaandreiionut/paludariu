package ro.paludariu.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.webkit.HttpAuthHandler;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.LinearLayout;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.Locale;

/**
 * Pagina unei placi, pe tot ecranul, fara bara browserului.
 * Cat timp placa raspunde, aplicatia pastreaza o copie a paginii si a ultimelor date.
 * Cand placa nu raspunde, arata copia aceea in modul offline: animatiile merg, comenzile nu.
 */
public class BoardActivity extends Activity {
    static final String EXTRA_URL = "url";
    static final String EXTRA_NAME = "name";
    static final String EXTRA_HASH = "hash";   // pagina de deschis (ex. "istoric", din rezumatul de dimineata)

    private WebView web;
    private volatile String url;
    private volatile String host;
    private SharedPreferences prefs;
    private boolean triedSaved = false;   // parola salvata a fost deja incercata pentru incarcarea curenta
    private volatile boolean offline = false;
    private volatile boolean lostCheck = false;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean checking = false;
    // acces de la distanta: placa nu e in reteaua ta, dar raspunde prin serverul MQTT (criptat cap-coada)
    private volatile boolean remote = false;
    private volatile Remote.Session session;
    private boolean decided = false;       // prima verificare a placii s-a terminat
    private boolean cachedShown = false;   // pe ecran e copia salvata
    private boolean bannerConnecting = true;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Ui.darkBars(this);
        url = getIntent().getStringExtra(EXTRA_URL);
        if (url == null) {
            finish();
            return;
        }
        host = Uri.parse(url).getHost();
        prefs = Boards.prefs(this);
        Boards.setLast(this, url);   // la urmatoarea pornire, aplicatia deschide direct placa asta

        web = new WebView(this);
        web.setBackgroundColor(Ui.BG);
        WebSettings ws = web.getSettings();
        ws.setJavaScriptEnabled(true);          // pagina placii e facuta in JavaScript
        ws.setDomStorageEnabled(true);          // tema aleasa si lista de placi din pagina
        ws.setUseWideViewPort(true);
        ws.setLoadWithOverviewMode(true);
        web.addJavascriptInterface(new Bridge(), "PaludariuApp");
        web.setWebViewClient(new Client());
        web.setWebChromeClient(new Chrome());   // pentru alegerea fisierului la actualizarea placii
        setContentView(web);

        // Intai verificam, in fundal si repede (3 s), daca placa raspunde. Acasa raspunde aproape imediat,
        // asa ca pagina live se deschide direct. Doar daca dureaza peste 1,2 s aratam copia salvata,
        // cu un mesaj discret "Se conectează…"; banda rosie "offline" apare numai cand placa chiar nu raspunde.
        offline = true;
        showConnecting();
        ui.postDelayed(() -> {
            if (!decided && !isFinishing() && hasCache()) showCached(true);
        }, 1200);
        Thread t = new Thread(() -> {
            boolean ok = boardAnswersHome() || relocate();
            boolean rem = !ok && tryRemote();
            ui.post(() -> {
                decided = true;
                if (isFinishing()) return;
                if (ok) goOnline();
                else if (rem) goRemote();
                else {
                    if (!hasCache()) showOffline();
                    else if (cachedShown) setBanner(false);
                    else showCached(false);
                    startChecking();
                }
            });
        });
        t.setDaemon(true);
        t.start();
        watchNetwork();
    }

    // ---------------- schimbarea retelei (WiFi <-> date mobile) ----------------
    private android.net.ConnectivityManager.NetworkCallback netCb;
    private volatile long netGen = 0;

    /** Cand telefonul trece intre WiFi si date mobile, alegem din nou drumul: direct (acasa) sau prin internet. */
    private void watchNetwork() {
        android.net.ConnectivityManager cm = (android.net.ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        if (cm == null) return;
        netCb = new android.net.ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(android.net.Network n) { netChanged(); }
            @Override public void onLost(android.net.Network n) { netChanged(); }
        };
        try { cm.registerDefaultNetworkCallback(netCb); } catch (Exception e) { netCb = null; }
    }

    private void netChanged() {
        final long g = ++netGen;
        // asteptam 2 s sa se aseze reteaua noua (adresa, ruta), apoi verificam o singura data
        ui.postDelayed(() -> { if (g == netGen && decided && !isFinishing()) recheck(); }, 2000);
    }

    private void recheck() {
        new Thread(() -> {
            if (boardAnswersHome() || relocate()) {      // esti acasa: legatura directa
                ui.post(() -> { if (offline || remote) goOnline(); });
                return;
            }
            boolean rem = tryRemote();                   // legatura veche prin internet era pe reteaua de dinainte
            ui.post(() -> {
                if (isFinishing()) return;
                if (rem) goRemote();
                else if (!offline || remote) goOffline();
            });
        }).start();
    }

    private static String appVersion = null;
    private String appVersion() {
        if (appVersion == null) {
            try { appVersion = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
            catch (Exception e) { appVersion = ""; }
        }
        return appVersion;
    }

    @Override
    protected void onResume() {
        super.onResume();
        Sounds.get(this).resumed();
        if (web != null) web.onResume();
        if (offline) startChecking();
    }

    @Override
    protected void onPause() {
        if (web != null) web.onPause();
        checking = false;
        Sounds.get(this).paused();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        checking = false;
        if (netCb != null) {
            try { ((android.net.ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE)).unregisterNetworkCallback(netCb); } catch (Exception ignored) { }
            netCb = null;
        }
        closeSession();
        if (web != null) web.destroy();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        // Inapoi duce direct la lista de placi. Daca e deschis meniul de alegere a placii din pagina, il inchide doar pe el.
        if (web == null) { super.onBackPressed(); return; }
        // pagina noua (1.8.7): __back inchide fereastra deschisa / submeniul / revine la Acasa; altfel, ca inainte
        web.evaluateJavascript("(function(){if(window.__back)return window.__back();var s=document.getElementById('sh');if(s&&!s.hidden&&window.closeSh){closeSh();return 1}return 0})()",
                v -> { if (!"1".equals(v)) finish(); });
    }

    // ---------------- copia locala (cate un set de fisiere pentru fiecare placa) ----------------

    private File cacheFile(String path) {
        String name = path.equals("/") ? "pagina.html" : path.substring(1) + ".json";
        File dir = new File(getFilesDir(), "offline_" + (host == null ? "placa" : host.replaceAll("[^A-Za-z0-9.-]", "_")));
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, name);
    }

    private void writeCache(String path, String text) {
        try (FileOutputStream o = new FileOutputStream(cacheFile(path))) {
            o.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
        if (path.equals("/s")) prefs.edit().putLong("offline_t_" + host, System.currentTimeMillis()).apply();
    }

    private String readCache(String path) {
        File f = cacheFile(path);
        if (!f.exists()) return null;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int n = 0;
            while (n < b.length) {
                int r = in.read(b, n, b.length - n);
                if (r < 0) break;
                n += r;
            }
            return new String(b, 0, n, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return null;
        }
    }

    /** Pagina salvata, fara bucatile pe care le adauga aplicatia (o copie veche le putea avea de mai multe ori). */
    private String cleanPage() {
        String html = readCache("/");
        if (html == null) return null;
        String[] marks = {"<style>#rmb{", "<style>#offb{"};
        boolean changed = false;
        for (String m : marks) {
            int a;
            while ((a = html.indexOf(m)) >= 0) {
                int b = html.indexOf("})();</script>", a);
                if (b < 0) break;
                html = html.substring(0, a) + html.substring(b + "})();</script>".length());
                changed = true;
            }
        }
        if (changed) writeCache("/", html);
        return html;
    }

    private boolean hasCache() {
        return cacheFile("/").exists() && cacheFile("/s").exists();
    }

    /** Legatura dintre pagina si aplicatie: pagina ii da datele primite, ca sa le pastreze. */
    private class Bridge {
        @JavascriptInterface
        public void save(String path, String text) {
            if ((offline && !remote) || text == null || text.isEmpty()) return;
            if (path.equals("/") && (remote || text.contains("id='rmb'") || text.contains("id='offb'"))) return;   // doar pagina curata, de la placa
            if (path.equals("/") || path.equals("/s") || path.equals("/h") || path.equals("/h30") || path.equals("/e")) writeCache(path, text);
            if (path.equals("/s")) widgetData(text);
        }

        @JavascriptInterface
        public void lost() {   // placa nu mai raspunde de cateva ori la rand
            if (remote) {
                if (lostCheck) return;
                lostCheck = true;
                // prin internet un raspuns poate intarzia: intai verificam legatura, abia apoi zicem "offline"
                new Thread(() -> {
                    Remote.Session s = session;
                    boolean ok = false;
                    try { ok = s != null && s.isOpen() && s.request("/s", 10000).code == 200; } catch (Exception ignored) { }
                    if (!ok && tryRemote()) ok = true;
                    final boolean fine = ok;
                    lostCheck = false;
                    ui.post(() -> {
                        if (!remote) return;
                        if (fine) web.evaluateJavascript("window.load&&load()", null);
                        else goOffline();
                    });
                }).start();
                return;
            }
            ui.post(() -> { if (!offline) goOffline(); });
        }

        /** Sunetul unui buton din pagina: on, off, auto, click, save, rain, swipe. */
        @JavascriptInterface
        public void sound(String name) {
            if (name != null) Sounds.get(BoardActivity.this).ui(name);
        }

        /** Butonul din pagina: Setari > Acces de la distanta. */
        @JavascriptInterface
        public void remoteSetup() {
            ui.post(BoardActivity.this::showRemoteSetup);
        }

        /** Copia de rezerva a setarilor: pagina ne da textul, noi il salvam unde alegi. */
        @JavascriptInterface
        public void saveFile(String name, String text) {
            ui.post(() -> {
                pendingSave = text;
                android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT);
                i.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                i.setType("application/json");
                i.putExtra(android.content.Intent.EXTRA_TITLE, name);
                try { startActivityForResult(i, REQ_SAVE); } catch (Exception e) { pendingSave = null; }
            });
        }

        @JavascriptInterface
        public void retry() {
            ui.post(BoardActivity.this::goOnline);
        }

        // ---------- 1.8.9: jurnalul foto (pozele stau in telefon) ----------
        @JavascriptInterface
        public String fotoList() { return Foto.list(BoardActivity.this, boardKey()); }

        @JavascriptInterface
        public String fotoImg(String id) { return Foto.image(BoardActivity.this, boardKey(), id); }

        @JavascriptInterface
        public void fotoDel(String id) { Foto.delete(BoardActivity.this, boardKey(), id); }

        @JavascriptInterface
        public void fotoAdd(String temp, String hum) { ui.post(() -> fotoPick(temp, hum)); }

        // ---------- 1.8.9: copiile automate ale setarilor ----------
        @JavascriptInterface
        public String bkInfo() { return Copii.info(BoardActivity.this, boardKey()); }

        @JavascriptInterface
        public void bkSet(int on) { Copii.setOn(BoardActivity.this, boardKey(), on != 0); }

        @JavascriptInterface
        public void bkSave(String json) { Copii.save(BoardActivity.this, boardKey(), json, false); }

        @JavascriptInterface
        public String bkGet(String name) { String t = Copii.read(BoardActivity.this, boardKey(), name); return t == null ? "" : t; }

        // ---------- 1.8.9: invitatia "doar privit" ----------
        @JavascriptInterface
        public void invite(String viewPass) { if (viewPass != null && !viewPass.isEmpty()) makeInvite(viewPass); }
    }

    // ---------------- jurnal foto: camera sau galerie ----------------
    private static final int REQ_FOTO = 13;
    private Uri camUri;
    private String fotoT, fotoH;

    private void fotoPick(String temp, String hum) {
        fotoT = temp; fotoH = hum;
        android.content.Intent gal = new android.content.Intent(android.content.Intent.ACTION_GET_CONTENT);
        gal.setType("image/*");
        gal.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        android.content.Intent ch = android.content.Intent.createChooser(gal, "Poză pentru jurnal");
        camUri = null;
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            // camera scrie poza direct in galerie (Pictures/Paludariu); de acolo o copiem in jurnal
            try {
                android.content.ContentValues v = new android.content.ContentValues();
                v.put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, "paludariu_" + System.currentTimeMillis() + ".jpg");
                v.put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/jpeg");
                v.put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Paludariu");
                camUri = getContentResolver().insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, v);
            } catch (Exception e) {
                camUri = null;
            }
            if (camUri != null) {
                android.content.Intent cam = new android.content.Intent(android.provider.MediaStore.ACTION_IMAGE_CAPTURE);
                cam.putExtra(android.provider.MediaStore.EXTRA_OUTPUT, camUri);
                cam.addFlags(android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION | android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                ch.putExtra(android.content.Intent.EXTRA_INITIAL_INTENTS, new android.content.Intent[]{cam});
            }
        }
        try {
            startActivityForResult(ch, REQ_FOTO);
        } catch (Exception e) {
            dropCam();
        }
    }

    private void dropCam() {
        if (camUri != null) try { getContentResolver().delete(camUri, null, null); } catch (Exception ignored) { }
        camUri = null;
    }

    private void fotoResult(int res, android.content.Intent data) {
        Uri src = data != null ? data.getData() : null;
        boolean cam = camUri != null && (src == null || src.equals(camUri));
        if (res != RESULT_OK || (src == null && !cam)) { dropCam(); return; }
        if (cam) src = camUri;
        else dropCam();               // a ales din galerie: locul pregatit pentru camera nu mai trebuie
        camUri = null;
        final Uri f = src;
        final String board = boardKey();
        new Thread(() -> {
            boolean ok = Foto.add(this, board, f, fotoT, fotoH);
            ui.post(() -> {
                if (ok) web.evaluateJavascript("window.fotoGata&&fotoGata()", null);
                else android.widget.Toast.makeText(this, "Nu am putut citi poza.", android.widget.Toast.LENGTH_LONG).show();
            });
        }).start();
    }

    // ---------------- invitatia "doar privit" ----------------
    private void makeInvite(String viewPass) {
        new Thread(() -> {
            String board = boardKey();
            Remote.Cfg rc = Remote.load(this, board);
            String key2 = null;
            String nota = "";
            if (rc.ok()) {
                String saved = prefs.getString("rm_" + board + "_key2", "");
                String k = saved.length() == 64 ? saved : Remote.newKeyHex();
                String r = null;
                // cheia ajunge la placa doar pe WiFi-ul de acasa (niciodata prin internet)
                if (!remote) try { r = AlertChecker.get(this, url, "/privit?key=" + k); } catch (Exception ignored) { }
                if (r != null) { key2 = k; prefs.edit().putString("rm_" + board + "_key2", k).apply(); }
                else if (saved.length() == 64) key2 = saved;   // placa o are deja de data trecuta
                else nota = "\n\n(Invitația merge doar pe WiFi-ul meu. Pentru acces prin internet, o fac din nou când sunt acasă.)";
            }
            String code;
            try { code = Invitatie.encode(getIntent().getStringExtra(EXTRA_NAME) == null ? board : getIntent().getStringExtra(EXTRA_NAME), url, viewPass, rc, key2); }
            catch (Exception e) { return; }
            String text = "\uD83E\uDD80 Te invit să vezi paludariul meu („" + board + "”). Poți vedea tot, dar nu poți porni sau opri nimic.\n"
                    + "1. Instalează aplicația Paludariu.\n2. Apasă „Adaugă o placă” → „Am un cod de invitație” și lipește tot mesajul ăsta.\n\n"
                    + code + (rc.ok() ? nota : "\n\n(Merge doar pe WiFi-ul unde e placa.)");
            ui.post(() -> {
                android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_SEND);
                i.setType("text/plain");
                i.putExtra(android.content.Intent.EXTRA_TEXT, text);
                try { startActivity(android.content.Intent.createChooser(i, "Trimite invitația (doar familiei)")); } catch (Exception ignored) { }
                web.evaluateJavascript("window.pvGata&&pvGata()", null);
            });
        }).start();
    }

    private long widgetT = 0;
    private void widgetData(String json) {
        long now = System.currentTimeMillis();
        if (now - widgetT < 60000) return;
        widgetT = now;
        try {
            org.json.JSONObject d = new org.json.JSONObject(json);
            Widget.store(this, url, getIntent().getStringExtra(EXTRA_NAME), d);
            Widget.updateAll(this);
            org.json.JSONObject sys = d.optJSONObject("sys");
            String gh = sys == null ? "" : sys.optString("gh", "");
            if (!gh.isEmpty()) prefs.edit().putString("gh_repo", gh).apply();   // pentru verificarea versiunilor aplicatiei
        } catch (Exception ignored) {
        }
    }

    // ---------------- alegerea fisierului .bin pentru actualizarea placii ----------------
    private static final int REQ_FILE = 11, REQ_SAVE = 12;
    private String pendingSave;
    private android.webkit.ValueCallback<Uri[]> fileCb;

    private class Chrome extends android.webkit.WebChromeClient {
        @Override
        public boolean onShowFileChooser(WebView v, android.webkit.ValueCallback<Uri[]> cb, FileChooserParams params) {
            if (fileCb != null) fileCb.onReceiveValue(null);
            fileCb = cb;
            android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_GET_CONTENT);
            i.addCategory(android.content.Intent.CATEGORY_OPENABLE);
            i.setType("*/*");   // fisierele .bin n-au un tip anume pe telefon
            try {
                startActivityForResult(android.content.Intent.createChooser(i, "Alege paludariu.ino.bin"), REQ_FILE);
            } catch (Exception e) {
                fileCb = null;
                return false;
            }
            return true;
        }
    }

    @Override
    protected void onActivityResult(int req, int res, android.content.Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_FOTO) { fotoResult(res, data); return; }
        if (req == REQ_SAVE) {
            Uri u = (res == RESULT_OK && data != null) ? data.getData() : null;
            String text = pendingSave;
            pendingSave = null;
            if (u == null || text == null) return;
            try (java.io.OutputStream o = getContentResolver().openOutputStream(u)) {
                o.write(text.getBytes(StandardCharsets.UTF_8));
                web.evaluateJavascript("(function(){var m=document.getElementById('cfM');if(m)m.textContent='Fișierul s-a salvat.'})()", null);
            } catch (Exception e) {
                web.evaluateJavascript("(function(){var m=document.getElementById('cfM');if(m)m.textContent='Nu am putut salva fișierul.'})()", null);
            }
            return;
        }
        if (req != REQ_FILE || fileCb == null) return;
        Uri u = (res == RESULT_OK && data != null) ? data.getData() : null;
        fileCb.onReceiveValue(u == null ? null : new Uri[]{u});
        fileCb = null;
    }

    // Se injecteaza in pagina placii, dupa ce s-a incarcat: copiaza raspunsurile in aplicatie.
    private static final String JS_CAPTURE =
            "(function(){if(window.__pal||!window.PaludariuApp)return;window.__pal=1;var B=window.PaludariuApp,of=window.fetch,fail=0;"
          + "function sv(p){return of(p).then(function(r){return r.ok?r.text():''}).then(function(t){if(t)B.save(p,t)}).catch(function(){})}"
          + "window.fetch=function(u,o){var p=new URL(u,location.href).pathname;"
          + "return of.apply(this,arguments).then(function(r){if((p=='/s'||p=='/h'||p=='/h30'||p=='/e')&&r.ok)r.clone().text().then(function(t){B.save(p,t)});if(p=='/s')fail=0;return r},"
          // cat se incarca o actualizare pe placa, ea nu raspunde: asta nu inseamna ca e offline
          + "function(e){if(p=='/s'&&++fail>=3&&!window.__upd)B.lost();throw e})};"
          // copia completa se face la 5 s dupa deschidere, pe rand, ca sa nu incetineasca primele comenzi
          + "setTimeout(function(){sv('/s').then(function(){return sv('/e')}).then(function(){return sv('/h')}).then(function(){return sv('/h30')}).then(function(){return sv('/')})},5000)})();";

    // ---------------- trecerea intre online si offline ----------------

    private void goOffline() {
        offline = true;
        remote = false;
        closeSession();
        if (hasCache()) showCached(false);
        else showOffline();
        startChecking();
    }

    private void goOnline() {
        offline = false;
        remote = false;
        closeSession();
        checking = false;
        cachedShown = false;
        web.loadUrl(url);
    }

    /** Cat timp suntem offline, verificam la 15 secunde daca placa a revenit. */
    private void startChecking() {
        if (checking) return;
        checking = true;
        Thread t = new Thread(() -> {
            while (checking && offline) {
                long pause = (remote && onWifi()) ? 5000 : 15000;   // pe WiFi, prin internet: poate esti acasa
                try { Thread.sleep(pause); } catch (InterruptedException e) { return; }
                if (!checking || !offline) return;
                if (boardAnswers() || relocate()) {
                    ui.post(() -> { if (offline) goOnline(); });   // ai ajuns acasa: direct, fara internet
                    return;
                }
                if (!remote && tryRemote()) ui.post(() -> { if (offline && session != null) goRemote(); });
            }
        });
        t.setDaemon(true);
        t.start();
    }

    private boolean boardAnswers() { return boardAnswersAt(url); }

    /** Telefonul e pe WiFi (oricare)? Atunci probabil esti acasa si merita insistat pe legatura directa. */
    private boolean onWifi() {
        try {
            android.net.ConnectivityManager cm = (android.net.ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
            android.net.Network n = cm == null ? null : cm.getActiveNetwork();
            android.net.NetworkCapabilities c = n == null ? null : cm.getNetworkCapabilities(n);
            return c != null && c.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI);
        } catch (Exception e) {
            return false;
        }
    }

    /** Pe WiFi incercam direct de 3 ori (placa poate intarzia o clipa); pe date mobile o data e de ajuns. */
    private boolean boardAnswersHome() {
        int n = onWifi() ? 3 : 1;
        for (int i = 0; i < n; i++) {
            if (boardAnswers()) return true;
            if (i + 1 < n) try { Thread.sleep(700); } catch (InterruptedException e) { return false; }
        }
        return false;
    }

    private String boardKey() {
        String n = getIntent().getStringExtra(EXTRA_NAME);
        return n == null || n.isEmpty() ? (host == null ? "placa" : host) : n;
    }

    // ---------------- accesul de la distanta (prin serverul MQTT) ----------------

    /** Se poate ajunge la placa prin internet? Deschide legatura si cere starea. Ruleaza pe un fir separat. */
    private boolean tryRemote() {
        Remote.Cfg cfg = Remote.load(this, boardKey());
        if (!cfg.ok() || !hasCache()) return false;
        Remote.Session s = new Remote.Session(cfg);
        try {
            s.open();
            Remote.Resp r = s.request("/s", 9000);
            if (r.code != 200) { s.close(); return false; }
            writeCache("/s", r.body);
            closeSession();
            session = s;
            return true;
        } catch (Exception e) {
            s.close();
            return false;
        }
    }

    private void closeSession() {
        Remote.Session s = session;
        session = null;
        if (s != null) new Thread(s::close).start();
    }

    private void goRemote() {
        offline = true;          // nu suntem in reteaua placii...
        remote = true;           // ...dar vorbim cu ea prin internet
        cachedShown = false;
        web.loadDataWithBaseURL(url + "/", remoteHtml(), "text/html", "utf-8", null);
        startChecking();         // cand ajungi acasa, trecem singuri pe legatura directa
    }

    private String remoteHtml() {
        String html = cleanPage();
        if (html == null) return "";
        String ver = appVersion().replace("'", "");
        String inject = "<style>#rmb{position:sticky;top:8px;z-index:60;width:max-content;max-width:100%;margin:10px auto 0;padding:8px 14px;"
                + "border-radius:99px;background:color-mix(in srgb,var(--cy) 18%,var(--c));border:1px solid color-mix(in srgb,var(--cy) 45%,transparent);"
                + "color:var(--t);font:500 13px Figtree,system-ui,sans-serif}</style><script>(function(){window.__remote=true;"
                + "var b=document.createElement('div');b.id='rmb';b.textContent='\\uD83C\\uDF10 Prin internet: ești departe de casă';"
                + "var h=document.querySelector('header.top');if(h)h.parentNode.insertBefore(b,h.nextSibling);"
                + "var f=document.querySelector('.ft');if(f&&'" + ver + "')f.innerHTML=f.innerHTML.replace(/v[0-9.]+/,'v" + ver + "');"
                + "})();</script>";
        int end = html.lastIndexOf("</body>");
        return end >= 0 ? html.substring(0, end) + inject + html.substring(end) : html + inject;
    }

    private static final java.util.Set<String> REMOTE_PATHS = new java.util.HashSet<>(java.util.Arrays.asList(
            "/s", "/h", "/h30", "/e", "/m", "/save", "/pl", "/mem", "/cfg", "/vac", "/gh", "/tgtest", "/opt",
            "/hran", "/fan", "/noapte", "/privit"));

    /** O cerere a paginii, trimisa placii prin internet. */
    private WebResourceResponse remoteResponse(Uri u) {
        String p = u.getPath() == null ? "/" : u.getPath();
        if (p.equals("/")) return new WebResourceResponse("text/html", "utf-8", new ByteArrayInputStream(remoteHtml().getBytes(StandardCharsets.UTF_8)));
        WebResourceResponse r;
        if (!REMOTE_PATHS.contains(p)) {
            r = new WebResourceResponse("text/plain", "utf-8", new ByteArrayInputStream("De la distanță nu se poate face asta.".getBytes(StandardCharsets.UTF_8)));
            r.setStatusCodeAndReasonPhrase(403, "Interzis");
            return r;
        }
        Remote.Session s = session;
        String q = u.getEncodedQuery();
        try {
            if (s == null || !s.isOpen()) throw new IOException("fara legatura");
            Remote.Resp rr = s.request(p + (q == null ? "" : "?" + q), 12000);
            String mime = rr.type == null ? "text/plain" : rr.type.split(";")[0].trim();
            r = new WebResourceResponse(mime, "utf-8", new ByteArrayInputStream(rr.body.getBytes(StandardCharsets.UTF_8)));
            r.setStatusCodeAndReasonPhrase(rr.code, rr.code < 400 ? "OK" : "Eroare");
            if (rr.code == 200 && (p.equals("/s") || p.equals("/h") || p.equals("/h30") || p.equals("/e"))) writeCache(p, rr.body);
            return r;
        } catch (Exception e) {
            r = new WebResourceResponse("text/plain", "utf-8", new ByteArrayInputStream("Placa nu răspunde prin internet.".getBytes(StandardCharsets.UTF_8)));
            r.setStatusCodeAndReasonPhrase(503, "Indisponibil");
            return r;
        }
    }

    /**
     * Placa nu raspunde la adresa salvata: poate routerul i-a dat alta adresa (de ex. dupa o pana de curent).
     * O cautam dupa nume in retea; daca o gasim, tinem minte adresa noua si mergem acolo.
     */
    private boolean relocate() {
        String name = getIntent().getStringExtra(EXTRA_NAME);
        if (name == null || name.isEmpty()) return false;
        String found = findByName(name, 6000);
        if (found == null || found.equalsIgnoreCase(url) || !boardAnswersAt(found)) return false;
        Boards.save(this, name, found);
        Boards.setLast(this, found);
        url = found;
        host = Uri.parse(found).getHost();
        return true;
    }

    private String findByName(String name, long ms) {
        final String[] found = {null};
        final android.net.nsd.NsdManager nsd = (android.net.nsd.NsdManager) getSystemService(NSD_SERVICE);
        if (nsd == null) return null;
        android.net.nsd.NsdManager.DiscoveryListener dl = new android.net.nsd.NsdManager.DiscoveryListener() {
            public void onStartDiscoveryFailed(String t, int e) { }
            public void onStopDiscoveryFailed(String t, int e) { }
            public void onDiscoveryStarted(String t) { }
            public void onDiscoveryStopped(String t) { }
            public void onServiceLost(android.net.nsd.NsdServiceInfo i) { }
            @SuppressWarnings("deprecation")
            public void onServiceFound(android.net.nsd.NsdServiceInfo info) {
                if (!name.equals(info.getServiceName())) return;
                try {
                    nsd.resolveService(info, new android.net.nsd.NsdManager.ResolveListener() {
                        public void onResolveFailed(android.net.nsd.NsdServiceInfo i, int e) { }
                        public void onServiceResolved(android.net.nsd.NsdServiceInfo i) {
                            java.net.InetAddress h = i.getHost();
                            if (h instanceof java.net.Inet4Address)
                                found[0] = "http://" + h.getHostAddress() + (i.getPort() == 80 ? "" : ":" + i.getPort());
                        }
                    });
                } catch (RuntimeException ignored) { }
            }
        };
        try { nsd.discoverServices("_paludariu._tcp.", android.net.nsd.NsdManager.PROTOCOL_DNS_SD, dl); } catch (RuntimeException e) { return null; }
        long t0 = System.currentTimeMillis();
        while (found[0] == null && !isFinishing() && System.currentTimeMillis() - t0 < ms) {
            try { Thread.sleep(300); } catch (InterruptedException e) { break; }
        }
        try { nsd.stopServiceDiscovery(dl); } catch (RuntimeException ignored) { }
        return found[0];
    }

    private boolean boardAnswersAt(String base) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(base + "/manifest.json").openConnection();   // nu cere parola
            c.setConnectTimeout(3000);
            c.setReadTimeout(3000);
            return c.getResponseCode() == 200;
        } catch (IOException e) {
            return false;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** Pagina salvata, cu banda "offline" sus si comenzile oprite. Animatiile merg cu ultima stare primita. */
    private void showCached(boolean connecting) {
        String html = cleanPage();
        if (html == null) { showOffline(); return; }
        cachedShown = true;
        bannerConnecting = connecting;
        String ver = appVersion().replace("'", "");
        long t = prefs.getLong("offline_t_" + host, 0);
        String cand = t == 0 ? "" : " la " + formatTime(t);
        String inject = "<style>"
                + "#offb{position:sticky;top:8px;z-index:60;margin:10px 0 0;padding:10px 12px 10px 16px;border-radius:18px;"
                + "background:var(--bad);color:#fff;font:600 14px/1.35 Figtree,system-ui,sans-serif;display:flex;align-items:center;gap:10px;"
                + "box-shadow:0 6px 20px rgba(0,0,0,.35)}#offb span{flex:1}"
                + "#offb button{flex:none;border:0;border-radius:99px;padding:9px 14px;background:#fff;color:#14261b;font:600 13px Figtree,system-ui,sans-serif}"
                // cat timp se conecteaza: o pastila discreta, nu banda rosie
                + "#offb.con{background:var(--c);color:var(--m);border:1px solid var(--b);box-shadow:none;font-weight:500;font-size:13px;"
                + "padding:8px 14px;width:max-content;max-width:100%;margin:10px auto 0;border-radius:99px}"
                + "#offb.con span{flex:none}#offb.con button{display:none}"
                + "#offb.con span::before{content:'';display:inline-block;width:11px;height:11px;margin-right:8px;vertical-align:-1px;"
                + "border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:offsp .8s linear infinite}"
                + "@keyframes offsp{to{transform:rotate(360deg)}}"
                + "body.offl.offc .gauges,body.offl.offc .ledc,body.offl.offc .ac .as,body.offl.offc #vz .lbl{opacity:1;filter:none}"
                + "body.offl.offc #p0>.card:first-child::after,body.offl.offc .ledc::after,body.offl.offc .vih span::after,"
                + "body.offl.offc .vl small::after,body.offl.offc .ac .as::after{content:none}"
                + "body.offl .gauges,body.offl .ledc,body.offl .ac .as,body.offl #vz .lbl{opacity:.6;filter:saturate(.4)}"
                + "body.offl #p0>.card:first-child,body.offl .ledc{position:relative}"
                + "body.offl #p0>.card:first-child::after,body.offl .ledc::after{content:'offline';position:absolute;top:10px;right:12px;"
                + "padding:2px 8px;border-radius:99px;background:var(--trk);color:var(--bad);font:600 11px Figtree,system-ui,sans-serif;letter-spacing:.5px}"
                + "body.offl .vih span::after,body.offl .vl small::after,body.offl .ac .as::after{content:' \\00b7  offline';color:var(--bad)}"
                + "body.offl .tg,body.offl .vbt button,body.offl #plB,body.offl .save{opacity:.45}"
                + "</style><script>(function(){"
                + "var OFF='Placa e offline. Vezi ultimele date primite" + cand + ".',CON='Se conectează…';"
                + "var b=document.createElement('div');b.id='offb';"
                + "b.innerHTML='<span></span><button>Reîncearcă</button>';"
                + "window.__offSet=function(c){b.querySelector('span').textContent=c?CON:OFF;b.className=c?'con':'';document.body.classList.toggle('offc',!!c)};"
                + "var f=document.querySelector('.ft');if(f&&'" + ver + "')f.innerHTML=f.innerHTML.replace(/v[0-9.]+/,'v" + ver + "');"
                + "var h=document.querySelector('header.top');h.parentNode.insertBefore(b,h.nextSibling);"
                + "document.body.classList.add('offl');__offSet(" + connecting + ");"
                + "b.querySelector('button').onclick=function(){window.PaludariuApp&&PaludariuApp.retry()};"
                + "function no(){if(b.className=='con')return;var s=b.querySelector('span');s.textContent='Placa e offline, comenzile nu pot fi trimise acum.';"
                + "setTimeout(function(){s.textContent=OFF},3500)}"
                + "['mod','salv','plCmd','tgTest','memGata','upload','optCmd','oreGata','hrFed','hrAdd','hrDel','hrZi','hrOra','fanAuto','fanTest','pvToggle','pvNew','pvInvite','bkPune','bkAcum'].forEach(function(f){window[f]=function(){no();return Promise.resolve()}});"
                + "})();</script>";
        int end = html.lastIndexOf("</body>");
        html = end >= 0 ? html.substring(0, end) + inject + html.substring(end) : html + inject;
        web.loadDataWithBaseURL(url + "/", html, "text/html", "utf-8", null);
    }

    private void setBanner(boolean connecting) {
        bannerConnecting = connecting;
        web.evaluateJavascript("window.__offSet&&__offSet(" + connecting + ")", null);
    }

    private void showConnecting() {
        String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                + "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0b1610;"
                + "color:#93a99a;font:15px system-ui,sans-serif}"
                // apare abia dupa 0,4 s: acasa pagina live vine inainte, deci nu clipeste nimic
                + "div{opacity:0;animation:in .3s .4s forwards}@keyframes in{to{opacity:1}}"
                + "i{display:inline-block;width:12px;height:12px;margin-right:10px;vertical-align:-1px;border:2px solid currentColor;"
                + "border-right-color:transparent;border-radius:50%;animation:sp .8s linear infinite}@keyframes sp{to{transform:rotate(360deg)}}"
                + "</style></head><body><div><i></i>Se conectează…</div></body></html>";
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null);
    }

    private static String formatTime(long t) {
        Calendar now = Calendar.getInstance(), c = Calendar.getInstance();
        c.setTimeInMillis(t);
        boolean azi = now.get(Calendar.YEAR) == c.get(Calendar.YEAR) && now.get(Calendar.DAY_OF_YEAR) == c.get(Calendar.DAY_OF_YEAR);
        return new SimpleDateFormat(azi ? "HH:mm" : "dd.MM HH:mm", Locale.getDefault()).format(new Date(t));
    }

    private class Client extends WebViewClient {
        @Override
        public void onPageFinished(WebView view, String u) {
            triedSaved = false;
            String hash = getIntent().getStringExtra(EXTRA_HASH);
            if (hash != null && u != null && !u.startsWith("data:") && !u.startsWith("about:")) {
                getIntent().removeExtra(EXTRA_HASH);
                String h = hash.replaceAll("[^a-z]", "");
                view.evaluateJavascript("(function(){try{var i=PG.indexOf('" + h + "');if(i>0)setTimeout(function(){go(i,1)},300)}catch(e){}})()", null);
            }   // pagina s-a incarcat: urmatoarea cerere de parola incearca iar parola salvata
            if (offline && cachedShown) view.evaluateJavascript("window.__offSet&&__offSet(" + bannerConnecting + ")", null);
            if ((offline && !remote) || u == null) return;
            Uri pu = Uri.parse(u);
            if (host != null && pu != null && host.equalsIgnoreCase(pu.getHost())) {
                view.evaluateJavascript(JS_CAPTURE, null);
                String ver = appVersion().replace("'", "");
                if (!ver.isEmpty()) view.evaluateJavascript("(function(){var f=document.querySelector('.ft');if(f)f.innerHTML=f.innerHTML.replace(/v[0-9.]+/,'v" + ver + "')})();", null);
            }
        }

        @Override
        public void onReceivedHttpAuthRequest(WebView view, HttpAuthHandler handler, String h, String realm) {
            String user = prefs.getString("user_" + h, null);
            String pass = prefs.getString("pass_" + h, null);
            if (user != null && pass != null && !triedSaved) {
                triedSaved = true;
                handler.proceed(user, pass);
                return;
            }
            askPassword(handler, h, user);
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            if (request.isForMainFrame() && !offline && !remote) goOffline();
        }

        /** In modul offline, cererile catre placa primesc raspunsurile salvate. */
        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            if (!offline) return null;
            Uri u = request.getUrl();
            if (u == null || host == null || !host.equals(u.getHost())) return null;
            if (remote) return remoteResponse(u);
            String p = u.getPath() == null ? "/" : u.getPath();
            String body = (p.equals("/s") || p.equals("/h") || p.equals("/h30") || p.equals("/e")) ? readCache(p) : null;
            if (body == null) {
                WebResourceResponse r = new WebResourceResponse("text/plain", "utf-8",
                        new ByteArrayInputStream("offline".getBytes(StandardCharsets.UTF_8)));
                r.setStatusCodeAndReasonPhrase(503, "Offline");
                return r;
            }
            return new WebResourceResponse("application/json", "utf-8",
                    new ByteArrayInputStream(body.getBytes(StandardCharsets.UTF_8)));
        }
    }

    /** Cere utilizatorul si parola paginii (WEB_USER / WEB_PASS din cod) si le tine minte pentru placa asta. */
    private void askPassword(HttpAuthHandler handler, String h, String lastUser) {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 20);
        form.setPadding(p, Ui.dp(this, 8), p, 0);
        EditText user = Ui.input(this, "Utilizator");
        user.setText(lastUser != null ? lastUser : "admin");
        EditText pass = Ui.input(this, "Parolă");
        pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        form.addView(user, Ui.wide(this, 0));
        form.addView(pass, Ui.wide(this, 10));

        new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle(triedSaved ? "Parola nu se potrivește" : "Parola plăcii")
                .setMessage("Sunt datele din cod, de la WEB_USER și WEB_PASS. Le scrii o singură dată.")
                .setView(form)
                .setCancelable(false)
                .setPositiveButton("Conectează", (d, w) -> {
                    String u = user.getText().toString().trim();
                    String pw = pass.getText().toString();
                    prefs.edit().putString("user_" + h, u).putString("pass_" + h, pw).apply();
                    triedSaved = true;
                    handler.proceed(u, pw);
                })
                .setNegativeButton("Înapoi", (d, w) -> {
                    handler.cancel();
                    finish();
                })
                .show();
    }

    /** Placa nu raspunde si nu avem inca nicio copie salvata: mesaj simplu, cu buton de reincercare. */
    private void showOffline() {
        String name = getIntent().getStringExtra(EXTRA_NAME);
        String safeName = name == null ? "Placa" : name.replace("&", "&amp;").replace("<", "&lt;");
        String html = "<!doctype html><html><head><meta charset='utf-8'>"
                + "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                + "<style>body{margin:0;min-height:100vh;display:flex;flex-direction:column;justify-content:center;"
                + "padding:24px;box-sizing:border-box;background:#0b1610;color:#e8f1e7;font:16px/1.5 system-ui,sans-serif}"
                + "h1{font:600 26px Georgia,serif;margin:0 0 8px}p{color:#93a99a;margin:0 0 24px}"
                + "button{display:block;width:100%;padding:15px;border:0;border-radius:99px;background:#86cf8e;color:#0c1a12;"
                + "font:600 16px system-ui,sans-serif}</style></head><body>"
                + "<h1>" + safeName + " nu răspunde</h1>"
                + "<p>Verifică dacă placa e pornită și dacă telefonul e pe același WiFi. Adresa: "
                + Boards.shortUrl(url).replace("<", "&lt;") + "<br>După prima conectare reușită, aici vei vedea ultimele date primite.</p>"
                + "<button onclick='window.PaludariuApp&&PaludariuApp.retry()'>Încearcă din nou</button></body></html>";
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null);
    }

    // ---------------- configurarea accesului de la distanta (se face de acasa) ----------------

    private void showRemoteSetup() {
        Remote.Cfg cur = Remote.load(this, boardKey());
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        int pd = Ui.dp(this, 20);
        f.setPadding(pd, Ui.dp(this, 4), pd, 0);
        EditText host = Ui.input(this, "Adresa serverului (…emqxsl.com)");
        host.setText(cur.host);
        EditText bu = Ui.input(this, "Utilizatorul plăcii (ex. placa1)");
        EditText bp = Ui.input(this, "Parola plăcii");
        bp.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        EditText pu = Ui.input(this, "Utilizatorul telefonului (ex. telefon)");
        pu.setText(cur.user);
        EditText pp = Ui.input(this, "Parola telefonului");
        pp.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        pp.setText(cur.pass);
        android.widget.TextView note = Ui.text(this, "Le iei din EMQX: Access Control → Authentication. Parolele rămân doar în placă și în telefon.", 13, Ui.MUTED, false);
        f.addView(host, Ui.wide(this, 0));
        f.addView(bu, Ui.wide(this, 10));
        f.addView(bp, Ui.wide(this, 8));
        f.addView(pu, Ui.wide(this, 10));
        f.addView(pp, Ui.wide(this, 8));
        f.addView(note, Ui.wide(this, 10));
        android.widget.ScrollView sv = new android.widget.ScrollView(this);
        sv.addView(f);
        AlertDialog.Builder b = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Acces de la distanță")
                .setView(sv)
                .setPositiveButton("Salvează și testează", null)
                .setNegativeButton("Renunță", null);
        if (cur.ok()) b.setNeutralButton("Oprește", (d, w) -> remoteOff());
        AlertDialog d = b.create();
        d.setOnShowListener(x -> d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String h = host.getText().toString().trim(), u1 = bu.getText().toString().trim(), u2 = pu.getText().toString().trim();
            if (h.isEmpty() || u1.isEmpty() || u2.isEmpty() || pp.getText().length() == 0 || (bp.getText().length() == 0)) {
                note.setTextColor(Ui.BAD);
                note.setText("Completează toate câmpurile: adresa, utilizatorul și parola plăcii, utilizatorul și parola telefonului.");
                return;
            }
            d.dismiss();
            remoteConfigure(h, u1, bp.getText().toString(), u2, pp.getText().toString(), cur.keyHex);
        }));
        d.show();
    }

    private AlertDialog progress;
    private void remoteProgress(String msg) {
        ui.post(() -> {
            if (progress == null) {
                progress = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                        .setTitle("Acces de la distanță").setMessage(msg).setCancelable(false).create();
                progress.show();
            } else progress.setMessage(msg);
        });
    }

    private void remoteDone(String title, String msg) {
        ui.post(() -> {
            if (progress != null) { progress.dismiss(); progress = null; }
            if (isFinishing()) return;
            new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                    .setTitle(title).setMessage(msg).setPositiveButton("OK", null).show();
            web.evaluateJavascript("window.load&&load()", null);
        });
    }

    private static String enc(String s) {
        try { return java.net.URLEncoder.encode(s, "UTF-8"); } catch (Exception e) { return s; }
    }

    private void remoteConfigure(String hostSpec, String boardUser, String boardPass, String phoneUser, String phonePass, String oldKey) {
        remoteProgress("Trimit setările plăcii…");
        new Thread(() -> {
            String key = oldKey != null && oldKey.length() == 64 ? oldKey : Remote.newKeyHex();
            try {
                // 1. placa primeste adresa, contul ei si cheia (doar prin WiFi-ul de acasa, cu parola paginii)
                String r = AlertChecker.get(this, url, "/mqttcfg?host=" + enc(hostSpec) + "&user=" + enc(boardUser) + "&pass=" + enc(boardPass) + "&key=" + key);
                if (r == null) { remoteDone("Nu a mers", "Placa nu a răspuns. Configurarea se face de acasă, cu telefonul pe același WiFi cu placa."); return; }
                org.json.JSONObject j = new org.json.JSONObject(r);
                if (j.optInt("are", 1) == 0) { remoteDone("Nu se poate", "Codul plăcii e compilat cu o versiune veche de ESP32 (2.x). Actualizează în Arduino IDE pachetul esp32 la 3.x."); return; }
                Remote.Cfg cfg = new Remote.Cfg();
                cfg.host = hostSpec; cfg.user = phoneUser; cfg.pass = phonePass; cfg.keyHex = key; cfg.id = j.optString("id", "");
                Remote.save(this, boardKey(), cfg);
                AlertChecker.reschedule(this);
                // 2. asteptam ca placa sa se conecteze la server
                int cod = 1;
                String err = "";
                for (int i = 0; i < 30 && cod == 1; i++) {   // placa isi asteapta randul (Telegram), apoi se conecteaza
                    remoteProgress("Placa se conectează la server… (" + (i * 2) + " s)");
                    Thread.sleep(2000);
                    String st = AlertChecker.get(this, url, "/mqttcfg");
                    if (st != null) {
                        org.json.JSONObject sj = new org.json.JSONObject(st);
                        cod = sj.optInt("cod", 1);
                        err = sj.optString("err", "");
                    }
                }
                String det = err.isEmpty() ? "" : "\n\nCe spune placa: " + err;
                if (cod == 3) { remoteDone("Placa nu a fost primită", "Serverul a refuzat utilizatorul sau parola plăcii. Verifică-le în EMQX: Access Control → Authentication." + det); return; }
                if (cod == 4) {
                    // incercam si de pe telefon, din acelasi WiFi: asa aflam daca e adresa/reteaua sau placa
                    remoteProgress("Încerc serverul și de pe telefon…");
                    String tel;
                    MiniMqtt t = new MiniMqtt();
                    try {
                        byte[] r4 = new byte[4];
                        Remote.RND.nextBytes(r4);
                        t.connect(hostSpec, "app-test-" + Remote.hex(r4), phoneUser, phonePass, 30, null);
                        tel = "Telefonul, din același WiFi, ajunge la server, deci adresa e bună și rețeaua nu blochează nimic: problema e la placă.";
                    } catch (MiniMqtt.RefusedException e) {
                        tel = "Telefonul ajunge la server, deci adresa e bună (dar contul telefonului e refuzat).";
                    } catch (Exception e) {
                        tel = "Nici telefonul nu ajunge la server: " + e.getClass().getSimpleName() + (e.getMessage() != null ? " · " + e.getMessage() : "") + ". Verifică adresa (EMQX → Overview → Address).";
                    } finally {
                        t.disconnect();
                    }
                    remoteDone("Placa nu ajunge la server", tel + det);
                    return;
                }
                if (cod != 2) { remoteDone("Încă se conectează", "Placa nu s-a conectat la server într-un minut. Uită-te peste un minut în Setări → Acces de la distanță: placa încearcă singură, din 30 în 30 de secunde."); return; }
                // 3. testam si drumul telefonului, ca de la distanta
                remoteProgress("Testez legătura telefonului…");
                Remote.Resp t = Remote.once(cfg, "/s");
                if (t.code != 200) throw new java.io.IOException("cod " + t.code);
                remoteDone("Gata", "Accesul de la distanță merge. Când nu ești acasă, aplicația vorbește cu placa prin internet, criptat cap-coadă.");
            } catch (MiniMqtt.RefusedException e) {
                remoteDone("Telefonul nu a fost primit", "Serverul a refuzat utilizatorul sau parola telefonului. Verifică-le în EMQX: Access Control → Authentication.");
            } catch (Exception e) {
                remoteDone("Nu a mers", "Testul n-a reușit: " + e.getMessage());
            }
        }).start();
    }

    private void remoteOff() {
        new Thread(() -> {
            String r = null;
            try { r = AlertChecker.get(this, url, "/mqttcfg?off=1"); } catch (Exception ignored) { }
            Remote.remove(this, boardKey());
            AlertChecker.reschedule(this);
            remoteDone("Oprit", r != null ? "Accesul de la distanță e oprit, pe placă și în telefon." : "L-am oprit în telefon. Placa n-a răspuns; o oprești de acasă la fel.");
        }).start();
    }
}
