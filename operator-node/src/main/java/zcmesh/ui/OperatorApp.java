package zcmesh.ui;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.beans.property.IntegerProperty;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.beans.property.SimpleLongProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.scene.control.TextField;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import zcmesh.operator.OperatorRuntime;
import zcmesh.pipeline.NodeState;
import zcmesh.pipeline.OperatorSnapshot;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * Thin structural shell over OperatorRuntime — live counters + optional mesh control.
 */
public final class OperatorApp extends Application {
    private static final int DEFAULT_PORT = 9900;

    private OperatorRuntime runtime;
    private ScheduledExecutorService ticker;
    private Label rateLabel;
    private Label statsLabel;
    private Label controlStatus;
    private final ObservableList<NodeRow> rows = FXCollections.observableArrayList();
    private final Map<Integer, NodeRow> byNode = new HashMap<>();

    @Override
    public void start(Stage stage) throws Exception {
        int port = DEFAULT_PORT;
        Path recordPath = null;
        String controlEp = null;
        List<String> raw = getParameters().getRaw();
        for (String arg : raw) {
            if (arg.startsWith("record=")) {
                recordPath = Path.of(arg.substring("record=".length()));
            } else if (arg.startsWith("control=")) {
                controlEp = arg.substring("control=".length());
            } else if (!arg.isEmpty()) {
                port = Integer.parseInt(arg);
            }
        }

        runtime = new OperatorRuntime(port, 8192, recordPath, true, controlEp);
        runtime.start();

        rateLabel = new Label("frames/s: 0   bytes/s: 0");
        statsLabel = new Label("ok: 0   crc: 0   gaps: 0   hops: -   last_seq: -");
        controlStatus = new Label(controlEp != null ? "control: " + controlEp : "control: off");

        TableView<NodeRow> table = new TableView<>(rows);
        table.setColumnResizePolicy(TableView.CONSTRAINED_RESIZE_POLICY_FLEX_LAST_COLUMN);
        TableColumn<NodeRow, Number> cNode = new TableColumn<>("node");
        cNode.setCellValueFactory(c -> c.getValue().nodeIdProperty());
        TableColumn<NodeRow, Number> cSeq = new TableColumn<>("seq");
        cSeq.setCellValueFactory(c -> c.getValue().seqProperty());
        TableColumn<NodeRow, Number> cType = new TableColumn<>("sensor");
        cType.setCellValueFactory(c -> c.getValue().sensorTypeProperty());
        TableColumn<NodeRow, Number> cVal = new TableColumn<>("raw");
        cVal.setCellValueFactory(c -> c.getValue().rawValueProperty());
        TableColumn<NodeRow, Number> cHop = new TableColumn<>("hop");
        cHop.setCellValueFactory(c -> c.getValue().hopIdxProperty());
        TableColumn<NodeRow, Number> cLast = new TableColumn<>("last");
        cLast.setCellValueFactory(c -> c.getValue().lastHopProperty());
        TableColumn<NodeRow, Number> cTs = new TableColumn<>("ts_lo");
        cTs.setCellValueFactory(c -> c.getValue().timestampLoProperty());
        table.getColumns().add(cNode);
        table.getColumns().add(cSeq);
        table.getColumns().add(cType);
        table.getColumns().add(cVal);
        table.getColumns().add(cHop);
        table.getColumns().add(cLast);
        table.getColumns().add(cTs);

        VBox top = new VBox(6, rateLabel, statsLabel, controlStatus);
        top.setPadding(new Insets(10));
        if (runtime.control() != null) {
            top.getChildren().add(buildControlBar());
        }

        BorderPane root = new BorderPane();
        root.setTop(top);
        root.setCenter(table);

        stage.setTitle("ZCMesh Operator");
        stage.setScene(new Scene(root, 780, 460));
        stage.show();

        ticker = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "ui-tick");
            t.setDaemon(true);
            return t;
        });
        ticker.scheduleAtFixedRate(this::tick, 0, 50, TimeUnit.MILLISECONDS);
        ticker.scheduleAtFixedRate(this::refreshRates, 1, 1, TimeUnit.SECONDS);
        stage.setOnCloseRequest(e -> shutdown());
    }

    private HBox buildControlBar() {
        TextField nodeField = new TextField("1");
        nodeField.setPrefColumnCount(4);
        TextField maskField = new TextField("1");
        maskField.setPrefColumnCount(4);
        Button skip = new Button("SET_SKIP");
        Button clear = new Button("CLEAR");
        skip.setOnAction(e -> {
            try {
                int node = Integer.parseInt(nodeField.getText().trim());
                int mask = Integer.parseInt(maskField.getText().trim());
                runtime.setHopSkip(node, mask);
                controlStatus.setText("sent SET_SKIP node=" + node + " mask=" + mask);
            } catch (Exception ex) {
                controlStatus.setText("control error: " + ex.getMessage());
            }
        });
        clear.setOnAction(e -> {
            try {
                int node = Integer.parseInt(nodeField.getText().trim());
                runtime.clearHopSkip(node);
                controlStatus.setText("sent CLEAR node=" + node);
            } catch (Exception ex) {
                controlStatus.setText("control error: " + ex.getMessage());
            }
        });
        HBox bar = new HBox(8, new Label("node"), nodeField, new Label("mask"), maskField, skip, clear);
        bar.setPadding(new Insets(0, 0, 4, 0));
        return bar;
    }

    private void tick() {
        try {
            runtime.drainToRecorder(512);
            List<NodeState> nodes = runtime.pipeline().latestNodesSnapshot();
            Platform.runLater(() -> {
                for (NodeState n : nodes) {
                    upsert(n);
                }
            });
        } catch (Exception e) {
            e.printStackTrace(System.err);
        }
    }

    private void upsert(NodeState n) {
        NodeRow row = byNode.get(n.nodeId);
        if (row == null) {
            row = new NodeRow(n.nodeId, n.seq, n.sensorType, n.rawValue, n.timestampLo, n.hopIdx, n.lastHop);
            byNode.put(n.nodeId, row);
            rows.add(row);
        } else {
            row.setSeq(n.seq);
            row.setSensorType(n.sensorType);
            row.setRawValue(n.rawValue);
            row.setTimestampLo(n.timestampLo);
            row.setHopIdx(n.hopIdx);
            row.setLastHop(n.lastHop ? 1 : 0);
        }
    }

    private void refreshRates() {
        OperatorSnapshot s = runtime.sampler().snapshot();
        Platform.runLater(() -> {
            rateLabel.setText(String.format("frames/s: %.0f   bytes/s: %.0f", s.framesPerSec, s.bytesPerSec));
            statsLabel.setText(String.format(
                    "ok: %d  crc: %d  gaps: %d  dups: %d  drops: %d  V/I/T: %d/%d/%d  "
                            + "hop0/1/2+: %d/%d/%d  last_hop: %.0f%%  seq: %d  q: %d",
                    s.framesOk, s.crcFail, s.gaps, s.dups, s.ringDrops,
                    s.sensorVoltage, s.sensorCurrent, s.sensorTemp,
                    s.hopIdx0, s.hopIdx1, s.hopIdx2Plus, s.lastHopPct(),
                    s.lastSeq, s.queued));
        });
    }

    private void shutdown() {
        if (ticker != null) {
            ticker.shutdownNow();
        }
        if (runtime != null) {
            try {
                runtime.close();
            } catch (Exception ex) {
                ex.printStackTrace(System.err);
            }
            runtime = null;
        }
    }

    @Override
    public void stop() {
        shutdown();
    }

    public static void main(String[] args) {
        launch(args);
    }

    public static final class NodeRow {
        private final IntegerProperty nodeId = new SimpleIntegerProperty();
        private final LongProperty seq = new SimpleLongProperty();
        private final IntegerProperty sensorType = new SimpleIntegerProperty();
        private final IntegerProperty rawValue = new SimpleIntegerProperty();
        private final LongProperty timestampLo = new SimpleLongProperty();
        private final IntegerProperty hopIdx = new SimpleIntegerProperty();
        private final IntegerProperty lastHop = new SimpleIntegerProperty();

        public NodeRow(int nodeId, long seq, int sensorType, int rawValue, long timestampLo,
                       int hopIdx, boolean lastHop) {
            this.nodeId.set(nodeId);
            this.seq.set(seq);
            this.sensorType.set(sensorType);
            this.rawValue.set(rawValue);
            this.timestampLo.set(timestampLo);
            this.hopIdx.set(hopIdx);
            this.lastHop.set(lastHop ? 1 : 0);
        }

        public IntegerProperty nodeIdProperty() { return nodeId; }
        public LongProperty seqProperty() { return seq; }
        public IntegerProperty sensorTypeProperty() { return sensorType; }
        public IntegerProperty rawValueProperty() { return rawValue; }
        public LongProperty timestampLoProperty() { return timestampLo; }
        public IntegerProperty hopIdxProperty() { return hopIdx; }
        public IntegerProperty lastHopProperty() { return lastHop; }

        public void setSeq(long v) { seq.set(v); }
        public void setSensorType(int v) { sensorType.set(v); }
        public void setRawValue(int v) { rawValue.set(v); }
        public void setTimestampLo(long v) { timestampLo.set(v); }
        public void setHopIdx(int v) { hopIdx.set(v); }
        public void setLastHop(int v) { lastHop.set(v); }
    }
}
