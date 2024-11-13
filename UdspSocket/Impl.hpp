#pragma once
#include "UdspSocket.hpp"
#include "UdpSocket.hpp"

#include <cassert>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <array>
#include <unordered_map>

//#define UDSP_TRACE_LEVEL 3

#if UDSP_TRACE_LEVEL or !defined(NDEBUG)
#   include <iostream>
#   include <iomanip>
#endif

#if UDSP_TRACE_LEVEL
#   include "../colored_cout/colored_cout.h"
#endif

#define UDSP_TRACE_LEVEL_ERROR          1
#define UDSP_TRACE_LEVEL_STATE_CHANGED  2
#define UDSP_TRACE_LEVEL_STATISTICS     3
#define UDSP_TRACE_LEVEL_MIN_RTT        4
#define UDSP_TRACE_LEVEL_TX_PACING      5
#define UDSP_TRACE_LEVEL_RX_PACKET      6
#define UDSP_TRACE_LEVEL_TX_KEEP_ALIVE  7

#pragma pack(push, 0)

inline int64_t tick_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

class SmoothModelHolt {
public:
    SmoothModelHolt() = default;
    SmoothModelHolt(const float alpha1, const float alpha2)
        : m_alpha1(alpha1), m_alpha2(alpha2) {}

    void reset(const float value = std::numeric_limits<float>::quiet_NaN()) {
        m_prevAt = value;
        m_prevBt = 0.0f;
    }
    // 0 ≤ alpha1 ≤ 1, 0 ≤ alpha2 ≤ 1
    void init(const float alpha1, const float alpha2) {
        m_alpha1 = alpha1;
        m_alpha2 = alpha2;
    }
    void update(const float value) {
        if (std::isnan(m_prevAt)) {
            reset(value);
        }
        else {
            const float At = m_alpha1 * value + (1.0f - m_alpha1) * (m_prevAt - m_prevBt);
            const float Bt = m_alpha2 * (At - m_prevAt) + (1.0f - m_alpha2) * m_prevBt;
            m_prevAt = At;
            m_prevBt = Bt;
        }
    }
    float result() const {
        return m_prevAt;
    }
private:
    float m_alpha1 = 0.05f;
    float m_alpha2 = 0.05f;
    float m_prevAt = std::numeric_limits<float>::quiet_NaN();
    float m_prevBt = 0.0f;
};


// [uint32:packedId][uint32:packetNumber][uint64:connectionId][uint16:PMTUProbeSize_B]
// [uint16:packetsLoss_prc100][uint32:RTTDelay_us][uint8:RTTRequest][uint8:RTTResponse]
// ([uint8:chunkId][uint8:streamId][uint32:packetId]...)*

enum class PacketId : uint32_t {
    TypeA       = 0x51302201,
    //TypeB     = 0x51302202,
    Disconnect  = 0x51302203,
    PMTUProbe   = 0x51302204,
};
#pragma pack(push, 1)
struct PacketHeader {
    PacketId packetId;
    uint32_t packetNumber;
    uint64_t connectionId;
    //uint32_t speedLimit_B_s;
    uint16_t PMTUProbeSize_B;
    uint16_t packetsLoss_prc100; // 0...10000
    uint32_t RTTDelay_us; // 0...71 m
    uint8_t RTTRequest;
    uint8_t RTTResponse;
};
static_assert(sizeof(PacketHeader) == 26, "");
#pragma pack(pop)

#pragma pack(push, 1)
// [uint8:chunkId][uint8:streamId][uint32:packetId]...
union ChunkId {
    enum class Type : uint8_t {
        // ...[uint?:packetSize_B][bytes:data]
        // ... - isAcknowledge == true
        SmallPacket,
        // ...[uint?:packetSize_B][uint32:crc32]
        BeginOfPacket,
        // ...[uint?:offset_B][uint?:pieceSize_B][bytes:data]
        PieceOfPacket,
        // ...[uint?:repeatOffset_B][uint?:repeatSize_B]
        RepeatInfo,
    };
    enum class Bits : uint8_t {
        u8,
        u16,
        u32,
        u64,
    };
    static Bits getNumberOfBits(const uint64_t value) {
        const uint8_t b63_32 = !!(value & 0xFFFFFFFF00000000); // x3, y 11
        const uint8_t b31_16 = !!(value & 0xFFFF0000); // x2, y 10
        const uint8_t b15_8 = !!(value & 0xFF00); // x1, y 01
        // x3 x2 x1 y2 y1
        // 0  0  0  0  0
        // 0  0  1  0  1
        // 0  1  0  1  0
        // 0  1  1  1  0
        // 1  0  0  1  1
        // 1  0  1  1  1
        // 1  1  0  1  1
        // 1  1  1  1  1
        return static_cast<Bits>(
            ((b63_32 | b31_16) << 1)
            | (b15_8 >> b31_16) | b63_32
        );
    }
    static constexpr uint32_t getNumberOfBytes(const Bits bits) {
        return 1 << static_cast<uint32_t>(bits);
    }
    struct {
        Type chunkType : 2;
        uint8_t _ : 6;
    } type;
    struct {
        Type chunkType : 2;
        uint8_t _ : 1;
        uint8_t isAcknowledge : 1;
        uint8_t isReliable : 1;
        uint8_t isFront : 1;
        Bits packetSizeBits : 2; // 1 is enough
    } smallPacket;
    struct {
        Type chunkType : 2;
        uint8_t _ : 2;
        uint8_t isReliable : 1;
        uint8_t isFront : 1;
        Bits packetSizeBits : 2;
    } beginOfPacket;
    struct {
        Type chunkType : 2;
        //uint8_t _ : 1;
        uint8_t isReliable : 1;
        uint8_t isFront : 1;
        Bits offsetBits : 2;
        Bits pieceSizeBits : 2; // 1 is enough
    } pieceOfPacket;
    struct {
        Type chunkType : 2;
        uint8_t _ : 2;
        Bits repeatOffsetBits : 2;
        Bits repeatSizeBits : 2;
        //Bits continueOffsetBits : 2;
    } repeatInfo;
    uint8_t total = 0;
    ChunkId() { total = 0; }
};
static_assert(sizeof(ChunkId) == sizeof(uint8_t), "");
#pragma pack(pop)

enum class Priority : uint8_t {
    Realtime,
    High,
    Medium,
    Low,
    _count_,
    None,
};
struct TxPacket {
    std::vector<uint8_t> copy;
    const uint8_t* pointer = nullptr;
    uint64_t size_B = 0;
    uint64_t offset_B = 0;
    int64_t timeout_us = 0;
    int64_t begin_us = 0;
    uint64_t datagramId = UINT64_MAX; // > uint32_t packetsTxCount
    uintptr_t context = 0;
    uint32_t id = 0;
    //Priority priority = Priority::None;
    bool isReliable = false;
    bool isAcknowledged = false;
    bool isStarted = false;
};
struct TxStream {
    int64_t fifoReturnTime_us = 0;
    std::deque<TxPacket> fifo;
    std::deque<uint32_t> fifoShadow;
    size_t packetFifoIdx = 0;
    //int64_t timeout_ms = 0;
    uint32_t nextPacketId = 0;
    uint8_t id = 0;
    Priority priority = Priority::Medium;
    bool isNew = true;
    bool isReliable = true;
};
struct TxStreams {
    std::unordered_map<uint8_t, TxStream> map;
    decltype(map)::iterator streamIt = map.end();

    struct PriorityMeta {
        std::vector<TxStream*> vec;
        size_t vecIdx = 0;
        TxPacket* packet = nullptr;
        uint32_t countSent_B = 0;
    };
    std::array<PriorityMeta, size_t(Priority::_count_)> vec;

    bool isStreamsChanged = true;

    //bool empty() const {
    //    return (countRealtimeInFifo | countHighInFifo | countMediumInFifo | countLowInFifo) == 0;
    //}
    //void next() {
    //    if (isStreamsChanged) {
    //        isStreamsChanged = false;
    //        streamIt = map.begin();
    //    }
    //    else {
    //        if (++streamIt == map.end()) {
    //            streamIt = map.begin();
    //        }
    //    }
    //}
    void update() {
        if (not isStreamsChanged) {
            return;
        }
        for (auto& it : vec) {
            it.vec.clear();
        }
        for (auto& it : map) {
            vec[size_t(it.second.priority)].vec.push_back(&it.second);
        }
        isStreamsChanged = false;
    }
};

struct RxPacket {
    std::vector<uint8_t> copy;
    //const uint8_t* pointer = nullptr;
    uint64_t size_B = 0;
    uint64_t offset_B = 0;
    uint64_t sizeRepeat_B = 0; // unimplemented
    uint64_t offsetRepeat_B = UINT64_MAX;
    //uint64_t offsetContinue_B = UINT64_MAX;
    int64_t timeout_us = 0; // useful for unreliable streams
    uint64_t datagramId = UINT64_MAX; // > uint32_t packetsTxCount
    uintptr_t context = 0;
    uint32_t crc32 = 0;
    uint32_t id = 0;
    bool isReliable = false;
    bool isReceived = false;
    //bool isAcknowledged = true;

    //  +++++--------------++++++-----------
    // [    [^ sizeRepeat_B]     ^    size_B]
    //       |                   |
    //       offsetRepeat_B      offset_B
    bool needToSendRepeatInfo() const {
        return isReliable and offsetRepeat_B != UINT64_MAX;
    }
};
struct RxStream {
//private:
    std::deque<RxPacket> fifo;
    std::deque<uint32_t> fifoShadow;
public:
    int64_t timeout_us = 0;
    size_t packetIdx = SIZE_MAX;
    //uint32_t frontUnfinishedPacketId = UINT32_MAX;
    uint32_t frontPacketId = 0;
    uint32_t debugPacketId = UINT32_MAX;
    //uint32_t lastReceivedPacketId = UINT32_MAX;
    uint8_t id = 0;
    bool isNew = true;
    bool isReliable = false;

    void updateFrontPacketId(const bool isFront, const uint32_t packetId) {
        if (not fifoShadow.empty()) {
            if (frontPacketId < fifoShadow.front()) {
                frontPacketId = fifoShadow.front();
            }
            else if (fifoShadow.front() < UINT32_MAX / 4 and UINT32_MAX / 4 * 3 < frontPacketId) {
                frontPacketId = fifoShadow.front();
            }
        }
        if (not isFront) {
            return;
        }
        if (frontPacketId < packetId) {
            frontPacketId = packetId;
        }
        else if (packetId < UINT32_MAX / 4 and UINT32_MAX / 4 * 3 < frontPacketId) {
            frontPacketId = packetId;
        }
        assert(frontPacketId >= packetId); //TODO: Debug
    }

    bool isReceivedBefore(const uint32_t packetId) const {
        // packetId=1%  = frontPacketId=1%   diff=0%   false
        // packetId=1%  < frontPacketId=25%  diff=24%  true
        // packetId=1%  < frontPacketId=50%  diff=49%  false
        // packetId=1%  < frontPacketId=75%  diff=74%  false
        // packetId=1%  < frontPacketId=99%  diff=98%  false
        // packetId=45% < frontPacketId=55%  diff=10%  true
        // packetId=99% > frontPacketId=1%   diff=98%  true
        // packetId=75% > frontPacketId=1%   diff=74%  true
        // packetId=50% > frontPacketId=1%   diff=49%  false
        // packetId=25% > frontPacketId=1%   diff=24%  false
        // packetId=55% > frontPacketId=45%  diff=10%  false
        if (packetId < frontPacketId) {
            if (frontPacketId - packetId <= UINT32_MAX / 4) {
                return true;
            }
            else {
                return false;
            }
        }
        else {
            if (packetId - frontPacketId >= UINT32_MAX / 4 * 3) {
                return true;
            }
            else {
                return false; // also if equal
            }
        }
    }

    RxPacket* emplace(const int64_t now_us, const uint32_t packetId = 0) {
        // new=packetId
        if (fifoShadow.empty()) {
            fifo.emplace_back();
            auto& packet = fifo.back();
            packet.id = packetId;
            packet.isReliable = isReliable;
            packet.timeout_us = now_us + 50 * 1000;
            fifoShadow.emplace_back(packetId);
            return &packet;
        }
        // ... old-1 old=packetId
        //if (fifoShadow.back() == packetId) {
        //    return fifo.back();
        //}
        // ... old-1 old new+1 new+2 ... new+n=packetId
        while (fifoShadow.back() < packetId) {
            const uint32_t id = fifoShadow.back();
            fifo.emplace_back();
            auto& packet = fifo.back();
            packet.id = id + 1;
            packet.isReliable = isReliable;
            packet.timeout_us = now_us + 50 * 1000;
            fifoShadow.emplace_back(id + 1);
            if (fifoShadow.back() == packetId) {
                return &packet;
            }
        }
        // ... old=253 old=254 new=255 new=0 new=1 new=2=packetId
        if (UINT32_MAX / 4 * 3 <= fifoShadow.back() and packetId < UINT32_MAX / 4
                and fifoShadow.back() - packetId > UINT32_MAX / 4) {
            for (uint32_t id = fifoShadow.back(); 0 < id and id <= UINT32_MAX; ++id) {
                fifo.emplace_back();
                auto& packet = fifo.back();
                packet.id = id;
                packet.isReliable = isReliable;
                packet.timeout_us = now_us + 50 * 1000;
                fifoShadow.emplace_back(id);
            }
            for (uint32_t id = 0; id <= packetId; ++id) {
                fifo.emplace_back();
                auto& packet = fifo.back();
                packet.id = id;
                packet.isReliable = isReliable;
                packet.timeout_us = now_us + 50 * 1000;
                fifoShadow.emplace_back(id);
            }
            return &fifo.back();
        }
        // ... old-2 old-1 old=packetId old+1 old+2 ...
        //for (intptr_t i = fifoShadow.size() - 1; i >= 0; --i) {
        //    if (fifoShadow[i] == packetId) {
        //        return fifo[i];
        //    }
        //}
        //assert(false);
        //return fifo.front();
        return nullptr;
    }
    RxPacket& get(const int64_t now_us, const uint32_t packetId = 0) {
        if (frontPacketId < packetId) {
            emplace(now_us, frontPacketId);
        }
        else if (packetId < UINT32_MAX / 4 and UINT32_MAX / 4 * 3 < frontPacketId) {
            emplace(now_us, frontPacketId);
        }
        RxPacket* packet = emplace(now_us, packetId);
        if (packet != nullptr) {
            return *packet;
        }
        // ... old-1 old=packetId
        if (fifoShadow.back() == packetId) {
            return fifo.back();
        }
        // ... old-2 old-1 old=packetId old+1 old+2 ...
        for (intptr_t i = fifoShadow.size() - 1; i >= 0; --i) {
            if (fifoShadow[i] == packetId) {
                return fifo[i];
            }
        }
        assert(false);
        return fifo.front();
    }
    RxPacket& get2(const int64_t now_us, const uint32_t packetId = 0) {
        // new=packetId
        if (fifoShadow.empty()) {
            fifo.emplace_back();
            auto& packet = fifo.back();
            packet.id = packetId;
            packet.isReliable = isReliable;
            packet.timeout_us = now_us + 50 * 1000;
            fifoShadow.emplace_back(packetId);
            return packet;
        }
        // ... old-1 old=packetId
        if (fifoShadow.back() == packetId) {
            return fifo.back();
        }
        // ... old-1 old new+1 new+2 ... new+n=packetId
        while (fifoShadow.back() < packetId) {
            const uint32_t id = fifoShadow.back();
            fifo.emplace_back();
            auto& packet = fifo.back();
            packet.id = id + 1;
            packet.isReliable = isReliable;
            packet.timeout_us = now_us + 50 * 1000;
            fifoShadow.emplace_back(id + 1);
            if (fifoShadow.back() == packetId) {
                return packet;
            }
        }
        // ... old=253 old=254 new=255 new=0 new=1 new=2=packetId
        if (UINT32_MAX / 4 * 3 <= fifoShadow.back() and packetId < UINT32_MAX / 4
                and fifoShadow.back() - packetId > UINT32_MAX / 4) {
            for (uint32_t id = fifoShadow.back(); 0 < id and id <= UINT32_MAX; ++id) {
                fifo.emplace_back();
                auto& packet = fifo.back();
                packet.id = id;
                packet.isReliable = isReliable;
                packet.timeout_us = now_us + 50 * 1000;
                fifoShadow.emplace_back(id);
            }
            for (uint32_t id = 0; id <= packetId; ++id) {
                fifo.emplace_back();
                auto& packet = fifo.back();
                packet.id = id;
                packet.isReliable = isReliable;
                packet.timeout_us = now_us + 50 * 1000;
                fifoShadow.emplace_back(id);
            }
            return fifo.back();
        }
        // ... old-2 old-1 old=packetId old+1 old+2 ...
        for (intptr_t i = fifoShadow.size() - 1; i >= 0; --i) {
            if (fifoShadow[i] == packetId) {
                return fifo[i];
            }
        }
        assert(false);
        return fifo.front();
    }
    RxPacket& next() {
        if (fifo.empty()) {
            assert(false);
            return fifo.front();
        }
        if (++packetIdx >= fifo.size()) {
            packetIdx = 0;
        }
        return fifo[packetIdx];
    }
};
struct RxStreams {
    std::deque<std::pair<uint8_t, uint32_t>> acks; // streamId, packetId
    std::unordered_map<uint8_t, RxStream> map;
    decltype(map)::iterator streamIt = map.end();
    bool isStreamsChanged = true;

    void next() {
        if (isStreamsChanged) {
            isStreamsChanged = false;
            streamIt = map.begin();
        }
        else {
            if (++streamIt == map.end()) {
                streamIt = map.begin();
            }
        }
    }
};

struct UDSPSocket::Connection {
    Connection(UDSPSocket::Impl* impl_) : impl(impl_) {}
    UDSPSocket::Impl* impl = nullptr;

    //uint64_t CCId = 0; // Client Connection Identificator
    //uint64_t SCId = 0; // Server Connection Identificator
    uint64_t connectionId = 0;

    uint32_t IPv4 = 0;
    uint16_t port = 0;
    //bool isConnected = false;

    // Path Maximum Transmission Unit  (MSS (Maximum Segment Size))
    uint32_t PMTU_B = 0; // s_basePMTU_IPv4_B;
    uint32_t PMTUProbeSize_B = 0;
    uint32_t PMTUProbeCount = 0;
    uint32_t PMTUProbeResponse_B = 0;
    int32_t PMTUProbeStep_B = 0;
    int64_t nextPMTUProbe_us = 0;

    uint32_t txPacketsCount = 0;
    uint32_t txCount_B = 0;
    uint32_t txCount_B_s = 0;
    uint32_t txSpeed_B_s = 0;

    uint32_t rxPacketNumberReceived = 0;
    uint32_t rxPacketNumberProcessed = 0;
    uint32_t rxPacketNumberInWindowProcessed = 0;
    uint32_t txPacketsWindowIdx = 0;
    uint32_t rxPacketsWindowIdx = 0;
    uint32_t rxPacketsCountReceived = 0;
    uint32_t rxPacketsCountInWindow = 0;
    uint32_t rxCount_B_s = 0;
    uint32_t rxSpeed_B_s = 0;

    uint32_t txPacketsLossSum_prc100 = 0;
    uint32_t txPacketsLossSum_count = 0;
    //uint16_t txPacketsLossInWindow_prc100 = 0;
    uint16_t rxPacketsLossInWindow_prc100 = 0;
    float txPacketsLoss_prc = 0;
    float rxPacketsLoss_prc = 0;

    bool isRTTProbing = false;
    uint8_t RTTCount = 0;
    uint8_t RTTRequest = 0;
    uint8_t RTTResponse = 0;
    uint32_t RTT_us = 0;
    uint32_t minRTT_us = UINT32_MAX;

    uint32_t nextMinRTT_us = UINT32_MAX;
    //uint32_t avgRTT_us = 0;
    //uint32_t avgRTTCount = 0;
    SmoothModelHolt RTTSmooth_us;

    int64_t RTTRequestTick_us = 0;
    int64_t RTTResponseTick_us = 0;

    int64_t next1Hz_us = 0;
    int64_t nextRTTProbe_us = 0;

    int64_t lastPacketTick_us = INT64_MAX;
    bool isConnected() const { return lastPacketTick_us != INT64_MAX; }
    int64_t nextKeepAliveTick_us = 0;
    int64_t nextEraseTick_us = INT64_MAX;

    int64_t prevTick_us = 0;
    uint32_t txLimit_B_s = 0; // s_basePMTU_IPv4_B * s_packetsWindowSize * 8;
    uint32_t desiredTxLimit_B_s = (1 * 1000 * 1000 * 1000) / 8; // 1 Gbps

# if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
    uint32_t debugMaxRxQueue = 0;
    uint32_t debugMaxTxQueue = 0;
# endif // UDSP_TRACE_LEVEL

    //int64_t nextProcess_us = 0;
    bool tooBigLoss = false;
    bool tooBigRTT = false;
    bool isDisconnectRequested = false;

    void partialReset();

    std::function<void(
        uintptr_t context, Connection* connection, const void* data, uint64_t size_B,
        uint8_t txStreamId, char status
    )> onDelivered;
    std::function<uintptr_t(
        uintptr_t context, Connection* connection, const void* data,
        uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status
    )> onReceived;

    std::vector<uint8_t> txBuffer;
    TxStreams txStreams;
    RxStreams rxStreams;
    std::deque<std::function<void()>> commands;
    void doCommands() {
        while (not commands.empty()) {
            commands.front()(); //TODO: segfault
            commands.pop_front();
        }
    }

    // Omax(log n)
    static size_t findPacketIdx(const std::deque<uint32_t>& fifo, const uint32_t packetId);

    bool writePacket(const bool isTestBandwidthEnabled, const bool forceSend);
    void writePMTUProbe();
    void writeDisconnect();

    void nextDatagram();
    size_t writeMetaChunk(const int64_t now_us, uint8_t* buffer, const uint32_t available_B);
    // Omin(streams) Omax(packets)
    size_t writeDataChunk(const int64_t now_us, uint8_t* buffer, const uint32_t available_B);
    size_t writeChunk(const int64_t now_us, uint8_t* buffer, const uint32_t available_B);
    size_t readChunk(const int64_t now_us, const uint8_t* buffer, const uint32_t available_B);

    void processRxFifo(RxStream& stream, const int64_t now_us);
    void processTxFifo(TxStream& stream, const int64_t now_us);

    void onDisconnected();
}; // struct UDSPSocket::Connection

// DPLPMTUD RFC8899
//enum class PMTUDPhase {
//    Base,
//    Search,
//    SearchComplete,
//    Error,
//};

struct UDSPSocket::Impl {
    UDPSocket udpSocket;

    std::unordered_map<uint64_t, std::unique_ptr<Connection>> connections; // key=connectionId

    std::thread thread;
    mutable std::mutex mutex;

    std::function<void(Connection* connection)> onConnected;
    std::function<void(Connection* connection, char reason)> onDisconnected;

    uint32_t rxPacketBufferSizeThreshold_B = 0;

    int64_t spent_us = 0;
    uint32_t TPS = 0;

    // NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE
    uint16_t localPort = 0;
    bool isRunning = false;
    bool isServer = false;
    bool isTestBandwidthEnabled = false;

    Impl();
    ~Impl();
    void stop();
    void testStreams();

    Connection& clientConnection();
    Connection& serverConnection(const uint64_t connectionId);

    bool initConnection(Connection& c, const uint16_t port, const uint32_t IPv4);

    bool connect(const uint16_t port, const uint32_t IPv4);
    bool disconnect();
    bool isConnected();
    bool listen(const uint16_t port);

    void setTxStreamPriority_ts(Connection* connection, uint8_t txStreamId, char priority);
    //char getTxStreamPriority_ts(Connection* connection, uint8_t txStreamId) const;
    bool send_ts(uintptr_t context, Connection* connection, const void* data, uint64_t size_B,
        bool copy, uint8_t txStreamId, uint32_t timeout_ms, int64_t now_us);

    void process();
    void process_ts();
    void processConnection(Connection& c, const int64_t now_us);

    void onUdpReceived(void* data, uint32_t size_B, uint16_t port, uint32_t IPv4);
};
