// Actual installed LVGL function bodies, generated without editing upstream.
// Only lightweight types/API macros and constexpr qualification are adapted.
#include "lvgl_mapping_fixture.h"
#include "../../src/display_rotation_buffer.h"

struct MappingOps {
    uint8_t storage[1]{};
    unsigned allocations = 0, rotations = 0, transfers = 0, completions = 0, failures = 0;
    bool fail = false;
    display_rotation::Plan plan{};
    constexpr uint32_t stride(uint32_t width) { return width * 2; }
    constexpr uint8_t *resize(uint8_t *, size_t) { ++allocations; return fail ? nullptr : storage; }
    constexpr void rotate(const display_rotation::Request &, const display_rotation::Plan &, uint8_t *) { ++rotations; }
    constexpr display_rotation::Error dma(const uint8_t *, const display_rotation::Plan &p) {
        ++transfers; plan = p; return display_rotation::Error::None;
    }
    constexpr void failed(display_rotation::Error, size_t) { ++failures; }
    constexpr void ready() { ++completions; }
};

constexpr bool zeroFromActualGetters(lv_display_t &display)
{
    return display_rotation::zeroRawOffsetFromPublic(display.rotation,
        lv_display_get_offset_x(&display), lv_display_get_offset_y(&display),
        lv_display_get_physical_horizontal_resolution(&display),
        lv_display_get_physical_vertical_resolution(&display));
}

constexpr bool mappedAreaThroughPolicy(lv_display_t &display, lv_area_t original, bool fail = false)
{
    const int32_t logicalW = lv_display_get_horizontal_resolution(&display);
    const int32_t logicalH = lv_display_get_vertical_resolution(&display);
    if (original.x1 < 0 || original.y1 < 0 || original.x2 >= logicalW || original.y2 >= logicalH ||
        original.x2 < original.x1 || original.y2 < original.y1 || !zeroFromActualGetters(display)) return false;
    const uint32_t width = lv_area_get_width(&original), height = lv_area_get_height(&original);
    lv_area_t mapped = original;
    lv_display_rotate_area(&display, &mapped); // Actual upstream implementation.
    const uint32_t outWidth = lv_area_get_width(&mapped), outHeight = lv_area_get_height(&mapped);
    if (outWidth != (display.rotation & 1 ? height : width) ||
        outHeight != (display.rotation & 1 ? width : height) || mapped.x1 < 0 || mapped.y1 < 0 ||
        mapped.x2 >= display.hor_res || mapped.y2 >= display.ver_res) return false;
    uint8_t source[1]{}; // Fake policy operations never access image bytes.
    display_rotation::Request request{};
    request.source = source; request.sourceAvailable = size_t(width) * height * 2;
    request.width = width; request.height = height; request.sourceStride = width * 2;
    request.framebufferWidth = display.hor_res; request.framebufferHeight = display.ver_res;
    request.x = mapped.x1; request.y = mapped.y1; request.rotation = display.rotation; request.rgb565 = true;
    display_rotation::Buffer buffer{};
    MappingOps ops{};
    ops.fail = fail;
    display_rotation::Plan details{};
    const auto result = display_rotation::flush(buffer, request, ops, &details);
    if (fail)
        return result == display_rotation::Error::Allocation && ops.allocations == 1 &&
            !ops.rotations && !ops.transfers && ops.failures == 1 && ops.completions == 1;
    return result == display_rotation::Error::None && details.geometryReject == display_rotation::GeometryReject::None &&
        ops.transfers == 1 && ops.completions == 1 && !ops.failures &&
        ops.rotations == 1 &&
        details.width == outWidth && details.height == outHeight &&
        details.destinationStride == outWidth * 2 && details.bytes == size_t(width) * height * 2 &&
        details.framebufferOffset == size_t(mapped.y1 * display.hor_res + mapped.x1) * 2;
}

constexpr bool allEdgesCornersAndOrigins(unsigned rotation)
{
    lv_display_t display{};
    display.rotation = static_cast<lv_display_rotation_t>(rotation);
    const int32_t w = lv_display_get_horizontal_resolution(&display), h = lv_display_get_vertical_resolution(&display);
    const lv_area_t areas[] = {
        {0, 0, 0, 0}, {w - 1, 0, w - 1, 0}, {0, h - 1, 0, h - 1}, {w - 1, h - 1, w - 1, h - 1},
        {0, 0, w - 1, 0}, {w - 1, 0, w - 1, h - 1}, {0, h - 1, w - 1, h - 1}, {0, 0, 0, h - 1},
        {2, 3, 7, 11}, {w - 8, h - 12, w - 2, h - 3}, {1, 1, w - 2, 2}, {2, 1, 3, h - 2}
    };
    for (const auto &area : areas) if (!mappedAreaThroughPolicy(display, area)) return false;
    return true;
}

constexpr bool trueRawOffsetsAreRejected(unsigned rotation)
{
    lv_display_t display{};
    display.rotation = static_cast<lv_display_rotation_t>(rotation);
    if (!zeroFromActualGetters(display)) return false;
    for (int32_t offset = -1; offset <= 1; offset += 2) {
        display.offset_x = offset; display.offset_y = 0;
        if (zeroFromActualGetters(display)) return false;
        display.offset_x = 0; display.offset_y = offset;
        if (zeroFromActualGetters(display)) return false;
    }
    return true;
}

constexpr bool nominalActual270Mapping()
{
    lv_display_t display{};
    display.rotation = LV_DISPLAY_ROTATION_270;
    if (lv_display_get_offset_x(&display) != 800 || lv_display_get_offset_y(&display) != 480 ||
        !zeroFromActualGetters(display)) return false;
    lv_area_t area{0, 0, 799, 23};
    lv_display_rotate_area(&display, &area);
    return area.x1 == 456 && area.y1 == 0 && area.x2 == 479 && area.y2 == 799 &&
        mappedAreaThroughPolicy(display, {0, 0, 799, 23});
}

constexpr bool realBadSourceAndDestination(unsigned rotation)
{
    lv_display_t display{};
    display.rotation = static_cast<lv_display_rotation_t>(rotation);
    const int32_t w = lv_display_get_horizontal_resolution(&display), h = lv_display_get_vertical_resolution(&display);
    if (mappedAreaThroughPolicy(display, {-1, 0, 0, 0}) ||
        mappedAreaThroughPolicy(display, {0, 0, w, 0}) ||
        mappedAreaThroughPolicy(display, {0, 0, 0, h})) return false;
    uint8_t source[1]{};
    display_rotation::Request request{};
    request.source = source; request.sourceAvailable = 2; request.width = request.height = 1;
    request.sourceStride = 2; request.framebufferWidth = 480; request.framebufferHeight = 800;
    request.rotation = rotation; request.rgb565 = true; request.x = 480;
    display_rotation::Buffer buffer{};
    MappingOps ops{};
    if (display_rotation::flush(buffer, request, ops) != display_rotation::Error::Geometry ||
        ops.transfers || ops.rotations || ops.completions != 1) return false;
    return mappedAreaThroughPolicy(display, {2, 3, 7, 11}, true);
}

static_assert(allEdgesCornersAndOrigins(0), "Actual LVGL mapping covers all corners/edges and nonzero origins at 0 degrees");
static_assert(allEdgesCornersAndOrigins(1), "Actual LVGL mapping covers all corners/edges and nonzero origins at 90 degrees");
static_assert(allEdgesCornersAndOrigins(2), "Actual LVGL mapping covers all corners/edges and nonzero origins at 180 degrees");
static_assert(allEdgesCornersAndOrigins(3), "Actual LVGL mapping covers all corners/edges and nonzero origins at 270 degrees");
static_assert(trueRawOffsetsAreRejected(0), "Actual zero/raw-offset getter semantics at 0 degrees");
static_assert(trueRawOffsetsAreRejected(1), "Actual zero/raw-offset getter semantics at 90 degrees");
static_assert(trueRawOffsetsAreRejected(2), "Actual zero/raw-offset getter semantics at 180 degrees");
static_assert(trueRawOffsetsAreRejected(3), "Actual 800/480 getter output at 270 represents zero raw translation");
static_assert(nominalActual270Mapping(), "Actual LVGL 800x24 mapping yields 456,0,479,799 and production policy accepts it");
static_assert(realBadSourceAndDestination(0), "Actual 0-degree mapping retains genuine source/destination guards and ready");
static_assert(realBadSourceAndDestination(1), "Actual 90-degree mapping retains genuine source/destination guards and allocation abort ready");
static_assert(realBadSourceAndDestination(2), "Actual 180-degree mapping retains genuine source/destination guards and allocation abort ready");
static_assert(realBadSourceAndDestination(3), "Actual 270-degree mapping retains genuine source/destination guards and allocation abort ready");
