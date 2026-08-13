#pragma once

#include "Config.h"

namespace TradeTogether
{
    class NetworkTransport
    {
    public:
        using PacketHandler = std::function<void(std::string)>;

        static NetworkTransport& GetSingleton();

        bool Start(const Config& a_config, PacketHandler a_handler);
        void Stop();
        bool SendTo(std::string_view a_playerName, std::string_view a_payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _active.load() != ActiveTransport::kNone;
        }

        [[nodiscard]] std::string GetLocalPlayerName() const;
        [[nodiscard]] std::string_view GetActiveTransportName() const noexcept;

    private:
        enum class ActiveTransport
        {
            kNone,
            kSTRPlugin,
            kUDP
        };

        NetworkTransport() = default;
        ~NetworkTransport();
        NetworkTransport(const NetworkTransport&) = delete;
        NetworkTransport& operator=(const NetworkTransport&) = delete;

        std::atomic<ActiveTransport> _active{ ActiveTransport::kNone };
    };
}
