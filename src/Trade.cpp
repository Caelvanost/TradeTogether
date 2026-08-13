#include "PCH.h"
#include "Trade.h"

namespace TradeTogether::Trade
{
    namespace
    {
        void Notify(const char* a_message)
        {
            RE::DebugNotification(a_message);
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
    }

    bool OpenCrosshairActorInventory()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pickData = RE::CrosshairPickData::GetSingleton();

        if (!player || !pickData) {
            spdlog::warn("F6 ignored: PlayerCharacter or CrosshairPickData unavailable");
            Notify("TradeTogether: ciblage indisponible.");
            return false;
        }

        // Skyrim Together's remote player representation is still an Actor in the
        // local game. Prefer targetActor; fall back to the generic crosshair target.
        // ObjectRefHandle::get() returns NiPointer<TESObjectREFR> in CommonLibSSE-NG
        // 3.5.2. Keep the smart pointer alive while we inspect/use the target.
        auto target = pickData->targetActor.get();
        if (!target) {
            target = pickData->target.get();
        }

        if (!target) {
            spdlog::info("F6: no crosshair target");
            Notify("TradeTogether: vise l'autre joueur.");
            return false;
        }

        auto* actor = target->As<RE::Actor>();
        if (!actor) {
            spdlog::info("F6: target {:08X} is not an actor", target->GetFormID());
            Notify("TradeTogether: la cible n'est pas un acteur.");
            return false;
        }

        if (actor == player) {
            spdlog::info("F6: local player targeted; ignored");
            Notify("TradeTogether: vise l'autre joueur.");
            return false;
        }

        const char* displayName = actor->GetDisplayFullName();
        if (!displayName || displayName[0] == '\0') {
            displayName = "<sans nom>";
        }

        spdlog::info(
            "Trade target: form={:08X} name=\"{}\" base={:08X} type={}",
            actor->GetFormID(),
            displayName,
            actor->GetActorBase() ? actor->GetActorBase()->GetFormID() : 0,
            static_cast<std::uint32_t>(actor->GetFormType()));

        if (!OpenInventoryThroughPapyrus(actor)) {
            Notify("TradeTogether: impossible d'ouvrir l'inventaire.");
            return false;
        }

        std::string notification = "TradeTogether: echange avec ";
        notification += displayName;
        Notify(notification.c_str());

        spdlog::info("Actor.OpenInventory(true) dispatched successfully for {:08X}", actor->GetFormID());
        return true;
    }
}
