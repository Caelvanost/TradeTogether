#pragma once

namespace TradeTogether
{
    struct Config
    {
        bool networkEnabled{ true };
        bool autoDiscovery{ true };
        bool autoRemoteFromSTR{ false };

        // TradeTogether has its own port so it can run beside Skyrim Together.
        std::uint16_t localPort{ 27993 };
        std::string peerHost{};
        std::uint16_t peerPort{ 27993 };

        std::uint32_t discoveryIntervalMs{ 1000 };
        std::uint32_t peerTimeoutMs{ 10000 };
        std::uint32_t requestTimeoutMs{ 30000 };
        std::uint32_t sessionTimeoutMs{ 300000 };

        static Config Load();
    };
}
