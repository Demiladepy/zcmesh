package zcmesh.wire;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Cross-language golden: must match shared/golden_frame.hex and C++ zcmesh_test_frame.
 */
public final class WireFrameGolden {
    public static final String HEX =
            "435a01022a0000008877665507000200d7dcffff8984ecfd";
    public static final long CRC = 0xFDEC8489L;

    public static void main(String[] args) {
        byte[] raw = hexToBytes(HEX);
        if (raw.length != WireFrame.SIZE) {
            fail("size");
        }
        ByteBuffer buf = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
        WireFrame f = WireFrame.decode(buf);
        if (!f.crcOk) {
            fail("crcOk");
        }
        if (f.checksum != CRC) {
            fail("checksum want=" + Long.toHexString(CRC) + " got=" + Long.toHexString(f.checksum));
        }
        if (f.seq != 42 || f.nodeId != 7 || f.sensorType != 2 || f.rawValue != -9001) {
            fail("fields");
        }
        if (f.timestampLo != 0x55667788L) {
            fail("timestamp");
        }
        ByteBuffer enc = ByteBuffer.allocate(WireFrame.SIZE).order(ByteOrder.LITTLE_ENDIAN);
        f.encode(enc);
        enc.flip();
        byte[] round = new byte[WireFrame.SIZE];
        enc.get(round);
        for (int i = 0; i < WireFrame.SIZE; i++) {
            if (round[i] != raw[i]) {
                fail("encode mismatch at " + i);
            }
        }
        System.out.println("WireFrameGolden: OK");
    }

    private static byte[] hexToBytes(String hex) {
        int n = hex.length();
        byte[] out = new byte[n / 2];
        for (int i = 0; i < n; i += 2) {
            out[i / 2] = (byte) Integer.parseInt(hex.substring(i, i + 2), 16);
        }
        return out;
    }

    private static void fail(String msg) {
        System.err.println("WireFrameGolden FAIL: " + msg);
        System.exit(1);
    }

    private WireFrameGolden() {}
}
