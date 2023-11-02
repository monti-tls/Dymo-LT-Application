#include "dymo_lt_ble_interface.h"
#include <QBuffer>
#include <QTimer>

DymoLTBLEInterface::DymoLTBLEInterface(QObject* parent) :
    QObject{parent},
    m_state(State::Idle),
    m_data_ep_uuid(QBluetoothUuid::fromString("{be3dd651-2b3d-42f1-99c1-f0f749dd0678}")),
    m_status_uuid(QBluetoothUuid::fromString("{be3dd652-2b3d-42f1-99c1-f0f749dd0678}")),
    m_dev(nullptr)
{
    m_discovery = new QBluetoothDeviceDiscoveryAgent(this);
    m_discovery->setLowEnergyDiscoveryTimeout(1000);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &DymoLTBLEInterface::M_deviceFound);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, &DymoLTBLEInterface::M_discoveryError);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::finished, this, &DymoLTBLEInterface::M_discoveryDone);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::canceled, this, &DymoLTBLEInterface::M_discoveryDone);

    m_dev_connect_timer.setInterval(3000);
    m_dev_connect_timer.setSingleShot(true);
    connect(&m_dev_connect_timer, &QTimer::timeout, this, [this]()
    {
        if (!m_dev || m_dev->state() != QLowEnergyController::ConnectedState)
            M_error("Device connection timeout");
    });
}

DymoLTBLEInterface::State DymoLTBLEInterface::state() const
{
    return m_state;
}

QString DymoLTBLEInterface::readError()
{
    if (m_state == State::Error)
        M_setState(State::Idle);

    QString msg = m_error_message;
    m_error_message.clear();

    return msg;
}

bool DymoLTBLEInterface::print(QBitArray const& pixels)
{
    // Check if the pixel data matches our strip size
    const int LINE_WIDTH = 32;
    if (pixels.size() < LINE_WIDTH || (pixels.size() % LINE_WIDTH) != 0)
        return false;

    // Pack pixels into an array of bytes, adding the head & tail commands
    QByteArray blob = M_makeHeadCmds(pixels.size() / LINE_WIDTH);
    quint8 pixel = 0;
    for (int i = 0, j = 0; i < pixels.size(); ++i)
    {
        pixel >>= 1;
        pixel |= pixels[i] ? 0x80 : 0x00;

        if (++j == 8)
        {
            blob.append(reinterpret_cast<const char*>(&pixel), 1);
            pixel = 0;
            j = 0;
        }
    }
    blob.append(M_makeTailCmds(pixels.size() / LINE_WIDTH));

    // Prepare the header
    m_header = M_makeHeader(pixels.size() / LINE_WIDTH);

    // Separate blob into buffers of max 501 bytes
    QByteArray data(1, char(0x00));
    for (int i = 0; i < blob.size(); ++i)
    {
        data.append(1, blob[i]);

        if (data.size() == 501 || i == blob.size()-1)
        {
            m_data.enqueue(data);
            data = QByteArray(1, data[0]+1); // increment buffer size
        }
    }

    M_connect();
    return true;
}

void DymoLTBLEInterface::M_connect()
{
    if (m_state != State::Idle)
        return;

    // Start device discovery
    M_setState(State::Scanning);
    m_discovery->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void DymoLTBLEInterface::M_discoveryError(QBluetoothDeviceDiscoveryAgent::Error const& error)
{
    QTextStream s;
    s << "Discovery error: " << error;
    M_error(s.readAll());
}

void DymoLTBLEInterface::M_deviceFound(QBluetoothDeviceInfo const& info)
{
    // Ignore non-BLE devices
    if ((info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) &&
        info.name().startsWith("Letratag"))
    {
        if (!m_dev_info.isValid())
            m_dev_info = info;

        qDebug() << m_dev_info.manufacturerData();
        qDebug() << m_dev_info.manufacturerIds();
    }
}

void DymoLTBLEInterface::M_discoveryDone()
{
    if (!m_dev_info.isValid())
        M_error("No Dymo Letratag found");
    else
    {
        // Create device
        m_dev = QLowEnergyController::createCentral(m_dev_info);
        m_service_uuid = QBluetoothUuid();

        // When connected, discover services
        QObject::connect(m_dev, &QLowEnergyController::connected, this, [this](){ m_dev->discoverServices(); });

        // Manage errors when they happen
        QObject::connect(m_dev, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error const& error)
        {
            QTextStream s;
            s << "Device error: " << error;
            M_error(s.readAll());
        });

        QObject::connect(m_dev, &QLowEnergyController::disconnected, this, &DymoLTBLEInterface::M_deviceDisconnected);
        QObject::connect(m_dev, &QLowEnergyController::serviceDiscovered, this, &DymoLTBLEInterface::M_serviceFound);
        QObject::connect(m_dev, &QLowEnergyController::discoveryFinished, this, &DymoLTBLEInterface::M_serviceDiscoveryDone);

        m_dev_connect_timer.start();
        M_setState(State::Connecting);
        m_dev->connectToDevice();
    }
}

void DymoLTBLEInterface::M_deviceDisconnected()
{
    m_dev->deleteLater();
    m_dev = nullptr;
    m_dev_info = QBluetoothDeviceInfo();

    if (m_state != State::Disconnecting && m_state != State::Error)
        M_error(!m_error_message.isEmpty() ? m_error_message : "Unexpected disconnection");
    else
        M_setState(State::Idle);
}

void DymoLTBLEInterface::M_serviceFound(QBluetoothUuid const& uuid)
{
    if (uuid.toString() == "{be3dd650-2b3d-42f1-99c1-f0f749dd0678}")
        m_service_uuid = uuid;
}

void DymoLTBLEInterface::M_serviceDiscoveryDone()
{
    qDebug() << m_service_uuid;

    if (m_service_uuid.isNull())
        M_error("No Letratag service found");
    else
    {
        m_service = m_dev->createServiceObject(m_service_uuid, m_dev);

        connect(m_service, &QLowEnergyService::stateChanged, this, &DymoLTBLEInterface::M_serviceStateChanged);
        connect(m_service, &QLowEnergyService::characteristicChanged, this, &DymoLTBLEInterface::M_charChanged);
        connect(m_service, &QLowEnergyService::descriptorWritten, this, &DymoLTBLEInterface::M_descWritten);
        connect(m_service, &QLowEnergyService::characteristicWritten, this, &DymoLTBLEInterface::M_charWritten);
        m_service->discoverDetails();
    }
}

void DymoLTBLEInterface::M_serviceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state == QLowEnergyService::RemoteServiceDiscovered)
    {
        M_setState(State::Connected);
        m_dev_connect_timer.stop();

        // Enable notifications on status descriptor
        QLowEnergyCharacteristic status = m_service->characteristic(m_status_uuid);
        m_service->writeDescriptor(status.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration), QByteArray::fromHex("0100"));
    }
}

void DymoLTBLEInterface::M_charChanged(QLowEnergyCharacteristic const& c, QByteArray const& value)
{
    Q_UNUSED(value)

    // When the status characteristic is updated, printing is done
    if (m_state == State::Printing && c.uuid() == m_status_uuid)
    {
        M_setState(State::Disconnecting);
        m_dev->disconnectFromDevice();
    }
}

void DymoLTBLEInterface::M_descWritten(QLowEnergyDescriptor const& d, QByteArray const& value)
{
    Q_UNUSED(d)
    Q_UNUSED(value)

    // Send header
    M_setState(State::SendingHeader);
    m_service->writeCharacteristic(m_service->characteristic(m_data_ep_uuid), m_header);
}

void DymoLTBLEInterface::M_charWritten(QLowEnergyCharacteristic const& c, QByteArray const& value)
{
    Q_UNUSED(c)
    Q_UNUSED(value)

    if (m_state == State::SendingHeader || m_state == State::SendingData)
    {
        if (m_data.isEmpty())
            M_setState(State::Printing);
        else
        {
            M_setState(State::SendingData);
            QByteArray line = m_data.dequeue();
            m_service->writeCharacteristic(m_service->characteristic(m_data_ep_uuid), line);
        }
    }
}

void DymoLTBLEInterface::M_setState(State state)
{
    if (m_state != state)
        emit stateChanged(m_state = state);

    if (m_state == State::Error)
        emit errorOccured();

    if (m_state == State::Printing)
        emit printDone();
}

void DymoLTBLEInterface::M_error(QString const& message)
{
    m_error_message = message;

    if (m_dev)
    {
        if (m_dev->state() != QLowEnergyController::UnconnectedState)
            m_dev->disconnectFromDevice();
        else
        {
            m_dev->deleteLater();
            m_dev = nullptr;
            M_setState(State::Error);
        }
    }
    else
        M_setState(State::Error);
}

QByteArray DymoLTBLEInterface::M_makeHeader(int lines)
{
    QByteArray header;
    QBuffer buffer(&header);
    buffer.open(QIODevice::WriteOnly);

    // Magic sequence
    buffer.write("\xFF\xF0\x12\x34", 4);

    // Number of pixel data bytes + 24
    quint32 size = qToLittleEndian(4*lines + 24);
    buffer.write(reinterpret_cast<const char*>(&size), sizeof(size));

    // Simple checksum
    quint8 checksum = 0;
    for (int i = 0; i < header.size(); ++i)
        checksum += header[i];
    buffer.write(reinterpret_cast<const char*>(&checksum), 1);

    return header;
}

QByteArray DymoLTBLEInterface::M_makeHeadCmds(int lines)
{
    QByteArray header;
    QBuffer buffer(&header);
    buffer.open(QIODevice::WriteOnly);

    // Seems to be command for 'session counter'
    buffer.write("\x1B\x73", 2);
    buffer.write("\x9A\x02\x00\x00", 4);

    // Seems to be command to specify size of graphic
    buffer.write("\x1B\x44\x01\x02", 4);

    // Number of lines (height of graphic)
    quint32 size = qToLittleEndian(lines);
    buffer.write(reinterpret_cast<const char*>(&size), sizeof(size));

    // Number of rows (width of strip), 32 pixels
    buffer.write("\x20\x00\x00\x00", 4);

    return header;
}

QByteArray DymoLTBLEInterface::M_makeTailCmds(int lines)
{
    Q_UNUSED(lines)

    QByteArray header;
    QBuffer buffer(&header);
    buffer.open(QIODevice::WriteOnly);

    // Magic sequence
    buffer.write("\x1B\x45", 2); // Line feed
    buffer.write("\x1B\x41", 2);
    buffer.write("\x1B\x51\x12\x34", 4); // Line tab

    return header;
}
