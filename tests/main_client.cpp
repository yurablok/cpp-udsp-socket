#include "UDSPSocket/UDPSocket.hpp"
#include "UDSPSocket/UDSPSocket.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <vector>
#include <array>

int64_t tick_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

class StreamGenerator {
public:
    std::function<void(const void* data, const uint32_t size_b)> onSend;

    void onDelivered(const uint64_t size_b) {
        if (m_queueSize_b >= size_b) {
            m_queueSize_b -= size_b;
        }
        else {
            m_queueSize_b = 0;
        }
    }
    void start(const uint32_t totalBytesToSend_b, const uint32_t packetSize_b) {
        m_queueSize_b = 0;
        m_totalBytesToSend_b = totalBytesToSend_b;
        m_totalBytesSent_b = 0;
        m_packetSize_b = packetSize_b;
        m_packetsCount = 0;
        m_buffer.resize(m_packetSize_b);
    }
    void process() {
        //TODO: Too big queue causes timeouts, that is useful for debugging
        //TODO: Adaptive queue size limit to the current speed
        //assert(m_buffer.size() >= 4);
        while (m_queueSize_b < m_queueSizeLimit_b) {
            if (isFinished()) {
                return;
            }
            *reinterpret_cast<uint32_t*>(&m_buffer[0]) = m_packetsCount++;
            onSend(m_buffer.data(), m_packetSize_b);
            m_queueSize_b += m_packetSize_b;
            m_totalBytesSent_b += m_packetSize_b;
        }
    }
    float getProgress_prc() const {
        return std::min<uint32_t>(
            (uint64_t(m_totalBytesSent_b) * 1000)
            / uint64_t(m_totalBytesToSend_b),
            1000
        ) * 0.1f;
    }
    bool isFinished() const {
        return m_totalBytesSent_b >= m_totalBytesToSend_b;
    }
private:
    std::vector<uint8_t> m_buffer;
    uint32_t m_queueSizeLimit_b = 16 * 1024 * 1024;
    uint32_t m_queueSize_b = 0;
    uint32_t m_totalBytesToSend_b = 1;
    uint32_t m_totalBytesSent_b = 1;
    uint32_t m_packetSize_b = 0;
    uint32_t m_packetsCount = 0;
};

enum class Step {
    Start,
    TestTxSpeed,
    TestRxSpeed,
    TestRtxSpeed,
    SingleReliable,
    SeveralReliable,
    SingleUnreliable,
    SeveralUnreliable,
    SingleSmallReliable,
    SeveralSmallReliable,
    Finish,
};
struct Client {
    Step step = Step::Start;

    void nextStep() {
        static const std::initializer_list<Step> steps = {
            Step::Start,
            //Step::TestTxSpeed,
            //Step::TestRxSpeed,
            //Step::TestRtxSpeed,
            //Step::SingleReliable,
            Step::SeveralReliable,
            //Step::SingleUnreliable,
            //Step::SeveralUnreliable,
            //Step::SingleSmallReliable,
            //Step::SeveralSmallReliable,
            Step::Finish,
        };
        size_t idx = 0;
        for (; idx < steps.size(); ++idx) {
            if (steps.begin()[idx] == step) {
                break;
            }
        }
        if (idx + 1 >= steps.size()) {
            return;
        }
        if (step == Step::Start) {
            udsp.send(0, connection, "\0", 1, true, 'C');
            udsp.setTestBandwidthState(false);

            udsp.setTxStreamPriority(connection, 'C', 'R');

            udsp.setTxStreamPriority(connection, 'R', 'R');
            udsp.setTxStreamPriority(connection, 'r', 'r');
            udsp.setTxStreamPriority(connection, 'H', 'H');
            udsp.setTxStreamPriority(connection, 'h', 'h');
            udsp.setTxStreamPriority(connection, 'M', 'M');
            udsp.setTxStreamPriority(connection, 'm', 'm');
            udsp.setTxStreamPriority(connection, 'L', 'L');
            udsp.setTxStreamPriority(connection, 'l', 'l');
        }
        step = steps.begin()[idx + 1];

        constexpr uint32_t totalBytesToSend_b
            = 10 // seconds
            * 50 * 1024 * 1024; // 50 MiB/s

        switch (step) {
        case Step::TestTxSpeed:
        case Step::TestRxSpeed:
        case Step::TestRtxSpeed:
            break;
        default:
            udsp.send(0, connection, "\0", 1, true, 'C');
            udsp.setTestBandwidthState(false);
            break;
        }

        switch (step) {
        case Step::TestTxSpeed:
            std::cout << "Info: Started TestTxSpeed\n";
            udsp.send(0, connection, "\0", 1, true, 'C');
            udsp.setTestBandwidthState(true);
            break;
        case Step::TestRxSpeed:
            std::cout << "Info: Started TestRxSpeed\n";
            udsp.send(0, connection, "\1", 1, true, 'C');
            udsp.setTestBandwidthState(false);
            break;
        case Step::TestRtxSpeed:
            std::cout << "Info: Started TestRtxSpeed\n";
            udsp.send(0, connection, "\1", 1, true, 'C');
            udsp.setTestBandwidthState(true);
            break;
        case Step::SingleReliable:
            std::cout << "Info: Started SingleReliable\n";
            stream_R.start(totalBytesToSend_b, 4096);
            break;
        case Step::SeveralReliable:
            std::cout << "Info: Started SeveralReliable\n";
            stream_R.start(totalBytesToSend_b, 4096);
            stream_H.start(totalBytesToSend_b, 4096);
            stream_M.start(totalBytesToSend_b, 4096);
            stream_L.start(totalBytesToSend_b, 4096);
            break;
        case Step::SingleUnreliable:
            std::cout << "Info: Started SingleUnreliable\n";
            stream_r.start(totalBytesToSend_b, 4096);
            break;
        case Step::SeveralUnreliable:
            std::cout << "Info: Started SeveralUnreliable\n";
            stream_r.start(totalBytesToSend_b, 4096);
            stream_h.start(totalBytesToSend_b, 4096);
            stream_m.start(totalBytesToSend_b, 4096);
            stream_l.start(totalBytesToSend_b, 4096);
            break;
        case Step::SingleSmallReliable:
            std::cout << "Info: Started SingleSmallReliable\n";
            stream_R.start(totalBytesToSend_b, 4);
            break;
        case Step::SeveralSmallReliable:
            std::cout << "Info: Started SeveralSmallReliable\n";
            stream_R.start(totalBytesToSend_b, 4);
            stream_H.start(totalBytesToSend_b, 4);
            stream_M.start(totalBytesToSend_b, 4);
            stream_L.start(totalBytesToSend_b, 4);
            break;
        case Step::Finish:
            std::cout << "Info: Finished\n";
            break;
        default:
            break;
        }
    }

    UDSPSocket::Connection* connection = nullptr;
    UDSPSocket udsp;

    StreamGenerator stream_R;
    StreamGenerator stream_r;
    StreamGenerator stream_H;
    StreamGenerator stream_h;
    StreamGenerator stream_M;
    StreamGenerator stream_m;
    StreamGenerator stream_L;
    StreamGenerator stream_l;
};



int32_t main() {
    Client client;

    uint64_t countR_b = 0;
    uint64_t countH_b = 0;
    uint64_t countM_b = 0;
    uint64_t countL_b = 0;

    client.udsp.setOnConnected([&](UDSPSocket::Connection* c) {
        client.step = Step::Start;
        client.connection = c;
        //client.udsp.setTxSpeedLimit_b_s(c, 10 * 1000 * 1000);

        client.udsp.setOnReceived(c, [](uintptr_t context, UDSPSocket::Connection* c,
                const void* data, uint64_t size_b, uintptr_t offset_b, uint8_t streamId,
                char status) {
            std::cout << "onReceived: size_b=" << size_b << " streamId="
                << static_cast<char>(streamId) << std::endl;
            return 0;
        });
        client.udsp.setOnDelivered(c, [&](uintptr_t context, UDSPSocket::Connection* c,
                const void* data, uint64_t size_b, uint8_t streamId, char status) {
            //std::cout << "onDelivered: size_b=" << size_b << " streamId="
            //    << static_cast<char>(streamId) << " status=" << status << std::endl;
            if (status != 's') {
                return;
            }
            switch (streamId) {
            case 'R':
                countR_b += size_b;
                client.stream_R.onDelivered(size_b);
                break;
            case 'r':
                countR_b += size_b;
                client.stream_r.onDelivered(size_b);
                break;
            case 'H':
                countH_b += size_b;
                client.stream_H.onDelivered(size_b);
                break;
            case 'h':
                countH_b += size_b;
                client.stream_h.onDelivered(size_b);
                break;
            case 'M':
                countM_b += size_b;
                client.stream_M.onDelivered(size_b);
                break;
            case 'm':
                countM_b += size_b;
                client.stream_m.onDelivered(size_b);
                break;
            case 'L':
                countL_b += size_b;
                client.stream_L.onDelivered(size_b);
                break;
            case 'l':
                countL_b += size_b;
                client.stream_l.onDelivered(size_b);
                break;
            default:
                break;
            }
        });
    });
    client.udsp.setOnDisconnected([&](UDSPSocket::Connection* c, char reason) {
        client.connection = nullptr;
    });
    client.stream_R.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'R', 10 * 60 * 1000);
    };
    client.stream_r.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'r', 10 * 60 * 1000);
    };
    client.stream_H.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'H', 10 * 60 * 1000);
    };
    client.stream_h.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'h', 10 * 60 * 1000);
    };
    client.stream_M.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'M', 10 * 60 * 1000);
    };
    client.stream_m.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'm', 10 * 60 * 1000);
    };
    client.stream_L.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'L', 10 * 60 * 1000);
    };
    client.stream_l.onSend = [&](const void* data, const uint32_t size_b) {
        client.udsp.send(0, client.connection, data, size_b, true, 'l', 10 * 60 * 1000);
    };

    UDPSocket udp;
    udp.onReceived = [&](void* data, uint32_t size_b, uint16_t port, uint32_t IPv4) {
        if (not client.udsp.isConnected()) {
            client.udsp.connect(22222, IPv4);
        }
    };
    udp.bind(11111);

    //udsp.setTestBandwidthState(true);

    uint32_t counter = 0;
    uint32_t counter10Hz = 0;
    uint8_t value = 0;
    std::ofstream csv("rec.csv");
    const int64_t begin_ms = tick_ms();
    while (true) {
        udp.process(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (client.connection == nullptr) {
            continue;
        }

        if (++counter10Hz >= 10) {
            counter10Hz = 0;
            csv << tick_ms() - begin_ms << ","
                //<< countR_b * 10 << ","
                //<< countH_b * 10 << ","
                //<< countM_b * 10 << ","
                //<< countL_b * 10 << ","
                << countR_b << ","
                << countH_b << ","
                << countM_b << ","
                << countL_b << ","
                << client.udsp.getRxSpeed_b_s(client.connection) << ","
                << client.udsp.getTxSpeed_b_s(client.connection) << ","
                << client.udsp.getRxLoss_prc(client.connection) << ","
                << client.udsp.getTxLoss_prc(client.connection) << "\n";
            countR_b = 0;
            countH_b = 0;
            countM_b = 0;
            countL_b = 0;
            std::cout << std::setprecision(1)
                << "R:" << client.stream_R.getProgress_prc()
                << " H:" << client.stream_H.getProgress_prc()
                << " M:" << client.stream_M.getProgress_prc()
                << " L:" << client.stream_L.getProgress_prc()
                << " r:" << client.stream_r.getProgress_prc()
                << " h:" << client.stream_h.getProgress_prc()
                << " m:" << client.stream_m.getProgress_prc()
                << " l:" << client.stream_l.getProgress_prc()
                << "\n";
        }

        switch (client.step) {
        case Step::Start:
            client.nextStep();
            break;
        case Step::TestTxSpeed:
            if (++counter >= 15 * 10) {
                counter = 0;
                client.nextStep();
            }
            break;
        case Step::TestRxSpeed:
            if (++counter >= 15 * 10) {
                counter = 0;
                client.nextStep();
            }
            break;
        case Step::TestRtxSpeed:
            if (++counter >= 15 * 10) {
                counter = 0;
                client.nextStep();
            }
            break;
        case Step::SingleReliable:
        case Step::SingleSmallReliable:
            client.stream_R.process();
            if (client.stream_R.isFinished()) {
                client.nextStep();
            }
            break;
        case Step::SingleUnreliable:
        //case Step::SingleSmallUnreliable:
            client.stream_r.process();
            if (client.stream_r.isFinished()) {
                client.nextStep();
            }
            break;
        case Step::SeveralReliable:
        case Step::SeveralSmallReliable:
            client.stream_R.process();
            client.stream_H.process();
            client.stream_M.process();
            client.stream_L.process();
            if (client.stream_R.isFinished() and client.stream_H.isFinished()
                    and client.stream_M.isFinished() and client.stream_L.isFinished()) {
                client.nextStep();
            }
            break;
        case Step::Finish:
            client.udsp.disconnect();
            return 0;
        default:
            break;
        }
    }
    return 0;
}
