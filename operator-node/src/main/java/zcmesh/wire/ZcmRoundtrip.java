package zcmesh.wire;

import java.nio.file.Files;
import java.nio.file.Path;

/** Write/read .zcm via ZcmWriter and verify CRC + count. */
public final class ZcmRoundtrip {
    public static void main(String[] args) throws Exception {
        Path path = Files.createTempFile("zcmesh-rt-", ".zcm");
        try {
            byte[] golden = hexToBytes(WireFrameGolden.HEX);
            WireFrame src = WireFrame.decode(java.nio.ByteBuffer.wrap(golden));
            if (!src.crcOk) {
                fail("golden crc");
            }
            try (ZcmWriter w = new ZcmWriter(path)) {
                for (int i = 0; i < 50; i++) {
                    w.write(src);
                }
            }
            byte[] all = Files.readAllBytes(path);
            if (all.length != ZcmWriter.HEADER_SIZE + 50 * WireFrame.SIZE) {
                fail("size " + all.length);
            }
            int magic = (all[0] & 0xff) | ((all[1] & 0xff) << 8) | ((all[2] & 0xff) << 16) | ((all[3] & 0xff) << 24);
            if (magic != ZcmWriter.MAGIC) {
                fail("magic");
            }
            long count = 0;
            for (int i = 0; i < 8; i++) {
                count |= ((long) (all[8 + i] & 0xff)) << (8 * i);
            }
            if (count != 50) {
                fail("count " + count);
            }
            for (int i = 0; i < 50; i++) {
                int off = ZcmWriter.HEADER_SIZE + i * WireFrame.SIZE;
                WireFrame f = WireFrame.decode(java.nio.ByteBuffer.wrap(all, off, WireFrame.SIZE));
                if (!f.crcOk || f.seq != 42 || f.nodeId != 7) {
                    fail("frame " + i);
                }
            }
            System.out.println("ZcmRoundtrip: OK");
        } finally {
            Files.deleteIfExists(path);
        }
    }

    private static byte[] hexToBytes(String hex) {
        byte[] out = new byte[hex.length() / 2];
        for (int i = 0; i < hex.length(); i += 2) {
            out[i / 2] = (byte) Integer.parseInt(hex.substring(i, i + 2), 16);
        }
        return out;
    }

    private static void fail(String m) {
        System.err.println("ZcmRoundtrip FAIL: " + m);
        System.exit(1);
    }

    private ZcmRoundtrip() {}
}
