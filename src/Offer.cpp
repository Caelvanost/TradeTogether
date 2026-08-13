#include "PCH.h"
#include "Offer.h"
#include "Protocol.h"

namespace TradeTogether
{
    namespace
    {
        constexpr std::size_t kMaximumOfferLines = 24;
        constexpr std::size_t kMaximumItemNameBytes = 120;
        constexpr std::uint32_t kMaximumQuantity = 9999;
    }

    std::string EncodeOffer(const Offer& a_offer)
    {
        std::string encoded;
        const auto count = std::min(a_offer.size(), kMaximumOfferLines);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& line = a_offer[index];
            if (line.name.empty() || line.quantity == 0) {
                continue;
            }
            if (!encoded.empty()) {
                encoded.push_back(';');
            }
            encoded += fmt::format(
                "{}:{}",
                std::min(line.quantity, kMaximumQuantity),
                Protocol::HexEncode(
                    std::string_view(line.name).substr(
                        0,
                        kMaximumItemNameBytes)));
        }
        return encoded.empty() ? "-" : encoded;
    }

    std::optional<Offer> DecodeOffer(std::string_view a_value)
    {
        Offer result;
        if (a_value == "-") {
            return result;
        }
        if (a_value.empty()) {
            return std::nullopt;
        }

        std::size_t start = 0;
        while (start <= a_value.size()) {
            if (result.size() >= kMaximumOfferLines) {
                return std::nullopt;
            }

            auto end = a_value.find(';', start);
            if (end == std::string_view::npos) {
                end = a_value.size();
            }
            const auto token = a_value.substr(start, end - start);
            const auto separator = token.find(':');
            if (separator == std::string_view::npos) {
                return std::nullopt;
            }

            std::uint32_t quantity = 0;
            try {
                const auto parsed = std::stoul(
                    std::string(token.substr(0, separator)));
                if (parsed == 0 || parsed > kMaximumQuantity) {
                    return std::nullopt;
                }
                quantity = static_cast<std::uint32_t>(parsed);
            } catch (...) {
                return std::nullopt;
            }

            auto name = Protocol::HexDecode(token.substr(separator + 1));
            if (!name || name->empty() || name->size() > kMaximumItemNameBytes) {
                return std::nullopt;
            }
            result.push_back(OfferLine{ 0, std::move(*name), quantity, 0 });

            if (end == a_value.size()) {
                break;
            }
            start = end + 1;
        }
        return result;
    }

    std::string FormatOffer(
        std::string_view a_title,
        const Offer& a_offer,
        std::size_t a_maxLines)
    {
        std::string result(a_title);
        result += "\n";
        if (a_offer.empty()) {
            result += "  (aucun objet)";
            return result;
        }

        const auto visible = std::min(a_offer.size(), a_maxLines);
        for (std::size_t index = 0; index < visible; ++index) {
            result += fmt::format(
                "  {} x {}\n",
                a_offer[index].quantity,
                a_offer[index].name);
        }
        if (visible < a_offer.size()) {
            result += fmt::format(
                "  ... et {} autre(s)",
                a_offer.size() - visible);
        } else if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        return result;
    }
}
