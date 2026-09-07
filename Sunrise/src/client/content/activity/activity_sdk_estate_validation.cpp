#include "activity_sdk_estate_validation.h"

#include <Windows.h>

#include <string>
#include <utility>

#include "activity_sdk_tree_publication.h"

namespace sunrise::client::content::activity::sdk_generation::estate_validation {
namespace {

namespace publication = tree_publication;

SRWLOCK g_lock{SRWLOCK_INIT};
std::wstring g_artifactDirectory{};
Digest g_sourceFingerprint{};
Result g_result{};
bool g_hasResult{};

[[nodiscard]] bool cancelled(CancelProbe probe, void* context) noexcept {
    return probe != nullptr && probe(context);
}

[[nodiscard]] bool valid_digest(const Digest& value) noexcept {
    return value != Digest{};
}

/** Resolves one lexical path without opening the path or any generated artifact. */
[[nodiscard]] bool full_path(std::wstring_view input, std::wstring& output) noexcept {
    output.clear();
    if (input.empty()) {
        return false;
    }
    try {
        const std::wstring terminated(input);
        const DWORD needed = GetFullPathNameW(terminated.c_str(), 0, nullptr, nullptr);
        if (needed == 0) {
            return false;
        }
        std::wstring pending(static_cast<std::size_t>(needed), L'\0');
        const DWORD written = GetFullPathNameW(terminated.c_str(), needed, pending.data(), nullptr);
        if (written == 0 || written >= needed) {
            return false;
        }
        pending.resize(written);
        while (pending.size() > 3U && (pending.back() == L'\\' || pending.back() == L'/')) {
            pending.pop_back();
        }
        output = std::move(pending);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

[[nodiscard]] bool same_text(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size()
           && CompareStringOrdinal(left.data(),
                                   static_cast<int>(left.size()),
                                   right.data(),
                                   static_cast<int>(right.size()),
                                   TRUE)
                  == CSTR_EQUAL;
}

[[nodiscard]] bool path_prefix(std::wstring_view parent, std::wstring_view child) noexcept {
    return child.size() > parent.size() && child[parent.size()] == L'\\'
           && CompareStringOrdinal(parent.data(),
                                   static_cast<int>(parent.size()),
                                   child.data(),
                                   static_cast<int>(parent.size()),
                                   TRUE)
                  == CSTR_EQUAL;
}

[[nodiscard]] bool valid_result(const Result& result) noexcept {
    return result.scenarioCount != 0 && result.activityRootCount != 0 && result.activityCount != 0
           && result.packBytes != 0 && valid_digest(result.payloadSha256);
}

[[nodiscard]] bool expected_matches(const ExpectedCounts* expected, const Result& result) noexcept {
    return expected == nullptr
           || (expected->scenarioCount == result.scenarioCount
               && expected->activityRootCount == result.activityRootCount
               && expected->activityCount == result.activityCount);
}

} // namespace

/** @return The stable log name of one estate validation status. */
const char* status_name(Status value) noexcept {
    switch (value) {
    case Status::ready:
        return "ready";
    case Status::cancelled:
        return "cancelled";
    case Status::invalidInput:
        return "invalid_input";
    case Status::notGenerated:
        return "not_generated";
    case Status::publication:
        return "publication";
    }
    return "not_generated";
}

/** Records one validation result for an artifact tree. @return False when an input is bad. */
bool remember(std::wstring_view artifactDirectory,
              const Digest& sourceFingerprint,
              const Result& result) noexcept {
    std::wstring path;
    if (!valid_digest(sourceFingerprint) || !valid_result(result)
        || !full_path(artifactDirectory, path)) {
        return false;
    }
    remember_canonical(std::move(path), sourceFingerprint, result);
    return true;
}

void remember_canonical(std::wstring artifactDirectory,
                        const Digest& sourceFingerprint,
                        const Result& result) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_artifactDirectory = std::move(artifactDirectory);
    g_sourceFingerprint = sourceFingerprint;
    g_result = result;
    g_hasResult = true;
    ReleaseSRWLockExclusive(&g_lock);
}

void forget(std::wstring_view artifactDirectory) noexcept {
    std::wstring path;
    if (!full_path(artifactDirectory, path)) {
        return;
    }
    forget_canonical(path);
}

void forget_canonical(std::wstring_view artifactDirectory) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_hasResult && same_text(artifactDirectory, g_artifactDirectory)) {
        g_artifactDirectory.clear();
        g_sourceFingerprint = {};
        g_result = {};
        g_hasResult = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Validates one generated artifact directory against its fingerprint and expected counts. */
Status accept_generated(std::wstring_view artifactDirectory,
                        const Digest& sourceFingerprint,
                        const ExpectedCounts* expected,
                        CancelProbe cancel,
                        void* cancelContext,
                        Result& output) noexcept {
    output = {};
    if (cancelled(cancel, cancelContext)) {
        return Status::cancelled;
    }
    std::wstring path;
    if (!valid_digest(sourceFingerprint) || !full_path(artifactDirectory, path)) {
        return Status::invalidInput;
    }
    AcquireSRWLockShared(&g_lock);
    const bool matched = g_hasResult && same_text(path, g_artifactDirectory)
                         && sourceFingerprint == g_sourceFingerprint
                         && expected_matches(expected, g_result);
    if (matched) {
        output = g_result;
    }
    ReleaseSRWLockShared(&g_lock);
    return matched ? Status::ready : Status::notGenerated;
}

/** Validates a staged artifact tree, then publishes it over the output tree. */
Status publish_generated(std::wstring_view stageArtifactDirectory,
                         std::wstring_view outputArtifactDirectory,
                         const Digest& sourceFingerprint,
                         CancelProbe cancel,
                         void* cancelContext,
                         Result& output) noexcept {
    output = {};
    std::wstring stage;
    std::wstring target;
    if (!full_path(stageArtifactDirectory, stage) || !full_path(outputArtifactDirectory, target)
        || !valid_digest(sourceFingerprint) || same_text(stage, target)
        || path_prefix(stage, target) || path_prefix(target, stage)) {
        return Status::invalidInput;
    }
    if (cancelled(cancel, cancelContext)) {
        return Status::cancelled;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!g_hasResult || !same_text(stage, g_artifactDirectory)
        || sourceFingerprint != g_sourceFingerprint) {
        ReleaseSRWLockExclusive(&g_lock);
        return Status::notGenerated;
    }
    const Result retained = g_result;
    if (publication::publish(stage.c_str(), target.c_str(), nullptr, nullptr)
        != publication::Status::ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return Status::publication;
    }
    g_artifactDirectory = std::move(target);
    g_sourceFingerprint = sourceFingerprint;
    g_result = retained;
    g_hasResult = true;
    ReleaseSRWLockExclusive(&g_lock);
    output = retained;
    return Status::ready;
}

} // namespace sunrise::client::content::activity::sdk_generation::estate_validation
