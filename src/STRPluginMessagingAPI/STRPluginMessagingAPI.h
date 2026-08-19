#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define STRPM_CALL __cdecl
#else
#define STRPM_CALL
#endif

namespace STRPM
{
    inline constexpr std::uint32_t kInterfaceVersion = 2;
    inline constexpr std::uint32_t kDiagnosticsVersion = 2;
    inline constexpr std::uint32_t kMaxPayloadBytes = 24 * 1024;
    inline constexpr char kQueryInterfaceExportName[] = "STR_QueryPluginMessagingInterface";
    inline constexpr char kQueryDiagnosticsExportName[] = "STR_QueryPluginMessagingDiagnostics";

    using ConnectionID = std::uint64_t;

    enum class Result : std::uint32_t
    {
        kOk = 0,
        kNotAvailable = 1,
        kUnsupportedVersion = 2,
        kInvalidArgument = 3,
        kNotConnected = 4,
        kChannelAlreadyRegistered = 5,
        kChannelNotRegistered = 6,
        kPayloadTooLarge = 7,
        kRateLimited = 8,
        kTransportError = 9,
        kTargetNotFound = 10
    };

    enum MessageFlags : std::uint32_t
    {
        kMessageNone = 0,
        kMessageReliable = 1u << 0,
        kMessageOrdered = 1u << 1,
        kMessageAllowLoopback = 1u << 2
    };

    enum class TargetKind : std::uint32_t
    {
        kServer = 1,
        kHost = 2,
        kPlayer = 3,
        kAllPlayers = 4
    };

    enum class RuntimeBackend : std::uint32_t
    {
        kNone = 0,
        kUdp = 1,
        kStrBridge = 2
    };

    enum class RuntimeBackendMode : std::uint32_t
    {
        kAuto = 0,
        kUdp = 1,
        kStrBridge = 2
    };

    struct Target
    {
        TargetKind kind{ TargetKind::kAllPlayers };
        ConnectionID connectionID{ 0 };
        const char* displayName{ nullptr };
    };

    struct Sender
    {
        ConnectionID connectionID{ 0 };
        const char* displayName{ nullptr };
        bool isHost{ false };
    };

    struct Message
    {
        const char* channel{ nullptr };
        const void* data{ nullptr };
        std::size_t size{ 0 };
        Sender sender{};
        std::uint32_t flags{ kMessageNone };
        std::uint64_t sequence{ 0 };
    };

    struct ListenerHandle
    {
        std::uint64_t value{ 0 };
    };

    using ReceiveCallback = void(STRPM_CALL*)(const Message*, void*);
    using LogCallback = void(STRPM_CALL*)(const char*, void*);

    struct Interface
    {
        std::uint32_t version{ kInterfaceVersion };
        Result(STRPM_CALL* registerChannel)(const char*, ReceiveCallback, void*, ListenerHandle*);
        Result(STRPM_CALL* unregisterChannel)(ListenerHandle);
        Result(STRPM_CALL* send)(const char*, Target, const void*, std::size_t, std::uint32_t);
        Result(STRPM_CALL* getLocalConnectionID)(ConnectionID*);
        Result(STRPM_CALL* setLogCallback)(LogCallback, void*);
        Result(STRPM_CALL* setLocalDisplayName)(const char*);
    };

    struct RuntimeStatus
    {
        std::uint32_t version{ kDiagnosticsVersion };
        std::uint32_t knownPeerCount{ 0 };
        std::uint32_t configuredPeerCount{ 0 };
        std::uint32_t autoDiscovery{ 0 };
        std::uint32_t relayMode{ 0 };
        std::uint32_t requireKnownPeer{ 0 };
        std::uint16_t localPort{ 0 };
        std::uint16_t reserved{ 0 };
        RuntimeBackend activeBackend{ RuntimeBackend::kNone };
        RuntimeBackendMode configuredBackendMode{ RuntimeBackendMode::kAuto };
        std::uint32_t strBridgeAvailable{ 0 };
        std::uint32_t strBridgeActive{ 0 };
    };

    struct DiagnosticsInterface
    {
        std::uint32_t version{ kDiagnosticsVersion };
        Result(STRPM_CALL* getRuntimeStatus)(RuntimeStatus*);
    };

    using QueryInterfaceFn = Result(STRPM_CALL*)(std::uint32_t, const Interface**);
    using QueryDiagnosticsFn = Result(STRPM_CALL*)(std::uint32_t, const DiagnosticsInterface**);

    [[nodiscard]] const Interface* LoadFromModule(const wchar_t* a_moduleName = L"STRPluginMessagingAPI.dll") noexcept;
    [[nodiscard]] const DiagnosticsInterface* LoadDiagnosticsFromModule(const wchar_t* a_moduleName = L"STRPluginMessagingAPI.dll") noexcept;
    [[nodiscard]] const char* ResultToString(Result a_result) noexcept;
}
