#pragma once

namespace TradeTogether
{
    class InputEventSink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputEventSink* GetSingleton();
        void Register();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        static constexpr std::uint32_t kTScanCode = 0x14;
        // Trade.cpp still uses the historical F6 scan code as its internal
        // request/validate action identifier. Input.cpp maps T to this value.
        static constexpr std::uint32_t kTradeActionCode = 0x40;
        static constexpr std::uint32_t kEScanCode = 0x12;
        static constexpr std::uint32_t kTabScanCode = 0x0F;
        static constexpr std::uint32_t kDeleteScanCode = 0xD3;
    };
}
