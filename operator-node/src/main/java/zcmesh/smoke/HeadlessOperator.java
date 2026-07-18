package zcmesh.smoke;

import zcmesh.net.FrameReceiver;
import zcmesh.pipeline.TelemetryPipeline;

/**
 * Headless soak listener for CI / local smoke — no JavaFX.
 * Args: [port] [minFrames] [timeoutSec]
 */
public final class HeadlessOperator {
    public static void main(String[] args) throws Exception {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 9900;
        long minFrames = args.length > 1 ? Long.parseLong(args[1]) : 100;
        int timeoutSec = args.length > 2 ? Integer.parseInt(args[2]) : 30;

        TelemetryPipeline pipeline = new TelemetryPipeline(8192);
        FrameReceiver receiver = new FrameReceiver(port, pipeline);
        Thread t = new Thread(receiver, "frame-receiver");
        t.setDaemon(true);
        t.start();

        long deadline = System.nanoTime() + timeoutSec * 1_000_000_000L;
        while (System.nanoTime() < deadline) {
            long ok = pipeline.framesOk();
            if (ok >= minFrames) {
                System.out.printf("SMOKE_OK frames=%d crc_fail=%d last_seq=%d bytes=%d%n",
                        ok, pipeline.framesCrcFail(), pipeline.lastSeq(), pipeline.bytesIn());
                receiver.stop();
                System.exit(0);
            }
            Thread.sleep(100);
        }
        System.err.printf("SMOKE_FAIL frames=%d crc_fail=%d last_seq=%d%n",
                pipeline.framesOk(), pipeline.framesCrcFail(), pipeline.lastSeq());
        receiver.stop();
        System.exit(1);
    }

    private HeadlessOperator() {}
}
