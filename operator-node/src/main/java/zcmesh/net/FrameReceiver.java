package zcmesh.net;

import zcmesh.pipeline.TelemetryPipeline;
import zcmesh.wire.WireFrame;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicBoolean;

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
             ServerSocketChannel server = ServerSocketChannel.open()) {
            server.configureBlocking(false);
            server.bind(new InetSocketAddress(port));
            server.register(selector, SelectionKey.OP_ACCEPT);
            System.err.println("FrameReceiver listening on " + port);

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
                        read(key);
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
        ByteBuffer buf = ByteBuffer.allocateDirect(64 * 1024);
        client.register(selector, SelectionKey.OP_READ, buf);
        System.err.println("accepted " + client.getRemoteAddress());
    }

    private void read(SelectionKey key) throws IOException {
        SocketChannel ch = (SocketChannel) key.channel();
        ByteBuffer buf = (ByteBuffer) key.attachment();
        int n = ch.read(buf);
        if (n < 0) {
            ch.close();
            key.cancel();
            return;
        }
        buf.flip();
        while (buf.remaining() >= WireFrame.SIZE) {
            ByteBuffer slice = buf.slice();
            slice.limit(WireFrame.SIZE);
            WireFrame frame = WireFrame.decode(slice);
            pipeline.offer(frame);
            buf.position(buf.position() + WireFrame.SIZE);
        }
        buf.compact();
    }
}
