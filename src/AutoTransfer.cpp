#include "PCH.h"
#include "AutoTransfer.h"
#include "UdpTransport.h"

namespace TradeTogether::AutoTransfer
{
    namespace
    {
        constexpr RE::FormID kGoldFormID = 0x0000000F;

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
            auto notify = [message = std::move(a_message)]() {
                RE::DebugNotification(message.c_str());
            };

            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(std::move(notify));
            } else {
                notify();
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
                "STRPM inventory transfer event: id={} peer={} state={} result={} base={:08X}:{:08X} count={}",
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
                a_error = "service d'inventaire STRPM indisponible";
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
                if (result != STRPM::Result::kOk) {
                    spdlog::warn(
                        "TradeTogether could not register STRPM inventory listener: {}",
                        STRPM::ResultToString(result));
                } else {
                    registeredInterface = inventory;
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
            if (!a_inventory) {
                a_error = "service d'inventaire STRPM indisponible";
                return false;
            }

            for (int attempt = 0; attempt < 2; ++attempt) {
                std::size_t count = 0;
                auto result = a_inventory->getLocalInventory(nullptr, 0, &count);
                if (result != STRPM::Result::kOk) {
                    a_error = fmt::format(
                        "snapshot STRPM impossible ({})",
                        STRPM::ResultToString(result));
                    return false;
                }

                a_items.assign(count, STRPM::InventoryItem{});
                if (count == 0) {
                    return true;
                }

                auto capacity = a_items.size();
                result = a_inventory->getLocalInventory(
                    a_items.data(),
                    capacity,
                    &count);
                if (result == STRPM::Result::kBufferTooSmall) {
                    continue;
                }
                if (result != STRPM::Result::kOk) {
                    a_error = fmt::format(
                        "snapshot STRPM impossible ({})",
                        STRPM::ResultToString(result));
                    return false;
                }

                a_items.resize(count);
                return true;
            }

            a_error = "inventaire modifie pendant le snapshot STRPM";
            return false;
        }

        std::vector<const STRPM::InventoryItem*> FindCandidates(
            const std::vector<STRPM::InventoryItem>& a_items,
            RE::FormID a_formID)
        {
            std::vector<const STRPM::InventoryItem*> result;
            for (const auto& item : a_items) {
                if (item.localBaseFormID == a_formID &&
                    item.entry.count > 0) {
                    result.push_back(std::addressof(item));
                }
            }
            return result;
        }

        bool PrepareEntry(
            const std::vector<STRPM::InventoryItem>& a_items,
            RE::FormID a_formID,
            std::uint32_t a_quantity,
            std::string_view a_name,
            STRPM::InventoryEntry& a_entry,
            std::string& a_error)
        {
            if (a_formID == 0 || a_quantity == 0) {
                a_error = "offre locale invalide";
                return false;
            }

            const auto candidates = FindCandidates(a_items, a_formID);
            if (candidates.empty()) {
                a_error = fmt::format(
                    "objet absent du snapshot STRPM: {}",
                    a_name.empty() ? fmt::format("{:08X}", a_formID) : std::string(a_name));
                return false;
            }

            if (candidates.size() != 1) {
                a_error = fmt::format(
                    "plusieurs variantes STRPM pour {} ; transfert ambigu",
                    a_name.empty() ? fmt::format("{:08X}", a_formID) : std::string(a_name));
                return false;
            }

            const auto* item = candidates.front();
            if (item->entry.count < static_cast<std::int32_t>(a_quantity)) {
                a_error = fmt::format(
                    "quantite insuffisante pour {} ({} disponible, {} requis)",
                    a_name.empty() ? fmt::format("{:08X}", a_formID) : std::string(a_name),
                    item->entry.count,
                    a_quantity);
                return false;
            }

            a_entry = item->entry;
            a_entry.count = static_cast<std::int32_t>(a_quantity);
            return true;
        }

        bool ValidateOfferAgainstSnapshot(
            const std::vector<STRPM::InventoryItem>& a_items,
            const Offer& a_offer,
            std::uint32_t a_gold,
            std::string& a_error)
        {
            std::unordered_map<RE::FormID, std::uint64_t> requested;
            for (const auto& line : a_offer) {
                if (line.formID == 0 || line.quantity == 0 || line.name.empty()) {
                    a_error = "offre locale invalide";
                    return false;
                }
                requested[line.formID] += line.quantity;
            }
            if (a_gold > 0) {
                requested[kGoldFormID] += a_gold;
            }

            for (const auto& [formID, quantity] : requested) {
                if (quantity > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
                    a_error = "quantite offerte trop importante";
                    return false;
                }

                STRPM::InventoryEntry entry{};
                const auto name = formID == kGoldFormID ?
                    std::string_view("septims") :
                    [&]() -> std::string_view {
                        for (const auto& line : a_offer) {
                            if (line.formID == formID) {
                                return line.name;
                            }
                        }
                        return {};
                    }();

                if (!PrepareEntry(
                        a_items,
                        formID,
                        static_cast<std::uint32_t>(quantity),
                        name,
                        entry,
                        a_error)) {
                    return false;
                }
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
        if (!a_player) {
            a_error = "joueur local indisponible";
            return false;
        }

        const auto* inventory = GetInventoryInterface(a_error);
        if (!inventory) {
            return false;
        }

        std::vector<STRPM::InventoryItem> items;
        if (!SnapshotLocalInventory(inventory, items, a_error)) {
            return false;
        }

        return ValidateOfferAgainstSnapshot(
            items,
            a_offer,
            a_gold,
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

        std::vector<STRPM::InventoryItem> items;
        if (!SnapshotLocalInventory(inventory, items, a_error) ||
            !ValidateOfferAgainstSnapshot(
                items,
                a_localOffer,
                a_localGold,
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
            UdpTransport::GetSingleton().ResolvePlayerConnectionID(*peerName);
        if (!peerConnection || *peerConnection == 0) {
            a_error = "ConnectionID STR du partenaire introuvable";
            return false;
        }

        auto startTransfer =
            [&](RE::FormID a_formID,
                std::uint32_t a_quantity,
                std::string_view a_name) -> bool {
                STRPM::InventoryEntry entry{};
                if (!PrepareEntry(
                        items,
                        a_formID,
                        a_quantity,
                        a_name,
                        entry,
                        a_error)) {
                    return false;
                }

                STRPM::InventoryTransferID transferID = 0;
                const auto result = inventory->transfer(
                    *peerConnection,
                    &entry,
                    &transferID);
                if (result != STRPM::Result::kOk ||
                    transferID == 0) {
                    a_error = fmt::format(
                        "STRPM transfer refuse pour {} ({})",
                        a_name,
                        STRPM::ResultToString(result));
                    return false;
                }

                spdlog::info(
                    "STRPM inventory transfer started: id={} peer={} form={:08X} name=\"{}\" count={}",
                    transferID,
                    *peerConnection,
                    a_formID,
                    a_name,
                    a_quantity);
                return true;
            };

        for (const auto& line : a_localOffer) {
            if (!startTransfer(
                    line.formID,
                    line.quantity,
                    line.name)) {
                return false;
            }
        }

        if (a_localGold > 0 &&
            !startTransfer(
                kGoldFormID,
                a_localGold,
                "septims")) {
            return false;
        }

        return true;
    }
}
