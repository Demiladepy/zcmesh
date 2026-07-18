package zcmesh.pipeline;

import zcmesh.wire.WireFrame;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

public final class TelemetryPipeline {
    private final ArrayBlockingQueue<WireFrame> queue;
    private final LongAdder framesOk = new LongAdder();
    private final LongAdder framesCrcFail = new LongAdder();
    private final LongAdder bytesIn = new LongAdder();
    private final AtomicLong lastSeq = new AtomicLong(-1);

    public TelemetryPipeline(int capacity) {
        this.queue = new ArrayBlockingQueue<>(capacity);
    }

    public void offer(WireFrame frame) {
        bytesIn.add(WireFrame.SIZE);
        if (!frame.crcOk) {
            framesCrcFail.increment();
            return;
        }
        framesOk.increment();
        lastSeq.set(frame.seq);
        if (!queue.offer(frame)) {
            queue.poll();
            queue.offer(frame);
        }
    }

    public WireFrame poll(long timeoutMs) throws InterruptedException {
        return queue.poll(timeoutMs, TimeUnit.MILLISECONDS);
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

    public long lastSeq() {
        return lastSeq.get();
    }

    public int queued() {
        return queue.size();
    }
}
