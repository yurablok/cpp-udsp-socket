#include "Impl.hpp"

namespace {
    constexpr int64_t g_resetTxCountersPeriod_us = 10 * 1000 * 1000;
    //constexpr uint8_t g_maxInflightPacketsPerTxStream = 10;
    constexpr uint32_t g_chunkHeader_B = 1 + 1 + 4;
    constexpr uint32_t g_txFifoReturnK = 2; // RTT * K
    constexpr uint32_t g_rxFifoCleanupK = 200; // RTT * K

    template <typename value_t>
    void write_u8(uint8_t*& buffer, const value_t value) {
        assert(0 <= value and value <= UINT8_MAX);
        *buffer = static_cast<uint8_t>(value);
        buffer += sizeof(uint8_t);
    }
    template <typename value_t>
    void write_u16(uint8_t*& buffer, const value_t value) {
        assert(0 <= value and value <= UINT16_MAX);
        *reinterpret_cast<uint16_t*>(buffer) = static_cast<uint16_t>(value);
        buffer += sizeof(uint16_t);
    }
    template <typename value_t>
    void write_u32(uint8_t*& buffer, const value_t value) {
        assert(0 <= value and value <= UINT32_MAX);
        *reinterpret_cast<uint32_t*>(buffer) = static_cast<uint32_t>(value);
        buffer += sizeof(uint32_t);
    }
    template <typename value_t>
    void write_u64(uint8_t*& buffer, const value_t value) {
        assert(0 <= value and value <= UINT64_MAX);
        *reinterpret_cast<uint64_t*>(buffer) = static_cast<uint64_t>(value);
        buffer += sizeof(uint64_t);
    }
    void write_data(uint8_t*& buffer, const void* data, const size_t size_B) {
        auto ptr = static_cast<const uint8_t*>(data);
        std::copy(ptr, ptr + size_B, buffer);
        buffer += size_B;
    }

    uint8_t read_u8(const uint8_t*& buffer) {
        const uint8_t value = *buffer;
        buffer += sizeof(uint8_t);
        return value;
    }
    uint16_t read_u16(const uint8_t*& buffer) {
        const uint16_t value = *reinterpret_cast<const uint16_t*>(buffer);
        buffer += sizeof(uint16_t);
        return value;
    }
    uint32_t read_u32(const uint8_t*& buffer) {
        const uint32_t value = *reinterpret_cast<const uint32_t*>(buffer);
        buffer += sizeof(uint32_t);
        return value;
    }
    uint64_t read_u64(const uint8_t*& buffer) {
        const uint64_t value = *reinterpret_cast<const uint64_t*>(buffer);
        buffer += sizeof(uint64_t);
        return value;
    }
} // namespace

void UDSPSocket::Impl::setTxStreamPriority_ts(Connection* c, uint8_t txStreamId, char priority) {
    Priority newPriority = Priority::Medium;
    switch (priority) {
    case 'R': case 'r':
        newPriority = Priority::Realtime;
        break;
    case 'H': case 'h':
        newPriority = Priority::High;
        break;
    case 'M': case 'm':
        newPriority = Priority::Medium;
        break;
    case 'L': case 'l':
        newPriority = Priority::Low;
        break;
    default:
        assert(false and priority);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    c->commands.emplace_back([=] {
        auto& stream = c->txStreams.map[txStreamId];
        if (stream.isNew) {
            stream.isNew = false;
            stream.id = txStreamId;
            c->txStreams.isStreamsChanged = true;
        }
        const bool isReliable = priority < 'a';
        if (stream.priority == newPriority and stream.isReliable == isReliable) {
            return;
        }
        stream.priority = newPriority;
        stream.isReliable = isReliable;
        c->txStreams.isStreamsChanged = true;
    });
}
//char UDSPSocket::Impl::getTxStreamPriority_ts(Connection& c, uint8_t txStreamId) const {
//    std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
//    if (not lock.try_lock()) {
//        return 0;
//    }
//    auto& stream = c.txStreams.map[txStreamId];
//    if (stream.isNew) {
//        stream.isNew = false;
//        stream.id = txStreamId;
//        c.txStreams.isStreamsChanged = true;
//    }
//    switch (stream.priority) {
//    case Priority::Realtime:
//        return stream.isReliable ? 'R' : 'r';
//    case Priority::High:
//        return stream.isReliable ? 'H' : 'h';
//    case Priority::Medium:
//        return stream.isReliable ? 'M' : 'm';
//    case Priority::Low:
//        return stream.isReliable ? 'L' : 'l';
//    default:
//        return 0;
//    }
//}

bool UDSPSocket::Impl::send_ts(uintptr_t context, Connection* c, const void* data,
        uint64_t size_B, bool copy, uint8_t streamId, uint32_t timeout_ms, int64_t now_us) {
    if (c == nullptr) {
        return false;
    }
    if (data == nullptr or size_B == 0) {
        return false;
    }
    if (timeout_ms < 10) {
        return false;
    }

    std::vector<uint8_t> bufferA;
    if (copy) {
        auto bytes = static_cast<const uint8_t*>(data);
        bufferA.insert(bufferA.end(), bytes, bytes + size_B);
    }

    std::lock_guard<std::mutex> lock(mutex);
    c->commands.emplace_back([=, bufferB = std::move(bufferA)] {
        auto& stream = c->txStreams.map[streamId];
        if (stream.isNew) {
            stream.isNew = false;
            stream.id = streamId;
            c->txStreams.isStreamsChanged = true;
        }
        auto& fifo = stream.fifo;
        //if (fifo.size() >= 1000000) {
        //    assert(fifo.size() < 1000000);
        //    return false;
        //}
        fifo.emplace_back();
        auto& packet = fifo.back();
        if (copy) {
            packet.copy = std::move(bufferB);
            packet.pointer = packet.copy.data();
        }
        else {
            packet.pointer = static_cast<const uint8_t*>(data);
        }
        packet.size_B = size_B;
        packet.timeout_us = now_us + int64_t(timeout_ms) * 1000;
        packet.id = stream.nextPacketId++;
        //std::cout << "Debug: Enqueue packetId=" << packet.id << "\n";
        packet.isReliable = stream.isReliable;
        stream.fifoShadow.emplace_back(packet.id);
    });
    return true;
}

void UDSPSocket::Connection::nextDatagram() {
    ++txPacketsCount;
    //txStreams.countRealtimeInDatagram = 0;
    //txStreams.countHighInDatagram = 0;
    //txStreams.countMediumInDatagram = 0;
    //txStreams.countLowInDatagram = 0;
}
size_t UDSPSocket::Connection::writeMetaChunk(const int64_t now_us,
        uint8_t* buffer, const uint32_t available_B) {
    if (not rxStreams.acks.empty()) {
        const size_t chunkSize_B = g_chunkHeader_B;
        if (chunkSize_B > available_B) {
            return 0;
        }
        ChunkId chunkId;
        chunkId.smallPacket.chunkType = ChunkId::Type::SmallPacket;
        chunkId.smallPacket.isAcknowledge = true;
        chunkId.smallPacket.isReliable = true;
        write_u8(buffer, chunkId.total);
        write_u8(buffer, rxStreams.acks.front().first);
        write_u32(buffer, rxStreams.acks.front().second);
        rxStreams.acks.pop_front();
        return chunkSize_B;
    }
    for (uint32_t i1 = 0, s1 = uint32_t(rxStreams.map.size()); i1 < s1; ++i1) {
        rxStreams.next();
        auto& stream = rxStreams.streamIt->second;
        processRxFifo(stream, now_us);

        for (uint32_t i2 = 0, s2 = uint32_t(stream.fifo.size()); i2 < s2; ++i2) {
            auto& packet = stream.next();
            if (packet.datagramId == txPacketsCount) {
                break;
            }
            if (packet.needToSendRepeatInfo()) {
                ChunkId chunkId;
                chunkId.repeatInfo.chunkType = ChunkId::Type::RepeatInfo;
                chunkId.repeatInfo.repeatOffsetBits
                    = ChunkId::getNumberOfBits(packet.offsetRepeat_B);
                chunkId.repeatInfo.repeatSizeBits
                    = ChunkId::getNumberOfBits(packet.sizeRepeat_B);

                const uint32_t repeatOffsetBytes_B
                    = ChunkId::getNumberOfBytes(chunkId.repeatInfo.repeatOffsetBits);
                const uint32_t repeatSizeBytes_B
                    = ChunkId::getNumberOfBytes(chunkId.repeatInfo.repeatSizeBits);
                const size_t chunkSize_B = g_chunkHeader_B + repeatOffsetBytes_B + repeatSizeBytes_B;
                if (chunkSize_B > available_B) {
                    return 0;
                }
                write_u8(buffer, chunkId.total);
                write_u8(buffer, stream.id);
                write_u32(buffer, packet.id);
                switch (ChunkId::Bits(chunkId.repeatInfo.repeatOffsetBits)) { // GCC 4.9
                case ChunkId::Bits::u8:     write_u8(buffer, packet.offsetRepeat_B);    break;
                case ChunkId::Bits::u16:    write_u16(buffer, packet.offsetRepeat_B);   break;
                case ChunkId::Bits::u32:    write_u32(buffer, packet.offsetRepeat_B);   break;
                case ChunkId::Bits::u64:    write_u64(buffer, packet.offsetRepeat_B);   break;
                default:                                                                return 0;
                }
                switch (ChunkId::Bits(chunkId.repeatInfo.repeatSizeBits)) { // GCC 4.9
                case ChunkId::Bits::u8:     write_u8(buffer, packet.sizeRepeat_B);  break;
                case ChunkId::Bits::u16:    write_u16(buffer, packet.sizeRepeat_B); break;
                case ChunkId::Bits::u32:    write_u32(buffer, packet.sizeRepeat_B); break;
                case ChunkId::Bits::u64:    write_u64(buffer, packet.sizeRepeat_B); break;
                default:                                                            return 0;
                }

                packet.offsetRepeat_B = UINT64_MAX;
                packet.datagramId = txPacketsCount;
                //std::cout << now_us / 1000 << " Debug: TX RepeatInfo streamId="
                //    << rxStreams.streamIt->first
                //    << " offsetRepeat_B=" << packet.offsetRepeat_B << "\n";
                return chunkSize_B;
            }
        }
    }
    return 0;
}
size_t UDSPSocket::Connection::writeDataChunk(const int64_t now_us,
        uint8_t* buffer, const uint32_t available_B) {
    if (available_B <= g_chunkHeader_B) {
        return 0;
    }

    // Нужно выбрать следующий кусочек следующего пакета следующего стрима по приоритету.
    // При выборе по приоритету, должна быть информация про следующие пакеты каждого приоритета.

    txStreams.update();

    std::array<TxStream*, size_t(Priority::_count_)> streamByPriority = {};
    std::array<TxPacket*, size_t(Priority::_count_)> packetByPriority = {};

    for (uint8_t iPriority = 0; iPriority < uint8_t(Priority::_count_); ++iPriority) {
        auto& priorityMeta = txStreams.vec[iPriority];

        for (size_t iPrVec = 0, vecIdx = priorityMeta.vecIdx;
                iPrVec < priorityMeta.vec.size(); ++iPrVec) {
            if (vecIdx >= priorityMeta.vec.size()) {
                vecIdx = 0;
            }
            TxStream& stream = *priorityMeta.vec[vecIdx++];
            processTxFifo(stream, now_us);

            std::deque<TxPacket>& fifo = stream.fifo;
            for (size_t iPacket = 0; iPacket < fifo.size(); ++iPacket) {
                if (stream.fifoReturnTime_us + RTT_us * g_txFifoReturnK < now_us) {
                    stream.fifoReturnTime_us = now_us + RTT_us * g_txFifoReturnK;
                    stream.packetFifoIdx = 0;
                }
                // To limit RX queue in the receiver side
                //if (stream.packetFifoIdx > UINT16_MAX / 2) {
                if (stream.packetFifoIdx > 1 << 17) {
                    break;
                }
                if (stream.packetFifoIdx >= fifo.size()) {
                    break;
                }
                TxPacket& packet = fifo[stream.packetFifoIdx];
                if (packet.isAcknowledged) {
                    ++stream.packetFifoIdx;
                    continue;
                }
                // Timed out, waiting for pop_front
                if (packet.timeout_us <= now_us) {
                    ++stream.packetFifoIdx;
                    continue;
                }
                // To prevent sending the same small packet through the same datagram
                if (packet.datagramId == txPacketsCount) {
                    ++stream.packetFifoIdx;
                    continue;
                }
                // To prevent sending the same small packet too frequently
                if (not packet.isStarted and now_us < packet.begin_us + RTT_us * 2) {
                    ++stream.packetFifoIdx;
                    continue; //TODO: Check
                }
                streamByPriority[iPriority] = &stream;
                packetByPriority[iPriority] = &packet;
                break;
            }
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
            debugMaxTxQueue = std::max(debugMaxTxQueue, uint32_t(stream.packetFifoIdx));
#         endif // UDSP_TRACE_LEVEL
            if (packetByPriority[iPriority] != nullptr) {
                break;
            }
        }
    }

    const uint32_t countSentRealtime_B = txStreams.vec[size_t(Priority::Realtime)].countSent_B;
    const uint32_t countSentHigh_B = txStreams.vec[size_t(Priority::High)].countSent_B;
    const uint32_t countSentMedium_B = txStreams.vec[size_t(Priority::Medium)].countSent_B;
    const uint32_t countSentLow_B = txStreams.vec[size_t(Priority::Low)].countSent_B;
    const uint32_t countSentTotalNonRT_B = countSentHigh_B + countSentMedium_B + countSentLow_B + 1;
    const uint32_t countSentTotal_B = countSentRealtime_B + countSentHigh_B
                                    + countSentMedium_B + countSentLow_B + 1;

    Priority priority = Priority::None;
    TxStream* streamPtr = nullptr;
    TxPacket* packetPtr = nullptr;
    TxStreams::PriorityMeta* metaPtr = nullptr;

    if (packetByPriority[size_t(Priority::Realtime)] != nullptr) {
        if ((countSentRealtime_B * 100) / countSentTotal_B < 98) {
            priority = Priority::Realtime;
        }
        else {
            std::array<Priority, size_t(Priority::_count_)> priorities;
            for (size_t i = 0, p = 0; i < size_t(Priority::_count_); ++i) {
                for (;; ++p) {
                    if (p >= size_t(Priority::_count_)) {
                        p = 0;
                    }
                    if (packetByPriority[p] == nullptr) {
                        continue;
                    }
                    priorities[i] = Priority(p++);
                    break;
                }
            }
#         if defined(NDEBUG)
            priority = priorities[(size_t(now_us) * 48271) & 0x3];
#         else
            thread_local std::minstd_rand rng;
            priority = priorities[rng() & 0x3];
#         endif
        }
    }
    // high 80%, medium 16%, low 4%
    //
    // 80 x + 4 x = 100
    // x = 1.190476
    // high 95%, low 5%
    //
    // 80 x + 16 x = 100
    // x = 1.041666
    // high 83.33%, medium 16.66%
    //
    // 16 x + 4 x = 100
    // x = 5
    // medium 80%, low 20%
    //
    // 000  1. - - -
    // 001  2. - - L  100%
    // 010  3. - M -  100%
    // 011  4. - M L  80% 20%
    // 100  5. H - -  100%
    // 101  6. H - L  95% 5%
    // 110  7. H M -  84% 16%
    // 111  8. H M L  80% 16% 4%
    else if (packetByPriority[size_t(Priority::High)] != nullptr) {
        if (packetByPriority[size_t(Priority::Medium)] != nullptr) {
            if (packetByPriority[size_t(Priority::Low)] != nullptr) {
                // 111  8. H M L  80% 16% 4%
                if ((countSentHigh_B * 100) / countSentTotalNonRT_B < 80) {
                    priority = Priority::High;
                }
                else if ((countSentMedium_B * 100) / countSentTotalNonRT_B < 16) {
                    priority = Priority::Medium;
                }
                else {
                    priority = Priority::Low;
                }
            }
            else {
                // 110  7. H M -  84% 16%
                if ((countSentHigh_B * 100) / countSentTotalNonRT_B < 84) {
                    priority = Priority::High;
                }
                else {
                    priority = Priority::Medium;
                }
            }
        }
        else {
            if (packetByPriority[size_t(Priority::Low)] != nullptr) {
                // 101  6. H - L  95% 5%
                if ((countSentHigh_B * 100) / countSentTotalNonRT_B < 95) {
                    priority = Priority::High;
                }
                else {
                    priority = Priority::Low;
                }
            }
            else {
                // 100  5. H - -  100%
                priority = Priority::High;
            }
        }
    }
    else {
        if (packetByPriority[size_t(Priority::Medium)] != nullptr) {
            if (packetByPriority[size_t(Priority::Low)] != nullptr) {
                // 011  4. - M L  80% 20%
                if ((countSentMedium_B * 100) / countSentTotalNonRT_B < 80) {
                    priority = Priority::Medium;
                }
                else {
                    priority = Priority::Low;
                }
            }
            else {
                // 010  3. - M -  100%
                priority = Priority::Medium;
            }
        }
        else {
            if (packetByPriority[size_t(Priority::Low)] != nullptr) {
                // 001  2. - - L  100%
                priority = Priority::Low;
            }
            else {
                // 000  1. - - -
            }
        }
    }
    if (priority == Priority::None) {
        return 0;
    }
    TxStream& stream = *streamByPriority[size_t(priority)];
    TxPacket& packet = *packetByPriority[size_t(priority)];
    TxStreams::PriorityMeta& priorityMeta = txStreams.vec[size_t(priority)];
    uint32_t& countSent_B = priorityMeta.countSent_B;
    if (++priorityMeta.vecIdx >= priorityMeta.vec.size()) {
        priorityMeta.vecIdx = 0;
    }

    ChunkId chunkId;
    const auto packetSizeBits = ChunkId::getNumberOfBits(packet.size_B);
    const uint32_t packetSizeBytes_B = ChunkId::getNumberOfBytes(packetSizeBits);

    if (not packet.isStarted) {
        const uint64_t smallPacket_B = g_chunkHeader_B + packetSizeBytes_B + packet.size_B;
        if (smallPacket_B <= available_B) {
            chunkId.smallPacket.chunkType = ChunkId::Type::SmallPacket;
            chunkId.smallPacket.isReliable = packet.isReliable;
            chunkId.smallPacket.isFront = stream.packetFifoIdx == 0;
            chunkId.smallPacket.packetSizeBits = packetSizeBits;

            write_u8(buffer, chunkId.total);
            write_u8(buffer, stream.id);
            write_u32(buffer, packet.id);
            switch (packetSizeBits) {
            case ChunkId::Bits::u8:     write_u8(buffer, packet.size_B);    break;
            case ChunkId::Bits::u16:    write_u16(buffer, packet.size_B);   break;
            default:                    assert(false);                      return 0;
            }
            write_data(buffer, packet.pointer, packet.size_B);

            if (not packet.isReliable) {
                packet.isAcknowledged = true;
                processTxFifo(stream, now_us);
            }
            else {
                //packet.isStarted = true; //TODO
                packet.datagramId = txPacketsCount;
            }
            packet.begin_us = now_us;
            //*countInDatagram += 1;
            countSent_B += uint32_t(smallPacket_B);
            //std::cout << "Debug: writeDataChunk packetId=" << packet.id << "\n";
            //thread_local uint32_t debugPacketId = 0;
            //if (packet.id - debugPacketId > 1) {
            //    std::cout << "Warning: prev=" << debugPacketId << " this=" << packet.id
            //        << " diff=" << packet.id - debugPacketId << "\n";
            //}
            //debugPacketId = packet.id;

            //debugMinPacketIdSent = std::min(debugMinPacketIdSent, packet.id);
            //debugMaxPacketIdSent = std::max(debugMaxPacketIdSent, packet.id);
            //thread_local std::unordered_set<uint32_t> debugMap;
            //debugMap.insert(packet.id);
            //if (packet.id > 100000) {
            //    assert(false);
            //}
            return smallPacket_B;
        }

        const uint32_t beginOfPacket_B = g_chunkHeader_B + packetSizeBytes_B + 4;
        if (beginOfPacket_B <= available_B) {
            chunkId.beginOfPacket.chunkType = ChunkId::Type::BeginOfPacket;
            chunkId.beginOfPacket.isReliable = packet.isReliable;
            chunkId.beginOfPacket.isFront = stream.packetFifoIdx == 0;
            chunkId.beginOfPacket.packetSizeBits = packetSizeBits;

            write_u8(buffer, chunkId.total);
            write_u8(buffer, stream.id);
            write_u32(buffer, packet.id);
            switch (packetSizeBits) {
            case ChunkId::Bits::u8:     write_u8(buffer, packet.size_B);    break;
            case ChunkId::Bits::u16:    write_u16(buffer, packet.size_B);   break;
            case ChunkId::Bits::u32:    write_u32(buffer, packet.size_B);   break;
            case ChunkId::Bits::u64:    write_u64(buffer, packet.size_B);   break;
            default:                    assert(false);                      return 0;
            }
            write_u32(buffer, 0);

            packet.isStarted = true;
            packet.begin_us = now_us;
            //packet.datagramId = packetsTxCount;
            countSent_B += beginOfPacket_B;
            //std::cout << CLR_BLUE " Debug: TX BeginOfPacket packetId=" CLR_RESET << packet.id << "\n";
            //std::cout << "Debug: writeDataChunk packetId=" << packet.id << "\n";
            return beginOfPacket_B;
        }
        return 0;
    }
    else {
        const auto offsetBits = ChunkId::getNumberOfBits(packet.offset_B);
        const uint32_t offsetBytes_B = ChunkId::getNumberOfBytes(offsetBits);

        const uint32_t pieceOfPacketMaxFor1B_B = g_chunkHeader_B + offsetBytes_B + 2 + 1;
        if (available_B < pieceOfPacketMaxFor1B_B) {
            //continue;
            return 0;
        }
        const uint32_t pieceSize_B = static_cast<uint32_t>(std::min<uint64_t>(
            (available_B - pieceOfPacketMaxFor1B_B) + 1,
            packet.size_B - packet.offset_B
        ));

        const auto pieceSizeBits = ChunkId::getNumberOfBits(pieceSize_B);
        const uint32_t pieceSizeBytes_B = ChunkId::getNumberOfBytes(pieceSizeBits);

        const uint32_t pieceOfPacket_B = g_chunkHeader_B + offsetBytes_B
            + pieceSizeBytes_B + pieceSize_B;

        chunkId.pieceOfPacket.chunkType = ChunkId::Type::PieceOfPacket;
        chunkId.pieceOfPacket.isReliable = packet.isReliable;
        chunkId.pieceOfPacket.isFront = stream.packetFifoIdx == 0;
        chunkId.pieceOfPacket.offsetBits = offsetBits;
        chunkId.pieceOfPacket.pieceSizeBits = pieceSizeBits;

        write_u8(buffer, chunkId.total);
        write_u8(buffer, stream.id);
        write_u32(buffer, packet.id);
        switch (offsetBits) {
        case ChunkId::Bits::u8:     write_u8(buffer, packet.offset_B);      break;
        case ChunkId::Bits::u16:    write_u16(buffer, packet.offset_B);     break;
        case ChunkId::Bits::u32:    write_u32(buffer, packet.offset_B);     break;
        case ChunkId::Bits::u64:    write_u64(buffer, packet.offset_B);     break;
        default:                    assert(false);                          return 0;
        }
        switch (pieceSizeBits) {
        case ChunkId::Bits::u8:     write_u8(buffer, pieceSize_B);          break;
        case ChunkId::Bits::u16:    write_u16(buffer, pieceSize_B);         break;
        default:                    assert(false);                          return 0;
        }
        write_data(buffer, &packet.pointer[packet.offset_B], pieceSize_B);

        packet.offset_B += pieceSize_B;
        if (not packet.isReliable and packet.offset_B >= packet.size_B) {
            packet.isAcknowledged = true;
            processTxFifo(stream, now_us);
        }
        else {
            if (packet.offset_B >= packet.size_B) {
                packet.offset_B = 0;
                packet.isStarted = false; //TODO
            }
            else {
                packet.isStarted = true; //TODO
            }
            packet.datagramId = txPacketsCount;
        }
        countSent_B += pieceOfPacket_B;
        //std::cout << "Debug: writeDataChunk packetId=" << packet.id << "\n";
        return pieceOfPacket_B;
    }
    return 0;
}
size_t UDSPSocket::Connection::writeChunk(const int64_t now_us,
        uint8_t* buffer, const uint32_t available_B) {
    const size_t written_B = writeMetaChunk(now_us, buffer, available_B);
    if (written_B != 0) {
        return written_B;
    }
    return writeDataChunk(now_us, buffer, available_B);
}

size_t UDSPSocket::Connection::readChunk(const int64_t now_us, const uint8_t* buffer,
        const uint32_t available_B) {
    if (available_B < 3) {
        return 0;
    }
    ChunkId chunkId;
    chunkId.total = read_u8(buffer);
    const uint8_t streamId = read_u8(buffer);
    const uint32_t packetId = read_u32(buffer);
    //std::cout << "Debug: readChunk packetId=" << packetId << "\n";
    switch (ChunkId::Type(chunkId.type.chunkType)) { // GCC 4.9
    case ChunkId::Type::SmallPacket: {
        if (chunkId.smallPacket.isAcknowledge) {
            //if (not chunkId.smallPacket.isReliable) {
            //    return 0;
            //}
            //std::cout << CLR_GREEN " Debug: RX Acknowledge packetId=" CLR_RESET << packetId << "\n";
            //std::cout << now_us / 1000 << " Debug: RX Acknowledge streamId=" << streamId << "\n";

            auto streamIt = txStreams.map.find(streamId);
            if (streamIt == txStreams.map.end()) {
                return g_chunkHeader_B;
            }
            auto& stream = streamIt->second;
            const size_t packetIdx = findPacketIdx(stream.fifoShadow, packetId);
            if (packetIdx == SIZE_MAX) {
                //std::cout << CLR_RED << packetId << CLR_RESET "\n";
                return g_chunkHeader_B;
            }
            stream.fifo[packetIdx].isAcknowledged = true;
            processTxFifo(stream, now_us);
            //std::cout << CLR_GREEN << packetId << CLR_RESET "\n";
            return g_chunkHeader_B;
        }
        const uint32_t packetSizeBytes_B = ChunkId::getNumberOfBytes(
            chunkId.smallPacket.packetSizeBits);
        if (g_chunkHeader_B + packetSizeBytes_B > available_B) {
            return 0;
        }
        uint32_t size_B = 0;
        switch (ChunkId::Bits(chunkId.smallPacket.packetSizeBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     size_B = read_u8(buffer);   break;
        case ChunkId::Bits::u16:    size_B = read_u16(buffer);  break;
        default:                    assert(false);              return 0;
        }
        if (size_B == 0) {
            return 0;
        }
        const size_t chunkSize_B = g_chunkHeader_B + packetSizeBytes_B + size_B;
        if (chunkSize_B > available_B) {
            return 0;
        }
        //thread_local std::unordered_set<uint32_t> debugMap;
        //debugMap.insert(packetId);
        //if (packetId > 5000000) { // 2600000
        //    assert(false);
        //}

        //std::cout << now_us / 1000 << " Debug: RX SmallPacket streamId=" << streamId
        //    << " size_B=" << size_B << "\n";
        auto& stream = rxStreams.map[streamId];
        if (stream.isNew) {
            stream.isNew = false;
            stream.id = streamId;
            stream.get(now_us);
            rxStreams.isStreamsChanged = true;
        }
        stream.updateFrontPacketId(chunkId.smallPacket.isFront, packetId);
        if (stream.fifo.size() >= UINT16_MAX / 2) {
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
            std::cout << CLR_YELLOW " Debug: RX fifo overflow" CLR_RESET "\n";
#         endif // UDSP_TRACE_LEVEL
            return chunkSize_B;
        }
        if (stream.isReceivedBefore(packetId)) {
            if (chunkId.smallPacket.isReliable) {
                if (not rxStreams.acks.empty()
                        and rxStreams.acks.back().first == streamId
                        and rxStreams.acks.back().second == packetId) {
                    return chunkSize_B;
                }
                rxStreams.acks.emplace_back(streamId, packetId);
            }
            return chunkSize_B;
        }
        auto& packet = stream.get(now_us, packetId);
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
        debugMaxRxQueue = std::max(debugMaxRxQueue, uint32_t(stream.fifo.size()));
#     endif // UDSP_TRACE_LEVEL
        if (packet.id != packetId) {
            assert(packet.id == packetId);
            return chunkSize_B;
        }
        packet.timeout_us = now_us + RTT_us * g_rxFifoCleanupK;
        if (not packet.isReceived) {
            packet.isReceived = true;
            if (stream.fifo.front().id == packetId) {
                if (onReceived != nullptr) {
                    onReceived(
                        packet.context, this, buffer, size_B, 0, streamId, 's'
                    );
                }
                stream.debugPacketId = packetId;
                stream.updateFrontPacketId(true, packetId + 1);
                stream.fifo.pop_front();
                stream.fifoShadow.pop_front();
                if (stream.packetIdx > 0) {
                    --stream.packetIdx;
                }
            }
            else {
                packet.size_B = size_B;
                packet.copy.clear();
                packet.copy.insert(packet.copy.end(), buffer, buffer + size_B);
            }
        }
        if (chunkId.smallPacket.isReliable) {
            if (not rxStreams.acks.empty()
                    and rxStreams.acks.back().first == streamId
                    and rxStreams.acks.back().second == packetId) {
            }
            else {
                rxStreams.acks.emplace_back(streamId, packetId);
            }
        }
        processRxFifo(stream, now_us);
        return chunkSize_B;
    }
    case ChunkId::Type::BeginOfPacket: {
        const uint32_t packetSizeBytes_B = ChunkId::getNumberOfBytes(
            chunkId.beginOfPacket.packetSizeBits);
        const size_t chunkSize_B = g_chunkHeader_B + packetSizeBytes_B + 4;
        if (chunkSize_B > available_B) {
            return 0;
        }
        uint64_t packetSize_B = 0;
        switch (ChunkId::Bits(chunkId.beginOfPacket.packetSizeBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     packetSize_B = read_u8(buffer);     break;
        case ChunkId::Bits::u16:    packetSize_B = read_u16(buffer);    break;
        case ChunkId::Bits::u32:    packetSize_B = read_u32(buffer);    break;
        case ChunkId::Bits::u64:    packetSize_B = read_u64(buffer);    break;
        default:                                                        return 0;
        }
        //std::cout << now_us / 1000 << " Debug: RX BeginOfPacket streamId=" << streamId
        //    << " packetSize_B=" << packetSize_B << "\n";
        auto& stream = rxStreams.map[streamId];
        if (stream.isNew) {
            stream.isNew = false;
            stream.id = streamId;
            stream.get(now_us);
            rxStreams.isStreamsChanged = true;
        }
        stream.updateFrontPacketId(chunkId.beginOfPacket.isFront, packetId);
        if (stream.fifo.size() >= UINT16_MAX / 2) {
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
            std::cout << CLR_YELLOW " Debug: RX fifo overflow" CLR_RESET "\n";
#         endif // UDSP_TRACE_LEVEL
            return chunkSize_B;
        }
        if (stream.isReceivedBefore(packetId)) {
            //if (chunkId.beginOfPacket.isReliable) {
            //    rxStreams.acks.emplace_back(streamId, packetId);
            //}
            return chunkSize_B;
        }
        auto& packet = stream.get(now_us, packetId);
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
        debugMaxRxQueue = std::max(debugMaxRxQueue, uint32_t(stream.fifo.size()));
#     endif // UDSP_TRACE_LEVEL
        if (packet.id != packetId) {
            return chunkSize_B;
        }
        packet.timeout_us = now_us + RTT_us * g_rxFifoCleanupK;
        if (packet.size_B == 0) {
            packet.size_B = packetSize_B;
            packet.crc32 = read_u32(buffer);
            packet.id = packetId;
            packet.isReliable = chunkId.beginOfPacket.isReliable;
            if (packetSize_B <= impl->rxPacketBufferSizeThreshold_B) {
                packet.copy.resize(packetSize_B);
            }
        }
        processRxFifo(stream, now_us);
        //if (stream.frontPacketId < stream.fifoShadow.front()) {
        //    stream.frontPacketId = stream.frontPacketId;
        //}
        return chunkSize_B;
    }
    case ChunkId::Type::PieceOfPacket: {
        const uint32_t offsetBytes_B = ChunkId::getNumberOfBytes(
            chunkId.pieceOfPacket.offsetBits);
        const uint32_t pieceSizeBytes_B = ChunkId::getNumberOfBytes(
            chunkId.pieceOfPacket.pieceSizeBits);
        if (g_chunkHeader_B + offsetBytes_B + pieceSizeBytes_B + 1 > available_B) {
            return 0;
        }
        uint64_t offset_B = 0;
        switch (ChunkId::Bits(chunkId.pieceOfPacket.offsetBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     offset_B = read_u8(buffer);     break;
        case ChunkId::Bits::u16:    offset_B = read_u16(buffer);    break;
        case ChunkId::Bits::u32:    offset_B = read_u32(buffer);    break;
        case ChunkId::Bits::u64:    offset_B = read_u64(buffer);    break;
        default:                                                    return 0;
        }
        uint32_t pieceSize_B = 0;
        switch (ChunkId::Bits(chunkId.pieceOfPacket.pieceSizeBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     pieceSize_B = read_u8(buffer);  break;
        case ChunkId::Bits::u16:    pieceSize_B = read_u16(buffer); break;
        default:                    assert(false);                  return 0;
        }
        const size_t chunkSize_B = g_chunkHeader_B + offsetBytes_B + pieceSizeBytes_B + pieceSize_B;
        if (chunkSize_B > available_B) {
            return 0;
        }
        //std::cout << now_us / 1000 << " Debug: RX PieceOfPacket streamId=" << streamId
        //    << " offset_B=" << offset_B << " pieceSize_B=" << pieceSize_B << "\n";
        auto& stream = rxStreams.map[streamId];
        if (stream.isNew) {
            stream.isNew = false;
            stream.id = streamId;
            stream.get(now_us);
            rxStreams.isStreamsChanged = true;
        }
        stream.updateFrontPacketId(chunkId.pieceOfPacket.isFront, packetId);
        if (stream.fifo.size() >= UINT16_MAX / 2) {
#         if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
            std::cout << CLR_YELLOW " Debug: RX fifo overflow" CLR_RESET "\n";
#         endif // UDSP_TRACE_LEVEL
            return chunkSize_B;
        }
        if (stream.isReceivedBefore(packetId)) {
            if (chunkId.pieceOfPacket.isReliable) {
                if (not rxStreams.acks.empty()
                        and rxStreams.acks.back().first == streamId
                        and rxStreams.acks.back().second == packetId) {
                    return chunkSize_B;
                }
                rxStreams.acks.emplace_back(streamId, packetId);
            }
            return chunkSize_B;
        }
        auto& packet = stream.get(now_us, packetId);
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_STATISTICS
        debugMaxRxQueue = std::max(debugMaxRxQueue, uint32_t(stream.fifo.size()));
#     endif // UDSP_TRACE_LEVEL
        if (packet.id != packetId) {
            return chunkSize_B;
        }
        //processRxFifo(stream, now_us);

        packet.timeout_us = now_us + RTT_us * g_rxFifoCleanupK;
        if (packet.size_B == 0) {
            packet.offsetRepeat_B = 0;
            packet.isReliable = chunkId.pieceOfPacket.isReliable;
            return chunkSize_B;
        }
        if (offset_B + pieceSize_B > packet.size_B) {
            //TODO: What to do with the wrong piece?
            return chunkSize_B;
        }
        if (offset_B < packet.offset_B) { // Already received
            return chunkSize_B;
        }
        if (packet.offset_B != offset_B) { // Need to repeat
            packet.offset_B = offset_B;
            packet.offsetRepeat_B = packet.offset_B;
            packet.sizeRepeat_B = offset_B - packet.offset_B;
            return chunkSize_B;
        }
        if (not packet.copy.empty()) {
            assert(offset_B + pieceSize_B <= packet.copy.size());
            std::copy(&buffer[0], &buffer[pieceSize_B], &packet.copy[offset_B]);
        }
        else if (stream.frontPacketId != packetId) { // Repeat due to incorrect order
            //TODO: Receive into the copy
            packet.offset_B = 0;
            packet.offsetRepeat_B = 0;
            packet.sizeRepeat_B = packet.size_B;
            return chunkSize_B;
        }
        else if (onReceived != nullptr) {
            //TODO: check CRC32
            packet.context = onReceived(
                packet.context, this, &buffer[0], pieceSize_B, offset_B, streamId, 'p'
            );
        }
        packet.offset_B = offset_B + pieceSize_B;
        if (packet.offset_B >= packet.size_B) {
            //TODO: check CRC32
            packet.isReceived = true;
            if (packet.isReliable) {
                if (not rxStreams.acks.empty()
                        and rxStreams.acks.back().first == streamId
                        and rxStreams.acks.back().second == packetId) {
                }
                else {
                    rxStreams.acks.emplace_back(streamId, packetId);
                }
            }
            processRxFifo(stream, now_us);
        }
        return chunkSize_B;
    }
    case ChunkId::Type::RepeatInfo: {
        const uint32_t repeatOffsetBytes_B = ChunkId::getNumberOfBytes(
            chunkId.repeatInfo.repeatOffsetBits);
        const uint32_t repeatSizeBytes_B = ChunkId::getNumberOfBytes(
            chunkId.repeatInfo.repeatSizeBits);
        const size_t chunkSize_B = g_chunkHeader_B + repeatOffsetBytes_B + repeatSizeBytes_B;
        if (chunkSize_B > available_B) {
            return 0;
        }
        uint64_t repeatOffset_B = 0;
        switch (ChunkId::Bits(chunkId.repeatInfo.repeatOffsetBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     repeatOffset_B = read_u8(buffer);   break;
        case ChunkId::Bits::u16:    repeatOffset_B = read_u16(buffer);  break;
        case ChunkId::Bits::u32:    repeatOffset_B = read_u32(buffer);  break;
        case ChunkId::Bits::u64:    repeatOffset_B = read_u64(buffer);  break;
        default:                                                        return 0;
        }
        uint64_t repeatSize_B = 0;
        switch (ChunkId::Bits(chunkId.repeatInfo.repeatSizeBits)) { // GCC 4.9
        case ChunkId::Bits::u8:     repeatSize_B = read_u8(buffer);     break;
        case ChunkId::Bits::u16:    repeatSize_B = read_u16(buffer);    break;
        case ChunkId::Bits::u32:    repeatSize_B = read_u32(buffer);    break;
        case ChunkId::Bits::u64:    repeatSize_B = read_u64(buffer);    break;
        default:                                                        return 0;
        }
#     if UDSP_TRACE_LEVEL >= UDSP_TRACE_LEVEL_ERROR
        std::cout << CLR_YELLOW " Debug: RX RepeatInfo packetId=" CLR_RESET << packetId << "\n";
#     endif // UDSP_TRACE_LEVEL
        auto streamIt = txStreams.map.find(streamId);
        if (streamIt == txStreams.map.end()) {
            return chunkSize_B;
        }
        auto& stream = streamIt->second;
        const size_t packetIdx = findPacketIdx(stream.fifoShadow, packetId);
        if (packetIdx == SIZE_MAX) {
            return chunkSize_B;
        }
        auto& packet = stream.fifo[packetIdx];
        if (not packet.isReliable) {
            return chunkSize_B;
        }
        packet.offset_B = repeatOffset_B;
        if (repeatOffset_B == 0) {
            packet.isStarted = false;
        }
        return chunkSize_B;
    }
    default:
        break;
    }
    return 0;
}

void UDSPSocket::Connection::processRxFifo(RxStream& stream, const int64_t now_us) {
    //const bool hasOnReceived = impl->onReceived != nullptr;
    while (not stream.fifo.empty()) {
        auto& packet = stream.fifo.front();
        bool keep = true;
        if (packet.isReceived) {
            if (onReceived) {
                onReceived(
                    packet.context, this, packet.copy.empty() ? nullptr : packet.copy.data(),
                    packet.size_B, 0, stream.id, 's'
                );
            }
            stream.debugPacketId = packet.id;
            keep = false;
        }
        else if (packet.id < stream.frontPacketId) {
            //std::cout << CLR_YELLOW "================\n";
            if (packet.id < UINT32_MAX / 4 and UINT32_MAX / 4 * 3 < stream.frontPacketId) {
                break;
            }
            if (onReceived) {
                onReceived(
                    packet.context, this, nullptr, packet.size_B, 0, stream.id, 't'
                );
            }
            keep = false;
        }
        else if (packet.timeout_us <= now_us) {
            if (onReceived) {
                onReceived(
                    packet.context, this, nullptr, packet.size_B, packet.offset_B,
                    stream.id, 't'
                );
            }
            keep = false;
        }
        if (keep) {
            break;
        }
        stream.updateFrontPacketId(true, packet.id + 1);
        stream.fifo.pop_front();
        stream.fifoShadow.pop_front();
        if (stream.packetIdx > 0) {
            --stream.packetIdx;
        }
    }
}

void UDSPSocket::Connection::processTxFifo(TxStream& stream, const int64_t now_us) {
    // Удаляются подтверждённые пакеты.
    // Удаляются опоздавшие неподтверждённые пакеты.
    // Остаются успевающие неподтверждённые пакеты.
    //const bool hasOnDelivered = impl->onDelivered != nullptr;
    while (not stream.fifo.empty()) {
        auto& packet = stream.fifo.front();
        if (not packet.isAcknowledged) {
            if (now_us <= packet.timeout_us) {
                break;
            }
            if (onDelivered) {
                onDelivered(
                    packet.context, this, packet.pointer, packet.size_B, stream.id, 't'
                );
            }
        }
        else {
            if (onDelivered) {
                onDelivered(
                    packet.context, this, packet.pointer, packet.size_B, stream.id, 's'
                );
            }
        }
        stream.fifo.pop_front();
        stream.fifoShadow.pop_front();
        if (stream.packetFifoIdx > 0) {
            --stream.packetFifoIdx;
        }
    }
}

void UDSPSocket::Connection::onDisconnected() {
    commands.clear();
    connectionId = 0;

    //const bool hasOnDelivered = impl->onDelivered != nullptr;
    for (const auto& txStream : txStreams.map) {
        for (const auto& txPacket : txStream.second.fifo) {
            if (txPacket.isAcknowledged) {
                continue;
            }
            if (onDelivered) {
                onDelivered(
                    txPacket.context, this, txPacket.pointer, txPacket.size_B,
                    txStream.first, 'd'
                );
            }
        }
    }
    txStreams.map.clear();
    txStreams.isStreamsChanged = true;
    for (auto& it : txStreams.vec) {
        it.countSent_B = 0;
    }

    //const bool hasOnReceived = impl->onReceived != nullptr;
    for (const auto& rxStream : rxStreams.map) {
        for (const auto& rxPacket : rxStream.second.fifo) {
            if (rxPacket.isReceived) {
                continue;
            }
            if (onReceived) {
                onReceived(
                    rxPacket.context, this, nullptr, rxPacket.size_B,
                    rxPacket.offset_B, rxStream.first, 'd'
                );
            }
        }
    }
    rxStreams.acks.clear();
    rxStreams.map.clear();
    rxStreams.isStreamsChanged = true;
}
