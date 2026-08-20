#pragma once

#include "Config.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace TradeTogether
{
    class StrMessagingTransport
    {
    public:
        using PacketHandler = std::function<void(std::string)>;

        static StrMessagingTransport& GetSingleton();

        bool Start(const Config& a_config, PacketHandler a_handler);
        void Stop();
        bool SendTo(std::string_view a_playerName, std::string_view a_payload);

        [[nodiscard]] bool IsRunning() const noexcept { return _running.load(); }
        [[nodiscard]] std::string GetLocalPlayerName() const;
        [[nodiscard]] std::optional<std::string> GetMostRecentPeerName();

    private:
        StrMessagingTransport() = default;
        ~StrMessagingTransport();
        StrMessagingTransport(const StrMessagingTransport&) = delete;
        StrMessagingTransport& operator=(const StrMessagingTransport&) = delete;

        static void STRPM_CALL OnMessage(const STRPM::Message* a_message, void* a_userData);
        static void STRPM_CALL OnProxyMapping(const STRPM::ProxyMappingEvent* a_event, void* a_userData);

        void HandleMessage(const STRPM::Message* a_message);
        void HandleProxyMapping(const STRPM::ProxyMappingEvent* a_event);
        void RefreshIdentity();
        std::optional<STRPM::ConnectionID> ResolveConnectionIDForPlayerName(std::string_view a_playerName);
        std::optional<STRPM::RuntimeStatus> QueryRuntimeStatus() const;
        void LogRuntimeStatus(std::string_view a_context) const;

        const STRPM::Interface* _api{ nullptr };
        const STRPM::DiagnosticsInterface* _diagnostics{ nullptr };
        const STRPM::ProxyResolverInterface* _proxyResolver{ nullptr };
        STRPM::ListenerHandle _listener{};
        STRPM::ConnectionID _localConnectionID{ 0 };
        PacketHandler _handler;
        std::atomic_bool _running{ false };
        mutable std::mutex _handlerMutex;
        mutable std::mutex _peerMutex;
        mutable std::mutex _proxyMutex;
        std::optional<std::string> _mostRecentPeerName;
        std::unordered_map<STRPM::ProxyFormID, STRPM::ConnectionID> _proxyConnections;
        std::unordered_map<std::string, STRPM::ConnectionID> _namedConnections;
    };
}
