#include "CANMonitor.hpp"
#include <QTableWidgetItem>

CANMonitor::CANMonitor(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background:#12121e; color:white;");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Toolbar ────────────────────────────────────
    auto* toolbar = new QHBoxLayout;
    auto* title = new QLabel("CAN Frame Monitor", this);
    title->setStyleSheet("font-weight:bold; font-size:13px;");
    toolbar->addWidget(title);
    toolbar->addStretch();

    pause_chk_ = new QCheckBox("Pause", this);
    pause_chk_->setStyleSheet("color:#aaa;");
    toolbar->addWidget(pause_chk_);

    auto_scroll_ = new QCheckBox("Auto-scroll", this);
    auto_scroll_->setChecked(true);
    auto_scroll_->setStyleSheet("color:#aaa;");
    toolbar->addWidget(auto_scroll_);

    clear_btn_ = new QPushButton("Clear", this);
    clear_btn_->setFixedSize(64, 26);
    clear_btn_->setStyleSheet(
        "QPushButton{background:#333;color:#aaa;border:none;border-radius:4px;}"
        "QPushButton:hover{background:#444;}");
    toolbar->addWidget(clear_btn_);
    root->addLayout(toolbar);

    // ── Table ──────────────────────────────────────
    setupTable();
    root->addWidget(table_);

    connect(clear_btn_, &QPushButton::clicked, this, &CANMonitor::clearLog);
}

void CANMonitor::setupTable() {
    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels({"Time", "ID", "Name", "Value", "Len", "Raw (hex)"});
    table_->setStyleSheet(
        "QTableWidget{background:#12121e;color:#ddd;gridline-color:#2a2a3e;"
        "selection-background-color:#2a2a4e;border:none;}"
        "QHeaderView::section{background:#1e1e30;color:#aaa;border:none;"
        "padding:4px;font-size:11px;border-bottom:1px solid #333;}"
        "QTableWidget::item{padding:3px 6px;font-size:11px;}");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setShowGrid(true);
    table_->setWordWrap(false);
}

void CANMonitor::addFrame(const DecodedFrame& frame) {
    if (pause_chk_->isChecked()) return;

    // Enforce row limit — remove oldest
    if (table_->rowCount() >= MAX_ROWS) {
        table_->removeRow(0);
    }

    int row = table_->rowCount();
    table_->insertRow(row);

    auto item = [&](const QString& text, Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft) {
        auto* it = new QTableWidgetItem(text);
        it->setTextAlignment(align);
        QColor bg = rowColor(frame);
        if (bg.isValid()) it->setBackground(bg);
        return it;
    };

    QString time_str = frame.timestamp.toString("hh:mm:ss.zzz");
    QString raw_hex  = frame.raw_data.toHex(' ').toUpper();

    table_->setItem(row, 0, item(time_str));
    table_->setItem(row, 1, item(frame.id_str, Qt::AlignCenter | Qt::AlignVCenter));
    table_->setItem(row, 2, item(frame.name));
    table_->setItem(row, 3, item(frame.value_str));
    table_->setItem(row, 4, item(QString::number(frame.len), Qt::AlignCenter | Qt::AlignVCenter));
    table_->setItem(row, 5, item(raw_hex));

    if (auto_scroll_->isChecked()) {
        table_->scrollToBottom();
    }
}

void CANMonitor::clearLog() {
    table_->setRowCount(0);
}

QColor CANMonitor::rowColor(const DecodedFrame& frame) {
    if (frame.is_fault)         return QColor(60, 20, 20);    // dark red for faults
    if (frame.id == CAN_ID_RPM) return QColor(16, 28, 42);    // subtle blue for RPM
    return QColor();                                           // default
}
