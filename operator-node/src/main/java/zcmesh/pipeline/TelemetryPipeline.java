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
    private final AtomicLong lastOfferNs = new AtomicLong(0);
    private final AtomicLong interArrivalEwmaNs = new AtomicLong(0);
    private final LongAdder interArrivalSamples = new LongAdder();

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
        framesOk.increment();
        lastSeq.set(frame.seq);
        ring.offer(frame);
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
            /* alpha ~= 1/16 */
            interArrivalEwmaNs.set(ewma - (ewma >> 4) + (delta >> 4));
        }
        interArrivalSamples.increment();
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

    public long interArrivalEwmaNs() {
        return interArrivalEwmaNs.get();
    }

    public int queued() {
        return ring.size();
    }
}
