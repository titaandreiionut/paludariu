package ro.paludariu.app;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.widget.RemoteViews;

/** Widgetul cu 4 butoane rapide (1.8.9): Ploaie acum, Ceata 5 min, Lumina 1 ora, Vant 10 min. */
public class WidgetRapid extends AppWidgetProvider {
    private static final int[] IDS = {R.id.r_ploaie, R.id.r_ceata, R.id.r_lumina, R.id.r_vant};

    @Override
    public void onUpdate(Context c, AppWidgetManager m, int[] ids) {
        for (int id : ids) {
            RemoteViews v = new RemoteViews(c.getPackageName(), R.layout.widget_rapid);
            for (int k = 0; k < IDS.length; k++) {
                Intent i = new Intent(c, Rapid.class);
                i.putExtra(Rapid.EXTRA_A, Rapid.ACT[k][0]);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION);
                PendingIntent pi = PendingIntent.getActivity(c, 700 + k, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
                v.setOnClickPendingIntent(IDS[k], pi);
            }
            m.updateAppWidget(id, v);
        }
    }
}
