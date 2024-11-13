#include "Impl.hpp"


namespace {
    // The maximum time to receive an acknowledgment to a probe packet.
    // This value MUST NOT be smaller than 1 second and SHOULD be larger than 15 seconds.
    constexpr int64_t g_PMTUProbePeriod_us = 1 * 1000 * 1000;
    // After this period, a sender reenters the Search Phase.
    constexpr int64_t g_PMTURaisePeriod_us = 10 * 60 * 1000 * 1000;
    constexpr uint32_t g_PMTUMaxProbes = 3;
    constexpr uint32_t g_minPMTU_IPv4_B = 68;
    constexpr uint32_t g_minPMTU_IPv6_B = 1280;

    constexpr uint32_t g_headerSize_IPv4_B = 20 + 8;
    constexpr uint32_t g_headerSize_IPv6_B = 40 + 8;
    constexpr uint32_t g_maxPMTU_B = 16384;
    constexpr uint32_t g_basePMTU_IPv4_B = 1200 - g_headerSize_IPv4_B;
    constexpr uint32_t g_basePMTU_IPv6_B = 1280 - g_headerSize_IPv6_B;

    constexpr uint8_t g_packetsWindowSize = 1 << 3; // 1 << 5;
    constexpr uint8_t g_RTTNewProbeLimit = 1 << 3; // 1 << 4;
    constexpr uint8_t g_RTTBadProbeLimit = 1 << 4; // 1 << 5;
    constexpr uint32_t g_txLimitDefault_B_s = 1 << 18;

    constexpr int64_t g_keepAlivePeriod_us = 500 * 1000;
    constexpr int64_t g_connectionTimeout_us = 4 * 1000 * 1000;
    constexpr int64_t g_RTTProbePeriod_us = 10 * 1000 * 1000;

    constexpr uint32_t g_kAllowedRTT = 5;
    //constexpr uint32_t g_kIncOnGoodRTT = 1 << 8; // 1 << 7;
    //constexpr uint32_t g_kDecOnBadRTT = 1 << 7; // 5 - too big, 7 - too small
    //constexpr uint32_t g_kIncOnSmallLoss = 1 << 8; // 9, 10
    //constexpr uint32_t g_kDecOnBigLoss = 1 << 7; // 1 - too big, 5 - too small
    constexpr uint32_t g_kIncStep = 1 << 8;
    constexpr uint32_t g_kDecStep = 1 << 7;

    constexpr bool g_isRTTCongestionEnabled = true;
    constexpr bool g_isLossCongestionEnabled = true;

    thread_local int64_t prev_us = 0;
} // namespace


void UDSPSocket::Connection::partialReset() {
    nextKeepAliveTick_us = 0;

    PMTU_B = g_basePMTU_IPv4_B;
    nextPMTUProbe_us = 0;
    PMTUProbeStep_B = 0;
    PMTUProbeResponse_B = 0;

    //packetsTxStepCount = 0;

    rxPacketsLossInWindow_prc100 = 0;

    RTTRequest = 0;
    RTTResponse = 0;
    RTT_us = 1000;
    nextMinRTT_us = UINT32_MAX;
    minRTT_us = UINT32_MAX;
    RTTRequestTick_us = 0;
    RTTResponseTick_us = 0;
    RTTSmooth_us.init(0.05f, 0.05f);

    next1Hz_us = 0;

    prevTick_us = 0;
    txLimit_B_s = g_txLimitDefault_B_s;
}

size_t UDSPSocket::Connection::findPacketIdx(
        const std::deque<uint32_t>& fifo, const uint32_t packetId) {
    if (fifo.empty()) {
        return SIZE_MAX;
    }
    size_t leftIdx = 0;
    size_t rightIdx = fifo.size() - 1;
    while (leftIdx <= rightIdx) {
        size_t midIdx = leftIdx + (rightIdx - leftIdx) / 2;
        if (fifo[midIdx] == packetId) {
            return midIdx;
        }
        if (fifo[leftIdx] <= fifo[midIdx]) {
            if (fifo[leftIdx] <= packetId and packetId < fifo[midIdx]) {
                rightIdx = midIdx - 1;
            }
            else {
                leftIdx = midIdx + 1;
            }
        }
        else {
            if (fifo[midIdx] < packetId and packetId <= fifo[rightIdx]) {
                leftIdx = midIdx + 1;
            }
            else {
                rightIdx = midIdx - 1;
            }
        }
    }
    return SIZE_MAX;
}

bool UDSPSocket::Connection::writePacket(
        const bool isTestBandwidthEnabled, const bool forceSend) {
    txBuffer.resize(PMTU_B);
    //std::fill(txBuffer.begin(), txBuffer.end(), 0);
    const int64_t now_us = tick_us();

    nextDatagram();

    size_t offset_B = sizeof(PacketHeader);
    while (true) {
        const size_t available_B = txBuffer.size() - offset_B;
        if (available_B == 0) {
            break;
        }
        const size_t written_B = writeMetaChunk(now_us, &txBuffer[offset_B], available_B);
        assert(written_B <= available_B);
        if (written_B <= 0) {
            break;
        }
        offset_B += written_B;
    }
    while (true) {
        const size_t available_B = txBuffer.size() - offset_B;
        if (available_B == 0) {
            break;
        }
        const size_t written_B = writeDataChunk(now_us, &txBuffer[offset_B], available_B);
        assert(written_B <= available_B);
        if (written_B <= 0) {
            break;
        }
        offset_B += written_B;
    }
    txBuffer.resize(offset_B);
    do {
        if (isTestBandwidthEnabled) {
            txBuffer.resize(PMTU_B);
            break;
        }
        if (forceSend) {
            break;
        }
        if (offset_B == sizeof(PacketHeader)) {
            --txPacketsCount;
            return false;
        }
    } while (false);

    auto& header = *reinterpret_cast<PacketHeader*>(&txBuffer[0]);
    header.packetId = PacketId::TypeA;
    header.packetNumber = txPacketsCount;
    header.connectionId = connectionId;

    header.packetsLoss_prc100 = rxPacketsLossInWindow_prc100;

    if (RTTRequestTick_us == 0) {
        RTTRequestTick_us = now_us;
        ++RTTRequest;
    }
    header.RTTRequest = RTTRequest;
    header.RTTResponse = RTTResponse;

    if (RTTResponseTick_us != 0) {
        header.RTTDelay_us = static_cast<uint32_t>(now_us - RTTResponseTick_us);
    }
    else {
        header.RTTDelay_us = 0;
    }

    header.PMTUProbeSize_B = PMTUProbeResponse_B;
    return true;
}
void UDSPSocket::Connection::writePMTUProbe() {
    PMTUProbeSize_B = PMTU_B + PMTUProbeStep_B;
    txBuffer.resize(PMTUProbeSize_B);
    std::fill(txBuffer.begin(), txBuffer.end(), 0);
    auto& header = *reinterpret_cast<PacketHeader*>(&txBuffer[0]);

    header.packetId = PacketId::PMTUProbe;
    header.packetNumber = 0; // txPacketsCount; // Don't increment here
    header.connectionId = connectionId;

    header.packetsLoss_prc100 = rxPacketsLossInWindow_prc100;

    header.RTTRequest = RTTRequest;
    header.RTTResponse = RTTResponse;
    header.PMTUProbeSize_B = PMTUProbeResponse_B;
}
void UDSPSocket::Connection::writeDisconnect() {
    txBuffer.resize(sizeof(PacketHeader));
    auto& header = *reinterpret_cast<PacketHeader*>(&txBuffer[0]);

    header.packetId = PacketId::Disconnect;
}

void UDSPSocket::Impl::process_ts() {
    const int64_t now_us = tick_us();
    std::lock_guard<std::mutex> lock(mutex);

    for (auto connectionIt = connections.begin(); connectionIt != connections.end();) {
        auto& c = *connectionIt->second;

        c.doCommands();

        if (c.isDisconnectRequested) {
            c.writeDisconnect();
            udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);

            c.onDisconnected();
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATE_CHANGED
            std::cout << CLR_MAGENTA "Disconnected " << c.connectionId
                << " (closed)" CLR_RESET << std::endl;
#         endif // UDSP_TRACE_LEVEL
            if (onDisconnected != nullptr) {
                onDisconnected(&c, 'c');
            }
            auto toRemove = connectionIt++;
            connections.erase(toRemove);
            continue;
        }
        else if (c.nextEraseTick_us == INT64_MAX) {
            ++connectionIt;
            if (c.lastPacketTick_us <= now_us - g_connectionTimeout_us) {
                c.nextEraseTick_us = now_us + 2 * 1000 * 1000;
                c.lastPacketTick_us = INT64_MAX;
                c.onDisconnected();
#             if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATE_CHANGED
                std::cout << CLR_MAGENTA "Disconnected " << c.connectionId
                    << " (timeout)" CLR_RESET << std::endl;
#             endif // UDSP_TRACE_LEVEL
                if (onDisconnected != nullptr) {
                    onDisconnected(&c, 't');
                }
                continue;
            }
            // else normal work
        }
        else if (c.nextEraseTick_us <= now_us) {
            if (isServer) {
                auto toRemove = connectionIt++;
                connections.erase(toRemove);
            }
            else {
                ++connectionIt;
                c.partialReset();
                c.nextEraseTick_us = INT64_MAX;
                c.lastPacketTick_us = INT64_MAX;
            }
            continue;
        }
        else { // waiting for erase
            ++connectionIt;
            continue;
        }
    }
}
void UDSPSocket::Impl::process() {
    while (isRunning) {
        ++TPS;
        udpSocket.process(10);
        //udpSocket.process(1);

        process_ts();

        for (auto& it : connections) {
            auto& c = *it.second;
            const int64_t now_us = tick_us();

            if (c.lastPacketTick_us != INT64_MAX) {
                processConnection(c, now_us);
            }

            if (c.nextKeepAliveTick_us <= now_us) {
                //c.writeHeader().packetId = PacketId::TypeA;
                c.writePacket(false, true);
                udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);
                c.nextKeepAliveTick_us = now_us + g_keepAlivePeriod_us;
                const uint32_t sent_B = c.txBuffer.size() + g_headerSize_IPv4_B;
                c.txCount_B += sent_B;
                c.txCount_B_s += sent_B;

#             if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_TX_KEEP_ALIVE
                std::cout << CLR_YELLOW << __LINE__ << " send id=" << c.txPacketsCount
                    << " diff=" << now_us - prev_us << CLR_RESET "\n";
                prev_us = now_us;
#             endif // UDSP_TRACE_LEVEL
            }

            spent_us += tick_us() - now_us;
        }
    }
}
void UDSPSocket::Impl::processConnection(Connection& c, const int64_t now_us) {
    //if (now_us < c.nextProcess_us) {
    //    return;
    //}
    //c.nextProcess_us = now_us + 1000000 / 4000;
    //++TPS;

    if (c.lastPacketTick_us <= now_us - g_connectionTimeout_us / 2) {
        if (c.PMTU_B != g_basePMTU_IPv4_B) {
            c.partialReset();
        }
    }

    if (c.nextPMTUProbe_us <= now_us) {
        if (c.PMTUProbeStep_B == 0) {
            c.PMTUProbeStep_B = 128;
        }
        else if (++c.PMTUProbeCount >= g_PMTUMaxProbes) {
            c.PMTUProbeCount = 0;
            c.PMTUProbeStep_B /= 2;
        }

        if (std::abs(c.PMTUProbeStep_B) < 4) {
            c.PMTUProbeStep_B = 0;
            c.nextPMTUProbe_us = now_us + g_PMTURaisePeriod_us;
            // SearchComplete Phase
            //std::cout << CLR_MAGENTA "SearchComplete Phase" CLR_RESET << std::endl;
        }
        else {
            c.nextPMTUProbe_us = now_us + g_PMTUProbePeriod_us;
            // Search Phase
            c.writePMTUProbe();
            udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);
            //std::cout << CLR_MAGENTA "Search Phase " << c.PMTU_B << " + "
            //    << c.PMTUProbeStep_B << CLR_RESET << std::endl;
        }
    }

    uint32_t maxPortion_B = 0;
    if (c.prevTick_us == 0) {
        c.prevTick_us = now_us;
    }
    else {
        const int64_t diff_us = now_us - c.prevTick_us;
        // lim_B  1000000 us
        // max_B    diff_us
        maxPortion_B = (diff_us * c.txLimit_B_s) / 1000000;
        c.prevTick_us = now_us;

        if (c.isRTTProbing) {
            constexpr uint32_t d = 1 << 8;
            constexpr uint32_t m = d * 0.65f; // 65 %
            maxPortion_B = (maxPortion_B * m) / d;
        }
    }

    uint32_t txCount_B = c.txCount_B;
    while (txCount_B < maxPortion_B) {
        if (not c.writePacket(isTestBandwidthEnabled, false)) {
            break;
        }
        udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);
        txCount_B += uint32_t(c.txBuffer.size()) + g_headerSize_IPv4_B;

#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_TX_PACING
        std::cout << CLR_YELLOW << __LINE__ << " send id=" << c.txPacketsCount
            << " size=" << c.txBuffer.size() << " diff=" << now_us - prev_us << CLR_RESET "\n";
        prev_us = now_us;
#     endif // UDSP_TRACE_LEVEL
    }
    if (txCount_B > 0) {
        c.txCount_B = 0;
        c.txCount_B_s += txCount_B;
        c.nextKeepAliveTick_us = now_us + g_keepAlivePeriod_us;
    }

    if (c.next1Hz_us <= now_us) {
        //TODO: diff != 1 s
        const int64_t diff_us = now_us - c.next1Hz_us;
        c.next1Hz_us += 1 * 1000 * 1000;
        if (c.next1Hz_us <= now_us) {
            c.next1Hz_us = now_us + 1 * 1000 * 1000;
        }

        c.txSpeed_B_s = c.txCount_B_s;
        c.txCount_B_s = 0;
        c.txLimit_B_s = std::min(
            c.txLimit_B_s,
            std::max(
                g_txLimitDefault_B_s,
                c.txSpeed_B_s * 4
            )
        );

        c.rxSpeed_B_s = c.rxCount_B_s;
        c.rxCount_B_s = 0;

        if (c.txPacketsLossSum_count == 0) {
            c.txPacketsLoss_prc = 0.0f;
        }
        else {
            c.txPacketsLoss_prc = (c.txPacketsLossSum_prc100 / c.txPacketsLossSum_count) * 0.01f;
        }
        c.txPacketsLossSum_prc100 = 0;
        c.txPacketsLossSum_count = 0;

        uint32_t rxPacketsCountExpected = 0;
        // processed=990, received=1010 --> expected=20
        if (c.rxPacketNumberProcessed < c.rxPacketNumberReceived) {
            rxPacketsCountExpected = c.rxPacketNumberReceived - c.rxPacketNumberProcessed;
        }
        // processed=1010, received=990 --> expected=0
        else if (c.rxPacketNumberProcessed - c.rxPacketNumberReceived < UINT32_MAX / 2) {
            rxPacketsCountExpected = 0;
        }
        // processed=UINT32_MAX-10, received=10 --> expected=20
        else {
            rxPacketsCountExpected = UINT32_MAX
                - c.rxPacketNumberProcessed + c.rxPacketNumberReceived;
        }
        c.rxPacketNumberProcessed = c.rxPacketNumberReceived;

        if (rxPacketsCountExpected == 0) {
            c.rxPacketsLoss_prc = 0.0f;
        }
        else {
            c.rxPacketsLoss_prc = 100.0f
                - (float(c.rxPacketsCountReceived) * 100.0f) / float(rxPacketsCountExpected);
            //c.rxPacketsLoss_prc = std::max(c.rxPacketsLoss_prc, 0.0f);
        }
        c.rxPacketsCountReceived = 0;

#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
        //TODO: + connectionId
        std::cout << std::fixed << CLR_DARKGRAY "Loss,%=R:" CLR_WHITE << std::setprecision(2);
        if (c.rxPacketsLoss_prc > 1.0f) {
            std::cout << CLR_RED << c.rxPacketsLoss_prc;
        }
        else {
            std::cout << c.rxPacketsLoss_prc;
        }
        std::cout << CLR_DARKGRAY ",T:" CLR_WHITE;
        if (c.txPacketsLoss_prc > 1.0f) {
            std::cout << CLR_RED << c.txPacketsLoss_prc;
        }
        else {
            std::cout << c.txPacketsLoss_prc;
        }
        std::cout << CLR_DARKGRAY " RTT,ms=" CLR_WHITE << std::setprecision(1);
        if (c.RTT_us > 10000) {
            std::cout << CLR_RED << c.RTT_us / 100 * 0.1f;
        }
        else {
            std::cout << c.RTT_us / 100 * 0.1f;
        }
        //std::cout << CLR_DARKGRAY "," CLR_WHITE;
        //if (c.avgRTT_us > 10000) {
        //    std::cout << CLR_RED << c.avgRTT_us / 1000;
        //}
        //else {
        //    std::cout << c.avgRTT_us / 1000;
        //}
        std::cout << CLR_DARKGRAY "," CLR_WHITE;
        if (c.minRTT_us > 10000.0f) {
            std::cout << CLR_RED << c.minRTT_us / 100 * 0.1f;
        }
        else {
            std::cout << c.minRTT_us / 100 * 0.1f;
        }
        std::cout << CLR_DARKGRAY " TPS=" CLR_WHITE << TPS
            << CLR_DARKGRAY " Speed,MB/s=R:" CLR_WHITE << std::setprecision(2);
        if (c.rxSpeed_B_s > 2 * 1000 * 1000) {
            std::cout << CLR_GREEN << c.rxSpeed_B_s / (1000.0f * 1000.0f);
        }
        else {
            std::cout << c.rxSpeed_B_s / (1000.0f * 1000.0f);
        }
        std::cout << CLR_DARKGRAY ",T:" CLR_WHITE;
        if (c.txSpeed_B_s > 2 * 1000 * 1000) {
            std::cout << CLR_GREEN << c.txSpeed_B_s / (1000.0f * 1000.0f);
        }
        else {
            std::cout << c.txSpeed_B_s / (1000.0f * 1000.0f);
        }
        std::cout << CLR_DARKGRAY ",L:" CLR_WHITE << c.txLimit_B_s / (1000.0f * 1000.0f);
        const uint32_t load_prc = (spent_us * 100) / 1000000;
        std::cout << CLR_DARKGRAY " Load,%=" CLR_WHITE;
        if (load_prc > 50) {
            std::cout << CLR_RED << load_prc;
        }
        else {
            std::cout << load_prc;
        }
        std::cout << CLR_DARKGRAY " MQ=R:" CLR_WHITE << c.debugMaxRxQueue
            << CLR_DARKGRAY ",T:" CLR_WHITE << c.debugMaxTxQueue;
        c.debugMaxRxQueue = 0;
        c.debugMaxTxQueue = 0;
        std::cout << CLR_RESET << std::endl;
        //std::cout << CLR_DARKGRAY " txLimit=" << c.txLimit_B_s / (1024.0f * 1024.0f)
        //    << "MiB/s" CLR_RESET << std::endl;
#     endif // UDSP_TRACE_LEVEL

        for (auto& it : c.txStreams.vec) {
            it.countSent_B = 0;
        }
        //c.txStreams.countSentHigh_B = 0;
        //c.txStreams.countSentMedium_B = 0;
        //c.txStreams.countSentLow_B = 0;
        TPS = 0; //TODO: For all connections
        spent_us = 0;
    }
}

void UDSPSocket::Impl::onUdpReceived(void* data, uint32_t size_B, uint16_t port, uint32_t IPv4) {
    const int64_t now_us = tick_us();

    //thread_local std::minstd_rand rng(now_us);
    //if ((rng() % 8) == 0) {
    //    return; // 12.5% loss
    //}

    if (size_B < sizeof(PacketHeader)) {
        return;
    }
    auto& header = *reinterpret_cast<const PacketHeader*>(data);
    switch (header.packetId) {
    case PacketId::Disconnect: {
        auto connectionIt = connections.find(header.connectionId);
        if (connectionIt == connections.end()) {
            return;
        }
        if (connectionIt->second->nextEraseTick_us == INT64_MAX) {
            connectionIt->second->partialReset();
            connectionIt->second->nextEraseTick_us = now_us + 2 * 1000 * 1000;
            connectionIt->second->lastPacketTick_us = INT64_MAX;
            connectionIt->second->onDisconnected();
            //sigDisconnected();
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATE_CHANGED
            std::cout << CLR_MAGENTA "Disconnected " << connectionIt->second->connectionId
                << " (closed)" CLR_RESET << std::endl;
#         endif // UDSP_TRACE_LEVEL
            if (onDisconnected != nullptr) {
                onDisconnected(&*connectionIt->second, 'c');
            }
        }
        return;
    }
    case PacketId::TypeA:
    case PacketId::PMTUProbe: {
        auto connectionIt = connections.find(header.connectionId);
        if (connectionIt != connections.end()) {
            if (connectionIt->second->nextEraseTick_us != INT64_MAX) {
                return;
            }
        }
        break;
    }
    default:
        return;
    }

    if (not isServer and connections.empty()) {
        return;
    }
    //TODO: Connections number limit
    auto& c = isServer ? serverConnection(header.connectionId) : clientConnection();
    if (isServer and c.IPv4 == 0) {
        c.connectionId = header.connectionId;
        //TODO: Migration
        if (not initConnection(c, port, IPv4)) {
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
            std::cout << "Unknown error" << std::endl;
#         endif // UDSP_TRACE_LEVEL
            return;
        }
    }
    if (c.lastPacketTick_us == INT64_MAX) {
        c.RTTRequestTick_us = now_us;
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATE_CHANGED
        std::cout << CLR_MAGENTA "Connected " << c.connectionId
            << CLR_RESET << std::endl;
#     endif // UDSP_TRACE_LEVEL
        if (onConnected) {
            onConnected(&c);
        }
        //c.RTTSmooth_us.init(0.05f, 0.05f);
    }
    c.lastPacketTick_us = now_us;

    if (header.RTTResponse == c.RTTRequest and c.RTTRequestTick_us > 0) {
        int64_t RTT_us = now_us - c.RTTRequestTick_us - header.RTTDelay_us;
        if (RTT_us <= 0) {
            RTT_us = 400;
        }
        c.RTTRequestTick_us = 0;
        if (0 < RTT_us and RTT_us < UINT32_MAX and RTT_us < c.minRTT_us) {
            c.minRTT_us = RTT_us;
        }

        //const uint32_t sumRTT_us = c.avgRTT_us * c.avgRTTCount + RTT_us;
        //++c.avgRTTCount;
        //c.avgRTT_us = sumRTT_us / c.avgRTTCount;

        c.RTTSmooth_us.update(RTT_us);
        c.RTT_us = uint32_t(c.RTTSmooth_us.result());

        if (c.isRTTProbing) {
            if (0 < RTT_us and RTT_us < UINT32_MAX and RTT_us < c.nextMinRTT_us) {
                c.nextMinRTT_us = RTT_us;
            }
            if (++c.RTTCount >= g_RTTNewProbeLimit) {
#             if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_MIN_RTT
                std::cout << "minRTT prev=" << c.minRTT_us
                    << " new=" << c.nextMinRTT_us << std::endl;
#             endif // UDSP_TRACE_LEVEL
                c.RTTCount = 0;
                c.minRTT_us = c.nextMinRTT_us;
                c.nextMinRTT_us = UINT32_MAX;
                c.isRTTProbing = false;
                c.nextRTTProbe_us = now_us + g_RTTProbePeriod_us;
            }
        }
        else if (g_isRTTCongestionEnabled) {
            const int64_t allowedRTT_us = c.minRTT_us * g_kAllowedRTT;

            const bool tooBigRTT = 1000 < RTT_us
                and (allowedRTT_us < RTT_us or 100000 < RTT_us);

            if (tooBigRTT or c.tooBigLoss) {
                c.txLimit_B_s -= c.txLimit_B_s / g_kDecStep;
                if (c.nextRTTProbe_us <= now_us and ++c.RTTCount >= g_RTTBadProbeLimit) {
                    c.RTTCount = 0;
                    c.isRTTProbing = true;
                    //std::cout << "isRTTProbing = true" << std::endl;
                }
            }
            else {
                c.RTTCount = 0;
                if (c.txLimit_B_s / g_kIncStep <= g_packetsWindowSize * g_basePMTU_IPv4_B) {
                    c.txLimit_B_s += g_packetsWindowSize * g_basePMTU_IPv4_B;
                }
                else {
                    c.txLimit_B_s += c.txLimit_B_s / g_kIncStep;
                }
                c.txLimit_B_s = std::min(c.txLimit_B_s, c.desiredTxLimit_B_s);
            }
            c.tooBigRTT = tooBigRTT;
        }
    }

    if (c.RTTResponse != header.RTTRequest) {
        c.RTTResponse = header.RTTRequest;
        c.RTTResponseTick_us = now_us;
    }

    if (c.PMTUProbeSize_B > 0) {
        if (header.PMTUProbeSize_B == c.PMTUProbeSize_B) {
            c.PMTU_B = c.PMTUProbeSize_B;
            c.PMTUProbeSize_B = 0;
            c.PMTUProbeCount = 0;
            //std::cout << CLR_YELLOW "c.PMTU_B = " << c.PMTU_B << CLR_RESET << std::endl;
        }
    }

    const uint32_t txPacketsWindowIdx = c.txPacketsCount / g_packetsWindowSize;
    if (c.txPacketsWindowIdx != txPacketsWindowIdx) {
        c.txPacketsWindowIdx = txPacketsWindowIdx;

        const uint16_t txPacketsLossInWindow_prc100 = header.packetsLoss_prc100;
        c.txPacketsLossSum_prc100 += txPacketsLossInWindow_prc100;
        ++c.txPacketsLossSum_count;

        if (not c.isRTTProbing and g_isLossCongestionEnabled) {
            assert(txPacketsLossInWindow_prc100 < 10100);
            const bool tooBigLoss = 100 < txPacketsLossInWindow_prc100
                                      and txPacketsLossInWindow_prc100 < 10100;
            if (tooBigLoss or c.tooBigRTT) {
                c.txLimit_B_s -= c.txLimit_B_s / g_kDecStep;
            }
            else {
                if (c.txLimit_B_s / g_kIncStep <= g_packetsWindowSize * g_basePMTU_IPv4_B) {
                    c.txLimit_B_s += g_packetsWindowSize * g_basePMTU_IPv4_B;
                }
                else {
                    c.txLimit_B_s += c.txLimit_B_s / g_kIncStep;
                }
                c.txLimit_B_s = std::min(c.txLimit_B_s, c.desiredTxLimit_B_s);
            }
            c.tooBigLoss = tooBigLoss;
        }
    }

    switch (header.packetId) {
    case PacketId::TypeA: {
        size_t offset_B = sizeof(PacketHeader);
        auto rxBuffer = static_cast<const uint8_t*>(data);
        while (true) {
            const size_t available_B = size_t(size_B) - offset_B;
            if (available_B == 0) {
                break;
            }
            const size_t readed_B = c.readChunk(now_us, &rxBuffer[offset_B], available_B);
            assert(readed_B <= available_B);
            if (readed_B <= 0) {
                break;
            }
            offset_B += readed_B;
        }
        break;
    }
    case PacketId::PMTUProbe:
        c.PMTUProbeResponse_B = size_B;
        //std::cout << CLR_YELLOW "c.PMTUProbeResponse_B = " << size_B << CLR_RESET << std::endl;
        return;
    case PacketId::Disconnect:
    default:
        return;
    }


# if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_RX_PACKET
    std::cout << __LINE__ << " received " << size_B + g_headerSize_IPv4_B
        << " id=" << header.packetNumber << "\n";
# endif // UDSP_TRACE_LEVEL
    c.rxCount_B_s += size_B + g_headerSize_IPv4_B;
    c.rxPacketNumberReceived = header.packetNumber;
    ++c.rxPacketsCountReceived;
    ++c.rxPacketsCountInWindow;

    const uint32_t rxPacketNumberReceived = header.packetNumber;
    // Probability of incorrect calculation of the packets loss
    //   = Probability of receiving a packet out of order
    //   * (1 / g_packetsWindowSize)
    const uint32_t rxPacketsWindowIdx = rxPacketNumberReceived / g_packetsWindowSize;
    if (c.rxPacketsWindowIdx != rxPacketsWindowIdx) {
        c.rxPacketsWindowIdx = rxPacketsWindowIdx;

        uint32_t rxPacketsCountInWindowExpected = 0;
        // processed=990, received=1010 --> expected=20
        if (c.rxPacketNumberInWindowProcessed < rxPacketNumberReceived) {
            rxPacketsCountInWindowExpected = rxPacketNumberReceived
                - c.rxPacketNumberInWindowProcessed;
        }
        // processed=1010, received=990 --> expected=0
        else if (c.rxPacketNumberInWindowProcessed - rxPacketNumberReceived < UINT32_MAX / 2) {
            rxPacketsCountInWindowExpected = 0;
        }
        // processed=UINT32_MAX-10, received=10 --> expected=20
        else {
            rxPacketsCountInWindowExpected = UINT32_MAX
                - c.rxPacketNumberInWindowProcessed + rxPacketNumberReceived;
        }

        if (rxPacketsCountInWindowExpected > 0) {
            if (c.rxPacketsCountInWindow < rxPacketsCountInWindowExpected) {
                c.rxPacketsLossInWindow_prc100 = 10000
                    - (c.rxPacketsCountInWindow * 10000) / rxPacketsCountInWindowExpected;
            }
            else {
                c.rxPacketsLossInWindow_prc100 = 0;
            }
            assert(c.rxPacketsLossInWindow_prc100 < 10100);

            c.rxPacketsCountInWindow = 0;
            c.rxPacketNumberInWindowProcessed = rxPacketNumberReceived;
        }
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_RX_PACKET
        std::cout << "processed=" << c.rxPacketNumberInWindowProcessed << " received="
            << rxPacketNumberReceived << " expected=" << rxPacketsCountInWindowExpected
            << " actual=" << c.rxPacketsCountInWindow
            << " loss=" << c.rxPacketsLossInWindow_prc100 * 0.01f <<"\n";
#     endif // UDSP_TRACE_LEVEL

        c.writePacket(false, true);
        udpSocket.send(c.txBuffer.data(), c.txBuffer.size(), c.port, c.IPv4);
        c.nextKeepAliveTick_us = now_us + g_keepAlivePeriod_us;
        const uint32_t sent_B = c.txBuffer.size() + g_headerSize_IPv4_B;
        c.txCount_B += sent_B;
        c.txCount_B_s += sent_B;

#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_TX_PACING
        std::cout << CLR_YELLOW << __LINE__ << " send id=" << c.txPacketsCount
            << " diff=" << now_us - prev_us << CLR_RESET "\n";
        prev_us = now_us;
#     endif // UDSP_TRACE_LEVEL
    }
}
