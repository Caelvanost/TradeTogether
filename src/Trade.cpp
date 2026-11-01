#include "PCH.h"
#include "Trade.h"
#include "AutoTransfer.h"
#include "Config.h"
#include "InstanceData.h"
#include "Offer.h"
#include "Protocol.h"
#include "UdpTransport.h"

namespace TradeTogether::Trade
{
    namespace
    {
        constexpr std::string_view kGameplayPrefix = "TTNET|v2|";

        constexpr std::uint32_t kF6ScanCode = 0x40;
        constexpr std::uint32_t kEScanCode = 0x12;
        constexpr std::uint32_t kTabScanCode = 0x0F;
        constexpr std::uint32_t kDeleteScanCode = 0xD3;
        constexpr RE::FormID kGoldFormID = 0x0000000F;

        struct PendingTrade
        {
            std::string requestID;
            RE::ActorHandle target;
            RE::FormID targetFormID{ 0 };
            std::string targetName;
            std::chrono::steady_clock::time_point sentAt{};
        };

        struct TradeSession
        {
            std::string requestID;
            std::string peerName;
            RE::ActorHandle target;
            RE::FormID targetFormID{ 0 };
            bool initiator{ false };

            Offer localOffer;
            Offer remoteOffer;
            std::uint32_t localGold{ 0 };
            std::uint32_t remoteGold{ 0 };
            std::uint64_t localRevision{ 0 };
            std::uint64_t remoteRevision{ 0 };
            bool localReady{ false };
            bool remoteReady{ false };
            bool localConfirmed{ false };
            bool remoteConfirmed{ false };
            bool summaryPromptOpen{ false };
            bool finalPromptOpen{ false };
            std::chrono::steady_clock::time_point lastActivity{};
        };

        struct TradeState
        {
            Config config{};
            std::optional<PendingTrade> pending;
            std::optional<TradeSession> session;
            std::unordered_map<
                std::string,
                std::chrono::steady_clock::time_point>
                recentIncoming;
            std::uint32_t nextRequest{ 0 };
            bool initialized{ false };
        };

        TradeState& GetState()
        {
            static TradeState state;
            return state;
        }

        void Notify(const char* a_message)
        {
            RE::DebugNotification(a_message);
        }

        void Notify(const std::string& a_message)
        {
            Notify(a_message.c_str());
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

        std::string GetLocalPlayerName()
        {
            return UdpTransport::GetSingleton().GetLocalPlayerName();
        }

        std::string MakeRequestID()
        {
            auto& state = GetState();
            return fmt::format(
                "{:08X}{:016X}{:08X}",
                GetCurrentProcessId(),
                GetTickCount64(),
                ++state.nextRequest);
        }

        RE::Actor* GetCrosshairActor()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* pickData = RE::CrosshairPickData::GetSingleton();
            if (!player || !pickData) {
                spdlog::warn(
                    "F6 ignored: PlayerCharacter or CrosshairPickData unavailable");
                Notify("TradeTogether: targeting unavailable.");
                return nullptr;
            }

            auto target = pickData->targetActor.get();
            if (!target) {
                target = pickData->target.get();
            }
            if (!target) {
                spdlog::info("F6: no crosshair target");
                Notify("TradeTogether: target the other player.");
                return nullptr;
            }

            auto* actor = target->As<RE::Actor>();
            if (!actor) {
                spdlog::info(
                    "F6: target {:08X} is not an actor",
                    target->GetFormID());
                Notify("TradeTogether: the target is not an actor.");
                return nullptr;
            }
            if (actor == player) {
                spdlog::info("F6: local player targeted; ignored");
                Notify("TradeTogether: target the other player.");
                return nullptr;
            }
            return actor;
        }

        void QueueInventoryMessage(RE::UI_MESSAGE_TYPE a_type)
        {
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(
                    RE::BSFixedString(RE::InventoryMenu::MENU_NAME),
                    a_type,
                    nullptr);
            }
        }

        bool IsInventoryOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
        }

        void OpenOfferInventory()
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }

            state.session->summaryPromptOpen = false;
            state.session->finalPromptOpen = false;
            if (!IsInventoryOpen()) {
                QueueInventoryMessage(RE::UI_MESSAGE_TYPE::kShow);
            }
            Notify(
                "Offer: Insert/Delete items | Numpad +/- gold | T review | Tab cancel");
        }

        void ScheduleOpenOfferInventory()
        {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { OpenOfferInventory(); });
            } else {
                OpenOfferInventory();
            }
        }

        bool SendSessionPacket(std::string_view a_payload)
        {
            auto& state = GetState();
            if (!state.session) {
                return false;
            }

            const auto packet = fmt::format(
                "{}|request={}|to={}",
                a_payload,
                state.session->requestID,
                Protocol::HexEncode(state.session->peerName));
            const auto sent = UdpTransport::GetSingleton().SendTo(
                state.session->peerName,
                packet);
            if (sent) {
                state.session->lastActivity =
                    std::chrono::steady_clock::now();
            }
            return sent;
        }

        void SendLocalState(bool a_ready)
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }
            ++state.session->localRevision;
            SendSessionPacket(fmt::format(
                "type=STATE|seq={}|ready={}|gold={}|items={}",
                state.session->localRevision,
                a_ready ? 1 : 0,
                state.session->localGold,
                EncodeOffer(state.session->localOffer)));
        }

        std::optional<std::uint64_t> ParseRevision(
            const std::optional<std::string>& a_value)
        {
            if (!a_value || a_value->empty()) {
                return std::nullopt;
            }
            try {
                return std::stoull(*a_value);
            } catch (...) {
                return std::nullopt;
            }
        }

        std::optional<std::uint32_t> ParseGold(
            const std::optional<std::string>& a_value)
        {
            if (!a_value || a_value->empty()) {
                return std::nullopt;
            }
            try {
                const auto value = std::stoull(*a_value);
                if (value > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        void ResetLocalApproval()
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }
            state.session->localReady = false;
            state.session->localConfirmed = false;
            state.session->finalPromptOpen = false;
        }

        std::string FormatOfferAndGold(
            std::string_view a_title,
            const Offer& a_offer,
            std::uint32_t a_gold)
        {
            auto result = FormatOffer(a_title, a_offer);
            if (a_gold > 0) {
                result += fmt::format("\n  {} gold", a_gold);
            }
            return result;
        }

        std::string BuildOfferSummary(bool a_final)
        {
            const auto& session = *GetState().session;
            std::string summary = a_final ?
                "FINAL CONFIRMATION\n\n" : "TRADE OFFERS\n\n";
            summary += FormatOfferAndGold(
                "Your offer:",
                session.localOffer,
                session.localGold);
            summary += "\n\n";
            summary += FormatOfferAndGold(
                fmt::format("Offer from {}:", session.peerName),
                session.remoteOffer,
                session.remoteGold);
            if (!a_final) {
                summary += fmt::format(
                    "\n\nStatus: you are {} | {} {}",
                    session.localReady ? "ready" : "editing",
                    session.peerName,
                    session.remoteReady ? "is ready" : "is editing");
            }
            return summary;
        }

        void ShowFinalPrompt();

        void MarkLocalReady()
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }

            state.session->summaryPromptOpen = false;
            state.session->localReady = true;
            state.session->localConfirmed = false;
            QueueInventoryMessage(RE::UI_MESSAGE_TYPE::kHide);
            SendLocalState(true);
            Notify(fmt::format(
                "TradeTogether: offer ready, waiting for {}.",
                state.session->peerName));
            if (state.session->remoteReady) {
                ShowFinalPrompt();
            }
        }

        class OfferSummaryCallback final : public RE::IMessageBoxCallback
        {
        public:
            explicit OfferSummaryCallback(std::string a_requestID) :
                _requestID(std::move(a_requestID))
            {}

            void Run(Message a_message) override
            {
                auto& state = GetState();
                if (!state.session ||
                    state.session->requestID != _requestID) {
                    return;
                }
                state.session->summaryPromptOpen = false;
                if (a_message == Message::kUnk0) {
                    MarkLocalReady();
                } else {
                    OpenOfferInventory();
                }
            }

        private:
            std::string _requestID;
        };

        void ShowOfferSummary()
        {
            auto& state = GetState();
            if (!state.session || state.session->summaryPromptOpen) {
                return;
            }

            state.session->summaryPromptOpen = true;
            const auto summary = BuildOfferSummary(false);
            RE::CreateMessage(
                summary.c_str(),
                new OfferSummaryCallback(state.session->requestID),
                0,
                4,
                10,
                "Ready",
                "Modify");
        }

        void CancelSession(bool a_notifyPeer, std::string_view a_reason)
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }

            const auto peerName = state.session->peerName;
            if (a_notifyPeer) {
                SendSessionPacket("type=CANCEL");
            }
            QueueInventoryMessage(RE::UI_MESSAGE_TYPE::kHide);
            state.session.reset();
            Notify(fmt::format(
                "TradeTogether: trade with {} cancelled ({}).",
                peerName,
                a_reason));
            spdlog::info(
                "Trade session cancelled: peer=\"{}\" reason={}",
                peerName,
                a_reason);
        }

        void FinalizeIfComplete()
        {
            auto& state = GetState();
            if (!state.session ||
                !state.session->localReady ||
                !state.session->remoteReady ||
                !state.session->localConfirmed ||
                !state.session->remoteConfirmed) {
                return;
            }

            auto session = std::move(*state.session);
            auto* player = RE::PlayerCharacter::GetSingleton();
            std::string transferError;
            if (!AutoTransfer::ExecuteLocalExchange(
                    player,
                    session.localOffer,
                    session.remoteOffer,
                    session.localGold,
                    session.remoteGold,
                    session.peerName,
                    transferError)) {
                state.session.reset();
                Notify(fmt::format(
                    "TradeTogether: transfer cancelled ({})",
                    transferError));
                spdlog::error(
                    "Automatic trade transfer failed: request={} peer=\"{}\" reason=\"{}\"",
                    session.requestID,
                    session.peerName,
                    transferError);
                return;
            }

            state.session.reset();
            Notify(fmt::format(
                "TradeTogether: STRPM transfers started with {}.",
                session.peerName));
            spdlog::info(
                "STRPM trade transfers started: request={} peer=\"{}\" givenLines={} receivedLines={} givenGold={} receivedGold={}",
                session.requestID,
                session.peerName,
                session.localOffer.size(),
                session.remoteOffer.size(),
                session.localGold,
                session.remoteGold);
        }

        void ConfirmLocalOffer()
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }
            state.session->finalPromptOpen = false;
            if (!state.session->localReady || !state.session->remoteReady) {
                Notify("TradeTogether: an offer changed. Please review it again.");
                ShowOfferSummary();
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            std::string validationError;
            if (!AutoTransfer::ValidateLocalOffer(
                    player,
                    state.session->localOffer,
                    state.session->localGold,
                    validationError)) {
                ResetLocalApproval();
                SendLocalState(false);
                Notify(fmt::format(
                    "TradeTogether: invalid offer ({})",
                    validationError));
                OpenOfferInventory();
                return;
            }

            state.session->localConfirmed = true;
            SendSessionPacket(fmt::format(
                "type=CONFIRM|mine={}|theirs={}",
                state.session->localRevision,
                state.session->remoteRevision));
            Notify(fmt::format(
                "TradeTogether: confirmation sent, waiting for {}.",
                state.session->peerName));
            FinalizeIfComplete();
        }

        class FinalConfirmationCallback final : public RE::IMessageBoxCallback
        {
        public:
            explicit FinalConfirmationCallback(std::string a_requestID) :
                _requestID(std::move(a_requestID))
            {}

            void Run(Message a_message) override
            {
                auto& state = GetState();
                if (!state.session ||
                    state.session->requestID != _requestID) {
                    return;
                }
                state.session->finalPromptOpen = false;
                if (a_message == Message::kUnk0) {
                    ConfirmLocalOffer();
                } else {
                    ResetLocalApproval();
                    SendLocalState(false);
                    OpenOfferInventory();
                }
            }

        private:
            std::string _requestID;
        };

        void ShowFinalPrompt()
        {
            auto& state = GetState();
            if (!state.session ||
                !state.session->localReady ||
                !state.session->remoteReady ||
                state.session->finalPromptOpen) {
                return;
            }

            state.session->finalPromptOpen = true;
            const auto summary = BuildOfferSummary(true);
            RE::CreateMessage(
                summary.c_str(),
                new FinalConfirmationCallback(state.session->requestID),
                0,
                4,
                10,
                "Confirm",
                "Modify");
        }

        RE::ItemList::Item* GetSelectedInventoryItem()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
                return nullptr;
            }
            auto menu = ui->GetMenu<RE::InventoryMenu>();
            if (!menu) {
                return nullptr;
            }
            auto* itemList = menu->GetRuntimeData().itemList;
            return itemList ? itemList->GetSelectedItem() : nullptr;
        }

        void EditSelectedItem(bool a_add)
        {
            auto& state = GetState();
            if (!state.session) {
                return;
            }

            auto* selected = GetSelectedInventoryItem();
            if (!selected || !selected->data.objDesc) {
                Notify("TradeTogether: select an item in your inventory.");
                return;
            }

            auto* entry = selected->data.objDesc;
            auto* object = entry->GetObject();
            const auto* displayName = entry->GetDisplayName();
            const auto available = selected->data.GetCount();
            if (!object || !displayName || !*displayName || available == 0) {
                Notify("TradeTogether: this item cannot be added.");
                return;
            }
            if (object->GetFormID() == kGoldFormID) {
                Notify("TradeTogether: use the configured gold key.");
                return;
            }
            if (entry->IsQuestObject()) {
                Notify("TradeTogether: quest items cannot be offered.");
                return;
            }

            struct SelectedStack
            {
                std::string instanceData;
                std::uint32_t available{ 0 };
            };

            std::vector<SelectedStack> selectedStacks;
            std::int32_t represented = 0;

            if (entry->extraLists) {
                for (auto* extraList : *entry->extraLists) {
                    if (!extraList) {
                        continue;
                    }

                    const auto stackCount =
                        std::max(extraList->GetCount(), 1);
                    const auto* instanceName =
                        extraList->GetDisplayName(object);
                    if (!instanceName ||
                        std::string_view(instanceName) != displayName) {
                        continue;
                    }

                    InstanceData::Snapshot snapshot{};
                    std::string snapshotError;
                    if (!InstanceData::Capture(
                            object,
                            extraList,
                            snapshot,
                            snapshotError)) {
                        Notify(fmt::format(
                            "TradeTogether: {} cannot be traded exactly ({}).",
                            displayName,
                            snapshotError));
                        spdlog::warn(
                            "Exact instance capture rejected: form={:08X} name=\"{}\" reason=\"{}\"",
                            object->GetFormID(),
                            displayName,
                            snapshotError);
                        return;
                    }

                    represented += stackCount;
                    const auto payload =
                        InstanceData::Encode(snapshot);
                    auto existing = std::find_if(
                        selectedStacks.begin(),
                        selectedStacks.end(),
                        [&](const SelectedStack& a_stack) {
                            return a_stack.instanceData == payload;
                        });
                    if (existing == selectedStacks.end()) {
                        selectedStacks.push_back(
                            SelectedStack{
                                payload,
                                static_cast<std::uint32_t>(stackCount)
                            });
                    } else {
                        existing->available +=
                            static_cast<std::uint32_t>(stackCount);
                    }
                }
            }

            const auto baseCount =
                std::max(
                    static_cast<std::int32_t>(available) - represented,
                    0);
            if (baseCount > 0) {
                auto base = std::find_if(
                    selectedStacks.begin(),
                    selectedStacks.end(),
                    [](const SelectedStack& a_stack) {
                        return a_stack.instanceData == "-";
                    });
                if (base == selectedStacks.end()) {
                    selectedStacks.push_back(
                        SelectedStack{
                            "-",
                            static_cast<std::uint32_t>(baseCount)
                        });
                } else {
                    base->available +=
                        static_cast<std::uint32_t>(baseCount);
                }
            }

            if (selectedStacks.empty()) {
                // A menu row with no explicit ExtraData is a plain base stack.
                selectedStacks.push_back(
                    SelectedStack{ "-", available });
            }

            auto& offer = state.session->localOffer;

            if (a_add) {
                OfferLine* chosenLine = nullptr;
                const SelectedStack* chosenStack = nullptr;

                for (const auto& stack : selectedStacks) {
                    auto iterator = std::find_if(
                        offer.begin(),
                        offer.end(),
                        [&](const OfferLine& a_line) {
                            return a_line.formID == object->GetFormID() &&
                                   a_line.name == displayName &&
                                   a_line.instanceData == stack.instanceData;
                        });

                    const auto alreadyOffered =
                        iterator != offer.end() ?
                            iterator->quantity : 0;
                    if (alreadyOffered < stack.available) {
                        chosenStack = std::addressof(stack);
                        if (iterator == offer.end()) {
                            if (offer.size() >= 24) {
                                Notify("TradeTogether: the offer is limited to 24 item lines.");
                                return;
                            }
                            offer.push_back(OfferLine{
                                object->GetFormID(),
                                displayName,
                                0,
                                stack.available,
                                stack.instanceData
                            });
                            chosenLine = std::addressof(offer.back());
                        } else {
                            chosenLine = std::addressof(*iterator);
                            chosenLine->available = stack.available;
                        }
                        break;
                    }
                }

                if (!chosenLine || !chosenStack) {
                    Notify("TradeTogether: the entire stack is already in the offer.");
                    return;
                }

                ++chosenLine->quantity;
                ResetLocalApproval();
                SendLocalState(false);
                Notify(fmt::format(
                    "Offer: {} x {}",
                    chosenLine->quantity,
                    chosenLine->name));
            } else {
                auto iterator = std::find_if(
                    offer.rbegin(),
                    offer.rend(),
                    [&](const OfferLine& a_line) {
                        return a_line.formID == object->GetFormID() &&
                               a_line.name == displayName;
                    });
                if (iterator == offer.rend()) {
                    Notify("TradeTogether: this item is not in the offer.");
                    return;
                }

                if (iterator->quantity > 1) {
                    --iterator->quantity;
                } else {
                    offer.erase(std::next(iterator).base());
                }

                ResetLocalApproval();
                SendLocalState(false);
                Notify(fmt::format(
                    "Offer updated: {}",
                    displayName));
            }

            spdlog::info(
                "Local offer edited: action={} form={:08X} name=\"{}\" available={} lines={} exactInstances=1",
                a_add ? "add" : "remove",
                object->GetFormID(),
                displayName,
                available,
                offer.size());
        }

        void StartSession(
            std::string a_requestID,
            std::string a_peerName,
            bool a_initiator,
            RE::ActorHandle a_target = {},
            RE::FormID a_targetFormID = 0)
        {
            auto& state = GetState();
            TradeSession session{};
            session.requestID = std::move(a_requestID);
            session.peerName = std::move(a_peerName);
            session.target = std::move(a_target);
            session.targetFormID = a_targetFormID;
            session.initiator = a_initiator;
            session.lastActivity = std::chrono::steady_clock::now();
            state.session = std::move(session);

            SendLocalState(false);
            ScheduleOpenOfferInventory();
            spdlog::info(
                "Trade offer session started: request={} peer=\"{}\" initiator={}",
                state.session->requestID,
                state.session->peerName,
                a_initiator ? 1 : 0);
        }

        bool SendResponse(
            const std::string& a_requestID,
            const std::string& a_requester,
            bool a_acceptedd)
        {
            const auto payload = fmt::format(
                "type=RESPONSE|request={}|to={}|acceptedd={}",
                a_requestID,
                Protocol::HexEncode(a_requester),
                a_acceptedd ? 1 : 0);
            return UdpTransport::GetSingleton().SendTo(a_requester, payload);
        }

        void RespondToRequest(
            std::string a_requestID,
            std::string a_requester,
            std::chrono::steady_clock::time_point a_receivedAt,
            bool a_acceptedd)
        {
            auto& state = GetState();
            const auto expired =
                std::chrono::steady_clock::now() - a_receivedAt >
                std::chrono::milliseconds(state.config.requestTimeoutMs);
            if (expired || state.session) {
                a_acceptedd = false;
            }

            if (a_acceptedd) {
                StartSession(a_requestID, a_requester, false);
            }
            if (!SendResponse(a_requestID, a_requester, a_acceptedd)) {
                if (a_acceptedd) {
                    state.session.reset();
                }
                Notify("TradeTogether: could not send the response.");
                return;
            }

            if (expired) {
                Notify("TradeTogether: this trade request expired.");
            } else {
                Notify(fmt::format(
                    "TradeTogether: trade {} for {}.",
                    a_acceptedd ? "accepted" : "declined",
                    a_requester));
            }
            spdlog::info(
                "Trade request answered: request={} requester=\"{}\" acceptedd={} expired={}",
                a_requestID,
                a_requester,
                a_acceptedd ? 1 : 0,
                expired ? 1 : 0);
        }

        class TradePromptCallback final : public RE::IMessageBoxCallback
        {
        public:
            TradePromptCallback(
                std::string a_requestID,
                std::string a_requester,
                std::chrono::steady_clock::time_point a_receivedAt) :
                _requestID(std::move(a_requestID)),
                _requester(std::move(a_requester)),
                _receivedAt(a_receivedAt)
            {}

            void Run(Message a_message) override
            {
                RespondToRequest(
                    std::move(_requestID),
                    std::move(_requester),
                    _receivedAt,
                    a_message == Message::kUnk0);
            }

        private:
            std::string _requestID;
            std::string _requester;
            std::chrono::steady_clock::time_point _receivedAt;
        };

        void HandleIncomingRequest(
            const std::string& a_requestID,
            const std::string& a_requester)
        {
            auto& state = GetState();
            if (state.recentIncoming.contains(a_requestID)) {
                return;
            }
            state.recentIncoming.emplace(
                a_requestID,
                std::chrono::steady_clock::now());

            if (state.session || state.pending) {
                SendResponse(a_requestID, a_requester, false);
                spdlog::info(
                    "Trade request declinedd automatically: busy with another trade");
                return;
            }

            const auto prompt = fmt::format(
                "{} souhaite echanger avec vous.\n"
                "Accept et composer votre offre ?",
                a_requester);
            RE::CreateMessage(
                prompt.c_str(),
                new TradePromptCallback(
                    a_requestID,
                    a_requester,
                    std::chrono::steady_clock::now()),
                0,
                4,
                10,
                "Accept",
                "Decline");
            spdlog::info(
                "Trade confirmation displayed: request={} requester=\"{}\"",
                a_requestID,
                a_requester);
        }

        void HandleIncomingResponse(
            const std::string& a_requestID,
            const std::string& a_responder,
            bool a_acceptedd)
        {
            auto& state = GetState();
            if (!state.pending ||
                state.pending->requestID != a_requestID ||
                !Protocol::EqualsInsensitive(
                    state.pending->targetName,
                    a_responder)) {
                spdlog::warn(
                    "Unexpected trade response ignored: request={} responder=\"{}\"",
                    a_requestID,
                    a_responder);
                return;
            }

            PendingTrade pending = std::move(*state.pending);
            state.pending.reset();
            if (std::chrono::steady_clock::now() - pending.sentAt >
                std::chrono::milliseconds(state.config.requestTimeoutMs)) {
                Notify("TradeTogether: the trade request expired.");
                return;
            }
            if (!a_acceptedd) {
                Notify(fmt::format(
                    "TradeTogether: {} a declined l'echange.",
                    a_responder));
                return;
            }

            auto actor = pending.target.get();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!actor || actor.get() == player) {
                Notify("TradeTogether: the target player is no longer available.");
                return;
            }
            const auto currentName = GetActorName(actor.get());
            if (currentName.empty() ||
                !Protocol::EqualsInsensitive(currentName, pending.targetName)) {
                Notify("TradeTogether: the trade target changed.");
                return;
            }

            StartSession(
                pending.requestID,
                pending.targetName,
                true,
                pending.target,
                pending.targetFormID);
            Notify(fmt::format(
                "TradeTogether: {} a accepted. Compose ton offre.",
                pending.targetName));
        }

        bool RequestCrosshairActorTrade()
        {
            auto& state = GetState();
            if (!state.initialized ||
                !UdpTransport::GetSingleton().IsRunning()) {
                Notify("TradeTogether: network confirmation unavailable.");
                return false;
            }
            if (state.session) {
                ShowOfferSummary();
                return false;
            }
            if (state.pending) {
                Notify(fmt::format(
                    "TradeTogether: waiting for {}.",
                    state.pending->targetName));
                return false;
            }

            auto* actor = GetCrosshairActor();
            if (!actor) {
                return false;
            }
            const auto displayName = GetActorName(actor);
            if (displayName.empty()) {
                Notify("TradeTogether: the target has no network name.");
                return false;
            }

            const auto requestID = MakeRequestID();
            const auto payload = fmt::format(
                "type=REQUEST|request={}|to={}",
                requestID,
                Protocol::HexEncode(displayName));
            state.pending = PendingTrade{
                requestID,
                actor->CreateRefHandle(),
                actor->GetFormID(),
                displayName,
                std::chrono::steady_clock::now()
            };
            if (!UdpTransport::GetSingleton().SendTo(displayName, payload)) {
                state.pending.reset();
                Notify("TradeTogether: could not send the trade request.");
                return false;
            }

            Notify(fmt::format(
                "TradeTogether: trade request sent to {}.",
                displayName));
            spdlog::info(
                "Trade requested: request={} form={:08X} name=\"{}\"",
                requestID,
                actor->GetFormID(),
                displayName);
            return true;
        }
    }

    bool Initialize()
    {
        auto& state = GetState();
        if (state.initialized) {
            return true;
        }

        state.config = Config::Load();
        state.initialized = UdpTransport::GetSingleton().Start(
            state.config,
            [](std::string a_packet) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(
                        [packet = std::move(a_packet)]() mutable {
                            HandleNetworkPacket(std::move(packet));
                        });
                } else {
                    spdlog::error(
                        "Trade packet dropped: SKSE task interface unavailable");
                }
            });
        return state.initialized;
    }

    void Shutdown()
    {
        UdpTransport::GetSingleton().Stop();
        auto& state = GetState();
        state.pending.reset();
        state.session.reset();
        state.recentIncoming.clear();
        state.initialized = false;
    }

    void Reset()
    {
        auto& state = GetState();
        state.pending.reset();
        state.session.reset();
        state.recentIncoming.clear();
        spdlog::info("Trade request and offer state reset");
    }

    void Update()
    {
        auto& state = GetState();
        const auto now = std::chrono::steady_clock::now();
        const auto requestTimeout =
            std::chrono::milliseconds(state.config.requestTimeoutMs);

        if (state.pending && now - state.pending->sentAt > requestTimeout) {
            spdlog::info(
                "Trade request expired: request={} target=\"{}\"",
                state.pending->requestID,
                state.pending->targetName);
            state.pending.reset();
            Notify("TradeTogether: aucune reponse, demande expiree.");
        }

        if (state.session &&
            now - state.session->lastActivity >
                std::chrono::milliseconds(state.config.sessionTimeoutMs)) {
            CancelSession(true, "timed out");
        }

        const auto historyLifetime = std::max(
            requestTimeout * 2,
            std::chrono::milliseconds(60000));
        for (auto iterator = state.recentIncoming.begin();
             iterator != state.recentIncoming.end();) {
            if (now - iterator->second > historyLifetime) {
                iterator = state.recentIncoming.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void HandleKey(std::uint32_t a_scanCode)
    {
        Update();

        auto& state = GetState();
        if (a_scanCode == kTabScanCode && state.session) {
            CancelSession(true, "cancelled locally");
            return;
        }
        if (a_scanCode == kEScanCode && state.session) {
            EditSelectedItem(true);
            return;
        }
        if (a_scanCode == kDeleteScanCode && state.session) {
            EditSelectedItem(false);
            return;
        }
        if (a_scanCode == kF6ScanCode) {
            RequestCrosshairActorTrade();
        }
    }

    void AdjustGold(std::int32_t a_delta)
    {
        Update();

        auto& state = GetState();
        if (!state.session || !IsInventoryOpen() || a_delta == 0) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGoldFormID);
        if (!player || !gold) {
            Notify("TradeTogether: could not read your gold.");
            return;
        }

        const auto counts = player->GetInventoryCounts();
        const auto iterator = counts.find(gold);
        const auto available = iterator != counts.end() ?
            std::max(iterator->second, 0) : 0;

        const auto current = static_cast<std::int64_t>(state.session->localGold);
        const auto requested = current + static_cast<std::int64_t>(a_delta);
        const auto next = std::clamp<std::int64_t>(requested, 0, available);
        if (next == current) {
            Notify(fmt::format(
                "Offer: {} gold ({} available)",
                current,
                available));
            return;
        }

        state.session->localGold = static_cast<std::uint32_t>(next);
        ResetLocalApproval();
        SendLocalState(false);
        Notify(fmt::format(
            "Offer: {} gold / {} available",
            state.session->localGold,
            available));
        spdlog::info(
            "Local gold offer edited: delta={} gold={} available={}",
            a_delta,
            state.session->localGold,
            available);
    }

    void HandleNetworkPacket(std::string a_packet)
    {
        if (!a_packet.starts_with(kGameplayPrefix)) {
            return;
        }
        Update();

        const auto type = Protocol::ReadField(a_packet, "type");
        const auto requestID = Protocol::ReadField(a_packet, "request");
        const auto encodedFrom = Protocol::ReadField(a_packet, "from");
        const auto encodedTo = Protocol::ReadField(a_packet, "to");
        if (!type || !requestID || requestID->empty() ||
            !encodedFrom || !encodedTo) {
            spdlog::warn("Malformed trade packet ignored");
            return;
        }

        const auto from = Protocol::HexDecode(*encodedFrom);
        const auto to = Protocol::HexDecode(*encodedTo);
        if (!from || from->empty() || from->size() > 256 ||
            !to || to->empty() || to->size() > 256) {
            spdlog::warn("Trade packet contains invalid player names");
            return;
        }

        const auto localName = GetLocalPlayerName();
        if (!Protocol::EqualsInsensitive(*to, localName) ||
            Protocol::EqualsInsensitive(*from, localName)) {
            return;
        }

        if (*type == "REQUEST") {
            HandleIncomingRequest(*requestID, *from);
            return;
        }
        if (*type == "RESPONSE") {
            const auto acceptedd = Protocol::ReadField(a_packet, "acceptedd");
            if (!acceptedd || (*acceptedd != "0" && *acceptedd != "1")) {
                spdlog::warn("Malformed trade response ignored");
                return;
            }
            HandleIncomingResponse(*requestID, *from, *acceptedd == "1");
            return;
        }

        auto& state = GetState();
        if (!state.session ||
            state.session->requestID != *requestID ||
            !Protocol::EqualsInsensitive(state.session->peerName, *from)) {
            spdlog::warn(
                "Trade session packet ignored: type={} request={} from=\"{}\"",
                *type,
                *requestID,
                *from);
            return;
        }
        state.session->lastActivity = std::chrono::steady_clock::now();

        if (*type == "STATE") {
            const auto sequence = ParseRevision(
                Protocol::ReadField(a_packet, "seq"));
            const auto ready = Protocol::ReadField(a_packet, "ready");
            const auto gold = ParseGold(Protocol::ReadField(a_packet, "gold"));
            const auto items = Protocol::ReadField(a_packet, "items");
            const auto offer = items ? DecodeOffer(*items) : std::nullopt;
            if (!sequence || *sequence == 0 ||
                !ready || (*ready != "0" && *ready != "1") ||
                !gold || !offer) {
                spdlog::warn("Malformed remote trade state ignored");
                return;
            }
            if (*sequence <= state.session->remoteRevision) {
                spdlog::debug(
                    "Stale remote trade state ignored: received={} current={}",
                    *sequence,
                    state.session->remoteRevision);
                return;
            }

            state.session->remoteRevision = *sequence;
            state.session->remoteOffer = *offer;
            state.session->remoteGold = *gold;
            state.session->remoteReady = *ready == "1";
            state.session->localConfirmed = false;
            state.session->remoteConfirmed = false;
            state.session->finalPromptOpen = false;
            Notify(fmt::format(
                "TradeTogether: {} {}.",
                state.session->peerName,
                state.session->remoteReady ? "is ready" : "is editing son offre"));
            if (state.session->localReady && state.session->remoteReady) {
                ShowFinalPrompt();
            }
            return;
        }
        if (*type == "CONFIRM") {
            const auto remoteRevision = ParseRevision(
                Protocol::ReadField(a_packet, "mine"));
            const auto localRevision = ParseRevision(
                Protocol::ReadField(a_packet, "theirs"));
            if (!remoteRevision || !localRevision ||
                !state.session->localReady ||
                !state.session->remoteReady ||
                *remoteRevision != state.session->remoteRevision ||
                *localRevision != state.session->localRevision) {
                spdlog::warn(
                    "Stale or malformed trade confirmation ignored: remote={} local={}",
                    remoteRevision.value_or(0),
                    localRevision.value_or(0));
                return;
            }
            state.session->remoteConfirmed = true;
            Notify(fmt::format(
                "TradeTogether: {} confirmed the trade.",
                state.session->peerName));
            FinalizeIfComplete();
            return;
        }
        if (*type == "CANCEL") {
            CancelSession(false, "cancelled remotely");
            return;
        }

        spdlog::warn("Unknown trade packet type ignored: {}", *type);
    }
}
