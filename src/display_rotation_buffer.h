#ifndef MOFA_DISPLAY_ROTATION_BUFFER_H
#define MOFA_DISPLAY_ROTATION_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

namespace display_rotation {
enum class Error : uint8_t { None, Display, Format, Geometry, Stride, Source, Allocation, Transfer, Count };
enum class GeometryReject : uint8_t { None, Input, WidthOverflow, DestinationBounds, OutputSizeOverflow, FramebufferOverflow };

// LVGL 9.3/9.6 offset getters return rotated coordinates, not raw offsets.
// At 180/270 a zero raw translation is exposed as the physical resolution;
// at 0/90 it is exposed as (0,0). Pass the same rotated physical getters used
// by LVGL itself. Equality avoids overflow-prone reverse subtraction.
constexpr bool zeroRawOffsetFromPublic(unsigned rotation, int32_t offsetX, int32_t offsetY,
                                       int32_t physicalWidth, int32_t physicalHeight) {
    if (rotation > 3 || physicalWidth <= 0 || physicalHeight <= 0) return false;
    if (rotation < 2) return offsetX == 0 && offsetY == 0;
    return offsetX == physicalWidth && offsetY == physicalHeight;
}

struct Request {
    const uint8_t *source = nullptr;
    size_t sourceAvailable = 0;
    uint32_t width = 0, height = 0, sourceStride = 0, strideAlignment = 1;
    uint32_t framebufferWidth = 0, framebufferHeight = 0, x = 0, y = 0;
    unsigned rotation = 0; // Quarter turns, matching LVGL 0/90/180/270.
    bool rgb565 = false;
    Error preflight = Error::None;
};
struct Plan {
    uint32_t width = 0, height = 0, destinationStride = 0;
    size_t bytes = 0, framebufferOffset = 0;
    GeometryReject geometryReject = GeometryReject::None;
};

constexpr Error geometryFailure(Plan &plan, GeometryReject detail) {
    plan.geometryReject = detail;
    return Error::Geometry;
}

constexpr bool multiply(size_t a, size_t b, size_t &out) {
    if (b && a > SIZE_MAX / b) return false;
    out = a * b;
    return true;
}

template<class Ops>
constexpr Error makePlan(const Request &request, Plan &plan, Ops &ops) {
    if (request.preflight != Error::None) return request.preflight;
    if (!request.rgb565) return Error::Format;
    if (!request.width || !request.height || request.rotation > 3) return geometryFailure(plan, GeometryReject::Input);
    // LVGL's default stride callback evaluates width * 16 + 7 in uint32_t.
    // Check that expression and alignment before invoking the public API.
    const uint32_t widthLimit = (UINT32_MAX - 7u) / 16u;
    if (request.width > widthLimit || request.height > widthLimit) return geometryFailure(plan, GeometryReject::WidthOverflow);
    plan.width = request.rotation & 1 ? request.height : request.width;
    plan.height = request.rotation & 1 ? request.width : request.height;
    if (request.x > request.framebufferWidth || plan.width > request.framebufferWidth - request.x ||
        request.y > request.framebufferHeight || plan.height > request.framebufferHeight - request.y)
        return geometryFailure(plan, GeometryReject::DestinationBounds);
    const uint32_t rowBytes = request.width * 2u;
    const uint32_t destinationBytes = plan.width * 2u;
    if (!request.strideAlignment ||
        destinationBytes > UINT32_MAX - (request.strideAlignment - 1u)) return Error::Stride;
    if (request.sourceStride < rowBytes || request.sourceStride % 2 ||
        request.sourceStride > INT32_MAX) return Error::Stride;
    size_t prefix = 0;
    if (!multiply(request.height - 1u, request.sourceStride, prefix) ||
        prefix > SIZE_MAX - rowBytes || prefix + rowBytes > INT32_MAX ||
        !request.source || prefix + rowBytes > request.sourceAvailable) return Error::Source;
    // DMA2D's existing InputOffset=0 cannot consume padded destination rows.
    plan.destinationStride = request.rotation ? ops.stride(plan.width) : request.sourceStride;
    if (plan.destinationStride != destinationBytes) return Error::Stride;
    if (!multiply(plan.destinationStride, plan.height, plan.bytes) || plan.bytes > INT32_MAX)
        return geometryFailure(plan, GeometryReject::OutputSizeOverflow);
    size_t row = 0, framePixels = 0, frameBytes = 0;
    if (!multiply(request.y, request.framebufferWidth, row) || row > SIZE_MAX - request.x ||
        !multiply(row + request.x, 2, plan.framebufferOffset) ||
        !multiply(request.framebufferWidth, request.framebufferHeight, framePixels) ||
        !multiply(framePixels, 2, frameBytes) || plan.framebufferOffset >= frameBytes)
        return geometryFailure(plan, GeometryReject::FramebufferOverflow);
    return Error::None;
}

struct Buffer {
    uint8_t *data = nullptr;
    size_t capacity = 0;
    bool pinned = false;
    template<class Ops>
    constexpr bool ensure(size_t required, Ops &ops) {
        if (!required || pinned) return false;
        if (data && required <= capacity) return true;
        // Never overwrite the only owning pointer until realloc succeeds.
        uint8_t *replacement = ops.resize(data, required);
        if (!replacement) return false;
        data = replacement;
        capacity = required;
        return true;
    }
};

// The hardware adapter and regression fakes use this same ordering: validate,
// grow if necessary, rotate, DMA, then exactly one ready on every outcome.
template<class Ops>
constexpr Error flush(Buffer &buffer, const Request &request, Ops &ops, Plan *details = nullptr) {
    Plan plan;
    Error error = makePlan(request, plan, ops);
    if (details) *details = plan;
    if (buffer.pinned) error = Error::Transfer;
    if (error == Error::None) {
        const uint8_t *pixels = request.source;
        {
            if (!buffer.ensure(plan.bytes, ops)) error = Error::Allocation;
            else {
                ops.rotate(request, plan, buffer.data);
                pixels = buffer.data;
            }
        }
        if (error == Error::None) error = ops.dma(pixels, plan);
    }
    if (error != Error::None) ops.failed(error, plan.bytes);
    ops.ready();
    return error;
}
} // namespace display_rotation
#endif
