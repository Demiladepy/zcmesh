package zcmesh.smoke;

import zcmesh.net.FrameReceiver;
import zcmesh.pipeline.TelemetryPipeline;
import zcmesh.wire.WireFrame;
import zcmesh.wire.ZcmWriter;

import java.nio.file.Path;

/**
 * Headless soak listener for CI / local smoke — no JavaFX.
 * Args: [port] [minFrames] [timeoutSec] [record.zcm]
 */
public final class HeadlessOperator {
    public static void main(String[] args) throws Exception {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 9900;
        long minFrames = args.length > 1 ? Long.parseLong(args[1]) : 100;
        int timeoutSec = args.length > 2 ? Integer.parseInt(args[2]) : 30;
        Path recordPath = args.length > 3 ? Path.of(args[3]) : null;

        TelemetryPipeline pipeline = new TelemetryPipeline(8192);
        FrameReceiver receiver = new FrameReceiver(port, pipeline);
        Thread t = new Thread(receiver, "frame-receiver");
        t.setDaemon(true);
        t.start();

        int exitCode = 1;
        ZcmWriter writer = null;
        try {
            if (recordPath != null) {
                writer = new ZcmWriter(recordPath);
            }
            long deadline = System.nanoTime() + timeoutSec * 1_000_000_000L;
            while (System.nanoTime() < deadline) {
                WireFrame f;
                while ((f = pipeline.poll(0)) != null) {
                    if (writer != null) {
                        writer.write(f);
                    }
                }
                long ok = pipeline.framesOk();
                if (ok >= minFrames) {
                    System.out.printf("SMOKE_OK frames=%d crc_fail=%d gaps=%d nodes=%d last_seq=%d bytes=%d%n",
                            ok, pipeline.framesCrcFail(), pipeline.seqGaps(),
                            pipeline.uniqueNodes(), pipeline.lastSeq(), pipeline.bytesIn());
                    if (writer != null) {
                        System.out.printf("RECORD frames=%d path=%s%n", writer.frameCount(), recordPath);
                    }
                    exitCode = 0;
                    break;
                }
                Thread.sleep(50);
            }
            if (exitCode != 0) {
                System.err.printf("SMOKE_FAIL frames=%d crc_fail=%d last_seq=%d%n",
                        pipeline.framesOk(), pipeline.framesCrcFail(), pipeline.lastSeq());
            }
        } finally {
            receiver.stop();
            if (writer != null) {
                writer.close();
            }
        }
        System.exit(exitCode);
    }

    private HeadlessOperator() {}
}
