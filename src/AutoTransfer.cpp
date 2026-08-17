#include "PCH.h"
#include "AutoTransfer.h"

namespace TradeTogether::AutoTransfer
{
    namespace
    {
        RE::TESBoundObject* LookupObject(RE::FormID a_formID)
        {
            return RE::TESForm::LookupByID<RE::TESBoundObject>(a_formID);
        }

        std::int32_t GetCount(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object)
        {
            if (!a_player || !a_object) {
                return 0;
            }

            const auto counts = a_player->GetInventoryCounts();
            const auto iterator = counts.find(a_object);
            return iterator != counts.end() ? iterator->second : 0;
        }
    }

    bool ValidateLocalOffer(
        RE::PlayerCharacter* a_player,
        const Offer& a_offer,
        std::string& a_error)
    {
        if (!a_player) {
            a_error = "joueur local indisponible";
            return false;
        }

        for (const auto& line : a_offer) {
            if (line.formID == 0 || line.quantity == 0) {
                a_error = "offre locale invalide";
                return false;
            }

            auto* object = LookupObject(line.formID);
            if (!object) {
                a_error = fmt::format("objet introuvable: {}", line.name);
                return false;
            }

            const auto count = GetCount(a_player, object);
            if (count < static_cast<std::int32_t>(line.quantity)) {
                a_error = fmt::format(
                    "quantite insuffisante pour {} ({} disponible, {} requis)",
                    line.name,
                    count,
                    line.quantity);
                return false;
            }
        }

        return true;
    }

    bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer& a_remoteOffer,
        std::string& a_error)
    {
        if (!ValidateLocalOffer(a_player, a_localOffer, a_error)) {
            return false;
        }

        // Each client only mutates its own real PlayerCharacter. This avoids
        // relying on the remote STR proxy for the actual transaction.
        for (const auto& line : a_localOffer) {
            auto* object = LookupObject(line.formID);
            if (!object) {
                a_error = fmt::format("objet local introuvable: {}", line.name);
                return false;
            }

            a_player->RemoveItem(
                object,
                static_cast<std::int32_t>(line.quantity),
                RE::ITEM_REMOVE_REASON::kRemove,
                nullptr,
                nullptr);
        }

        for (const auto& line : a_remoteOffer) {
            auto* object = LookupObject(line.formID);
            if (!object) {
                a_error = fmt::format("objet distant introuvable: {}", line.name);
                return false;
            }

            a_player->AddObjectToContainer(
                object,
                nullptr,
                static_cast<std::int32_t>(line.quantity),
                nullptr);
        }

        return true;
    }
}
