#pragma once

namespace TradeTogether::Trade
{
    bool Initialize();
    void Shutdown();
    void Reset();
    void Update();

    bool RequestCrosshairActorTrade();
    void HandleNetworkPacket(std::string a_packet);
}
