package zcmesh.net;

/**
 * CLI mirror of zcmesh_ctl for operator / soak scripts.
 * Args: host:port --node-id N (--skip MASK | --clear)
 */
public final class ControlCli {
    public static void main(String[] args) throws Exception {
        if (args.length < 4) {
            System.err.println("Usage: ControlCli host:port --node-id N (--skip MASK | --clear)");
            System.exit(2);
        }
        String target = args[0];
        int nodeId = -1;
        Integer skip = null;
        boolean clear = false;
        for (int i = 1; i < args.length; ++i) {
            if (args[i].equals("--node-id") && i + 1 < args.length) {
                nodeId = Integer.parseInt(args[++i]);
            } else if (args[i].equals("--skip") && i + 1 < args.length) {
                skip = Integer.parseInt(args[++i]);
            } else if (args[i].equals("--clear")) {
                clear = true;
            } else {
                System.err.println("bad arg: " + args[i]);
                System.exit(2);
            }
        }
        if (nodeId < 0 || (skip == null && !clear) || (skip != null && clear)) {
            System.err.println("need --node-id and exactly one of --skip / --clear");
            System.exit(2);
        }
        try (ControlClient c = ControlClient.parse(target)) {
            if (clear) {
                c.clear(nodeId);
                System.out.println("CLEAR node=" + nodeId + " -> " + target);
            } else {
                c.setSkip(nodeId, skip);
                System.out.println("SET_SKIP node=" + nodeId + " mask=" + skip + " -> " + target);
            }
        }
    }

    private ControlCli() {}
}
