#include "ECUDashboard.hpp"
#include <QGroupBox>

ECUDashboard::ECUDashboard(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background-color: #1a1a2e; color: white;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // ── Top: engine state + fault badge ──────────
    auto* status_row = new QHBoxLayout;
    engine_state_ = new QLabel("● ENGINE OFF", this);
    engine_state_->setStyleSheet("font-size:14px; font-weight:bold; color:#888;");
    status_row->addWidget(engine_state_);
    status_row->addStretch();

    fault_badge_ = new QLabel("  NO FAULTS  ", this);
    fault_badge_->setStyleSheet(
        "background:#2a4a2a; color:#3db464; border-radius:6px;"
        "padding:4px 10px; font-size:12px; font-weight:bold;");
    status_row->addWidget(fault_badge_);
    root->addLayout(status_row);

    // ── Arc gauges row ────────────────────────────
    auto* gauges_row = new QHBoxLayout;
    gauges_row->setSpacing(16);

    rpm_gauge_ = new ArcGauge("RPM", 0, 8000, "rpm", this);
    rpm_gauge_->setWarningThreshold(6000);
    rpm_gauge_->setCriticalThreshold(7200);
    rpm_gauge_->setMinimumSize(180, 180);

    coolant_gauge_ = new ArcGauge("Coolant", -40, 150, "°C", this);
    coolant_gauge_->setWarningThreshold(100);
    coolant_gauge_->setCriticalThreshold(120);
    coolant_gauge_->setMinimumSize(180, 180);

    gauges_row->addStretch();
    gauges_row->addWidget(rpm_gauge_);
    gauges_row->addSpacing(24);
    gauges_row->addWidget(coolant_gauge_);
    gauges_row->addStretch();
    root->addLayout(gauges_row);

    // ── Bar gauges ────────────────────────────────
    auto* bars_group = new QGroupBox("Sensors", this);
    bars_group->setStyleSheet(
        "QGroupBox { color:#aaa; border:1px solid #333; border-radius:6px;"
        "margin-top:8px; padding-top:8px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:10px; }"
    );
    auto* bars_lay = new QVBoxLayout(bars_group);
    bars_lay->setSpacing(8);

    throttle_bar_ = new BarGauge("Throttle",  0,  100, "%",  this);
    fuel_bar_     = new BarGauge("Fuel level", 0, 100, "%",  this);
    battery_bar_  = new BarGauge("Battery",  900, 1400, "mV/100", this);

    bars_lay->addWidget(throttle_bar_);
    bars_lay->addWidget(fuel_bar_);
    bars_lay->addWidget(battery_bar_);
    root->addWidget(bars_group);

    root->addStretch();
}

void ECUDashboard::setRPM(int rpm) {
    rpm_gauge_->setValue(static_cast<double>(rpm));
}

void ECUDashboard::setThrottle(int pct) {
    throttle_bar_->setValue(pct);
}

void ECUDashboard::setCoolantTemp(double degC) {
    coolant_gauge_->setValue(degC);
    coolant_gauge_->update();
}

void ECUDashboard::setFuelLevel(int pct) {
    fuel_bar_->setValue(pct);
    if (pct >= 0 && pct < 15) fuel_bar_->setWarningColor(true);
    else                       fuel_bar_->setWarningColor(false);
}

void ECUDashboard::setBatteryVoltage(double volts) {
    // Display as mV/100 to fit in bar range
    battery_bar_->setValue(static_cast<int>(volts * 10));
    battery_bar_->setWarningColor(volts < 11.0);
}

void ECUDashboard::setFaultMask(uint8_t mask) {
    if (mask == 0) {
        fault_badge_->setText("  NO FAULTS  ");
        fault_badge_->setStyleSheet(
            "background:#2a4a2a; color:#3db464; border-radius:6px;"
            "padding:4px 10px; font-size:12px; font-weight:bold;");
    } else {
        QStringList active;
        if (mask & 0x01) active << "OVERHEAT";
        if (mask & 0x02) active << "SENSOR DISC";
        if (mask & 0x04) active << "VOLT DROP";
        if (mask & 0x08) active << "WATCHDOG";
        fault_badge_->setText("  ⚠ " + active.join(" | ") + "  ");
        fault_badge_->setStyleSheet(
            "background:#4a1a1a; color:#ff5555; border-radius:6px;"
            "padding:4px 10px; font-size:12px; font-weight:bold;");
    }
}

void ECUDashboard::setEngineState(int s) {
    engine_state_->setText(engineStateStr(s));
    engine_state_->setStyleSheet(engineStateStyle(s));
}

QString ECUDashboard::engineStateStr(int s) {
    switch (s) {
    case 0: return "● ENGINE OFF";
    case 1: return "⟳ CRANKING...";
    case 2: return "▶ RUNNING";
    case 3: return "⚠ FAULT";
    default: return "? UNKNOWN";
    }
}

QString ECUDashboard::engineStateStyle(int s) {
    switch (s) {
    case 0: return "font-size:14px; font-weight:bold; color:#888;";
    case 1: return "font-size:14px; font-weight:bold; color:#ddaa22;";
    case 2: return "font-size:14px; font-weight:bold; color:#3db464;";
    case 3: return "font-size:14px; font-weight:bold; color:#ff5555;";
    default: return "font-size:14px; font-weight:bold; color:#aaa;";
    }
}
