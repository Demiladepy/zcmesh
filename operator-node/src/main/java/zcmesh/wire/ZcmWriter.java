package zcmesh.wire;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.file.Path;

/**
 * Streaming .zcm writer (header rewritten on close with final frame count).
 * Layout mirrors shared/zcm_file.h.
 */
public final class ZcmWriter implements AutoCloseable {
    public static final int MAGIC = 0x314D435A;
    public static final int VERSION = 1;
    public static final int HEADER_SIZE = 16;

    private final RandomAccessFile raf;
    private final FileChannel channel;
    private long frameCount;
    private boolean closed;

    public ZcmWriter(Path path) throws IOException {
        this.raf = new RandomAccessFile(path.toFile(), "rw");
        this.channel = raf.getChannel();
        raf.setLength(0);
        ByteBuffer hdr = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        hdr.putInt(MAGIC);
        hdr.putShort((short) VERSION);
        hdr.putShort((short) 0);
        hdr.putLong(0);
        hdr.flip();
        channel.write(hdr);
        frameCount = 0;
    }

    public synchronized void write(WireFrame frame) throws IOException {
        if (closed) {
            throw new IOException("writer closed");
        }
        if (!frame.crcOk) {
            return;
        }
        ByteBuffer buf = ByteBuffer.allocate(WireFrame.SIZE).order(ByteOrder.LITTLE_ENDIAN);
        frame.encode(buf);
        buf.flip();
        while (buf.hasRemaining()) {
            channel.write(buf);
        }
        frameCount++;
    }

    public long frameCount() {
        return frameCount;
    }

    @Override
    public synchronized void close() throws IOException {
        if (closed) {
            return;
        }
        ByteBuffer hdr = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        hdr.putInt(MAGIC);
        hdr.putShort((short) VERSION);
        hdr.putShort((short) 0);
        hdr.putLong(frameCount);
        hdr.flip();
        channel.position(0);
        channel.write(hdr);
        channel.force(true);
        channel.close();
        raf.close();
        closed = true;
    }
}
