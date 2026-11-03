package ro.paludariu.app;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * Client MQTT 3.1.1 minimal, doar ce ne trebuie: conectare criptata (TLS), abonare, publicare (QoS 0), ping.
 * Fara biblioteci externe. Certificatul serverului e verificat de Android, inclusiv numele lui.
 */
final class MiniMqtt {
    interface Listener {
        void onMessage(String topic, byte[] payload);
        void onClosed(Exception why);
    }

    private SSLSocket sock;
    private InputStream in;
    private OutputStream out;
    private volatile boolean open = false;
    private int packetId = 1;
    private Listener listener;
    private Thread reader, pinger;

    /** Codul primit de la server la conectare: 0 = acceptat, 4 = utilizator/parola gresite, 5 = neautorizat. */
    static final class RefusedException extends IOException {
        final int code;
        RefusedException(int code) {
            super(code == 4 || code == 5 ? "Serverul MQTT a refuzat utilizatorul sau parola." : "Serverul MQTT a refuzat conectarea (cod " + code + ").");
            this.code = code;
        }
    }

    /** "xxx.emqxsl.com", "xxx.emqxsl.com:8883" sau "mqtts://xxx:8883" */
    static String[] hostPort(String h) {
        h = h.trim();
        int s = h.indexOf("://");
        if (s >= 0) h = h.substring(s + 3);
        while (h.endsWith("/")) h = h.substring(0, h.length() - 1);
        int c = h.lastIndexOf(':');
        if (c > 0) return new String[]{h.substring(0, c), h.substring(c + 1)};
        return new String[]{h, "8883"};
    }

    synchronized void connect(String hostSpec, String clientId, String user, String pass, int keepAliveSec, Listener l) throws IOException {
        String[] hp = hostPort(hostSpec);
        String host = hp[0];
        int port;
        try { port = Integer.parseInt(hp[1]); } catch (NumberFormatException e) { port = 8883; }
        Socket raw = new Socket();
        raw.connect(new InetSocketAddress(host, port), 8000);
        sock = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket(raw, host, port, true);
        sock.setSoTimeout(15000);
        javax.net.ssl.SSLParameters sp = sock.getSSLParameters();
        sp.setEndpointIdentificationAlgorithm("HTTPS");   // certificatul trebuie sa fie chiar pentru adresa asta
        sock.setSSLParameters(sp);
        sock.startHandshake();
        boolean android = String.valueOf(System.getProperty("java.vendor")).toLowerCase().contains("android");
        if (android && !HttpsURLConnection.getDefaultHostnameVerifier().verify(host, sock.getSession())) {
            sock.close();
            throw new IOException("Certificatul serverului nu corespunde adresei " + host + ".");
        }
        in = sock.getInputStream();
        out = sock.getOutputStream();
        listener = l;

        // CONNECT
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        str(v, "MQTT");
        v.write(4);                                   // MQTT 3.1.1
        int flags = 0x02;                             // sesiune curata
        if (user != null && !user.isEmpty()) flags |= 0x80;
        if (pass != null && !pass.isEmpty()) flags |= 0x40;
        v.write(flags);
        v.write(keepAliveSec >> 8);
        v.write(keepAliveSec & 0xff);
        str(v, clientId);
        if ((flags & 0x80) != 0) str(v, user);
        if ((flags & 0x40) != 0) str(v, pass);
        send(0x10, v.toByteArray());
        int[] h = header();
        byte[] b = body(h[1]);
        if (h[0] != 0x20 || b.length < 2) throw new IOException("Răspuns neașteptat de la serverul MQTT.");
        if (b[1] != 0) throw new RefusedException(b[1] & 0xff);
        open = true;
        sock.setSoTimeout(0);

        reader = new Thread(this::readLoop, "mqtt-citire");
        reader.setDaemon(true);
        reader.start();
        final long pingMs = Math.max(10, keepAliveSec / 2) * 1000L;
        pinger = new Thread(() -> {
            while (open) {
                try { Thread.sleep(pingMs); } catch (InterruptedException e) { return; }
                try { if (open) send(0xC0, new byte[0]); } catch (IOException e) { close(e); return; }
            }
        }, "mqtt-ping");
        pinger.setDaemon(true);
        pinger.start();
    }

    void subscribe(String topic) throws IOException {
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        int id = nextId();
        v.write(id >> 8);
        v.write(id & 0xff);
        str(v, topic);
        v.write(0);   // QoS 0
        send(0x82, v.toByteArray());
    }

    void publish(String topic, byte[] payload, boolean retain) throws IOException {
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        str(v, topic);
        v.write(payload, 0, payload.length);
        send(0x30 | (retain ? 1 : 0), v.toByteArray());
    }

    boolean isOpen() { return open; }

    void disconnect() {
        if (open) {
            try { send(0xE0, new byte[0]); } catch (IOException ignored) { }
        }
        close(null);
    }

    private synchronized int nextId() {
        packetId = packetId % 65535 + 1;
        return packetId;
    }

    private void close(Exception why) {
        boolean was = open;
        open = false;
        try { if (sock != null) sock.close(); } catch (IOException ignored) { }
        if (pinger != null) pinger.interrupt();
        if (was && listener != null) listener.onClosed(why);
    }

    private void readLoop() {
        try {
            while (open) {
                int[] h = header();
                byte[] b = body(h[1]);
                int type = h[0] & 0xF0;
                if (type == 0x30) {                       // PUBLISH
                    int tl = ((b[0] & 0xff) << 8) | (b[1] & 0xff);
                    String topic = new String(b, 2, tl, StandardCharsets.UTF_8);
                    int off = 2 + tl;
                    if (((h[0] >> 1) & 3) > 0) off += 2;   // QoS > 0: are si numar de pachet
                    byte[] p = new byte[b.length - off];
                    System.arraycopy(b, off, p, 0, p.length);
                    if (listener != null) listener.onMessage(topic, p);
                }
                // SUBACK, PINGRESP: nu e nimic de facut
            }
        } catch (Exception e) {
            close(open ? e : null);
        }
    }

    // ---------- pachete ----------

    private synchronized void send(int type, byte[] body) throws IOException {
        ByteArrayOutputStream p = new ByteArrayOutputStream(body.length + 5);
        p.write(type);
        int len = body.length;
        do {
            int d = len % 128;
            len /= 128;
            if (len > 0) d |= 0x80;
            p.write(d);
        } while (len > 0);
        p.write(body, 0, body.length);
        out.write(p.toByteArray());
        out.flush();
    }

    private int[] header() throws IOException {
        int t = in.read();
        if (t < 0) throw new EOFException();
        int mult = 1, len = 0, d;
        do {
            d = in.read();
            if (d < 0) throw new EOFException();
            len += (d & 127) * mult;
            mult *= 128;
        } while ((d & 128) != 0 && mult <= 128 * 128 * 128);
        return new int[]{t, len};
    }

    private byte[] body(int len) throws IOException {
        byte[] b = new byte[len];
        int n = 0;
        while (n < len) {
            int r = in.read(b, n, len - n);
            if (r < 0) throw new EOFException();
            n += r;
        }
        return b;
    }

    private static void str(ByteArrayOutputStream o, String s) {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        o.write(b.length >> 8);
        o.write(b.length & 0xff);
        o.write(b, 0, b.length);
    }
}
