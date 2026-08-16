#include "PCH.h"
#include "SafeMessageBox.h"

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

        data->bodyText = a_message ? a_message : "";
        data->buttonText.clear();

        if (a_buttonText && *a_buttonText) {
            data->buttonText.emplace_back(a_buttonText);
        }
        if (a_secondaryButtonText && *a_secondaryButtonText) {
            data->buttonText.emplace_back(a_secondaryButtonText);
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
