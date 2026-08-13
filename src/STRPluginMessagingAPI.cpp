#include "PCH.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#include <Windows.h>

namespace STRPM
{
    const Interface* LoadFromModule(const wchar_t* a_moduleName) noexcept
    {
        if (!a_moduleName) {
            return nullptr;
        }

        auto* module = GetModuleHandleW(a_moduleName);
        if (!module) {
            module = LoadLibraryW(a_moduleName);
        }
        if (!module) {
            std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";
            pluginPath += a_moduleName;
            module = LoadLibraryW(pluginPath.c_str());
        }
        if (!module) {
            return nullptr;
        }

        const auto rawExport =
            GetProcAddress(module, kQueryInterfaceExportName);
        if (!rawExport) {
            return nullptr;
        }

        const auto queryInterface =
            reinterpret_cast<QueryInterfaceFn>(rawExport);

        const Interface* api = nullptr;
        if (queryInterface(kInterfaceVersion, &api) != Result::kOk) {
            return nullptr;
        }

        if (!api || api->version != kInterfaceVersion) {
            return nullptr;
        }

        return api;
    }

    const char* ResultToString(Result a_result) noexcept
    {
        switch (a_result) {
        case Result::kOk:
            return "ok";
        case Result::kNotAvailable:
            return "not available";
        case Result::kUnsupportedVersion:
            return "unsupported version";
        case Result::kInvalidArgument:
            return "invalid argument";
        case Result::kNotConnected:
            return "not connected";
        case Result::kChannelAlreadyRegistered:
            return "channel already registered";
        case Result::kChannelNotRegistered:
            return "channel not registered";
        case Result::kPayloadTooLarge:
            return "payload too large";
        case Result::kRateLimited:
            return "rate limited";
        case Result::kTransportError:
            return "transport error";
        case Result::kTargetNotFound:
            return "target not found";
        default:
            return "unknown result";
        }
    }
}
