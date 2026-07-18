package zcmesh.net;

import zcmesh.wire.MeshControl;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;

/**
 * UDP sender for edge --control (SET_SKIP / CLEAR hop mask).
 */
public final class ControlClient implements AutoCloseable {
    private final InetSocketAddress target;
    private final DatagramSocket socket;

    public ControlClient(String host, int port) throws IOException {
        this.target = new InetSocketAddress(host, port);
        this.socket = new DatagramSocket();
    }

    public static ControlClient parse(String hostPort) throws IOException {
        int colon = hostPort.lastIndexOf(':');
        if (colon <= 0) {
            throw new IllegalArgumentException("need host:port");
        }
        String host = hostPort.substring(0, colon);
        int port = Integer.parseInt(hostPort.substring(colon + 1));
        return new ControlClient(host, port);
    }

    public void setSkip(int nodeId, int mask) throws IOException {
        send(MeshControl.setSkip(nodeId, mask));
    }

    public void clear(int nodeId) throws IOException {
        send(MeshControl.clear(nodeId));
    }

    public void send(MeshControl msg) throws IOException {
        byte[] raw = msg.encode();
        DatagramPacket pkt = new DatagramPacket(raw, raw.length, target);
        socket.send(pkt);
    }

    @Override
    public void close() {
        socket.close();
    }
}
