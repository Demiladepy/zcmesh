package zcmesh.net;

import zcmesh.operator.OperatorRuntime;

import java.util.Map;

/**
 * Boots a short-lived OperatorRuntime and scrapes StatsServer.
 */
public final class StatsClientCheck {
    public static void main(String[] args) throws Exception {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 19900;
        try (OperatorRuntime rt = new OperatorRuntime(port, 1024, null, true, null)) {
            rt.start();
            Thread.sleep(400);
            String plain = StatsClient.scrape("127.0.0.1", rt.statsPort());
            Map<String, String> m = StatsClient.parse(plain);
            if (!plain.startsWith("zcmesh_stats")) {
                throw new IllegalStateException("bad banner: " + plain);
            }
            long ok = StatsClient.requireLong(m, "frames_ok");
            long nodes = StatsClient.requireLong(m, "nodes");
            if (ok < 0 || nodes < 0) {
                throw new IllegalStateException("negative counters");
            }
            System.out.println("StatsClientCheck: OK port=" + rt.statsPort()
                    + " frames_ok=" + ok + " nodes=" + nodes);
        }
    }

    private StatsClientCheck() {}
}
