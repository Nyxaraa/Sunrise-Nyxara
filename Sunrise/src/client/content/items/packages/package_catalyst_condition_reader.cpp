#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "../../../../middleware/content/packages/tables/definition_index_table.h"
#include "package_socket_plug_build.h"

namespace sunrise::client::content::items::packages {
namespace {

/** One value term is read-value, push-literal, then greater-or-equal. */
constexpr std::size_t kValueExpressionTokenCount = 3;

/** Adds one unique positive flag term to a bounded condition. */
[[nodiscard]] bool append_flag(catalysts::CompletionRequirements& output,
                               std::uint16_t definitionIndex) noexcept {
    const auto end = output.flags.begin() + output.flagCount;
    if (std::find(output.flags.begin(), end, definitionIndex) != end) {
        return true;
    }
    if (output.flagCount >= output.flags.size()) {
        return false;
    }
    output.flags[output.flagCount++] = definitionIndex;
    return true;
}

/** Adds or raises one unique signed-value minimum in a bounded condition. */
[[nodiscard]] bool upsert_value(catalysts::CompletionRequirements& output,
                                std::uint16_t index,
                                std::int32_t minimum) noexcept {
    for (std::size_t row = 0; row < output.valueCount; ++row) {
        if (output.values[row].index == index) {
            output.values[row].minimum = (std::max)(output.values[row].minimum, minimum);
            return true;
        }
    }
    if (output.valueCount >= output.values.size()) {
        return false;
    }
    output.values[output.valueCount++] = {index, minimum};
    return true;
}

/** Marks a conflicting or over-capacity native rule and clears its partial operands. */
void mark_ambiguous(catalysts::CompletionCondition& output) noexcept {
    output.completion = {};
    output.objectiveDefinitionIndex = catalysts::kUnavailableObjectiveIndex;
    output.state = catalysts::CompletionConditionState::ambiguous;
}

} // namespace

/**
 * Reads one catalyst's completion flags, value minimums and objective reference.
 * @param output Receives the unique condition, or its absent or ambiguous state.
 */
void read_catalyst_completion_condition(std::span<const std::byte> definition,
                                        std::uint16_t itemDefinitionIndex,
                                        catalysts::CompletionCondition& output) noexcept {
    output = {};
    output.itemDefinitionIndex = itemDefinitionIndex;
    output.objectiveDefinitionIndex = catalysts::kUnavailableObjectiveIndex;
    for (std::size_t descriptor = 0; descriptor + 2 * sizeof(std::uint64_t) <= definition.size();
         descriptor += sizeof(std::uint64_t)) {
        tables::Array expression{};
        if (!tables::find_array_at(definition, descriptor, expression)) {
            continue;
        }
        // An objective reference row is one 16-bit objective index; an array of several is skipped.
        // TODO: read the whole array and pick the objective a multi-objective catalyst completes
        // on.
        if (expression.elementClass == tables::kObjectiveReferenceArrayClass
            && expression.count == 1 && expression.dataOffset <= definition.size()
            && definition.size() - expression.dataOffset >= 2 * sizeof(std::uint32_t)) {
            std::array<std::uint32_t, 2> reference{};
            std::memcpy(
                reference.data(), definition.data() + expression.dataOffset, sizeof reference);
            if (reference[0] < catalysts::kUnavailableObjectiveIndex
                && reference[1] == tables::kObjectiveReferenceRowClass) {
                const auto objective = static_cast<std::uint16_t>(reference[0]);
                if (output.objectiveDefinitionIndex == catalysts::kUnavailableObjectiveIndex) {
                    output.objectiveDefinitionIndex = objective;
                } else if (output.objectiveDefinitionIndex != objective) {
                    mark_ambiguous(output);
                    return;
                }
            }
            continue;
        }
        if (expression.elementClass != tables::kInvestmentExpressionRowClass
            || expression.dataOffset > definition.size()
            || expression.count > (definition.size() - expression.dataOffset)
                                      / tables::kUnlockInstructionStride) {
            continue;
        }
        for (std::size_t token = 0; token < expression.count; ++token) {
            std::array<std::uint32_t, 2> current{};
            std::memcpy(current.data(),
                        definition.data() + expression.dataOffset
                            + token * tables::kUnlockInstructionStride,
                        sizeof current);
            if (current[0] == tables::kUnlockReadFlagOpcode
                && current[1] < state::build_data::items::kDefinitionCapacity
                && !append_flag(output.completion, static_cast<std::uint16_t>(current[1]))) {
                mark_ambiguous(output);
                return;
            }
            if (current[0] != tables::kUnlockReadValueOpcode
                || token + kValueExpressionTokenCount > expression.count) {
                continue;
            }
            std::array<std::uint32_t, kValueExpressionTokenCount * 2> tokens{};
            std::memcpy(tokens.data(),
                        definition.data() + expression.dataOffset
                            + token * tables::kUnlockInstructionStride,
                        sizeof tokens);
            if (tokens[2] != tables::kUnlockLiteralOpcode
                || tokens[4] != tables::kUnlockGreaterEqualOpcode || tokens[5] != UINT32_MAX
                || tokens[1] >= catalysts::kUnavailableCompletionValueIndex || tokens[3] == 0
                || tokens[3]
                       > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
                continue;
            }
            if (!upsert_value(output.completion,
                              static_cast<std::uint16_t>(tokens[1]),
                              static_cast<std::int32_t>(tokens[3]))) {
                mark_ambiguous(output);
                return;
            }
        }
    }
    std::sort(output.completion.flags.begin(),
              output.completion.flags.begin() + output.completion.flagCount);
    std::sort(output.completion.values.begin(),
              output.completion.values.begin() + output.completion.valueCount,
              [](const catalysts::CompletionValue& first,
                 const catalysts::CompletionValue& second) { return first.index < second.index; });
    if (output.completion.flagCount != 0 || output.completion.valueCount != 0
        || output.objectiveDefinitionIndex != catalysts::kUnavailableObjectiveIndex) {
        output.state = catalysts::CompletionConditionState::present;
    }
}

} // namespace sunrise::client::content::items::packages
