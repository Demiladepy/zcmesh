package zcmesh.net;

import zcmesh.pipeline.TelemetryPipeline;
import zcmesh.wire.WireFrame;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.StandardProtocolFamily;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.DatagramChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Dual-path receiver: non-blocking TCP stream + UDP datagrams on the same port.
 * TCP path scans for wire magic after desync (partial-send / mid-frame reconnect).
 */
public final class FrameReceiver implements Runnable {
    private final int port;
    private final TelemetryPipeline pipeline;
    private final AtomicBoolean running = new AtomicBoolean(true);

    public FrameReceiver(int port, TelemetryPipeline pipeline) {
        this.port = port;
        this.pipeline = pipeline;
    }

    public void stop() {
        running.set(false);
    }

    @Override
    public void run() {
        try (Selector selector = Selector.open();
             ServerSocketChannel server = ServerSocketChannel.open();
             DatagramChannel udp = DatagramChannel.open(StandardProtocolFamily.INET)) {

            server.configureBlocking(false);
            server.bind(new InetSocketAddress(port));
            server.register(selector, SelectionKey.OP_ACCEPT);

            udp.configureBlocking(false);
            udp.setOption(StandardSocketOptions.SO_REUSEADDR, true);
            udp.bind(new InetSocketAddress(port));
            ByteBuffer udpBuf = ByteBuffer.allocateDirect(64 * 1024);
            udp.register(selector, SelectionKey.OP_READ, udpBuf);

            System.err.println("FrameReceiver TCP+UDP listening on " + port);

            while (running.get()) {
                selector.select(250);
                Iterator<SelectionKey> it = selector.selectedKeys().iterator();
                while (it.hasNext()) {
                    SelectionKey key = it.next();
                    it.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isAcceptable()) {
                        accept(server, selector);
                    } else if (key.isReadable()) {
                        if (key.channel() instanceof DatagramChannel) {
                            readUdp(key);
                        } else {
                            readTcp(key);
                        }
                    }
                }
            }
        } catch (IOException e) {
            if (running.get()) {
                e.printStackTrace(System.err);
            }
        }
    }

    private void accept(ServerSocketChannel server, Selector selector) throws IOException {
        SocketChannel client = server.accept();
        if (client == null) {
            return;
        }
        client.configureBlocking(false);
        ByteBuffer buf = ByteBuffer.allocateDirect(256 * 1024);
        client.register(selector, SelectionKey.OP_READ, buf);
        System.err.println("accepted tcp " + client.getRemoteAddress());
    }

    private void readTcp(SelectionKey key) throws IOException {
        SocketChannel ch = (SocketChannel) key.channel();
        ByteBuffer buf = (ByteBuffer) key.attachment();
        int n = ch.read(buf);
        if (n < 0) {
            ch.close();
            key.cancel();
            return;
        }
        decodeStream(buf);
    }

    private void readUdp(SelectionKey key) throws IOException {
        DatagramChannel ch = (DatagramChannel) key.channel();
        ByteBuffer buf = (ByteBuffer) key.attachment();
        buf.clear();
        if (ch.receive(buf) == null) {
            return;
        }
        buf.flip();
        while (buf.remaining() >= WireFrame.SIZE) {
            ByteBuffer slice = buf.slice();
            slice.limit(WireFrame.SIZE);
            pipeline.offer(WireFrame.decode(slice));
            buf.position(buf.position() + WireFrame.SIZE);
        }
    }

    private void decodeStream(ByteBuffer buf) {
        buf.flip();
        while (buf.remaining() >= WireFrame.SIZE) {
            int pos = buf.position();
            ByteBuffer peek = buf.duplicate().order(ByteOrder.LITTLE_ENDIAN);
            int magic = Short.toUnsignedInt(peek.getShort(pos));
            if (magic != WireFrame.MAGIC) {
                int found = findMagic(buf, pos);
                if (found < 0) {
                    /* Keep at most SIZE-1 bytes for a straddling magic. */
                    int keep = Math.min(buf.remaining(), WireFrame.SIZE - 1);
                    buf.position(buf.limit() - keep);
                    break;
                }
                pipeline.noteTcpResync(found - pos);
                buf.position(found);
                continue;
            }
            ByteBuffer slice = buf.slice();
            slice.limit(WireFrame.SIZE);
            WireFrame frame = WireFrame.decode(slice);
            if (!frame.crcOk) {
                /* Aligned magic but bad CRC: advance one byte and resync. */
                pipeline.noteCrcFail();
                pipeline.noteTcpResync(1);
                buf.position(pos + 1);
                continue;
            }
            pipeline.offer(frame);
            buf.position(pos + WireFrame.SIZE);
        }
        buf.compact();
    }

    /**
     * Scan for little-endian wire magic 0x5A43 starting at from (inclusive).
     * @return absolute buffer index of magic, or -1 if not found with room for a frame
     */
    static int findMagic(ByteBuffer buf, int from) {
        final byte lo = (byte) (WireFrame.MAGIC & 0xFF);
        final byte hi = (byte) ((WireFrame.MAGIC >> 8) & 0xFF);
        final int last = buf.limit() - WireFrame.SIZE;
        for (int i = from; i <= last; ++i) {
            if (buf.get(i) == lo && buf.get(i + 1) == hi) {
                return i;
            }
        }
        return -1;
    }
}
