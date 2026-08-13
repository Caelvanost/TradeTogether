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

    private:
        struct Peer
        {
            sockaddr_in address{};
            std::string name;
            std::string instanceID;
            std::chrono::steady_clock::time_point lastSeen{};
        };

        UdpTransport() = default;
        ~UdpTransport();
        UdpTransport(const UdpTransport&) = delete;
        UdpTransport& operator=(const UdpTransport&) = delete;

        void ReceiverLoop();
        void MaintenanceLoop(std::stop_token a_stopToken);
        void SendHello();
        void SendHelloTo(
            const sockaddr_in& a_destination,
            bool a_useObservedSourcePort);
        bool HandleDiscovery(
            std::string_view a_packet,
            const sockaddr_in& a_source);
        void RegisterPeer(
            const sockaddr_in& a_source,
            std::uint16_t a_advertisedPort,
            std::string_view a_name,
            std::string_view a_instanceID);
        void TouchGameplayPeer(
            std::string_view a_packet,
            const sockaddr_in& a_source);
        void ExpirePeers();
        std::vector<sockaddr_in> SnapshotDestinations(
            std::string_view a_playerName,
            const sockaddr_in* a_excluded = nullptr);
        void RelayGameplayPacket(
            std::string_view a_packet,
            const sockaddr_in& a_source);
        bool SendPacketTo(
            std::string_view a_packet,
            const sockaddr_in& a_destination,
            std::string_view a_operation);
        std::optional<sockaddr_in> ResolveRemotePeer(
            const Config::RemotePeer& a_peer) const;
        void RefreshSkyrimTogetherAutoConfig(bool a_force);
        std::vector<sockaddr_in> SnapshotConfiguredPeers() const;
        std::string GetSharedSecretSnapshot() const;
        std::string SignPacket(std::string a_packet) const;
        bool AuthenticatePacket(std::string_view a_packet) const;
        static std::string RemoveAuthField(std::string_view a_packet);
        std::string MarkRelayed(std::string_view a_packet) const;

        static std::string AddressToString(const sockaddr_in& a_address);

        Config _config{};
        PacketHandler _handler;
        SOCKET _socket{ INVALID_SOCKET };
        sockaddr_in _broadcast{};
        std::vector<sockaddr_in> _configuredPeers;
        mutable std::mutex _configuredPeerMutex;
        mutable std::mutex _authMutex;
        std::string _sharedSecret;
        std::chrono::steady_clock::time_point _lastStrAutoConfigRefresh{};
        std::string _instanceID;

        std::jthread _receiver;
        std::jthread _maintenance;
        std::atomic_bool _running{ false };
        std::mutex _sendMutex;
        std::mutex _peerMutex;
        std::unordered_map<std::string, Peer> _peers;
    };
}
