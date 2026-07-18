package zcmesh.pipeline;

import zcmesh.wire.WireFrame;

import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

public final class TelemetryPipeline {
    private final FrameRing ring;
    private final LongAdder framesOk = new LongAdder();
    private final LongAdder framesCrcFail = new LongAdder();
    private final LongAdder bytesIn = new LongAdder();
    private final LongAdder seqGaps = new LongAdder();
    private final AtomicLong lastSeq = new AtomicLong(-1);
    private final AtomicLong[] lastSeqByNode = new AtomicLong[65536];

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
        framesOk.increment();
        lastSeq.set(frame.seq);
        ring.offer(frame);
    }

    private void trackGaps(WireFrame frame) {
        int id = frame.nodeId & 0xFFFF;
        AtomicLong prev = lastSeqByNode[id];
        if (prev == null) {
            AtomicLong created = new AtomicLong(frame.seq);
            synchronized (lastSeqByNode) {
                if (lastSeqByNode[id] == null) {
                    lastSeqByNode[id] = created;
                    return;
                }
                prev = lastSeqByNode[id];
            }
        }
        long expected = prev.get() + 1;
        if (frame.seq > expected) {
            seqGaps.add(frame.seq - expected);
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

    public long lastSeq() {
        return lastSeq.get();
    }

    public int queued() {
        return ring.size();
    }
}
