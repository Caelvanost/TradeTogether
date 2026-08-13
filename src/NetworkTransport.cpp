#include "PCH.h"
#include "NetworkTransport.h"
#include "StrMessagingTransport.h"
#include "UdpTransport.h"

namespace TradeTogether
{
    namespace
    {
        std::string_view TransportModeName(Config::TransportMode a_mode)
        {
            switch (a_mode) {
            case Config::TransportMode::kSTRPlugin:
                return "STRPlugin";
            case Config::TransportMode::kUDP:
                return "UDP";
            case Config::TransportMode::kAuto:
            default:
                return "Auto";
            }
        }
    }

    NetworkTransport& NetworkTransport::GetSingleton()
    {
        static NetworkTransport singleton;
        return singleton;
    }

    NetworkTransport::~NetworkTransport()
    {
        Stop();
    }

    bool NetworkTransport::Start(
        const Config& a_config,
        PacketHandler a_handler)
    {
        if (IsRunning()) {
            return true;
        }
        if (!a_config.networkEnabled || !a_handler) {
            spdlog::warn("TradeTogether network transport is disabled");
            return false;
        }

        spdlog::info(
            "TradeTogether transport selection requested: {}",
            TransportModeName(a_config.transportMode));

        if (a_config.transportMode != Config::TransportMode::kUDP) {
            auto handlerCopy = a_handler;
            if (StrMessagingTransport::GetSingleton().Start(
                    a_config,
                    std::move(handlerCopy))) {
                _active.store(ActiveTransport::kSTRPlugin);
                spdlog::info(
                    "TradeTogether active transport: STRPlugin no-port channel");
                return true;
            }

            if (a_config.transportMode == Config::TransportMode::kSTRPlugin) {
                spdlog::warn(
                    "TradeTogether STRPlugin transport requested but unavailable; UDP fallback disabled");
                return false;
            }

            spdlog::warn(
                "TradeTogether STRPlugin transport unavailable; falling back to legacy UDP");
        }

        if (UdpTransport::GetSingleton().Start(
                a_config,
                std::move(a_handler))) {
            _active.store(ActiveTransport::kUDP);
            spdlog::info("TradeTogether active transport: legacy UDP");
            return true;
        }

        return false;
    }

    void NetworkTransport::Stop()
    {
        const auto active = _active.exchange(ActiveTransport::kNone);
        if (active == ActiveTransport::kSTRPlugin) {
            StrMessagingTransport::GetSingleton().Stop();
        } else if (active == ActiveTransport::kUDP) {
            UdpTransport::GetSingleton().Stop();
        }
    }

    bool NetworkTransport::SendTo(
        std::string_view a_playerName,
        std::string_view a_payload)
    {
        const auto active = _active.load();
        if (active == ActiveTransport::kSTRPlugin) {
            return StrMessagingTransport::GetSingleton().SendTo(
                a_playerName,
                a_payload);
        }
        if (active == ActiveTransport::kUDP) {
            return UdpTransport::GetSingleton().SendTo(
                a_playerName,
                a_payload);
        }
        return false;
    }

    std::string NetworkTransport::GetLocalPlayerName() const
    {
        const auto active = _active.load();
        if (active == ActiveTransport::kSTRPlugin) {
            return StrMessagingTransport::GetSingleton().GetLocalPlayerName();
        }
        return UdpTransport::GetSingleton().GetLocalPlayerName();
    }

    std::string_view NetworkTransport::GetActiveTransportName() const noexcept
    {
        switch (_active.load()) {
        case ActiveTransport::kSTRPlugin:
            return "STRPlugin";
        case ActiveTransport::kUDP:
            return "UDP";
        case ActiveTransport::kNone:
        default:
            return "None";
        }
    }
}
