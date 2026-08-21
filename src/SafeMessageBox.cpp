#include "PCH.h"
#include "SafeMessageBox.h"
#include "Localization.h"

#include <RE/B/BSTCreateFactoryManager.h>
#include <RE/M/MessageBoxData.h>

#include <cstring>

namespace TradeTogether::SafeMessageBox
{
    namespace
    {
        constexpr std::uint32_t kAcceptScanCode = 0x14;  // T
        constexpr std::uint32_t kDeclineScanCode = 0x0F;  // Tab

        std::mutex g_promptMutex;
        std::unique_ptr<RE::IMessageBoxCallback> g_nonModalCallback;

        bool IsIncomingTradePrompt(
            const char* a_primaryButton,
            const char* a_secondaryButton)
        {
            return a_primaryButton && a_secondaryButton &&
                   std::strcmp(a_primaryButton, "Accepter") == 0 &&
                   std::strcmp(a_secondaryButton, "Refuser") == 0;
        }

        void QueueIncomingTradePrompt(
            const char* a_message,
            RE::IMessageBoxCallback* a_callback)
        {
            {
                std::scoped_lock lock(g_promptMutex);
                if (g_nonModalCallback) {
                    spdlog::warn(
                        "SafeMessageBox: replacing an unanswered incoming trade prompt");
                }
                g_nonModalCallback.reset(a_callback);
            }

            auto body = Localization::TranslateUserText(
                a_message ? std::string_view(a_message) : std::string_view{});
            std::replace(body.begin(), body.end(), '\n', ' ');
            const auto notification = fmt::format(
                "{}  [T] Accept | [Tab] Decline",
                body);
            RE::LocalizedDebugNotification(notification.c_str());

            spdlog::info(
                "SafeMessageBox incoming trade prompt displayed non-modally: T=accept Tab=decline");
        }
    }

    bool HandleKey(std::uint32_t a_scanCode)
    {
        if (a_scanCode != kAcceptScanCode && a_scanCode != kDeclineScanCode) {
            return false;
        }

        std::unique_ptr<RE::IMessageBoxCallback> callback;
        {
            std::scoped_lock lock(g_promptMutex);
            if (!g_nonModalCallback) {
                return false;
            }
            callback = std::move(g_nonModalCallback);
        }

        const auto message = a_scanCode == kAcceptScanCode ?
            RE::IMessageBoxCallback::Message::kUnk0 :
            RE::IMessageBoxCallback::Message::kUnk1;

        spdlog::info(
            "SafeMessageBox non-modal incoming trade response: {}",
            a_scanCode == kAcceptScanCode ? "accept" : "decline");
        callback->Run(message);
        return true;
    }

    void ClearNonModalPrompt()
    {
        std::scoped_lock lock(g_promptMutex);
        g_nonModalCallback.reset();
    }

    bool TryQueueIncomingTradePrompt(
        const char* a_message,
        RE::IMessageBoxCallback* a_callback,
        const char* a_primaryButton,
        const char* a_secondaryButton)
    {
        if (!IsIncomingTradePrompt(a_primaryButton, a_secondaryButton)) {
            return false;
        }

        QueueIncomingTradePrompt(a_message, a_callback);
        return true;
    }
}

namespace RE
{
    void SafeCreateMessage(
        const char* a_message,
        IMessageBoxCallback* a_callback,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        const char* a_buttonText,
        const char* a_secondaryButtonText)
    {
        // The receiver freeze is isolated to the initial network request prompt.
        // Keep that one prompt out of Skyrim's modal MessageBoxData pipeline.
        // Offer summaries and final confirmations retain the existing modal UI.
        if (TradeTogether::SafeMessageBox::TryQueueIncomingTradePrompt(
                a_message,
                a_callback,
                a_buttonText,
                a_secondaryButtonText)) {
            return;
        }

        auto* manager = MessageDataFactoryManager::GetSingleton();
        if (!manager) {
            spdlog::error("SafeMessageBox: MessageDataFactoryManager unavailable");
            delete a_callback;
            return;
        }

        auto* creator = manager->GetCreator<MessageBoxData>(BSFixedString("MessageBoxData"));
        if (!creator) {
            spdlog::error("SafeMessageBox: MessageBoxData creator unavailable");
            delete a_callback;
            return;
        }

        auto* data = creator->Create();
        if (!data) {
            spdlog::error("SafeMessageBox: failed to allocate MessageBoxData");
            delete a_callback;
            return;
        }

        const auto localizedBody = TradeTogether::Localization::TranslateUserText(
            a_message ? std::string_view(a_message) : std::string_view{});
        const auto localizedPrimary = TradeTogether::Localization::TranslateUserText(
            a_buttonText ? std::string_view(a_buttonText) : std::string_view{});
        const auto localizedSecondary = TradeTogether::Localization::TranslateUserText(
            a_secondaryButtonText ? std::string_view(a_secondaryButtonText) : std::string_view{});

        data->bodyText = localizedBody.c_str();
        data->buttonText.clear();

        if (!localizedPrimary.empty()) {
            data->buttonText.emplace_back(localizedPrimary.c_str());
        }
        if (!localizedSecondary.empty()) {
            data->buttonText.emplace_back(localizedSecondary.c_str());
        }

        data->unk38 = 0;
        data->unk3C = -1;
        data->unk48 = 0;
        data->unk4C = 0;
        data->unk4D = 0;
        data->unk4E = 0;
        data->unk4F = 0;
        data->callback.reset(a_callback);

        spdlog::debug(
            "SafeMessageBox queued with {} button(s)",
            data->buttonText.size());
        data->QueueMessage();
    }
}
