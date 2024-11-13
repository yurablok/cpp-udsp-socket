#pragma once
#include <cstdint>
#include <string>
#include <functional>


class IPAddress {
public:
    //struct uint128_t { uint32_t u[4]; };

    IPAddress(const uint32_t IPv4 = 0);
    IPAddress(const char* string, size_t size_b = 0);
    IPAddress(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3);

    static const IPAddress LocalHostV4; // 127.0.0.1
    //static const IPAddress LocalHostV6; // ::1
    static const IPAddress BroadcastV4; // 255.255.255.255
    static const IPAddress AnyV4; // 0.0.0.0
    //static const IPAddress AnyV6; // ::

    std::string toString() const;
    uint32_t toIntegerV4() const;
    //uint128_t toDataV6() const;

private:
    uint32_t m_IPv4 = 0;
    //uint128_t m_address = {};
    // Invalid embedded IPv4 mapping: ::FFFF:192.168.300.1
};

class UDPSocket {
public:
    UDPSocket();
    UDPSocket(const UDPSocket& other) = delete;
    ~UDPSocket();
    UDPSocket& operator=(const UDPSocket& other) = delete;

    bool bind(const uint16_t port = 0, const uint32_t IPv4 = 0);
    void unbind();
    // port, IPv4
    //std::pair<uint16_t, uint32_t> getLocalAddress() const;
    static IPAddress getLocalAddress();
    uint16_t getLocalPort() const;

    bool send(const void* data, const uint32_t size_b, const uint16_t port,
        const uint32_t IPv4 = UINT32_MAX);
    std::function<void(void* data, uint32_t size_b, uint16_t port, uint32_t IPv4)>
        onReceived;

    bool setIpDontFragment(const bool isEnabled);
    bool setReusePort(const bool isEnabled);
    bool setRxBufferSize_b(const uint32_t size_b);
    uint32_t getRxBufferSize_b() const;
    bool setTxBufferSize_b(const uint32_t size_b);
    uint32_t getTxBufferSize_b() const;

    // timeout_ms:
    // 0 - non-blocking, the delay of input flow depends on polling frequency
    // >0 - blocking, the delay of input flow depends on receive events
    void process(const uint32_t timeout_ms = 1000 / 50);

    static uint32_t IPv4FromString(const char* string);

    // 'H'ighest, 'h'igh, 'n'ormal, 'l'ow, 'L'owest
    static bool setThreadPriority(const uintptr_t thread, const char priority = 'n');

private:
    void open();
    void close();
    uintptr_t m_socket = 0;
};
