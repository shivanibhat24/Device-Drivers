#pragma once
// ─────────────────────────────────────────────────────
//  CANParser
//
//  Receives raw CANFrame structs, decodes them by ID,
//  and emits typed signals the GUI widgets bind to.
//
//  Every signal carries a decoded value + the raw
//  CANFrame so the CAN monitor can display it too.
// ─────────────────────────────────────────────────────
#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include "ecu_protocol.hpp"

// A decoded CAN message ready for display
struct DecodedFrame {
    QDateTime   timestamp;
    uint16_t    id;
    QString     id_str;        // "0x100"
    QString     name;          // "RPM"
    QString     value_str;     // "3200 rpm"
    double      value;         // numeric for charting
    QString     unit;
    QByteArray  raw_data;
    uint8_t     len;
    bool        is_fault = false;
};

class CANParser : public QObject {
    Q_OBJECT

public:
    explicit CANParser(QObject* parent = nullptr);

public slots:
    void parseFrame(CANFrame frame);

signals:
    // Typed sensor signals — dashboard binds to these
    void rpmUpdated(int rpm);
    void throttleUpdated(int pct);
    void coolantTempUpdated(double degC);
    void fuelLevelUpdated(int pct);
    void batteryVoltageUpdated(double volts);
    void faultMaskUpdated(uint8_t mask);
    void engineStateUpdated(int state);   // 0=off 1=cranking 2=running 3=fault
    void dtcReceived(uint16_t code, uint8_t count);

    // Raw decoded frame — CAN monitor table binds to this
    void frameDecoded(DecodedFrame frame);

private:
    DecodedFrame makeFrame(const CANFrame& raw, const QString& name,
                           double value, const QString& unit) const;
};
