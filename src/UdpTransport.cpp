#include "PCH.h"
#include "UdpTransport.h"
#include "Protocol.h"
#include "StrServerDiscovery.h"

namespace TradeTogether
{
    namespace
    {
        constexpr std::string_view kDiscoveryPrefix =
            "TTDISC|v1|HELLO|";
        constexpr std::string_view kGameplayPrefix =
            "TTNET|v1|";

        std::optional<std::uint16_t> ParsePort(std::string_view a_text)
        {
            try {
                const auto value = std::stoul(std::string(a_text));
                if (value == 0 || value > 65535) {
                    return std::nullopt;
                }
                return static_cast<std::uint16_t>(value);
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    UdpTransport& UdpTransport::GetSingleton()
    {
        static UdpTransport singleton;
        return singleton;
    }

    UdpTransport::~UdpTransport()
    {
        Stop();
    }

    std::string UdpTransport::GetLocalPlayerName() const
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const auto* name = player->GetName();
            if (name && *name) {
                return name;
            }
        }
        return "Player";
    }

    std::optional<std::string> UdpTransport::GetMostRecentPeerName()
    {
        std::scoped_lock lock(_peerMutex);

        const Peer* mostRecent = nullptr;
        for (const auto& [key, peer] : _peers) {
            if (peer.name.empty() ||
                Protocol::EqualsInsensitive(peer.name, GetLocalPlayerName())) {
                continue;
            }
            if (!mostRecent || peer.lastSeen > mostRecent->lastSeen) {
                mostRecent = std::addressof(peer);
            }
        }

        if (!mostRecent || mostRecent->name == "Peer") {
            return std::nullopt;
        }
        return mostRecent->name;
    }

    std::string UdpTransport::AddressToString(const sockaddr_in& a_address)
    {
        std::array<char, INET_ADDRSTRLEN> ip{};
        InetNtopA(
            AF_INET,
            &a_address.sin_addr,
            ip.data(),
            static_cast<DWORD>(ip.size()));
        return fmt::format(
            "{}:{}",
            ip.data(),
            ntohs(a_address.sin_port));
    }

    bool UdpTransport::ConfigureManualPeer(
        std::string_view a_host,
        bool a_fromSTR)
    {
        if (a_host.empty()) {
            return false;
        }

        {
            std::scoped_lock lock(_manualPeerMutex);
            if (_hasManualPeer &&
                Protocol::EqualsInsensitive(_manualPeerHost, a_host)) {
                return true;
            }
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* addresses = nullptr;
        const auto portText = std::to_string(_config.peerPort);
        const auto status = getaddrinfo(
            std::string(a_host).c_str(),
            portText.c_str(),
            &hints,
            &addresses);
        if (status != 0 || !addresses) {
            spdlog::warn(
                "TradeTogether could not resolve {} remote host \"{}\" on UDP {}",
                a_fromSTR ? "STR" : "manual",
                a_host,
                _config.peerPort);
            if (addresses) {
                freeaddrinfo(addresses);
            }
            return false;
        }

        std::optional<sockaddr_in> resolved;
        for (auto* item = addresses; item; item = item->ai_next) {
            if (item->ai_family == AF_INET &&
                item->ai_addrlen >= static_cast<int>(sizeof(sockaddr_in))) {
                resolved = *reinterpret_cast<sockaddr_in*>(item->ai_addr);
                break;
            }
        }
        freeaddrinfo(addresses);

        if (!resolved) {
            return false;
        }

        {
            std::scoped_lock lock(_manualPeerMutex);
            _manualPeer = *resolved;
            _hasManualPeer = true;
            _manualPeerHost = std::string(a_host);
        }

        spdlog::info(
            "TradeTogether {} remote configured: STR/manual host=\"{}\" endpoint={}",
            a_fromSTR ? "automatic STR" : "manual",
            a_host,
            AddressToString(*resolved));
        return true;
    }

    void UdpTransport::RefreshAutoRemoteFromSTR()
    {
        if (!_config.autoRemoteFromSTR || _config.autoDiscovery) {
            return;
        }

        const auto host = StrServerDiscovery::ReadLastConnectedHost();
        if (!host || host->empty()) {
            return;
        }

        bool changed = false;
        {
            std::scoped_lock lock(_manualPeerMutex);
            changed = !_hasManualPeer ||
                      !Protocol::EqualsInsensitive(_manualPeerHost, *host);
        }

        if (changed && ConfigureManualPeer(*host, true)) {
            SendHello();
        }
    }

    std::optional<sockaddr_in> UdpTransport::SnapshotManualPeer()
    {
        std::scoped_lock lock(_manualPeerMutex);
        if (!_hasManualPeer) {
            return std::nullopt;
        }
        return _manualPeer;
    }

    bool UdpTransport::Start(
        const Config& a_config,
        PacketHandler a_handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!a_config.networkEnabled || !a_handler) {
            spdlog::warn("Trade confirmation transport is disabled");
            return false;
        }

        _config = a_config;
        _handler = std::move(a_handler);

        WSADATA winsock{};
        if (const auto result = WSAStartup(MAKEWORD(2, 2), &winsock);
            result != 0) {
            spdlog::error("Winsock initialization failed: {}", result);
            return false;
        }

        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_socket == INVALID_SOCKET) {
            spdlog::error("UDP socket creation failed: {}", WSAGetLastError());
            WSACleanup();
            return false;
        }

        BOOL enabled = TRUE;
        if (setsockopt(
                _socket,
                SOL_SOCKET,
                SO_BROADCAST,
                reinterpret_cast<const char*>(&enabled),
                sizeof(enabled)) == SOCKET_ERROR) {
            spdlog::error("UDP broadcast setup failed: {}", WSAGetLastError());
            closesocket(_socket);
            _socket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }
        setsockopt(
            _socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled),
            sizeof(enabled));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(_config.localPort);
        if (bind(
                _socket,
                reinterpret_cast<sockaddr*>(&local),
                sizeof(local)) == SOCKET_ERROR) {
            spdlog::error(
                "UDP bind failed on port {}: {}",
                _config.localPort,
                WSAGetLastError());
            closesocket(_socket);
            _socket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        DWORD timeoutMs = 250;
        setsockopt(
            _socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs),
            sizeof(timeoutMs));

        _broadcast = {};
        _broadcast.sin_family = AF_INET;
        _broadcast.sin_port = htons(_config.localPort);
        _broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

        {
            std::scoped_lock lock(_manualPeerMutex);
            _hasManualPeer = false;
            _manualPeerHost.clear();
        }
        if (!_config.autoDiscovery && !_config.peerHost.empty()) {
            ConfigureManualPeer(_config.peerHost, false);
        }

        _instanceID = fmt::format(
            "{:08X}-{:016X}",
            GetCurrentProcessId(),
            GetTickCount64());
        _running.store(true);
        _receiver = std::jthread(
            [this](std::stop_token) {
                ReceiverLoop();
            });

        if (_config.autoRemoteFromSTR) {
            RefreshAutoRemoteFromSTR();
        }

        if (_config.autoDiscovery ||
            _config.autoRemoteFromSTR ||
            SnapshotManualPeer().has_value()) {
            _discovery = std::jthread(
                [this](std::stop_token a_token) {
                    DiscoveryLoop(a_token);
                });
            SendHello();
        }

        spdlog::info(
            "Trade UDP started: player=\"{}\" port={} discovery={} autoRemoteFromSTR={} remoteConfigured={} instance={}",
            GetLocalPlayerName(),
            _config.localPort,
            _config.autoDiscovery ? 1 : 0,
            _config.autoRemoteFromSTR ? 1 : 0,
            SnapshotManualPeer().has_value() ? 1 : 0,
            _instanceID);
        return true;
    }

    void UdpTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_receiver.joinable()) {
            _receiver.request_stop();
        }
        if (_discovery.joinable()) {
            _discovery.request_stop();
        }
        if (_socket != INVALID_SOCKET) {
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }
        if (_receiver.joinable()) {
            _receiver.join();
        }
        if (_discovery.joinable()) {
            _discovery.join();
        }

        {
            std::scoped_lock lock(_peerMutex);
            _peers.clear();
        }
        {
            std::scoped_lock lock(_manualPeerMutex);
            _hasManualPeer = false;
            _manualPeerHost.clear();
        }
        _handler = {};
        WSACleanup();
        spdlog::info("Trade UDP stopped");
    }

    void UdpTransport::SendHello()
    {
        if (!_running.load()) {
            return;
        }

        if (_config.autoDiscovery) {
            SendHelloTo(_broadcast);
        } else if (const auto peer = SnapshotManualPeer()) {
            SendHelloTo(*peer);
        }
    }

    void UdpTransport::SendHelloTo(const sockaddr_in& a_destination)
    {
        const auto packet = fmt::format(
            "TTDISC|v1|HELLO|id={}|name={}|port={}",
            _instanceID,
            Protocol::HexEncode(GetLocalPlayerName()),
            _config.localPort);

        std::scoped_lock lock(_sendMutex);
        const auto sent = sendto(
            _socket,
            packet.data(),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&a_destination),
            sizeof(a_destination));
        if (sent == SOCKET_ERROR && _running.load()) {
            spdlog::warn(
                "Discovery send failed to {}: {}",
                AddressToString(a_destination),
                WSAGetLastError());
        }
    }

    bool UdpTransport::HandleDiscovery(
        std::string_view a_packet,
        const sockaddr_in& a_source)
    {
        if (!a_packet.starts_with(kDiscoveryPrefix)) {
            return false;
        }

        const auto id = Protocol::ReadField(a_packet, "id");
        const auto encodedName = Protocol::ReadField(a_packet, "name");
        const auto portText = Protocol::ReadField(a_packet, "port");
        if (!id || !encodedName || !portText || *id == _instanceID) {
            return true;
        }

        const auto name = Protocol::HexDecode(*encodedName);
        const auto port = ParsePort(*portText);
        if (!name || name->empty() || !port) {
            spdlog::warn(
                "Malformed trade discovery packet from {}",
                AddressToString(a_source));
            return true;
        }

        auto peerAddress = a_source;
        // On LAN, the advertised listening port is authoritative. Across NAT,
        // the observed source port is authoritative because the router may
        // have rewritten the client's local port.
        if (_config.autoDiscovery) {
            peerAddress.sin_port = htons(*port);
        }

        const auto key = fmt::format(
            "{}|{}",
            *id,
            AddressToString(peerAddress));

        bool inserted = false;
        {
            std::scoped_lock lock(_peerMutex);
            auto [iterator, isNew] = _peers.try_emplace(key);
            inserted = isNew;
            iterator->second.address = peerAddress;
            iterator->second.name = *name;
            iterator->second.lastSeen = std::chrono::steady_clock::now();
        }

        if (inserted) {
            spdlog::info(
                "Trade peer discovered: player=\"{}\" address={}",
                *name,
                AddressToString(peerAddress));
            SendHelloTo(peerAddress);
        }
        return true;
    }

    void UdpTransport::TouchGameplayPeer(
        std::string_view a_packet,
        const sockaddr_in& a_source)
    {
        if (!a_packet.starts_with(kGameplayPrefix)) {
            return;
        }

        const auto id = Protocol::ReadField(a_packet, "id");
        const auto encodedName = Protocol::ReadField(a_packet, "from");
        const auto name = encodedName ?
            Protocol::HexDecode(*encodedName) : std::nullopt;
        const auto key = fmt::format(
            "{}|{}",
            id.value_or("gameplay"),
            AddressToString(a_source));

        std::scoped_lock lock(_peerMutex);
        auto& peer = _peers[key];
        peer.address = a_source;
        peer.name = name.value_or("Peer");
        peer.lastSeen = std::chrono::steady_clock::now();
    }

    void UdpTransport::ExpirePeers()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timeout =
            std::chrono::milliseconds(_config.peerTimeoutMs);

        std::scoped_lock lock(_peerMutex);
        for (auto iterator = _peers.begin(); iterator != _peers.end();) {
            if (now - iterator->second.lastSeen > timeout) {
                iterator = _peers.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::vector<sockaddr_in> UdpTransport::SnapshotPeers(
        std::string_view a_playerName)
    {
        std::vector<sockaddr_in> result;
        std::unordered_set<std::string> endpoints;

        std::scoped_lock lock(_peerMutex);
        for (const auto& [key, peer] : _peers) {
            if (!Protocol::EqualsInsensitive(peer.name, a_playerName)) {
                continue;
            }

            const auto endpoint = AddressToString(peer.address);
            if (endpoints.insert(endpoint).second) {
                result.push_back(peer.address);
            }
        }
        return result;
    }

    bool UdpTransport::SendTo(
        std::string_view a_playerName,
        std::string_view a_payload)
    {
        if (!_running.load() ||
            _socket == INVALID_SOCKET ||
            a_playerName.empty() ||
            a_payload.empty()) {
            return false;
        }

        const auto packet = fmt::format(
            "TTNET|v1|id={}|from={}|{}",
            _instanceID,
            Protocol::HexEncode(GetLocalPlayerName()),
            a_payload);

        std::vector<sockaddr_in> destinations = SnapshotPeers(a_playerName);
        if (destinations.empty()) {
            if (_config.autoDiscovery) {
                destinations.push_back(_broadcast);
            } else if (const auto peer = SnapshotManualPeer()) {
                destinations.push_back(*peer);
            }
        }

        if (destinations.empty()) {
            spdlog::warn(
                "Trade packet dropped: no route to player \"{}\"",
                a_playerName);
            return false;
        }

        std::size_t sentCount = 0;
        {
            std::scoped_lock lock(_sendMutex);
            for (const auto& destination : destinations) {
                const auto sent = sendto(
                    _socket,
                    packet.data(),
                    static_cast<int>(packet.size()),
                    0,
                    reinterpret_cast<const sockaddr*>(&destination),
                    sizeof(destination));
                if (sent == SOCKET_ERROR) {
                    spdlog::warn(
                        "Trade packet send failed to {}: {}",
                        AddressToString(destination),
                        WSAGetLastError());
                    continue;
                }
                ++sentCount;
            }
        }

        spdlog::info(
            "Trade packet sent: player=\"{}\" destinations={} bytes={}",
            a_playerName,
            sentCount,
            packet.size());
        return sentCount > 0;
    }

    void UdpTransport::DiscoveryLoop(std::stop_token a_stopToken)
    {
        while (!a_stopToken.stop_requested() && _running.load()) {
            if (_config.autoRemoteFromSTR) {
                RefreshAutoRemoteFromSTR();
            }
            SendHello();
            ExpirePeers();

            const auto interval =
                std::chrono::milliseconds(_config.discoveryIntervalMs);
            auto elapsed = std::chrono::milliseconds(0);
            constexpr auto slice = std::chrono::milliseconds(100);
            while (elapsed < interval &&
                   !a_stopToken.stop_requested() &&
                   _running.load()) {
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
        }
    }

    void UdpTransport::ReceiverLoop()
    {
        std::array<char, 8192> buffer{};
        while (_running.load()) {
            sockaddr_in source{};
            int sourceLength = sizeof(source);
            const auto received = recvfrom(
                _socket,
                buffer.data(),
                static_cast<int>(buffer.size() - 1),
                0,
                reinterpret_cast<sockaddr*>(&source),
                &sourceLength);

            if (received == SOCKET_ERROR) {
                const auto error = WSAGetLastError();
                if (!_running.load()) {
                    break;
                }
                if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                    continue;
                }
                spdlog::warn("Trade UDP receive failed: {}", error);
                continue;
            }
            if (received <= 0) {
                continue;
            }

            const std::string packet(
                buffer.data(),
                static_cast<std::size_t>(received));

            if (HandleDiscovery(packet, source)) {
                continue;
            }
            if (!packet.starts_with(kGameplayPrefix)) {
                continue;
            }
            if (const auto id = Protocol::ReadField(packet, "id");
                id && *id == _instanceID) {
                continue;
            }

            TouchGameplayPeer(packet, source);
            if (_handler) {
                _handler(packet);
            }
        }
    }
}
