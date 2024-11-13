#include "UdspSocket/UdpSocket.hpp"
#include "UdspSocket/UdspSocket.hpp"

#include <iostream>


int32_t main() {
    uint32_t packetId_R = 0;
    uint32_t packetId_r = 0;
    uint32_t packetId_H = 0;
    uint32_t packetId_h = 0;
    uint32_t packetId_M = 0;
    uint32_t packetId_m = 0;
    uint32_t packetId_L = 0;
    uint32_t packetId_l = 0;

    UDSPSocket udsp;
    udsp.setOnConnected([&](UDSPSocket::Connection* c) {
        //udsp.setTxSpeedLimit_B_s(c, 32 * 1000 * 1000);

        udsp.setOnReceived(c, [&](uintptr_t context, UDSPSocket::Connection* connection,
                const void* data, uint64_t size_B, uint64_t offset_B, uint8_t rxStreamId,
                char status) {
            uint32_t* prevPacketId = nullptr;
            switch (rxStreamId) {
            case 'R':   prevPacketId = &packetId_R;     break;
            case 'r':   prevPacketId = &packetId_r;     break;
            case 'H':   prevPacketId = &packetId_H;     break;
            case 'h':   prevPacketId = &packetId_h;     break;
            case 'M':   prevPacketId = &packetId_M;     break;
            case 'm':   prevPacketId = &packetId_m;     break;
            case 'L':   prevPacketId = &packetId_L;     break;
            case 'l':   prevPacketId = &packetId_l;     break;
            case 'C':
                if (status != 's') {
                    return 0;
                }
                udsp.setTestBandwidthState(static_cast<const uint8_t*>(data)[0]);
                return 0;
            default:
                std::cout << "Warning: Unknown streamId=" << rxStreamId << "\n";
                return 0;
            }
            switch (status) {
            case 't': {
                const uint32_t packetId = ++(*prevPacketId);
                std::cout << "Warning: Timed out streamId=" << rxStreamId
                    << " packetId=" << packetId << "\n";
                break;
            }
            case 's': {
                //if (status != 's' or size_B < 4 or offset_B != 0) {
                if (size_B < 4 or offset_B != 0) {
                    std::cout << "Warning: Something went wrong " << status << " "
                        << size_B << " " << offset_B << "\n";
                    break;
                }
                const uint32_t packetId = static_cast<const uint32_t*>(data)[0];
                if (packetId == 0) {
                    *prevPacketId = 0;
                    std::cout << "Info: Started streamId=" << rxStreamId << "\n";
                    break;
                }
                if (packetId - *prevPacketId > 1) {
                    std::cout << "Warning: Loss detected streamId=" << rxStreamId
                        << " diff=" << packetId - *prevPacketId
                        << " this=" << packetId
                        << " prev=" << *prevPacketId << "\n";
                }
                *prevPacketId = packetId;
                break;
            }
            default:
                std::cout << "Warning: Something went wrong " << status << " "
                    << size_B << " " << offset_B << "\n";
                break;
            }
            return 0;
        });
        //udsp.setOnDelivered(c, [](uintptr_t context, UDSPSocket::Connection* c,
        //        const void* data, uint64_t size_B, uint8_t streamId, char status) {
        //    std::cout << "onDelivered: size_B=" << size_B << " streamId="
        //        << static_cast<char>(streamId) << " status=" << status << std::endl;
        //});
    });
    udsp.listen(22222);

    UDPSocket udp;
    while (true) {
        udp.send("UDSPTestServer", 14, 11111);
        udp.process(500);
    }
    return 0;
}
