#pragma once

namespace TradeTogether
{
    struct Config
    {
        struct RemotePeer
        {
            std::string host;
            std::uint16_t port{ 27993 };
        };

        bool networkEnabled{ true };
        bool autoDiscovery{ true };
        bool relayMode{ false };
        bool autoRemoteFromSTR{ true };
        bool autoSharedSecretFromSTR{ false };

        // TradeTogether has its own port so it can run beside the other
        // Together synchronization plugins in this workspace.
        std::uint16_t localPort{ 27993 };
        std::uint16_t autoRemotePort{ 27993 };
        std::string peerHost{};
        std::uint16_t peerPort{ 27993 };
        std::vector<RemotePeer> remotePeers;
        std::string sharedSecret;

        std::uint32_t discoveryIntervalMs{ 1000 };
        std::uint32_t peerTimeoutMs{ 10000 };
        std::uint32_t requestTimeoutMs{ 30000 };
        std::uint32_t sessionTimeoutMs{ 300000 };

        static Config Load();
    };
}
