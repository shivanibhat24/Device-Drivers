#include "ConnectionManager.hpp"
#include <QDebug>

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
{
    tcp_    = new QTcpSocket(this);
    serial_ = new QSerialPort(this);

    // TCP signals
    connect(tcp_, &QTcpSocket::connected,    this, &ConnectionManager::onTcpConnected);
    connect(tcp_, &QTcpSocket::disconnected, this, &ConnectionManager::onTcpDisconnected);
    connect(tcp_, &QTcpSocket::readyRead,    this, &ConnectionManager::onTcpReadyRead);
    connect(tcp_, &QTcpSocket::errorOccurred,this, &ConnectionManager::onTcpError);

    // Serial signals
    connect(serial_, &QSerialPort::readyRead, this, &ConnectionManager::onSerialReadyRead);
    connect(serial_, &QSerialPort::errorOccurred, this, &ConnectionManager::onSerialError);

    // Keepalive ping every 2s
    ping_timer_ = new QTimer(this);
    ping_timer_->setInterval(2000);
    connect(ping_timer_, &QTimer::timeout, this, &ConnectionManager::pingTimer);
}

// ── Control (TCP) ──────────────────────────────────────

void ConnectionManager::connectControl(const QString& host, quint16 port) {
    emit statusMessage(QStringLiteral("Connecting to %1:%2...").arg(host).arg(port));
    tcp_->connectToHost(host, port);
}

void ConnectionManager::disconnectControl() {
    tcp_->disconnectFromHost();
    ping_timer_->stop();
}

bool ConnectionManager::isControlConnected() const {
    return tcp_->state() == QAbstractSocket::ConnectedState;
}

void ConnectionManager::onTcpConnected() {
    emit statusMessage(QStringLiteral("Control channel connected"));
    emit controlConnected();
    ping_timer_->start();
}

void ConnectionManager::onTcpDisconnected() {
    emit statusMessage(QStringLiteral("Control channel disconnected"));
    emit controlDisconnected();
    ping_timer_->stop();
}

void ConnectionManager::onTcpError(QAbstractSocket::SocketError) {
    emit statusMessage(QStringLiteral("TCP error: ") + tcp_->errorString());
}

void ConnectionManager::onTcpReadyRead() {
    // We don't currently expect data back on the control socket,
    // but read and discard to keep the socket clean.
    tcp_->readAll();
}

// ── CAN serial stream ──────────────────────────────────

void ConnectionManager::connectCAN(const QString& portName, int baudRate) {
    serial_->setPortName(portName);
    serial_->setBaudRate(baudRate);
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);

    if (serial_->open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("CAN stream connected on ") + portName);
        emit canConnected();
    } else {
        emit statusMessage(QStringLiteral("CAN serial open failed: ") + serial_->errorString());
    }
}

void ConnectionManager::disconnectCAN() {
    if (serial_->isOpen()) serial_->close();
}

bool ConnectionManager::isCANConnected() const {
    return serial_->isOpen();
}

void ConnectionManager::onSerialReadyRead() {
    QByteArray data = serial_->readAll();
    for (char c : data) {
        processSerialByte(static_cast<uint8_t>(c));
    }
}

void ConnectionManager::onSerialError(QSerialPort::SerialPortError err) {
    if (err != QSerialPort::NoError) {
        emit statusMessage(QStringLiteral("Serial error: ") + serial_->errorString());
        emit canDisconnected();
    }
}

// ── Frame parser state machine ─────────────────────────

void ConnectionManager::processSerialByte(uint8_t b) {
    switch (frame_state_) {
    case FrameState::WAIT_SOF:
        if (b == FRAME_SOF) {
            frame_buf_.clear();
            frame_buf_.append(static_cast<char>(b));
            frame_state_ = FrameState::READ_BODY;
        }
        break;

    case FrameState::READ_BODY:
        frame_buf_.append(static_cast<char>(b));
        if (frame_buf_.size() == FRAME_SIZE) {
            if (static_cast<uint8_t>(frame_buf_.back()) == FRAME_EOF) {
                dispatchFrame();
            }
            // Either way, reset and hunt for next SOF
            frame_state_ = FrameState::WAIT_SOF;
            frame_buf_.clear();
        }
        break;
    }
}

void ConnectionManager::dispatchFrame() {
    if (frame_buf_.size() != FRAME_SIZE) return;

    CANFrame f;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(frame_buf_.constData());
    f.sof  = raw[0];
    f.id   = static_cast<uint16_t>((raw[1] << 8) | raw[2]);
    f.len  = raw[3];
    memcpy(f.data, raw + 4, 8);
    f.eof  = raw[12];

    emit canFrameReceived(f);
}

// ── Command sending ────────────────────────────────────

void ConnectionManager::sendCommand(ControlCmd cmd, uint8_t arg0, uint8_t arg1) {
    if (!isControlConnected()) {
        emit statusMessage(QStringLiteral("Not connected — command dropped"));
        return;
    }
    QByteArray pkt;
    pkt.append(static_cast<char>(static_cast<uint8_t>(cmd)));
    // Determine how many arg bytes to send
    if (cmd == ControlCmd::SET_THROTTLE) {
        pkt.append(static_cast<char>(arg0));
    } else if (cmd == ControlCmd::SET_RPM_TARGET) {
        pkt.append(static_cast<char>(arg0));
        pkt.append(static_cast<char>(arg1));
    }
    tcp_->write(pkt);
}

void ConnectionManager::sendThrottle(int pct) {
    sendCommand(ControlCmd::SET_THROTTLE, static_cast<uint8_t>(qBound(0, pct, 100)));
}

void ConnectionManager::sendFaultInject(ControlCmd faultCmd) {
    sendCommand(faultCmd);
}

void ConnectionManager::sendClearFaults() {
    sendCommand(ControlCmd::CLEAR_FAULTS);
}

void ConnectionManager::sendPing() {
    sendCommand(ControlCmd::PING);
}

void ConnectionManager::pingTimer() {
    sendPing();
}
