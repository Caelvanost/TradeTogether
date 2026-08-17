#include "PCH.h"
#include "StrServerDiscovery.h"

#include <fstream>
#include <iterator>
#include <ranges>

namespace TradeTogether::StrServerDiscovery
{
    namespace
    {
        const std::filesystem::path kLocalStoragePath =
            L".\\Data\\SkyrimTogetherReborn\\cache\\Default\\Local Storage\\leveldb";

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
                std::find_if_not(a_value.rbegin(), a_value.rend(), isSpace).base(),
                a_value.end());
            return a_value;
        }

        bool IsReasonableValue(std::string_view a_value)
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

        std::optional<std::string> TryReadChromiumValue(
            std::string_view a_data,
            std::size_t a_offset)
        {
            for (std::size_t skip = 0;
                 skip < 8 && a_offset + skip < a_data.size();
                 ++skip) {
                const auto length = ReadVarint(a_data, a_offset + skip);
                if (!length) {
                    continue;
                }

                const auto typeOffset = a_offset + skip + length->second;
                if (typeOffset >= a_data.size() || a_data[typeOffset] != '\x01') {
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
                if (IsReasonableValue(value)) {
                    return value;
                }
            }
            return std::nullopt;
        }

        std::optional<std::string> ReadLatestLocalStorageValue(
            std::string_view a_key)
        {
            std::error_code error;
            if (!std::filesystem::exists(kLocalStoragePath, error)) {
                return std::nullopt;
            }

            std::vector<StoredValue> values;
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
                while ((position = data.find(a_key, position)) != std::string::npos) {
                    const auto modified = entry.last_write_time(error);
                    values.push_back(StoredValue{
                        error ? std::filesystem::file_time_type{} : modified,
                        order++,
                        TryReadChromiumValue(data, position + a_key.size()) });
                    position += a_key.size();
                }
            }

            if (values.empty()) {
                return std::nullopt;
            }

            std::ranges::sort(values, [](const StoredValue& a_left,
                                         const StoredValue& a_right) {
                if (a_left.modified != a_right.modified) {
                    return a_left.modified < a_right.modified;
                }
                return a_left.order < a_right.order;
            });
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
    }

    std::optional<std::string> ReadLastConnectedHost()
    {
        auto address = ReadLatestLocalStorageValue("last_connected_address");
        if (!address) {
            return std::nullopt;
        }

        auto value = Trim(std::move(*address));
        if (value.empty() ||
            value.find('|') != std::string::npos ||
            value.find('/') != std::string::npos ||
            value.find('\\') != std::string::npos) {
            return std::nullopt;
        }

        auto host = value;
        const auto separator = value.rfind(':');
        if (separator != std::string::npos) {
            const auto portText = value.substr(separator + 1);
            if (!portText.empty() &&
                std::ranges::all_of(portText, [](unsigned char a_character) {
                    return std::isdigit(a_character) != 0;
                })) {
                host = Trim(value.substr(0, separator));
            }
        }

        if (!IsSafeHost(host)) {
            return std::nullopt;
        }
        return host;
    }
}
