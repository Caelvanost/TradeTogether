#include "PCH.h"
#include "StrMessagingTransport.h"
#include "Protocol.h"

#include <cstring>

namespace TradeTogether
{
    namespace
    {
        constexpr char kChannel[] = "chaos.trade_together.offer.v1";
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
            return a_message.channel &&
                   std::strcmp(a_message.channel, kChannel) == 0;
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

    void STRPM_CALL StrMessagingTransport::OnMessage(
        const STRPM::Message* a_message,
        void* a_userData)
    {
        if (!a_userData) {
            return;
        }

        static_cast<StrMessagingTransport*>(a_userData)->HandleMessage(
            a_message);
    }

    bool StrMessagingTransport::Start(
        const Config& a_config,
        PacketHandler a_handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!a_config.networkEnabled || !a_handler) {
            return false;
        }

        _api = STRPM::LoadFromModule();
        if (!_api) {
            spdlog::info(
                "TradeTogether STR Plugin Messaging API unavailable");
            return false;
        }

        {
            std::scoped_lock lock(_handlerMutex);
            _handler = std::move(a_handler);
        }

        const auto localName = GetLocalPlayerName();
        if (_api->setLocalDisplayName) {
            const auto result =
                _api->setLocalDisplayName(localName.c_str());
            if (result != STRPM::Result::kOk) {
                spdlog::warn(
                    "TradeTogether STRPM setLocalDisplayName failed: {}",
                    STRPM::ResultToString(result));
            }
        }

        _localConnectionID = 0;
        if (_api->getLocalConnectionID) {
            const auto result =
                _api->getLocalConnectionID(&_localConnectionID);
            if (result != STRPM::Result::kOk) {
                spdlog::info(
                    "TradeTogether STRPM local connection id unavailable yet: {}",
                    STRPM::ResultToString(result));
            }
        }

        STRPM::ListenerHandle listener{};
        const auto result = _api->registerChannel(
            kChannel,
            &StrMessagingTransport::OnMessage,
            this,
            &listener);
        if (result != STRPM::Result::kOk) {
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
            spdlog::warn(
                "TradeTogether STRPM registerChannel failed: {}",
                STRPM::ResultToString(result));
            return false;
        }

        _listener = listener;
        _running.store(true);
        spdlog::info(
            "TradeTogether STRPM transport started: channel={} player=\"{}\" connection={}",
            kChannel,
            localName,
            _localConnectionID);
        return true;
    }

    void StrMessagingTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_api && _api->unregisterChannel && _listener.value != 0) {
            const auto result = _api->unregisterChannel(_listener);
            if (result != STRPM::Result::kOk &&
                result != STRPM::Result::kChannelNotRegistered) {
                spdlog::warn(
                    "TradeTogether STRPM unregisterChannel failed: {}",
                    STRPM::ResultToString(result));
            }
        }

        {
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
        }
        _listener = {};
        _localConnectionID = 0;
        _api = nullptr;
        spdlog::info("TradeTogether STRPM transport stopped");
    }

    bool StrMessagingTransport::SendTo(
        std::string_view a_playerName,
        std::string_view a_payload)
    {
        if (!_running.load() ||
            !_api ||
            !_api->send ||
            a_playerName.empty() ||
            a_payload.empty()) {
            return false;
        }

        const auto packet = fmt::format(
            "TTNET|v1|id={}|from={}|relay=0|{}",
            _localConnectionID,
            Protocol::HexEncode(GetLocalPlayerName()),
            a_payload);
        if (packet.size() > STRPM::kMaxPayloadBytes) {
            spdlog::warn(
                "TradeTogether STRPM packet too large: bytes={}",
                packet.size());
            return false;
        }

        const std::string targetName(a_playerName);
        const STRPM::Target playerTarget{
            STRPM::TargetKind::kPlayer,
            0,
            targetName.c_str()
        };
        constexpr auto flags =
            STRPM::kMessageReliable | STRPM::kMessageOrdered;

        auto result = _api->send(
            kChannel,
            playerTarget,
            packet.data(),
            packet.size(),
            flags);

        if (result == STRPM::Result::kTargetNotFound) {
            const STRPM::Target partyTarget{
                STRPM::TargetKind::kAllPlayers,
                0,
                nullptr
            };
            result = _api->send(
                kChannel,
                partyTarget,
                packet.data(),
                packet.size(),
                flags);
            if (result == STRPM::Result::kOk) {
                spdlog::info(
                    "TradeTogether STRPM sent through party fallback: target=\"{}\" bytes={}",
                    a_playerName,
                    packet.size());
                return true;
            }
        }

        if (result != STRPM::Result::kOk) {
            spdlog::warn(
                "TradeTogether STRPM send failed target=\"{}\" result={}",
                a_playerName,
                STRPM::ResultToString(result));
            return false;
        }

        spdlog::info(
            "TradeTogether STRPM packet sent: target=\"{}\" bytes={}",
            a_playerName,
            packet.size());
        return true;
    }

    void StrMessagingTransport::HandleMessage(const STRPM::Message* a_message)
    {
        if (!_running.load() ||
            !a_message ||
            !SameChannel(*a_message) ||
            !a_message->data ||
            a_message->size == 0 ||
            a_message->size > STRPM::kMaxPayloadBytes) {
            return;
        }

        std::string packet(
            static_cast<const char*>(a_message->data),
            a_message->size);
        if (!packet.starts_with(kGameplayPrefix)) {
            spdlog::warn(
                "TradeTogether STRPM ignored non-trade payload from \"{}\"",
                a_message->sender.displayName ?
                    a_message->sender.displayName : "unknown");
            return;
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
