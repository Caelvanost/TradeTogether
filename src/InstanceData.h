#pragma once

namespace TradeTogether::InstanceData
{
    struct EffectSnapshot
    {
        RE::FormID effectFormID{ 0 };
        float magnitude{ 0.0F };
        std::uint32_t area{ 0 };
        std::uint32_t duration{ 0 };
    };

    struct Snapshot
    {
        std::optional<float> health;
        std::optional<float> charge;
        std::optional<std::string> customName;
        std::optional<RE::FormID> ownershipFormID;
        std::optional<std::int32_t> ownershipRank;
        bool cannotWear{ false };

        bool hasEnchantment{ false };
        bool dynamicEnchantment{ false };
        RE::FormID enchantmentFormID{ 0 };
        std::uint16_t enchantmentMaxCharge{ 0 };
        bool removeOnUnequip{ false };
        std::vector<EffectSnapshot> enchantmentEffects;

        std::optional<RE::FormID> poisonFormID;
        std::uint32_t poisonCount{ 0 };
        std::optional<std::uint8_t> soulLevel;
    };

    [[nodiscard]] bool Capture(
        RE::TESBoundObject* a_object,
        RE::ExtraDataList* a_extraList,
        Snapshot& a_out,
        std::string& a_error);

    [[nodiscard]] std::string Encode(const Snapshot& a_snapshot);
    [[nodiscard]] std::optional<Snapshot> Decode(std::string_view a_encoded);

    [[nodiscard]] RE::ExtraDataList* Rebuild(
        RE::TESBoundObject* a_object,
        const Snapshot& a_snapshot,
        std::string& a_error);

    [[nodiscard]] bool IsEmpty(const Snapshot& a_snapshot);
}
