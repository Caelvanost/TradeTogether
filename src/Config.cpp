#include "PCH.h"
#include "Config.h"

namespace TradeTogether
{
    namespace
    {
        constexpr wchar_t kIniPath[] =
            L".\\Data\\SKSE\\Plugins\\TradeTogether.ini";

        std::uint32_t ReadUInt(
            const wchar_t* a_section,
            const wchar_t* a_key,
            std::uint32_t a_fallback)
        {
            return static_cast<std::uint32_t>(
                GetPrivateProfileIntW(
                    a_section,
                    a_key,
                    static_cast<int>(a_fallback),
                    kIniPath));
        }

        bool ReadBool(
            const wchar_t* a_section,
            const wchar_t* a_key,
            bool a_fallback)
        {
            return ReadUInt(
                       a_section,
                       a_key,
                       a_fallback ? 1U : 0U) != 0;
        }

        std::string ReadString(
            const wchar_t* a_section,
            const wchar_t* a_key,
            const wchar_t* a_fallback)
        {
            std::array<wchar_t, 2048> buffer{};
            GetPrivateProfileStringW(
                a_section,
                a_key,
                a_fallback,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                kIniPath);

            const auto required = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 1) {
                return {};
            }

            std::string value(static_cast<std::size_t>(required), '\0');
            const auto written = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                value.data(),
                required,
                nullptr,
                nullptr);
            if (written <= 1) {
                return {};
            }

            value.resize(static_cast<std::size_t>(written - 1));
            return value;
        }

        std::string Trim(std::string a_value)
        {
            const auto isSpace = [](unsigned char a_character) {
                return std::isspace(a_character) != 0;
            };
            a_value.erase(
                a_value.begin(),
                std::find_if_not(a_value.begin(), a_value.end(), isSpace));
            a_value.erase(
                std::find_if_not(
                    a_value.rbegin(),
                    a_value.rend(),
                    isSpace).base(),
                a_value.end());
            return a_value;
        }

        std::uint16_t ReadPort(
            const wchar_t* a_key,
            std::uint16_t a_fallback)
        {
            const auto value = ReadUInt(
                L"Network",
                a_key,
                a_fallback);
            return value > 0 && value <= 65535 ?
                static_cast<std::uint16_t>(value) : a_fallback;
        }

        std::optional<Config::RemotePeer> ParseRemotePeer(
            std::string a_value,
            std::uint16_t a_defaultPort)
        {
            a_value = Trim(std::move(a_value));
            if (a_value.empty()) {
                return std::nullopt;
            }

            Config::RemotePeer result{};
            result.port = a_defaultPort;

            const auto separator = a_value.rfind(':');
            if (separator != std::string::npos) {
                const auto portText = Trim(a_value.substr(separator + 1));
                try {
                    const auto parsed = std::stoul(portText);
                    if (parsed == 0 || parsed > 65535) {
                        return std::nullopt;
                    }
                    result.port = static_cast<std::uint16_t>(parsed);
                    a_value.resize(separator);
                } catch (...) {
                    return std::nullopt;
                }
            }

            result.host = Trim(std::move(a_value));
            if (result.host.empty() ||
                result.host.find('|') != std::string::npos) {
                return std::nullopt;
            }
            return result;
        }

        std::vector<Config::RemotePeer> ParseRemotePeers(
            std::string a_value,
            std::uint16_t a_defaultPort)
        {
            std::vector<Config::RemotePeer> result;
            std::unordered_set<std::string> seen;

            std::size_t start = 0;
            while (start <= a_value.size()) {
                const auto end = a_value.find_first_of(",;", start);
                const auto item = a_value.substr(
                    start,
                    end == std::string::npos ?
                        std::string::npos : end - start);

                if (auto peer = ParseRemotePeer(item, a_defaultPort)) {
                    auto key = peer->host;
                    std::transform(
                        key.begin(),
                        key.end(),
                        key.begin(),
                        [](unsigned char a_character) {
                            return static_cast<char>(
                                std::tolower(a_character));
                        });
                    key = fmt::format("{}:{}", key, peer->port);
                    if (seen.insert(key).second) {
                        result.push_back(std::move(*peer));
                    }
                } else if (!Trim(item).empty()) {
                    spdlog::warn(
                        "TradeTogether ignored invalid RemotePeers entry: \"{}\"",
                        Trim(item));
                }

                if (result.size() >= 64) {
                    spdlog::warn("TradeTogether RemotePeers limited to 64 entries");
                    break;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return result;
        }
    }

    Config Config::Load()
    {
        Config config{};

        config.networkEnabled =
            !ReadBool(L"Network", L"Disabled", false);
        config.autoDiscovery =
            ReadBool(L"Network", L"AutoDiscovery", config.autoDiscovery);
        config.relayMode =
            ReadBool(L"Network", L"RelayMode", config.relayMode);
        config.localPort =
            ReadPort(L"LocalPort", config.localPort);
        config.peerPort =
            ReadPort(L"PeerPort", config.localPort);
        config.peerHost =
            ReadString(L"Network", L"PeerHost", L"");
        config.remotePeers = ParseRemotePeers(
            ReadString(L"Network", L"RemotePeers", L""),
            config.peerPort);
        if (auto legacyPeer = ParseRemotePeer(config.peerHost, config.peerPort)) {
            const auto duplicate = std::any_of(
                config.remotePeers.begin(),
                config.remotePeers.end(),
                [&](const Config::RemotePeer& a_peer) {
                    return _stricmp(
                               a_peer.host.c_str(),
                               legacyPeer->host.c_str()) == 0 &&
                           a_peer.port == legacyPeer->port;
                });
            if (!duplicate) {
                config.remotePeers.push_back(std::move(*legacyPeer));
            }
        }
        config.sharedSecret =
            ReadString(L"Network", L"SharedSecret", L"");
        config.discoveryIntervalMs = std::clamp(
            ReadUInt(
                L"Network",
                L"DiscoveryIntervalMs",
                config.discoveryIntervalMs),
            250U,
            5000U);
        config.peerTimeoutMs = std::clamp(
            ReadUInt(
                L"Network",
                L"PeerTimeoutMs",
                config.peerTimeoutMs),
            3000U,
            60000U);
        config.requestTimeoutMs = std::clamp(
            ReadUInt(
                L"Trade",
                L"RequestTimeoutMs",
                config.requestTimeoutMs),
            5000U,
            120000U);
        config.sessionTimeoutMs = std::clamp(
            ReadUInt(
                L"Trade",
                L"SessionTimeoutMs",
                config.sessionTimeoutMs),
            60000U,
            1800000U);

        return config;
    }
}
