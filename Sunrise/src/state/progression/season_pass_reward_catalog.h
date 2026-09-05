#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::progression::season_pass {

/** Account progression used by the installed Season of Arrivals pass. */
inline constexpr std::uint16_t kProgressionDefinitionIndex = 40;
/** Repeating HUD bar paired with the Arrivals progression. */
inline constexpr std::uint16_t kHudProgressionDefinitionIndex = 41;

/** Each rank-one premium wrapper opens into one class armour set plus the pass weapon. */
inline constexpr std::size_t kPremiumPackageItemCount = 6;

/** The pass package expands into every installed Season 11 planetary-material stack. */
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

inline constexpr std::uint32_t kLegendaryEngramHash = 2223145359U;
inline constexpr std::uint32_t kExoticEngramHash = 3875551374U;

/** Exact non-class rewards advertised by the two auto-decrypting engram definitions. */
inline constexpr std::array<std::uint32_t, 30> kLegendaryEngramWeapons{
    991314988U, 720351795U, 4230993599U, 253196586U, 4146702548U, 2957367743U,
    2009277538U, 3745990145U, 3356526253U, 821154603U, 3504336176U, 2199171672U,
    188882152U, 3863882743U, 4106983932U, 3569802112U, 1529450902U, 1807343361U,
    3622137132U, 3055192515U, 2257180473U, 2742838701U, 2742838700U, 1162247618U,
    1723380073U, 2807687156U, 1786797708U, 2857348871U, 1946491241U, 1835747805U,
};
inline constexpr std::array<std::uint32_t, 18> kLegendaryTitanArmour{
    3852389988U, 3250360146U, 362404956U, 3299386902U, 1648238545U, 2629014079U,
    881579413U, 2112821379U, 1063507982U, 3651598572U, 1076538456U, 1560040304U,
    445618861U, 238618945U, 3406670226U, 3238424670U, 1566911695U, 1407026808U,
};
inline constexpr std::array<std::uint32_t, 18> kLegendaryHunterArmour{
    3239215026U, 974507844U, 1399263478U, 2808379196U, 1153347999U, 265279665U,
    2567710435U, 2298664693U, 4064910796U, 2905153902U, 3750877150U, 344824594U,
    940065571U, 382498903U, 299852984U, 3554672786U, 902989307U, 2944336620U,
};
inline constexpr std::array<std::uint32_t, 18> kLegendaryWarlockArmour{
    1920259123U, 4239920089U, 695071581U, 2364041279U, 2581516944U, 3611199822U,
    2339155434U, 597618504U, 1655109893U, 2837138379U, 785967407U, 3931361417U,
    1416697412U, 1557571326U, 936010065U, 2640935765U, 4245441464U, 3523809305U,
};

inline constexpr std::array<std::uint32_t, 10> kExoticEngramWeapons{
    4068264807U, 2130065553U, 3325463374U, 1541131350U, 814876685U,
    2694576561U, 3766045777U, 1852863732U, 2044500762U, 3413860063U,
};
inline constexpr std::array<std::uint32_t, 10> kExoticTitanArmour{
    1190497097U, 2326396534U, 2255796155U, 2240152949U, 2578771006U,
    2423243921U, 1734844650U, 2808156426U, 106575079U, 1591207518U,
};
inline constexpr std::array<std::uint32_t, 10> kExoticHunterArmour{
    2268523867U, 1219761634U, 2757274117U, 4165919945U, 1474735276U,
    978537162U, 1688602431U, 1163283805U, 896224899U, 903984858U,
};
inline constexpr std::array<std::uint32_t, 10> kExoticWarlockArmour{
    2822465023U, 235591051U, 3948284065U, 3084282676U, 4057299719U,
    121305948U, 3288917178U, 3627185503U, 1725917554U, 1030017949U,
};

template <std::size_t Size>
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
[[nodiscard]] constexpr bool
contains_engram_reward(std::uint32_t engramHash,
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

struct PremiumClassPackage {
    std::uint32_t hash{};
    std::array<std::uint32_t, kPremiumPackageItemCount> items{};
};

inline constexpr std::array<PremiumClassPackage, 3> kPremiumClassPackages{{
    {1937677973U, {3750210364U, 2930001572U, 3673831673U, 3097544525U, 3136019014U, 2357297366U}},
    {1115291847U, {287888126U, 1585947570U, 1756730947U, 1214477175U, 1131831128U, 2357297366U}},
    {156406410U, {327547301U, 119457531U, 2771141010U, 1173249516U, 674876967U, 2357297366U}},
}};

[[nodiscard]] constexpr const PremiumClassPackage*
find_premium_class_package(std::uint32_t hash) noexcept {
    for (const auto& package : kPremiumClassPackages) {
        if (package.hash == hash) {
            return &package;
        }
    }
    return nullptr;
}

/** First account acquired-flag byte backing an Arrivals reward's claimed unlock. */
inline constexpr std::size_t kFirstClaimAccountFlagIndex = 11'726;
/** Reward 61 follows one reserved unlock row, so every later mapping advances one extra byte. */
inline constexpr std::uint16_t kClaimFlagGapRewardIndex = 61;

/** One opcode-2400 reward row. Field order keeps each row eight bytes. */
struct Reward {
    std::uint32_t itemHash{};
    std::uint16_t quantity{};
    std::uint8_t requiredRank{};

    constexpr Reward(std::uint8_t rank, std::uint32_t hash, std::uint16_t count) noexcept
        : itemHash(hash), quantity(count), requiredRank(rank) {}
};

static_assert(sizeof(Reward) == 8);

/**
 * Season of Arrivals reward rows in native progression order.
 * The opcode names the array index, so class-specific rows at one rank remain distinct.
 */
inline constexpr std::array<Reward, 196> kRewards{{
    {2, 2979281381U, 3},     {3, 3159615086U, 8000},   {4, 2979281381U, 3},
    {5, 3750210364U, 1},     {5, 287888126U, 1},       {5, 327547301U, 1},
    {6, 3159615086U, 8000},  {7, 3453985408U, 1},      {8, 2979281381U, 3},
    {9, 2817410917U, 200},   {10, 3136019014U, 1},     {10, 1131831128U, 1},
    {10, 674876967U, 1},     {11, 2979281381U, 2},     {12, 3159615086U, 6000},
    {13, 3767535285U, 1},    {14, 2979281381U, 2},     {15, 3673831673U, 1},
    {15, 1756730947U, 1},    {15, 2771141010U, 1},     {16, 3159615086U, 6000},
    {17, 3767535285U, 1},    {18, 2979281381U, 2},     {19, 2817410917U, 200},
    {20, 2930001572U, 1},    {20, 1585947570U, 1},     {20, 119457531U, 1},
    {21, 2979281381U, 2},    {23, 3767535285U, 1},     {25, 3097544525U, 1},
    {25, 1214477175U, 1},    {25, 1173249516U, 1},     {26, 2979281381U, 2},
    {27, 3767535285U, 1},    {29, 2817410917U, 200},   {30, 614426548U, 1},
    {33, 3767535285U, 1},    {35, 2357297366U, 1},     {37, 3767535285U, 1},
    {40, 2817410917U, 200},  {43, 3767535285U, 1},     {45, 1216130969U, 1},
    {47, 3767535285U, 1},    {50, 2817410917U, 200},   {53, 3767535285U, 1},
    {55, 3467984096U, 1},    {57, 3767535285U, 1},     {60, 2817410917U, 200},
    {63, 3767535285U, 1},    {65, 3875551374U, 1},     {67, 3767535285U, 1},
    {70, 2817410917U, 200},  {73, 3767535285U, 1},     {77, 3767535285U, 1},
    {80, 2817410917U, 200},  {83, 3767535285U, 1},     {87, 3767535285U, 1},
    {90, 2817410917U, 200},  {93, 3767535285U, 1},     {97, 3767535285U, 1},
    {100, 2817410917U, 500}, {1, 1937677973U, 1},      {1, 1115291847U, 1},
    {1, 156406410U, 1},      {2, 4237793825U, 250},    {2, 669434421U, 250},
    {3, 1869036888U, 1},     {4, 3853748946U, 5},      {5, 3659337057U, 1},
    {6, 1138508277U, 1},     {7, 1960641613U, 1},      {8, 3104539653U, 1},
    {9, 51755992U, 1},       {10, 3594816059U, 1},     {11, 1993687886U, 1},
    {12, 4237793825U, 250},  {12, 669434421U, 250},    {13, 2223145359U, 1},
    {14, 3750210364U, 1},    {14, 287888126U, 1},      {14, 327547301U, 1},
    {15, 3853748946U, 5},    {16, 1960641612U, 1},     {17, 3136019014U, 1},
    {17, 1131831128U, 1},    {17, 674876967U, 1},      {18, 4104235240U, 1},
    {19, 3104539653U, 1},    {20, 574946530U, 1},      {21, 1022552290U, 25},
    {22, 1993687887U, 1},    {23, 3159615086U, 10000}, {24, 3673831673U, 1},
    {24, 1756730947U, 1},    {24, 2771141010U, 1},     {25, 3875551374U, 1},
    {26, 3659337056U, 1},    {27, 2930001572U, 1},     {27, 1585947570U, 1},
    {27, 119457531U, 1},     {28, 200218998U, 1},      {29, 3104539653U, 1},
    {30, 2817410917U, 200},  {31, 1022552290U, 25},    {32, 889395850U, 1},
    {33, 3853748946U, 5},    {34, 3097544525U, 1},     {34, 1214477175U, 1},
    {34, 1173249516U, 1},    {35, 2223145359U, 1},     {36, 1960641615U, 1},
    {37, 3750210364U, 1},    {37, 287888126U, 1},      {37, 327547301U, 1},
    {38, 1412498128U, 1},    {39, 3104539653U, 1},     {40, 51755993U, 5},
    {41, 2223145359U, 1},    {42, 3159615086U, 12000}, {43, 1052346102U, 1},
    {44, 3136019014U, 1},    {44, 1131831128U, 1},     {44, 674876967U, 1},
    {45, 3853748946U, 5},    {46, 1960641614U, 1},     {47, 3673831673U, 1},
    {47, 1756730947U, 1},    {47, 2771141010U, 1},     {48, 2373084427U, 1},
    {49, 3104539653U, 1},    {50, 1901005163U, 1},     {51, 3382825553U, 1},
    {52, 3453985408U, 1},    {53, 2385964065U, 1},     {54, 2930001572U, 1},
    {54, 1585947570U, 1},    {54, 119457531U, 1},      {55, 2223145359U, 1},
    {56, 3659337059U, 1},    {57, 3097544525U, 1},     {57, 1214477175U, 1},
    {57, 1173249516U, 1},    {58, 3149088570U, 1},     {59, 3104539653U, 1},
    {60, 4257549984U, 3},    {61, 3382825552U, 1},     {62, 3159615086U, 14000},
    {63, 2935936434U, 1},    {64, 3045302654U, 1},     {64, 377715970U, 1},
    {64, 263289775U, 1},     {65, 3875551374U, 1},     {66, 1960641609U, 1},
    {67, 3889245656U, 1},    {67, 2463438524U, 1},     {67, 413332433U, 1},
    {68, 1915493615U, 1},    {69, 3104539653U, 1},     {70, 4257549984U, 3},
    {71, 849868305U, 1},     {72, 889395851U, 1},      {73, 255933968U, 1},
    {74, 3875551374U, 1},    {75, 3282419336U, 5},     {76, 1960641608U, 1},
    {77, 2745739715U, 1},    {77, 2082764111U, 1},     {77, 3378492868U, 1},
    {78, 1022552290U, 25},   {79, 3104539653U, 1},     {80, 4257549985U, 1},
    {81, 126458193U, 1},     {82, 3159615086U, 16000}, {83, 1711249415U, 1},
    {84, 3282419336U, 5},    {85, 4257549985U, 1},     {86, 3659337058U, 1},
    {87, 2574956338U, 1},    {87, 1764669670U, 1},     {87, 1598189577U, 1},
    {88, 2801311442U, 1},    {89, 3104539653U, 1},     {90, 372702809U, 1},
    {91, 3786371266U, 1},    {92, 4237793825U, 1000},  {92, 686728455U, 1000},
    {93, 213682406U, 1},     {94, 4257549985U, 1},     {95, 3103387299U, 1},
    {96, 2817410917U, 200},  {97, 3971891703U, 1},     {97, 2884709491U, 1},
    {97, 1034893694U, 1},    {98, 3875551374U, 1},     {99, 772166226U, 1},
    {100, 609666430U, 1},
}};

[[nodiscard]] constexpr const Reward* find(std::uint16_t rewardIndex) noexcept {
    return rewardIndex < kRewards.size() ? &kRewards[rewardIndex] : nullptr;
}

/** Resolves a native reward-array index to its backing account acquired-flag byte. */
[[nodiscard]] constexpr std::size_t claim_account_flag_index(std::uint16_t rewardIndex) noexcept {
    return kFirstClaimAccountFlagIndex + rewardIndex
           + static_cast<std::size_t>(rewardIndex >= kClaimFlagGapRewardIndex);
}

static_assert(claim_account_flag_index(63) == 11'790);
static_assert(claim_account_flag_index(static_cast<std::uint16_t>(kRewards.size() - 1U)) == 11'922);

} // namespace sunrise::state::progression::season_pass
