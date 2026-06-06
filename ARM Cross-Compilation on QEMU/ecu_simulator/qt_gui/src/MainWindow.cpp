#include "MainWindow.hpp"
#include <QSplitter>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ECU Simulator — QEMU RISC-V");
    resize(1280, 800);
    setStyleSheet("QMainWindow{background:#0d0d1a;} QTabWidget::pane{border:none;}");

    conn_mgr_  = new ConnectionManager(this);
    can_parser_= new CANParser(this);

    buildUI();
    buildToolbar();
    wireSignals();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUI() {
    // ── Central: left panel + tab panel ───────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet("QSplitter::handle{background:#2a2a3e; width:2px;}");
    setCentralWidget(splitter);

    // Left: dashboard + fault injector stacked
    auto* left_panel = new QWidget(this);
    auto* left_lay   = new QVBoxLayout(left_panel);
    left_lay->setContentsMargins(0, 0, 0, 0);
    left_lay->setSpacing(0);

    dashboard_ = new ECUDashboard(left_panel);
    injector_  = new FaultInjector(left_panel);
    left_lay->addWidget(dashboard_, 3);
    left_lay->addWidget(injector_,  2);
    splitter->addWidget(left_panel);

    // Right: tabbed CAN monitor + DTC viewer
    auto* tabs = new QTabWidget(this);
    tabs->setStyleSheet(
        "QTabBar::tab{background:#1a1a2e;color:#888;padding:8px 18px;"
        "border:none;border-bottom:2px solid transparent;}"
        "QTabBar::tab:selected{color:white;border-bottom:2px solid #3db464;}"
        "QTabBar::tab:hover{color:#ccc;}"
        "QTabWidget::pane{border:none;background:#12121e;}");

    can_monitor_ = new CANMonitor(this);
    dtc_viewer_  = new DTCViewer(this);
    tabs->addTab(can_monitor_, "CAN Monitor");
    tabs->addTab(dtc_viewer_,  "DTC Viewer");
    splitter->addWidget(tabs);

    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    // ── Status bar ────────────────────────────────
    ctrl_status_ = new QLabel("Control: ✗ Disconnected", statusBar());
    ctrl_status_->setStyleSheet("color:#888; padding: 0 8px;");
    can_status_  = new QLabel("CAN: ✗ Disconnected", statusBar());
    can_status_->setStyleSheet("color:#888; padding: 0 8px;");

    statusBar()->setStyleSheet("background:#0d0d1a; color:#888; border-top:1px solid #2a2a3e;");
    statusBar()->addPermanentWidget(ctrl_status_);
    statusBar()->addPermanentWidget(can_status_);
}

void MainWindow::buildToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setStyleSheet(
        "QToolBar{background:#1a1a2e;border-bottom:1px solid #2a2a3e;spacing:4px;}"
        "QToolButton{color:#aaa;background:none;border:none;padding:4px 10px;"
        "border-radius:4px;font-size:12px;}"
        "QToolButton:hover{background:#2a2a4e;color:white;}");

    auto* connect_act = new QAction("⚡  Connect to QEMU", this);
    auto* about_act   = new QAction("?  About", this);

    tb->addAction(connect_act);
    tb->addSeparator();
    tb->addAction(about_act);

    connect(connect_act, &QAction::triggered, this, &MainWindow::openConnectDialog);
    connect(about_act,   &QAction::triggered, this, [this]{
        QMessageBox::information(this, "About",
            "ECU Simulator\n\nFirmware: FreeRTOS on QEMU RISC-V (sifive_u)\n"
            "GUI: Qt 6 on Windows\n\nControl channel: TCP :5000\nCAN stream: Serial port (UART0 PTY)");
    });
}

void MainWindow::wireSignals() {
    // CAN frames: connection → parser → widgets
    connect(conn_mgr_,  &ConnectionManager::canFrameReceived,
            can_parser_, &CANParser::parseFrame);

    connect(can_parser_, &CANParser::rpmUpdated,
            dashboard_,  &ECUDashboard::setRPM);
    connect(can_parser_, &CANParser::throttleUpdated,
            dashboard_,  &ECUDashboard::setThrottle);
    connect(can_parser_, &CANParser::coolantTempUpdated,
            dashboard_,  &ECUDashboard::setCoolantTemp);
    connect(can_parser_, &CANParser::fuelLevelUpdated,
            dashboard_,  &ECUDashboard::setFuelLevel);
    connect(can_parser_, &CANParser::batteryVoltageUpdated,
            dashboard_,  &ECUDashboard::setBatteryVoltage);
    connect(can_parser_, &CANParser::faultMaskUpdated,
            dashboard_,  &ECUDashboard::setFaultMask);
    connect(can_parser_, &CANParser::engineStateUpdated,
            dashboard_,  &ECUDashboard::setEngineState);
    connect(can_parser_, &CANParser::frameDecoded,
            can_monitor_,&CANMonitor::addFrame);
    connect(can_parser_, &CANParser::dtcReceived,
            dtc_viewer_, &DTCViewer::addDTC);

    // Fault injector → connection manager
    connect(injector_, &FaultInjector::throttleChanged,
            conn_mgr_, &ConnectionManager::sendThrottle);
    connect(injector_, &FaultInjector::injectFault,
            conn_mgr_, &ConnectionManager::sendFaultInject);
    connect(injector_, &FaultInjector::clearFaultsRequested,
            conn_mgr_, &ConnectionManager::sendClearFaults);
    connect(dtc_viewer_, &DTCViewer::clearRequested,
            conn_mgr_,   &ConnectionManager::sendClearFaults);

    // Connection status
    connect(conn_mgr_, &ConnectionManager::controlConnected,    this, &MainWindow::onControlConnected);
    connect(conn_mgr_, &ConnectionManager::controlDisconnected, this, &MainWindow::onControlDisconnected);
    connect(conn_mgr_, &ConnectionManager::canConnected,        this, &MainWindow::onCANConnected);
    connect(conn_mgr_, &ConnectionManager::canDisconnected,     this, &MainWindow::onCANDisconnected);
    connect(conn_mgr_, &ConnectionManager::statusMessage,       this, &MainWindow::onStatusMessage);
}

// ── Connection dialog ─────────────────────────────────

void MainWindow::openConnectDialog() {
    QSettings s("ECUSim", "QEMU");
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Connect to QEMU");
    dlg->setFixedWidth(380);
    dlg->setStyleSheet(
        "QDialog{background:#1a1a2e;color:white;}"
        "QLabel{color:#aaa;}"
        "QLineEdit,QSpinBox,QComboBox{background:#0d0d1a;color:white;"
        "border:1px solid #333;border-radius:4px;padding:4px;}"
        "QPushButton{background:#2a4a3a;color:#3db464;border:none;"
        "border-radius:4px;padding:6px 16px;}"
        "QPushButton:hover{background:#3a5a4a;}");

    auto* form = new QFormLayout(dlg);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);

    auto* tcp_host = new QLineEdit(s.value("tcp_host", "localhost").toString(), dlg);
    auto* tcp_port = new QSpinBox(dlg);
    tcp_port->setRange(1, 65535);
    tcp_port->setValue(s.value("tcp_port", 5001).toInt());

    auto* serial_port = new QLineEdit(s.value("serial_port", "COM3").toString(), dlg);
    // On WSL, QEMU exposes a PTY; socat or npiperelay maps it to a COM port on Windows.

    form->addRow("Control host:",  tcp_host);
    form->addRow("Control port:",  tcp_port);
    form->addRow("CAN serial port:", serial_port);

    auto* note = new QLabel(
        "💡 Start QEMU via scripts/launch_qemu.sh first.\n"
        "Serial port: use COM3 or the PTY path socat creates.", dlg);
    note->setStyleSheet("color:#666; font-size:11px;");
    note->setWordWrap(true);
    form->addRow(note);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() == QDialog::Accepted) {
        s.setValue("tcp_host",    tcp_host->text());
        s.setValue("tcp_port",    tcp_port->value());
        s.setValue("serial_port", serial_port->text());

        conn_mgr_->connectControl(tcp_host->text(),
                                   static_cast<quint16>(tcp_port->value()));
        conn_mgr_->connectCAN(serial_port->text());
    }
    dlg->deleteLater();
}

void MainWindow::onControlConnected() {
    ctrl_status_->setText("Control: ✔ Connected");
    ctrl_status_->setStyleSheet("color:#3db464; padding:0 8px;");
}

void MainWindow::onControlDisconnected() {
    ctrl_status_->setText("Control: ✗ Disconnected");
    ctrl_status_->setStyleSheet("color:#888; padding:0 8px;");
}

void MainWindow::onCANConnected() {
    can_status_->setText("CAN: ✔ Connected");
    can_status_->setStyleSheet("color:#3db464; padding:0 8px;");
}

void MainWindow::onCANDisconnected() {
    can_status_->setText("CAN: ✗ Disconnected");
    can_status_->setStyleSheet("color:#888; padding:0 8px;");
}

void MainWindow::onStatusMessage(const QString& msg) {
    statusBar()->showMessage(msg, 4000);
}
