package zcmesh.pipeline;

/**
 * Immutable metrics point-in-time view for UI / stats / logging.
 * No JavaFX types — safe to use from NIO threads and future UIs.
 */
public final class OperatorSnapshot {
    public final long framesOk;
    public final long crcFail;
    public final long gaps;
    public final long nodes;
    public final long bytesIn;
    public final long queued;
    public final long lastSeq;
    public final long iaEwmaNs;
    public final long ringDrops;
    public final long sensorVoltage;
    public final long sensorCurrent;
    public final long sensorTemp;
    public final long sensorOther;
    public final double framesPerSec;
    public final double bytesPerSec;
    public final long capturedAtNs;

    public OperatorSnapshot(
            long framesOk, long crcFail, long gaps, long nodes, long bytesIn,
            long queued, long lastSeq, long iaEwmaNs, long ringDrops,
            long sensorVoltage, long sensorCurrent, long sensorTemp, long sensorOther,
            double framesPerSec, double bytesPerSec, long capturedAtNs) {
        this.framesOk = framesOk;
        this.crcFail = crcFail;
        this.gaps = gaps;
        this.nodes = nodes;
        this.bytesIn = bytesIn;
        this.queued = queued;
        this.lastSeq = lastSeq;
        this.iaEwmaNs = iaEwmaNs;
        this.ringDrops = ringDrops;
        this.sensorVoltage = sensorVoltage;
        this.sensorCurrent = sensorCurrent;
        this.sensorTemp = sensorTemp;
        this.sensorOther = sensorOther;
        this.framesPerSec = framesPerSec;
        this.bytesPerSec = bytesPerSec;
        this.capturedAtNs = capturedAtNs;
    }

    public String toPlainText() {
        return "zcmesh_stats 1\n"
                + "frames_ok=" + framesOk + "\n"
                + "crc_fail=" + crcFail + "\n"
                + "gaps=" + gaps + "\n"
                + "nodes=" + nodes + "\n"
                + "bytes=" + bytesIn + "\n"
                + "queued=" + queued + "\n"
                + "last_seq=" + lastSeq + "\n"
                + "ia_ewma_ns=" + iaEwmaNs + "\n"
                + "ring_drops=" + ringDrops + "\n"
                + "sensor_voltage=" + sensorVoltage + "\n"
                + "sensor_current=" + sensorCurrent + "\n"
                + "sensor_temp=" + sensorTemp + "\n"
                + "sensor_other=" + sensorOther + "\n"
                + "frames_s=" + (long) framesPerSec + "\n"
                + "bytes_s=" + (long) bytesPerSec + "\n";
    }
}
