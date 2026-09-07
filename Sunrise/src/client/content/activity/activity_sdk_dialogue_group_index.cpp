#include "activity_sdk_dialogue_group_index.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace sunrise::client::content::activity::sdk_generation::dialogue_group_index {
namespace {

// One dialogue group row is 16 bytes in the blob.
constexpr std::size_t kGroupStride = 16U;

template <typename Value>
[[nodiscard]] bool
read_value(std::span<const std::byte> bytes, std::size_t offset, Value& output) noexcept {
    output = {};
    if (offset > bytes.size() || sizeof output > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&output, bytes.data() + offset, sizeof output);
    return true;
}

/** Applies one signed blob-relative offset. @return False when the result leaves the blob. */
[[nodiscard]] bool
add_relative(std::size_t member, std::int64_t relative, std::size_t& target) noexcept {
    if (relative >= 0) {
        const auto distance = static_cast<std::uint64_t>(relative);
        if (distance > (std::numeric_limits<std::size_t>::max)() - member) {
            return false;
        }
        target = member + static_cast<std::size_t>(distance);
        return true;
    }
    const auto distance = static_cast<std::uint64_t>(-(relative + 1)) + 1U;
    if (distance > member) {
        return false;
    }
    target = member - static_cast<std::size_t>(distance);
    return true;
}

} // namespace

/** Builds the sorted group index over one dialogue blob. @return False when it is malformed. */
bool build(std::span<const std::byte> bytes,
           std::size_t groupRows,
           std::size_t groupCount,
           std::vector<Span>& output) noexcept {
    output.clear();
    if (groupRows > bytes.size() || groupCount > (bytes.size() - groupRows) / kGroupStride) {
        return false;
    }
    try {
        output.reserve(groupCount);
        for (std::size_t index = 0; index < groupCount; ++index) {
            const std::size_t row = groupRows + index * kGroupStride;
            Span group{};
            std::int64_t relative = 0;
            if (!read_value(bytes, row, group.definitionHash)
                || !read_value(bytes, row + 8U, relative)
                || !add_relative(row + 8U, relative, group.begin) || group.begin > bytes.size()) {
                output.clear();
                return false;
            }
            output.push_back(group);
        }
        for (Span& group : output) {
            group.end = bytes.size();
            for (const Span& candidate : output) {
                if (candidate.begin > group.begin && candidate.begin < group.end) {
                    group.end = candidate.begin;
                }
            }
        }
        std::sort(output.begin(), output.end(), [](const Span& first, const Span& second) {
            return first.definitionHash < second.definitionHash;
        });
        if (std::adjacent_find(output.begin(),
                               output.end(),
                               [](const Span& first, const Span& second) {
                                   return first.definitionHash == second.definitionHash;
                               })
            != output.end()) {
            output.clear();
            return false;
        }
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

/** Finds one group by definition hash in the sorted index. */
bool find(std::span<const Span> groups, std::uint32_t definitionHash, Span& output) noexcept {
    output = {};
    const auto row = std::lower_bound(
        groups.begin(), groups.end(), definitionHash, [](const Span& group, std::uint32_t hash) {
            return group.definitionHash < hash;
        });
    if (row == groups.end() || row->definitionHash != definitionHash) {
        return false;
    }
    output = *row;
    return true;
}

} // namespace sunrise::client::content::activity::sdk_generation::dialogue_group_index
