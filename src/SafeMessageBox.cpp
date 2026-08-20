#include "PCH.h"
#include "SafeMessageBox.h"
#include "Localization.h"

#include <RE/B/BSTCreateFactoryManager.h>
#include <RE/M/MessageBoxData.h>

namespace RE
{
    namespace
    {
        class GameplayDispatchMessageBoxCallback final : public IMessageBoxCallback
        {
        public:
            explicit GameplayDispatchMessageBoxCallback(IMessageBoxCallback* a_inner) :
                _inner(a_inner)
            {}

            void Run(Message a_message) override
            {
                auto* inner = _inner.release();
                if (!inner) {
                    return;
                }

                auto runGameplayCallback = [inner, a_message]() {
                    inner->Run(a_message);
                    delete inner;
                };

                if (auto* tasks = SKSE::GetTaskInterface()) {
                    spdlog::debug("SafeMessageBox dispatching callback to gameplay task");
                    tasks->AddTask(std::move(runGameplayCallback));
                } else {
                    spdlog::warn("SafeMessageBox: SKSE task interface unavailable for callback; running synchronously");
                    runGameplayCallback();
                }
            }

        private:
            std::unique_ptr<IMessageBoxCallback> _inner;
        };
    }

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
        data->callback.reset(new GameplayDispatchMessageBoxCallback(a_callback));

        spdlog::debug(
            "SafeMessageBox queueing from gameplay task with {} button(s)",
            data->buttonText.size());
        data->QueueMessage();
        spdlog::debug("SafeMessageBox queue completed");
    }
}
