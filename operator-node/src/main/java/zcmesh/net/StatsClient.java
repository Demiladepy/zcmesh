package zcmesh.net;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Scrape StatsServer: TCP connect → one ASCII snapshot → close.
 */
public final class StatsClient {
    private StatsClient() {}

    public static String scrape(String host, int port) throws IOException {
        return scrape(host, port, 3000);
    }

    public static String scrape(String host, int port, int timeoutMs) throws IOException {
        try (Socket sock = new Socket()) {
            sock.connect(new InetSocketAddress(host, port), timeoutMs);
            sock.setSoTimeout(timeoutMs);
            InputStream in = sock.getInputStream();
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] tmp = new byte[4096];
            int n;
            while ((n = in.read(tmp)) >= 0) {
                if (n > 0) {
                    buf.write(tmp, 0, n);
                }
                if (n == 0) {
                    break;
                }
            }
            return buf.toString(java.nio.charset.StandardCharsets.US_ASCII);
        }
    }

    public static Map<String, String> parse(String plain) {
        Map<String, String> m = new LinkedHashMap<>();
        if (plain == null) {
            return m;
        }
        for (String line : plain.split("\n")) {
            String t = line.trim();
            int eq = t.indexOf('=');
            if (eq > 0) {
                m.put(t.substring(0, eq), t.substring(eq + 1));
            }
        }
        return m;
    }

    public static long longField(Map<String, String> fields, String key) {
        String v = fields.get(key);
        if (v == null || v.isEmpty()) {
            throw new IllegalArgumentException("missing " + key);
        }
        return Long.parseLong(v.trim());
    }

    public static long requireLong(Map<String, String> fields, String key) {
        return longField(fields, key);
    }
}
