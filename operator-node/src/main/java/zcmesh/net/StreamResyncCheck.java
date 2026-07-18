package zcmesh.net;

import zcmesh.wire.WireFrame;
import zcmesh.wire.WireFrameGolden;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Self-check: magic scan recovers framing after injected garbage prefix.
 */
public final class StreamResyncCheck {
    public static void main(String[] args) {
        byte[] raw = WireFrameGolden.hexToBytes(WireFrameGolden.HEX);
        ByteBuffer buf = ByteBuffer.allocate(3 + WireFrame.SIZE).order(ByteOrder.LITTLE_ENDIAN);
        buf.put((byte) 0x11);
        buf.put((byte) 0x22);
        buf.put((byte) 0x33);
        buf.put(raw);
        buf.flip();

        int found = FrameReceiver.findMagic(buf, 0);
        if (found != 3) {
            System.err.println("StreamResyncCheck: expected magic at 3, got " + found);
            System.exit(1);
        }
        ByteBuffer slice = buf.duplicate();
        slice.position(found);
        slice.limit(found + WireFrame.SIZE);
        WireFrame frame = WireFrame.decode(slice);
        if (!frame.crcOk || frame.seq != 42) {
            System.err.println("StreamResyncCheck: decode after resync failed crcOk="
                    + frame.crcOk + " seq=" + frame.seq);
            System.exit(1);
        }
        if (FrameReceiver.findMagic(buf, 0) != 3) {
            System.err.println("StreamResyncCheck: findMagic unstable");
            System.exit(1);
        }
        System.out.println("StreamResyncCheck: OK");
    }
}
