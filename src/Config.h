#pragma once

namespace TradeTogether
{
    struct Config
    {
        bool networkEnabled{ true };
        bool autoDiscovery{ true };

        // TradeTogether has its own port so it can run beside the other
        // Together synchronization plugins in this workspace.
        std::uint16_t localPort{ 27993 };
        std::string peerHost{};
        std::uint16_t peerPort{ 27993 };

        std::uint32_t discoveryIntervalMs{ 1000 };
        std::uint32_t peerTimeoutMs{ 10000 };
        std::uint32_t requestTimeoutMs{ 30000 };

        static Config Load();
    };
}
