#include "UdpSocket.hpp"

#include <cassert>
#include <array>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#   define NOMINMAX // Fixes the conflicts with STL
#   define _WIN32_WINNT 0x0601 // Targeting Windows 7 and later
#   define WINVER 0x0601 // Ensuring compatibility with Windows 7
//#   include <Winsock2.h>
#   include <Ws2tcpip.h>
//#   include <Ws2ipdef.h>
#else
#   include <unistd.h>
#   include <poll.h>
#   include <fcntl.h>
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <pthread.h>
#endif


#ifdef _WIN32
namespace {
struct SocketInitializer {
    SocketInitializer() {
        WSADATA init;
        void(WSAStartup(MAKEWORD(2, 2), &init));
    }
    ~SocketInitializer() {
        WSACleanup();
    }
};
SocketInitializer g_socketInitializer;
} // namespace
#endif


IPAddress::IPAddress(const uint32_t IPv4) {
    m_bytes[10] = 0xFF;
    m_bytes[11] = 0xFF;
    const uint32_t IPv4_BE = bigEndian(IPv4);
    std::memcpy(m_bytes.data() + 12, &IPv4_BE, 4);
    m_variant = Variant::V4;
}
IPAddress::IPAddress(const char* string, size_t size_B) {
    //if (size_B == 0) {
    //    size_B = strlen(string);
    //}
    if (inet_pton(AF_INET6, string, m_bytes.data()) == 1) {
        if (std::memcmp(m_bytes.data(), "\0\0\0\0\0\0\0\0\0\0\xFF\xFF", 12) == 0) {
            m_variant = Variant::V4inV6;
        }
        else {
            m_variant = Variant::V6;
        }
        return;
    }
    uint32_t IPv4_BE = 0;
    if (inet_pton(AF_INET, string, &IPv4_BE) == 1) {
        m_bytes[10] = 0xFF;
        m_bytes[11] = 0xFF;
        std::memcpy(m_bytes.data() + 12, &IPv4_BE, 4);
        m_variant = Variant::V4;
        return;
    }
    m_variant = Variant::Invalid;
}
IPAddress::IPAddress(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) {
    m_bytes[10] = 0xFF;
    m_bytes[11] = 0xFF;
    m_bytes[12] = byte0;
    m_bytes[13] = byte1;
    m_bytes[14] = byte2;
    m_bytes[15] = byte3;
    m_variant = Variant::V4;
}

const IPAddress IPAddress::localHostV4 = IPAddress(INADDR_LOOPBACK); // 127.0.0.1
const IPAddress IPAddress::localHostV6 = IPAddress("::1");
const IPAddress IPAddress::broadcastV4 = IPAddress(INADDR_BROADCAST); // 255.255.255.255
const IPAddress IPAddress::anyV4 = IPAddress(INADDR_ANY); // 0.0.0.0
const IPAddress IPAddress::anyV6 = IPAddress("::");

bool IPAddress::isV4() const {
    switch (m_variant) {
    case Variant::V4:
        return true;
    default:
        return false;
    }
}
bool IPAddress::isV4inV6() const {
    switch (m_variant) {
    case Variant::V4inV6:
        return true;
    default:
        return false;
    }
}
bool IPAddress::isV6() const {
    switch (m_variant) {
    case Variant::V4inV6:
    case Variant::V6:
        return true;
    default:
        return false;
    }
}

std::string IPAddress::toString() const {
    if (m_variant == Variant::Invalid) {
        return {};
    }
    char buffer[INET6_ADDRSTRLEN] = {};
    if (isV4()) {
        uint32_t IPv4_BE = 0;
        std::memcpy(&IPv4_BE, m_bytes.data() + 12, 4);
        if (inet_ntop(AF_INET, &IPv4_BE, buffer, INET_ADDRSTRLEN) == nullptr) {
            return {};
        }
        return buffer;
    }
    else {
        if (inet_ntop(AF_INET6, m_bytes.data(), buffer, INET6_ADDRSTRLEN) == nullptr) {
            return {};
        }
        return buffer;
    }
}
uint32_t IPAddress::toIntegerV4() const {
    return bigEndian<uint32_t>(m_bytes.data() + 12);
}
const std::array<uint8_t, 16>& IPAddress::toBytesV6() const {
    return m_bytes;
}

UDPSocket::UDPSocket() {
    open();
}
UDPSocket::~UDPSocket() {
    close();
}

bool UDPSocket::bind(const uint16_t port, const uint32_t IPv4) {
    if (IPv4 == UINT32_MAX) {
        return false;
    }
    unbind();
    sockaddr_in addr = {};
    addr.sin_addr.s_addr = bigEndian(IPv4);
    addr.sin_family = AF_INET;
    addr.sin_port = bigEndian(port);
    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        //err() << "Failed to bind socket to port " << port << std::endl;
        return false;
    }
    return true;
}
void UDPSocket::unbind() {
    open();
}
//std::pair<uint16_t, uint32_t> UDPSocket::getLocalAddress() const {
//    if (m_socket == 0) {
//        return { 0, 0 };
//    }
//    sockaddr_in addr = {};
//# if defined(_WIN32)
//    int32_t size = sizeof(addr);
//# else
//    uint32_t size = sizeof(addr);
//# endif
//    if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &size) == -1) {
//        return { 0, 0 };
//    }
//    return { bigEndian(addr.sin_port), bigEndian(addr.sin_addr.s_addr) };
//}
IPAddress UDPSocket::getLocalAddress() {
    //if (m_socket == 0) {
    //    return { 0, 0 };
    //}
    UDPSocket udp;
    //if (not udp.bind(0, INADDR_LOOPBACK)) {
    if (not udp.bind(0, INADDR_LOOPBACK)) {
        return {};
    }
    sockaddr_in addr = {};
//# if defined(_WIN32)
//    int32_t size = sizeof(addr);
//# else
//    uint32_t size = sizeof(addr);
//# endif
    socklen_t size = sizeof(addr);
    if (::getsockname(udp.m_socket, reinterpret_cast<sockaddr*>(&addr), &size) == -1) {
        return {};
    }
    return IPAddress(bigEndian(addr.sin_addr.s_addr));
}
uint16_t UDPSocket::getLocalPort() const {
    if (m_socket == 0) {
        return 0;
    }
    sockaddr_in addr = {};
//# if defined(_WIN32)
//    int32_t size = sizeof(addr);
//# else
//    uint32_t size = sizeof(addr);
//# endif
    socklen_t size = sizeof(addr);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &size) == -1) {
        return 0;
    }
    return bigEndian(addr.sin_port);
}
bool UDPSocket::isLocalPortOpen(const uint16_t port) {
#ifdef _WIN32
    //#include <winsock2.h>
#else
    //#include <errno.h>
#endif

    int32_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
# ifdef _WIN32
    u_long mode = 1;
    ::ioctlsocket(sock, FIONBIO, &mode);
# else
    ::fcntl(sock, F_SETFL, O_NONBLOCK);
# endif

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = bigEndian(port);
    addr.sin_addr.s_addr = bigEndian<uint32_t>(INADDR_LOOPBACK);

    int32_t result = ::connect(sock, (sockaddr*)&addr, sizeof(addr));
    if (result < 0) {
#     ifdef _WIN32
        const int32_t err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            ::closesocket(sock);
            return false;
        }
#     else
        if (errno != EINPROGRESS) {
            ::close(sock);
            return false;
        }
#     endif
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100 * 1000; // 100 ms

    result = ::select(sock + 1, nullptr, &writeSet, nullptr, &timeout);

    bool open = false;
    if (result > 0 && FD_ISSET(sock, &writeSet)) {
        int32_t err = 0;
        socklen_t len = sizeof(err);
        ::getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        open = (err == 0);
    }
# ifdef _WIN32
    ::closesocket(sock);
# else
    ::close(sock);
# endif
    return open;
}

bool UDPSocket::send(const void* data, const uint32_t size_B, const uint16_t port,
        const uint32_t IPv4) {
    if (m_socket == 0) {
        return false;
    }
    if (size_B > 65507) { // 65527 NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE
        return false;
    }
    sockaddr_in addr = {};
    addr.sin_addr.s_addr = bigEndian(IPv4);
    addr.sin_family = AF_INET;
    addr.sin_port = bigEndian(port);
    if (::sendto(m_socket, static_cast<const char*>(data), size_B, 0,
            reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return false;
    }
    return true;
}

bool UDPSocket::setIpDontFragment(const bool isEnabled) {
    switch (AF_INET) { //TODO: IPv6
# if defined(IP_DONTFRAGMENT)
    case AF_INET: {
        const int32_t value = isEnabled ? 1 : 0;
        if (::setsockopt(m_socket, IPPROTO_IP, IP_DONTFRAGMENT,
                reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
            //std::cout << "setIpDontFragment: " << strerror(errno) << std::endl;
            return false;
        }
        break;
    }
# elif defined(IP_DONTFRAG)
    case AF_INET: {
        const int32_t value = isEnabled ? 1 : 0;
        if (::setsockopt(m_socket, IPPROTO_IP, IP_DONTFRAG,
                reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
            //std::cout << "setsockopt: IP_DONTFRAG: " << strerror(errno) << std::endl;
            return false;
        }
        break;
    }
# elif defined(__linux__) && defined(IP_MTU_DISCOVER)
    case AF_INET: {
        const int32_t value = isEnabled ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
        if (::setsockopt(m_socket, IPPROTO_IP, IP_MTU_DISCOVER,
                reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
            return false;
        }
        break;
    }
# else
#   error Not implemented
# endif
# if defined(IPV6_DONTFRAG)
    case AF_INET6: {
        const int32_t value = isEnabled ? 1 : 0;
        if (::setsockopt(m_socket, IPPROTO_IPV6, IPV6_DONTFRAG,
                reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
            return false;
        }
        break;
    }
# elif defined(__linux__) && defined(IPV6_MTUDISCOVER)
    case AF_INET6: {
        const int32_t value = isEnabled ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
        if (::setsockopt(m_socket, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
                reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
            return false;
        }
        break;
    }
//TODO: Is it available with GCC 4.9?
//# else
//#   error Not implemented
# endif
    default:
        return false;
    }
    return true;
}
bool UDPSocket::setReusePort(const bool isEnabled) {
#ifdef _WIN32
    //const int32_t value = isEnabled ? 1 : 0;
    //if (::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR,
    //        reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
    //    return false;
    //}
    return false; // only for raw sockets
#else
    const int32_t value = isEnabled ? 1 : 0;
    if (::setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT,
            reinterpret_cast<const char*>(&value), sizeof(value)) == -1) {
        return false;
    }
    return true;
#endif
}
bool UDPSocket::setRxBufferSize_B(const uint32_t size_B) {
    constexpr int32_t len_B = sizeof(size_B);
    if (setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char*>(&size_B), len_B) != 0) {
        return false;
    }
    return true;
}
uint32_t UDPSocket::getRxBufferSize_B() const {
    uint32_t size_B = 0;
//# if defined(_WIN32)
//    int32_t len_B = sizeof(size_B);
//# else
//    uint32_t len_B = sizeof(size_B);
//# endif
    socklen_t len_B = sizeof(size_B);
    if (getsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<char*>(&size_B), &len_B) != 0) {
        return 0;
    }
    return size_B;
}
bool UDPSocket::setTxBufferSize_B(const uint32_t size_B) {
    constexpr int32_t len_B = sizeof(size_B);
    if (setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<const char*>(&size_B), len_B) != 0) {
        return false;
    }
    return true;
}
uint32_t UDPSocket::getTxBufferSize_B() const {
    uint32_t size_B = 0;
//# if defined(_WIN32)
//    int32_t len_B = sizeof(size_B);
//# else
//    uint32_t len_B = sizeof(size_B);
//# endif
    socklen_t len_B = sizeof(size_B);
    if (getsockopt(m_socket, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<char*>(&size_B), &len_B) != 0) {
        return 0;
    }
    return size_B;
}

void UDPSocket::process(const uint32_t timeout_ms) {
    if (timeout_ms > 0) {
#     ifdef _WIN32
        WSAPOLLFD fd = {};
        fd.fd = m_socket;
        fd.events = POLLIN;
        if (WSAPoll(&fd, 1, timeout_ms) <= 0) {
            return;
        }
#     else
        struct pollfd fd = {};
        fd.fd = m_socket;
        fd.events = POLLIN;
        if (::poll(&fd, 1, timeout_ms) <= 0) {
            return;
        }
#     endif
    }
    thread_local std::array<char, UINT16_MAX> buffer;
    sockaddr_in from = {};
//# if defined(_WIN32)
//    int32_t fromLen = sizeof(from);
//# else
//    uint32_t fromLen = sizeof(from);
//# endif
    socklen_t fromLen_B = sizeof(from);

    if (onReceived == nullptr) {
        return;
    }
    while (true) {
        const int32_t received_B = ::recvfrom(
            m_socket, buffer.data(), buffer.size(), 0,
            reinterpret_cast<sockaddr*>(&from), &fromLen_B
        );
        if (received_B < 0) {
            break;
        }
        onReceived(buffer.data(), received_B, bigEndian(from.sin_port), bigEndian(from.sin_addr.s_addr));
    }
}

uint32_t UDPSocket::IPv4FromString(const char* string) {
    return bigEndian(inet_addr(string));
}

bool UDPSocket::setThreadPriority(const uintptr_t thread, const char priority) {
# ifdef _WIN32
    switch (priority) {
    case 'H': return SetThreadPriority(reinterpret_cast<HANDLE>(thread), THREAD_PRIORITY_TIME_CRITICAL);
    case 'h': return SetThreadPriority(reinterpret_cast<HANDLE>(thread), THREAD_PRIORITY_HIGHEST);
    case 'n': return SetThreadPriority(reinterpret_cast<HANDLE>(thread), THREAD_PRIORITY_NORMAL);
    case 'l': return SetThreadPriority(reinterpret_cast<HANDLE>(thread), THREAD_PRIORITY_LOWEST);
    case 'L': return SetThreadPriority(reinterpret_cast<HANDLE>(thread), THREAD_PRIORITY_IDLE);
    default: return false;
    }
# else
    const int32_t policy = SCHED_OTHER;
    const int32_t max_priority = sched_get_priority_max(policy);
    const int32_t min_priority = sched_get_priority_min(policy);
    struct sched_param param;
    switch (priority) {
    case 'H': param.sched_priority = max_priority; break;
    case 'h': param.sched_priority = ((min_priority + max_priority) / 2 + max_priority) / 2; break;
    case 'n': param.sched_priority = (min_priority + max_priority) / 2; break;
    case 'l': param.sched_priority = (min_priority + (min_priority + max_priority) / 2) / 2; break;
    case 'L': param.sched_priority = min_priority; break;
    default: return false;
    }
    return pthread_setschedparam(thread, policy, &param) == 0;
# endif
}

void UDPSocket::open() {
    close();
    m_socket = ::socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

# ifdef _WIN32
    u_long nonBlocking = 1;
    ::ioctlsocket(m_socket, static_cast<long>(FIONBIO), &nonBlocking);
# else
    int32_t status = ::fcntl(m_socket, F_GETFL);
    if (::fcntl(m_socket, F_SETFL, status | O_NONBLOCK) == -1) {
        //err() << "Failed to set file status flags: " << errno << std::endl;
    }
# endif

    // Enable broadcast by default for UDP sockets
    int32_t yes = 1;
    if (::setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST,
            reinterpret_cast<char*>(&yes), sizeof(yes)) == -1) {
        //err() << "Failed to enable broadcast on UDP socket" << std::endl;
    }
}
void UDPSocket::close() {
    if (m_socket == 0) {
        return;
    }
# ifdef _WIN32
    ::closesocket(m_socket);
# else
    ::close(m_socket);
# endif
    m_socket = 0;
}
