#pragma once
#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ecu_protocol.hpp"

class FaultInjector : public QWidget {
    Q_OBJECT
public:
    explicit FaultInjector(QWidget* parent = nullptr);

signals:
    void throttleChanged(int pct);
    void injectFault(ControlCmd cmd);
    void clearFaultsRequested();

private slots:
    void onThrottleSlider(int value);
    void onOverheatBtn();
    void onSensorDiscBtn();
    void onVoltDropBtn();
    void onClearBtn();

private:
    QSlider*     throttle_slider_ = nullptr;
    QLabel*      throttle_label_  = nullptr;
    QPushButton* overheat_btn_    = nullptr;
    QPushButton* sensor_disc_btn_ = nullptr;
    QPushButton* volt_drop_btn_   = nullptr;
    QPushButton* clear_btn_       = nullptr;

    static QPushButton* makeFaultBtn(const QString& label,
                                      const QString& color,
                                      QWidget* parent);
};
