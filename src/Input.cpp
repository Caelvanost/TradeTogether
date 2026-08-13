#include "PCH.h"
#include "Input.h"
#include "Trade.h"

namespace TradeTogether
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
                spdlog::error(
                    "Cannot open inventory: Papyrus object handle policy is unavailable");
                return false;
            }

            const auto handle = handlePolicy->GetHandleForObject(
                a_actor->GetFormType(),
                a_actor);
            if (handle == handlePolicy->EmptyHandle()) {
                spdlog::error(
                    "Cannot open inventory: no Papyrus handle for actor {:08X}",
                    a_actor->GetFormID());
                return false;
            }

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
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

        bool OpenCrosshairActorInventory()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* pickData = RE::CrosshairPickData::GetSingleton();

            if (!player || !pickData) {
                spdlog::warn(
                    "T ignored: PlayerCharacter or CrosshairPickData unavailable");
                Notify("TradeTogether: ciblage indisponible.");
                return false;
            }

            auto target = pickData->targetActor.get();
            if (!target) {
                target = pickData->target.get();
            }

            if (!target) {
                spdlog::info("T: no crosshair target");
                Notify("TradeTogether: vise l'autre joueur.");
                return false;
            }

            auto* actor = target->As<RE::Actor>();
            if (!actor) {
                spdlog::info(
                    "T: target {:08X} is not an actor",
                    target->GetFormID());
                Notify("TradeTogether: la cible n'est pas un acteur.");
                return false;
            }

            if (actor == player) {
                spdlog::info("T: local player targeted; ignored");
                Notify("TradeTogether: vise l'autre joueur.");
                return false;
            }

            const char* displayName = actor->GetDisplayFullName();
            if (!displayName || displayName[0] == '\0') {
                displayName = "<sans nom>";
            }

            spdlog::info(
                "Direct trade target: form={:08X} name=\"{}\" base={:08X} type={}",
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

            spdlog::info(
                "Actor.OpenInventory(true) dispatched successfully for {:08X}",
                actor->GetFormID());
            return true;
        }
    }

    InputEventSink* InputEventSink::GetSingleton()
    {
        static InputEventSink singleton;
        return std::addressof(singleton);
    }

    void InputEventSink::Register()
    {
        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            spdlog::error("Could not register input sink: BSInputDeviceManager is null");
            return;
        }

        inputManager->AddEventSink(GetSingleton());
        spdlog::info(
            "Input event sink registered (direct trade key=T / DIK 0x14)");
    }

    RE::BSEventNotifyControl InputEventSink::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        // Keep the existing subsystem serviced, even though the direct-inventory
        // branch no longer uses the synchronized offer workflow from keyboard input.
        Trade::Update();

        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button || !button->IsDown()) {
                continue;
            }

            if (button->GetIDCode() == kTScanCode) {
                spdlog::info("T pressed: opening targeted actor inventory directly");
                OpenCrosshairActorInventory();
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
