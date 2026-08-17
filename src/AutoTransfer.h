#pragma once

#include "Offer.h"

namespace TradeTogether::AutoTransfer
{
    [[nodiscard]] bool ValidateLocalOffer(
        RE::PlayerCharacter* a_player,
        const Offer& a_offer,
        std::string& a_error);

    [[nodiscard]] bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer& a_remoteOffer,
        std::string& a_error);
}
