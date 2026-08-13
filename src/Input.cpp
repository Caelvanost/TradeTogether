#include "PCH.h"
#include "Input.h"
#include "Trade.h"

namespace TradeTogether
{
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
        spdlog::info("Input event sink registered (trade key=F6 / DIK 0x40)");
    }

    RE::BSEventNotifyControl InputEventSink::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
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

            if (button->GetIDCode() == kF6ScanCode) {
                spdlog::info("F6 pressed");
                Trade::RequestCrosshairActorTrade();
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
