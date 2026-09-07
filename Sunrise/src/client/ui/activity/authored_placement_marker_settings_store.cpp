/**
 * Persistent presentation for package-authored world markers. Session/catalog identities remain
 * in authored_placement_marker and are deliberately never serialized.
 */

#include "authored_placement_marker_settings_store.h"

#include <Windows.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "../../../core/filesystem/path.h"
#include "../../../core/logging/log.h"

namespace sunrise::client::ui::activity::authored_placement_marker::settings_store {
namespace {

// Marker presentation is stored in this file below the artifact root.
constexpr std::wstring_view kFileSuffix = L"\\world_markers.json";
// Fixed read and token buffers; a larger file or token is refused.
constexpr std::size_t kFileCapacity = 4'096;
constexpr std::size_t kScalarCapacity = 64;

SRWLOCK g_lock{SRWLOCK_INIT};
Options g_options{};
core::path::Buffer g_path{};
bool g_pathResolved{};

/** @return True when one colour is finite and bounded for both ImGui and D3D conversion. */
[[nodiscard]] bool valid_color(const MarkerColor& color) noexcept {
    for (const float channel : color) {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
            return false;
        }
    }
    return true;
}

/** @return True when every persisted presentation field is in its renderer-safe domain. */
[[nodiscard]] bool valid(const Options& options) noexcept {
    const auto scope = static_cast<std::uint8_t>(options.displayScope);
    const auto glyph = static_cast<std::uint8_t>(options.worldGlyph);
    return scope <= static_cast<std::uint8_t>(DisplayScope::nearbyRows)
           && glyph <= static_cast<std::uint8_t>(WorldGlyph::diagnosticSphere)
           && std::isfinite(options.nearbyRadius) && options.nearbyRadius >= kMinimumNearbyRadius
           && options.nearbyRadius <= kMaximumNearbyRadius && std::isfinite(options.worldGlyphSize)
           && options.worldGlyphSize >= kMinimumWorldGlyphSize
           && options.worldGlyphSize <= kMaximumWorldGlyphSize
           && std::isfinite(options.worldLineWidth)
           && options.worldLineWidth >= kMinimumWorldLineWidth
           && options.worldLineWidth <= kMaximumWorldLineWidth
           && valid_color(options.sourceColors.authoredPlacement)
           && valid_color(options.sourceColors.containerPlacement)
           && valid_color(options.sourceColors.triggerVolume)
           && valid_color(options.sourceColors.type23Placement)
           && valid_color(options.sourceColors.embeddedPlacement)
           && valid_color(options.sourceColors.sdkSquadAnchor);
}

/** Emits one compact store failure without exposing the resolved installation path. */
void report_fail(const char* reason) noexcept {
    std::array<char, 112> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=world_markers stage=store result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Removes JSON whitespace around one scalar token. */
[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty()
           && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'
               || value.front() == '\n')) {
        value.remove_prefix(1);
    }
    while (!value.empty()
           && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'
               || value.back() == '\n')) {
        value.remove_suffix(1);
    }
    return value;
}

/** Finds one key's raw scalar token. */
[[nodiscard]] bool
scalar_for(std::string_view text, std::string_view key, std::string_view& output) noexcept {
    const std::size_t at = text.find(key);
    if (at == std::string_view::npos) {
        return false;
    }
    const std::size_t colon = text.find(':', at + key.size());
    if (colon == std::string_view::npos) {
        return false;
    }
    std::size_t end = colon + 1;
    while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != '\n'
           && text[end] != '\r') {
        ++end;
    }
    output = trim(text.substr(colon + 1, end - colon - 1));
    return !output.empty();
}

/** Parses one finite float token without accepting a valid prefix followed by junk. */
[[nodiscard]] bool parse_float(std::string_view value, float& output) noexcept {
    value = trim(value);
    if (value.empty() || value.size() >= kScalarCapacity) {
        return false;
    }
    std::array<char, kScalarCapacity> buffer{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        buffer[index] = value[index];
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(buffer.data(), &end);
    if (end == buffer.data() || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    output = parsed;
    return true;
}

/** Reads one strict JSON boolean, preserving the existing value when malformed. */
void boolean_for(std::string_view text, std::string_view key, bool& output) noexcept {
    std::string_view scalar;
    if (!scalar_for(text, key, scalar)) {
        return;
    }
    if (scalar == "true") {
        output = true;
    } else if (scalar == "false") {
        output = false;
    }
}

/** Reads one bounded float, preserving the existing value when missing or malformed. */
void bounded_float_for(std::string_view text,
                       std::string_view key,
                       float minimum,
                       float maximum,
                       float& output) noexcept {
    std::string_view scalar;
    float parsed = 0.0F;
    if (scalar_for(text, key, scalar) && parse_float(scalar, parsed) && parsed >= minimum
        && parsed <= maximum) {
        output = parsed;
    }
}

/** Reads one four-lane colour array atomically, preserving the default if any lane is bad. */
void color_for(std::string_view text, std::string_view key, MarkerColor& output) noexcept {
    const std::size_t at = text.find(key);
    if (at == std::string_view::npos) {
        return;
    }
    const std::size_t colon = text.find(':', at + key.size());
    if (colon == std::string_view::npos) {
        return;
    }
    std::size_t open = colon + 1;
    while (
        open < text.size()
        && (text[open] == ' ' || text[open] == '\t' || text[open] == '\r' || text[open] == '\n')) {
        ++open;
    }
    if (open == text.size() || text[open] != '[') {
        return;
    }
    MarkerColor parsed{};
    std::size_t cursor = open + 1;
    for (std::size_t lane = 0; lane < parsed.size(); ++lane) {
        const char separator = lane + 1 == parsed.size() ? ']' : ',';
        const std::size_t end = text.find(separator, cursor);
        if (end == std::string_view::npos
            || !parse_float(text.substr(cursor, end - cursor), parsed[lane]) || parsed[lane] < 0.0F
            || parsed[lane] > 1.0F) {
            return;
        }
        cursor = end + 1;
    }
    output = parsed;
}

/** Layers one settings document over stable built-in defaults. */
void parse(std::string_view text, Options& output) noexcept {
    boolean_for(text, "\"enabled\"", output.enabled);
    boolean_for(text, "\"always_label\"", output.alwaysShowLabels);
    boolean_for(text, "\"invert_x\"", output.invertX);
    boolean_for(text, "\"invert_y\"", output.invertY);
    boolean_for(text, "\"only_renderable_objects\"", output.onlyRenderableObjects);

    std::string_view scalar;
    if (scalar_for(text, "\"display_scope\"", scalar)) {
        if (scalar == "\"selected\"") {
            output.displayScope = DisplayScope::selectedRows;
        } else if (scalar == "\"visible\"") {
            output.displayScope = DisplayScope::publishedRows;
        } else if (scalar == "\"nearby\"") {
            output.displayScope = DisplayScope::nearbyRows;
        }
    }
    if (scalar_for(text, "\"world_glyph\"", scalar)) {
        if (scalar == "\"point\"") {
            output.worldGlyph = WorldGlyph::point;
        } else if (scalar == "\"axes\"") {
            output.worldGlyph = WorldGlyph::axes;
        } else if (scalar == "\"box\"") {
            output.worldGlyph = WorldGlyph::diagnosticBox;
        } else if (scalar == "\"sphere\"") {
            output.worldGlyph = WorldGlyph::diagnosticSphere;
        }
    }
    bounded_float_for(
        text, "\"nearby_radius\"", kMinimumNearbyRadius, kMaximumNearbyRadius, output.nearbyRadius);
    bounded_float_for(text,
                      "\"glyph_size\"",
                      kMinimumWorldGlyphSize,
                      kMaximumWorldGlyphSize,
                      output.worldGlyphSize);
    bounded_float_for(text,
                      "\"line_width\"",
                      kMinimumWorldLineWidth,
                      kMaximumWorldLineWidth,
                      output.worldLineWidth);
    color_for(text, "\"object_color\"", output.sourceColors.embeddedPlacement);
    color_for(text, "\"device_color\"", output.sourceColors.type23Placement);
    color_for(text, "\"trigger_color\"", output.sourceColors.triggerVolume);
    color_for(text, "\"scenario_color\"", output.sourceColors.authoredPlacement);
    color_for(text, "\"container_color\"", output.sourceColors.containerPlacement);
    color_for(text, "\"sdk_squad_color\"", output.sourceColors.sdkSquadAnchor);
}

/** @return The settings-file name of one display scope. */
[[nodiscard]] const char* display_scope_name(DisplayScope scope) noexcept {
    switch (scope) {
    case DisplayScope::selectedRows:
        return "selected";
    case DisplayScope::publishedRows:
        return "visible";
    case DisplayScope::nearbyRows:
        return "nearby";
    }
    return "selected";
}

/** @return The settings-file name of one world glyph. */
[[nodiscard]] const char* world_glyph_name(WorldGlyph glyph) noexcept {
    switch (glyph) {
    case WorldGlyph::point:
        return "point";
    case WorldGlyph::axes:
        return "axes";
    case WorldGlyph::diagnosticBox:
        return "box";
    case WorldGlyph::diagnosticSphere:
        return "sphere";
    }
    return "axes";
}

/** Writes the complete, small presentation document. */
[[nodiscard]] bool store(const Options& options) noexcept {
    if (!g_pathResolved) {
        return false;
    }
    const MarkerColor& object = options.sourceColors.embeddedPlacement;
    const MarkerColor& device = options.sourceColors.type23Placement;
    const MarkerColor& trigger = options.sourceColors.triggerVolume;
    const MarkerColor& scenario = options.sourceColors.authoredPlacement;
    const MarkerColor& container = options.sourceColors.containerPlacement;
    const MarkerColor& sdkSquad = options.sourceColors.sdkSquadAnchor;
    std::array<char, kFileCapacity> document{};
    const int size = std::snprintf(document.data(),
                                   document.size(),
                                   "{\n"
                                   "  \"enabled\": %s,\n"
                                   "  \"display_scope\": \"%s\",\n"
                                   "  \"nearby_radius\": %.3f,\n"
                                   "  \"world_glyph\": \"%s\",\n"
                                   "  \"glyph_size\": %.3f,\n"
                                   "  \"line_width\": %.3f,\n"
                                   "  \"always_label\": %s,\n"
                                   "  \"invert_x\": %s,\n"
                                   "  \"invert_y\": %s,\n"
                                   "  \"only_renderable_objects\": %s,\n"
                                   "  \"object_color\": [%.4f, %.4f, %.4f, %.4f],\n"
                                   "  \"device_color\": [%.4f, %.4f, %.4f, %.4f],\n"
                                   "  \"trigger_color\": [%.4f, %.4f, %.4f, %.4f],\n"
                                   "  \"scenario_color\": [%.4f, %.4f, %.4f, %.4f],\n"
                                   "  \"container_color\": [%.4f, %.4f, %.4f, %.4f],\n"
                                   "  \"sdk_squad_color\": [%.4f, %.4f, %.4f, %.4f]\n"
                                   "}\n",
                                   options.enabled ? "true" : "false",
                                   display_scope_name(options.displayScope),
                                   static_cast<double>(options.nearbyRadius),
                                   world_glyph_name(options.worldGlyph),
                                   static_cast<double>(options.worldGlyphSize),
                                   static_cast<double>(options.worldLineWidth),
                                   options.alwaysShowLabels ? "true" : "false",
                                   options.invertX ? "true" : "false",
                                   options.invertY ? "true" : "false",
                                   options.onlyRenderableObjects ? "true" : "false",
                                   static_cast<double>(object[0]),
                                   static_cast<double>(object[1]),
                                   static_cast<double>(object[2]),
                                   static_cast<double>(object[3]),
                                   static_cast<double>(device[0]),
                                   static_cast<double>(device[1]),
                                   static_cast<double>(device[2]),
                                   static_cast<double>(device[3]),
                                   static_cast<double>(trigger[0]),
                                   static_cast<double>(trigger[1]),
                                   static_cast<double>(trigger[2]),
                                   static_cast<double>(trigger[3]),
                                   static_cast<double>(scenario[0]),
                                   static_cast<double>(scenario[1]),
                                   static_cast<double>(scenario[2]),
                                   static_cast<double>(scenario[3]),
                                   static_cast<double>(container[0]),
                                   static_cast<double>(container[1]),
                                   static_cast<double>(container[2]),
                                   static_cast<double>(container[3]),
                                   static_cast<double>(sdkSquad[0]),
                                   static_cast<double>(sdkSquad[1]),
                                   static_cast<double>(sdkSquad[2]),
                                   static_cast<double>(sdkSquad[3]));
    if (size <= 0 || static_cast<std::size_t>(size) >= document.size()) {
        return false;
    }
    const HANDLE file = CreateFileW(g_path.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool complete =
        WriteFile(file, document.data(), static_cast<DWORD>(size), &written, nullptr) != FALSE
        && written == static_cast<DWORD>(size);
    complete = CloseHandle(file) != FALSE && complete;
    return complete;
}

/** Reads the settings file when it exists; absent and malformed members keep defaults. */
void load() noexcept {
    const HANDLE file = CreateFileW(g_path.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart <= 0
        || size.QuadPart >= static_cast<LONGLONG>(kFileCapacity)) {
        (void)CloseHandle(file);
        report_fail("size");
        return;
    }
    std::array<char, kFileCapacity> document{};
    DWORD read = 0;
    const DWORD expected = static_cast<DWORD>(size.QuadPart);
    const bool readOk = ReadFile(file, document.data(), expected, &read, nullptr) != FALSE;
    (void)CloseHandle(file);
    if (!readOk || read != expected) {
        report_fail("read");
        return;
    }
    Options parsed{};
    parse(std::string_view(document.data(), read), parsed);
    if (!valid(parsed)) {
        report_fail("range");
        return;
    }
    g_options = parsed;
}

} // namespace

/** Resolves the store's file path and loads any options already written. */
void initialize(void* module) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_options = Options{};
    g_pathResolved =
        core::path::artifact_directory(module, g_path) && core::path::append(g_path, kFileSuffix);
    if (g_pathResolved) {
        load();
    } else {
        report_fail("path");
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_options = Options{};
    g_path = core::path::Buffer{};
    g_pathResolved = false;
    ReleaseSRWLockExclusive(&g_lock);
}

Options get() noexcept {
    AcquireSRWLockShared(&g_lock);
    const Options snapshot = g_options;
    ReleaseSRWLockShared(&g_lock);
    return snapshot;
}

/** Publishes one validated option set and writes it back. @return False when invalid. */
bool publish(const Options& options) noexcept {
    if (!valid(options)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_options = options;
    const bool stored = store(options);
    ReleaseSRWLockExclusive(&g_lock);
    if (!stored) {
        report_fail("write");
    }
    return true;
}

} // namespace sunrise::client::ui::activity::authored_placement_marker::settings_store
