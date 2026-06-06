#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include "CANParser.hpp"

class CANMonitor : public QWidget {
    Q_OBJECT
public:
    explicit CANMonitor(QWidget* parent = nullptr);

public slots:
    void addFrame(const DecodedFrame& frame);
    void clearLog();

private:
    QTableWidget* table_       = nullptr;
    QCheckBox*    auto_scroll_ = nullptr;
    QPushButton*  clear_btn_   = nullptr;
    QCheckBox*    pause_chk_   = nullptr;

    static constexpr int MAX_ROWS = 500;

    void setupTable();
    static QColor rowColor(const DecodedFrame& frame);
};
