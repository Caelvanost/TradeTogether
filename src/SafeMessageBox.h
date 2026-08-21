#pragma once

namespace TradeTogether::SafeMessageBox
{
    // Incoming trade requests use a non-modal notification prompt on the STRPM
    // branch. T accepts and Tab declines. Other TradeTogether message boxes keep
    // using Skyrim's normal MessageBoxData path.
    bool HandleKey(std::uint32_t a_scanCode);
    void ClearNonModalPrompt();
}

namespace RE
{
    void SafeCreateMessage(
        const char* a_message,
        IMessageBoxCallback* a_callback,
        std::uint32_t a_arg3,
        std::uint32_t a_arg4,
        std::uint32_t a_arg5,
        const char* a_buttonText,
        const char* a_secondaryButtonText);
}
