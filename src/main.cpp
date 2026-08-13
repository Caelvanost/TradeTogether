#include "PCH.h"
#include "Input.h"

namespace
{
    void SetupLog()
    {
        auto logDirectory = SKSE::log::log_directory();
        if (!logDirectory) {
            return;
        }

        auto logPath = *logDirectory / "TradeTogether.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("TradeTogether", std::move(sink));

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [TradeTogether] [%l] [%s:%#] %v");
        spdlog::set_default_logger(std::move(logger));
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
            TradeTogether::InputEventSink::GetSingleton()->Register();
            spdlog::info("TradeTogether v0.3.1 ready - Papyrus OpenInventory mode");
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();

    spdlog::info("TradeTogether v0.3.1 loading");

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::critical("SKSE messaging interface is unavailable");
        return false;
    }

    if (!messaging->RegisterListener(OnSKSEMessage)) {
        spdlog::critical("Could not register SKSE messaging listener");
        return false;
    }

    return true;
}
