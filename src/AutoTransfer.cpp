#include "PCH.h"
#include "AutoTransfer.h"
#include "Protocol.h"
#include "UdpTransport.h"

namespace TradeTogether::AutoTransfer
{
    namespace
    {
        struct StackMatch
        {
            RE::ExtraDataList* extraList{ nullptr };
            std::int32_t count{ 0 };
        };

        struct TransferPlan
        {
            RE::TESBoundObject* object{ nullptr };
            std::vector<StackMatch> stacks;
            std::int32_t baseCount{ 0 };
            std::int32_t available{ 0 };
        };

        RE::TESBoundObject* LookupObject(RE::FormID a_formID)
        {
            return RE::TESForm::LookupByID<RE::TESBoundObject>(a_formID);
        }

        std::string GetActorName(RE::Actor* a_actor)
        {
            if (!a_actor) {
                return {};
            }

            const auto* name = a_actor->GetDisplayFullName();
            if (!name || !*name) {
                name = a_actor->GetName();
            }
            return name && *name ? name : std::string{};
        }

        RE::Actor* ResolveMostRecentPeerActor(RE::PlayerCharacter* a_player)
        {
            const auto peerName = UdpTransport::GetSingleton().GetMostRecentPeerName();
            if (!peerName || peerName->empty()) {
                return nullptr;
            }

            auto* processLists = RE::ProcessLists::GetSingleton();
            if (!processLists) {
                return nullptr;
            }

            RE::Actor* result = nullptr;
            processLists->ForEachHighActor(
                [&](RE::Actor& a_actor) {
                    if (std::addressof(a_actor) == a_player) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    const auto actorName = GetActorName(std::addressof(a_actor));
                    if (!actorName.empty() &&
                        Protocol::EqualsInsensitive(actorName, *peerName)) {
                        result = std::addressof(a_actor);
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });

            if (result) {
                spdlog::debug(
                    "Resolved native transfer peer: player=\"{}\" actor={:08X}",
                    *peerName,
                    result->GetFormID());
            }
            return result;
        }

        bool NamesEqual(const char* a_left, std::string_view a_right)
        {
            return a_left && *a_left && std::string_view(a_left) == a_right;
        }

        TransferPlan BuildTransferPlan(
            RE::PlayerCharacter* a_player,
            const OfferLine& a_line)
        {
            TransferPlan plan{};
            plan.object = LookupObject(a_line.formID);
            if (!a_player || !plan.object) {
                return plan;
            }

            const auto inventory = a_player->GetInventory();
            const auto iterator = inventory.find(plan.object);
            if (iterator == inventory.end()) {
                return plan;
            }

            const auto totalCount = std::max(iterator->second.first, 0);
            auto* entry = iterator->second.second.get();
            if (!entry || !entry->extraLists || entry->extraLists->empty()) {
                plan.baseCount = totalCount;
                plan.available = totalCount;
                return plan;
            }

            std::int32_t representedByExtraLists = 0;
            for (auto* extraList : *entry->extraLists) {
                if (!extraList) {
                    continue;
                }

                const auto stackCount = std::max(extraList->GetCount(), 1);
                representedByExtraLists += stackCount;
                const auto* displayName = extraList->GetDisplayName(plan.object);
                if (NamesEqual(displayName, a_line.name)) {
                    plan.stacks.push_back({ extraList, stackCount });
                    plan.available += stackCount;
                }
            }

            // Items without per-instance data are represented by the residual
            // count outside the extra lists. Use that base stack only when no
            // modified instance matched the selected inventory row.
            plan.baseCount = std::max(totalCount - representedByExtraLists, 0);
            if (plan.stacks.empty() && plan.baseCount > 0) {
                plan.available += plan.baseCount;
            } else {
                plan.baseCount = 0;
            }

            return plan;
        }

        bool ValidateLine(
            RE::PlayerCharacter* a_player,
            const OfferLine& a_line,
            std::string& a_error)
        {
            if (a_line.formID == 0 || a_line.quantity == 0 || a_line.name.empty()) {
                a_error = "offre locale invalide";
                return false;
            }

            const auto plan = BuildTransferPlan(a_player, a_line);
            if (!plan.object) {
                a_error = fmt::format("objet introuvable: {}", a_line.name);
                return false;
            }
            if (plan.available < static_cast<std::int32_t>(a_line.quantity)) {
                a_error = fmt::format(
                    "instance indisponible pour {} ({} disponible, {} requis)",
                    a_line.name,
                    plan.available,
                    a_line.quantity);
                return false;
            }
            return true;
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
            if (!ValidateLine(a_player, line, a_error)) {
                return false;
            }
        }
        return true;
    }

    bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer&,
        std::string& a_error)
    {
        // Validate every local line before touching the inventory. No extra UI
        // is shown: this remains an invisible preflight after final confirmation.
        if (!ValidateLocalOffer(a_player, a_localOffer, a_error)) {
            return false;
        }

        if (a_localOffer.empty()) {
            return true;
        }

        auto* peerActor = ResolveMostRecentPeerActor(a_player);
        if (!peerActor) {
            a_error = "proxy du partenaire introuvable";
            return false;
        }

        // Move the exact local inventory stacks to STR's remote actor proxy.
        // Passing the original ExtraDataList makes Skyrim move the actual item
        // instance instead of recreating it from the base FormID. Tempering,
        // custom names, enchantments and charge therefore stay attached to the
        // instance and STR can synchronize the same native container transfer.
        for (const auto& line : a_localOffer) {
            auto plan = BuildTransferPlan(a_player, line);
            auto remaining = static_cast<std::int32_t>(line.quantity);

            for (const auto& stack : plan.stacks) {
                if (remaining <= 0) {
                    break;
                }

                const auto count = std::min(remaining, stack.count);
                a_player->RemoveItem(
                    plan.object,
                    count,
                    RE::ITEM_REMOVE_REASON::kStoreInTeammate,
                    stack.extraList,
                    peerActor);
                remaining -= count;

                spdlog::info(
                    "Native instance transfer: form={:08X} name=\"{}\" count={} extra=1 peer={:08X}",
                    line.formID,
                    line.name,
                    count,
                    peerActor->GetFormID());
            }

            if (remaining > 0 && plan.baseCount > 0) {
                const auto count = std::min(remaining, plan.baseCount);
                a_player->RemoveItem(
                    plan.object,
                    count,
                    RE::ITEM_REMOVE_REASON::kStoreInTeammate,
                    nullptr,
                    peerActor);
                remaining -= count;

                spdlog::info(
                    "Native instance transfer: form={:08X} name=\"{}\" count={} extra=0 peer={:08X}",
                    line.formID,
                    line.name,
                    count,
                    peerActor->GetFormID());
            }

            if (remaining > 0) {
                a_error = fmt::format(
                    "transfert incomplet pour {} ({} restant)",
                    line.name,
                    remaining);
                return false;
            }
        }

        return true;
    }
}
