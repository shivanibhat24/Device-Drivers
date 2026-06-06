#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QMap>

class DTCViewer : public QWidget {
    Q_OBJECT
public:
    explicit DTCViewer(QWidget* parent = nullptr);

public slots:
    void addDTC(uint16_t code, uint8_t count);
    void clearDTCs();

signals:
    void clearRequested();

private:
    QTableWidget* table_     = nullptr;
    QLabel*       count_lbl_ = nullptr;
    QPushButton*  clear_btn_ = nullptr;

    // Track codes already in table to update count in place
    QMap<uint16_t, int> code_to_row_;

    static QString dtcDescription(uint16_t code);
    static QString dtcSeverity(uint16_t code);
};
