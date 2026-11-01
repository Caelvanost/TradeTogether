#include "PCH.h"
#include "AutoTransfer.h"
#include "InstanceData.h"
#include "UdpTransport.h"

#include <bit>
#include <cmath>

namespace TradeTogether::AutoTransfer
{
    namespace
    {
        constexpr RE::FormID kGoldFormID = 0x0000000F;

        bool IsZeroGameId(const STRPM::InventoryGameId& a_id)
        {
            return a_id.baseId == 0 && a_id.modId == 0;
        }

        bool FloatEqual(float a_left, float a_right)
        {
            return std::bit_cast<std::uint32_t>(a_left) ==
                   std::bit_cast<std::uint32_t>(a_right);
        }

        const char* TransferStateName(STRPM::InventoryTransferState a_state)
        {
            switch (a_state) {
            case STRPM::InventoryTransferState::kPrepared:
                return "prepared";
            case STRPM::InventoryTransferState::kRemovingLocal:
                return "removing-local";
            case STRPM::InventoryTransferState::kCommitting:
                return "committing";
            case STRPM::InventoryTransferState::kApplyingRemote:
                return "applying-remote";
            case STRPM::InventoryTransferState::kComplete:
                return "complete";
            case STRPM::InventoryTransferState::kFailed:
                return "failed";
            case STRPM::InventoryTransferState::kRolledBack:
                return "rolled-back";
            default:
                return "unknown";
            }
        }

        void NotifyOnGameThread(std::string a_message)
        {
            auto task = [message = std::move(a_message)]() {
                RE::DebugNotification(message.c_str());
            };

            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(std::move(task));
            } else {
                task();
            }
        }

        void STRPM_CALL OnInventoryTransfer(
            const STRPM::InventoryTransferEvent* a_event,
            void*)
        {
            if (!a_event) {
                return;
            }

            spdlog::info(
                "STRPM inventory transfer event: id={} peer={} state={} result={} gameId={:08X}:{:08X} count={}",
                a_event->transferID,
                a_event->peerConnectionID,
                TransferStateName(a_event->state),
                STRPM::ResultToString(a_event->result),
                a_event->entry.baseId.modId,
                a_event->entry.baseId.baseId,
                a_event->entry.count);

            if (a_event->state == STRPM::InventoryTransferState::kFailed ||
                a_event->state == STRPM::InventoryTransferState::kRolledBack) {
                NotifyOnGameThread(fmt::format(
                    "TradeTogether: transfert STRPM echoue ({})",
                    STRPM::ResultToString(a_event->result)));
            }
        }

        const STRPM::InventoryInterface* GetInventoryInterface(
            std::string& a_error)
        {
            const auto* inventory = STRPM::LoadInventoryFromModule();
            if (!inventory ||
                inventory->version != STRPM::kInventoryInterfaceVersion ||
                !inventory->getLocalInventory ||
                !inventory->transfer) {
                a_error = "Inventory Bridge STRPM indisponible";
                return nullptr;
            }

            static std::mutex listenerMutex;
            static const STRPM::InventoryInterface* registeredInterface = nullptr;
            std::scoped_lock lock(listenerMutex);
            if (registeredInterface != inventory &&
                inventory->registerTransferListener) {
                const auto result = inventory->registerTransferListener(
                    &OnInventoryTransfer,
                    nullptr);
                if (result == STRPM::Result::kOk) {
                    registeredInterface = inventory;
                } else {
                    spdlog::warn(
                        "STRPM inventory listener registration failed: {}",
                        STRPM::ResultToString(result));
                }
            }

            return inventory;
        }

        bool SnapshotLocalInventory(
            const STRPM::InventoryInterface* a_inventory,
            std::vector<STRPM::InventoryItem>& a_items,
            std::string& a_error)
        {
            a_items.clear();

            for (int attempt = 0; attempt < 2; ++attempt) {
                std::size_t count = 0;
                auto result =
                    a_inventory->getLocalInventory(nullptr, 0, &count);
                if (result != STRPM::Result::kOk) {
                    a_error = fmt::format(
                        "snapshot STRPM impossible ({})",
                        STRPM::ResultToString(result));
                    return false;
                }

                if (count == 0) {
                    return true;
                }

                a_items.assign(count, STRPM::InventoryItem{});
                auto returned = count;
                result = a_inventory->getLocalInventory(
                    a_items.data(),
                    a_items.size(),
                    &returned);

                if (result == STRPM::Result::kBufferTooSmall) {
                    continue;
                }
                if (result != STRPM::Result::kOk) {
                    a_error = fmt::format(
                        "snapshot STRPM impossible ({})",
                        STRPM::ResultToString(result));
                    return false;
                }

                a_items.resize(returned);
                return true;
            }

            a_error = "inventaire modifie pendant le snapshot STRPM";
            return false;
        }

        bool HasBridgeUnsupportedMetadata(
            const InstanceData::Snapshot& a_snapshot,
            std::string& a_reason)
        {
            if (a_snapshot.customName) {
                a_reason = "nom personnalise non transporte par Inventory Bridge v1";
                return true;
            }
            if (a_snapshot.ownershipFormID || a_snapshot.ownershipRank) {
                a_reason = "ownership/rank non transporte par Inventory Bridge v1";
                return true;
            }
            if (a_snapshot.cannotWear) {
                a_reason = "CannotWear non transporte par Inventory Bridge v1";
                return true;
            }
            return false;
        }

        bool MatchesEffectShape(
            const InstanceData::EffectSnapshot& a_snapshot,
            const STRPM::InventoryEffect& a_effect)
        {
            return FloatEqual(a_snapshot.magnitude, a_effect.magnitude) &&
                   a_snapshot.area == static_cast<std::uint32_t>(
                       std::max(a_effect.area, 0)) &&
                   a_snapshot.duration == static_cast<std::uint32_t>(
                       std::max(a_effect.duration, 0)) &&
                   FloatEqual(a_snapshot.cost, a_effect.rawCost);
        }

        bool MatchesPortableEntry(
            const InstanceData::Snapshot& a_snapshot,
            const STRPM::InventoryEntry& a_entry)
        {
            if (a_entry.count <= 0) {
                return false;
            }

            if ((a_entry.flags &
                 (STRPM::kInventoryEntryWorn |
                  STRPM::kInventoryEntryWornLeft |
                  STRPM::kInventoryEntryQuestItem)) != 0) {
                return false;
            }

            const auto expectedHealth = a_snapshot.health.value_or(0.0F);
            const auto expectedCharge = a_snapshot.charge.value_or(0.0F);
            if (!FloatEqual(expectedHealth, a_entry.extraHealth) ||
                !FloatEqual(expectedCharge, a_entry.extraCharge)) {
                return false;
            }

            if (a_snapshot.soulLevel) {
                if (a_entry.extraSoulLevel !=
                    static_cast<std::int32_t>(*a_snapshot.soulLevel)) {
                    return false;
                }
            } else if (a_entry.extraSoulLevel != 0) {
                return false;
            }

            if (a_snapshot.poisonFormID) {
                if (IsZeroGameId(a_entry.extraPoisonId) ||
                    a_entry.extraPoisonCount != a_snapshot.poisonCount) {
                    return false;
                }
            } else if (!IsZeroGameId(a_entry.extraPoisonId) ||
                       a_entry.extraPoisonCount != 0) {
                return false;
            }

            if (!a_snapshot.hasEnchantment) {
                return IsZeroGameId(a_entry.extraEnchantId) &&
                       a_entry.extraEnchantCharge == 0 &&
                       a_entry.enchantEffectCount == 0 &&
                       (a_entry.flags &
                        (STRPM::kInventoryEntryEnchantIsWeapon |
                         STRPM::kInventoryEntryEnchantRemoveOnUnequip)) == 0;
            }

            if (a_entry.extraEnchantCharge !=
                a_snapshot.enchantmentMaxCharge) {
                return false;
            }

            const bool removeOnUnequip =
                (a_entry.flags &
                 STRPM::kInventoryEntryEnchantRemoveOnUnequip) != 0;
            if (removeOnUnequip != a_snapshot.removeOnUnequip) {
                return false;
            }

            if (!a_snapshot.dynamicEnchantment) {
                return !IsZeroGameId(a_entry.extraEnchantId) &&
                       a_entry.enchantEffectCount == 0;
            }

            if (a_entry.enchantEffectCount !=
                    a_snapshot.enchantmentEffects.size() ||
                a_entry.enchantEffectCount > STRPM::kMaxInventoryEffects) {
                return false;
            }

            for (std::size_t i = 0;
                 i < a_snapshot.enchantmentEffects.size();
                 ++i) {
                if (!MatchesEffectShape(
                        a_snapshot.enchantmentEffects[i],
                        a_entry.enchantEffects[i])) {
                    return false;
                }
            }

            return true;
        }

        struct PreparedTransfer
        {
            STRPM::InventoryEntry entry{};
            RE::FormID localFormID{ 0 };
            std::string name;
        };

        bool PrepareLine(
            const std::vector<STRPM::InventoryItem>& a_items,
            const OfferLine& a_line,
            std::unordered_map<const STRPM::InventoryItem*, std::int32_t>& a_reserved,
            PreparedTransfer& a_out,
            std::string& a_error)
        {
            const auto snapshot = InstanceData::Decode(a_line.instanceData);
            if (!snapshot) {
                a_error = fmt::format(
                    "etat d'instance invalide pour {}",
                    a_line.name);
                return false;
            }

            std::string unsupportedReason;
            if (HasBridgeUnsupportedMetadata(*snapshot, unsupportedReason)) {
                a_error = fmt::format(
                    "{}: {}",
                    a_line.name,
                    unsupportedReason);
                return false;
            }

            std::vector<const STRPM::InventoryItem*> candidates;
            for (const auto& item : a_items) {
                if (item.localBaseFormID != a_line.formID ||
                    !MatchesPortableEntry(*snapshot, item.entry)) {
                    continue;
                }

                const auto reserved = a_reserved.contains(std::addressof(item)) ?
                    a_reserved[std::addressof(item)] : 0;
                if (item.entry.count - reserved >=
                    static_cast<std::int32_t>(a_line.quantity)) {
                    candidates.push_back(std::addressof(item));
                }
            }

            if (candidates.empty()) {
                a_error = fmt::format(
                    "aucune entree STRPM compatible pour {}",
                    a_line.name);
                return false;
            }

            if (candidates.size() > 1) {
                a_error = fmt::format(
                    "plusieurs entrees STRPM correspondent a {} ; transfert ambigu",
                    a_line.name);
                return false;
            }

            const auto* selected = candidates.front();
            a_reserved[selected] +=
                static_cast<std::int32_t>(a_line.quantity);

            a_out.entry = selected->entry;
            a_out.entry.count =
                static_cast<std::int32_t>(a_line.quantity);
            a_out.localFormID = a_line.formID;
            a_out.name = a_line.name;
            return true;
        }

        bool PrepareGold(
            const std::vector<STRPM::InventoryItem>& a_items,
            std::uint32_t a_gold,
            std::unordered_map<const STRPM::InventoryItem*, std::int32_t>& a_reserved,
            PreparedTransfer& a_out,
            std::string& a_error)
        {
            if (a_gold == 0) {
                return true;
            }

            const STRPM::InventoryItem* selected = nullptr;
            for (const auto& item : a_items) {
                if (item.localBaseFormID != kGoldFormID) {
                    continue;
                }

                InstanceData::Snapshot plain{};
                if (!MatchesPortableEntry(plain, item.entry)) {
                    continue;
                }

                const auto reserved = a_reserved.contains(std::addressof(item)) ?
                    a_reserved[std::addressof(item)] : 0;
                if (item.entry.count - reserved <
                    static_cast<std::int32_t>(a_gold)) {
                    continue;
                }

                if (selected) {
                    a_error = "plusieurs piles STRPM de septims compatibles";
                    return false;
                }
                selected = std::addressof(item);
            }

            if (!selected) {
                a_error = "septims insuffisants ou absents du snapshot STRPM";
                return false;
            }

            a_reserved[selected] += static_cast<std::int32_t>(a_gold);
            a_out.entry = selected->entry;
            a_out.entry.count = static_cast<std::int32_t>(a_gold);
            a_out.localFormID = kGoldFormID;
            a_out.name = "septims";
            return true;
        }

        bool BuildTransferBatch(
            const STRPM::InventoryInterface* a_inventory,
            const Offer& a_offer,
            std::uint32_t a_gold,
            std::vector<PreparedTransfer>& a_transfers,
            std::string& a_error)
        {
            std::vector<STRPM::InventoryItem> items;
            if (!SnapshotLocalInventory(
                    a_inventory,
                    items,
                    a_error)) {
                return false;
            }

            std::unordered_map<
                const STRPM::InventoryItem*,
                std::int32_t> reserved;

            a_transfers.clear();
            a_transfers.reserve(
                a_offer.size() + (a_gold > 0 ? 1u : 0u));

            for (const auto& line : a_offer) {
                if (line.formID == 0 ||
                    line.quantity == 0 ||
                    line.name.empty() ||
                    line.instanceData.empty()) {
                    a_error = "offre locale invalide";
                    return false;
                }

                PreparedTransfer transfer{};
                if (!PrepareLine(
                        items,
                        line,
                        reserved,
                        transfer,
                        a_error)) {
                    return false;
                }
                a_transfers.push_back(std::move(transfer));
            }

            if (a_gold > 0) {
                PreparedTransfer gold{};
                if (!PrepareGold(
                        items,
                        a_gold,
                        reserved,
                        gold,
                        a_error)) {
                    return false;
                }
                a_transfers.push_back(std::move(gold));
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

        const auto* inventory = GetInventoryInterface(a_error);
        if (!inventory) {
            return false;
        }

        std::vector<PreparedTransfer> transfers;
        return BuildTransferBatch(
            inventory,
            a_offer,
            a_gold,
            transfers,
            a_error);
    }

    bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer&,
        std::uint32_t a_localGold,
        std::uint32_t,
        std::string& a_error)
    {
        a_error.clear();
        if (!a_player) {
            a_error = "joueur local indisponible";
            return false;
        }

        if (a_localOffer.empty() && a_localGold == 0) {
            return true;
        }

        const auto* inventory = GetInventoryInterface(a_error);
        if (!inventory) {
            return false;
        }

        std::vector<PreparedTransfer> transfers;
        if (!BuildTransferBatch(
                inventory,
                a_localOffer,
                a_localGold,
                transfers,
                a_error)) {
            return false;
        }

        const auto peerName =
            UdpTransport::GetSingleton().GetMostRecentPeerName();
        if (!peerName || peerName->empty()) {
            a_error = "partenaire STR introuvable";
            return false;
        }

        const auto peerConnection =
            UdpTransport::GetSingleton().ResolvePlayerConnectionID(
                *peerName);
        if (!peerConnection || *peerConnection == 0) {
            a_error = "ConnectionID STR du partenaire introuvable";
            return false;
        }

        for (const auto& transfer : transfers) {
            STRPM::InventoryTransferID transferID = 0;
            const auto result = inventory->transfer(
                *peerConnection,
                &transfer.entry,
                &transferID);
            if (result != STRPM::Result::kOk ||
                transferID == 0) {
                a_error = fmt::format(
                    "Inventory Bridge refuse {} ({})",
                    transfer.name,
                    STRPM::ResultToString(result));
                spdlog::error(
                    "STRPM inventory transfer start failed: peer={} form={:08X} name=\"{}\" count={} result={}",
                    *peerConnection,
                    transfer.localFormID,
                    transfer.name,
                    transfer.entry.count,
                    STRPM::ResultToString(result));
                return false;
            }

            spdlog::info(
                "STRPM inventory transfer started: id={} peer={} form={:08X} name=\"{}\" count={}",
                transferID,
                *peerConnection,
                transfer.localFormID,
                transfer.name,
                transfer.entry.count);
        }

        return true;
    }
}
