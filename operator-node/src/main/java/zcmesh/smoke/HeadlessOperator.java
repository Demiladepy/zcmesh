package zcmesh.smoke;

import zcmesh.operator.OperatorRuntime;
import zcmesh.pipeline.OperatorSnapshot;

import java.nio.file.Path;

/**
 * Headless soak — uses OperatorRuntime (same core as future UI).
 * Args: [port] [minFrames] [timeoutSec] [record.zcm|-] [udp|tcp]
 */
public final class HeadlessOperator {
    public static void main(String[] args) throws Exception {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 9900;
        long minFrames = args.length > 1 ? Long.parseLong(args[1]) : 100;
        int timeoutSec = args.length > 2 ? Integer.parseInt(args[2]) : 30;
        Path recordPath = null;
        if (args.length > 3 && !args[3].equals("-") && !args[3].isEmpty()) {
            recordPath = Path.of(args[3]);
        }
        boolean tcpEnabled = true;
        if (args.length > 4 && args[4].equalsIgnoreCase("udp")) {
            tcpEnabled = false;
        }

        int exitCode = 1;
        try (OperatorRuntime runtime = new OperatorRuntime(port, 8192, recordPath, tcpEnabled)) {
            runtime.start();
            long deadline = System.nanoTime() + timeoutSec * 1_000_000_000L;
            while (System.nanoTime() < deadline) {
                runtime.drainToRecorder(256);
                long ok = runtime.pipeline().framesOk();
                if (ok >= minFrames) {
                    OperatorSnapshot s = runtime.sampler().snapshot();
                    System.out.printf(
                            "SMOKE_OK frames=%d crc_fail=%d gaps=%d nodes=%d last_seq=%d bytes=%d drops=%d%n",
                            s.framesOk, s.crcFail, s.gaps, s.nodes, s.lastSeq, s.bytesIn, s.ringDrops);
                    if (runtime.recorder() != null) {
                        System.out.printf("RECORD frames=%d path=%s%n",
                                runtime.recorder().frameCount(), recordPath);
                    }
                    exitCode = 0;
                    break;
                }
                Thread.sleep(50);
            }
            if (exitCode != 0) {
                System.err.printf("SMOKE_FAIL frames=%d last_seq=%d%n",
                        runtime.pipeline().framesOk(), runtime.pipeline().lastSeq());
            }
        }
        System.exit(exitCode);
    }

    private HeadlessOperator() {}
}
