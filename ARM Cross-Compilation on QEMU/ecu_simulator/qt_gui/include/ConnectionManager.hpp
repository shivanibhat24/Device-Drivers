#pragma once
// ─────────────────────────────────────────────────────
//  ConnectionManager
//
//  Owns two connections to QEMU:
//    1. QTcpSocket  → TCP :5000 (control commands)
//    2. QSerialPort → COM port / PTY (CAN frames)
//
//  The PTY path is exposed by QEMU on WSL as something
//  like /dev/pts/3. On Windows you bridge it with a
//  named pipe or socat. The launch script handles this.
//
//  Signals emitted to the rest of the GUI:
//    canFrameReceived(CANFrame)
//    controlConnected / controlDisconnected
//    canConnected    / canDisconnected
//    statusMessage(QString)
// ─────────────────────────────────────────────────────
#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QSerialPort>
#include <QTimer>
#include <QByteArray>
#include "ecu_protocol.hpp"

class ConnectionManager : public QObject {
    Q_OBJECT

public:
    explicit ConnectionManager(QObject* parent = nullptr);

    // Connect to QEMU control channel (TCP)
    void connectControl(const QString& host, quint16 port);
    void disconnectControl();

    // Connect to QEMU CAN serial stream
    void connectCAN(const QString& portName, int baudRate = 115200);
    void disconnectCAN();

    bool isControlConnected() const;
    bool isCANConnected()     const;

public slots:
    // Send a control command to the firmware
    void sendCommand(ControlCmd cmd, uint8_t arg0 = 0, uint8_t arg1 = 0);
    void sendThrottle(int pct);
    void sendFaultInject(ControlCmd faultCmd);
    void sendClearFaults();
    void sendPing();

signals:
    void canFrameReceived(CANFrame frame);
    void controlConnected();
    void controlDisconnected();
    void canConnected();
    void canDisconnected();
    void statusMessage(const QString& msg);

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError(QAbstractSocket::SocketError err);
    void onTcpReadyRead();

    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError err);

    void pingTimer();

private:
    QTcpSocket*  tcp_    = nullptr;
    QSerialPort* serial_ = nullptr;
    QTimer*      ping_timer_ = nullptr;

    // Framing state machine for incoming serial data
    enum class FrameState { WAIT_SOF, READ_BODY };
    FrameState   frame_state_ = FrameState::WAIT_SOF;
    QByteArray   frame_buf_;

    void processSerialByte(uint8_t b);
    void dispatchFrame();
};
