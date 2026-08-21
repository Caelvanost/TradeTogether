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
            "Input event sink registered (T=request/validate, Insert=add, Delete=remove, Numpad +/-=gold, Tab=cancel; MessageBoxMenu input isolated)");
    }

    RE::BSEventNotifyControl InputEventSink::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        Trade::Update();

        // SkyUI / InventoryMenu does not consistently expose the numpad +/-
        // keys through ButtonEvent::GetIDCode(). Read their physical Windows
        // virtual-key state directly and trigger only on the transition from
        // released to pressed. Release events still pass through this sink, so
        // the latch is reset without generating repeated gold changes.
        static bool addWasDown = false;
        static bool subtractWasDown = false;
        static bool messageBoxWasOpen = false;

        const bool addDown = (GetAsyncKeyState(VK_ADD) & 0x8000) != 0;
        const bool subtractDown = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;

        // Skyrim Souls RE deliberately lets MessageBoxMenu run while the game is
        // unpaused. In that configuration our global input sink also continues
        // receiving keyboard events. Never let a key intended for the message
        // box trigger a TradeTogether action behind it.
        auto* ui = RE::UI::GetSingleton();
        const bool messageBoxOpen =
            ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
        if (messageBoxOpen) {
            if (!messageBoxWasOpen) {
                spdlog::debug(
                    "MessageBoxMenu opened; TradeTogether hotkeys suspended until it closes");
            }
            messageBoxWasOpen = true;
            addWasDown = addDown;
            subtractWasDown = subtractDown;
            return RE::BSEventNotifyControl::kContinue;
        }
        if (messageBoxWasOpen) {
            spdlog::debug("MessageBoxMenu closed; TradeTogether hotkeys resumed");
            messageBoxWasOpen = false;
        }

        if ((addDown && !addWasDown) ||
            (subtractDown && !subtractWasDown)) {
            const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const std::int32_t step = control ? 100 : (shift ? 10 : 1);
            const std::int32_t delta = addDown && !addWasDown ? step : -step;

            spdlog::info(
                "Trade gold key pressed: source=Win32 key={} delta={}",
                delta > 0 ? "VK_ADD" : "VK_SUBTRACT",
                delta);
            Trade::AdjustGold(delta);
        }

        addWasDown = addDown;
        subtractWasDown = subtractDown;

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

            if (scanCode == kInsertScanCode) {
                spdlog::debug("Trade add key pressed: Insert / DIK 0x{:02X}", scanCode);
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
