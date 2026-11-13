package ro.paludariu.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.net.Uri;
import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Jurnalul foto (1.8.9): pozele paludariului stau in telefon, in memoria aplicatiei, cate un dosar pe placa.
 * Fiecare poza: <id>.jpg (cel mult 1600 px), <id>_t.jpg (miniatura) si <id>.json (cand, temperatura, umiditatea).
 */
final class Foto {
    private Foto() {}

    static File dir(Context c, String board) {
        File d = new File(c.getFilesDir(), "foto_" + Copii.safe(board));
        if (!d.exists()) d.mkdirs();
        return d;
    }

    private static boolean idOk(String id) { return id != null && id.matches("[0-9]{6,16}"); }

    /** Lista pentru pagina, cele mai noi primele: [{"id","t","temp","hum","th":"data:image/jpeg;base64,..."}] */
    static String list(Context c, String board) {
        JSONArray a = new JSONArray();
        File[] f = dir(c, board).listFiles((d, n) -> n.endsWith(".json"));
        if (f == null) return "[]";
        Arrays.sort(f, (x, y) -> y.getName().compareTo(x.getName()));
        for (File m : f) {
            String id = m.getName().replace(".json", "");
            try {
                JSONObject j = new JSONObject(new String(readAll(m), StandardCharsets.UTF_8));
                File th = new File(m.getParentFile(), id + "_t.jpg");
                if (!th.exists()) continue;
                JSONObject o = new JSONObject();
                o.put("id", id);
                o.put("t", j.optLong("t"));
                if (j.has("temp")) o.put("temp", j.optDouble("temp"));
                if (j.has("hum")) o.put("hum", j.optDouble("hum"));
                o.put("th", "data:image/jpeg;base64," + Base64.encodeToString(readAll(th), Base64.NO_WRAP));
                a.put(o);
            } catch (Exception ignored) {
            }
        }
        return a.toString();
    }

    static String image(Context c, String board, String id) {
        if (!idOk(id)) return "";
        File f = new File(dir(c, board), id + ".jpg");
        try {
            return "data:image/jpeg;base64," + Base64.encodeToString(readAll(f), Base64.NO_WRAP);
        } catch (Exception e) {
            return "";
        }
    }

    static void delete(Context c, String board, String id) {
        if (!idOk(id)) return;
        File d = dir(c, board);
        new File(d, id + ".jpg").delete();
        new File(d, id + "_t.jpg").delete();
        new File(d, id + ".json").delete();
    }

    /** Copiaza poza aleasa (galerie sau camera) in jurnal, micsorata si intoarsa dupa EXIF. */
    static boolean add(Context c, String board, Uri src, String temp, String hum) {
        try {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inJustDecodeBounds = true;
            try (InputStream in = c.getContentResolver().openInputStream(src)) { BitmapFactory.decodeStream(in, null, o); }
            if (o.outWidth <= 0) return false;
            int sample = 1;
            while (Math.max(o.outWidth, o.outHeight) / (sample * 2) >= 1600) sample *= 2;
            BitmapFactory.Options o2 = new BitmapFactory.Options();
            o2.inSampleSize = sample;
            Bitmap bm;
            try (InputStream in = c.getContentResolver().openInputStream(src)) { bm = BitmapFactory.decodeStream(in, null, o2); }
            if (bm == null) return false;
            int rot = 0;
            try (InputStream in = c.getContentResolver().openInputStream(src)) {
                int or = new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
                rot = or == ExifInterface.ORIENTATION_ROTATE_90 ? 90 : or == ExifInterface.ORIENTATION_ROTATE_180 ? 180 : or == ExifInterface.ORIENTATION_ROTATE_270 ? 270 : 0;
            } catch (Exception ignored) { }
            bm = scale(bm, 1600, rot);
            String id = String.valueOf(System.currentTimeMillis());
            File d = dir(c, board);
            write(new File(d, id + ".jpg"), bm, 85);
            Bitmap th = thumb(bm, 320);
            write(new File(d, id + "_t.jpg"), th, 78);
            JSONObject j = new JSONObject();
            j.put("t", Long.parseLong(id));
            try { if (temp != null && !temp.isEmpty()) j.put("temp", Double.parseDouble(temp)); } catch (NumberFormatException ignored) { }
            try { if (hum != null && !hum.isEmpty()) j.put("hum", Double.parseDouble(hum)); } catch (NumberFormatException ignored) { }
            try (FileOutputStream out = new FileOutputStream(new File(d, id + ".json"))) { out.write(j.toString().getBytes(StandardCharsets.UTF_8)); }
            return true;
        } catch (Exception | OutOfMemoryError e) {
            return false;
        }
    }

    private static Bitmap scale(Bitmap b, int max, int rot) {
        float k = Math.min(1f, max / (float) Math.max(b.getWidth(), b.getHeight()));
        if (k >= 1f && rot == 0) return b;
        Matrix m = new Matrix();
        m.postScale(k, k);
        if (rot != 0) m.postRotate(rot);
        return Bitmap.createBitmap(b, 0, 0, b.getWidth(), b.getHeight(), m, true);
    }

    /** Miniatura patrata (decupata din mijloc). */
    private static Bitmap thumb(Bitmap b, int size) {
        int s = Math.min(b.getWidth(), b.getHeight());
        Bitmap sq = Bitmap.createBitmap(b, (b.getWidth() - s) / 2, (b.getHeight() - s) / 2, s, s);
        return Bitmap.createScaledBitmap(sq, size, size, true);
    }

    private static void write(File f, Bitmap b, int q) throws Exception {
        try (FileOutputStream out = new FileOutputStream(f)) { b.compress(Bitmap.CompressFormat.JPEG, q, out); }
    }

    static byte[] readAll(File f) throws Exception {
        try (FileInputStream in = new FileInputStream(f); ByteArrayOutputStream o = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            return o.toByteArray();
        }
    }
}
