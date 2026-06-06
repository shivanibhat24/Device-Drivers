#pragma once
// ─────────────────────────────────────────────────────
//  ECUDashboard
//
//  Central widget showing live sensor gauges:
//    - RPM  (arc gauge, 0–8000)
//    - Coolant temperature (arc gauge, -40–150°C)
//    - Fuel level (bar)
//    - Battery voltage (bar)
//    - Throttle position (bar)
//    - Engine state badge
//    - Active fault indicator
//
//  All updates come via slots — no polling.
// ─────────────────────────────────────────────────────
#pragma once
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QTimer>
#include <cmath>

// ── Arc Gauge widget ──────────────────────────────────
class ArcGauge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue)

public:
    ArcGauge(const QString& title, double min, double max,
             const QString& unit, QWidget* parent = nullptr)
        : QWidget(parent), title_(title), min_(min), max_(max),
          unit_(unit), value_(min)
    {
        setMinimumSize(160, 160);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    double value() const { return value_; }

    void setWarningThreshold(double v)  { warn_ = v; }
    void setCriticalThreshold(double v) { crit_ = v; }

public slots:
    void setValue(double v) {
        value_ = qBound(min_, v, max_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int side = qMin(width(), height());
        QRectF rect(
            (width()  - side) / 2.0 + 10,
            (height() - side) / 2.0 + 10,
            side - 20, side - 20
        );

        // Arc parameters: 225° to 315° (270° sweep, open at bottom-right)
        constexpr double START_DEG = 225.0;
        constexpr double SPAN_DEG  = 270.0;

        // Background track
        QPen track(QColor(60, 60, 60), 12, Qt::SolidLine, Qt::RoundCap);
        p.setPen(track);
        p.drawArc(rect, static_cast<int>(START_DEG * 16),
                  static_cast<int>(-SPAN_DEG * 16));

        // Coloured fill arc
        double pct    = (value_ - min_) / (max_ - min_);
        double sweep  = pct * SPAN_DEG;
        QColor fill;
        if (crit_ > 0 && value_ >= crit_) fill = QColor(220, 50, 50);
        else if (warn_ > 0 && value_ >= warn_) fill = QColor(220, 160, 0);
        else fill = QColor(50, 180, 100);

        QPen arc_pen(fill, 12, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arc_pen);
        p.drawArc(rect, static_cast<int>(START_DEG * 16),
                  static_cast<int>(-sweep * 16));

        // Value text
        p.setPen(Qt::white);
        QFont vf = font();
        vf.setPointSize(static_cast<int>(side * 0.14));
        vf.setBold(true);
        p.setFont(vf);
        p.drawText(rect, Qt::AlignCenter,
                   QString::number(static_cast<int>(value_)));

        // Unit text
        QFont uf = font();
        uf.setPointSize(static_cast<int>(side * 0.08));
        p.setFont(uf);
        p.setPen(QColor(160, 160, 160));
        QRectF unit_rect = rect.adjusted(0, rect.height() * 0.35, 0, 0);
        p.drawText(unit_rect, Qt::AlignCenter, unit_);

        // Title text
        QFont tf = font();
        tf.setPointSize(static_cast<int>(side * 0.08));
        p.setFont(tf);
        p.setPen(QColor(180, 180, 180));
        QRectF title_rect(rect.left(), rect.bottom() - 22, rect.width(), 22);
        p.drawText(title_rect, Qt::AlignCenter, title_);
    }

private:
    QString title_, unit_;
    double  min_, max_, value_;
    double  warn_ = -1, crit_ = -1;
};

// ── Labelled bar gauge ────────────────────────────────
class BarGauge : public QWidget {
    Q_OBJECT
public:
    BarGauge(const QString& label, int min, int max,
             const QString& unit, QWidget* parent = nullptr)
        : QWidget(parent), label_(label), min_(min), max_(max), unit_(unit)
    {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);

        lbl_ = new QLabel(label + ":", this);
        lbl_->setFixedWidth(120);
        lbl_->setStyleSheet("color: #aaa; font-size: 12px;");
        lay->addWidget(lbl_);

        bar_ = new QProgressBar(this);
        bar_->setRange(min, max);
        bar_->setValue(min);
        bar_->setTextVisible(false);
        bar_->setFixedHeight(16);
        bar_->setStyleSheet(
            "QProgressBar { background: #333; border-radius: 4px; }"
            "QProgressBar::chunk { background: #3db464; border-radius: 4px; }"
        );
        lay->addWidget(bar_);

        val_lbl_ = new QLabel("--", this);
        val_lbl_->setFixedWidth(70);
        val_lbl_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val_lbl_->setStyleSheet("color: white; font-size: 12px;");
        lay->addWidget(val_lbl_);
    }

public slots:
    void setValue(int v) {
        bar_->setValue(qBound(min_, v, max_));
        val_lbl_->setText(v < 0
            ? "Disconnected"
            : QStringLiteral("%1 %2").arg(v).arg(unit_));
    }
    void setValueF(double v) { setValue(static_cast<int>(v)); }
    void setWarningColor(bool warn) {
        bar_->setStyleSheet(warn
            ? "QProgressBar{background:#333;border-radius:4px;}"
              "QProgressBar::chunk{background:#dc8c14;border-radius:4px;}"
            : "QProgressBar{background:#333;border-radius:4px;}"
              "QProgressBar::chunk{background:#3db464;border-radius:4px;}");
    }

private:
    QLabel* lbl_;
    QLabel* val_lbl_;
    QProgressBar* bar_;
    QString label_, unit_;
    int min_, max_;
};

// ── Main dashboard widget ─────────────────────────────
class ECUDashboard : public QWidget {
    Q_OBJECT
public:
    explicit ECUDashboard(QWidget* parent = nullptr);

public slots:
    void setRPM(int rpm);
    void setThrottle(int pct);
    void setCoolantTemp(double degC);
    void setFuelLevel(int pct);
    void setBatteryVoltage(double volts);
    void setFaultMask(uint8_t mask);
    void setEngineState(int state);

private:
    ArcGauge*  rpm_gauge_     = nullptr;
    ArcGauge*  coolant_gauge_ = nullptr;
    BarGauge*  throttle_bar_  = nullptr;
    BarGauge*  fuel_bar_      = nullptr;
    BarGauge*  battery_bar_   = nullptr;
    QLabel*    engine_state_  = nullptr;
    QLabel*    fault_badge_   = nullptr;

    static QString engineStateStr(int s);
    static QString engineStateStyle(int s);
};
