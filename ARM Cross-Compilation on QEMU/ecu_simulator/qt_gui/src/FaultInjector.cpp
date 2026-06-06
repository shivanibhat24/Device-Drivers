#include "FaultInjector.hpp"

FaultInjector::FaultInjector(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background:#1a1a2e; color:white;");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(14);

    // ── Throttle control ───────────────────────────
    auto* throttle_box = new QGroupBox("Throttle control", this);
    throttle_box->setStyleSheet(
        "QGroupBox{color:#aaa;border:1px solid #333;border-radius:6px;"
        "margin-top:8px;padding-top:8px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;}");
    auto* tlay = new QVBoxLayout(throttle_box);

    auto* slider_row = new QHBoxLayout;
    QLabel* min_lbl = new QLabel("0%",   this);
    QLabel* max_lbl = new QLabel("100%", this);
    min_lbl->setStyleSheet("color:#666; font-size:11px;");
    max_lbl->setStyleSheet("color:#666; font-size:11px;");

    throttle_slider_ = new QSlider(Qt::Horizontal, this);
    throttle_slider_->setRange(0, 100);
    throttle_slider_->setValue(0);
    throttle_slider_->setTickInterval(10);
    throttle_slider_->setTickPosition(QSlider::TicksBelow);
    throttle_slider_->setStyleSheet(
        "QSlider::groove:horizontal{background:#333;height:6px;border-radius:3px;}"
        "QSlider::handle:horizontal{background:#3db464;width:16px;height:16px;"
        "margin:-5px 0;border-radius:8px;}"
        "QSlider::sub-page:horizontal{background:#3db464;border-radius:3px;}");

    slider_row->addWidget(min_lbl);
    slider_row->addWidget(throttle_slider_);
    slider_row->addWidget(max_lbl);
    tlay->addLayout(slider_row);

    throttle_label_ = new QLabel("Throttle: 0%", this);
    throttle_label_->setAlignment(Qt::AlignCenter);
    throttle_label_->setStyleSheet("font-size:16px; font-weight:bold; color:#3db464;");
    tlay->addWidget(throttle_label_);
    root->addWidget(throttle_box);

    connect(throttle_slider_, &QSlider::valueChanged,
            this, &FaultInjector::onThrottleSlider);

    // ── Fault injection ────────────────────────────
    auto* fault_box = new QGroupBox("Fault injection", this);
    fault_box->setStyleSheet(throttle_box->styleSheet());
    auto* flay = new QVBoxLayout(fault_box);
    flay->setSpacing(8);

    overheat_btn_    = makeFaultBtn("🌡  Inject Overheat",      "#8b2020", this);
    sensor_disc_btn_ = makeFaultBtn("⚡  Inject Sensor Disc.",   "#7a5a10", this);
    volt_drop_btn_   = makeFaultBtn("🔋  Inject Voltage Drop",   "#1e4a7a", this);

    flay->addWidget(overheat_btn_);
    flay->addWidget(sensor_disc_btn_);
    flay->addWidget(volt_drop_btn_);
    root->addWidget(fault_box);

    connect(overheat_btn_,    &QPushButton::clicked, this, &FaultInjector::onOverheatBtn);
    connect(sensor_disc_btn_, &QPushButton::clicked, this, &FaultInjector::onSensorDiscBtn);
    connect(volt_drop_btn_,   &QPushButton::clicked, this, &FaultInjector::onVoltDropBtn);

    // ── Clear faults ────────────────────────────────
    clear_btn_ = new QPushButton("✔  Clear All Faults", this);
    clear_btn_->setFixedHeight(40);
    clear_btn_->setStyleSheet(
        "QPushButton{background:#1e4a2a;color:#3db464;border:1px solid #3db464;"
        "border-radius:6px;font-size:13px;font-weight:bold;}"
        "QPushButton:hover{background:#2a6a3a;}"
        "QPushButton:pressed{background:#164a22;}");
    root->addWidget(clear_btn_);
    connect(clear_btn_, &QPushButton::clicked, this, &FaultInjector::onClearBtn);

    root->addStretch();
}

void FaultInjector::onThrottleSlider(int value) {
    throttle_label_->setText(QStringLiteral("Throttle: %1%").arg(value));
    emit throttleChanged(value);
}

void FaultInjector::onOverheatBtn()    { emit injectFault(ControlCmd::INJECT_OVERHEAT);    }
void FaultInjector::onSensorDiscBtn() { emit injectFault(ControlCmd::INJECT_SENSOR_DISC); }
void FaultInjector::onVoltDropBtn()   { emit injectFault(ControlCmd::INJECT_VOLT_DROP);   }
void FaultInjector::onClearBtn()      { emit clearFaultsRequested(); }

QPushButton* FaultInjector::makeFaultBtn(const QString& label,
                                          const QString& color,
                                          QWidget* parent) {
    auto* btn = new QPushButton(label, parent);
    btn->setFixedHeight(38);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:#eee;border:none;border-radius:6px;"
        "font-size:13px;text-align:left;padding-left:12px;}"
        "QPushButton:hover{filter:brightness(1.2);opacity:0.85;}"
        "QPushButton:pressed{opacity:0.7;}").arg(color));
    return btn;
}
