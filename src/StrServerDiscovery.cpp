#include "PCH.h"
#include "StrServerDiscovery.h"

#include <fstream>
#include <iterator>
#include <ranges>
#include <regex>

namespace TradeTogether::StrServerDiscovery
{
    namespace
    {
        const std::filesystem::path kLocalStoragePath =
            L".\\Data\\SkyrimTogetherReborn\\cache\\Default\\Local Storage\\leveldb";

        const std::filesystem::path kServerConfigPath =
            L".\\Data\\SkyrimTogetherReborn\\config\\STServer.ini";

        struct StoredValue
        {
            std::filesystem::file_time_type modified{};
            std::uintmax_t order{ 0 };
            std::optional<std::string> value;
        };

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

        bool IsReasonableLocalStorageValue(std::string_view a_value)
        {
            if (a_value.empty() || a_value.size() > 2048) {
                return false;
            }

            return std::ranges::all_of(a_value, [](unsigned char a_character) {
                return a_character >= 0x20 && a_character < 0x7F;
            });
        }

        std::optional<std::pair<std::uint64_t, std::size_t>> ReadVarint(
            std::string_view a_data,
            std::size_t a_offset)
        {
            std::uint64_t result = 0;
            std::uint32_t shift = 0;

            for (std::size_t index = 0;
                 index < 5 && a_offset + index < a_data.size();
                 ++index) {
                const auto byte =
                    static_cast<unsigned char>(a_data[a_offset + index]);
                result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    return std::make_pair(result, index + 1);
                }
                shift += 7;
            }

            return std::nullopt;
        }

        std::optional<std::string> TryReadChromiumLocalStorageValue(
            std::string_view a_data,
            std::size_t a_offset)
        {
            // STR stores its CEF/localStorage values in Chromium LevelDB.
            // The plugin only needs a couple of simple keys, so we parse the
            // small value envelope directly instead of shipping LevelDB.
            for (std::size_t skip = 0;
                 skip < 8 && a_offset + skip < a_data.size();
                 ++skip) {
                const auto length = ReadVarint(a_data, a_offset + skip);
                if (!length) {
                    continue;
                }

                const auto typeOffset = a_offset + skip + length->second;
                if (typeOffset >= a_data.size() ||
                    a_data[typeOffset] != '\x01') {
                    continue;
                }

                const auto valueOffset = typeOffset + 1;
                if (length->first == 0 ||
                    length->first > 2048 ||
                    valueOffset + length->first > a_data.size()) {
                    continue;
                }

                std::string value(a_data.substr(
                    valueOffset,
                    static_cast<std::size_t>(length->first)));
                if (IsReasonableLocalStorageValue(value)) {
                    return value;
                }
            }

            return std::nullopt;
        }

        std::vector<StoredValue> ReadLocalStorageValues(std::string_view a_key)
        {
            std::vector<StoredValue> result;

            std::error_code error;
            if (!std::filesystem::exists(kLocalStoragePath, error)) {
                return result;
            }

            std::uintmax_t order = 0;
            for (const auto& entry :
                 std::filesystem::directory_iterator(kLocalStoragePath, error)) {
                if (error || !entry.is_regular_file(error)) {
                    continue;
                }

                const auto extension = entry.path().extension().wstring();
                if (_wcsicmp(extension.c_str(), L".log") != 0 &&
                    _wcsicmp(extension.c_str(), L".ldb") != 0) {
                    continue;
                }

                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) {
                    continue;
                }

                std::string data{
                    std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>() };

                std::size_t position = 0;
                while ((position = data.find(a_key, position)) !=
                       std::string::npos) {
                    const auto modified = entry.last_write_time(error);
                    result.push_back(StoredValue{
                        error ? std::filesystem::file_time_type{} : modified,
                        order++,
                        TryReadChromiumLocalStorageValue(
                            data,
                            position + a_key.size()) });
                    position += a_key.size();
                }
            }

            std::ranges::sort(result, [](const StoredValue& a_left,
                                         const StoredValue& a_right) {
                if (a_left.modified != a_right.modified) {
                    return a_left.modified < a_right.modified;
                }
                return a_left.order < a_right.order;
            });
            return result;
        }

        std::optional<std::string> ReadLatestLocalStorageValue(
            std::string_view a_key)
        {
            const auto values = ReadLocalStorageValues(a_key);
            if (values.empty()) {
                return std::nullopt;
            }

            return values.back().value;
        }

        bool IsSafeHost(std::string_view a_host)
        {
            if (a_host.empty() || a_host.size() > 253) {
                return false;
            }

            return std::ranges::all_of(a_host, [](unsigned char a_character) {
                return std::isalnum(a_character) != 0 ||
                       a_character == '.' ||
                       a_character == '-' ||
                       a_character == '_';
            });
        }

        std::optional<Config::RemotePeer> ParseStrAddress(
            std::string a_value,
            std::uint16_t a_tradeTogetherPort)
        {
            a_value = Trim(std::move(a_value));
            if (a_value.empty() ||
                a_value.find('|') != std::string::npos ||
                a_value.find('/') != std::string::npos ||
                a_value.find('\\') != std::string::npos) {
                return std::nullopt;
            }

            std::string host = a_value;
            const auto separator = a_value.rfind(':');
            if (separator != std::string::npos) {
                const auto portText = a_value.substr(separator + 1);
                if (!portText.empty() &&
                    std::ranges::all_of(portText, [](unsigned char a_character) {
                        return std::isdigit(a_character) != 0;
                    })) {
                    try {
                        const auto parsed = std::stoul(portText);
                        if (parsed == 0 || parsed > 65535) {
                            return std::nullopt;
                        }
                        host = Trim(a_value.substr(0, separator));
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }

            if (!IsSafeHost(host)) {
                return std::nullopt;
            }

            Config::RemotePeer peer{};
            peer.host = std::move(host);
            peer.port = a_tradeTogetherPort;
            return peer;
        }

        std::optional<std::string> ReadPasswordFromSavedServerList(
            const Config::RemotePeer& a_peer)
        {
            const auto savedServerList =
                ReadLatestLocalStorageValue("savedServerList");
            if (!savedServerList || savedServerList->empty()) {
                return std::nullopt;
            }

            const std::regex itemPattern(
                R"json(\{\s*"ip"\s*:\s*"([^"]+)"\s*,\s*"port"\s*:\s*([0-9]+)\s*,\s*"password"\s*:\s*"([^"]*)")json",
                std::regex_constants::icase);

            for (std::sregex_iterator iterator(
                     savedServerList->begin(),
                     savedServerList->end(),
                     itemPattern),
                 end;
                 iterator != end;
                 ++iterator) {
                const auto host = (*iterator)[1].str();
                const auto password = (*iterator)[3].str();

                if (_stricmp(host.c_str(), a_peer.host.c_str()) == 0 &&
                    !password.empty()) {
                    return password;
                }
            }

            return std::nullopt;
        }

        std::optional<std::string> ReadIniValue(
            const std::filesystem::path& a_path,
            std::string_view a_section,
            std::string_view a_key)
        {
            std::ifstream file(a_path);
            if (!file) {
                return std::nullopt;
            }

            bool inSection = false;
            std::string line;
            while (std::getline(file, line)) {
                line = Trim(std::move(line));
                if (line.empty() || line.starts_with(';') ||
                    line.starts_with('#')) {
                    continue;
                }

                if (line.starts_with('[') && line.ends_with(']')) {
                    const auto current = line.substr(1, line.size() - 2);
                    inSection =
                        _stricmp(
                            current.c_str(),
                            std::string(a_section).c_str()) == 0;
                    continue;
                }

                if (!inSection) {
                    continue;
                }

                const auto separator = line.find('=');
                if (separator == std::string::npos) {
                    continue;
                }

                auto currentKey = Trim(line.substr(0, separator));
                if (_stricmp(
                        currentKey.c_str(),
                        std::string(a_key).c_str()) != 0) {
                    continue;
                }

                auto value = Trim(line.substr(separator + 1));
                const auto comment = value.find_first_of(";#");
                if (comment != std::string::npos) {
                    value = Trim(value.substr(0, comment));
                }
                return value;
            }

            return std::nullopt;
        }
    }

    ClientState ReadClientState(std::uint16_t a_tradeTogetherPort)
    {
        ClientState state{};

        if (const auto address =
                ReadLatestLocalStorageValue("last_connected_address")) {
            state.rawAddress = *address;
            state.remotePeer = ParseStrAddress(*address, a_tradeTogetherPort);
        }

        if (const auto password =
                ReadLatestLocalStorageValue("last_connected_password");
            password && !password->empty()) {
            state.password = *password;
        } else if (state.remotePeer) {
            state.password = ReadPasswordFromSavedServerList(*state.remotePeer);
        }

        return state;
    }

    std::optional<std::string> ReadServerPasswordFromConfig()
    {
        const auto password =
            ReadIniValue(kServerConfigPath, "GameServer", "sPassword");
        if (!password || password->empty()) {
            return std::nullopt;
        }

        return password;
    }
}
