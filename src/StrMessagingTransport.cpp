#include "PCH.h"
#include "StrMessagingTransport.h"
#include "Protocol.h"

#include <cstring>

namespace TradeTogether
{
    namespace
    {
        constexpr char kChannel[] = "tradetogether.offer.v1";
        constexpr std::string_view kGameplayPrefix = "TTNET|v1|";

        std::string ReadLocalPlayerName()
        {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const auto* name = player->GetName();
                if (name && *name) {
                    return name;
                }
            }
            return "Player";
        }

        bool SameChannel(const STRPM::Message& a_message)
        {
            return a_message.channel && std::strcmp(a_message.channel, kChannel) == 0;
        }

        std::string_view BackendName(STRPM::RuntimeBackend a_backend)
        {
            switch (a_backend) {
            case STRPM::RuntimeBackend::kUdp: return "UDP";
            case STRPM::RuntimeBackend::kStrBridge: return "STRBridge";
            case STRPM::RuntimeBackend::kNone:
            default: return "None";
            }
        }
    }

    StrMessagingTransport& StrMessagingTransport::GetSingleton()
    {
        static StrMessagingTransport singleton;
        return singleton;
    }

    StrMessagingTransport::~StrMessagingTransport()
    {
        Stop();
    }

    std::string StrMessagingTransport::GetLocalPlayerName() const
    {
        return ReadLocalPlayerName();
    }

    std::optional<std::string> StrMessagingTransport::GetMostRecentPeerName()
    {
        std::scoped_lock lock(_peerMutex);
        return _mostRecentPeerName;
    }

    void STRPM_CALL StrMessagingTransport::OnMessage(const STRPM::Message* a_message, void* a_userData)
    {
        if (a_userData) {
            static_cast<StrMessagingTransport*>(a_userData)->HandleMessage(a_message);
        }
    }

    void StrMessagingTransport::RefreshIdentity()
    {
        if (!_api) {
            return;
        }
        const auto localName = GetLocalPlayerName();
        if (_api->setLocalDisplayName) {
            const auto result = _api->setLocalDisplayName(localName.c_str());
            if (result != STRPM::Result::kOk) {
                spdlog::debug("TradeTogether STRPM setLocalDisplayName: {}", STRPM::ResultToString(result));
            }
        }
        if (_api->getLocalConnectionID) {
            STRPM::ConnectionID connectionID = 0;
            const auto result = _api->getLocalConnectionID(&connectionID);
            if (result == STRPM::Result::kOk && connectionID != 0) {
                _localConnectionID = connectionID;
            }
        }
    }

    bool StrMessagingTransport::Start(const Config& a_config, PacketHandler a_handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!a_config.networkEnabled || !a_handler) {
            return false;
        }

        _api = STRPM::LoadFromModule();
        if (!_api) {
            spdlog::error("TradeTogether STRPM API unavailable: STRPluginMessagingAPI.dll not found or incompatible");
            return false;
        }
        _diagnostics = STRPM::LoadDiagnosticsFromModule();

        {
            std::scoped_lock lock(_handlerMutex);
            _handler = std::move(a_handler);
        }
        {
            std::scoped_lock lock(_peerMutex);
            _mostRecentPeerName.reset();
        }

        RefreshIdentity();

        STRPM::ListenerHandle listener{};
        const auto result = _api->registerChannel(kChannel, &StrMessagingTransport::OnMessage, this, &listener);
        if (result != STRPM::Result::kOk) {
            spdlog::error("TradeTogether STRPM registerChannel failed: {}", STRPM::ResultToString(result));
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
            _api = nullptr;
            _diagnostics = nullptr;
            return false;
        }

        _listener = listener;
        _running.store(true);
        spdlog::info(
            "TradeTogether STRPM transport started: channel={} player=\"{}\" connection={}",
            kChannel,
            GetLocalPlayerName(),
            _localConnectionID);
        LogRuntimeStatus("startup");
        return true;
    }

    void StrMessagingTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_api && _api->unregisterChannel && _listener.value != 0) {
            const auto result = _api->unregisterChannel(_listener);
            if (result != STRPM::Result::kOk && result != STRPM::Result::kChannelNotRegistered) {
                spdlog::warn("TradeTogether STRPM unregisterChannel failed: {}", STRPM::ResultToString(result));
            }
        }
        {
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
        }
        {
            std::scoped_lock lock(_peerMutex);
            _mostRecentPeerName.reset();
        }
        _listener = {};
        _localConnectionID = 0;
        _api = nullptr;
        _diagnostics = nullptr;
        spdlog::info("TradeTogether STRPM transport stopped");
    }

    bool StrMessagingTransport::SendTo(std::string_view a_playerName, std::string_view a_payload)
    {
        if (!_running.load() || !_api || !_api->send || a_playerName.empty() || a_payload.empty()) {
            return false;
        }

        RefreshIdentity();
        const auto packet = fmt::format(
            "TTNET|v1|id={}|from={}|{}",
            _localConnectionID,
            Protocol::HexEncode(GetLocalPlayerName()),
            a_payload);
        if (packet.size() > STRPM::kMaxPayloadBytes) {
            spdlog::warn("TradeTogether STRPM packet too large: bytes={}", packet.size());
            return false;
        }

        const std::string targetName(a_playerName);
        const STRPM::Target playerTarget{ STRPM::TargetKind::kPlayer, 0, targetName.c_str() };
        constexpr auto flags = STRPM::kMessageReliable | STRPM::kMessageOrdered;
        auto result = _api->send(kChannel, playerTarget, packet.data(), packet.size(), flags);

        if (result == STRPM::Result::kTargetNotFound) {
            const STRPM::Target partyTarget{ STRPM::TargetKind::kAllPlayers, 0, nullptr };
            result = _api->send(kChannel, partyTarget, packet.data(), packet.size(), flags);
            if (result == STRPM::Result::kOk) {
                spdlog::info("TradeTogether STRPM used all-players fallback for target=\"{}\"", a_playerName);
            }
        }

        if (result != STRPM::Result::kOk) {
            spdlog::warn(
                "TradeTogether STRPM send failed: target=\"{}\" result={}",
                a_playerName,
                STRPM::ResultToString(result));
            LogRuntimeStatus("send failure");
            return false;
        }

        spdlog::info("TradeTogether STRPM packet sent: target=\"{}\" bytes={}", a_playerName, packet.size());
        return true;
    }

    std::optional<STRPM::RuntimeStatus> StrMessagingTransport::QueryRuntimeStatus() const
    {
        if (!_diagnostics || !_diagnostics->getRuntimeStatus) {
            return std::nullopt;
        }
        STRPM::RuntimeStatus status{};
        const auto result = _diagnostics->getRuntimeStatus(&status);
        if (result != STRPM::Result::kOk || status.version != STRPM::kDiagnosticsVersion) {
            return std::nullopt;
        }
        return status;
    }

    void StrMessagingTransport::LogRuntimeStatus(std::string_view a_context) const
    {
        const auto status = QueryRuntimeStatus();
        if (!status) {
            spdlog::info("TradeTogether STRPM diagnostics unavailable ({})", a_context);
            return;
        }
        spdlog::info(
            "TradeTogether STRPM status {}: backend={} bridgeAvailable={} bridgeActive={} knownPeers={} configuredPeers={}",
            a_context,
            BackendName(status->activeBackend),
            status->strBridgeAvailable,
            status->strBridgeActive,
            status->knownPeerCount,
            status->configuredPeerCount);
    }

    void StrMessagingTransport::HandleMessage(const STRPM::Message* a_message)
    {
        if (!_running.load() || !a_message || !SameChannel(*a_message) || !a_message->data || a_message->size == 0 || a_message->size > STRPM::kMaxPayloadBytes) {
            return;
        }

        std::string packet(static_cast<const char*>(a_message->data), a_message->size);
        if (!packet.starts_with(kGameplayPrefix)) {
            return;
        }

        std::optional<std::string> peerName;
        if (const auto encodedFrom = Protocol::ReadField(packet, "from")) {
            peerName = Protocol::HexDecode(*encodedFrom);
        }
        if ((!peerName || peerName->empty()) && a_message->sender.displayName && *a_message->sender.displayName) {
            peerName = std::string(a_message->sender.displayName);
        }
        if (peerName && !peerName->empty() && !Protocol::EqualsInsensitive(*peerName, GetLocalPlayerName())) {
            std::scoped_lock lock(_peerMutex);
            _mostRecentPeerName = *peerName;
        }

        PacketHandler handler;
        {
            std::scoped_lock lock(_handlerMutex);
            handler = _handler;
        }
        if (handler) {
            handler(std::move(packet));
        }
    }
}
