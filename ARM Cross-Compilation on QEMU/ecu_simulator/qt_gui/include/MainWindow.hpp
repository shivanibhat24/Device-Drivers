#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QAction>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSettings>

#include "ConnectionManager.hpp"
#include "CANParser.hpp"
#include "ECUDashboard.hpp"
#include "FaultInjector.hpp"
#include "CANMonitor.hpp"
#include "DTCViewer.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void openConnectDialog();
    void onControlConnected();
    void onControlDisconnected();
    void onCANConnected();
    void onCANDisconnected();
    void onStatusMessage(const QString& msg);

private:
    // Core components
    ConnectionManager* conn_mgr_  = nullptr;
    CANParser*         can_parser_= nullptr;

    // UI panels
    ECUDashboard*  dashboard_  = nullptr;
    FaultInjector* injector_   = nullptr;
    CANMonitor*    can_monitor_= nullptr;
    DTCViewer*     dtc_viewer_ = nullptr;

    // Status bar indicators
    QLabel* ctrl_status_  = nullptr;
    QLabel* can_status_   = nullptr;

    void buildUI();
    void buildToolbar();
    void wireSignals();
    void updateConnectionStatus();
};
