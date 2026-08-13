#include "PCH.h"
#include "UdpTransport.h"
#include "Protocol.h"
#include "StrServerDiscovery.h"

#include <bcrypt.h>

namespace TradeTogether
{
    namespace
    {
        constexpr std::string_view kDiscoveryPrefix =
            "TTDISC|v1|HELLO|";
        constexpr std::string_view kGameplayPrefix =
            "TTNET|v1|";
        constexpr std::string_view kAuthField =
            "|auth=";

        std::optional<std::uint16_t> ParsePort(
            const std::optional<std::string>& a_value)
        {
            if (!a_value || a_value->empty()) {
                return std::nullopt;
            }

            try {
                const auto value = std::stoul(*a_value);
                if (value == 0 || value > 65535) {
                    return std::nullopt;
                }
                return static_cast<std::uint16_t>(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        std::optional<std::string> ComputeHmacSha256(
            std::string_view a_secret,
            std::string_view a_data)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hashHandle = nullptr;
            std::vector<UCHAR> hashObject;
            std::vector<UCHAR> digest;

            auto status = BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                BCRYPT_ALG_HANDLE_HMAC_FLAG);
            if (!BCRYPT_SUCCESS(status)) {
                return std::nullopt;
            }

            DWORD objectLength = 0;
            DWORD digestLength = 0;
            DWORD written = 0;
            status = BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &written,
                0);
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&digestLength),
                    sizeof(digestLength),
                    &written,
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                hashObject.resize(objectLength);
                digest.resize(digestLength);
                status = BCryptCreateHash(
                    algorithm,
                    &hashHandle,
                    hashObject.data(),
                    static_cast<ULONG>(hashObject.size()),
                    reinterpret_cast<PUCHAR>(
                        const_cast<char*>(a_secret.data())),
                    static_cast<ULONG>(a_secret.size()),
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptHashData(
                    hashHandle,
                    reinterpret_cast<PUCHAR>(
                        const_cast<char*>(a_data.data())),
                    static_cast<ULONG>(a_data.size()),
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptFinishHash(
                    hashHandle,
                    digest.data(),
                    static_cast<ULONG>(digest.size()),
                    0);
            }

            if (hashHandle) {
                BCryptDestroyHash(hashHandle);
            }
            BCryptCloseAlgorithmProvider(algorithm, 0);

            if (!BCRYPT_SUCCESS(status)) {
                return std::nullopt;
            }

            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(digest.size() * 2);
            for (const auto byte : digest) {
                result.push_back(hex[(byte >> 4) & 0x0F]);
                result.push_back(hex[byte & 0x0F]);
            }
            return result;
        }

        bool SameEndpoint(
            const sockaddr_in& a_left,
            const sockaddr_in& a_right)
        {
            return a_left.sin_addr.s_addr == a_right.sin_addr.s_addr &&
                   a_left.sin_port == a_right.sin_port;
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

    std::string UdpTransport::RemoveAuthField(std::string_view a_packet)
    {
        const auto auth = a_packet.rfind(kAuthField);
        if (auth == std::string_view::npos) {
            return std::string(a_packet);
        }

        const auto next =
            a_packet.find('|', auth + kAuthField.size());
        std::string result(a_packet.substr(0, auth));
        if (next != std::string_view::npos) {
            result.append(a_packet.substr(next));
        }
        return result;
    }

    std::string UdpTransport::SignPacket(std::string a_packet) const
    {
        a_packet = RemoveAuthField(a_packet);
        const auto sharedSecret = GetSharedSecretSnapshot();
        if (sharedSecret.empty()) {
            return a_packet;
        }

        const auto tag = ComputeHmacSha256(sharedSecret, a_packet);
        if (!tag) {
            spdlog::error("TradeTogether HMAC generation failed");
            return {};
        }
        return fmt::format("{}|auth={}", a_packet, *tag);
    }

    bool UdpTransport::AuthenticatePacket(std::string_view a_packet) const
    {
        const auto sharedSecret = GetSharedSecretSnapshot();
        if (sharedSecret.empty()) {
            return true;
        }

        const auto auth = a_packet.rfind(kAuthField);
        if (auth == std::string_view::npos) {
            return false;
        }

        const auto tagStart = auth + kAuthField.size();
        const auto tagEnd = a_packet.find('|', tagStart);
        const auto supplied = a_packet.substr(
            tagStart,
            tagEnd == std::string_view::npos ?
                std::string_view::npos : tagEnd - tagStart);
        const auto unsignedPacket = RemoveAuthField(a_packet);
        const auto expected =
            ComputeHmacSha256(sharedSecret, unsignedPacket);
        if (!expected || supplied.size() != expected->size()) {
            return false;
        }

        unsigned char difference = 0;
        for (std::size_t index = 0; index < supplied.size(); ++index) {
            difference |= static_cast<unsigned char>(
                supplied[index] ^ (*expected)[index]);
        }
        return difference == 0;
    }

    std::string UdpTransport::MarkRelayed(std::string_view a_packet) const
    {
        auto result = RemoveAuthField(a_packet);
        constexpr std::string_view marker = "|relay=0|";
        const auto position = result.find(marker);
        if (position != std::string::npos) {
            result.replace(position, marker.size(), "|relay=1|");
        } else {
            result.insert(kGameplayPrefix.size(), "relay=1|");
        }
        return SignPacket(std::move(result));
    }

    std::optional<sockaddr_in> UdpTransport::ResolveRemotePeer(
        const Config::RemotePeer& a_peer) const
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* addresses = nullptr;
        const auto port = std::to_string(a_peer.port);
        if (getaddrinfo(
                a_peer.host.c_str(),
                port.c_str(),
                &hints,
                &addresses) != 0 ||
            !addresses) {
            return std::nullopt;
        }

        std::optional<sockaddr_in> result;
        for (auto* item = addresses; item; item = item->ai_next) {
            if (item->ai_family == AF_INET &&
                item->ai_addrlen >= static_cast<int>(sizeof(sockaddr_in))) {
                result = *reinterpret_cast<sockaddr_in*>(item->ai_addr);
                break;
            }
        }
        freeaddrinfo(addresses);
        return result;
    }

    std::string UdpTransport::GetSharedSecretSnapshot() const
    {
        std::scoped_lock lock(_authMutex);
        return _sharedSecret;
    }

    std::vector<sockaddr_in> UdpTransport::SnapshotConfiguredPeers() const
    {
        std::scoped_lock lock(_configuredPeerMutex);
        return _configuredPeers;
    }

    void UdpTransport::RefreshSkyrimTogetherAutoConfig(bool a_force)
    {
        if (!_config.autoRemoteFromSTR &&
            !_config.autoSharedSecretFromSTR) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!a_force &&
            _lastStrAutoConfigRefresh.time_since_epoch().count() != 0 &&
            now - _lastStrAutoConfigRefresh < std::chrono::seconds(5)) {
            return;
        }
        _lastStrAutoConfigRefresh = now;

        if (_config.autoSharedSecretFromSTR &&
            _config.sharedSecret.empty()) {
            const auto password = _config.relayMode ?
                StrServerDiscovery::ReadServerPasswordFromConfig() :
                StrServerDiscovery::ReadClientState(
                    _config.autoRemotePort).password;

            if (password && !password->empty()) {
                bool changed = false;
                {
                    std::scoped_lock lock(_authMutex);
                    if (_sharedSecret != *password) {
                        _sharedSecret = *password;
                        changed = true;
                    }
                }

                if (changed) {
                    spdlog::info(
                        "TradeTogether STR shared secret auto-loaded source={}",
                        _config.relayMode ? "STServer.ini" : "localStorage");
                }
            }
        }

        if (!_config.autoRemoteFromSTR ||
            _config.relayMode) {
            return;
        }

        const auto state =
            StrServerDiscovery::ReadClientState(_config.autoRemotePort);
        if (!state.remotePeer) {
            if (a_force) {
                spdlog::info(
                    "TradeTogether STR auto remote pending: no saved direct-connect address found");
            }
            return;
        }

        const auto resolved = ResolveRemotePeer(*state.remotePeer);
        if (!resolved) {
            spdlog::warn(
                "TradeTogether STR auto remote resolution failed address=\"{}\" host=\"{}\" port={}",
                state.rawAddress,
                state.remotePeer->host,
                state.remotePeer->port);
            return;
        }

        const auto endpoint = AddressToString(*resolved);
        bool inserted = false;
        {
            std::scoped_lock lock(_configuredPeerMutex);
            const auto duplicate = std::any_of(
                _configuredPeers.begin(),
                _configuredPeers.end(),
                [&](const sockaddr_in& a_existing) {
                    return SameEndpoint(a_existing, *resolved);
                });

            if (!duplicate) {
                _configuredPeers.push_back(*resolved);
                inserted = true;
            }
        }

        if (inserted) {
            spdlog::info(
                "TradeTogether STR auto remote configured address=\"{}\" endpoint={}",
                state.rawAddress,
                endpoint);
        }
    }

    bool UdpTransport::SendPacketTo(
        std::string_view a_packet,
        const sockaddr_in& a_destination,
        std::string_view a_operation)
    {
        if (a_packet.empty() || _socket == INVALID_SOCKET) {
            return false;
        }

        std::scoped_lock lock(_sendMutex);
        const auto sent = sendto(
            _socket,
            a_packet.data(),
            static_cast<int>(a_packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&a_destination),
            sizeof(a_destination));
        if (sent == SOCKET_ERROR) {
            if (_running.load()) {
                spdlog::warn(
                    "TradeTogether {} failed to {}: {}",
                    a_operation,
                    AddressToString(a_destination),
                    WSAGetLastError());
            }
            return false;
        }
        return true;
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
        {
            std::scoped_lock lock(_authMutex);
            _sharedSecret = _config.sharedSecret;
        }
        _lastStrAutoConfigRefresh = {};

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

        std::vector<sockaddr_in> configuredPeers;
        std::unordered_set<std::string> configuredEndpoints;
        for (const auto& peer : _config.remotePeers) {
            const auto resolved = ResolveRemotePeer(peer);
            if (!resolved) {
                spdlog::warn(
                    "TradeTogether remote peer resolution failed host=\"{}\" port={}",
                    peer.host,
                    peer.port);
                continue;
            }

            const auto endpoint = AddressToString(*resolved);
            if (configuredEndpoints.insert(endpoint).second) {
                configuredPeers.push_back(*resolved);
                spdlog::info(
                    "TradeTogether remote peer configured host=\"{}\" endpoint={}",
                    peer.host,
                    endpoint);
            }
        }
        {
            std::scoped_lock lock(_configuredPeerMutex);
            _configuredPeers = std::move(configuredPeers);
        }

        _instanceID = fmt::format(
            "{:08X}-{:016X}",
            GetCurrentProcessId(),
            GetTickCount64());

        RefreshSkyrimTogetherAutoConfig(true);
        const auto configuredPeerCount = SnapshotConfiguredPeers().size();
        const auto sharedSecret = GetSharedSecretSnapshot();

        _running.store(true);
        _receiver = std::jthread(
            [this](std::stop_token) {
                ReceiverLoop();
            });
        if (_config.autoDiscovery ||
            configuredPeerCount > 0 ||
            _config.relayMode ||
            _config.autoRemoteFromSTR ||
            _config.autoSharedSecretFromSTR) {
            _maintenance = std::jthread(
                [this](std::stop_token a_token) {
                    MaintenanceLoop(a_token);
                });
        }

        spdlog::info(
            "Trade UDP started: player=\"{}\" port={} auto={} relay={} auth={} configuredPeers={} instance={}",
            GetLocalPlayerName(),
            _config.localPort,
            _config.autoDiscovery ? 1 : 0,
            _config.relayMode ? 1 : 0,
            sharedSecret.empty() ? 0 : 1,
            configuredPeerCount,
            _instanceID);

        if (_config.relayMode && sharedSecret.empty()) {
            spdlog::warn(
                "TradeTogether relay is unauthenticated; set Network/SharedSecret before exposing UDP port {} to the Internet",
                _config.localPort);
        }

        SendHello();
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
        if (_maintenance.joinable()) {
            _maintenance.request_stop();
        }
        if (_socket != INVALID_SOCKET) {
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }
        if (_receiver.joinable()) {
            _receiver.join();
        }
        if (_maintenance.joinable()) {
            _maintenance.join();
        }

        {
            std::scoped_lock lock(_peerMutex);
            _peers.clear();
        }
        {
            std::scoped_lock lock(_configuredPeerMutex);
            _configuredPeers.clear();
        }
        {
            std::scoped_lock lock(_authMutex);
            _sharedSecret.clear();
        }
        _handler = {};
        WSACleanup();
        spdlog::info("Trade UDP stopped");
    }

    void UdpTransport::SendHello()
    {
        if (!_running.load() || _socket == INVALID_SOCKET) {
            return;
        }

        if (_config.autoDiscovery) {
            SendHelloTo(_broadcast, false);
        }
        for (const auto& peer : SnapshotConfiguredPeers()) {
            SendHelloTo(peer, true);
        }
    }

    void UdpTransport::SendHelloTo(
        const sockaddr_in& a_destination,
        bool a_useObservedSourcePort)
    {
        const auto packet = SignPacket(fmt::format(
            "TTDISC|v1|HELLO|id={}|name={}|port={}|observed={}",
            _instanceID,
            Protocol::HexEncode(GetLocalPlayerName()),
            _config.localPort,
            a_useObservedSourcePort ? 1 : 0));
        SendPacketTo(packet, a_destination, "discovery TX");
    }

    void UdpTransport::RegisterPeer(
        const sockaddr_in& a_source,
        std::uint16_t a_advertisedPort,
        std::string_view a_name,
        std::string_view a_instanceID)
    {
        if (a_instanceID.empty() || a_instanceID == _instanceID) {
            return;
        }

        auto peerAddress = a_source;
        peerAddress.sin_port = htons(a_advertisedPort);
        const auto key = fmt::format(
            "{}|{}",
            a_instanceID,
            AddressToString(peerAddress));

        bool inserted = false;
        {
            std::scoped_lock lock(_peerMutex);
            auto [iterator, isNew] = _peers.try_emplace(key);
            inserted = isNew;
            iterator->second.address = peerAddress;
            iterator->second.name = std::string(a_name);
            iterator->second.instanceID = std::string(a_instanceID);
            iterator->second.lastSeen = std::chrono::steady_clock::now();
        }

        if (inserted) {
            spdlog::info(
                "Trade peer discovered: player=\"{}\" address={} instance={}",
                a_name,
                AddressToString(peerAddress),
                a_instanceID);
            SendHelloTo(peerAddress, true);
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
        const auto advertisedPort = ParsePort(
            Protocol::ReadField(a_packet, "port"));
        const auto observed = Protocol::ReadField(a_packet, "observed");
        const auto port =
            observed && *observed == "1" ?
                ntohs(a_source.sin_port) : advertisedPort.value_or(0);

        const auto name = encodedName ?
            Protocol::HexDecode(*encodedName) : std::nullopt;
        if (!id || !name || name->empty() || port == 0) {
            spdlog::warn(
                "Malformed trade discovery packet from {}",
                AddressToString(a_source));
            return true;
        }

        RegisterPeer(a_source, port, *name, *id);
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
        peer.instanceID = id.value_or(key);
        peer.lastSeen = std::chrono::steady_clock::now();
    }

    void UdpTransport::ExpirePeers()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timeout =
            std::chrono::milliseconds(_config.peerTimeoutMs);

        std::vector<std::string> expired;
        {
            std::scoped_lock lock(_peerMutex);
            for (auto iterator = _peers.begin();
                 iterator != _peers.end();) {
                if (now - iterator->second.lastSeen > timeout) {
                    expired.push_back(fmt::format(
                        "\"{}\" {}",
                        iterator->second.name,
                        AddressToString(iterator->second.address)));
                    iterator = _peers.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }

        for (const auto& peer : expired) {
            spdlog::info("Trade peer expired: {}", peer);
        }
    }

    std::vector<sockaddr_in> UdpTransport::SnapshotDestinations(
        std::string_view a_playerName,
        const sockaddr_in* a_excluded)
    {
        std::vector<sockaddr_in> result;
        std::unordered_set<std::string> endpoints;

        const auto configuredPeers = SnapshotConfiguredPeers();
        result.reserve(configuredPeers.size());
        for (const auto& peer : configuredPeers) {
            if (a_excluded && SameEndpoint(peer, *a_excluded)) {
                continue;
            }
            const auto endpoint = AddressToString(peer);
            if (endpoints.insert(endpoint).second) {
                result.push_back(peer);
            }
        }

        std::scoped_lock lock(_peerMutex);
        result.reserve(result.size() + _peers.size());
        for (const auto& [key, peer] : _peers) {
            if (!a_playerName.empty() &&
                !Protocol::EqualsInsensitive(peer.name, a_playerName)) {
                continue;
            }
            if (a_excluded && SameEndpoint(peer.address, *a_excluded)) {
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

        const auto packet = SignPacket(fmt::format(
            "TTNET|v1|id={}|from={}|relay=0|{}",
            _instanceID,
            Protocol::HexEncode(GetLocalPlayerName()),
            a_payload));
        if (packet.empty()) {
            return false;
        }

        auto destinations = SnapshotDestinations(a_playerName);
        if (destinations.empty() && _config.autoDiscovery) {
            destinations.push_back(_broadcast);
        }

        if (destinations.empty()) {
            spdlog::warn(
                "Trade packet dropped: no route to player \"{}\"",
                a_playerName);
            return false;
        }

        std::size_t sentCount = 0;
        for (const auto& destination : destinations) {
            if (SendPacketTo(packet, destination, "TX")) {
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

    void UdpTransport::RelayGameplayPacket(
        std::string_view a_packet,
        const sockaddr_in& a_source)
    {
        if (!_config.relayMode || !a_packet.starts_with(kGameplayPrefix)) {
            return;
        }

        const auto relay = Protocol::ReadField(a_packet, "relay");
        if (relay && *relay != "0") {
            return;
        }

        const auto packet = MarkRelayed(a_packet);
        if (packet.empty()) {
            return;
        }

        const auto destinations = SnapshotDestinations({}, &a_source);
        std::size_t sentCount = 0;
        for (const auto& destination : destinations) {
            if (SendPacketTo(packet, destination, "RELAY")) {
                ++sentCount;
            }
        }

        spdlog::info(
            "Trade relay source={} destinations={} sender=\"{}\"",
            AddressToString(a_source),
            sentCount,
            Protocol::HexDecode(
                Protocol::ReadField(a_packet, "from").value_or(""))
                .value_or("Peer"));
    }

    void UdpTransport::MaintenanceLoop(std::stop_token a_stopToken)
    {
        while (!a_stopToken.stop_requested() && _running.load()) {
            RefreshSkyrimTogetherAutoConfig(false);
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
        // A full 24-line offer can exceed 4 KiB once item names are hex encoded.
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
            const bool discoveryPacket = packet.starts_with(kDiscoveryPrefix);
            const bool gameplayPacket = packet.starts_with(kGameplayPrefix);
            if (!discoveryPacket && !gameplayPacket) {
                continue;
            }

            if (!AuthenticatePacket(packet)) {
                spdlog::warn(
                    "Trade UDP authentication failed from {}",
                    AddressToString(source));
                continue;
            }

            if (HandleDiscovery(packet, source)) {
                continue;
            }

            if (const auto id = Protocol::ReadField(packet, "id");
                id && *id == _instanceID) {
                continue;
            }

            TouchGameplayPeer(packet, source);
            RelayGameplayPacket(packet, source);
            if (_handler) {
                _handler(packet);
            }
        }
    }
}
