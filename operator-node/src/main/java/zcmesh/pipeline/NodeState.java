package zcmesh.pipeline;

/**
 * Latest frame fields per node — UI table source that does not consume the ring.
 */
public final class NodeState {
    public final int nodeId;
    public final long seq;
    public final int sensorType;
    public final int rawValue;
    public final long timestampLo;
    public final int hopIdx;
    public final boolean lastHop;
    public final long updatedAtNs;

    public NodeState(int nodeId, long seq, int sensorType, int rawValue, long timestampLo, long updatedAtNs) {
        this(nodeId, seq, sensorType, rawValue, timestampLo, 0, false, updatedAtNs);
    }

    public NodeState(
            int nodeId, long seq, int sensorType, int rawValue, long timestampLo,
            int hopIdx, boolean lastHop, long updatedAtNs) {
        this.nodeId = nodeId;
        this.seq = seq;
        this.sensorType = sensorType;
        this.rawValue = rawValue;
        this.timestampLo = timestampLo;
        this.hopIdx = hopIdx;
        this.lastHop = lastHop;
        this.updatedAtNs = updatedAtNs;
    }
}
