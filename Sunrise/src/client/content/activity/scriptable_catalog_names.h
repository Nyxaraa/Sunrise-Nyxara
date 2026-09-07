#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

/** One bounded inline package string retained with its exact source definition. */
struct InlineName final {
    std::uint32_t hash{};
    std::uint32_t sourceTag{};
    std::string value{};
};

using NameCancelCheck = bool (*)() noexcept;

/** Resolves authored hashes and exact installed-package definition tags in one snapshot. */
[[nodiscard]] bool resolve_names(state::build_data::scriptables::Snapshot& output,
                                 std::span<const InlineName> inlineNames,
                                 NameCancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
