#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "../../../core/filesystem/path.h"
#include "../../../core/filesystem/temporary_sibling.h"
#include "../../../middleware/crypto/sha256.h"
#include "catalog_manifest_internal.h"

namespace sunrise::state::activity_sdk::generated_world::manifest {

using namespace internal;

namespace {

/** Process-local sequence separates sibling writers on one thread. */
volatile LONG g_temporaryCounter{};

/** Writes one exact byte span and rejects successful short writes. */
[[nodiscard]] bool write_exact(HANDLE file, std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return true;
    }
    if (bytes.size() > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }
    DWORD copied = 0;
    return WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &copied, nullptr)
               != FALSE
           && copied == bytes.size();
}

/** Reads one exact byte span and rejects successful short reads. */
[[nodiscard]] bool read_exact(HANDLE file, std::span<std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return true;
    }
    if (bytes.size() > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }
    DWORD copied = 0;
    return ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &copied, nullptr) != FALSE
           && copied == bytes.size();
}

/** Returns the fixed object as immutable bytes. */
template <typename Value>
[[nodiscard]] std::span<const std::byte> bytes_of(const Value& value) noexcept {
    return {reinterpret_cast<const std::byte*>(&value), sizeof value};
}

/** Returns one contiguous vector as immutable bytes. */
template <typename Value>
[[nodiscard]] std::span<const std::byte> bytes_of(std::span<const Value> values) noexcept {
    return {reinterpret_cast<const std::byte*>(values.data()), values.size_bytes()};
}

/** Returns one contiguous vector as mutable bytes. */
template <typename Value>
[[nodiscard]] std::span<std::byte> writable_bytes(std::span<Value> values) noexcept {
    return {reinterpret_cast<std::byte*>(values.data()), values.size_bytes()};
}

/** Appends one fixed-width record section to the authenticated payload. */
template <typename Value>
void append_payload(std::vector<std::byte>& payload,
                    std::size_t& offset,
                    std::span<const Value> values) noexcept {
    const std::span<const std::byte> bytes = bytes_of(values);
    if (!bytes.empty()) {
        std::memcpy(payload.data() + offset, bytes.data(), bytes.size());
    }
    offset += bytes.size();
}

/** Reads one fixed-width disk row out of an authenticated payload. */
template <typename Value>
[[nodiscard]] Value payload_record(std::span<const std::byte> payload,
                                   std::size_t offset) noexcept {
    Value output{};
    std::memcpy(&output, payload.data() + offset, sizeof output);
    return output;
}

/** Appends one fixed hexadecimal writer identity to a sibling path. */
[[nodiscard]] bool append_identifier(core::path::Buffer& path, DWORD value) noexcept {
    if (!core::path::append(path, kComponentSeparator)
        || path.length + kWriterIdentifierDigits >= path.chars.size()) {
        return false;
    }
    for (std::size_t digit = 0; digit < kWriterIdentifierDigits; ++digit) {
        const std::size_t remainingDigits = kWriterIdentifierDigits - digit - 1U;
        const std::size_t shift = remainingDigits * kBitsPerHexadecimalDigit;
        const std::size_t character = (value >> shift) & kHexadecimalDigitMask;
        path.chars[path.length++] = kHexadecimalDigits[character];
    }
    path.chars[path.length] = L'\0';
    return true;
}

/** Builds one writer-owned sibling path beside the final catalog. */
[[nodiscard]] bool
make_temporary_path(const wchar_t* finalPath, LONG counter, core::path::Buffer& output) noexcept {
    return finalPath != nullptr && finalPath[0] != L'\0' && core::path::assign(output, finalPath)
           && append_identifier(output, GetCurrentProcessId())
           && append_identifier(output, GetCurrentThreadId())
           && append_identifier(output, static_cast<DWORD>(counter))
           && core::path::append(output, kTemporarySuffix);
}

/** Opens one unique writer-owned sibling with readback access. */
[[nodiscard]] HANDLE open_temporary(const wchar_t* finalPath,
                                    core::path::Buffer& temporaryPath) noexcept {
    for (std::size_t attempt = 0; attempt < kTemporaryNameAttempts; ++attempt) {
        const LONG counter = InterlockedIncrement(&g_temporaryCounter);
        if (!make_temporary_path(finalPath, counter, temporaryPath)) {
            return INVALID_HANDLE_VALUE;
        }
        const HANDLE file = CreateFileW(temporaryPath.chars.data(),
                                        GENERIC_READ | GENERIC_WRITE,
                                        0,
                                        nullptr,
                                        CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                        nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) {
            return file;
        }
    }
    return INVALID_HANDLE_VALUE;
}

/** Returns the exact payload size encoded by one version-four header. */
[[nodiscard]] std::uint64_t payload_size(const Header& header) noexcept {
    return sizeof(DiskGenerationIdentity) + sizeof(DiskBindingCompleteness)
           + static_cast<std::uint64_t>(header.scenarioRecordCount) * sizeof(DiskScenarioRecord)
           + static_cast<std::uint64_t>(header.activityRootRecordCount)
                 * sizeof(DiskActivityRootRecord)
           + static_cast<std::uint64_t>(header.activityVariantRecordCount)
                 * sizeof(DiskActivityVariantRecord)
           + static_cast<std::uint64_t>(header.evidenceTagRecordCount) * sizeof(std::uint32_t)
           + static_cast<std::uint64_t>(header.packageLocatorRecordCount)
                 * sizeof(DiskPackageLocator);
}

/** Returns the exact scenario payload size encoded by one version-one header. */
[[nodiscard]] std::uint64_t legacy_payload_size(const LegacyHeader& header) noexcept {
    return static_cast<std::uint64_t>(header.recordCount) * sizeof(DiskScenarioRecord);
}

/** Checks the exact version-one header shape and bounded file size. */
[[nodiscard]] bool valid_legacy_header_shape(const LegacyHeader& header,
                                             std::uint64_t fileSize) noexcept {
    return header.magic == kMagic && header.version == kLegacyVersion
           && header.headerSize == sizeof(LegacyHeader) && header.fileSize == fileSize
           && header.recordSize == sizeof(DiskScenarioRecord)
           && header.recordCount <= kMaximumScenarioRecords && header.reserved == 0
           && header.fileSize == sizeof(LegacyHeader) + legacy_payload_size(header)
           && legacy_payload_size(header) <= kMaximumScenarioRecords * sizeof(DiskScenarioRecord);
}

/** Checks the exact version-four header shape and bounded file size. */
[[nodiscard]] bool valid_header_shape(const Header& header, std::uint64_t fileSize) noexcept {
    return header.magic == kMagic && header.version == kVersion
           && header.headerSize == sizeof(Header) && header.fileSize == fileSize
           && header.scenarioRecordSize == sizeof(DiskScenarioRecord)
           && header.scenarioRecordCount <= kMaximumScenarioRecords
           && header.activityRootRecordSize == sizeof(DiskActivityRootRecord)
           && header.activityRootRecordCount <= kMaximumActivityRootRecords
           && header.activityVariantRecordSize == sizeof(DiskActivityVariantRecord)
           && header.activityVariantRecordCount <= kMaximumActivityVariantRecords
           && header.evidenceTagRecordSize == sizeof(std::uint32_t)
           && header.evidenceTagRecordCount <= kMaximumEvidenceTags
           && header.packageLocatorRecordSize == sizeof(DiskPackageLocator)
           && header.packageLocatorRecordCount <= kMaximumPackageLocators && header.reserved == 0
           && valid_digest(header.sourceFingerprint) && valid_digest(header.payloadSha256)
           && header.fileSize == sizeof(Header) + payload_size(header)
           && payload_size(header) <= kMaximumPayloadSize;
}

/** Loads a strict version-one catalog for scenario shard reuse during migration. */
[[nodiscard]] bool load_legacy_catalog(HANDLE file,
                                       std::uint64_t fileSize,
                                       const Digest& expectedSourceFingerprint,
                                       Catalog& catalog,
                                       LoadStatus& status) noexcept {
    LARGE_INTEGER beginning{};
    LegacyHeader header{};
    bool complete = fileSize >= sizeof(LegacyHeader)
                    && SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) != FALSE
                    && read_exact(file, writable_bytes(std::span(&header, 1)));
    if (!complete || !valid_legacy_header_shape(header, fileSize)) {
        (void)CloseHandle(file);
        return false;
    }
    if (header.sourceFingerprint != expectedSourceFingerprint) {
        (void)CloseHandle(file);
        status = LoadStatus::sourceMismatch;
        return false;
    }

    std::vector<std::byte> payload;
    std::vector<ScenarioRecord> scenarios;
    try {
        payload.resize(static_cast<std::size_t>(legacy_payload_size(header)));
        scenarios.resize(header.recordCount);
    } catch (...) {
        (void)CloseHandle(file);
        return false;
    }
    complete = read_exact(file, std::span(payload));
    complete = CloseHandle(file) != FALSE && complete;
    Digest payloadSha256{};
    complete =
        complete
        && middleware::crypto::sha256::hash(std::span<const std::byte>(payload), payloadSha256)
        && payloadSha256 == header.payloadSha256;

    std::size_t offset = 0;
    for (std::size_t index = 0; complete && index < scenarios.size(); ++index) {
        const DiskScenarioRecord disk = payload_record<DiskScenarioRecord>(payload, offset);
        offset += sizeof disk;
        complete = decode_scenario(disk, scenarios[index]);
    }
    complete = complete && offset == payload.size()
               && valid_catalog(scenarios,
                                std::span<const ActivityRootRecord>{},
                                std::span<const ActivityVariantRecord>{});
    if (!complete) {
        return false;
    }

    catalog.sourceFingerprint = header.sourceFingerprint;
    catalog.payloadSha256 = header.payloadSha256;
    catalog.records = std::move(scenarios);
    status = LoadStatus::loaded;
    return true;
}

} // namespace

namespace {

/**
 * Loads one complete catalog and optionally requires an already-known SDK identity.
 * The source-only form authenticates the hashed identity payload before returning it.
 */
[[nodiscard]] bool load_impl(const wchar_t* path,
                             const Digest& expectedSourceFingerprint,
                             const SdkIdentity* expectedSdk,
                             Catalog& catalog,
                             LoadStatus& status) noexcept {
    catalog = {};
    status = LoadStatus::invalid;
    if (path == nullptr || path[0] == L'\0' || !valid_digest(expectedSourceFingerprint)
        || (expectedSdk != nullptr && !valid_sdk(*expectedSdk))) {
        return false;
    }
    const HANDLE file = CreateFileW(path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        status = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                     ? LoadStatus::missing
                     : LoadStatus::invalid;
        return false;
    }

    LARGE_INTEGER nativeSize{};
    Prefix prefix{};
    bool complete = GetFileSizeEx(file, &nativeSize) != FALSE && nativeSize.QuadPart >= 0
                    && nativeSize.QuadPart >= static_cast<LONGLONG>(sizeof(Prefix))
                    && read_exact(file, writable_bytes(std::span(&prefix, 1)));
    if (!complete || prefix.magic != kMagic) {
        (void)CloseHandle(file);
        return false;
    }
    if (prefix.version == kLegacyVersion) {
        const bool legacyLoaded =
            load_legacy_catalog(file,
                                static_cast<std::uint64_t>(nativeSize.QuadPart),
                                expectedSourceFingerprint,
                                catalog,
                                status);
        if (legacyLoaded) {
            catalog = {};
            status = LoadStatus::sdkMismatch;
        }
        return false;
    }
    if (prefix.version != kVersion) {
        (void)CloseHandle(file);
        status = LoadStatus::versionMismatch;
        return false;
    }

    LARGE_INTEGER beginning{};
    Header header{};
    complete = nativeSize.QuadPart >= static_cast<LONGLONG>(sizeof(Header))
               && SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) != FALSE
               && read_exact(file, writable_bytes(std::span(&header, 1)));
    if (!complete || !valid_header_shape(header, static_cast<std::uint64_t>(nativeSize.QuadPart))) {
        (void)CloseHandle(file);
        return false;
    }
    if (header.sourceFingerprint != expectedSourceFingerprint) {
        (void)CloseHandle(file);
        status = LoadStatus::sourceMismatch;
        return false;
    }

    std::vector<std::byte> payload;
    std::vector<ScenarioRecord> scenarios;
    std::vector<ActivityRootRecord> roots;
    std::vector<ActivityVariantRecord> variants;
    std::vector<DiskActivityVariantRecord> diskVariants;
    std::vector<std::uint32_t> evidenceTags;
    std::vector<DiskPackageLocator> packageLocators;
    try {
        payload.resize(static_cast<std::size_t>(payload_size(header)));
        scenarios.resize(header.scenarioRecordCount);
        roots.resize(header.activityRootRecordCount);
        variants.resize(header.activityVariantRecordCount);
        diskVariants.resize(header.activityVariantRecordCount);
        evidenceTags.resize(header.evidenceTagRecordCount);
        packageLocators.resize(header.packageLocatorRecordCount);
    } catch (...) {
        (void)CloseHandle(file);
        return false;
    }
    complete = read_exact(file, std::span(payload));
    complete = CloseHandle(file) != FALSE && complete;
    Digest payloadSha256{};
    complete =
        complete
        && middleware::crypto::sha256::hash(std::span<const std::byte>(payload), payloadSha256)
        && payloadSha256 == header.payloadSha256;

    std::size_t offset = 0;
    const DiskGenerationIdentity diskIdentity =
        payload_record<DiskGenerationIdentity>(payload, offset);
    offset += sizeof diskIdentity;
    const SdkIdentity sdk{diskIdentity.sdkBuildSha256, diskIdentity.sdkPayloadSha256};
    complete = complete && diskIdentity.sourceFingerprint == header.sourceFingerprint
               && diskIdentity.sourceFingerprint == expectedSourceFingerprint && valid_sdk(sdk);
    if (complete && expectedSdk != nullptr && sdk != *expectedSdk) {
        status = LoadStatus::sdkMismatch;
        return false;
    }
    const DiskBindingCompleteness diskCompleteness =
        payload_record<DiskBindingCompleteness>(payload, offset);
    offset += sizeof diskCompleteness;
    BindingCompleteness persistedCompleteness{};
    complete = complete && decode_binding_completeness(diskCompleteness, persistedCompleteness);
    for (std::size_t index = 0; complete && index < scenarios.size(); ++index) {
        const DiskScenarioRecord disk = payload_record<DiskScenarioRecord>(payload, offset);
        offset += sizeof disk;
        complete = decode_scenario(disk, scenarios[index]);
    }
    for (std::size_t index = 0; complete && index < roots.size(); ++index) {
        const DiskActivityRootRecord disk = payload_record<DiskActivityRootRecord>(payload, offset);
        offset += sizeof disk;
        complete = decode_activity_root(disk, roots[index]);
    }
    for (std::size_t index = 0; complete && index < diskVariants.size(); ++index) {
        diskVariants[index] = payload_record<DiskActivityVariantRecord>(payload, offset);
        offset += sizeof(DiskActivityVariantRecord);
    }
    for (std::size_t index = 0; complete && index < evidenceTags.size(); ++index) {
        evidenceTags[index] = payload_record<std::uint32_t>(payload, offset);
        offset += sizeof(std::uint32_t);
    }
    for (std::size_t index = 0; complete && index < packageLocators.size(); ++index) {
        packageLocators[index] = payload_record<DiskPackageLocator>(payload, offset);
        offset += sizeof(DiskPackageLocator);
    }
    std::size_t nextEvidenceTag = 0;
    std::size_t nextPackageLocator = 0;
    for (std::size_t index = 0; complete && index < variants.size(); ++index) {
        complete = decode_activity_variant(diskVariants[index],
                                           evidenceTags,
                                           packageLocators,
                                           nextEvidenceTag,
                                           nextPackageLocator,
                                           variants[index]);
    }
    BindingCompleteness measuredCompleteness{};
    complete = complete && offset == payload.size() && nextEvidenceTag == evidenceTags.size()
               && nextPackageLocator == packageLocators.size()
               && valid_catalog(scenarios, roots, variants, &measuredCompleteness)
               && persistedCompleteness == measuredCompleteness;
    if (!complete) {
        return false;
    }

    catalog.sourceFingerprint = header.sourceFingerprint;
    catalog.sdk = sdk;
    catalog.payloadSha256 = header.payloadSha256;
    catalog.records = std::move(scenarios);
    catalog.activityRoots = std::move(roots);
    catalog.activityVariants = std::move(variants);
    catalog.bindingCompleteness = measuredCompleteness;
    status = LoadStatus::loaded;
    return true;
}

} // namespace

/** Loads and validates one explicit generated SDK catalog path. */
bool load(const wchar_t* path,
          const Digest& expectedSourceFingerprint,
          const SdkIdentity& expectedSdk,
          Catalog& catalog,
          LoadStatus& status) noexcept {
    return load_impl(path, expectedSourceFingerprint, &expectedSdk, catalog, status);
}

/** Loads one source-authenticated catalog and returns its payload-owned SDK identity. */
bool load(const wchar_t* path,
          const Digest& expectedSourceFingerprint,
          Catalog& catalog,
          LoadStatus& status) noexcept {
    return load_impl(path, expectedSourceFingerprint, nullptr, catalog, status);
}

/** Retains the scenario-only writer contract while publishing the version-four envelope. */
bool write(const wchar_t* path,
           const Digest& sourceFingerprint,
           const SdkIdentity& sdk,
           std::span<const Record> records) noexcept {
    return write(path,
                 sourceFingerprint,
                 sdk,
                 records,
                 std::span<const ActivityRootRecord>{},
                 std::span<const ActivityVariantRecord>{});
}

/** Atomically publishes every scenario, root binding, and activity variant. */
bool write(const wchar_t* path,
           const Digest& sourceFingerprint,
           const SdkIdentity& sdk,
           std::span<const ScenarioRecord> scenarios,
           std::span<const ActivityRootRecord> activityRoots,
           std::span<const ActivityVariantRecord> activityVariants) noexcept {
    if (path == nullptr || path[0] == L'\0' || !valid_digest(sourceFingerprint) || !valid_sdk(sdk)
        || scenarios.size() > kMaximumScenarioRecords
        || activityRoots.size() > kMaximumActivityRootRecords
        || activityVariants.size() > kMaximumActivityVariantRecords) {
        return false;
    }

    std::vector<ScenarioRecord> canonicalScenarios;
    std::vector<ActivityRootRecord> canonicalRoots;
    std::vector<ActivityVariantRecord> canonicalVariants;
    std::vector<DiskScenarioRecord> diskScenarios;
    std::vector<DiskActivityRootRecord> diskRoots;
    std::vector<DiskActivityVariantRecord> diskVariants;
    std::vector<std::uint32_t> evidenceTags;
    std::vector<DiskPackageLocator> packageLocators;
    try {
        canonicalScenarios.assign(scenarios.begin(), scenarios.end());
        canonicalRoots.assign(activityRoots.begin(), activityRoots.end());
        canonicalVariants.assign(activityVariants.begin(), activityVariants.end());
        diskScenarios.resize(scenarios.size());
        diskRoots.resize(activityRoots.size());
        diskVariants.resize(activityVariants.size());
    } catch (...) {
        return false;
    }
    std::sort(canonicalScenarios.begin(),
              canonicalScenarios.end(),
              [](const ScenarioRecord& row, const ScenarioRecord& other) {
                  return row.scenarioTag < other.scenarioTag;
              });
    std::sort(canonicalRoots.begin(),
              canonicalRoots.end(),
              [](const ActivityRootRecord& row, const ActivityRootRecord& other) {
                  return row.activityRootTag < other.activityRootTag;
              });
    std::sort(canonicalVariants.begin(), canonicalVariants.end(), variant_less);

    std::size_t evidenceTagCount = 0;
    std::size_t packageLocatorCount = 0;
    for (const ActivityVariantRecord& variant : canonicalVariants) {
        const auto account_evidence = [&evidenceTagCount](std::size_t count) {
            if (count > kMaximumEvidenceTags - evidenceTagCount) {
                return false;
            }
            evidenceTagCount += count;
            return true;
        };
        if (!account_evidence(variant.activityRootCandidateTags.size())
            || !account_evidence(variant.scenarioNameCandidateTags.size())
            || !account_evidence(variant.evidenceRootTags.size())
            || variant.locators.size() > kMaximumPackageLocators - packageLocatorCount) {
            return false;
        }
        packageLocatorCount += variant.locators.size();
    }
    try {
        evidenceTags.reserve(evidenceTagCount);
        packageLocators.reserve(packageLocatorCount);
    } catch (...) {
        return false;
    }

    for (std::size_t index = 0; index < canonicalScenarios.size(); ++index) {
        if ((index != 0
             && canonicalScenarios[index - 1U].scenarioTag == canonicalScenarios[index].scenarioTag)
            || !encode_scenario(canonicalScenarios[index], diskScenarios[index])
            || !decode_scenario(diskScenarios[index], canonicalScenarios[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < canonicalRoots.size(); ++index) {
        if ((index != 0
             && canonicalRoots[index - 1U].activityRootTag == canonicalRoots[index].activityRootTag)
            || !encode_activity_root(canonicalRoots[index], diskRoots[index])
            || !decode_activity_root(diskRoots[index], canonicalRoots[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < canonicalVariants.size(); ++index) {
        if ((index != 0 && !variant_less(canonicalVariants[index - 1U], canonicalVariants[index]))
            || !encode_activity_variant(
                canonicalVariants[index], evidenceTags, packageLocators, diskVariants[index])) {
            return false;
        }
    }
    BindingCompleteness bindingCompleteness{};
    if (evidenceTags.size() != evidenceTagCount || packageLocators.size() != packageLocatorCount
        || !valid_catalog(
            canonicalScenarios, canonicalRoots, canonicalVariants, &bindingCompleteness)) {
        return false;
    }
    DiskBindingCompleteness diskCompleteness{};
    if (!encode_binding_completeness(bindingCompleteness, diskCompleteness)) {
        return false;
    }

    std::vector<std::byte> payload;
    try {
        payload.resize(sizeof(DiskGenerationIdentity) + sizeof(DiskBindingCompleteness)
                       + diskScenarios.size() * sizeof(DiskScenarioRecord)
                       + diskRoots.size() * sizeof(DiskActivityRootRecord)
                       + diskVariants.size() * sizeof(DiskActivityVariantRecord)
                       + evidenceTags.size() * sizeof(std::uint32_t)
                       + packageLocators.size() * sizeof(DiskPackageLocator));
    } catch (...) {
        return false;
    }
    std::size_t offset = 0;
    const DiskGenerationIdentity generationIdentity{
        sourceFingerprint, sdk.buildSha256, sdk.payloadSha256};
    append_payload(payload, offset, std::span(&generationIdentity, 1));
    append_payload(payload, offset, std::span<const DiskBindingCompleteness>(&diskCompleteness, 1));
    append_payload(payload, offset, std::span<const DiskScenarioRecord>(diskScenarios));
    append_payload(payload, offset, std::span<const DiskActivityRootRecord>(diskRoots));
    append_payload(payload, offset, std::span<const DiskActivityVariantRecord>(diskVariants));
    append_payload(payload, offset, std::span<const std::uint32_t>(evidenceTags));
    append_payload(payload, offset, std::span<const DiskPackageLocator>(packageLocators));
    if (offset != payload.size()) {
        return false;
    }

    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.headerSize = sizeof(Header);
    header.fileSize = sizeof(Header) + payload.size();
    header.scenarioRecordSize = sizeof(DiskScenarioRecord);
    header.scenarioRecordCount = static_cast<std::uint32_t>(diskScenarios.size());
    header.activityRootRecordSize = sizeof(DiskActivityRootRecord);
    header.activityRootRecordCount = static_cast<std::uint32_t>(diskRoots.size());
    header.activityVariantRecordSize = sizeof(DiskActivityVariantRecord);
    header.activityVariantRecordCount = static_cast<std::uint32_t>(diskVariants.size());
    header.evidenceTagRecordSize = sizeof(std::uint32_t);
    header.evidenceTagRecordCount = static_cast<std::uint32_t>(evidenceTags.size());
    header.packageLocatorRecordSize = sizeof(DiskPackageLocator);
    header.packageLocatorRecordCount = static_cast<std::uint32_t>(packageLocators.size());
    header.sourceFingerprint = sourceFingerprint;
    if (!middleware::crypto::sha256::hash(std::span<const std::byte>(payload),
                                          header.payloadSha256)) {
        return false;
    }

    core::path::remove_stale_siblings(path);
    core::path::Buffer temporaryPath;
    const HANDLE file = open_temporary(path, temporaryPath);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool complete = write_exact(file, bytes_of(header))
                    && write_exact(file, std::span<const std::byte>(payload));
    complete = complete && FlushFileBuffers(file) != FALSE;
    complete = CloseHandle(file) != FALSE && complete;
    if (complete) {
        complete = core::path::publish_sibling(temporaryPath.chars.data(), path);
    }
    if (complete) {
        Catalog reopened{};
        LoadStatus status = LoadStatus::invalid;
        complete =
            load(path, sourceFingerprint, sdk, reopened, status) && status == LoadStatus::loaded;
    }
    if (!complete) {
        (void)DeleteFileW(temporaryPath.chars.data());
    }
    return complete;
}

} // namespace sunrise::state::activity_sdk::generated_world::manifest
