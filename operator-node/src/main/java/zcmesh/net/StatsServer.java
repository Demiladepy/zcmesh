package zcmesh.net;

import zcmesh.pipeline.MetricsSampler;
import zcmesh.pipeline.TelemetryPipeline;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Side-channel metrics: TCP connect → one plaintext snapshot → close.
 */
public final class StatsServer implements Runnable {
    private final int port;
    private final MetricsSampler sampler;
    private final AtomicBoolean running = new AtomicBoolean(true);

    public StatsServer(int port, TelemetryPipeline pipeline) {
        this.port = port;
        this.sampler = new MetricsSampler(pipeline);
    }

    public StatsServer(int port, MetricsSampler sampler) {
        this.port = port;
        this.sampler = sampler;
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
            System.err.println("StatsServer listening on " + port);

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
                        SocketChannel ch = server.accept();
                        if (ch == null) {
                            continue;
                        }
                        ch.configureBlocking(false);
                        ByteBuffer buf = ByteBuffer.wrap(
                                sampler.snapshot().toPlainText().getBytes(StandardCharsets.US_ASCII));
                        ch.register(selector, SelectionKey.OP_WRITE, buf);
                    } else if (key.isWritable()) {
                        SocketChannel ch = (SocketChannel) key.channel();
                        ByteBuffer buf = (ByteBuffer) key.attachment();
                        ch.write(buf);
                        if (!buf.hasRemaining()) {
                            ch.close();
                            key.cancel();
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
}
