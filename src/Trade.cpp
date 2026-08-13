#include "PCH.h"
#include "Trade.h"
#include "Config.h"
#include "Protocol.h"
#include "UdpTransport.h"

namespace TradeTogether::Trade
{
    namespace
    {
        constexpr std::string_view kGameplayPrefix = "TTNET|v1|";

        struct PendingTrade
        {
            std::string requestID;
            RE::ActorHandle target;
            RE::FormID targetFormID{ 0 };
            std::string targetName;
            std::chrono::steady_clock::time_point sentAt{};
        };

        struct TradeState
        {
            Config config{};
            std::optional<PendingTrade> pending;
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

        bool OpenInventoryThroughPapyrus(RE::Actor* a_actor)
        {
            if (!a_actor) {
                return false;
            }

            auto* skyrimVM = RE::SkyrimVM::GetSingleton();
            if (!skyrimVM || !skyrimVM->impl) {
                spdlog::error("Cannot open inventory: SkyrimVM is unavailable");
                return false;
            }

            auto* vm = skyrimVM->impl.get();
            auto* handlePolicy = vm->GetObjectHandlePolicy();
            if (!handlePolicy) {
                spdlog::error("Cannot open inventory: Papyrus object handle policy is unavailable");
                return false;
            }

            const auto handle = handlePolicy->GetHandleForObject(a_actor->GetFormType(), a_actor);
            if (handle == handlePolicy->EmptyHandle()) {
                spdlog::error(
                    "Cannot open inventory: no Papyrus handle for actor {:08X}",
                    a_actor->GetFormID());
                return false;
            }

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

            // Actor.OpenInventory(true): force opening the actor inventory even when the
            // actor is not a normal teammate. This keeps Skyrim/STR in charge of the
            // actual inventory operation and the synchronization of item instances.
            const bool dispatched = vm->DispatchMethodCall(
                handle,
                RE::BSFixedString("Actor"),
                RE::BSFixedString("OpenInventory"),
                RE::MakeFunctionArguments(true),
                callback);

            if (!dispatched) {
                spdlog::error(
                    "Papyrus dispatch Actor.OpenInventory(true) failed for actor {:08X}",
                    a_actor->GetFormID());
            }

            return dispatched;
        }

        RE::Actor* GetCrosshairActor()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* pickData = RE::CrosshairPickData::GetSingleton();

            if (!player || !pickData) {
                spdlog::warn(
                    "F6 ignored: PlayerCharacter or CrosshairPickData unavailable");
                Notify("TradeTogether: ciblage indisponible.");
                return nullptr;
            }

            // Skyrim Together's remote player representation is still an
            // Actor in the local game. Prefer targetActor; fall back to the
            // generic crosshair target.
            auto target = pickData->targetActor.get();
            if (!target) {
                target = pickData->target.get();
            }

            if (!target) {
                spdlog::info("F6: no crosshair target");
                Notify("TradeTogether: vise l'autre joueur.");
                return nullptr;
            }

            auto* actor = target->As<RE::Actor>();
            if (!actor) {
                spdlog::info(
                    "F6: target {:08X} is not an actor",
                    target->GetFormID());
                Notify("TradeTogether: la cible n'est pas un acteur.");
                return nullptr;
            }

            if (actor == player) {
                spdlog::info("F6: local player targeted; ignored");
                Notify("TradeTogether: vise l'autre joueur.");
                return nullptr;
            }

            return actor;
        }

        void RespondToRequest(
            std::string a_requestID,
            std::string a_requester,
            std::chrono::steady_clock::time_point a_receivedAt,
            bool a_accepted)
        {
            const auto& config = GetState().config;
            const auto expired =
                std::chrono::steady_clock::now() - a_receivedAt >
                std::chrono::milliseconds(config.requestTimeoutMs);
            if (expired) {
                a_accepted = false;
                Notify("TradeTogether: cette demande a expire.");
            }

            const auto payload = fmt::format(
                "type=RESPONSE|request={}|to={}|accepted={}",
                a_requestID,
                Protocol::HexEncode(a_requester),
                a_accepted ? 1 : 0);
            if (!UdpTransport::GetSingleton().SendTo(a_requester, payload)) {
                Notify("TradeTogether: impossible d'envoyer la reponse.");
                return;
            }

            if (!expired) {
                Notify(fmt::format(
                    "TradeTogether: echange {} pour {}.",
                    a_accepted ? "accepte" : "refuse",
                    a_requester));
            }
            spdlog::info(
                "Trade request answered: request={} requester=\"{}\" accepted={} expired={}",
                a_requestID,
                a_requester,
                a_accepted ? 1 : 0,
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
                spdlog::debug(
                    "Duplicate trade request ignored: {}",
                    a_requestID);
                return;
            }

            const auto receivedAt = std::chrono::steady_clock::now();
            state.recentIncoming.emplace(a_requestID, receivedAt);

            const auto prompt = fmt::format(
                "{} souhaite echanger avec vous.\n"
                "L'autoriser a ouvrir votre inventaire ?",
                a_requester);
            RE::CreateMessage(
                prompt.c_str(),
                new TradePromptCallback(
                    a_requestID,
                    a_requester,
                    receivedAt),
                0,
                4,
                10,
                "Accepter",
                "Refuser");

            spdlog::info(
                "Trade confirmation displayed: request={} requester=\"{}\"",
                a_requestID,
                a_requester);
        }

        void HandleIncomingResponse(
            const std::string& a_requestID,
            const std::string& a_responder,
            bool a_accepted)
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
                spdlog::info(
                    "Late trade response ignored: request={} responder=\"{}\"",
                    a_requestID,
                    a_responder);
                Notify("TradeTogether: la demande d'echange a expire.");
                return;
            }

            if (!a_accepted) {
                Notify(fmt::format(
                    "TradeTogether: {} a refuse l'echange.",
                    a_responder));
                spdlog::info(
                    "Trade request declined: request={} target=\"{}\"",
                    a_requestID,
                    a_responder);
                return;
            }

            auto actor = pending.target.get();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!actor || actor.get() == player) {
                spdlog::warn(
                    "Accepted trade target is no longer available: request={} form={:08X}",
                    a_requestID,
                    pending.targetFormID);
                Notify("TradeTogether: le joueur cible n'est plus disponible.");
                return;
            }

            const auto currentName = GetActorName(actor.get());
            if (currentName.empty() ||
                !Protocol::EqualsInsensitive(currentName, pending.targetName)) {
                spdlog::warn(
                    "Accepted trade target changed: request={} expected=\"{}\" actual=\"{}\" form={:08X}",
                    a_requestID,
                    pending.targetName,
                    currentName,
                    actor->GetFormID());
                Notify("TradeTogether: la cible de l'echange a change.");
                return;
            }

            if (!OpenInventoryThroughPapyrus(actor.get())) {
                Notify("TradeTogether: impossible d'ouvrir l'inventaire.");
                return;
            }

            Notify(fmt::format(
                "TradeTogether: echange accepte par {}.",
                pending.targetName));
            spdlog::info(
                "Accepted trade opened: request={} actor={:08X} name=\"{}\"",
                a_requestID,
                actor->GetFormID(),
                pending.targetName);
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
        state.recentIncoming.clear();
        state.initialized = false;
    }

    void Reset()
    {
        auto& state = GetState();
        state.pending.reset();
        state.recentIncoming.clear();
        spdlog::info("Trade request state reset");
    }

    void Update()
    {
        auto& state = GetState();
        const auto now = std::chrono::steady_clock::now();
        const auto timeout =
            std::chrono::milliseconds(state.config.requestTimeoutMs);

        if (state.pending && now - state.pending->sentAt > timeout) {
            spdlog::info(
                "Trade request expired: request={} target=\"{}\"",
                state.pending->requestID,
                state.pending->targetName);
            state.pending.reset();
            Notify("TradeTogether: aucune reponse, demande expiree.");
        }

        const auto historyLifetime = std::max(
            timeout * 2,
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

    bool RequestCrosshairActorTrade()
    {
        Update();

        auto& state = GetState();
        if (!state.initialized ||
            !UdpTransport::GetSingleton().IsRunning()) {
            spdlog::warn("F6 ignored: trade confirmation network unavailable");
            Notify("TradeTogether: confirmation reseau indisponible.");
            return false;
        }
        if (state.pending) {
            Notify(fmt::format(
                "TradeTogether: en attente de {}.",
                state.pending->targetName));
            return false;
        }

        auto* actor = GetCrosshairActor();
        if (!actor) {
            return false;
        }

        const auto displayName = GetActorName(actor);
        if (displayName.empty()) {
            spdlog::warn(
                "F6: actor {:08X} has no usable player name",
                actor->GetFormID());
            Notify("TradeTogether: la cible n'a pas de nom reseau.");
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
            Notify("TradeTogether: impossible d'envoyer la demande.");
            return false;
        }

        spdlog::info(
            "Trade requested: request={} form={:08X} name=\"{}\" base={:08X} type={}",
            requestID,
            actor->GetFormID(),
            displayName,
            actor->GetActorBase() ? actor->GetActorBase()->GetFormID() : 0,
            static_cast<std::uint32_t>(actor->GetFormType()));
        Notify(fmt::format(
            "TradeTogether: demande envoyee a {}.",
            displayName));
        return true;
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
            const auto accepted = Protocol::ReadField(a_packet, "accepted");
            if (!accepted || (*accepted != "0" && *accepted != "1")) {
                spdlog::warn("Malformed trade response ignored");
                return;
            }
            HandleIncomingResponse(*requestID, *from, *accepted == "1");
            return;
        }

        spdlog::warn("Unknown trade packet type ignored: {}", *type);
    }
}
