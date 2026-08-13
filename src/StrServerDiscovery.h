#pragma once

#include "PCH.h"
#include "Config.h"

namespace TradeTogether::StrServerDiscovery
{
    struct ClientState
    {
        std::optional<Config::RemotePeer> remotePeer;
        std::optional<std::string> password;
        std::string rawAddress;
    };

    ClientState ReadClientState(std::uint16_t a_tradeTogetherPort);
    std::optional<std::string> ReadServerPasswordFromConfig();
}
