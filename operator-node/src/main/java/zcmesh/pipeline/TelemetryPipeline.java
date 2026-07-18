package zcmesh.pipeline;

import zcmesh.wire.WireFrame;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

public final class TelemetryPipeline {
    private final FrameRing ring;
    private final ConcurrentHashMap<Integer, NodeState> latestByNode = new ConcurrentHashMap<>();
    private final LongAdder framesOk = new LongAdder();
    private final LongAdder framesCrcFail = new LongAdder();
    private final LongAdder bytesIn = new LongAdder();
    private final LongAdder seqGaps = new LongAdder();
    private final LongAdder seqDups = new LongAdder();
    private final LongAdder uniqueNodes = new LongAdder();
    private final AtomicLong lastSeq = new AtomicLong(-1);
    private final AtomicLong[] lastSeqByNode = new AtomicLong[65536];
    private final AtomicLong lastOfferNs = new AtomicLong(0);
    private final AtomicLong interArrivalEwmaNs = new AtomicLong(0);
    private final LongAdder sensorVoltage = new LongAdder();
    private final LongAdder sensorCurrent = new LongAdder();
    private final LongAdder sensorTemp = new LongAdder();
    private final LongAdder sensorOther = new LongAdder();
    private final LongAdder tcpResync = new LongAdder();
    private final LongAdder framesLastHop = new LongAdder();
    private final LongAdder hopIdx0 = new LongAdder();
    private final LongAdder hopIdx1 = new LongAdder();
    private final LongAdder hopIdx2Plus = new LongAdder();

    public TelemetryPipeline(int ringCapacityPow2) {
        this.ring = new FrameRing(ringCapacityPow2);
    }

    public void offer(WireFrame frame) {
        bytesIn.add(WireFrame.SIZE);
        if (!frame.crcOk) {
            framesCrcFail.increment();
            return;
        }
        trackGaps(frame);
        trackInterArrival();
        trackSensor(frame.sensorType);
        trackHop(frame);
        framesOk.increment();
        lastSeq.set(frame.seq);
        long now = System.nanoTime();
        latestByNode.put(frame.nodeId, new NodeState(
                frame.nodeId, frame.seq, frame.sensorType, frame.rawValue, frame.timestampLo, now));
        ring.offer(frame);
    }

    private void trackHop(WireFrame frame) {
        if ((frame.flags & WireFrame.FLAG_LAST_HOP) != 0) {
            framesLastHop.increment();
        }
        int hi = frame.reserved & 0xFF;
        if (hi == 0) {
            hopIdx0.increment();
        } else if (hi == 1) {
            hopIdx1.increment();
        } else {
            hopIdx2Plus.increment();
        }
    }

    /** Bytes skipped while hunting for 0x5A43 after a TCP framing desync. */
    public void noteTcpResync(int skippedBytes) {
        if (skippedBytes > 0) {
            tcpResync.add(skippedBytes);
        }
    }

    public void noteCrcFail() {
        framesCrcFail.increment();
    }

    private void trackSensor(int type) {
        switch (type) {
            case 0:
                sensorVoltage.increment();
                break;
            case 1:
                sensorCurrent.increment();
                break;
            case 2:
                sensorTemp.increment();
                break;
            default:
                sensorOther.increment();
                break;
        }
    }

    private void trackInterArrival() {
        long now = System.nanoTime();
        long prev = lastOfferNs.getAndSet(now);
        if (prev == 0) {
            return;
        }
        long delta = now - prev;
        long ewma = interArrivalEwmaNs.get();
        if (ewma == 0) {
            interArrivalEwmaNs.set(delta);
        } else {
            interArrivalEwmaNs.set(ewma - (ewma >> 4) + (delta >> 4));
        }
    }

    private void trackGaps(WireFrame frame) {
        int id = frame.nodeId & 0xFFFF;
        AtomicLong prev = lastSeqByNode[id];
        if (prev == null) {
            AtomicLong created = new AtomicLong(frame.seq);
            synchronized (lastSeqByNode) {
                if (lastSeqByNode[id] == null) {
                    lastSeqByNode[id] = created;
                    uniqueNodes.increment();
                    return;
                }
                prev = lastSeqByNode[id];
            }
        }
        long expected = prev.get() + 1;
        if (frame.seq > expected) {
            seqGaps.add(frame.seq - expected);
        } else if (frame.seq <= prev.get()) {
            seqDups.increment();
        }
        prev.set(frame.seq);
    }

    public WireFrame poll(long timeoutMs) throws InterruptedException {
        WireFrame f = ring.poll();
        if (f != null || timeoutMs <= 0) {
            return f;
        }
        long deadline = System.nanoTime() + timeoutMs * 1_000_000L;
        while (System.nanoTime() < deadline) {
            f = ring.poll();
            if (f != null) {
                return f;
            }
            Thread.sleep(1);
        }
        return null;
    }

    /** Non-consuming view of latest per-node state (for UI tables). */
    public Collection<NodeState> latestNodes() {
        return latestByNode.values();
    }

    public List<NodeState> latestNodesSnapshot() {
        return new ArrayList<>(latestByNode.values());
    }

    public NodeState latestNode(int nodeId) {
        return latestByNode.get(nodeId);
    }

    public long framesOk() {
        return framesOk.sum();
    }

    public long framesCrcFail() {
        return framesCrcFail.sum();
    }

    public long bytesIn() {
        return bytesIn.sum();
    }

    public long seqGaps() {
        return seqGaps.sum();
    }

    public long seqDups() {
        return seqDups.sum();
    }

    public long uniqueNodes() {
        return uniqueNodes.sum();
    }

    public long lastSeq() {
        return lastSeq.get();
    }

    public long interArrivalEwmaNs() {
        return interArrivalEwmaNs.get();
    }

    public long ringDrops() {
        return ring.drops();
    }

    public long sensorVoltage() {
        return sensorVoltage.sum();
    }

    public long sensorCurrent() {
        return sensorCurrent.sum();
    }

    public long sensorTemp() {
        return sensorTemp.sum();
    }

    public long sensorOther() {
        return sensorOther.sum();
    }

    public long tcpResyncBytes() {
        return tcpResync.sum();
    }

    public long framesLastHop() {
        return framesLastHop.sum();
    }

    public long hopIdx0() {
        return hopIdx0.sum();
    }

    public long hopIdx1() {
        return hopIdx1.sum();
    }

    public long hopIdx2Plus() {
        return hopIdx2Plus.sum();
    }

    public int queued() {
        return ring.size();
    }
}
