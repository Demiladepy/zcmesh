package zcmesh.operator;

import zcmesh.net.ControlClient;
import zcmesh.net.FrameReceiver;
import zcmesh.net.StatsServer;
import zcmesh.pipeline.MetricsSampler;
import zcmesh.pipeline.TelemetryPipeline;
import zcmesh.wire.WireFrame;
import zcmesh.wire.ZcmWriter;

import java.io.IOException;
import java.nio.file.Path;

/**
 * Headless operator runtime — start/stop without JavaFX.
 * Future UI should own one of these and bind to pipeline()/sampler()/control().
 */
public final class OperatorRuntime implements AutoCloseable {
    private final TelemetryPipeline pipeline;
    private final MetricsSampler sampler;
    private final FrameReceiver receiver;
    private final StatsServer stats;
    private final ControlClient control;
    private final Thread rxThread;
    private final Thread statsThread;
    private final ZcmWriter recorder;
    private final int telemetryPort;
    private final int statsPort;

    public OperatorRuntime(int telemetryPort, int ringPow2, Path recordPath) throws IOException {
        this(telemetryPort, ringPow2, recordPath, true, null);
    }

    public OperatorRuntime(int telemetryPort, int ringPow2, Path recordPath, boolean tcpEnabled)
            throws IOException {
        this(telemetryPort, ringPow2, recordPath, tcpEnabled, null);
    }

    public OperatorRuntime(
            int telemetryPort, int ringPow2, Path recordPath, boolean tcpEnabled, String controlEndpoint)
            throws IOException {
        this.telemetryPort = telemetryPort;
        this.statsPort = telemetryPort + 9;
        this.pipeline = new TelemetryPipeline(ringPow2);
        this.sampler = new MetricsSampler(pipeline);
        this.receiver = new FrameReceiver(telemetryPort, pipeline, tcpEnabled);
        this.stats = new StatsServer(statsPort, sampler);
        this.control = controlEndpoint != null && !controlEndpoint.isEmpty()
                ? ControlClient.parse(controlEndpoint)
                : null;
        this.recorder = recordPath != null ? new ZcmWriter(recordPath) : null;
        this.rxThread = new Thread(receiver, "frame-receiver");
        this.statsThread = new Thread(stats, "stats-server");
        this.rxThread.setDaemon(true);
        this.statsThread.setDaemon(true);
    }

    public void start() {
        rxThread.start();
        statsThread.start();
        System.err.println("OperatorRuntime telemetry=" + telemetryPort + " stats=" + statsPort
                + (control != null ? " control=on" : ""));
    }

    public TelemetryPipeline pipeline() {
        return pipeline;
    }

    public MetricsSampler sampler() {
        return sampler;
    }

    public ZcmWriter recorder() {
        return recorder;
    }

    /** Null if no --control endpoint was configured. */
    public ControlClient control() {
        return control;
    }

    public void setHopSkip(int nodeId, int mask) throws IOException {
        if (control == null) {
            throw new IllegalStateException("no control endpoint configured");
        }
        control.setSkip(nodeId, mask);
    }

    public void clearHopSkip(int nodeId) throws IOException {
        if (control == null) {
            throw new IllegalStateException("no control endpoint configured");
        }
        control.clear(nodeId);
    }

    public int telemetryPort() {
        return telemetryPort;
    }

    public int statsPort() {
        return statsPort;
    }

    /** Drain ring into optional recorder; returns frames drained. */
    public int drainToRecorder(int max) throws IOException, InterruptedException {
        int n = 0;
        while (n < max) {
            WireFrame f = pipeline.poll(0);
            if (f == null) {
                break;
            }
            if (recorder != null) {
                recorder.write(f);
            }
            n++;
        }
        return n;
    }

    @Override
    public void close() throws IOException {
        receiver.stop();
        stats.stop();
        rxThread.interrupt();
        statsThread.interrupt();
        if (control != null) {
            control.close();
        }
        if (recorder != null) {
            recorder.close();
        }
    }
}
