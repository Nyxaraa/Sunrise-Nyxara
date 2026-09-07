#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "../../../core/filesystem/path.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "../items/packages/internal.h"
#include "activity_sdk_generation_worker.h"
#include "scriptable_catalog_builder.h"
#include "scriptable_catalog_worker.h"

namespace sunrise::client::content::activity::scriptables {
namespace {

namespace catalog = state::build_data::scriptables;
namespace package_reader = middleware::content::packages::reader;

/** Delay before one failed package-reader start may be tried again. */
constexpr ULONGLONG kStartRetryDelayMs = 1'000;

/** One immutable selected-scenario request copied into worker-owned storage. */
struct Request final {
    std::uint64_t serial{};
    std::uint32_t scenarioTag{};
    std::array<char, catalog::kScenarioNameCapacity> name{};
    std::uint8_t nameLength{};
};

/** Inputs whose lifetime spans one selected-scenario package-reader thread. */
struct Work final {
    Request request{};
    package_reader::BlockKeys keys{};
    core::path::Buffer directory{};
    bool packageReady{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
bool g_accepting{};
Request g_requested{};
std::uint64_t g_started{};
std::uint64_t g_retrySerial{};
ULONGLONG g_retryAfter{};
HANDLE g_thread{};
std::atomic_bool g_cancel{};
std::atomic<std::uint64_t> g_revision{1};

[[nodiscard]] bool cancelled() noexcept {
    return g_cancel.load(std::memory_order_relaxed);
}

/** Replaces one bounded panel detail string. */
void set_detail(catalog::Snapshot& output, const char* detail) noexcept {
    output.detail = {};
    if (detail != nullptr) {
        (void)std::snprintf(output.detail.data(), output.detail.size(), "%s", detail);
    }
}

/** Applies one request identity to a worker result. */
void copy_request(const Request& request, catalog::Snapshot& output) noexcept {
    output.request = request.serial;
    output.scenarioTag = request.scenarioTag;
    output.scenarioName = request.name;
    output.scenarioNameLength = request.nameLength;
}

/** Builds one immutable status snapshot without publishing it. */
[[nodiscard]] std::shared_ptr<catalog::Snapshot>
status_snapshot(const Request& request, catalog::BuildStatus status, const char* detail) noexcept {
    try {
        auto output = std::make_shared<catalog::Snapshot>();
        copy_request(request, *output);
        output->revision = g_revision.fetch_add(1, std::memory_order_relaxed);
        output->status = status;
        set_detail(*output, detail);
        return output;
    } catch (...) {
        return {};
    }
}

/** Publishes a completed build only while its exact request still owns the worker. */
void publish_if_current(std::uint64_t serial, std::shared_ptr<catalog::Snapshot> output) noexcept {
    if (output == nullptr) {
        return;
    }
    AcquireSRWLockShared(&g_lock);
    if (g_accepting && g_requested.serial == serial && !cancelled()) {
        catalog::publish(std::move(output));
    }
    ReleaseSRWLockShared(&g_lock);
}

/** Leaves one current request eligible for a later package-read retry. */
void schedule_worker_retry(std::uint64_t serial) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_accepting && g_requested.serial == serial && !cancelled()) {
        g_started = 0;
        g_retrySerial = serial;
        g_retryAfter = GetTickCount64() + kStartRetryDelayMs;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Tests whether the same cache phase is already visible for this request. */
[[nodiscard]] bool cache_phase_visible(const Request& request,
                                       catalog::BuildCoverage coverage) noexcept {
    const catalog::SnapshotView current = catalog::snapshot();
    return current != nullptr && current->request == request.serial
           && current->scenarioTag == request.scenarioTag
           && current->status == catalog::BuildStatus::ready && current->coverage == coverage;
}

/** Owns one package-reader work item until cache load and enrichment finish. */
DWORD WINAPI thread_main(void* opaque) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(opaque));
    std::shared_ptr<catalog::Snapshot> output{};
    std::shared_ptr<catalog::Snapshot> cached{};
    try {
        if (sdk_generation::load_cached_scenario(
                work->request.scenarioTag,
                std::string_view(work->request.name.data(), work->request.nameLength),
                cached)) {
            copy_request(work->request, *cached);
            cached->revision = g_revision.fetch_add(1, std::memory_order_relaxed);
            const bool fullCache = cached->coverage == catalog::BuildCoverage::full;
            set_detail(*cached,
                       fullCache ? "generated SDK full cache" : "generated SDK core cache");
            if (!cache_phase_visible(work->request, cached->coverage)) {
                publish_if_current(work->request.serial, cached);
            }
            if (fullCache) {
                SecureZeroMemory(&work->keys, sizeof work->keys);
                return 0;
            }
        }

        if (!work->packageReady) {
            if (cached == nullptr) {
                publish_if_current(work->request.serial,
                                   status_snapshot(work->request,
                                                   catalog::BuildStatus::queued,
                                                   "waiting for package keys or directory"));
            }
            SecureZeroMemory(&work->keys, sizeof work->keys);
            schedule_worker_retry(work->request.serial);
            return 0;
        }

        if (cached == nullptr) {
            publish_if_current(work->request.serial,
                               status_snapshot(work->request,
                                               catalog::BuildStatus::building,
                                               "reading installed packages"));
        }

        const package_reader::Source source{
            std::wstring_view(work->directory.chars.data(), work->directory.length), &work->keys};
        internal::ContainerIndex containers{};
        if (!internal::build_container_index(source, containers, &cancelled)) {
            containers = {};
        }
        auto scratch = std::make_unique<package_reader::Scratch>();
        output = internal::build_scenario_catalog(
            source,
            containers,
            *scratch,
            work->request.scenarioTag,
            std::string_view(work->request.name.data(), work->request.nameLength),
            &cancelled);
        if (output && output->status == catalog::BuildStatus::failed && cached != nullptr
            && !cancelled()) {
            output.reset();
        }
        if (output != nullptr) {
            copy_request(work->request, *output);
            output->revision = g_revision.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (...) {
        if (cached == nullptr) {
            output = status_snapshot(
                work->request, catalog::BuildStatus::failed, "unexpected extraction exception");
        } else {
            output.reset();
        }
    }
    SecureZeroMemory(&work->keys, sizeof work->keys);
    if (output != nullptr) {
        publish_if_current(work->request.serial, std::move(output));
    }
    return 0;
}

/** Publishes one bounded start failure for the current request. */
void report_start_failure(const Request& request, const char* detail) noexcept {
    publish_if_current(request.serial,
                       status_snapshot(request, catalog::BuildStatus::failed, detail));
}

/** Leaves a failed start eligible for one later retry after a fixed delay. */
void schedule_start_retry(const Request& request) noexcept {
    g_started = 0;
    g_retrySerial = request.serial;
    g_retryAfter = GetTickCount64() + kStartRetryDelayMs;
}

} // namespace

/** Allows selected-scenario requests after all owning State is ready. */
void activate() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_accepting = true;
    g_cancel.store(false, std::memory_order_relaxed);
    g_retrySerial = 0;
    g_retryAfter = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Replaces the selected scenario request without blocking on package IO. */
bool request(std::uint32_t scenarioTag, std::string_view scenarioName, bool force) noexcept {
    if (scenarioTag == 0 || scenarioName.empty()
        || scenarioName.size() >= catalog::kScenarioNameCapacity) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_accepting) {
        const bool same =
            g_requested.scenarioTag == scenarioTag && g_requested.nameLength == scenarioName.size()
            && std::equal(scenarioName.begin(), scenarioName.end(), g_requested.name.begin());
        if (!same || force) {
            Request requested{};
            requested.serial = g_requested.serial + 1;
            requested.scenarioTag = scenarioTag;
            requested.nameLength = static_cast<std::uint8_t>(scenarioName.size());
            std::copy(scenarioName.begin(), scenarioName.end(), requested.name.begin());
            g_requested = requested;
            g_retrySerial = 0;
            g_retryAfter = 0;
            if (g_thread != nullptr) {
                g_cancel.store(true, std::memory_order_relaxed);
            }
            catalog::publish(status_snapshot(
                requested, catalog::BuildStatus::queued, "waiting for package reader"));
        }
    }
    const bool accepted = g_accepting;
    ReleaseSRWLockExclusive(&g_lock);
    return accepted;
}

/** Reaps the old reader and starts the newest queued request. */
void service() noexcept {
    Request requested{};
    AcquireSRWLockExclusive(&g_lock);
    if (g_thread != nullptr && WaitForSingleObject(g_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    const ULONGLONG now = GetTickCount64();
    const bool retryWaiting = g_retrySerial == g_requested.serial && now < g_retryAfter;
    if (!g_accepting || g_thread != nullptr || g_requested.serial == 0
        || g_started == g_requested.serial || retryWaiting) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    requested = g_requested;
    g_started = requested.serial;
    g_cancel.store(false, std::memory_order_relaxed);
    auto* work = new (std::nothrow) Work();
    if (work == nullptr) {
        schedule_start_retry(requested);
        ReleaseSRWLockExclusive(&g_lock);
        report_start_failure(requested, "package reader allocation failed");
        return;
    }
    work->request = requested;
    work->packageReady = items::packages::collect_keys(work->keys)
                         && items::packages::package_directory(work->directory);
    if (!work->packageReady) {
        SecureZeroMemory(&work->keys, sizeof work->keys);
    }
    g_thread = CreateThread(nullptr, 0, &thread_main, work, 0, nullptr);
    if (g_thread == nullptr) {
        SecureZeroMemory(&work->keys, sizeof work->keys);
        delete work;
        schedule_start_retry(requested);
        ReleaseSRWLockExclusive(&g_lock);
        report_start_failure(requested, "package reader thread did not start");
        return;
    }
    g_retrySerial = 0;
    g_retryAfter = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Cancels and joins the reader before withdrawing its transient snapshot. */
void reset() noexcept {
    HANDLE thread = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    g_accepting = false;
    g_cancel.store(true, std::memory_order_relaxed);
    thread = g_thread;
    ReleaseSRWLockExclusive(&g_lock);
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_thread != nullptr) {
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    g_requested = {};
    g_started = 0;
    g_retrySerial = 0;
    g_retryAfter = 0;
    ReleaseSRWLockExclusive(&g_lock);
    catalog::clear();
}

} // namespace sunrise::client::content::activity::scriptables
