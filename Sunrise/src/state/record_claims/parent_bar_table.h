#pragma once

#include <array>
#include <cstdint>

namespace sunrise::state::record_claims::parent_bar_table {

/** Lore parent-bar values keyed by node; four decoded entries remain unconfirmed in game. */
struct Bar {
    std::uint16_t nodeIndex;
    std::uint16_t valueIndex;
};

inline constexpr std::array<Bar, 35> kBars{{
    {838U, 2398U}, // Confessions
    {815U, 1932U}, // The Lawless Frontier (parent: The Tangled Shore)
    {816U, 1933U}, // The Man They Call Cayde
    {817U, 1940U}, // Ghost Stories
    {818U, 1941U}, // Most Loyal
    {819U, 2266U}, // Letters from a Renegade (decoded)
    {821U, 2273U}, // Dawning Delights
    {831U, 1931U}, // The Forsaken Prince
    {832U, 1936U}, // Truth to Power
    {833U, 1938U}, // A Drifter's Gambit
    {836U, 2347U}, // For Every Rose, a Thorn
    {837U, 2399U}, // The Chronicon
    {842U, 2585U}, // Trials and Tribulations
    {843U, 2663U}, // The Singular Exegete
    {845U, 1934U}, // The Dreaming City
    {846U, 1935U}, // Marasenna
    {847U, 1937U}, // The Awoken of the Reef
    {848U, 2267U}, // The Black Armory Papers
    {849U, 2348U}, // Ecdysis
    {851U, 2397U}, // Nothing Ends
    {824U, 2349U}, // The Warlock Aunor — decoded, unconfirmed (gate 2346)
    {828U, 2575U}, // Constellations — decoded, unconfirmed (gate 2574)
    {829U, 2665U}, // Duress and Egress — decoded, unconfirmed (gate 2664)
    {854U, 2583U}, // The Liar — decoded, unconfirmed (gate 2584)
    {835U, 2265U}, // The Book of Unmaking (measured)
    {850U, 2341U}, // A Man with No Name (shared gate/bar)
    {822U, 2342U}, // Dust (shared gate/bar)
    {823U, 2344U}, // Stolen Intelligence (shared gate/bar)
    {839U, 2514U}, // Unveiling (shared gate/bar)
    {852U, 2516U}, // Aspect (shared gate/bar)
    {840U, 2517U}, // Last Days on Kraken Mare (shared gate/bar)
    {853U, 2518U}, // Revelation (shared gate/bar)
    {841U, 2519U}, // Inquisition of the Damned (shared gate/bar)
    {825U, 2520U}, // Luna's Lost (shared gate/bar)
    {826U, 2521U}, // Letters from Eris (shared gate/bar)
}};

} // namespace sunrise::state::record_claims::parent_bar_table
