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
        spdlog::info(
            "Input event sink registered (T=request/validate, A=add, Delete=remove, Tab=cancel)");
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

            const auto scanCode = button->GetIDCode();
            if (scanCode == kTScanCode) {
                spdlog::debug("Trade key pressed: T / DIK 0x{:02X}", scanCode);
                Trade::HandleKey(kTradeActionCode);
                break;
            }

            if (scanCode == kAScanCode) {
                spdlog::debug("Trade add key pressed: A / DIK 0x{:02X}", scanCode);
                Trade::HandleKey(kAddActionCode);
                break;
            }

            if (scanCode == kDeleteScanCode ||
                scanCode == kTabScanCode) {
                spdlog::debug("Trade key pressed: DIK 0x{:02X}", scanCode);
                Trade::HandleKey(scanCode);
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
