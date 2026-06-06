#include "DTCViewer.hpp"
#include <QTableWidgetItem>
#include <QHeaderView>

DTCViewer::DTCViewer(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background:#12121e; color:white;");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Header ─────────────────────────────────────
    auto* header_row = new QHBoxLayout;
    auto* title = new QLabel("Diagnostic Trouble Codes", this);
    title->setStyleSheet("font-weight:bold; font-size:13px;");
    header_row->addWidget(title);
    header_row->addStretch();

    count_lbl_ = new QLabel("0 DTCs", this);
    count_lbl_->setStyleSheet("color:#888; font-size:12px;");
    header_row->addWidget(count_lbl_);

    clear_btn_ = new QPushButton("Clear DTCs", this);
    clear_btn_->setFixedSize(88, 26);
    clear_btn_->setStyleSheet(
        "QPushButton{background:#333;color:#aaa;border:none;border-radius:4px;}"
        "QPushButton:hover{background:#444;}");
    header_row->addWidget(clear_btn_);
    root->addLayout(header_row);

    // ── Table ──────────────────────────────────────
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({"Code", "Description", "Severity", "Count", "First seen"});
    table_->setStyleSheet(
        "QTableWidget{background:#12121e;color:#ddd;gridline-color:#2a2a3e;"
        "selection-background-color:#2a2a4e;border:none;}"
        "QHeaderView::section{background:#1e1e30;color:#aaa;border:none;"
        "padding:4px;font-size:11px;border-bottom:1px solid #333;}"
        "QTableWidget::item{padding:4px 8px;font-size:12px;}");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setShowGrid(true);
    root->addWidget(table_);

    connect(clear_btn_, &QPushButton::clicked, this, [this]{
        clearDTCs();
        emit clearRequested();
    });
}

void DTCViewer::addDTC(uint16_t code, uint8_t count) {
    QString code_str = QStringLiteral("P%1")
                           .arg(code, 4, 16, QLatin1Char('0')).toUpper();

    // Update existing row if already present
    if (code_to_row_.contains(code)) {
        int row = code_to_row_[code];
        table_->item(row, 3)->setText(QString::number(count));
        return;
    }

    // Insert new row
    int row = table_->rowCount();
    table_->insertRow(row);
    code_to_row_[code] = row;

    auto cell = [&](const QString& text, const QColor& fg = QColor("#ddd")) {
        auto* it = new QTableWidgetItem(text);
        it->setForeground(fg);
        it->setBackground(QColor(30, 10, 10));  // dark red tint
        return it;
    };

    QString sev = dtcSeverity(code);
    QColor  sev_color = (sev == "Critical") ? QColor("#ff5555")
                      : (sev == "Warning")  ? QColor("#ddaa22")
                                            : QColor("#aaaaaa");

    table_->setItem(row, 0, cell(code_str, QColor("#ff8888")));
    table_->setItem(row, 1, cell(dtcDescription(code)));
    table_->setItem(row, 2, cell(sev, sev_color));
    table_->setItem(row, 3, cell(QString::number(count)));
    table_->setItem(row, 4, cell(QDateTime::currentDateTime().toString("hh:mm:ss")));

    count_lbl_->setText(QStringLiteral("%1 DTC%2")
                            .arg(table_->rowCount())
                            .arg(table_->rowCount() == 1 ? "" : "s"));
}

void DTCViewer::clearDTCs() {
    table_->setRowCount(0);
    code_to_row_.clear();
    count_lbl_->setText("0 DTCs");
}

QString DTCViewer::dtcDescription(uint16_t code) {
    switch (code) {
    case 0x0217: return "Engine coolant over temperature condition";
    case 0x0197: return "Engine coolant temperature sensor low";
    case 0x0562: return "System voltage low";
    case 0x0100: return "Watchdog reset — task communication lost";
    default:     return QStringLiteral("Unknown DTC (code 0x%1)").arg(code, 4, 16, QLatin1Char('0'));
    }
}

QString DTCViewer::dtcSeverity(uint16_t code) {
    switch (code) {
    case 0x0217: return "Critical";
    case 0x0197: return "Warning";
    case 0x0562: return "Warning";
    case 0x0100: return "Critical";
    default:     return "Info";
    }
}
