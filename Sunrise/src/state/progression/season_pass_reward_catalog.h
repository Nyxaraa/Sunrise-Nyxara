#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::progression::season_pass {

/** Account progression used by the installed Season of Arrivals pass. */
inline constexpr std::uint16_t kProgressionDefinitionIndex = 40;
/** Repeating HUD bar paired with the Arrivals progression. */
inline constexpr std::uint16_t kHudProgressionDefinitionIndex = 41;

/**
 * The pass package expands into every installed Season 11 planetary-material stack.
 * TODO: extract; the package names a sack reward set, not an item list, and no reader
 * resolves a reward set to its members yet.
 */
inline constexpr std::uint32_t kDestinationResourceBundleHash = 3104539653U;
inline constexpr std::uint16_t kDestinationResourceQuantity = 50;
inline constexpr std::array<std::uint32_t, 9> kDestinationResourceHashes{
    3592324052U, // Helium Filaments
    31293053U,   // Seraphite
    49145143U,   // Simulation Seed
    2014411539U, // Alkane Dust
    1305274547U, // Phaseglass Needle
    950899352U,  // Dusklight Shard
    3487922223U, // Microphasic Datalattice
    592227263U,  // Baryon Bough
    1177810185U, // Etheric Spiral
};

/** The two auto-decrypting engram item definitions. */
inline constexpr std::uint32_t kLegendaryEngramHash = 2223145359U;
inline constexpr std::uint32_t kExoticEngramHash = 3875551374U;

/**
 * Exact non-class rewards advertised by the two auto-decrypting engram definitions.
 * TODO: extract; an engram names a sack reward set rather than items, and no reader resolves
 * a reward set to its members yet.
 */
inline constexpr std::array<std::uint32_t, 30> kLegendaryEngramWeapons{
    991314988U,  720351795U,  4230993599U, 253196586U,  4146702548U, 2957367743U,
    2009277538U, 3745990145U, 3356526253U, 821154603U,  3504336176U, 2199171672U,
    188882152U,  3863882743U, 4106983932U, 3569802112U, 1529450902U, 1807343361U,
    3622137132U, 3055192515U, 2257180473U, 2742838701U, 2742838700U, 1162247618U,
    1723380073U, 2807687156U, 1786797708U, 2857348871U, 1946491241U, 1835747805U,
};
/** Legendary engram Titan armour rewards. */
inline constexpr std::array<std::uint32_t, 18> kLegendaryTitanArmour{
    3852389988U,
    3250360146U,
    362404956U,
    3299386902U,
    1648238545U,
    2629014079U,
    881579413U,
    2112821379U,
    1063507982U,
    3651598572U,
    1076538456U,
    1560040304U,
    445618861U,
    238618945U,
    3406670226U,
    3238424670U,
    1566911695U,
    1407026808U,
};
/** Legendary engram Hunter armour rewards. */
inline constexpr std::array<std::uint32_t, 18> kLegendaryHunterArmour{
    3239215026U,
    974507844U,
    1399263478U,
    2808379196U,
    1153347999U,
    265279665U,
    2567710435U,
    2298664693U,
    4064910796U,
    2905153902U,
    3750877150U,
    344824594U,
    940065571U,
    382498903U,
    299852984U,
    3554672786U,
    902989307U,
    2944336620U,
};
/** Legendary engram Warlock armour rewards. */
inline constexpr std::array<std::uint32_t, 18> kLegendaryWarlockArmour{
    1920259123U,
    4239920089U,
    695071581U,
    2364041279U,
    2581516944U,
    3611199822U,
    2339155434U,
    597618504U,
    1655109893U,
    2837138379U,
    785967407U,
    3931361417U,
    1416697412U,
    1557571326U,
    936010065U,
    2640935765U,
    4245441464U,
    3523809305U,
};

/** Exotic engram non-class rewards. */
inline constexpr std::array<std::uint32_t, 10> kExoticEngramWeapons{
    4068264807U,
    2130065553U,
    3325463374U,
    1541131350U,
    814876685U,
    2694576561U,
    3766045777U,
    1852863732U,
    2044500762U,
    3413860063U,
};
/** Exotic engram Titan armour rewards. */
inline constexpr std::array<std::uint32_t, 10> kExoticTitanArmour{
    1190497097U,
    2326396534U,
    2255796155U,
    2240152949U,
    2578771006U,
    2423243921U,
    1734844650U,
    2808156426U,
    106575079U,
    1591207518U,
};
/** Exotic engram Hunter armour rewards. */
inline constexpr std::array<std::uint32_t, 10> kExoticHunterArmour{
    2268523867U,
    1219761634U,
    2757274117U,
    4165919945U,
    1474735276U,
    978537162U,
    1688602431U,
    1163283805U,
    896224899U,
    903984858U,
};
/** Exotic engram Warlock armour rewards. */
inline constexpr std::array<std::uint32_t, 10> kExoticWarlockArmour{
    2822465023U,
    235591051U,
    3948284065U,
    3084282676U,
    4057299719U,
    121305948U,
    3288917178U,
    3627185503U,
    1725917554U,
    1030017949U,
};

template <std::size_t Size>
/** @return True when the reward array holds that item hash. */
[[nodiscard]] constexpr bool contains(const std::array<std::uint32_t, Size>& hashes,
                                      std::uint32_t hash) noexcept {
    for (const std::uint32_t candidate : hashes) {
        if (candidate == hash) {
            return true;
        }
    }
    return false;
}

/** Checks one decrypted result against its wrapper's weapon and active-class pools. */
[[nodiscard]] constexpr bool contains_engram_reward(std::uint32_t engramHash,
                                                    std::uint32_t rewardHash,
                                                    std::uint8_t characterClass) noexcept {
    if (engramHash == kLegendaryEngramHash) {
        return contains(kLegendaryEngramWeapons, rewardHash)
               || (characterClass == 0 && contains(kLegendaryTitanArmour, rewardHash))
               || (characterClass == 1 && contains(kLegendaryHunterArmour, rewardHash))
               || (characterClass == 2 && contains(kLegendaryWarlockArmour, rewardHash));
    }
    if (engramHash == kExoticEngramHash) {
        return contains(kExoticEngramWeapons, rewardHash)
               || (characterClass == 0 && contains(kExoticTitanArmour, rewardHash))
               || (characterClass == 1 && contains(kExoticHunterArmour, rewardHash))
               || (characterClass == 2 && contains(kExoticWarlockArmour, rewardHash));
    }
    return false;
}

} // namespace sunrise::state::progression::season_pass
