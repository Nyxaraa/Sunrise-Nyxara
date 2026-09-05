#include "registry.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace sunrise::client::patterns {
namespace {

/** Returned by next_candidate when a range holds no further anchor byte. */
constexpr std::size_t kNoCandidate = static_cast<std::size_t>(-1);
/** One count per distinct byte value. */
constexpr std::size_t kByteValueCount = 256;
/** Most ranges one fingerprint describes. No PE image carries more sections than this. */
constexpr std::size_t kFingerprintCapacity = 96;

/** How often each byte value occurs across one set of scanned ranges. */
struct ByteCounts {
    std::array<std::uint64_t, kByteValueCount> values{};
};

/** Identity of the range set one histogram was built from. */
struct Fingerprint {
    std::array<const std::byte*, kFingerprintCapacity> data{};
    std::array<std::size_t, kFingerprintCapacity> size{};
    std::size_t count{};
    /** False for a range set too large to describe, which must never match a stored print. */
    bool valid{};
};

/**
 * The byte histogram and the ranges it came from.
 * Building it costs one traversal of the image. Without this cache every pattern would pay that
 * traversal, which is the very cost the anchor choice exists to avoid.
 */
struct FrequencyCache {
    SRWLOCK lock{SRWLOCK_INIT};
    Fingerprint fingerprint{};
    ByteCounts counts{};
};

FrequencyCache g_frequency;

/** @return Fingerprint of one range set, invalid when it holds more ranges than one can describe. */
[[nodiscard]] Fingerprint fingerprint_of(std::span<const ImageRange> image) noexcept {
    Fingerprint print{};
    if (image.size() > kFingerprintCapacity) {
        return print;
    }
    for (std::size_t index = 0; index < image.size(); ++index) {
        print.data[index] = image[index].bytes.data();
        print.size[index] = image[index].bytes.size();
    }
    print.count = image.size();
    print.valid = true;
    return print;
}

/** @return True when both fingerprints name the same ranges in the same order. */
[[nodiscard]] bool same_ranges(const Fingerprint& left, const Fingerprint& right) noexcept {
    if (!left.valid || !right.valid || left.count != right.count) {
        return false;
    }
    for (std::size_t index = 0; index < left.count; ++index) {
        if (left.data[index] != right.data[index] || left.size[index] != right.size[index]) {
            return false;
        }
    }
    return true;
}

/** Counts every byte value across one range set. */
void count_bytes(std::span<const ImageRange> image, ByteCounts& counts) noexcept {
    counts = {};
    for (const ImageRange range : image) {
        for (const std::byte value : range.bytes) {
            ++counts.values[std::to_integer<unsigned char>(value)];
        }
    }
}

/**
 * Reads the byte histogram for one range set, building it on the first request.
 * @param image Ranges about to be scanned.
 * @param counts Receives a copy, so no caller holds the cache lock while it scans.
 */
void byte_counts(std::span<const ImageRange> image, ByteCounts& counts) noexcept {
    const Fingerprint wanted = fingerprint_of(image);
    AcquireSRWLockExclusive(&g_frequency.lock);
    if (!same_ranges(g_frequency.fingerprint, wanted)) {
        count_bytes(image, g_frequency.counts);
        g_frequency.fingerprint = wanted;
    }
    counts = g_frequency.counts;
    ReleaseSRWLockExclusive(&g_frequency.lock);
}

/**
 * The one exact byte a pattern's candidate search keys on.
 * A pattern with no exact byte cannot be scanned, so the anchor doubles as the validity check.
 */
struct Anchor {
    /** Position of the anchor byte inside the pattern. */
    std::size_t index{};
    /** The byte itself, held unsigned so it reaches memchr without sign extension. */
    unsigned char value{};
    /** False for a pattern the sweep cannot scan, which is the pattern it must reject. */
    bool valid{};
};

/**
 * Picks the anchor byte for one pattern.
 * The candidate search keys on this byte, so the rarest exact byte is the one that lets memchr
 * skip the most. Taking the first exact byte instead lands on a REX prefix for most function
 * prologues, and those are among the most common bytes there are in compiled x64: the sweep then
 * stops to verify millions of times per pattern.
 * @param pattern Pattern name, bytes, and exact-byte mask.
 * @param counts How often each byte value occurs in the ranges about to be scanned.
 * @return A valid anchor when the pattern has a name, bytes, and at least one exact byte.
 */
[[nodiscard]] Anchor anchor_of(const Pattern& pattern, const ByteCounts& counts) noexcept {
    if (pattern.name.empty() || pattern.bytes.empty()) {
        return {};
    }
    Anchor best{};
    std::uint64_t bestCount = 0;
    for (std::size_t index = 0; index < pattern.bytes.size(); ++index) {
        if (!pattern.bytes[index].exact) {
            continue;
        }
        const auto value = std::to_integer<unsigned char>(pattern.bytes[index].value);
        const std::uint64_t occurrences = counts.values[value];
        // The earliest byte wins a tie, so one image always picks the same anchor.
        if (best.valid && occurrences >= bestCount) {
            continue;
        }
        best = Anchor{index, value, true};
        bestCount = occurrences;
    }
    return best;
}

/**
 * Tests one pattern at one bounded image offset.
 * @param image Executable image bytes.
 * @param pattern Masked pattern bytes.
 * @return True when every exact byte matches.
 */
[[nodiscard]] bool matches_at(std::span<const std::byte> image,
                              std::size_t offset,
                              std::span<const PatternByte> pattern) noexcept {
    // next_candidate never returns an offset past the last whole match, so this cannot wrap.
    if (pattern.size() > image.size() - offset) {
        return false;
    }
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (pattern[index].exact && image[offset + index] != pattern[index].value) {
            return false;
        }
    }
    return true;
}

/**
 * Finds the next offset at or after one start where the anchor byte lines up.
 * The bytes in between cannot begin a match, so memchr skips them at memory speed instead of the
 * sweep testing every one of them.
 * @param range One executable range.
 * @param patternSize Pattern length, which bounds the last offset that can hold a whole match.
 * @param anchor Valid anchor for that pattern.
 * @param from First offset to consider.
 * @return Candidate offset, or kNoCandidate when the range holds no further one.
 */
[[nodiscard]] std::size_t next_candidate(std::span<const std::byte> range,
                                         std::size_t patternSize,
                                         const Anchor& anchor,
                                         std::size_t from) noexcept {
    if (patternSize > range.size()) {
        return kNoCandidate;
    }
    const std::size_t lastOffset = range.size() - patternSize;
    if (from > lastOffset) {
        return kNoCandidate;
    }
    // The anchor sits at offset + index, so the search window is the offset window shifted by it.
    const std::size_t first = from + anchor.index;
    const std::size_t last = lastOffset + anchor.index;
    const void* const hit = std::memchr(range.data() + first, anchor.value, last - first + 1);
    if (hit == nullptr) {
        return kNoCandidate;
    }
    const auto* const found = static_cast<const std::byte*>(hit);
    return static_cast<std::size_t>(found - range.data()) - anchor.index;
}

} // namespace

/** Resolves every registered pattern against one executable range. */
bool resolve_all(std::span<std::byte> image,
                 std::span<const Pattern> patterns,
                 std::span<Match> matches) noexcept {
    const ImageRange range{image};
    return resolve_all(std::span(&range, 1), patterns, matches);
}

/** Resolves every pattern across disjoint executable image ranges. */
bool resolve_all(std::span<const ImageRange> image,
                 std::span<const Pattern> patterns,
                 std::span<Match> matches) noexcept {
    if (patterns.size() != matches.size()) {
        return false;
    }

    ByteCounts counts;
    byte_counts(image, counts);
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        const Anchor anchor = anchor_of(patterns[index], counts);
        matches[index] = anchor.valid ? Match{MatchStatus::missing, nullptr} : Match{};
        if (!anchor.valid) {
            continue;
        }

        Match& match = matches[index];
        const std::span<const PatternByte> bytes = patterns[index].bytes;
        for (const ImageRange range : image) {
            std::size_t offset = 0;
            while (match.status != MatchStatus::ambiguous) {
                offset = next_candidate(range.bytes, bytes.size(), anchor, offset);
                if (offset == kNoCandidate) {
                    break;
                }
                if (matches_at(range.bytes, offset, bytes)) {
                    // A second match invalidates the address instead of choosing one.
                    match = match.status == MatchStatus::missing
                                ? Match{MatchStatus::unique, range.bytes.data() + offset}
                                : Match{MatchStatus::ambiguous, nullptr};
                }
                ++offset;
            }
            // Ambiguous is final, so the remaining ranges cannot change this pattern's result.
            if (match.status == MatchStatus::ambiguous) {
                break;
            }
        }
    }
    return true;
}

/** Collects bounded matches for one signature that is expected to repeat. */
std::size_t collect_matches(std::span<const ImageRange> image,
                            const Pattern& pattern,
                            std::span<std::byte*> output) noexcept {
    ByteCounts counts;
    byte_counts(image, counts);
    const Anchor anchor = anchor_of(pattern, counts);
    if (!anchor.valid || output.empty()) {
        return 0;
    }
    std::size_t count = 0;
    for (const ImageRange range : image) {
        for (std::size_t offset = next_candidate(range.bytes, pattern.bytes.size(), anchor, 0);
             offset != kNoCandidate;
             offset = next_candidate(range.bytes, pattern.bytes.size(), anchor, offset + 1)) {
            if (!matches_at(range.bytes, offset, pattern.bytes)) {
                continue;
            }
            output[count++] = range.bytes.data() + offset;
            if (count == output.size()) {
                return count;
            }
        }
    }
    return count;
}

} // namespace sunrise::client::patterns
