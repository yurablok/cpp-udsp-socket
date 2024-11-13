#pragma once
#include <memory>
#include "UdpSocket.hpp"


class UDSPSocket {
public:
    UDSPSocket();
    ~UDSPSocket();
    void stop();

    // client
    bool connect(const uint16_t port, const char* IPv4);
    bool connect(const uint16_t port, const uint32_t IPv4);
    bool disconnect();
    bool isConnected() const;

    // server
    bool listen(const uint16_t port);

    struct Connection;
    void setOnConnected(std::function<void(Connection*)>&& onConnected);
    // reason:
    //  'c' - closed
    //  't' - timed out
    //  'i' - interrupted
    void setOnDisconnected(std::function<void(Connection*, char reason)>&& onDisconnected);

    IPAddress getLocalAddress() const;
    uint16_t getLocalPort() const;
    //uint32_t getPublicAddress() const; // O(network)
    IPAddress getPeerAddress(Connection* connection) const;
    uint16_t getPeerPort(Connection* connection) const;

    // priority:
    //  'R' - reliable realtime
    //  'r' - unreliable realtime
    //  'H' - reliable high
    //  'h' - unreliable high
    //  'M' - reliable medium (default)
    //  'm' - unreliable medium
    //  'L' - reliable low
    //  'l' - unreliable low
    //   0  - undefined
    //NOTE: Calling it in the onDisconnected callback causes a deadlock!
    void setTxStreamPriority(Connection* connection, uint8_t txStreamId, char priority = 'M');
    //char getTxStreamPriority(Connection* connection, uint8_t txStreamId) const;

    // copy:
    //   true - make an internal copy of the data
    //   false - work with the data by a pointer
    //NOTE: Calling it in the onDisconnected callback causes a deadlock!
    bool send(uintptr_t context, Connection* connection, const void* data, uint64_t size_B,
        bool copy, uint8_t txStreamId = 0, uint32_t timeout_ms = 5000);
    //bool send(uintptr_t context, Connection* connection, std::vector<uint8_t>&& data,
    //    uint8_t txStreamId = 0, uint32_t timeout_ms = 5000);
    //void setOnSend(std::function<void(
    //    uintptr_t context, Connection* connection, uint64_t readOffset_B,
    //    uint32_t pieceSize_B, uint8_t* writeBuffer, uint8_t txStreamId
    //)>&& onSend);

    // status:
    //  's' - success | sent
    //  't' - timed out
    //  'd' - disconnected
    void setOnDelivered(Connection* connection, std::function<void(
        uintptr_t context, Connection* connection, const void* data, uint64_t size_B,
        uint8_t txStreamId, char status
    )>&& onDelivered);

    // threshold_B:
    //   packetSize_B <= threshold_B  -  size_B == packetSize_B, offset_B == 0
    //   packetSize_B >  threshold_B  -  size_B <= packetSize_B, offset_B >= 0
    void setRxPacketBufferSizeThreshold_B(const uint32_t threshold_B = 0x100000);
    // status:
    //  's' - success
    //  'p' - piece
    //  -- 'b' - bad CRC32
    //  'l' - lost
    //  't' - timed out
    //  'd' - disconnected
    void setOnReceived(Connection* connection, std::function<uintptr_t(
        uintptr_t context, Connection* connection, const void* data,
        uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status
    )>&& onReceived);

    //void setRxSpeedLimit_B_s(Connection* connection, uint32_t limit_B_s);
    //uint32_t getRxSpeedLimit_B_s(Connection* connection) const;
    void setTxSpeedLimit_B_s(Connection* connection, uint32_t limit_B_s);
    uint32_t getTxSpeedLimit_B_s(Connection* connection) const;

    uint32_t getRxSpeed_B_s(Connection* connection) const; // 1 Hz
    uint32_t getTxSpeed_B_s(Connection* connection) const; // 1 Hz
    float getRxLoss_prc(Connection* connection) const; // 1 Hz
    float getTxLoss_prc(Connection* connection) const; // 1 Hz
    uint32_t getRTT_us(Connection* connection) const; // 1 Hz
    uint32_t getTPS() const; // 1 Hz

    void setTestBandwidthState(const bool isEnabled = false);
    bool getTestBandwidthState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
