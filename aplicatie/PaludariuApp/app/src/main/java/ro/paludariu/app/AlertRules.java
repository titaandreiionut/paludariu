package ro.paludariu.app;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Regulile de alerta, separate de Android ca sa poata fi verificate usor.
 * Primeste valorile citite de la placa (/s) si intoarce problemele active: cod -> mesaj.
 */
final class AlertRules {
    static final float TEMP_MIN = 20f;    // sub asta: prea frig
    static final float TEMP_MAX = 32f;    // peste asta: prea cald
    static final float HUM_MARGIN_LOW = 5f;   // umiditate cu 5% sub pragul de pornire a cetii
    static final float HUM_MARGIN_HIGH = 3f;  // umiditate cu 3% peste pragul de pornire a ventilatorului

    private AlertRules() {}

    /**
     * @param temp     temperatura (null = senzor indisponibil)
     * @param hum      umiditatea (null = senzor indisponibil)
     * @param mistOn   pragul la care porneste ceata (%)
     * @param fanOn    pragul la care porneste ventilatorul (%)
     * @param ledTemp  temperatura radiatorului LED (null = senzor LED lipsa)
     * @param ledOverheat true daca placa a oprit lumina din cauza supraincalzirii
     */
    static Map<String, String> evaluate(Float temp, Float hum, float mistOn, float fanOn,
                                        Float ledTemp, boolean ledOverheat) {
        return evaluate(temp, hum, mistOn, fanOn, ledTemp, ledOverheat, false);
    }

    /** La fel, plus ventilatorul de racire oprit (placa a stins lumina). */
    static Map<String, String> evaluate(Float temp, Float hum, float mistOn, float fanOn,
                                        Float ledTemp, boolean ledOverheat, boolean ledFanFail) {
        Map<String, String> a = new LinkedHashMap<>();
        if (temp == null || hum == null) {
            a.put("senzor", "Senzorul de temperatură și umiditate nu răspunde. Verifică firul DHT22.");
        } else {
            if (temp < TEMP_MIN) a.put("frig", "Temperatura a scăzut la " + fmt(temp) + " °C.");
            if (temp > TEMP_MAX) a.put("cald", "Temperatura a urcat la " + fmt(temp) + " °C.");
            if (hum < mistOn - HUM_MARGIN_LOW)
                a.put("uscat", "Umiditatea a scăzut la " + Math.round(hum) + "% (ceața pornește sub "
                        + Math.round(mistOn) + "%). Verifică apa din generatorul de ceață.");
            if (hum > fanOn + HUM_MARGIN_HIGH)
                a.put("umed", "Umiditatea a urcat la " + Math.round(hum) + "% (ventilatorul pornește peste "
                        + Math.round(fanOn) + "%). Verifică ventilatorul.");
        }
        if (ledOverheat) {
            a.put("led_cald", "Radiatorul LED s-a supraîncălzit" + (ledTemp != null ? " (" + fmt(ledTemp) + " °C)" : "")
                    + ", așa că lumina s-a oprit. Verifică ventilatorul de răcire.");
        } else if (ledTemp == null) {
            a.put("led_senzor", "Senzorul radiatorului LED nu răspunde, deci ventilatorul de răcire merge la maxim.");
        }
        if (ledFanFail) {
            a.put("led_vent", "Ventilatorul de răcire al LED-ului nu se învârte, așa că lumina s-a oprit. "
                    + "Verifică ventilatorul și firul de turație.");
        }
        return a;
    }

    /** Mementourile de intretinere al caror termen a trecut: nume -> zile de la termen (0 = azi). */
    static String reminder(String name, long secondsLeft) {
        long late = -secondsLeft / 86400;
        return "E timpul: " + name + (late >= 1 ? " (trebuia de " + (late == 1 ? "ieri" : late + " zile") + ")" : "")
                + ". Apasă „Gata” la Întreținere când termini.";
    }

    static String fmt(float v) {
        return String.format(java.util.Locale.ROOT, "%.1f", v).replace('.', ',');
    }
}
