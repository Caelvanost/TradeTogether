#include "PCH.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#include <Windows.h>

namespace STRPM
{
    namespace
    {
        HMODULE LoadPluginModule(const wchar_t* a_moduleName) noexcept
        {
            if (!a_moduleName) {
                return nullptr;
            }

            auto module = GetModuleHandleW(a_moduleName);
            if (!module) {
                module = LoadLibraryW(a_moduleName);
            }
            if (!module) {
                std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";
                pluginPath += a_moduleName;
                module = LoadLibraryW(pluginPath.c_str());
            }

            return module;
        }
    }

    const Interface* LoadFromModule(const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
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

    const DiagnosticsInterface* LoadDiagnosticsFromModule(
        const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
        if (!module) {
            return nullptr;
        }

        const auto rawExport =
            GetProcAddress(module, kQueryDiagnosticsExportName);
        if (!rawExport) {
            return nullptr;
        }

        const auto queryDiagnostics =
            reinterpret_cast<QueryDiagnosticsFn>(rawExport);

        const DiagnosticsInterface* diagnostics = nullptr;
        if (queryDiagnostics(kDiagnosticsVersion, &diagnostics) !=
            Result::kOk) {
            return nullptr;
        }

        if (!diagnostics || diagnostics->version != kDiagnosticsVersion) {
            return nullptr;
        }

        return diagnostics;
    }

    const TransportInterface* LoadTransportFromModule(
        const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
        if (!module) {
            return nullptr;
        }

        const auto rawExport =
            GetProcAddress(module, kQueryTransportExportName);
        if (!rawExport) {
            return nullptr;
        }

        const auto queryTransport =
            reinterpret_cast<QueryTransportInterfaceFn>(rawExport);

        const TransportInterface* transport = nullptr;
        if (queryTransport(kTransportInterfaceVersion, &transport) !=
            Result::kOk) {
            return nullptr;
        }

        if (!transport ||
            transport->version != kTransportInterfaceVersion) {
            return nullptr;
        }

        return transport;
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
