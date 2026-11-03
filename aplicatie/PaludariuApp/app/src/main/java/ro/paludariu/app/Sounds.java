package ro.paludariu.app;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;

/**
 * Sunetele aplicatiei. Fisierele stau in aplicatie (res/raw), nu pe placa.
 * - sunete la butoane si la pornire (comutator "Sunete la butoane")
 * - pasari care canta din cand in cand cat timp esti in aplicatie (comutator "Păsări în fundal" + volum)
 * Merg pe volumul de media al telefonului.
 */
final class Sounds {
    static final String P_BTN = "snd_btn", P_BG = "snd_bg", P_VOL = "snd_vol";
    static final int VOL_IMPLICIT = 30;

    private static Sounds inst;

    static synchronized Sounds get(Context c) {
        if (inst == null) inst = new Sounds(c.getApplicationContext());
        return inst;
    }

    private final Context app;
    private final SharedPreferences prefs;
    private final SoundPool pool;
    private final Map<String, Integer> ids = new HashMap<>();
    private final Set<Integer> loaded = new HashSet<>();
    private final Handler h = new Handler(Looper.getMainLooper());
    private final Random rnd = new Random();

    /** sunete care se aud acum (pentru stingerea lina cand iesi din aplicatie) */
    private static final class Play {
        final int stream; final float l, r; final long t;
        Play(int stream, float l, float r) { this.stream = stream; this.l = l; this.r = r; this.t = SystemClock.uptimeMillis(); }
    }
    private final List<Play> playing = new ArrayList<>();

    private String pending;            // sunet cerut inainte sa fie incarcat (sunetul de pornire)
    private long pendingT;
    private int foreground = 0;        // cate ecrane ale aplicatiei sunt deschise acum
    private boolean ambient = false;
    private long ambientStart = 0, ambientAfter = 0;

    private Sounds(Context c) {
        app = c;
        prefs = Boards.prefs(c);
        AudioAttributes aa = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        pool = new SoundPool.Builder().setMaxStreams(8).setAudioAttributes(aa).build();
        pool.setOnLoadCompleteListener((p, id, status) -> {
            String go = null;
            synchronized (this) {
                if (status == 0) loaded.add(id);
                if (pending != null && Integer.valueOf(id).equals(ids.get(pending))) {
                    if (SystemClock.uptimeMillis() - pendingT < 1500) go = pending;
                    pending = null;
                }
            }
            if (go != null) play(go, 1f, 1f);
        });
        load("on", R.raw.s_on);
        load("off", R.raw.s_off);
        load("auto", R.raw.s_auto);
        load("click", R.raw.s_click);
        load("save", R.raw.s_save);
        load("rain", R.raw.s_rain);
        load("swipe", R.raw.s_swipe);
        load("slide", R.raw.s_slide);
        load("start", R.raw.s_start);
        load("bird1", R.raw.s_bird1);
        load("bird2", R.raw.s_bird2);
        load("bird3", R.raw.s_bird3);
        load("bird4", R.raw.s_bird4);
        load("bird5", R.raw.s_bird5);
    }

    private void load(String name, int res) {
        ids.put(name, pool.load(app, res, 1));
    }

    // ---------------- setari ----------------

    boolean buttonsOn() { return prefs.getBoolean(P_BTN, true); }
    boolean birdsOn() { return prefs.getBoolean(P_BG, true); }
    int volume() { return prefs.getInt(P_VOL, VOL_IMPLICIT); }

    void setButtons(boolean on) { prefs.edit().putBoolean(P_BTN, on).apply(); }

    void setBirds(boolean on) {
        prefs.edit().putBoolean(P_BG, on).apply();
        if (on && foreground > 0) startAmbient(600);
        else stopAmbient();
    }

    void setVolume(int v) { prefs.edit().putInt(P_VOL, Math.max(0, Math.min(100, v))).apply(); }

    // ---------------- redare ----------------

    /** Sunetul unui buton (on, off, auto, click, save, rain, swipe). Se poate apela din orice fir. */
    void ui(String name) {
        if (!buttonsOn() || !ids.containsKey(name)) return;
        h.post(() -> play(name, 1f, 1f));
    }

    private int play(String name, float l, float r) {
        Integer id = ids.get(name);
        if (id == null) return 0;
        synchronized (this) {
            if (!loaded.contains(id)) { pending = name; pendingT = SystemClock.uptimeMillis(); return 0; }
        }
        int s = pool.play(id, l, r, 1, 0, 1f);
        if (s != 0) {
            long now = SystemClock.uptimeMillis();
            for (int i = playing.size() - 1; i >= 0; i--) if (now - playing.get(i).t > 4000) playing.remove(i);
            playing.add(new Play(s, l, r));
        }
        return s;
    }

    /** La deschiderea aplicatiei: pasari si picaturi, apoi "slide" cand apare ecranul principal. */
    void appStart() {
        ambientAfter = SystemClock.uptimeMillis() + 2450;
        if (!buttonsOn()) return;
        play("start", 1f, 1f);
        h.postDelayed(() -> { if (foreground > 0 && buttonsOn()) play("slide", 1f, 1f); }, 2150);
    }

    // ---------------- pasarile din fundal ----------------

    /** Un ecran al aplicatiei a aparut (onResume). */
    void resumed() {
        foreground++;
        h.removeCallbacks(stop);
        if (birdsOn() && !ambient) {
            long wait = Math.max(600, ambientAfter - SystemClock.uptimeMillis());
            startAmbient(wait);
        }
    }

    /** Un ecran al aplicatiei a disparut (onPause). Daca nu urmeaza altul, sunetele se sting. */
    void paused() {
        foreground = Math.max(0, foreground - 1);
        if (foreground == 0) h.postDelayed(stop, 700);
    }

    private final Runnable stop = () -> { if (foreground == 0) { stopAmbient(); fadeAll(); } };

    private void startAmbient(long delay) {
        if (ambient) return;
        ambient = true;
        ambientStart = SystemClock.uptimeMillis() + delay;
        h.removeCallbacks(bird);
        h.postDelayed(bird, delay);
    }

    private void stopAmbient() {
        ambient = false;
        h.removeCallbacks(bird);
    }

    private final Runnable bird = new Runnable() {
        @Override public void run() {
            if (!ambient) return;
            if (birdsOn() && foreground > 0) {
                float v = volume() / 100f;
                float in = Math.min(1f, (SystemClock.uptimeMillis() - ambientStart + 600) / 2600f);   // primele secunde: mai incet
                v *= Math.max(0.3f, in);
                float pan = rnd.nextFloat() * 1.6f - 0.8f;               // unele pasari in stanga, altele in dreapta
                float l = v * (pan > 0 ? 1 - pan : 1), r = v * (pan < 0 ? 1 + pan : 1);
                if (v > 0.001f) play("bird" + (1 + rnd.nextInt(5)), l, r);
            }
            h.postDelayed(this, 1800 + rnd.nextInt(4200));
        }
    };

    /** Stinge in 0,6 s tot ce se mai aude (cand iesi din aplicatie). */
    private void fadeAll() {
        final List<Play> list = new ArrayList<>(playing);
        playing.clear();
        if (list.isEmpty()) return;
        for (int k = 1; k <= 6; k++) {
            final float f = 1f - k / 6f;
            h.postDelayed(() -> {
                for (Play p : list) {
                    if (f <= 0f) pool.stop(p.stream);
                    else pool.setVolume(p.stream, p.l * f, p.r * f);
                }
            }, k * 100L);
        }
    }
}
