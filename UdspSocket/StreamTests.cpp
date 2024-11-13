#include "Impl.hpp"


void UDSPSocket::Impl::testStreams() {
# if not defined(NDEBUG) and defined(UDSP_TEST_STREAMS)
    {
        std::deque<uint32_t> fifo;
        assert(Connection::findPacketIdx(fifo, 5) == SIZE_MAX);
        fifo = { 0, 1, 2, 3, 4 };
        assert(Connection::findPacketIdx(fifo, 5) == SIZE_MAX);
        assert(Connection::findPacketIdx(fifo, 0) == 0);
        assert(Connection::findPacketIdx(fifo, 4) == 4);
        fifo = { UINT32_MAX - 1, UINT32_MAX, 0, 1, 2 };
        assert(Connection::findPacketIdx(fifo, UINT32_MAX - 2) == SIZE_MAX);
        assert(Connection::findPacketIdx(fifo, UINT32_MAX - 1) == 0);
        assert(Connection::findPacketIdx(fifo, UINT32_MAX) == 1);
        assert(Connection::findPacketIdx(fifo, 0) == 2);
        assert(Connection::findPacketIdx(fifo, 1) == 3);
        assert(Connection::findPacketIdx(fifo, 2) == 4);
        assert(Connection::findPacketIdx(fifo, 3) == SIZE_MAX);
    }
    {
        RxStream rxStream;
        // t1: |0| 1 2 3
        rxStream.updateFrontPacketId(true, 0);
        assert(rxStream.frontPacketId == 0);
        assert(rxStream.isReceivedBefore(UINT32_MAX));
        assert(not rxStream.isReceivedBefore(0));
        assert(not rxStream.isReceivedBefore(1));
        assert(rxStream.get(0, 0).id == 0);

        // t2: 0 |1| 2 3
        rxStream.updateFrontPacketId(false, 1);
        assert(rxStream.frontPacketId == 0);
        assert(rxStream.isReceivedBefore(UINT32_MAX));
        assert(not rxStream.isReceivedBefore(0));
        assert(not rxStream.isReceivedBefore(1));
        assert(rxStream.get(0, 1).id == 1);
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 0);
        assert(rxStream.fifo[1].id == 1);

        // t4: 0 1 2 |3|
        rxStream.updateFrontPacketId(false, 3);
        assert(not rxStream.isReceivedBefore(3));
        assert(rxStream.get(0, 3).id == 3);
        assert(rxStream.fifo.size() == 4);
        assert(rxStream.fifo[2].id == 2);
        assert(rxStream.fifo[3].id == 3);

        // t3: 0 1 |2| 3
        rxStream.updateFrontPacketId(false, 2);
        assert(not rxStream.isReceivedBefore(2));
        assert(rxStream.get(0, 2).id == 2);
        assert(rxStream.fifo.size() == 4);
        assert(rxStream.fifo[2].id == 2);
        assert(rxStream.fifo[3].id == 3);
    }
    {
        // 0 t, 1 s, 2 t, 3 s
        Connection a(this);
        //RxStream rxStream;
        auto& rxStream = a.rxStreams.map[0];
        rxStream.isReliable = true;
        // t1: 0 |1| 2 3
        rxStream.updateFrontPacketId(false, 1);
        assert(rxStream.frontPacketId == 0);
        assert(rxStream.isReceivedBefore(UINT32_MAX));
        assert(not rxStream.isReceivedBefore(0));
        {
            assert(not rxStream.isReceivedBefore(1));
            auto& packet = rxStream.get(0, 1);
            assert(packet.id == 1);
            packet.isReceived = true;
        }
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 0);
        assert(rxStream.fifo[1].id == 1);
        a.processRxFifo(rxStream, 0);
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 0);
        assert(rxStream.fifo[1].id == 1);

        // t2: 1 2 |3|
        rxStream.updateFrontPacketId(false, 3);
        assert(rxStream.frontPacketId == 0);
        {
            assert(not rxStream.isReceivedBefore(3));
            auto& packet = rxStream.get(0, 3);
            assert(packet.id == 3);
            packet.isReceived = true;
        }
        assert(rxStream.fifo.size() == 4);
        assert(rxStream.fifo[0].id == 0);
        assert(rxStream.fifo[1].id == 1);
        assert(rxStream.fifo[2].id == 2);
        assert(rxStream.fifo[3].id == 3);
        a.processRxFifo(rxStream, 0);
        assert(rxStream.frontPacketId == 0);
        assert(rxStream.fifo.size() == 4);

        // t3: |1| 2 3
        rxStream.updateFrontPacketId(true, 1);
        assert(rxStream.frontPacketId == 1);
        assert(rxStream.isReceivedBefore(UINT32_MAX));
        assert(rxStream.isReceivedBefore(0));
        assert(not rxStream.isReceivedBefore(1));
        assert(rxStream.get(0, 1).id == 1);
        assert(rxStream.fifo.size() == 4);
        assert(rxStream.fifo[1].id == 1);
        a.processRxFifo(rxStream, 0);
        assert(rxStream.frontPacketId == 2);
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 2);
        assert(rxStream.fifo[1].id == 3);

        // t4: 2 |3|
        rxStream.updateFrontPacketId(false, 3);
        assert(rxStream.frontPacketId == 2);
        assert(rxStream.isReceivedBefore(1));
        assert(not rxStream.isReceivedBefore(2));
        assert(not rxStream.isReceivedBefore(3));
        assert(rxStream.get(0, 3).id == 3);
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 2);
        assert(rxStream.fifo[1].id == 3);
        a.processRxFifo(rxStream, 0);
        assert(rxStream.frontPacketId == 2);
        assert(rxStream.fifo.size() == 2);
        assert(rxStream.fifo[0].id == 2);
        assert(rxStream.fifo[1].id == 3);

        // t5: |3|
        rxStream.updateFrontPacketId(true, 3);
        assert(rxStream.frontPacketId == 3);
        assert(rxStream.isReceivedBefore(2));
        assert(not rxStream.isReceivedBefore(3));
        assert(rxStream.get(0, 3).id == 3);
        a.processRxFifo(rxStream, 0);
        assert(rxStream.frontPacketId == 4);
        assert(rxStream.fifo.size() == 0);
    }
    const uint32_t backupRxPacketBufferSizeThreshold_B = rxPacketBufferSizeThreshold_B;
    rxPacketBufferSizeThreshold_B = 0x100000;
    //const int64_t now_us = tick_us();
    int64_t now_us = 1000000;
    Connection a(this);
    Connection b(this);
    a.RTT_us = 2000;
    b.RTT_us = 2000;
    a.txBuffer.resize(128);
    b.txBuffer.resize(128);
    size_t offset_B = 0;
    size_t written_B = 0;
    size_t readed_B = 0;
    char result = 0;

    // single small packet, reliable realtime, no acknowlege

    uint32_t count = 0;
    setTxStreamPriority_ts(&a, 3, 'R');
    send_ts(0, &a, "A", 1, true, 3, 5000, now_us);
    a.doCommands();
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        assert(size_B == 1);
        assert(static_cast<const char*>(data)[0] == 'A');
        assert(streamId == 3);
        assert(status == 't');
        ++count;
    };
    now_us += 10 * 1000000;
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) == 0); // timed out
    assert(count == 1);
    count = 0;
    send_ts(0, &a, "A", 1, true, 3, 5000, now_us);
    a.doCommands();
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) != 0); // writted
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) == 0); // nothing
    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status) {
        switch (count) {
        case 0:
            assert(status == 't');
            break;
        case 1:
            assert(status == 's');
            assert(size_B == 1);
            assert(offset_B == 0);
            assert(static_cast<const char*>(data)[0] == 'A');
            break;
        default:
            break;
        }
        ++count;
        return 0;
    };
    assert(b.readChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) != 0); // readed
    assert(b.readChunk(now_us, &a.txBuffer[0], 0) == 0); // nothing
    assert(count == 2);

    // second attempt, with acknowlege

    now_us += 1 * 1000000;
    a.nextDatagram();
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) != 0); // writted again
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) == 0); // nothing
    assert(b.readChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) != 0); // readed and ignored
    assert(count == 2); // already received
    assert(b.writeChunk(now_us, &b.txBuffer[0], b.txBuffer.size()) != 0); // writted ack
    assert(b.writeChunk(now_us, &b.txBuffer[0], b.txBuffer.size()) == 0); // nothing
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        assert(size_B == 1);
        assert(static_cast<const char*>(data)[0] == 'A');
        assert(streamId == 3);
        assert(status == 's');
        ++count;
    };
    assert(a.readChunk(now_us, &b.txBuffer[0], b.txBuffer.size()) != 0); // readed ack
    assert(count == 3);
    assert(a.writeChunk(now_us, &a.txBuffer[0], a.txBuffer.size()) == 0); // nothing
    assert(a.readChunk(now_us, &b.txBuffer[0], b.txBuffer.size()) != 0); // readed ack
    assert(count == 3);

    // 1 s, 2 t, 3 s

    now_us += 1 * 1000000;
    send_ts(0, &a, "1", 1, true, 3, 5000, now_us);
    send_ts(0, &a, "2", 1, true, 3, 1000, now_us);
    send_ts(0, &a, "3", 1, true, 3, 5000, now_us);
    a.doCommands();
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted "1"

    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status) {
        switch (count++) {
        case 0:
            assert(status == 's');
            assert(static_cast<const char*>(data)[0] == '1');
            break;
        case 1:
            assert(status == 't');
            break;
        case 2:
            assert(status == 's');
            assert(static_cast<const char*>(data)[0] == '3');
            break;
        default:
            assert(false);
            break;
        }
        return 0;
    };
    count = 0;
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed "1"
    assert(count == 1);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read

    now_us += 2 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted "1"
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // timed out "2", writted "3"
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed "1" and ingored
    assert(result == 0);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // timed out "2", readed "3"
    assert(result == 0);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    assert(count == 1);

    b.nextDatagram();
    std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted "1" ack
    // ignored "3" ack

    a.onDelivered = nullptr;
    offset_B = 0;
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed "1" ack
    assert(count == 1);

    now_us += 1 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted "3"
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed "3" and ingored
    assert(count == 3);

    a.onDisconnected();
    b.onDisconnected();

    // two small packets of each reliable priority
    // R0 R1
    // H2 H3
    // M4 M6   M4 M5
    // M5 M7   M6 M7
    // L8 L9

    now_us += 1 * 1000000;
    setTxStreamPriority_ts(&a, 5, 'M');
    send_ts(0, &a, "3", 1, true, 5, 5000, now_us);
    send_ts(0, &a, "6", 1, true, 5, 5000, now_us);
    setTxStreamPriority_ts(&a, 7, 'L');
    send_ts(0, &a, "8", 1, true, 7, 5000, now_us);
    send_ts(0, &a, "9", 1, true, 7, 5000, now_us);
    setTxStreamPriority_ts(&a, 6, 'M');
    send_ts(0, &a, "5", 1, true, 6, 5000, now_us);
    send_ts(0, &a, "7", 1, true, 6, 5000, now_us);
    setTxStreamPriority_ts(&a, 4, 'H');
    send_ts(0, &a, "2", 1, true, 4, 5000, now_us);
    send_ts(0, &a, "4", 1, true, 4, 5000, now_us);
    setTxStreamPriority_ts(&a, 3, 'R');
    send_ts(0, &a, "0", 1, true, 3, 5000, now_us);
    send_ts(0, &a, "1", 1, true, 3, 5000, now_us);
    a.doCommands();
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = 0;
    for (uint8_t i = 0; i < 10; ++i) {
        written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
        assert(written_B != 0);
        offset_B += written_B;
    }
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0);

    count = 0;
    result = 0;
    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status) {
        assert(status == 's');
        assert(size_B == 1);
        assert(offset_B == 0);
        result = static_cast<const char*>(data)[0];
        count += result;
        return 0;
    };
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0);
    assert(result == '0');
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0);
    assert(result == '1');
    offset_B += readed_B;
    for (uint8_t i = 0; i < 10 - 2; ++i) {
        readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
        assert(readed_B != 0);
        offset_B += readed_B;
    }
    b.onReceived = nullptr;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0);
    assert(count == 0 + '0' + '1' + '2' + '3' + '4' + '5' + '6' + '7' + '8' + '9');

    a.nextDatagram();
    std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
    offset_B = 0;
    for (uint8_t i = 0; i < 10; ++i) {
        written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
        assert(written_B != 0);
        offset_B += written_B;
    }
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0);

    count = 0;
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        assert(status == 's');
        assert(size_B == 1);
        count += static_cast<const char*>(data)[0];
    };
    offset_B = 0;
    for (uint8_t i = 0; i < 10; ++i) {
        readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
        assert(readed_B != 0);
        offset_B += readed_B;
    }
    assert(count == 0 + '0' + '1' + '2' + '3' + '4' + '5' + '6' + '7' + '8' + '9');
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B == 0);
    //assert(a.txStreams.countRealtimeInFifo == 0);
    //assert(a.txStreams.countHighInFifo == 0);
    //assert(a.txStreams.countMediumInFifo == 0);
    //assert(a.txStreams.countLowInFifo == 0);
    for (const auto& stream : a.txStreams.map) {
        assert(stream.second.fifo.empty());
        assert(stream.second.fifoShadow.empty());
    }

    // single big packet (3 pieces), reliable realtime, a few losses

    std::vector<uint8_t> v256(256);
    for (size_t i = 0; i < v256.size(); ++i) {
        v256[i] = uint8_t(i);
    }
    now_us += 1 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    setTxStreamPriority_ts(&a, 3, 'R');
    send_ts(0, &a, v256.data(), v256.size(), false, 3, 5000, now_us);
    a.doCommands();
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted BeginOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the first PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the second PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the third PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // no other packets in the TX queue

    now_us += 1 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted BeginOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the first PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the second PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the second PieceOfPacket
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    b.nextDatagram();
    std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted RepeatInfo
    offset_B += written_B;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    offset_B = 0;
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed RepeatInfo
    offset_B += readed_B;
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    now_us += 1 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted BeginOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the first PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed BeginOfPacket
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the first PieceOfPacket
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    b.nextDatagram();
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the second PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the second PieceOfPacket
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    b.nextDatagram();
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the third PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // no other packets in the TX queue

    result = 0;
    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status) {
        assert(status == 's');
        assert(size_B == v256.size());
        assert(offset_B == 0);
        assert(rxStreamId == 3);
        for (size_t i = 0; i < v256.size(); ++i) {
            assert(static_cast<const uint8_t*>(data)[i] == v256[i]);
        }
        result = 's';
        return 0;
    };
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the third PieceOfPacket
    assert(result == 's');
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    b.nextDatagram();
    std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted ack
    offset_B += written_B;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    now_us += 1 * 1000000;
    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted BeginOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the first PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    result = 0;
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        assert(size_B == v256.size());
        assert(streamId == 3);
        assert(status == 's');
        for (size_t i = 0; i < v256.size(); ++i) {
            assert(static_cast<const uint8_t*>(data)[i] == v256[i]);
        }
        result = 's';
    };
    offset_B = 0;
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed ack
    assert(result == 's');
    offset_B += readed_B;
    readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read

    result = 0;
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed BeginOfPacket
    assert(result == 0);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the first PieceOfPacket
    assert(result == 0);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    assert(result == 0);
    b.nextDatagram();
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted ack
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    // single big packet, reliable realtime, start sending, rx timed out, continue sending

    now_us += 1 * 1000000;
    a.nextDatagram();
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        assert(size_B == v256.size());
        assert(streamId == 3);
        assert(status == 'd');
        result = 'd';
    };
    send_ts(0, &a, v256.data(), v256.size(), false, 3, 5000, now_us);
    a.doCommands();
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted BeginOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the first PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    rxPacketBufferSizeThreshold_B = 0;
    count = 0;
    result = 0;
    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId, char status) {
        assert(rxStreamId == 3);
        switch (status) {
        case 'p':
            assert(offset_B == count);
            for (size_t i = 0; i < size_B; ++i) {
                assert(static_cast<const uint8_t*>(data)[i] == count);
                ++count;
            }
            result = 'p';
            break;
        //case 's':
        //    assert(size_B == v256.size());
        //    assert(offset_B == 0);
        //    for (size_t i = 0; i < v256.size(); ++i) {
        //        assert(static_cast<const uint8_t*>(data)[i] == v256[i]);
        //    }
        //    result = 's';
        //    break;
        case 't':
            result = 't';
            break;
        case 'd':
            result = 'd';
            break;
        default:
            assert(false);
            break;
        }
        return 0;
    };
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed BeginOfPacket
    assert(result == 0);
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the first PieceOfPacket
    assert(result == 'p');
    result = 0;
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    assert(result == 0);

    now_us += 1 * 1000000;
    b.nextDatagram();
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write
    assert(result == 't');

    a.nextDatagram();
    std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
    offset_B = 0;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted the second PieceOfPacket
    offset_B += written_B;
    written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(written_B == 0); // the buffer is full

    result = 0;
    offset_B = 0;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B != 0); // readed the second PieceOfPacket
    offset_B += readed_B;
    readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
    assert(readed_B == 0); // nothing to read
    assert(result == 0);
    b.nextDatagram();
    std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
    offset_B = 0;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B != 0); // writted RepeatInfo
    offset_B += written_B;
    written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
    assert(written_B == 0); // nothing to write

    result = 0;
    a.onDisconnected();
    assert(result == 'd');
    result = 0;
    b.onDisconnected();
    assert(result == 0);

    //

    rxPacketBufferSizeThreshold_B = 0x100000;
    std::vector<uint32_t> v1024u32(1024);
    for (size_t i = 0; i < v1024u32.size(); ++i) {
        v1024u32[i] = i;
    }
    setTxStreamPriority_ts(&a, 'r', 'r');
    setTxStreamPriority_ts(&a, 'R', 'R');
    setTxStreamPriority_ts(&a, 'h', 'h');
    setTxStreamPriority_ts(&a, 'H', 'H');
    setTxStreamPriority_ts(&a, 'm', 'm');
    setTxStreamPriority_ts(&a, 'M', 'M');
    setTxStreamPriority_ts(&a, 'l', 'l');
    setTxStreamPriority_ts(&a, 'L', 'L');
    send_ts(0, &a, "hello", sizeof("hello") - 1, true, 'R', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'R', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'r', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'H', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'h', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'M', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'm', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'L', 5000, now_us);
    send_ts(0, &a, v1024u32.data(), v1024u32.size() * sizeof(uint32_t), true, 'l', 5000, now_us);
    a.doCommands();

    count = 0;
    // onDelivered: size_B=4096 streamId=72 status=s
    // onDelivered: size_B=5 streamId=82 status=t
    // onDelivered: size_B=4096 streamId=82 status=t
    // onDelivered: size_B=4096 streamId=114 status=t
    // onDelivered: size_B=4096 streamId=77 status=t
    // onDelivered: size_B=4096 streamId=109 status=t
    // onDelivered: size_B=4096 streamId=76 status=t
    // onDelivered: size_B=4096 streamId=108 status=t
    a.onDelivered = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint8_t streamId, char status) {
        std::cout << "onDelivered: size=" << size_B << " streamId=" << streamId
            << " status=" << status << "\n";
        ++count;
    };

    // onReceived: size_B=5 streamId=82
    // onReceived: size_B=4096 streamId=82
    // onReceived: size_B=4096 streamId=72
    // onReceived: size_B=4096 streamId=77
    // onReceived: size_B=4096 streamId=104
    // onReceived: size_B=0 streamId=72
    // onReceived: size_B=4096 streamId=72
    // onReceived: size_B=4096 streamId=76
    // onReceived: size_B=0 streamId=77
    // onReceived: size_B=0 streamId=104
    b.onReceived = [&](uintptr_t context, Connection* connection, const void* data,
            uint64_t size_B, uint64_t offset_B, uint8_t streamId, char status) {
        std::cout << "onReceived: size=" << size_B << " streamId=" << streamId << "\n";
        ++count;
        return 0;
    };

    for (uint32_t i = 0; i < 1000; ++i) { // 100 ms
        now_us += 100;
        a.nextDatagram();
        b.nextDatagram();

        std::fill(a.txBuffer.begin(), a.txBuffer.end(), 0);
        offset_B = 0;
        do {
            written_B = a.writeChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
            offset_B += written_B;
        } while (written_B > 0);

        offset_B = 0;
        do {
            readed_B = b.readChunk(now_us, &a.txBuffer[offset_B], a.txBuffer.size() - offset_B);
            offset_B += readed_B;
        } while (readed_B > 0);

        std::fill(b.txBuffer.begin(), b.txBuffer.end(), 0);
        offset_B = 0;
        do {
            written_B = b.writeChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
            offset_B += written_B;
        } while (written_B > 0);

        offset_B = 0;
        do {
            readed_B = a.readChunk(now_us, &b.txBuffer[offset_B], b.txBuffer.size() - offset_B);
            offset_B += readed_B;
        } while (readed_B > 0);
    }

    //TODO:
    // 1. txBuffer 100 B, packet 110 B, writted BeginOfPacket and PieceOfPacket
    // 2. txBuffer 128 B, same packet, writted SmallPacket
    //SOLUTION?: if (not packet.isStarted)

    rxPacketBufferSizeThreshold_B = backupRxPacketBufferSizeThreshold_B;
    //onDelivered = nullptr;
    //onReceived = nullptr;
# endif // NDEBUG
}
