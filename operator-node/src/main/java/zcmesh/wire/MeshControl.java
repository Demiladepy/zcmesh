package zcmesh.wire;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Mirror of shared/mesh_control.h — 8-byte LE mesh control datagram.
 */
public final class MeshControl {
    public static final int MAGIC = 0x434D;
    public static final int VERSION = 1;
    public static final int SIZE = 8;

    public static final int OP_SET_SKIP = 1;
    public static final int OP_CLEAR = 2;

    public final int magic;
    public final int version;
    public final int opcode;
    public final int nodeId;
    public final int mask;

    public MeshControl(int opcode, int nodeId, int mask) {
        this.magic = MAGIC;
        this.version = VERSION;
        this.opcode = opcode;
        this.nodeId = nodeId & 0xFFFF;
        this.mask = mask & 0xFF;
    }

    public static MeshControl setSkip(int nodeId, int mask) {
        return new MeshControl(OP_SET_SKIP, nodeId, mask);
    }

    public static MeshControl clear(int nodeId) {
        return new MeshControl(OP_CLEAR, nodeId, 0);
    }

    public byte[] encode() {
        ByteBuffer b = ByteBuffer.allocate(SIZE).order(ByteOrder.LITTLE_ENDIAN);
        b.putShort(0, (short) magic);
        b.put(2, (byte) version);
        b.put(3, (byte) opcode);
        b.putShort(4, (short) nodeId);
        b.put(6, (byte) mask);
        b.put(7, (byte) 0);
        return b.array();
    }

    public static MeshControl decode(byte[] raw) {
        if (raw == null || raw.length < SIZE) {
            throw new IllegalArgumentException("need " + SIZE + " bytes");
        }
        ByteBuffer b = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
        int magic = Short.toUnsignedInt(b.getShort(0));
        int version = Byte.toUnsignedInt(b.get(2));
        int opcode = Byte.toUnsignedInt(b.get(3));
        int nodeId = Short.toUnsignedInt(b.getShort(4));
        int mask = Byte.toUnsignedInt(b.get(6));
        if (magic != MAGIC || version != VERSION) {
            throw new IllegalArgumentException("bad magic/version");
        }
        return new MeshControl(opcode, nodeId, mask);
    }
}
