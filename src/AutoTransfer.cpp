#include "PCH.h"
#include "AutoTransfer.h"
#include "InstanceData.h"

namespace TradeTogether::AutoTransfer
{
    namespace
    {
        constexpr RE::FormID kGoldFormID = 0x0000000F;

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

        struct IncomingStack
        {
            RE::TESBoundObject* object{ nullptr };
            RE::ExtraDataList* extraList{ nullptr };
            std::int32_t count{ 0 };
            std::string name;
            RE::FormID formID{ 0 };
        };

        RE::TESBoundObject* LookupObject(RE::FormID a_formID)
        {
            return RE::TESForm::LookupByID<RE::TESBoundObject>(a_formID);
        }

        std::int32_t GetObjectCount(
            RE::PlayerCharacter* a_player,
            RE::TESBoundObject* a_object)
        {
            if (!a_player || !a_object) {
                return 0;
            }

            const auto counts = a_player->GetInventoryCounts();
            const auto iterator = counts.find(a_object);
            return iterator != counts.end() ? std::max(iterator->second, 0) : 0;
        }

        void DestroyIncoming(std::vector<IncomingStack>& a_incoming)
        {
            for (auto& stack : a_incoming) {
                delete stack.extraList;
                stack.extraList = nullptr;
            }
            a_incoming.clear();
        }

        TransferPlan BuildTransferPlan(
            RE::PlayerCharacter* a_player,
            const OfferLine& a_line,
            std::string& a_error)
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

            std::int32_t representedByExtraLists = 0;
            if (entry && entry->extraLists) {
                for (auto* extraList : *entry->extraLists) {
                    if (!extraList) {
                        continue;
                    }

                    const auto stackCount = std::max(extraList->GetCount(), 1);
                    representedByExtraLists += stackCount;

                    InstanceData::Snapshot snapshot{};
                    std::string snapshotError;
                    if (!InstanceData::Capture(
                            plan.object,
                            extraList,
                            snapshot,
                            snapshotError)) {
                        // Unsupported ExtraData only blocks this offer line when
                        // the displayed instance could be the one being traded.
                        const auto* displayName =
                            extraList->GetDisplayName(plan.object);
                        if (displayName && a_line.name == displayName) {
                            a_error = fmt::format(
                                "{}: {}",
                                a_line.name,
                                snapshotError);
                            return {};
                        }
                        continue;
                    }

                    if (InstanceData::Encode(snapshot) == a_line.instanceData) {
                        plan.stacks.push_back({ extraList, stackCount });
                        plan.available += stackCount;
                    }
                }
            }

            plan.baseCount =
                std::max(totalCount - representedByExtraLists, 0);
            if (a_line.instanceData == "-" && plan.baseCount > 0) {
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
            if (a_line.formID == 0 ||
                a_line.quantity == 0 ||
                a_line.name.empty() ||
                a_line.instanceData.empty() ||
                !InstanceData::Decode(a_line.instanceData)) {
                a_error = "offre locale invalide";
                return false;
            }

            const auto plan =
                BuildTransferPlan(a_player, a_line, a_error);
            if (!a_error.empty()) {
                return false;
            }
            if (!plan.object) {
                a_error = fmt::format(
                    "objet introuvable: {}",
                    a_line.name);
                return false;
            }
            if (plan.available <
                static_cast<std::int32_t>(a_line.quantity)) {
                a_error = fmt::format(
                    "instance indisponible pour {} ({} disponible, {} requis)",
                    a_line.name,
                    plan.available,
                    a_line.quantity);
                return false;
            }
            return true;
        }

        bool ValidateGold(
            RE::PlayerCharacter* a_player,
            std::uint32_t a_gold,
            std::string& a_error)
        {
            if (a_gold == 0) {
                return true;
            }

            auto* gold = LookupObject(kGoldFormID);
            if (!gold) {
                a_error = "or introuvable";
                return false;
            }

            const auto available = GetObjectCount(a_player, gold);
            if (available < static_cast<std::int32_t>(a_gold)) {
                a_error = fmt::format(
                    "or insuffisant ({} disponible, {} requis)",
                    available,
                    a_gold);
                return false;
            }
            return true;
        }

        bool PrepareIncoming(
            const Offer& a_remoteOffer,
            std::vector<IncomingStack>& a_out,
            std::string& a_error)
        {
            for (const auto& line : a_remoteOffer) {
                if (line.formID == 0 ||
                    line.quantity == 0 ||
                    line.name.empty()) {
                    a_error = "offre distante invalide";
                    DestroyIncoming(a_out);
                    return false;
                }

                auto* object = LookupObject(line.formID);
                if (!object) {
                    a_error = fmt::format(
                        "objet distant introuvable localement: {} ({:08X})",
                        line.name,
                        line.formID);
                    DestroyIncoming(a_out);
                    return false;
                }

                const auto snapshot =
                    InstanceData::Decode(line.instanceData);
                if (!snapshot) {
                    a_error = fmt::format(
                        "etat d'instance invalide pour {}",
                        line.name);
                    DestroyIncoming(a_out);
                    return false;
                }

                RE::ExtraDataList* extraList = nullptr;
                if (!InstanceData::IsEmpty(*snapshot)) {
                    extraList =
                        InstanceData::Rebuild(
                            object,
                            *snapshot,
                            a_error);
                    if (!extraList) {
                        a_error = fmt::format(
                            "{}: {}",
                            line.name,
                            a_error);
                        DestroyIncoming(a_out);
                        return false;
                    }
                }

                a_out.push_back(IncomingStack{
                    object,
                    extraList,
                    static_cast<std::int32_t>(line.quantity),
                    line.name,
                    line.formID
                });
            }
            return true;
        }
    }

    bool ValidateLocalOffer(
        RE::PlayerCharacter* a_player,
        const Offer& a_offer,
        std::uint32_t a_gold,
        std::string& a_error)
    {
        a_error.clear();
        if (!a_player) {
            a_error = "joueur local indisponible";
            return false;
        }

        for (const auto& line : a_offer) {
            if (!ValidateLine(a_player, line, a_error)) {
                return false;
            }
        }
        return ValidateGold(a_player, a_gold, a_error);
    }

    bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer& a_remoteOffer,
        std::uint32_t a_localGold,
        std::uint32_t a_remoteGold,
        std::string& a_error)
    {
        if (!ValidateLocalOffer(
                a_player,
                a_localOffer,
                a_localGold,
                a_error)) {
            return false;
        }

        if (!a_player) {
            a_error = "joueur local indisponible";
            return false;
        }

        auto* gold = LookupObject(kGoldFormID);
        if ((a_localGold > 0 || a_remoteGold > 0) && !gold) {
            a_error = "or introuvable pendant le transfert";
            return false;
        }

        // Build every incoming ExtraDataList before debiting anything. A missing
        // mod form, MagicEffect, poison, owner or other reconstruction problem
        // therefore aborts the exchange without consuming the local offer.
        std::vector<IncomingStack> incoming;
        if (!PrepareIncoming(a_remoteOffer, incoming, a_error)) {
            return false;
        }

        for (const auto& line : a_localOffer) {
            auto plan = BuildTransferPlan(
                a_player,
                line,
                a_error);
            if (!a_error.empty()) {
                DestroyIncoming(incoming);
                return false;
            }

            auto remaining =
                static_cast<std::int32_t>(line.quantity);

            for (const auto& stack : plan.stacks) {
                if (remaining <= 0) {
                    break;
                }

                const auto count =
                    std::min(remaining, stack.count);
                a_player->RemoveItem(
                    plan.object,
                    count,
                    RE::ITEM_REMOVE_REASON::kRemove,
                    stack.extraList,
                    nullptr);
                remaining -= count;

                spdlog::info(
                    "Local trade debit: form={:08X} name=\"{}\" count={} instance=1",
                    line.formID,
                    line.name,
                    count);
            }

            if (remaining > 0 && plan.baseCount > 0) {
                const auto count =
                    std::min(remaining, plan.baseCount);
                a_player->RemoveItem(
                    plan.object,
                    count,
                    RE::ITEM_REMOVE_REASON::kRemove,
                    nullptr,
                    nullptr);
                remaining -= count;

                spdlog::info(
                    "Local trade debit: form={:08X} name=\"{}\" count={} instance=0",
                    line.formID,
                    line.name,
                    count);
            }

            if (remaining > 0) {
                DestroyIncoming(incoming);
                a_error = fmt::format(
                    "transfert incomplet pour {} ({} restant)",
                    line.name,
                    remaining);
                return false;
            }
        }

        if (a_localGold > 0) {
            a_player->RemoveItem(
                gold,
                static_cast<std::int32_t>(a_localGold),
                RE::ITEM_REMOVE_REASON::kRemove,
                nullptr,
                nullptr);
            spdlog::info(
                "Local trade gold debit: amount={}",
                a_localGold);
        }

        for (auto& stack : incoming) {
            a_player->AddObjectToContainer(
                stack.object,
                stack.extraList,
                stack.count,
                nullptr);

            spdlog::info(
                "Local trade receipt: form={:08X} name=\"{}\" count={} instance={}",
                stack.formID,
                stack.name,
                stack.count,
                stack.extraList ? 1 : 0);

            // AddObjectToContainer adopts the ExtraDataList as part of the
            // created inventory stack.
            stack.extraList = nullptr;
        }
        incoming.clear();

        if (a_remoteGold > 0) {
            a_player->AddObjectToContainer(
                gold,
                nullptr,
                static_cast<std::int32_t>(a_remoteGold),
                nullptr);
            spdlog::info(
                "Local trade gold receipt: amount={}",
                a_remoteGold);
        }

        return true;
    }
}
