package zcmesh.pipeline;

import zcmesh.wire.WireFrame;

import java.util.concurrent.atomic.AtomicLong;

/**
 * Power-of-two SPSC ring of decoded frames. Producer: NIO thread. Consumer: UI/tick.
 * No heap allocation on offer/poll after construction.
 */
public final class FrameRing {
    private final WireFrame[] slots;
    private final int mask;
    private final AtomicLong head = new AtomicLong(0); /* next write */
    private final AtomicLong tail = new AtomicLong(0); /* next read */

    public FrameRing(int capacityPow2) {
        if (capacityPow2 < 2 || (capacityPow2 & (capacityPow2 - 1)) != 0) {
            throw new IllegalArgumentException("capacity must be power of two");
        }
        this.slots = new WireFrame[capacityPow2];
        this.mask = capacityPow2 - 1;
    }

    public boolean offer(WireFrame frame) {
        long h = head.get();
        long t = tail.get();
        if (h - t >= slots.length) {
            /* Drop oldest to keep streaming under backpressure. */
            tail.compareAndSet(t, t + 1);
            t++;
        }
        slots[(int) (h & mask)] = frame;
        head.lazySet(h + 1);
        return true;
    }

    public WireFrame poll() {
        long t = tail.get();
        long h = head.get();
        if (t >= h) {
            return null;
        }
        WireFrame f = slots[(int) (t & mask)];
        slots[(int) (t & mask)] = null;
        tail.lazySet(t + 1);
        return f;
    }

    public int size() {
        return (int) (head.get() - tail.get());
    }
}
