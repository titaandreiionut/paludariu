package ro.paludariu.app;

import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.Window;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Culori si componente comune, in tema "Padure" a paginii web. */
final class Ui {
    static final int BG = 0xFF0B1610;
    static final int CARD = 0xFF13251B;
    static final int LINE = 0x29A0D2AA;
    static final int TXT = 0xFFE8F1E7;
    static final int MUTED = 0xFF93A99A;
    static final int GREEN = 0xFF86CF8E;
    static final int ON_GREEN = 0xFF0C1A12;
    static final int BAD = 0xFFEF6F5E;

    private Ui() {}

    static int dp(Context c, float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics()));
    }

    static void darkBars(Activity a) {
        Window w = a.getWindow();
        w.setStatusBarColor(BG);
        w.setNavigationBarColor(BG);
    }

    static GradientDrawable round(int fill, int stroke, float radiusDp, Context c) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(c, radiusDp));
        if (stroke != 0) g.setStroke(dp(c, 1), stroke);
        return g;
    }

    /** Card in forma de frunza: un colt (stanga-jos) ascutit, ca pe pagina web. */
    static GradientDrawable leaf(Context c) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(CARD);
        float r = dp(c, 22), s = dp(c, 8);
        g.setCornerRadii(new float[]{r, r, r, r, r, r, s, s});
        g.setStroke(dp(c, 1), LINE);
        return g;
    }

    static TextView text(Context c, String s, float sp, int color, boolean bold) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    static TextView title(Context c, String s, float sp) {
        TextView t = text(c, s, sp, TXT, false);
        t.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        return t;
    }

    static Button primary(Context c, String s) {
        Button b = new Button(c);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setTextColor(ON_GREEN);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(round(GREEN, 0, 99, c));
        b.setGravity(Gravity.CENTER);
        b.setMinHeight(dp(c, 52));
        b.setStateListAnimator(null);
        return b;
    }

    static Button secondary(Context c, String s) {
        Button b = primary(c, s);
        b.setTextColor(TXT);
        b.setTypeface(Typeface.DEFAULT);
        b.setBackground(round(CARD, LINE, 99, c));
        return b;
    }

    static EditText input(Context c, String hint) {
        EditText e = new EditText(c);
        e.setHint(hint);
        e.setSingleLine(true);
        e.setTextColor(TXT);
        e.setHintTextColor(MUTED);
        e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        e.setBackground(round(CARD, LINE, 14, c));
        int p = dp(c, 14);
        e.setPadding(p, p, p, p);
        return e;
    }

    static LinearLayout.LayoutParams wide(Context c, int topDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(c, topDp);
        return lp;
    }
}
