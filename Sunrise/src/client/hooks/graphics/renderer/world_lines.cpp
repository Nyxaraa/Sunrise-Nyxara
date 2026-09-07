#include "world_lines.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d11_1.h>
#include <d3dcompiler.h>

namespace sunrise::client::hooks::graphics::renderer::world_lines {
namespace {

/** One frame's fixed vertex and primitive budget; the writer drops anything past it. */
constexpr std::size_t kMaximumVertices = 32'768;
constexpr std::size_t kMaximumPrimitiveInputs = 4'096;
/** Segment range a sphere is drawn with, clamped so a caller cannot exhaust the budget. */
constexpr std::uint16_t kMinimumSphereSegments = 8;
constexpr std::uint16_t kMaximumSphereSegments = 64;
/** A direction shorter than this has no usable normal, so the segment is skipped. */
constexpr float kLengthEpsilon = 0.000001F;
/** Pi at float precision, for the sphere and circle sweeps. */
constexpr float kPi = 3.14159265358979323846F;
/** Line width range in pixels; the geometry shader expands to this thickness. */
constexpr float kMinimumLineWidth = 1.0F;
constexpr float kMaximumLineWidth = 16.0F;

/** Vertex stage: camera-relative expansion input for the line geometry stage. */
constexpr char kVertexShader[] = R"(
cbuffer CameraConstants : register(b0)
{
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
    float4 projection;
    float4 stroke;
};

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    const float3 relative = input.position - camera_position.xyz;
    const float depth = dot(relative, camera_forward.xyz);
    const float horizontal = dot(relative, camera_right.xyz) * projection.x * projection.z;
    const float vertical = dot(relative, camera_up.xyz) * projection.y * projection.w;
    output.position = float4(horizontal, vertical, 0.0f, depth);
    output.color = input.color;
    return output;
}
)";

/** Geometry stage: expands each line into a screen-facing quad of the requested width. */
constexpr char kGeometryShader[] = R"(
cbuffer CameraConstants : register(b0)
{
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
    float4 projection;
    float4 stroke;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

[maxvertexcount(4)]
void main(line VertexOutput input[2], inout TriangleStream<VertexOutput> output)
{
    const float minimum_depth = 0.001f;
    VertexOutput first = input[0];
    VertexOutput second = input[1];
    if (first.position.w < minimum_depth && second.position.w < minimum_depth) {
        return;
    }
    if (first.position.w < minimum_depth) {
        const float amount =
            (minimum_depth - first.position.w) / (second.position.w - first.position.w);
        first.position = lerp(first.position, second.position, amount);
        first.color = lerp(first.color, second.color, amount);
    } else if (second.position.w < minimum_depth) {
        const float amount =
            (minimum_depth - second.position.w) / (first.position.w - second.position.w);
        second.position = lerp(second.position, first.position, amount);
        second.color = lerp(second.color, first.color, amount);
    }
    first.position.z = 0.0f;
    second.position.z = 0.0f;
    const float2 first_ndc = first.position.xy / first.position.w;
    const float2 second_ndc = second.position.xy / second.position.w;
    const float2 pixel_delta = (second_ndc - first_ndc) / stroke.xy;
    const float length_squared = dot(pixel_delta, pixel_delta);
    if (length_squared <= 0.0001f) {
        return;
    }
    const float2 direction = pixel_delta * rsqrt(length_squared);
    const float2 offset_ndc = float2(-direction.y, direction.x) * stroke.z * stroke.xy;

    VertexOutput vertex;
    vertex = first;
    vertex.position.xy -= offset_ndc * vertex.position.w;
    output.Append(vertex);
    vertex = first;
    vertex.position.xy += offset_ndc * vertex.position.w;
    output.Append(vertex);
    vertex = second;
    vertex.position.xy -= offset_ndc * vertex.position.w;
    output.Append(vertex);
    vertex = second;
    vertex.position.xy += offset_ndc * vertex.position.w;
    output.Append(vertex);
    output.RestartStrip();
}
)";

/** Pixel stage: writes the vertex colour with no lighting or depth test of its own. */
constexpr char kPixelShader[] = R"(
struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET
{
    return input.color;
}
)";

/** One world vertex consumed by the private line shaders. */
struct Vertex final {
    Vector3 position{};
    std::uint32_t color{};
};

/** Camera basis and projection factors consumed by the vertex shader. */
struct alignas(16) CameraConstants final {
    std::array<float, 4> position{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> forward{};
    std::array<float, 4> projection{};
    std::array<float, 4> stroke{};
};

static_assert(sizeof(CameraConstants) % 16 == 0);

/** Device objects owned only by the private world-line pass. */
struct Resources final {
    ID3D11Device* owner{};
    ID3D11VertexShader* vertexShader{};
    ID3D11GeometryShader* geometryShader{};
    ID3D11PixelShader* pixelShader{};
    ID3D11InputLayout* inputLayout{};
    ID3D11Buffer* vertexBuffer{};
    ID3D11Buffer* constantBuffer{};
    ID3D11BlendState* blendState{};
    ID3D11DepthStencilState* depthState{};
    ID3D11RasterizerState* rasterizerState{};
    ID3DDeviceContextState* contextState{};
    bool failed{};
};

Resources g_resources{};
std::array<Vertex, kMaximumVertices> g_vertices{};

/** Releases and clears one owned COM pointer. */
template <typename Interface> void release_com(Interface*& object) noexcept {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

/** Releases the device objects without changing the remembered device identity. */
void release_objects() noexcept {
    release_com(g_resources.contextState);
    release_com(g_resources.rasterizerState);
    release_com(g_resources.depthState);
    release_com(g_resources.blendState);
    release_com(g_resources.constantBuffer);
    release_com(g_resources.vertexBuffer);
    release_com(g_resources.inputLayout);
    release_com(g_resources.pixelShader);
    release_com(g_resources.geometryShader);
    release_com(g_resources.vertexShader);
}

/** @return True when every vector lane is finite. */
[[nodiscard]] bool finite(const Vector3& value) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

/** @return Dot product of two vectors. */
[[nodiscard]] float dot(const Vector3& left, const Vector3& right) noexcept {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

/** @return The right-handed cross product. */
[[nodiscard]] Vector3 cross(const Vector3& left, const Vector3& right) noexcept {
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

/** Normalizes one finite vector. */
[[nodiscard]] bool normalize(Vector3& value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kLengthEpsilon) {
        return false;
    }
    const float inverse = 1.0F / std::sqrt(lengthSquared);
    for (float& lane : value) {
        lane *= inverse;
    }
    return true;
}

/** Builds the exact camera basis used by the existing marker projection. */
[[nodiscard]] bool camera_constants(const teleport::CameraPose& camera,
                                    bool invertX,
                                    bool invertY,
                                    CameraConstants& output) noexcept {
    output = {};
    if (!finite(camera.position) || !finite(camera.forward) || !finite(camera.up)
        || !std::isfinite(camera.horizontalFov) || !std::isfinite(camera.aspect)
        || camera.horizontalFov <= 0.0F || camera.horizontalFov >= kPi || camera.aspect <= 0.0F) {
        return false;
    }
    Vector3 forward = camera.forward;
    Vector3 up = camera.up;
    if (!normalize(forward)) {
        return false;
    }
    const float upAlongForward = dot(up, forward);
    for (std::size_t lane = 0; lane < up.size(); ++lane) {
        up[lane] -= forward[lane] * upAlongForward;
    }
    if (!normalize(up)) {
        return false;
    }
    Vector3 right = cross(forward, up);
    if (!normalize(right)) {
        return false;
    }
    const float tangent = std::tan(camera.horizontalFov * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return false;
    }
    output.position = {camera.position[0], camera.position[1], camera.position[2], 0.0F};
    output.right = {right[0], right[1], right[2], 0.0F};
    output.up = {up[0], up[1], up[2], 0.0F};
    output.forward = {forward[0], forward[1], forward[2], 0.0F};
    output.projection = {
        1.0F / tangent, camera.aspect / tangent, invertX ? -1.0F : 1.0F, invertY ? -1.0F : 1.0F};
    return std::isfinite(output.projection[0]) && std::isfinite(output.projection[1]);
}

/** Packs RGBA bytes for a DXGI_FORMAT_R8G8B8A8_UNORM input. */
[[nodiscard]] constexpr std::uint32_t pack(Color color) noexcept {
    return static_cast<std::uint32_t>(color.red) | (static_cast<std::uint32_t>(color.green) << 8U)
           | (static_cast<std::uint32_t>(color.blue) << 16U)
           | (static_cast<std::uint32_t>(color.alpha) << 24U);
}

/** Bounded writer for the one line-list vertex buffer. */
class Writer final {
public:
    /** Adds one finite edge, or records that the fixed vertex buffer is full. */
    void line(const Vector3& first, const Vector3& second, Color color) noexcept {
        if (!finite(first) || !finite(second)) {
            return;
        }
        if (count_ > g_vertices.size() - 2) {
            truncated_ = true;
            return;
        }
        const std::uint32_t packed = pack(color);
        g_vertices[count_++] = {first, packed};
        g_vertices[count_++] = {second, packed};
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }
    [[nodiscard]] bool truncated() const noexcept {
        return truncated_;
    }

    /** Records that input was omitted by one of the fixed work limits. */
    void mark_truncated() noexcept {
        truncated_ = true;
    }

private:
    std::size_t count_{};
    bool truncated_{};
};

/** Adds three centred lines for one position marker. */
void append_point(Writer& writer, const Point& point) noexcept {
    if (!finite(point.centre) || !std::isfinite(point.halfExtent) || point.halfExtent <= 0.0F) {
        return;
    }
    for (std::size_t lane = 0; lane < point.centre.size(); ++lane) {
        Vector3 first = point.centre;
        Vector3 second = point.centre;
        first[lane] -= point.halfExtent;
        second[lane] += point.halfExtent;
        writer.line(first, second, point.color);
    }
}

/** Adds three positive RGB axes for one position marker. */
void append_axes(Writer& writer, const Axes& axes) noexcept {
    if (!finite(axes.origin) || !std::isfinite(axes.extent) || axes.extent <= 0.0F) {
        return;
    }
    for (std::size_t lane = 0; lane < axes.origin.size(); ++lane) {
        Vector3 end = axes.origin;
        end[lane] += axes.extent;
        writer.line(axes.origin, end, axes.color);
    }
}

/** Adds the twelve edges of one ordered AABB. */
void append_box(Writer& writer, const Box& box) noexcept {
    if (!finite(box.minimum) || !finite(box.maximum)) {
        return;
    }
    for (std::size_t lane = 0; lane < box.minimum.size(); ++lane) {
        if (box.minimum[lane] > box.maximum[lane]) {
            return;
        }
    }
    const std::array<Vector3, 8> corners{{
        {box.minimum[0], box.minimum[1], box.minimum[2]},
        {box.maximum[0], box.minimum[1], box.minimum[2]},
        {box.maximum[0], box.maximum[1], box.minimum[2]},
        {box.minimum[0], box.maximum[1], box.minimum[2]},
        {box.minimum[0], box.minimum[1], box.maximum[2]},
        {box.maximum[0], box.minimum[1], box.maximum[2]},
        {box.maximum[0], box.maximum[1], box.maximum[2]},
        {box.minimum[0], box.maximum[1], box.maximum[2]},
    }};
    // The twelve box edges as corner index pairs, in the corner order built above.
    constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7},
    }};
    for (const auto& edge : edges) {
        writer.line(corners[edge[0]], corners[edge[1]], box.color);
    }
}

/** Adds one great circle around the selected pair of vector lanes. */
void append_circle(Writer& writer,
                   const Sphere& sphere,
                   std::size_t firstLane,
                   std::size_t secondLane,
                   std::uint16_t segments) noexcept {
    for (std::uint16_t segment = 0; segment < segments; ++segment) {
        const float firstAngle =
            2.0F * kPi * static_cast<float>(segment) / static_cast<float>(segments);
        const float secondAngle =
            2.0F * kPi * static_cast<float>(segment + 1U) / static_cast<float>(segments);
        Vector3 first = sphere.centre;
        Vector3 second = sphere.centre;
        first[firstLane] += std::cos(firstAngle) * sphere.radius;
        first[secondLane] += std::sin(firstAngle) * sphere.radius;
        second[firstLane] += std::cos(secondAngle) * sphere.radius;
        second[secondLane] += std::sin(secondAngle) * sphere.radius;
        writer.line(first, second, sphere.color);
    }
}

/** Adds three great circles for one sphere volume. */
void append_sphere(Writer& writer, const Sphere& sphere) noexcept {
    if (!finite(sphere.centre) || !std::isfinite(sphere.radius) || sphere.radius <= 0.0F) {
        return;
    }
    const std::uint16_t segments =
        std::clamp(sphere.segments, kMinimumSphereSegments, kMaximumSphereSegments);
    append_circle(writer, sphere, 0, 1, segments);
    append_circle(writer, sphere, 0, 2, segments);
    append_circle(writer, sphere, 1, 2, segments);
}

/** Expands every typed primitive into the one bounded line list. */
[[nodiscard]] Writer build_vertices(const Batch& batch) noexcept {
    Writer writer{};
    std::size_t inputsVisited{};
    const auto admit = [&writer, &inputsVisited]() noexcept {
        if (writer.truncated()) {
            return false;
        }
        if (inputsVisited >= kMaximumPrimitiveInputs) {
            writer.mark_truncated();
            return false;
        }
        ++inputsVisited;
        return true;
    };
    for (const Point& point : batch.points) {
        if (!admit()) {
            return writer;
        }
        append_point(writer, point);
    }
    for (const Axes& axes : batch.axes) {
        if (!admit()) {
            return writer;
        }
        append_axes(writer, axes);
    }
    for (const Box& box : batch.boxes) {
        if (!admit()) {
            return writer;
        }
        append_box(writer, box);
    }
    for (const Sphere& sphere : batch.spheres) {
        if (!admit()) {
            return writer;
        }
        append_sphere(writer, sphere);
    }
    for (const Edge& edge : batch.edges) {
        if (!admit()) {
            return writer;
        }
        writer.line(edge.first, edge.second, edge.color);
    }
    return writer;
}

/** Creates the empty context state used to preserve every game pipeline binding. */
[[nodiscard]] bool create_context_state(ID3D11Device* device) noexcept {
    ID3D11Device1* device1 = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device1)))
        || device1 == nullptr) {
        release_com(device1);
        return false;
    }
    const D3D_FEATURE_LEVEL requested = device->GetFeatureLevel();
    D3D_FEATURE_LEVEL selected{};
    const HRESULT result = device1->CreateDeviceContextState(0,
                                                             &requested,
                                                             1,
                                                             D3D11_SDK_VERSION,
                                                             __uuidof(ID3D11Device),
                                                             &selected,
                                                             &g_resources.contextState);
    release_com(device1);
    return SUCCEEDED(result) && g_resources.contextState != nullptr && selected == requested;
}

/** Creates shaders, buffers, fixed pipeline state, and a full context-state swap object. */
[[nodiscard]] bool initialize(ID3D11Device* device) noexcept {
    if (device == nullptr) {
        return false;
    }
    if (g_resources.owner == device) {
        return !g_resources.failed;
    }
    release();
    g_resources.owner = device;

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* geometryBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(kVertexShader,
                                std::strlen(kVertexShader),
                                nullptr,
                                nullptr,
                                nullptr,
                                "main",
                                "vs_4_0",
                                D3DCOMPILE_ENABLE_STRICTNESS,
                                0,
                                &vertexBlob,
                                &errors);
    release_com(errors);
    if (SUCCEEDED(result)) {
        result = D3DCompile(kPixelShader,
                            std::strlen(kPixelShader),
                            nullptr,
                            nullptr,
                            nullptr,
                            "main",
                            "ps_4_0",
                            D3DCOMPILE_ENABLE_STRICTNESS,
                            0,
                            &pixelBlob,
                            &errors);
        release_com(errors);
    }
    if (SUCCEEDED(result)) {
        result = D3DCompile(kGeometryShader,
                            std::strlen(kGeometryShader),
                            nullptr,
                            nullptr,
                            nullptr,
                            "main",
                            "gs_4_0",
                            D3DCOMPILE_ENABLE_STRICTNESS,
                            0,
                            &geometryBlob,
                            &errors);
        release_com(errors);
    }
    if (SUCCEEDED(result)) {
        result = device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                            vertexBlob->GetBufferSize(),
                                            nullptr,
                                            &g_resources.vertexShader);
    }
    if (SUCCEEDED(result)) {
        result = device->CreateGeometryShader(geometryBlob->GetBufferPointer(),
                                              geometryBlob->GetBufferSize(),
                                              nullptr,
                                              &g_resources.geometryShader);
    }
    if (SUCCEEDED(result)) {
        result = device->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                           pixelBlob->GetBufferSize(),
                                           nullptr,
                                           &g_resources.pixelShader);
    }
    // Input layout must match the Vertex struct: three floats, then a packed RGBA byte quad.
    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> layout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    }};
    if (SUCCEEDED(result)) {
        result = device->CreateInputLayout(layout.data(),
                                           static_cast<UINT>(layout.size()),
                                           vertexBlob->GetBufferPointer(),
                                           vertexBlob->GetBufferSize(),
                                           &g_resources.inputLayout);
    }
    release_com(geometryBlob);
    release_com(pixelBlob);
    release_com(vertexBlob);

    D3D11_BUFFER_DESC vertexDescription{};
    vertexDescription.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaximumVertices);
    vertexDescription.Usage = D3D11_USAGE_DYNAMIC;
    vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result)) {
        result = device->CreateBuffer(&vertexDescription, nullptr, &g_resources.vertexBuffer);
    }
    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = static_cast<UINT>(sizeof(CameraConstants));
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result)) {
        result = device->CreateBuffer(&constantDescription, nullptr, &g_resources.constantBuffer);
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result)) {
        result = device->CreateBlendState(&blend, &g_resources.blendState);
    }
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.StencilEnable = FALSE;
    if (SUCCEEDED(result)) {
        result = device->CreateDepthStencilState(&depth, &g_resources.depthState);
    }
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = TRUE;
    rasterizer.AntialiasedLineEnable = TRUE;
    if (SUCCEEDED(result)) {
        result = device->CreateRasterizerState(&rasterizer, &g_resources.rasterizerState);
    }
    if (SUCCEEDED(result) && !create_context_state(device)) {
        result = E_FAIL;
    }
    if (FAILED(result)) {
        release_objects();
        g_resources.failed = true;
        return false;
    }
    return true;
}

/** @return The exact texture size behind one render-target view. */
[[nodiscard]] bool
target_size(ID3D11RenderTargetView* target, float& width, float& height) noexcept {
    width = 0.0F;
    height = 0.0F;
    if (target == nullptr) {
        return false;
    }
    ID3D11Resource* resource = nullptr;
    ID3D11Texture2D* texture = nullptr;
    target->GetResource(&resource);
    if (resource == nullptr
        || FAILED(
            resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))
        || texture == nullptr) {
        release_com(texture);
        release_com(resource);
        return false;
    }
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    release_com(texture);
    release_com(resource);
    width = static_cast<float>(description.Width);
    height = static_cast<float>(description.Height);
    return description.Width != 0 && description.Height != 0;
}

/** Uploads one completed line list and draws it inside an already swapped context. */
[[nodiscard]] bool render(ID3D11DeviceContext* context,
                          ID3D11RenderTargetView* target,
                          CameraConstants camera,
                          float lineWidthPixels,
                          std::size_t vertexCount) noexcept {
    float width = 0.0F;
    float height = 0.0F;
    if (context == nullptr || target == nullptr || vertexCount == 0
        || vertexCount > g_vertices.size() || !target_size(target, width, height)) {
        return false;
    }
    const float widthPixels =
        std::isfinite(lineWidthPixels)
            ? std::clamp(lineWidthPixels, kMinimumLineWidth, kMaximumLineWidth)
            : kMinimumLineWidth;
    camera.stroke = {2.0F / width, 2.0F / height, widthPixels * 0.5F, 0.0F};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(g_resources.vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, g_vertices.data(), vertexCount * sizeof(Vertex));
    context->Unmap(g_resources.vertexBuffer, 0);
    if (FAILED(context->Map(g_resources.constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &camera, sizeof(camera));
    context->Unmap(g_resources.constantBuffer, 0);

    const D3D11_VIEWPORT viewport{0.0F, 0.0F, width, height, 0.0F, 1.0F};
    // Blend state uses no constant factor, and the one vertex stream starts at its first element.
    constexpr float blendFactor[4]{};
    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffer = g_resources.vertexBuffer;
    ID3D11Buffer* constantBuffer = g_resources.constantBuffer;
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(0, nullptr);
    context->RSSetState(g_resources.rasterizerState);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(g_resources.blendState, blendFactor, 0xFFFFFFFFU);
    context->OMSetDepthStencilState(g_resources.depthState, 0);
    context->IASetInputLayout(g_resources.inputLayout);
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(g_resources.vertexShader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constantBuffer);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(g_resources.geometryShader, nullptr, 0);
    context->GSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetShader(g_resources.pixelShader, nullptr, 0);
    context->Draw(static_cast<UINT>(vertexCount), 0);
    return true;
}

} // namespace

/** Draws one bounded world-line batch with depth disabled. */
Result draw(ID3D11Device* device,
            ID3D11DeviceContext* context,
            ID3D11RenderTargetView* target,
            const teleport::CameraPose& camera,
            const Batch& batch) noexcept {
    Result outcome{};
    CameraConstants constants{};
    if (!camera_constants(camera, batch.invertX, batch.invertY, constants)) {
        return outcome;
    }
    const Writer writer = build_vertices(batch);
    outcome.vertices = writer.count();
    outcome.truncated = writer.truncated();
    if (outcome.vertices == 0 || !initialize(device)) {
        return outcome;
    }
    ID3D11DeviceContext1* context1 = nullptr;
    if (context == nullptr
        || FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                          reinterpret_cast<void**>(&context1)))
        || context1 == nullptr) {
        release_com(context1);
        return outcome;
    }

    ID3DDeviceContextState* previous = nullptr;
    context1->SwapDeviceContextState(g_resources.contextState, &previous);
    outcome.rendered =
        previous != nullptr
        && render(context, target, constants, batch.lineWidthPixels, outcome.vertices);
    ID3DDeviceContextState* privateState = nullptr;
    context1->SwapDeviceContextState(previous, &privateState);
    release_com(privateState);
    release_com(previous);
    release_com(context1);
    return outcome;
}

/** Releases every device-owned resource created by the world-line pass. */
void release() noexcept {
    release_objects();
    g_resources.owner = nullptr;
    g_resources.failed = false;
}

} // namespace sunrise::client::hooks::graphics::renderer::world_lines
