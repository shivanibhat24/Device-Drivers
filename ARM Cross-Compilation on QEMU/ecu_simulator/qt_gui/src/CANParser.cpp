#include "CANParser.hpp"

CANParser::CANParser(QObject* parent) : QObject(parent) {}

void CANParser::parseFrame(CANFrame frame) {
    QByteArray raw(reinterpret_cast<const char*>(frame.data), frame.len);

    switch (frame.id) {

    case CAN_ID_RPM: {
        if (frame.len < 2) return;
        int rpm = (frame.data[0] << 8) | frame.data[1];
        emit rpmUpdated(rpm);
        emit frameDecoded(makeFrame(frame, "RPM", rpm, "rpm"));
        break;
    }

    case CAN_ID_THROTTLE: {
        if (frame.len < 1) return;
        int pct = frame.data[0];
        emit throttleUpdated(pct);
        emit frameDecoded(makeFrame(frame, "Throttle", pct, "%"));
        break;
    }

    case CAN_ID_COOLANT_TEMP: {
        if (frame.len < 2) return;
        // Signed 16-bit, scaled ×10
        int16_t raw16 = static_cast<int16_t>((frame.data[0] << 8) | frame.data[1]);
        double  degC  = raw16 / 10.0;
        emit coolantTempUpdated(degC);
        emit frameDecoded(makeFrame(frame, "Coolant temp", degC, "°C"));
        break;
    }

    case CAN_ID_FUEL_LEVEL: {
        if (frame.len < 1) return;
        int pct = frame.data[0];
        // 0xFF = sensor disconnected
        emit fuelLevelUpdated(pct == 0xFF ? -1 : pct);
        emit frameDecoded(makeFrame(frame, "Fuel level",
                                    pct == 0xFF ? -1.0 : pct, "%"));
        break;
    }

    case CAN_ID_VOLTAGE: {
        if (frame.len < 2) return;
        uint16_t mv    = static_cast<uint16_t>((frame.data[0] << 8) | frame.data[1]);
        double   volts = mv / 1000.0;
        emit batteryVoltageUpdated(volts);
        emit frameDecoded(makeFrame(frame, "Battery", volts, "V"));
        break;
    }

    case CAN_ID_FAULT: {
        if (frame.len < 1) return;
        uint8_t mask = frame.data[0];
        // data[1] carries engine_state when len >= 2
        if (frame.len >= 2) {
            emit engineStateUpdated(static_cast<int>(frame.data[1]));
        } else {
            // Infer: any fault → fault state; no fault → running (if we had RPM)
            emit engineStateUpdated(mask ? 3 : 2);
        }
        emit faultMaskUpdated(mask);
        DecodedFrame df = makeFrame(frame, "Fault mask", mask, "");
        df.is_fault  = (mask != 0);
        df.value_str = QStringLiteral("0x%1").arg(mask, 2, 16, QLatin1Char('0')).toUpper();
        emit frameDecoded(df);
        break;
    }

    case CAN_ID_DTC: {
        if (frame.len < 3) return;
        uint16_t code  = static_cast<uint16_t>((frame.data[0] << 8) | frame.data[1]);
        uint8_t  count = frame.data[2];
        emit dtcReceived(code, count);
        DecodedFrame df = makeFrame(frame, "DTC", code, "");
        df.is_fault  = true;
        df.value_str = QStringLiteral("P%1 ×%2")
                           .arg(code, 4, 16, QLatin1Char('0')).toUpper()
                           .arg(count);
        emit frameDecoded(df);
        break;
    }

    default: {
        // Unknown frame — pass through to monitor
        DecodedFrame df;
        df.timestamp = QDateTime::currentDateTime();
        df.id        = frame.id;
        df.id_str    = QStringLiteral("0x%1").arg(frame.id, 3, 16, QLatin1Char('0')).toUpper();
        df.name      = "Unknown";
        df.len       = frame.len;
        df.raw_data  = QByteArray(reinterpret_cast<const char*>(frame.data), frame.len);
        df.value     = 0;
        df.value_str = df.raw_data.toHex(' ').toUpper();
        df.unit      = "";
        emit frameDecoded(df);
        break;
    }
    }
}

DecodedFrame CANParser::makeFrame(const CANFrame& raw, const QString& name,
                                   double value, const QString& unit) const {
    DecodedFrame df;
    df.timestamp = QDateTime::currentDateTime();
    df.id        = raw.id;
    df.id_str    = QStringLiteral("0x%1").arg(raw.id, 3, 16, QLatin1Char('0')).toUpper();
    df.name      = name;
    df.value     = value;
    df.unit      = unit;
    df.len       = raw.len;
    df.raw_data  = QByteArray(reinterpret_cast<const char*>(raw.data), raw.len);
    df.value_str = unit.isEmpty()
                       ? QString::number(value)
                       : QStringLiteral("%1 %2").arg(value).arg(unit);
    return df;
}
