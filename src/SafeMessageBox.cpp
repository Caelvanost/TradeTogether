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

        void QueueMessageBoxOnUI(
            std::string a_body,
            std::string a_primary,
            std::string a_secondary,
            IMessageBoxCallback* a_callback)
        {
            spdlog::debug("SafeMessageBox UI task started");

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

            data->bodyText = a_body.c_str();
            data->buttonText.clear();

            if (!a_primary.empty()) {
                data->buttonText.emplace_back(a_primary.c_str());
            }
            if (!a_secondary.empty()) {
                data->buttonText.emplace_back(a_secondary.c_str());
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
                "SafeMessageBox queueing on UI thread with {} button(s)",
                data->buttonText.size());
            data->QueueMessage();
            spdlog::debug("SafeMessageBox UI queue completed");
        }
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
        const auto localizedBody = TradeTogether::Localization::TranslateUserText(
            a_message ? std::string_view(a_message) : std::string_view{});
        const auto localizedPrimary = TradeTogether::Localization::TranslateUserText(
            a_buttonText ? std::string_view(a_buttonText) : std::string_view{});
        const auto localizedSecondary = TradeTogether::Localization::TranslateUserText(
            a_secondaryButtonText ? std::string_view(a_secondaryButtonText) : std::string_view{});

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            spdlog::error("SafeMessageBox: SKSE task interface unavailable; message not shown");
            delete a_callback;
            return;
        }

        spdlog::debug(
            "SafeMessageBox scheduled on UI task with {} button(s)",
            static_cast<unsigned>(!localizedPrimary.empty()) +
                static_cast<unsigned>(!localizedSecondary.empty()));

        tasks->AddUITask(
            [body = localizedBody,
             primary = localizedPrimary,
             secondary = localizedSecondary,
             callback = a_callback]() mutable {
                QueueMessageBoxOnUI(
                    std::move(body),
                    std::move(primary),
                    std::move(secondary),
                    callback);
            });
    }
}
