#include "PCH.h"
#include "SafeMessageBox.h"
#include "Localization.h"

#include <RE/B/BSTCreateFactoryManager.h>
#include <RE/M/MessageBoxData.h>

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
