#ifndef DYMOLTBLEINTERFACE_H
#define DYMOLTBLEINTERFACE_H

#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyController>
#include <QQueue>
#include <QBitArray>
#include <QTimer>

class DymoLTBLEInterface : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,
        Scanning,
        Connecting,
        Connected,
        SendingHeader,
        SendingData,
        Printing,
        Disconnecting,
        Error
    };
    Q_ENUM(State)

public:
    explicit DymoLTBLEInterface(QObject* parent = nullptr);

    State state() const;
    QString readError();

public slots:
    bool print(QBitArray const& pixels);

signals:
    void stateChanged(DymoLTBLEInterface::State);
    void errorOccured();
    void printDone();

private slots:
    void M_connect();
    void M_discoveryError(QBluetoothDeviceDiscoveryAgent::Error const& error);
    void M_deviceFound(QBluetoothDeviceInfo const& info);
    void M_discoveryDone();
    void M_deviceDisconnected();
    void M_serviceFound(QBluetoothUuid const& uuid);
    void M_serviceDiscoveryDone();
    void M_serviceStateChanged(QLowEnergyService::ServiceState state);
    void M_charChanged(QLowEnergyCharacteristic const& c, QByteArray const& value);
    void M_descWritten(QLowEnergyDescriptor const& c, QByteArray const& value);
    void M_charWritten(QLowEnergyCharacteristic const& c, QByteArray const& value);

private:
    void M_setState(State state);
    void M_error(QString const& message);
    QByteArray M_makeHeader(int lines);
    QByteArray M_makeHeadCmds(int lines);
    QByteArray M_makeTailCmds(int lines);

private:
    State m_state;
    QString m_error_message;
    QBluetoothUuid const m_data_ep_uuid;
    QBluetoothUuid const m_status_uuid;
    QBluetoothDeviceDiscoveryAgent* m_discovery;
    QBluetoothDeviceInfo m_dev_info;
    QLowEnergyController* m_dev;
    QTimer m_dev_connect_timer;
    QBluetoothUuid m_service_uuid;
    QLowEnergyService* m_service;
    QByteArray m_header;
    QQueue<QByteArray> m_data;
};

#endif // DYMOLTBLEINTERFACE_H
