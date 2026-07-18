package zcmesh.ui;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.Label;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.beans.property.IntegerProperty;
import javafx.beans.property.LongProperty;
import javafx.beans.property.SimpleIntegerProperty;
import javafx.beans.property.SimpleLongProperty;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import zcmesh.net.FrameReceiver;
import zcmesh.net.StatsServer;
import zcmesh.pipeline.TelemetryPipeline;
import zcmesh.wire.WireFrame;
import zcmesh.wire.ZcmWriter;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public final class OperatorApp extends Application {
    private static final int DEFAULT_PORT = 9900;

    private TelemetryPipeline pipeline;
    private FrameReceiver receiver;
    private StatsServer statsServer;
    private Thread receiverThread;
    private Thread statsThread;
    private ScheduledExecutorService ticker;
    private long prevOk;
    private long prevBytes;
    private long prevTs;

    private Label rateLabel;
    private Label statsLabel;
    private ZcmWriter recorder;
    private final ObservableList<NodeRow> rows = FXCollections.observableArrayList();
    private final Map<Integer, NodeRow> byNode = new HashMap<>();

    @Override
    public void start(Stage stage) throws Exception {
        int port = DEFAULT_PORT;
        Path recordPath = null;
        List<String> raw = getParameters().getRaw();
        for (String arg : raw) {
            if (arg.startsWith("record=")) {
                recordPath = Path.of(arg.substring("record=".length()));
            } else {
                port = Integer.parseInt(arg);
            }
        }

        if (recordPath != null) {
            recorder = new ZcmWriter(recordPath);
            System.err.println("recording to " + recordPath);
        }

        pipeline = new TelemetryPipeline(8192);
        receiver = new FrameReceiver(port, pipeline);
        statsServer = new StatsServer(port + 9, pipeline);
        receiverThread = new Thread(receiver, "frame-receiver");
        statsThread = new Thread(statsServer, "stats-server");
        receiverThread.setDaemon(true);
        statsThread.setDaemon(true);
        receiverThread.start();
        statsThread.start();
        System.err.println("stats side-channel on " + (port + 9));

        rateLabel = new Label("frames/s: 0   bytes/s: 0");
        statsLabel = new Label("ok: 0   crc_fail: 0   gaps: 0   last_seq: -   queued: 0");

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
        TableColumn<NodeRow, Number> cTs = new TableColumn<>("ts_lo");
        cTs.setCellValueFactory(c -> c.getValue().timestampLoProperty());
        table.getColumns().add(cNode);
        table.getColumns().add(cSeq);
        table.getColumns().add(cType);
        table.getColumns().add(cVal);
        table.getColumns().add(cTs);

        VBox top = new VBox(6, rateLabel, statsLabel);
        top.setPadding(new Insets(10));

        BorderPane root = new BorderPane();
        root.setTop(top);
        root.setCenter(table);

        stage.setTitle("ZCMesh Operator");
        stage.setScene(new Scene(root, 720, 420));
        stage.show();

        prevTs = System.nanoTime();
        ticker = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "pipeline-tick");
            t.setDaemon(true);
            return t;
        });
        ticker.scheduleAtFixedRate(this::tick, 0, 50, TimeUnit.MILLISECONDS);
        ticker.scheduleAtFixedRate(this::refreshRates, 1, 1, TimeUnit.SECONDS);

        stage.setOnCloseRequest(e -> shutdown());
    }

    private void tick() {
        try {
            WireFrame f;
            int batch = 0;
            while (batch < 256 && (f = pipeline.poll(0)) != null) {
                final WireFrame frame = f;
                if (recorder != null) {
                    try {
                        recorder.write(frame);
                    } catch (Exception ex) {
                        ex.printStackTrace(System.err);
                    }
                }
                Platform.runLater(() -> upsert(frame));
                batch++;
            }
            Platform.runLater(() -> statsLabel.setText(String.format(
                    "ok: %d   crc_fail: %d   gaps: %d   drops: %d   last_seq: %d   queued: %d   ia_ewma_us: %.0f",
                    pipeline.framesOk(), pipeline.framesCrcFail(), pipeline.seqGaps(),
                    pipeline.ringDrops(), pipeline.lastSeq(), pipeline.queued(),
                    pipeline.interArrivalEwmaNs() / 1000.0)));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private void upsert(WireFrame frame) {
        NodeRow row = byNode.get(frame.nodeId);
        if (row == null) {
            row = new NodeRow(frame.nodeId, frame.seq, frame.sensorType, frame.rawValue, frame.timestampLo);
            byNode.put(frame.nodeId, row);
            rows.add(row);
        } else {
            row.setSeq(frame.seq);
            row.setSensorType(frame.sensorType);
            row.setRawValue(frame.rawValue);
            row.setTimestampLo(frame.timestampLo);
        }
    }

    private void refreshRates() {
        long now = System.nanoTime();
        long ok = pipeline.framesOk();
        long bytes = pipeline.bytesIn();
        double dt = (now - prevTs) / 1_000_000_000.0;
        if (dt <= 0) {
            return;
        }
        double fps = (ok - prevOk) / dt;
        double bps = (bytes - prevBytes) / dt;
        prevOk = ok;
        prevBytes = bytes;
        prevTs = now;
        Platform.runLater(() -> rateLabel.setText(String.format(
                "frames/s: %.0f   bytes/s: %.0f", fps, bps)));
    }

    private void shutdown() {
        if (receiver != null) {
            receiver.stop();
        }
        if (statsServer != null) {
            statsServer.stop();
        }
        if (ticker != null) {
            ticker.shutdownNow();
        }
        if (receiverThread != null) {
            receiverThread.interrupt();
        }
        if (statsThread != null) {
            statsThread.interrupt();
        }
        if (recorder != null) {
            try {
                System.err.println("closing record frames=" + recorder.frameCount());
                recorder.close();
            } catch (Exception ex) {
                ex.printStackTrace(System.err);
            }
            recorder = null;
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

        public NodeRow(int nodeId, long seq, int sensorType, int rawValue, long timestampLo) {
            this.nodeId.set(nodeId);
            this.seq.set(seq);
            this.sensorType.set(sensorType);
            this.rawValue.set(rawValue);
            this.timestampLo.set(timestampLo);
        }

        public IntegerProperty nodeIdProperty() { return nodeId; }
        public LongProperty seqProperty() { return seq; }
        public IntegerProperty sensorTypeProperty() { return sensorType; }
        public IntegerProperty rawValueProperty() { return rawValue; }
        public LongProperty timestampLoProperty() { return timestampLo; }

        public void setSeq(long v) { seq.set(v); }
        public void setSensorType(int v) { sensorType.set(v); }
        public void setRawValue(int v) { rawValue.set(v); }
        public void setTimestampLo(long v) { timestampLo.set(v); }
    }
}
