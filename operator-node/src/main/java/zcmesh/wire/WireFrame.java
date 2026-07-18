package zcmesh.wire;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.zip.CRC32;

/**
 * Mirror of shared/wire_frame.h — 24-byte little-endian frame.
 */
public final class WireFrame {
    public static final int MAGIC = 0x5A43;
    public static final int VERSION = 1;
    public static final int SIZE = 24;
    public static final int CRC_PAYLOAD_LEN = 20;

    public static final int FLAG_NIBBLE_PACK = 0x01;
    public static final int FLAG_LAST_HOP = 0x02;

    public static final int OFF_MAGIC = 0;
    public static final int OFF_VERSION = 2;
    public static final int OFF_FLAGS = 3;
    public static final int OFF_SEQ = 4;
    public static final int OFF_TIMESTAMP_LO = 8;
    public static final int OFF_NODE_ID = 12;
    public static final int OFF_SENSOR_TYPE = 14;
    public static final int OFF_RESERVED = 15;
    public static final int OFF_RAW_VALUE = 16;
    public static final int OFF_CHECKSUM = 20;

    public final int magic;
    public final int version;
    public final int flags;
    public final long seq;
    public final long timestampLo;
    public final int nodeId;
    public final int sensorType;
    public final int reserved;
    public final int rawValue;
    public final long checksum;
    public final boolean crcOk;

    private WireFrame(
            int magic, int version, int flags, long seq, long timestampLo,
            int nodeId, int sensorType, int reserved, int rawValue, long checksum, boolean crcOk) {
        this.magic = magic;
        this.version = version;
        this.flags = flags;
        this.seq = seq;
        this.timestampLo = timestampLo;
        this.nodeId = nodeId;
        this.sensorType = sensorType;
        this.reserved = reserved;
        this.rawValue = rawValue;
        this.checksum = checksum;
        this.crcOk = crcOk;
    }

    public static WireFrame decode(ByteBuffer buf) {
        if (buf.remaining() < SIZE) {
            throw new IllegalArgumentException("need " + SIZE + " bytes");
        }
        ByteBuffer b = buf.slice().order(ByteOrder.LITTLE_ENDIAN);
        int magic = Short.toUnsignedInt(b.getShort(OFF_MAGIC));
        int version = Byte.toUnsignedInt(b.get(OFF_VERSION));
        int flags = Byte.toUnsignedInt(b.get(OFF_FLAGS));
        long seq = Integer.toUnsignedLong(b.getInt(OFF_SEQ));
        long timestampLo = Integer.toUnsignedLong(b.getInt(OFF_TIMESTAMP_LO));
        int nodeId = Short.toUnsignedInt(b.getShort(OFF_NODE_ID));
        int sensorType = Byte.toUnsignedInt(b.get(OFF_SENSOR_TYPE));
        int reserved = Byte.toUnsignedInt(b.get(OFF_RESERVED));
        int rawValue = b.getInt(OFF_RAW_VALUE);
        long checksum = Integer.toUnsignedLong(b.getInt(OFF_CHECKSUM));

        byte[] payload = new byte[CRC_PAYLOAD_LEN];
        b.position(0);
        b.get(payload, 0, CRC_PAYLOAD_LEN);
        long expect = crc32Ieee(payload);
        boolean ok = magic == MAGIC && version == VERSION && expect == checksum;
        return new WireFrame(magic, version, flags, seq, timestampLo, nodeId, sensorType,
                reserved, rawValue, checksum, ok);
    }

    /** Encode this frame into buf (must have SIZE bytes remaining). Absolute writes. */
    public void encode(ByteBuffer buf) {
        ByteBuffer b = buf.slice().order(ByteOrder.LITTLE_ENDIAN);
        b.putShort(OFF_MAGIC, (short) magic);
        b.put(OFF_VERSION, (byte) version);
        b.put(OFF_FLAGS, (byte) flags);
        b.putInt(OFF_SEQ, (int) seq);
        b.putInt(OFF_TIMESTAMP_LO, (int) timestampLo);
        b.putShort(OFF_NODE_ID, (short) nodeId);
        b.put(OFF_SENSOR_TYPE, (byte) sensorType);
        b.put(OFF_RESERVED, (byte) reserved);
        b.putInt(OFF_RAW_VALUE, rawValue);
        b.putInt(OFF_CHECKSUM, (int) checksum);
        buf.position(buf.position() + SIZE);
    }

    /** IEEE CRC-32 matching the C++ table implementation (poly 0xEDB88320). */
    public static long crc32Ieee(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data);
        return crc.getValue();
    }

    private WireFrame() {
        throw new AssertionError();
    }
}
