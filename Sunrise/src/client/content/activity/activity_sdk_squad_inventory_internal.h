#pragma once

#include "activity_sdk_squad_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::squad_inventory::detail {

/** Checks the topology rows consumed by extraction and linking. */
[[nodiscard]] bool valid_topology(const topology_inventory::Snapshot& source) noexcept;

/** Validates all normalized facts before linking begins. */
[[nodiscard]] bool validate_facts(const topology_inventory::Snapshot& topology, const Facts& facts);

/** @return True when two parsed spawner definitions from different paths agree exactly. */
[[nodiscard]] bool same_spawner(const SpawnerFact& left, const SpawnerFact& right) noexcept;

/** @return True when two parsed spawn-rule definitions from different paths agree exactly. */
[[nodiscard]] bool same_rule(const RuleFact& left, const RuleFact& right) noexcept;

/** Parses one exact authored type-1 config. */
[[nodiscard]] bool
parse_spawner(std::uint32_t tag, std::span<const std::byte> blob, SpawnerFact& output);

/** Builds the rule row a spawner's own point set stands for. */
[[nodiscard]] RuleFact inline_rule(const SpawnerFact& spawner);

/** Parses one exact authored type-66 config. */
[[nodiscard]] bool parse_rule(std::uint32_t tag, std::span<const std::byte> blob, RuleFact& output);

/** Builds one member row and retains an unresolved actor link when needed. */
[[nodiscard]] bool build_member(const MemberFact& source,
                                std::string_view squadDigest,
                                std::uint32_t ordinal,
                                ActorResolver actorResolver,
                                void* actorContext,
                                SquadMember& output,
                                bool& actorLinksComplete);

} // namespace sunrise::client::content::activity::sdk_generation::squad_inventory::detail
