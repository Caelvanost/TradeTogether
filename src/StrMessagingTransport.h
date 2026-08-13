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

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::string GetLocalPlayerName() const;

    private:
        StrMessagingTransport() = default;
        ~StrMessagingTransport();
        StrMessagingTransport(const StrMessagingTransport&) = delete;
        StrMessagingTransport& operator=(const StrMessagingTransport&) = delete;

        void HandleMessage(const STRPM::Message* a_message);
        std::optional<STRPM::RuntimeStatus> QueryRuntimeStatus() const;
        void LogRuntimeStatus(std::string_view a_context) const;
        bool IsUdpBackendActive() const;
        void RefreshLocalConnectionID();

        static void STRPM_CALL OnMessage(
            const STRPM::Message* a_message,
            void* a_userData);

        const STRPM::Interface* _api{ nullptr };
        const STRPM::DiagnosticsInterface* _diagnostics{ nullptr };
        STRPM::ListenerHandle _listener{};
        STRPM::ConnectionID _localConnectionID{ 0 };
        PacketHandler _handler;
        std::atomic_bool _running{ false };
        mutable std::mutex _handlerMutex;
    };
}
