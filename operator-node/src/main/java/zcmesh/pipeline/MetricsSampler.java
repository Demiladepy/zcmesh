package zcmesh.pipeline;

/**
 * Rate sampler over TelemetryPipeline counters. Call snapshot() from UI tick / stats.
 */
public final class MetricsSampler {
    private final TelemetryPipeline pipeline;
    private long prevOk;
    private long prevBytes;
    private long prevNs;

    public MetricsSampler(TelemetryPipeline pipeline) {
        this.pipeline = pipeline;
        this.prevOk = pipeline.framesOk();
        this.prevBytes = pipeline.bytesIn();
        this.prevNs = System.nanoTime();
    }

    public OperatorSnapshot snapshot() {
        long now = System.nanoTime();
        long ok = pipeline.framesOk();
        long bytes = pipeline.bytesIn();
        double dt = (now - prevNs) / 1_000_000_000.0;
        double fps = 0;
        double bps = 0;
        if (dt > 0) {
            fps = (ok - prevOk) / dt;
            bps = (bytes - prevBytes) / dt;
        }
        prevOk = ok;
        prevBytes = bytes;
        prevNs = now;
        return new OperatorSnapshot(
                ok,
                pipeline.framesCrcFail(),
                pipeline.seqGaps(),
                pipeline.seqDups(),
                pipeline.uniqueNodes(),
                bytes,
                pipeline.queued(),
                pipeline.lastSeq(),
                pipeline.interArrivalEwmaNs(),
                pipeline.ringDrops(),
                pipeline.sensorVoltage(),
                pipeline.sensorCurrent(),
                pipeline.sensorTemp(),
                pipeline.sensorOther(),
                pipeline.tcpResyncBytes(),
                pipeline.framesLastHop(),
                pipeline.hopIdx0(),
                pipeline.hopIdx1(),
                pipeline.hopIdx2Plus(),
                fps,
                bps,
                now);
    }
}
