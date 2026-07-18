package zcmesh.wire;

/**
 * Self-check: MeshControl encode/decode matches shared/mesh_control.h layout.
 */
public final class MeshControlCheck {
    public static void main(String[] args) {
        MeshControl skip = MeshControl.setSkip(7, 0x01);
        byte[] raw = skip.encode();
        if (raw.length != MeshControl.SIZE) {
            fail("size");
        }
        // LE: magic 0x434D => 4D 43
        if (raw[0] != 0x4D || raw[1] != 0x43) {
            fail("magic bytes");
        }
        if (raw[2] != 1 || raw[3] != MeshControl.OP_SET_SKIP) {
            fail("version/opcode");
        }
        if (raw[4] != 7 || raw[5] != 0) {
            fail("node_id");
        }
        if (raw[6] != 1) {
            fail("mask");
        }
        MeshControl round = MeshControl.decode(raw);
        if (round.opcode != MeshControl.OP_SET_SKIP || round.nodeId != 7 || round.mask != 1) {
            fail("roundtrip fields");
        }
        MeshControl clr = MeshControl.clear(7);
        byte[] craw = clr.encode();
        if (craw[3] != MeshControl.OP_CLEAR) {
            fail("clear opcode");
        }
        System.out.println("MeshControlCheck: OK");
    }

    private static void fail(String msg) {
        System.err.println("MeshControlCheck FAIL: " + msg);
        System.exit(1);
    }

    private MeshControlCheck() {}
}
