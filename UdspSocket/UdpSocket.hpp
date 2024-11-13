#pragma once
#include <cstdint>
#include <string>
#include <cstring>
#include <functional>
#include <array>


class IPAddress {
public:
    IPAddress(const uint32_t IPv4 = 0); // native-endian
    IPAddress(const char* string, size_t size_B = 0);
    IPAddress(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3);

    static const IPAddress localHostV4; // 127.0.0.1
    static const IPAddress localHostV6; // ::1
    static const IPAddress broadcastV4; // 255.255.255.255
    static const IPAddress anyV4; // 0.0.0.0
    static const IPAddress anyV6; // ::

    bool isV4() const;
    bool isV4inV6() const; // IPv4-mapped IPv6
    bool isV6() const;

    // "127.126.125.124" => "127.126.125.124"
    // "::FFFF:127.126.125.124" => "::ffff:127.126.125.124"
    // "::FEFF:127.126.125.124" => "::feff:7f7e:7d7c"
    std::string toString() const;
    uint32_t toIntegerV4() const; // native-endian
    const std::array<uint8_t, 16>& toBytesV6() const; // big-endian

private:
    std::array<uint8_t, 16> m_bytes = {}; // big-endian
    enum class Variant : uint8_t {
        Invalid,
        V4,
        V4inV6,
        V6,
    };
    Variant m_variant = Variant::Invalid;
    //uint32_t m_IPv4 = 0;
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
    static bool isLocalPortOpen(const uint16_t port); // By using TCP

    bool send(const void* data, const uint32_t size_B, const uint16_t port,
        const uint32_t IPv4 = UINT32_MAX);
    std::function<void(void* data, uint32_t size_B, uint16_t port, uint32_t IPv4)>
        onReceived;

    bool setIpDontFragment(const bool isEnabled);
    bool setReusePort(const bool isEnabled);
    bool setRxBufferSize_B(const uint32_t size_B);
    uint32_t getRxBufferSize_B() const;
    bool setTxBufferSize_B(const uint32_t size_B);
    uint32_t getTxBufferSize_B() const;

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


//constexpr bool isBigEndian() {
//# if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
//    return true;
//# elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
//    return false;
//# elif defined(_BIG_ENDIAN) && !defined(_LITTLE_ENDIAN)
//    return true;
//# elif defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
//    return false;
//# elif defined(_WIN32)
//    return false; // Windows is always little-endian
//# else
//#   error "Cannot determine endianness"
//# endif
//}
enum class endian {
# if defined(_MSC_VER) and not defined(__clang__)
    little  = 0,
    big     = 1,
    native  = little
# else
    little  = __ORDER_LITTLE_ENDIAN__,
    big     = __ORDER_BIG_ENDIAN__,
    native  = __BYTE_ORDER__
# endif
};

template <typename value_t>
inline value_t byteswap(const value_t value) noexcept {
    if (sizeof(value_t) == sizeof(uint8_t)) {
        return value;
    }
    else if (sizeof(value_t) == sizeof(uint16_t)) {
#     if defined(_MSC_VER)
        return static_cast<value_t>(_byteswap_ushort(static_cast<uint16_t>(value)));
#     else
        return static_cast<value_t>(__builtin_bswap16(static_cast<uint16_t>(value)));
#     endif
    }
    else if (sizeof(value_t) == sizeof(uint32_t)) {
#     if defined(_MSC_VER)
        return static_cast<value_t>(_byteswap_ulong(static_cast<uint32_t>(value)));
#     else
        return static_cast<value_t>(__builtin_bswap32(static_cast<uint32_t>(value)));
#     endif
    }
    else if (sizeof(value_t) == sizeof(uint64_t)) {
#     if defined(_MSC_VER)
        return static_cast<value_t>(_byteswap_uint64(static_cast<uint64_t>(value)));
#     else
        return static_cast<value_t>(__builtin_bswap64(static_cast<uint64_t>(value)));
#     endif
    }
    else {
        static_assert(sizeof(value_t) <= sizeof(uint64_t), "unsupported size");
    }
}

template <typename value_t>
inline value_t bigEndian(const value_t value) {
    return endian::native == endian::big ? value : byteswap(value);
}
template <typename value_t>
inline value_t bigEndian(const void* data) {
    value_t value = {};
    std::memcpy(&value, data, sizeof(value_t));
    return bigEndian(value);
}
template <typename value_t>
inline value_t littleEndian(const value_t value) {
    return endian::native == endian::little ? value : byteswap(value);
}
template <typename value_t>
inline value_t littleEndian(const void* data) {
    value_t value = {};
    std::memcpy(&value, data, sizeof(value_t));
    return littleEndian(value);
}
