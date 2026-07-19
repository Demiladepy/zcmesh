package zcmesh.net;

import java.util.Map;

/**
 * Offline parse check for StatsServer plaintext (no live socket).
 * Live scrape: {@code java zcmesh.net.StatsScrape host port [minFrames]}
 */
public final class StatsScrape {
    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            String sample = ""
                    + "zcmesh_stats 1\n"
                    + "frames_ok=120\n"
                    + "crc_fail=0\n"
                    + "gaps=0\n"
                    + "nodes=2\n"
                    + "hop_idx_0=10\n"
                    + "hop_idx_1=110\n"
                    + "frames_s=400\n";
            Map<String, String> f = StatsClient.parse(sample);
            if (StatsClient.longField(f, "frames_ok") != 120) {
                throw new IllegalStateException("parse frames_ok");
            }
            if (StatsClient.longField(f, "nodes") != 2) {
                throw new IllegalStateException("parse nodes");
            }
            System.out.println("StatsScrapeCheck: OK");
            return;
        }
        String host = args[0];
        int port = Integer.parseInt(args[1]);
        long minFrames = args.length > 2 ? Long.parseLong(args[2]) : 1;
        String text = StatsClient.scrape(host, port);
        System.out.print(text);
        Map<String, String> f = StatsClient.parse(text);
        long ok = StatsClient.longField(f, "frames_ok");
        if (ok < minFrames) {
            System.err.println("frames_ok=" + ok + " < min " + minFrames);
            System.exit(1);
        }
        System.out.println("STATS_OK frames_ok=" + ok);
    }

    private StatsScrape() {}
}
