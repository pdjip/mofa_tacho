// Compile-time checks execute the production allocation/flush policy.
// Fake operations observe calls and geometry; they do not emulate pixel/DMA hardware.
#include "../../src/display_rotation_buffer.h"

struct RotationOps {
    uint8_t first[1]{}, grown[1]{};
    bool failAllocation = false;
    uint32_t padding = 0;
    unsigned strides = 0, allocations = 0, rotations = 0, transfers = 0, failures = 0, completions = 0;
    unsigned order = 0, angle = 0;
    size_t required = 0;
    uint8_t *oldPointer = nullptr, *rotatedPointer = nullptr;
    const uint8_t *transferred = nullptr;
    uint32_t observedSourceStride = 0;
    display_rotation::Plan plan{};
    display_rotation::Error error = display_rotation::Error::None;
    constexpr uint32_t stride(uint32_t width) { ++strides; order = order * 10 + 1; return width * 2 + padding; }
    constexpr uint8_t *resize(uint8_t *old, size_t bytes) {
        ++allocations; order = order * 10 + 2; oldPointer = old; required = bytes;
        return failAllocation ? nullptr : (allocations == 1 ? first : grown);
    }
    constexpr void rotate(const display_rotation::Request &request, const display_rotation::Plan &p, uint8_t *pixels) {
        ++rotations; order = order * 10 + 3; plan = p; angle = request.rotation;
        observedSourceStride = request.sourceStride; rotatedPointer = pixels;
    }
    constexpr display_rotation::Error dma(const uint8_t *pixels, const display_rotation::Plan &p) {
        ++transfers; order = order * 10 + 4; transferred = pixels; plan = p;
        return display_rotation::Error::None;
    }
    constexpr void failed(display_rotation::Error value, size_t bytes) {
        ++failures; order = order * 10 + 5; error = value; required = bytes;
    }
    constexpr void ready() { ++completions; order = order * 10 + 6; }
};

constexpr display_rotation::Request rotationRequest(const uint8_t *source, unsigned rotation = 1)
{
    display_rotation::Request request{};
    request.source = source; request.sourceAvailable = 12;
    request.width = 3; request.height = 2; request.sourceStride = 6;
    request.framebufferWidth = 800; request.framebufferHeight = 480;
    request.x = 5; request.y = 7; request.rotation = rotation; request.rgb565 = true;
    return request;
}

constexpr bool rotationGeometry(unsigned angle)
{
    uint8_t source[16]{};
    const auto request = rotationRequest(source, angle);
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    const auto result = display_rotation::flush(buffer, request, ops);
    const uint32_t width = angle & 1 ? 2 : 3, height = angle & 1 ? 3 : 2;
    if (result != display_rotation::Error::None || ops.failures || ops.completions != 1 || ops.transfers != 1 ||
        ops.plan.width != width || ops.plan.height != height || ops.plan.destinationStride != width * 2 ||
        ops.plan.bytes != 12 || ops.plan.framebufferOffset != (7 * 800 + 5) * 2) return false;
    if (!angle) return !ops.strides && ops.allocations == 1 && ops.rotations == 1 &&
        buffer.data == ops.first && ops.transferred == buffer.data && ops.transferred != source && ops.order == 2346;
    return ops.allocations == 1 && ops.rotations == 1 && ops.angle == angle && buffer.data == ops.first &&
        buffer.capacity == 12 && ops.rotatedPointer == buffer.data && ops.transferred == buffer.data &&
        ops.observedSourceStride == 6 && ops.order == 12346;
}

constexpr bool reuseAndGrow()
{
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    if (!buffer.ensure(12, ops) || buffer.data != ops.first || buffer.capacity != 12) return false;
    if (!buffer.ensure(12, ops) || !buffer.ensure(6, ops) || ops.allocations != 1) return false;
    if (!buffer.ensure(24, ops) || buffer.data != ops.grown || buffer.capacity != 24 ||
        ops.oldPointer != ops.first || ops.allocations != 2) return false;
    return buffer.ensure(18, ops) && ops.allocations == 2 && buffer.data == ops.grown;
}

constexpr bool allocationFailurePreservesOwner(bool initial)
{
    uint8_t source[16]{};
    auto request = rotationRequest(source);
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    if (!initial && !buffer.ensure(6, ops)) return false;
    const uint8_t *old = buffer.data;
    const size_t capacity = buffer.capacity;
    ops.failAllocation = true;
    const auto result = display_rotation::flush(buffer, request, ops);
    if (result != display_rotation::Error::Allocation || buffer.data != old || buffer.capacity != capacity ||
        ops.rotations || ops.transfers || ops.failures != 1 || ops.completions != 1 || ops.required != 12)
        return false;
    if (initial) return !buffer.data && !buffer.capacity && ops.allocations == 1;
    // A smaller subsequent flush safely reuses the retained buffer, even while
    // the fake allocator still fails. No allocation, pointer loss or replay.
    request.width = 1; request.sourceStride = 2; request.sourceAvailable = 4;
    const unsigned attempts = ops.allocations;
    return display_rotation::flush(buffer, request, ops) == display_rotation::Error::None &&
        ops.allocations == attempts && ops.transferred == old && buffer.capacity == 6 &&
        ops.rotations == 1 && ops.transfers == 1 && ops.completions == 2;
}

constexpr bool paddedSource(bool rotate)
{
    uint8_t source[16]{};
    auto request = rotationRequest(source, rotate ? 1 : 0);
    request.sourceStride = 8; request.sourceAvailable = 14;
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    const auto result = display_rotation::flush(buffer, request, ops);
    if (!rotate) return result == display_rotation::Error::Stride && !ops.rotations && !ops.transfers &&
        !ops.allocations && ops.completions == 1;
    return result == display_rotation::Error::None && ops.observedSourceStride == 8 &&
        ops.plan.destinationStride == 4 && ops.plan.bytes == 12 && ops.completions == 1;
}

constexpr bool rejected(unsigned reason)
{
    using display_rotation::Error;
    uint8_t source[16]{};
    auto request = rotationRequest(source);
    Error expected = Error::Geometry;
    RotationOps ops{};
    switch (reason) {
        case 0: request.preflight = Error::Display; expected = Error::Display; break;
        case 1: request.rgb565 = false; expected = Error::Format; break;
        case 2: request.width = 0; break;
        case 3: request.rotation = 4; break;
        case 4: request.width = UINT32_MAX; break;
        case 5: request.height = UINT32_MAX; break;
        case 6: request.x = 800; break;
        case 7: request.y = 479; break;
        case 8: request.sourceStride = 4; expected = Error::Stride; break;
        case 9: request.sourceStride = 7; expected = Error::Stride; break;
        case 10: request.sourceStride = UINT32_C(2147483648); expected = Error::Stride; break;
        case 11: request.source = nullptr; expected = Error::Source; break;
        case 12: request.sourceAvailable = 11; expected = Error::Source; break;
        case 13: request.strideAlignment = 0; expected = Error::Stride; break;
        case 14: request.strideAlignment = UINT32_MAX; expected = Error::Stride; break;
        case 15: ops.padding = 4; expected = Error::Stride; break;
        case 16: request.framebufferWidth = request.framebufferHeight = UINT32_MAX; break;
        case 17: request.preflight = Error::Format; request.rgb565 = false; expected = Error::Format; break;
        case 18: request.sourceStride = INT32_MAX - 1; request.height = 3; expected = Error::Source; break;
        default: return false;
    }
    display_rotation::Buffer buffer{};
    const auto result = display_rotation::flush(buffer, request, ops);
    return result == expected && ops.error == expected && ops.failures == 1 && ops.completions == 1 &&
        !ops.rotations && !ops.transfers && !ops.allocations && !buffer.data &&
        ((reason == 4 || reason == 5 || reason == 14) ? ops.strides == 0 : true);
}

constexpr bool maximumCurrentPartialBlock()
{
    uint8_t source[1]{}; // Pointer token only; fake rotate/DMA never read pixels.
    auto request = rotationRequest(source, 3);
    request.width = 480; request.height = 40; request.sourceStride = 960;
    request.sourceAvailable = 38400; request.x = request.y = 0;
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    return display_rotation::flush(buffer, request, ops) == display_rotation::Error::None &&
        buffer.capacity == 38400 && ops.plan.width == 40 && ops.plan.height == 480 &&
        ops.plan.bytes == 38400 && ops.plan.destinationStride == 80 && ops.completions == 1;
}

constexpr bool multiplicationAndZeroAllocation()
{
    size_t output = 77;
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    return !display_rotation::multiply(SIZE_MAX, 2, output) && output == 77 &&
        display_rotation::multiply(0, SIZE_MAX, output) && output == 0 &&
        !buffer.ensure(0, ops) && !ops.allocations && !buffer.data && !buffer.capacity;
}

static_assert(rotationGeometry(0), "Zero rotation copies tight RGB565 into project-owned DMA storage");
static_assert(rotationGeometry(1), "90 degree RGB565 uses two bytes per pixel and rotated dimensions");
static_assert(rotationGeometry(2), "180 degree RGB565 retains source dimensions with tight destination stride");
static_assert(rotationGeometry(3), "270 degree RGB565 swaps dimensions and preserves destination offset");
static_assert(reuseAndGrow(), "Buffer reuses capacity and commits growth only after allocation succeeds");
static_assert(allocationFailurePreservesOwner(true), "Initial allocation failure completes once without rotate or DMA");
static_assert(allocationFailurePreservesOwner(false), "Failed growth preserves pointer and capacity for safe subsequent reuse");
static_assert(paddedSource(true), "Rotation receives actual padded source stride and produces tight DMA rows");
static_assert(paddedSource(false), "Zero rotation rejects padded DMA input");
static_assert(maximumCurrentPartialBlock(), "Current 480x40 partial RGB565 needs 38400 instead of 76800 bytes");
static_assert(multiplicationAndZeroAllocation(), "Size overflow and zero allocation fail before allocation");
static_assert(rejected(0), "Unsupported display mode aborts and completes exactly once");
static_assert(rejected(1), "Unsupported color format aborts and completes exactly once");
static_assert(rejected(2), "Empty geometry is rejected");
static_assert(rejected(3), "Unknown rotation is rejected");
static_assert(rejected(4), "Width overflow is rejected before stride API or allocation");
static_assert(rejected(5), "Height overflow is rejected before stride API or allocation");
static_assert(rejected(6), "Destination x bound is checked");
static_assert(rejected(7), "Destination y bound is checked");
static_assert(rejected(8), "Source stride smaller than a row is rejected");
static_assert(rejected(9), "Odd RGB565 source stride is rejected");
static_assert(rejected(10), "Source stride exceeding signed LVGL argument range is rejected");
static_assert(rejected(11), "Null source never reaches rotate or DMA");
static_assert(rejected(12), "Source byte extent is checked");
static_assert(rejected(13), "Zero stride alignment is rejected");
static_assert(rejected(14), "Stride rounding overflow is rejected before stride API");
static_assert(rejected(15), "Padded destination is rejected because DMA input offset remains zero");
static_assert(rejected(16), "Framebuffer byte size overflow is rejected");
static_assert(rejected(17), "Explicit format preflight classification is preserved");
static_assert(rejected(18), "Source prefix multiplication and signed extent bounds are checked");

constexpr bool diagnosticPlanOnAbort(unsigned cause)
{
    using display_rotation::Error;
    using display_rotation::GeometryReject;
    uint8_t source[16]{};
    auto request = rotationRequest(source);
    RotationOps ops{};
    Error error = Error::Geometry;
    GeometryReject detail = GeometryReject::None;
    switch (cause) {
        case 0: request.preflight = Error::Geometry; break;
        case 1: request.width = 0; detail = GeometryReject::Input; break;
        case 2: request.width = UINT32_MAX; detail = GeometryReject::WidthOverflow; break;
        case 3: request.x = 800; detail = GeometryReject::DestinationBounds; break;
        case 4: request.framebufferWidth = request.framebufferHeight = UINT32_MAX;
                detail = GeometryReject::FramebufferOverflow; break;
        case 5: request.rgb565 = false; error = Error::Format; break;
        case 6: ops.failAllocation = true; error = Error::Allocation; break;
        case 7: request.sourceAvailable = 11; error = Error::Source; break;
        default: return false;
    }
    display_rotation::Buffer buffer{};
    display_rotation::Plan plan{};
    plan.geometryReject = GeometryReject::OutputSizeOverflow; // Must be overwritten by copied plan.
    const auto result = display_rotation::flush(buffer, request, ops, &plan);
    return result == error && plan.geometryReject == detail && ops.failures == 1 &&
        ops.completions == 1 && !ops.rotations && !ops.transfers &&
        (cause == 6 ? plan.bytes == 12 && ops.allocations == 1 : ops.allocations == 0);
}

constexpr bool nominalLandscape800x24ToPortrait480x800()
{
    uint8_t source[1]{}; // Pointer token only: fake rotate/DMA do not access pixels.
    auto request = rotationRequest(source, 3);
    request.width = 800; request.height = 24; request.sourceStride = 1600;
    request.sourceAvailable = 38400;
    request.framebufferWidth = 480; request.framebufferHeight = 800;
    request.x = 456; request.y = 0; // LVGL 270-degree map of area (0,0,799,23).
    RotationOps ops{};
    display_rotation::Buffer buffer{};
    display_rotation::Plan details{};
    return display_rotation::flush(buffer, request, ops, &details) == display_rotation::Error::None &&
        details.geometryReject == display_rotation::GeometryReject::None &&
        details.width == 24 && details.height == 800 && details.destinationStride == 48 &&
        details.bytes == 38400 && details.framebufferOffset == 912 &&
        buffer.capacity == 38400 && ops.rotations == 1 && ops.angle == 3 &&
        ops.transfers == 1 && ops.completions == 1 && !ops.failures;
}

constexpr bool explicitGeometryClassification()
{
    display_rotation::Plan plan{};
    return display_rotation::geometryFailure(plan, display_rotation::GeometryReject::OutputSizeOverflow) ==
        display_rotation::Error::Geometry &&
        plan.geometryReject == display_rotation::GeometryReject::OutputSizeOverflow;
}

static_assert(diagnosticPlanOnAbort(0), "Adapter geometry preflight preserves None policy subreason and completes once");
static_assert(diagnosticPlanOnAbort(1), "Invalid input records Input policy subreason");
static_assert(diagnosticPlanOnAbort(2), "Unsafe width records WidthOverflow before stride/allocation");
static_assert(diagnosticPlanOnAbort(3), "Outside destination records DestinationBounds");
static_assert(diagnosticPlanOnAbort(4), "Oversized framebuffer records FramebufferOverflow");
static_assert(diagnosticPlanOnAbort(5), "Format abort copies a None geometry detail and never transfers");
static_assert(diagnosticPlanOnAbort(6), "Allocation abort exports computed plan while retaining exactly-one ready");
static_assert(diagnosticPlanOnAbort(7), "Source extent abort exports a None geometry detail and never transfers");
static_assert(nominalLandscape800x24ToPortrait480x800(), "Nominal logical landscape maps to portrait framebuffer at 270 degrees");
static_assert(explicitGeometryClassification(), "Defensive output-size classification exports the requested subreason");

constexpr bool zeroRawOffsetCases(unsigned rotation)
{
    const int32_t x = rotation < 2 ? 0 : 800;
    const int32_t y = rotation < 2 ? 0 : 480;
    return display_rotation::zeroRawOffsetFromPublic(rotation, x, y, 800, 480) &&
        !display_rotation::zeroRawOffsetFromPublic(rotation, x + 1, y, 800, 480) &&
        !display_rotation::zeroRawOffsetFromPublic(rotation, x, y - 1, 800, 480);
}

constexpr bool zeroRawOffsetIntegerBounds()
{
    using display_rotation::zeroRawOffsetFromPublic;
    return zeroRawOffsetFromPublic(0, 0, 0, INT32_MAX, INT32_MAX) &&
        zeroRawOffsetFromPublic(3, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX) &&
        !zeroRawOffsetFromPublic(3, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MAX) &&
        !zeroRawOffsetFromPublic(2, INT32_MAX, INT32_MIN, INT32_MAX, INT32_MAX) &&
        !zeroRawOffsetFromPublic(0, 0, 0, 0, 1) && !zeroRawOffsetFromPublic(0, 0, 0, 1, -1) &&
        !zeroRawOffsetFromPublic(4, 0, 0, 1, 1) && !zeroRawOffsetFromPublic(UINT32_MAX, 0, 0, 1, 1);
}

static_assert(zeroRawOffsetCases(0), "Zero rotation accepts only zero public/raw translation");
static_assert(zeroRawOffsetCases(1), "90 degree zero raw translation is exposed as public zero");
static_assert(zeroRawOffsetCases(2), "180 degree public physical dimensions represent zero raw translation");
static_assert(zeroRawOffsetCases(3), "270 degree public 800/480 represents zero raw translation; real translations fail");
static_assert(zeroRawOffsetIntegerBounds(), "Offset classifier rejects invalid modes/dimensions and compares full int32 without subtraction overflow");
