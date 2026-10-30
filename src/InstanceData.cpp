#include "PCH.h"
#include "InstanceData.h"
#include "Protocol.h"

#include <bit>
#include <charconv>
#include <cmath>

namespace TradeTogether::InstanceData
{
    namespace
    {
        constexpr std::size_t kMaximumEffects = 8;
        constexpr std::size_t kMaximumCustomNameBytes = 120;

        std::string FloatHex(float a_value)
        {
            return fmt::format("{:08X}", std::bit_cast<std::uint32_t>(a_value));
        }

        std::optional<float> ParseFloatHex(std::string_view a_value)
        {
            if (a_value.size() != 8) {
                return std::nullopt;
            }
            std::uint32_t bits = 0;
            const auto result = std::from_chars(
                a_value.data(),
                a_value.data() + a_value.size(),
                bits,
                16);
            if (result.ec != std::errc{} ||
                result.ptr != a_value.data() + a_value.size()) {
                return std::nullopt;
            }
            return std::bit_cast<float>(bits);
        }

        template <class T>
        std::optional<T> ParseUnsigned(std::string_view a_value, int a_base = 10)
        {
            unsigned long long parsed = 0;
            const auto result = std::from_chars(
                a_value.data(),
                a_value.data() + a_value.size(),
                parsed,
                a_base);
            if (result.ec != std::errc{} ||
                result.ptr != a_value.data() + a_value.size() ||
                parsed > static_cast<unsigned long long>((std::numeric_limits<T>::max)())) {
                return std::nullopt;
            }
            return static_cast<T>(parsed);
        }

        std::vector<std::string_view> Split(std::string_view a_value, char a_separator)
        {
            std::vector<std::string_view> result;
            std::size_t start = 0;
            while (start <= a_value.size()) {
                auto end = a_value.find(a_separator, start);
                if (end == std::string_view::npos) {
                    end = a_value.size();
                }
                result.push_back(a_value.substr(start, end - start));
                if (end == a_value.size()) {
                    break;
                }
                start = end + 1;
            }
            return result;
        }

        void SetHealthCompat(
            RE::ExtraDataList* a_extraList,
            float a_health)
        {
            using func_t = void (*)(RE::ExtraDataList*, float);
            static REL::Relocation<func_t> func{ REL::ID(11616) };
            func(a_extraList, a_health);
        }

        void SetEnchantmentCompat(
            RE::ExtraDataList* a_extraList,
            RE::EnchantmentItem* a_enchantment,
            std::uint16_t a_chargeAmount,
            bool a_removeOnUnequip)
        {
            using func_t = void (*)(
                RE::ExtraDataList*,
                RE::EnchantmentItem*,
                std::uint16_t,
                bool);
            static REL::Relocation<func_t> func{
                RELOCATION_ID(11921, 12060)
            };
            func(
                a_extraList,
                a_enchantment,
                a_chargeAmount,
                a_removeOnUnequip);
        }

        void SetOverrideNameCompat(
            RE::ExtraDataList* a_extraList,
            const char* a_name)
        {
            if (!a_extraList || !a_name || !*a_name) {
                return;
            }

            auto* textData =
                a_extraList->GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData();
                a_extraList->Add(textData);
            }

            if (!textData->displayNameText && !textData->ownerQuest) {
                textData->SetName(a_name);
            }
        }

        bool IsIgnorableLocalExtra(RE::ExtraDataType a_type)
        {
            switch (a_type) {
            case RE::ExtraDataType::kCount:
            case RE::ExtraDataType::kWorn:
            case RE::ExtraDataType::kWornLeft:
            case RE::ExtraDataType::kHotkey:
            case RE::ExtraDataType::kUniqueID:
                return true;
            default:
                return false;
            }
        }

        bool IsSupportedIntrinsicExtra(RE::ExtraDataType a_type)
        {
            switch (a_type) {
            case RE::ExtraDataType::kHealth:
            case RE::ExtraDataType::kCharge:
            case RE::ExtraDataType::kOwnership:
            case RE::ExtraDataType::kRank:
            case RE::ExtraDataType::kCannotWear:
            case RE::ExtraDataType::kPoison:
            case RE::ExtraDataType::kTextDisplayData:
            case RE::ExtraDataType::kEnchantment:
            case RE::ExtraDataType::kSoul:
                return true;
            default:
                return false;
            }
        }

        std::string ReadCustomName(const RE::ExtraTextDisplayData& a_text)
        {
            if (!a_text.IsPlayerSet() || a_text.ownerQuest || a_text.displayNameText) {
                return {};
            }

            std::string_view name{ a_text.displayName.c_str() };
            if (a_text.customNameLength > 0) {
                name = name.substr(
                    0,
                    (std::min)(name.size(), static_cast<std::size_t>(a_text.customNameLength)));
            }
            name = name.substr(0, (std::min)(name.size(), kMaximumCustomNameBytes));

            std::string result(name);
            std::erase_if(result, [](char a_character) {
                const auto byte = static_cast<unsigned char>(a_character);
                return byte < 0x20 || byte == 0x7F;
            });
            return result;
        }

        bool CaptureEnchantment(
            const RE::ExtraEnchantment& a_extra,
            Snapshot& a_out,
            std::string& a_error)
        {
            auto* enchantment = a_extra.enchantment;
            if (!enchantment) {
                return true;
            }

            a_out.hasEnchantment = true;
            a_out.enchantmentMaxCharge = a_extra.charge;
            a_out.removeOnUnequip = a_extra.removeOnUnequip;

            if (!enchantment->IsDynamicForm()) {
                a_out.enchantmentFormID = enchantment->GetFormID();
                if (a_out.enchantmentFormID == 0) {
                    a_error = "enchantement statique sans FormID";
                    return false;
                }
                return true;
            }

            a_out.dynamicEnchantment = true;
            if (enchantment->effects.empty() ||
                enchantment->effects.size() > kMaximumEffects) {
                a_error = "enchantement dynamique trop complexe";
                return false;
            }

            for (const auto* effect : enchantment->effects) {
                if (!effect || !effect->baseEffect ||
                    effect->baseEffect->GetFormID() == 0) {
                    a_error = "effet d'enchantement dynamique non serialisable";
                    return false;
                }
                a_out.enchantmentEffects.push_back(EffectSnapshot{
                    effect->baseEffect->GetFormID(),
                    effect->effectItem.magnitude,
                    effect->effectItem.area,
                    effect->effectItem.duration,
                    effect->cost
                });
            }
            return true;
        }

        RE::EnchantmentItem* ResolveOrCreateEnchantment(
            RE::TESBoundObject* a_object,
            const Snapshot& a_snapshot,
            std::string& a_error)
        {
            if (!a_snapshot.hasEnchantment) {
                return nullptr;
            }

            if (!a_snapshot.dynamicEnchantment) {
                auto* enchantment =
                    RE::TESForm::LookupByID<RE::EnchantmentItem>(
                        a_snapshot.enchantmentFormID);
                if (!enchantment) {
                    a_error = fmt::format(
                        "enchantement introuvable localement: {:08X}",
                        a_snapshot.enchantmentFormID);
                }
                return enchantment;
            }

            if (!a_object ||
                (a_object->GetFormType() != RE::FormType::Weapon &&
                 a_object->GetFormType() != RE::FormType::Armor)) {
                a_error = "enchantement dynamique sur un type d'objet invalide";
                return nullptr;
            }

            // CommonLibSSE-NG 3.5.2 does not expose
            // RE::BGSCreatedObjectManager in its public headers. Use the same
            // Address Library relocations used by newer CommonLib versions
            // while keeping the project on the validated 3.5.2 dependency.
            static REL::Relocation<void**> managerSingleton{
                RELOCATION_ID(514172, 400320)
            };
            auto* manager = *managerSingleton;
            if (!manager) {
                a_error = "BGSCreatedObjectManager indisponible";
                return nullptr;
            }

            RE::BSTArray<RE::Effect> effects;
            effects.reserve(
                static_cast<std::uint32_t>(a_snapshot.enchantmentEffects.size()));

            for (const auto& encoded : a_snapshot.enchantmentEffects) {
                auto* setting =
                    RE::TESForm::LookupByID<RE::EffectSetting>(
                        encoded.effectFormID);
                if (!setting) {
                    a_error = fmt::format(
                        "MagicEffect introuvable localement: {:08X}",
                        encoded.effectFormID);
                    return nullptr;
                }

                auto& effect = effects.emplace_back();
                effect.baseEffect = setting;
                effect.effectItem.magnitude = encoded.magnitude;
                effect.effectItem.area = encoded.area;
                effect.effectItem.duration = encoded.duration;
                effect.cost = encoded.cost;
            }

            using AddEnchantmentFn =
                RE::EnchantmentItem* (*)(void*, RE::BSTArray<RE::Effect>&);

            static REL::Relocation<AddEnchantmentFn> addWeaponEnchantment{
                RELOCATION_ID(35263, 36165)
            };
            static REL::Relocation<AddEnchantmentFn> addArmorEnchantment{
                RELOCATION_ID(35264, 36166)
            };

            auto* enchantment =
                a_object->GetFormType() == RE::FormType::Weapon ?
                    addWeaponEnchantment(manager, effects) :
                    addArmorEnchantment(manager, effects);
            if (!enchantment) {
                a_error = "creation de l'enchantement dynamique impossible";
            }
            return enchantment;
        }
    }

    bool IsEmpty(const Snapshot& a_snapshot)
    {
        return !a_snapshot.health &&
               !a_snapshot.charge &&
               !a_snapshot.customName &&
               !a_snapshot.ownershipFormID &&
               !a_snapshot.ownershipRank &&
               !a_snapshot.cannotWear &&
               !a_snapshot.hasEnchantment &&
               !a_snapshot.poisonFormID &&
               !a_snapshot.soulLevel;
    }

    bool Capture(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        Snapshot& a_out,
        std::string& a_error)
    {
        a_out = {};
        if (!a_object || !a_extraList) {
            return true;
        }

        for (const auto& extra : *a_extraList) {
            const auto type = extra.GetType();
            if (!IsIgnorableLocalExtra(type) && !IsSupportedIntrinsicExtra(type)) {
                a_error = fmt::format(
                    "ExtraData non supportee pour transfert exact: 0x{:02X}",
                    static_cast<std::uint32_t>(type));
                return false;
            }
        }

        if (const auto* health = a_extraList->GetByType<RE::ExtraHealth>()) {
            if (!std::isfinite(health->health)) {
                a_error = "valeur de tempering invalide";
                return false;
            }
            a_out.health = health->health;
        }

        if (const auto* charge = a_extraList->GetByType<RE::ExtraCharge>()) {
            if (!std::isfinite(charge->charge)) {
                a_error = "charge d'enchantement invalide";
                return false;
            }
            a_out.charge = charge->charge;
        }

        if (const auto* text =
                a_extraList->GetByType<RE::ExtraTextDisplayData>()) {
            const auto customName = ReadCustomName(*text);
            if (!customName.empty()) {
                a_out.customName = customName;
            }
        }

        if (const auto* owner = a_extraList->GetByType<RE::ExtraOwnership>()) {
            if (owner->owner) {
                a_out.ownershipFormID = owner->owner->GetFormID();
            }
        }

        if (const auto* rank = a_extraList->GetByType<RE::ExtraRank>()) {
            a_out.ownershipRank = rank->rank;
        }

        a_out.cannotWear =
            a_extraList->GetByType<RE::ExtraCannotWear>() != nullptr;

        if (const auto* enchantment =
                a_extraList->GetByType<RE::ExtraEnchantment>()) {
            if (!CaptureEnchantment(*enchantment, a_out, a_error)) {
                return false;
            }
        }

        if (const auto* poison = a_extraList->GetByType<RE::ExtraPoison>()) {
            if (!poison->poison || poison->poison->GetFormID() == 0 ||
                poison->count == 0) {
                a_error = "poison d'arme non serialisable";
                return false;
            }
            a_out.poisonFormID = poison->poison->GetFormID();
            a_out.poisonCount = poison->count;
        }

        if (const auto* soul = a_extraList->GetByType<RE::ExtraSoul>()) {
            a_out.soulLevel =
                static_cast<std::uint8_t>(soul->GetContainedSoul());
        }

        return true;
    }

    std::string Encode(const Snapshot& a_snapshot)
    {
        if (IsEmpty(a_snapshot)) {
            return "-";
        }

        std::vector<std::string> fields;
        fields.emplace_back("I1");

        if (a_snapshot.health) {
            fields.push_back("h=" + FloatHex(*a_snapshot.health));
        }
        if (a_snapshot.charge) {
            fields.push_back("c=" + FloatHex(*a_snapshot.charge));
        }
        if (a_snapshot.customName) {
            fields.push_back(
                "n=" + Protocol::HexEncode(*a_snapshot.customName));
        }
        if (a_snapshot.ownershipFormID) {
            fields.push_back(
                fmt::format("o={:08X}", *a_snapshot.ownershipFormID));
        }
        if (a_snapshot.ownershipRank) {
            fields.push_back(
                fmt::format("r={}", *a_snapshot.ownershipRank));
        }
        if (a_snapshot.cannotWear) {
            fields.emplace_back("w=1");
        }

        if (a_snapshot.hasEnchantment) {
            fields.push_back(fmt::format(
                "x={}:{}:{}:{}",
                a_snapshot.dynamicEnchantment ? 1 : 0,
                a_snapshot.enchantmentFormID,
                a_snapshot.enchantmentMaxCharge,
                a_snapshot.removeOnUnequip ? 1 : 0));

            if (a_snapshot.dynamicEnchantment) {
                std::string encodedEffects;
                for (const auto& effect : a_snapshot.enchantmentEffects) {
                    if (!encodedEffects.empty()) {
                        encodedEffects.push_back(',');
                    }
                    encodedEffects += fmt::format(
                        "{:08X}.{}.{}.{}.{}",
                        effect.effectFormID,
                        FloatHex(effect.magnitude),
                        effect.area,
                        effect.duration,
                        FloatHex(effect.cost));
                }
                fields.push_back("e=" + encodedEffects);
            }
        }

        if (a_snapshot.poisonFormID) {
            fields.push_back(fmt::format(
                "p={:08X}:{}",
                *a_snapshot.poisonFormID,
                a_snapshot.poisonCount));
        }

        if (a_snapshot.soulLevel) {
            fields.push_back(
                fmt::format("s={}", *a_snapshot.soulLevel));
        }

        std::string result;
        for (const auto& field : fields) {
            if (!result.empty()) {
                result.push_back('|');
            }
            result += field;
        }
        return result;
    }

    std::optional<Snapshot> Decode(std::string_view a_encoded)
    {
        Snapshot result{};
        if (a_encoded == "-") {
            return result;
        }

        const auto fields = Split(a_encoded, '|');
        if (fields.empty() || fields.front() != "I1") {
            return std::nullopt;
        }

        for (std::size_t i = 1; i < fields.size(); ++i) {
            const auto field = fields[i];
            const auto equal = field.find('=');
            if (equal == std::string_view::npos) {
                return std::nullopt;
            }

            const auto key = field.substr(0, equal);
            const auto value = field.substr(equal + 1);

            if (key == "h") {
                result.health = ParseFloatHex(value);
                if (!result.health) return std::nullopt;
            } else if (key == "c") {
                result.charge = ParseFloatHex(value);
                if (!result.charge) return std::nullopt;
            } else if (key == "n") {
                auto decoded = Protocol::HexDecode(value);
                if (!decoded || decoded->empty() ||
                    decoded->size() > kMaximumCustomNameBytes) {
                    return std::nullopt;
                }
                result.customName = std::move(*decoded);
            } else if (key == "o") {
                result.ownershipFormID = ParseUnsigned<RE::FormID>(value, 16);
                if (!result.ownershipFormID) return std::nullopt;
            } else if (key == "r") {
                std::int32_t rank = 0;
                const auto parsed = std::from_chars(
                    value.data(), value.data() + value.size(), rank);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != value.data() + value.size()) {
                    return std::nullopt;
                }
                result.ownershipRank = rank;
            } else if (key == "w") {
                if (value != "1") return std::nullopt;
                result.cannotWear = true;
            } else if (key == "x") {
                const auto parts = Split(value, ':');
                if (parts.size() != 4) return std::nullopt;
                auto dynamic = ParseUnsigned<std::uint8_t>(parts[0]);
                auto formID = ParseUnsigned<RE::FormID>(parts[1]);
                auto maxCharge = ParseUnsigned<std::uint16_t>(parts[2]);
                auto remove = ParseUnsigned<std::uint8_t>(parts[3]);
                if (!dynamic || !formID || !maxCharge || !remove ||
                    *dynamic > 1 || *remove > 1) {
                    return std::nullopt;
                }
                result.hasEnchantment = true;
                result.dynamicEnchantment = *dynamic == 1;
                result.enchantmentFormID = *formID;
                result.enchantmentMaxCharge = *maxCharge;
                result.removeOnUnequip = *remove == 1;
            } else if (key == "e") {
                if (value.empty()) return std::nullopt;
                const auto encodedEffects = Split(value, ',');
                if (encodedEffects.empty() ||
                    encodedEffects.size() > kMaximumEffects) {
                    return std::nullopt;
                }
                for (const auto encodedEffect : encodedEffects) {
                    const auto parts = Split(encodedEffect, '.');
                    if (parts.size() != 5) return std::nullopt;
                    auto formID = ParseUnsigned<RE::FormID>(parts[0], 16);
                    auto magnitude = ParseFloatHex(parts[1]);
                    auto area = ParseUnsigned<std::uint32_t>(parts[2]);
                    auto duration = ParseUnsigned<std::uint32_t>(parts[3]);
                    auto cost = ParseFloatHex(parts[4]);
                    if (!formID || !magnitude || !area || !duration || !cost) {
                        return std::nullopt;
                    }
                    result.enchantmentEffects.push_back(
                        EffectSnapshot{
                            *formID,
                            *magnitude,
                            *area,
                            *duration,
                            *cost
                        });
                }
            } else if (key == "p") {
                const auto parts = Split(value, ':');
                if (parts.size() != 2) return std::nullopt;
                result.poisonFormID =
                    ParseUnsigned<RE::FormID>(parts[0], 16);
                const auto count =
                    ParseUnsigned<std::uint32_t>(parts[1]);
                if (!result.poisonFormID || !count || *count == 0) {
                    return std::nullopt;
                }
                result.poisonCount = *count;
            } else if (key == "s") {
                result.soulLevel = ParseUnsigned<std::uint8_t>(value);
                if (!result.soulLevel) return std::nullopt;
            } else {
                return std::nullopt;
            }
        }

        if (result.dynamicEnchantment &&
            (!result.hasEnchantment ||
             result.enchantmentEffects.empty())) {
            return std::nullopt;
        }
        if (!result.dynamicEnchantment &&
            result.hasEnchantment &&
            result.enchantmentFormID == 0) {
            return std::nullopt;
        }

        return result;
    }

    RE::ExtraDataList* Rebuild(
        RE::TESBoundObject* a_object,
        const Snapshot& a_snapshot,
        std::string& a_error)
    {
        if (!a_object) {
            a_error = "objet de base indisponible";
            return nullptr;
        }
        if (IsEmpty(a_snapshot)) {
            return nullptr;
        }

        auto* extraList = new RE::ExtraDataList();

        if (a_snapshot.health) {
            SetHealthCompat(extraList, *a_snapshot.health);
        }

        RE::EnchantmentItem* enchantment = nullptr;
        if (a_snapshot.hasEnchantment) {
            enchantment =
                ResolveOrCreateEnchantment(
                    a_object,
                    a_snapshot,
                    a_error);
            if (!enchantment) {
                delete extraList;
                return nullptr;
            }
            // Let Skyrim build/register the enchantment ExtraData itself.
            // This is important for dynamically-created enchantments: directly
            // constructing ExtraEnchantment bypasses engine-side bookkeeping
            // and can leave an inventory entry that crashes when enumerated.
            SetEnchantmentCompat(
                extraList,
                enchantment,
                a_snapshot.enchantmentMaxCharge,
                a_snapshot.removeOnUnequip);
        }

        if (a_snapshot.charge) {
            auto* charge = new RE::ExtraCharge();
            charge->charge = *a_snapshot.charge;
            extraList->Add(charge);
        }

        if (a_snapshot.customName) {
            SetOverrideNameCompat(
                extraList,
                a_snapshot.customName->c_str());
        }

        if (a_snapshot.ownershipFormID) {
            auto* owner =
                RE::TESForm::LookupByID(*a_snapshot.ownershipFormID);
            if (!owner) {
                a_error = fmt::format(
                    "owner introuvable localement: {:08X}",
                    *a_snapshot.ownershipFormID);
                delete extraList;
                return nullptr;
            }
            extraList->SetOwner(owner);
        }

        if (a_snapshot.ownershipRank) {
            extraList->Add(
                new RE::ExtraRank(*a_snapshot.ownershipRank));
        }

        if (a_snapshot.cannotWear) {
            extraList->Add(new RE::ExtraCannotWear());
        }

        if (a_snapshot.poisonFormID) {
            auto* poison =
                RE::TESForm::LookupByID<RE::AlchemyItem>(
                    *a_snapshot.poisonFormID);
            if (!poison) {
                a_error = fmt::format(
                    "poison introuvable localement: {:08X}",
                    *a_snapshot.poisonFormID);
                delete extraList;
                return nullptr;
            }
            extraList->Add(
                new RE::ExtraPoison(
                    poison,
                    static_cast<std::int32_t>(a_snapshot.poisonCount)));
        }

        if (a_snapshot.soulLevel) {
            extraList->Add(
                new RE::ExtraSoul(
                    static_cast<RE::SOUL_LEVEL>(*a_snapshot.soulLevel)));
        }

        return extraList;
    }
}
