#pragma once

namespace TradeTogether::StrServerDiscovery
{
    // Returns the host from STR's most recently connected server address.
    // The STR port is deliberately ignored; TradeTogether always uses its own
    // UDP port configured by PeerPort (27993 by default).
    [[nodiscard]] std::optional<std::string> ReadLastConnectedHost();
}
