#include "Impl.hpp"


// DPLPMTUD RFC8899
//enum class PMTUDPhase {
//    Base,
//    Search,
//    SearchComplete,
//    Error,
//};

UDSPSocket::Impl::Impl() {
    rxPacketBufferSizeThreshold_B = 0x100000;
    isTestBandwidthEnabled = false;

# if !defined(NDEBUG) // && defined(UDSP_TEST_STREAMS)
    testStreams();
# endif // NDEBUG

    udpSocket.onReceived = std::bind(
        &Impl::onUdpReceived, this,
        std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4
    );
    isRunning = true;
    thread = std::thread(&Impl::process, this);
    UDPSocket::setThreadPriority(uintptr_t(thread.native_handle()), 'H');
}
UDSPSocket::Impl::~Impl() {
    stop();
}
void UDSPSocket::Impl::stop() {
    isRunning = false;
    if (thread.joinable()) {
        thread.join();
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& connectionIt : connections) {
        auto& c = *connectionIt.second;
        if (not c.isConnected()) {
            continue;
        }
        c.lastPacketTick_us = INT64_MAX; // not isConnected
        c.writeDisconnect();
        udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);

        c.onDisconnected();
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATE_CHANGED
        std::cout << CLR_MAGENTA "Disconnected " << c.connectionId
            << " (interrupt)" CLR_RESET << std::endl;
#     endif // UDSP_TRACE_LEVEL
        if (onDisconnected != nullptr) {
            onDisconnected(&c, 'i');
        }
    }
}

UDSPSocket::Connection& UDSPSocket::Impl::clientConnection() {
    assert(not connections.empty());
    assert(connections.begin()->second != nullptr);
    return *connections.begin()->second;
}
UDSPSocket::Connection& UDSPSocket::Impl::serverConnection(const uint64_t connectionId) {
    //ConnectionId connectionId;
    //connectionId.parts.port = port;
    //connectionId.parts.IPv4 = IPv4;
    auto& ptr = connections[connectionId];
    if (ptr == nullptr) {
        ptr = std::make_unique<Connection>(this);
    }
    return *ptr;
}

bool UDSPSocket::Impl::initConnection(Connection& c, const uint16_t port, const uint32_t IPv4) {
    c.port = port;
    c.IPv4 = IPv4;
    c.partialReset();
    return true;
}

bool UDSPSocket::Impl::connect(const uint16_t port, const uint32_t IPv4) {
    if (not udpSocket.setIpDontFragment(true)) {
        return false;
    }
    if (not udpSocket.setRxBufferSize_B(1 << 23)) { // 2+ MiB for 1+ Gbps
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
        std::cout << "RxBufferSize=" << udpSocket.getRxBufferSize_B() << "\n";
#     endif // UDSP_TRACE_LEVEL
        assert(false);
    }
    if (not udpSocket.setTxBufferSize_B(1 << 23)) { // 2+ MiB for 1+ Gbps
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
        std::cout << "TxBufferSize=" << udpSocket.getTxBufferSize_B() << "\n";
#     endif // UDSP_TRACE_LEVEL
        assert(false);
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (isServer) {
        //TODO
    }
    localPort = 0;
    isServer = false;
    if (connections.empty()) {
        connections[0] = std::make_unique<Connection>(this);
    }
    auto& c = clientConnection();
    if (c.connectionId == 0) {
        std::minstd_rand rng(std::chrono::steady_clock::now().time_since_epoch().count());
        c.connectionId = std::uniform_int_distribution<uint64_t>()(rng);
        return initConnection(c, port, IPv4);
    }
    else if (c.port != port or c.IPv4 != IPv4) {
        c.onDisconnected();
        std::minstd_rand rng(std::chrono::steady_clock::now().time_since_epoch().count());
        c.connectionId = std::uniform_int_distribution<uint64_t>()(rng);
        return initConnection(c, port, IPv4);
    }
    return true;
    //std::cout << CLR_CYAN "rtcSocket.connect " << port << "@" << IPv4 << CLR_RESET << std::endl;
}

bool UDSPSocket::Impl::disconnect() {
    if (not isConnected()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (not connections.empty() and connections.begin()->second != nullptr) {
        connections.begin()->second->isDisconnectRequested = true;
    }
    return true;
}

bool UDSPSocket::Impl::isConnected() {
    if (isServer) {
        return false;
    }
    if (connections.empty()) {
        return false;
    }
    return clientConnection().isConnected();
}

bool UDSPSocket::Impl::listen(const uint16_t port) {
    if (not udpSocket.bind(port)) {
        return false;
    }
    if (not udpSocket.setIpDontFragment(true)) {
        return false;
    }
    if (not udpSocket.setRxBufferSize_B(1 << 23)) { // 2+ MiB for 1+ Gbps
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
        std::cout << "RxBufferSize=" << udpSocket.getRxBufferSize_B() << "\n";
#     endif // UDSP_TRACE_LEVEL
        assert(false);
    }
    if (not udpSocket.setTxBufferSize_B(1 << 23)) { // 2+ MiB for 1+ Gbps
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
        std::cout << "TxBufferSize=" << udpSocket.getTxBufferSize_B() << "\n";
#     endif // UDSP_TRACE_LEVEL
        assert(false);
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (not isServer and not connections.empty()) {
        connections.begin()->second->onDisconnected();
        connections.clear();
    }
    localPort = udpSocket.getLocalPort();
    isServer = true;
    //std::cout << CLR_CYAN "rtcSocket.listen " << localPort << CLR_RESET << std::endl;
    return true;
}

UDSPSocket::UDSPSocket() {
    m_impl = std::make_unique<Impl>();
}
UDSPSocket::~UDSPSocket() {
}
void UDSPSocket::stop() {
    m_impl->stop();
}

bool UDSPSocket::connect(const uint16_t port, const char* IPv4) {
    return m_impl->connect(port, UDPSocket::IPv4FromString(IPv4));
}
bool UDSPSocket::connect(const uint16_t port, const uint32_t IPv4) {
    return m_impl->connect(port, IPv4);
}
bool UDSPSocket::disconnect() {
    return m_impl->disconnect();
}
bool UDSPSocket::isConnected() const {
    return m_impl->isConnected();
}

bool UDSPSocket::listen(const uint16_t port) {
    return m_impl->listen(port);
}

void UDSPSocket::setOnConnected(std::function<void(Connection*)>&& onConnected) {
    m_impl->onConnected = std::move(onConnected);
}
void UDSPSocket::setOnDisconnected(std::function<void(Connection*, char)>&& onDisconnected) {
    m_impl->onDisconnected = std::move(onDisconnected);
}

IPAddress UDSPSocket::getLocalAddress() const {
    return m_impl->udpSocket.getLocalAddress();
}
uint16_t UDSPSocket::getLocalPort() const {
    return m_impl->udpSocket.getLocalPort();
}
IPAddress UDSPSocket::getPeerAddress(Connection* connection) const {
    return connection->IPv4;
}
uint16_t UDSPSocket::getPeerPort(Connection* connection) const {
    return connection->port;
}

void UDSPSocket::setTxStreamPriority(Connection* connection, uint8_t txStreamId, char priority) {
    m_impl->setTxStreamPriority_ts(connection, txStreamId, priority);
}
//char UDSPSocket::getTxStreamPriority(Connection* connection, uint8_t txStreamId) const {
//    return m_impl->getTxStreamPriority_ts(connection, txStreamId);
//}

bool UDSPSocket::send(uintptr_t context, Connection* connection, const void* data,
        uint64_t size_B, bool copy, uint8_t streamId, uint32_t timeout_ms) {
    return m_impl->send_ts(context, connection, data, size_B, copy, streamId, timeout_ms, tick_us());
}

void UDSPSocket::setOnDelivered(Connection* connection, std::function<void(
            uintptr_t context, Connection* connection, const void* data, uint64_t size_B,
            uint8_t txStreamId, char status
        )>&& onDelivered) {
    //m_impl->onDelivered = std::move(onDelivered);
    //connection->onDelivered = std::move(onDelivered);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    connection->commands.emplace_back([connection, on = std::move(onDelivered)] {
        connection->onDelivered = std::move(on);
    });
}

void UDSPSocket::setRxPacketBufferSizeThreshold_B(const uint32_t threshold_B) {
    m_impl->rxPacketBufferSizeThreshold_B = threshold_B;
}

void UDSPSocket::setOnReceived(Connection* connection, std::function<uintptr_t(
            uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status
        )>&& onReceived) {
    //m_impl->onReceived = std::move(onReceived);
    //connection->onReceived = std::move(onReceived);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    connection->commands.emplace_back([connection, on = std::move(onReceived)] {
        connection->onReceived = std::move(on);
    });
}

void UDSPSocket::setTxSpeedLimit_B_s(Connection* connection, uint32_t limit_B_s) {
    if (connection == nullptr) {
        return;
    }
    connection->desiredTxLimit_B_s = limit_B_s;
}
uint32_t UDSPSocket::getTxSpeedLimit_B_s(Connection* connection) const {
    if (connection == nullptr) {
        return 0;
    }
    return connection->desiredTxLimit_B_s;
}

uint32_t UDSPSocket::getRxSpeed_B_s(Connection* connection) const {
    if (connection == nullptr) {
        return 0;
    }
    return connection->rxSpeed_B_s;
}
uint32_t UDSPSocket::getTxSpeed_B_s(Connection* connection) const {
    if (connection == nullptr) {
        return 0;
    }
    return connection->txSpeed_B_s;
}
float UDSPSocket::getRxLoss_prc(Connection* connection) const {
    if (connection == nullptr) {
        return 0.0f;
    }
    return connection->rxPacketsLoss_prc;
}
float UDSPSocket::getTxLoss_prc(Connection* connection) const {
    if (connection == nullptr) {
        return 0.0f;
    }
    return connection->txPacketsLoss_prc;
}
uint32_t UDSPSocket::getRTT_us(Connection* connection) const {
    if (connection == nullptr) {
        return 0;
    }
    return connection->RTT_us;
}
uint32_t UDSPSocket::getTPS() const {
    return m_impl->TPS;
}

void UDSPSocket::setTestBandwidthState(const bool isEnabled) {
    m_impl->isTestBandwidthEnabled = isEnabled;
}
bool UDSPSocket::getTestBandwidthState() const {
    return m_impl->isTestBandwidthEnabled;
}
