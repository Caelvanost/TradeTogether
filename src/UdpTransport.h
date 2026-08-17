#pragma once

#include "Config.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace TradeTogether
{
    class UdpTransport
    {
    public:
        using PacketHandler = std::function<void(std::string)>;

        static UdpTransport& GetSingleton();

        bool Start(const Config& a_config, PacketHandler a_handler);
        void Stop();

        // Sends directly to the discovered owner when possible. Before LAN
        // discovery completes, one broadcast is used and receivers filter the
        // packet by its encoded `to` field.
        bool SendTo(std::string_view a_playerName, std::string_view a_payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::string GetLocalPlayerName() const;
        [[nodiscard]] std::optional<std::string> GetMostRecentPeerName();

    private:
        struct Peer
        {
            sockaddr_in address{};
            std::string name;
            std::chrono::steady_clock::time_point lastSeen{};
        };

        UdpTransport() = default;
        ~UdpTransport();
        UdpTransport(const UdpTransport&) = delete;
        UdpTransport& operator=(const UdpTransport&) = delete;

        void ReceiverLoop();
        void DiscoveryLoop(std::stop_token a_stopToken);
        void SendHello();
        void SendHelloTo(const sockaddr_in& a_destination);
        bool HandleDiscovery(
            std::string_view a_packet,
            const sockaddr_in& a_source);
        void TouchGameplayPeer(
            std::string_view a_packet,
            const sockaddr_in& a_source);
        void ExpirePeers();
        std::vector<sockaddr_in> SnapshotPeers(std::string_view a_playerName);

        static std::string AddressToString(const sockaddr_in& a_address);

        Config _config{};
        PacketHandler _handler;
        SOCKET _socket{ INVALID_SOCKET };
        sockaddr_in _broadcast{};
        sockaddr_in _manualPeer{};
        bool _hasManualPeer{ false };
        std::string _instanceID;

        std::jthread _receiver;
        std::jthread _discovery;
        std::atomic_bool _running{ false };
        std::mutex _sendMutex;
        std::mutex _peerMutex;
        std::unordered_map<std::string, Peer> _peers;
    };
}
