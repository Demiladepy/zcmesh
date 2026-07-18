package zcmesh.pipeline;

import zcmesh.wire.WireFrame;

import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReferenceArray;

/**
 * Power-of-two SPSC ring. Producer: NIO thread. Consumer: UI/tick.
 * AtomicReferenceArray provides the store/load barriers between threads.
 */
public final class FrameRing {
    private final AtomicReferenceArray<WireFrame> slots;
    private final int mask;
    private final AtomicLong head = new AtomicLong(0);
    private final AtomicLong tail = new AtomicLong(0);
    private final AtomicLong drops = new AtomicLong(0);

    public FrameRing(int capacityPow2) {
        if (capacityPow2 < 2 || (capacityPow2 & (capacityPow2 - 1)) != 0) {
            throw new IllegalArgumentException("capacity must be power of two");
        }
        this.slots = new AtomicReferenceArray<>(capacityPow2);
        this.mask = capacityPow2 - 1;
    }

    public boolean offer(WireFrame frame) {
        long h = head.get();
        long t = tail.get();
        if (h - t >= slots.length()) {
            if (tail.compareAndSet(t, t + 1)) {
                drops.incrementAndGet();
            }
            t = tail.get();
        }
        slots.set((int) (h & mask), frame);
        head.lazySet(h + 1);
        return true;
    }

    public WireFrame poll() {
        long t = tail.get();
        long h = head.get();
        if (t >= h) {
            return null;
        }
        int idx = (int) (t & mask);
        WireFrame f = slots.get(idx);
        slots.lazySet(idx, null);
        tail.lazySet(t + 1);
        return f;
    }

    public int size() {
        return (int) (head.get() - tail.get());
    }

    public long drops() {
        return drops.get();
    }
}
