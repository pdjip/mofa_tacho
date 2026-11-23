#include "display_rotation.h"
#include "display_rotation_buffer.h"
#include "display_dma_contract.h"
#include <Arduino.h>
#include <lvgl.h>
#include <dsi.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

namespace display_rotation {
namespace {
Buffer rotationBuffer;
display_dma::Ownership dmaOwnership;
DMA2D_HandleTypeDef checkedDma = {};
uint32_t errors[static_cast<unsigned>(Error::Count)] = {};
size_t requested[static_cast<unsigned>(Error::Count)] = {};

// Own values, not pointers into LVGL's transient area. Only the first request
// is retained, before any rotate/allocation/DMA is attempted.
struct Parameters {
    Request request;
    lv_area_t area = {}, destination = {};
    int32_t logicalWidth = 0, logicalHeight = 0, originalWidth = 0, originalHeight = 0;
    int32_t offsetX = 0, offsetY = 0;
    int32_t offsetPhysicalWidth = 0, offsetPhysicalHeight = 0;
    uint32_t framebuffer = 0, mode = 0, format = 0, bufferWidth = 0, bufferHeight = 0, bufferSize = 0;
    uint32_t rejectMask = 0;
    Error result = Error::None;
    GeometryReject policy = GeometryReject::None;
    bool valid = false, mapped = false, rawOffsetZero = false;
};
Parameters firstParameters;
struct TransferEvidence {
    uint32_t width = 0, height = 0, pixels = 0;
    display_dma::Result result = display_dma::Result::Complete;
    bool returned = false;
};
TransferEvidence firstTransfer;

char diagnosticLine[256];
struct Message {
    size_t length = 0;
    void text(const char *value) {
        while (*value && length + 1 < sizeof(diagnosticLine)) diagnosticLine[length++] = *value++;
    }
    void number(uint32_t value) {
        char digits[10]; unsigned count = 0;
        do { digits[count++] = char('0' + value % 10); value /= 10; } while (value);
        while (count && length + 1 < sizeof(diagnosticLine)) diagnosticLine[length++] = digits[--count];
    }
    void signedNumber(int32_t value) {
        if (value < 0) { text("-"); number(0u - uint32_t(value)); }
        else number(uint32_t(value));
    }
    void dimensions(uint32_t width, uint32_t height) { number(width); text("x"); number(height); }
    void area(const lv_area_t &value) {
        signedNumber(value.x1); text(","); signedNumber(value.y1); text(",");
        signedNumber(value.x2); text(","); signedNumber(value.y2);
    }
    bool send() {
        // UART's inherited availableForWrite() always returns zero on this
        // core. This is one bounded-size write outside flush, not a promise
        // that the core's Serial.write itself is nonblocking.
        if (!Serial) return false;
        diagnosticLine[length++] = '\n';
        Serial.write(reinterpret_cast<const uint8_t *>(diagnosticLine), length);
        return true;
    }
};

bool reportParameters(unsigned part) {
    const Parameters &p = firstParameters;
    Message message;
    message.text("[DISPLAY ROT DIAG] params"); message.number(part + 1);
    if (part == 0) {
        message.text(" reason="); message.number(static_cast<unsigned>(p.result));
        message.text(" mask="); message.number(p.rejectMask);
        message.text(" policy="); message.number(static_cast<unsigned>(p.policy));
        message.text(" rotation="); message.number(p.request.rotation);
        message.text(" mode="); message.number(p.mode);
        message.text(" cf="); message.number(p.format);
        message.text(" logical="); message.signedNumber(p.logicalWidth); message.text("x"); message.signedNumber(p.logicalHeight);
        message.text(" original="); message.signedNumber(p.originalWidth); message.text("x"); message.signedNumber(p.originalHeight);
        message.text(" dsi="); message.dimensions(p.request.framebufferWidth, p.request.framebufferHeight);
    } else if (part == 1) {
        message.text(" area="); message.area(p.area);
        message.text(" mapped="); message.number(p.mapped);
        message.text(" rotated="); message.area(p.destination);
        message.text(" offsetPublic="); message.signedNumber(p.offsetX); message.text(","); message.signedNumber(p.offsetY);
        message.text(" offsetPhysical="); message.signedNumber(p.offsetPhysicalWidth); message.text("x"); message.signedNumber(p.offsetPhysicalHeight);
        message.text(" rawOffsetZero="); message.number(p.rawOffsetZero);
    } else {
        message.text(" source="); message.dimensions(p.bufferWidth, p.bufferHeight);
        message.text(" stride="); message.number(p.request.sourceStride);
        message.text(" bytes="); message.number(p.bufferSize);
        message.text(" available="); message.number(p.request.sourceAvailable);
        message.text(" framebuffer="); message.number(p.framebuffer);
        message.text(" request="); message.dimensions(p.request.width, p.request.height);
        message.text(" dest="); message.number(p.request.x); message.text(","); message.number(p.request.y);
    }
    return message.send();
}

constexpr uint32_t initialGeometryMask(const Parameters &p) {
    return (p.area.x1 < 0 ? 1u << 0 : 0) |
           (p.area.y1 < 0 ? 1u << 1 : 0) |
           (p.area.x2 < p.area.x1 ? 1u << 2 : 0) |
           (p.area.y2 < p.area.y1 ? 1u << 3 : 0) |
           (p.logicalWidth <= 0 || p.logicalHeight <= 0 ? 1u << 4 : 0) |
           (p.area.x2 >= p.logicalWidth ? 1u << 5 : 0) |
           (p.area.y2 >= p.logicalHeight ? 1u << 6 : 0) |
           (p.originalWidth <= 0 || p.originalHeight <= 0 ? 1u << 7 : 0) |
           (p.originalWidth > 0 && uint32_t(p.originalWidth) > p.request.framebufferWidth ? 1u << 8 : 0) |
           (p.originalHeight > 0 && uint32_t(p.originalHeight) > p.request.framebufferHeight ? 1u << 9 : 0) |
           (!p.rawOffsetZero ? 1u << 10 : 0);
}

void record(Error error, size_t bytes) {
    const unsigned index = static_cast<unsigned>(error);
    if (errors[index] != UINT32_MAX) ++errors[index];
    requested[index] = bytes;
}

struct Hardware {
    lv_display_t *display;
    uint32_t framebuffer;
    uint32_t stride(uint32_t width) { return lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565); }
    uint8_t *resize(uint8_t *old, size_t bytes) { return static_cast<uint8_t *>(realloc(old, bytes)); }
    void rotate(const Request &request, const Plan &plan, uint8_t *destination) {
        if (!request.rotation) { memcpy(destination, request.source, plan.bytes); return; }
        lv_draw_rotate(request.source, destination, request.width, request.height,
            request.sourceStride, plan.destinationStride,
            static_cast<lv_display_rotation_t>(request.rotation), LV_COLOR_FORMAT_RGB565);
    }
    Error dma(const uint8_t *pixels, const Plan &plan) {
        struct Transfer {
            const uint8_t *pixels;
            const Plan &plan;
            uint32_t framebuffer;
            bool inactive() { __DSB(); return (DMA2D->CR & DMA2D_CR_START) == 0; }
            display_dma::Result start() {
                SCB_CleanInvalidateDCache();
                __DSB();
                checkedDma.Instance = DMA2D;
                checkedDma.Init.Mode = DMA2D_M2M_PFC;
                checkedDma.Init.ColorMode = DMA2D_OUTPUT_RGB565;
                checkedDma.Init.OutputOffset = dsi_getDisplayXSize() - plan.width;
                checkedDma.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA;
                checkedDma.LayerCfg[1].InputAlpha = 0;
                checkedDma.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
                checkedDma.LayerCfg[1].InputOffset = 0;
                if (HAL_DMA2D_Init(&checkedDma) != HAL_OK ||
                    HAL_DMA2D_ConfigLayer(&checkedDma, 1) != HAL_OK ||
                    HAL_DMA2D_Start(&checkedDma, uint32_t(uintptr_t(pixels)),
                        framebuffer + plan.framebufferOffset, plan.width, plan.height) != HAL_OK)
                    return display_dma::Result::Error;
                return display_dma::Result::Complete;
            }
            display_dma::Result poll() {
                const auto result = HAL_DMA2D_PollForTransfer(&checkedDma, 25);
                return result == HAL_OK ? display_dma::Result::Complete :
                    result == HAL_TIMEOUT ? display_dma::Result::Timeout : display_dma::Result::Error;
            }
            bool abort() { return HAL_DMA2D_Abort(&checkedDma) == HAL_OK; }
        } transfer{pixels, plan, framebuffer};
        const auto result = dmaOwnership.transfer(transfer);
        rotationBuffer.pinned = dmaOwnership.pinned;
        if (!firstTransfer.returned) {
            firstTransfer.width = plan.width;
            firstTransfer.height = plan.height;
            firstTransfer.pixels = plan.bytes / 2;
            firstTransfer.result = result;
            // Checked transfer result, not proof of the visible panel output.
            firstTransfer.returned = true;
        }
        return result == display_dma::Result::Complete ? Error::None : Error::Transfer;
    }
    void failed(Error error, size_t bytes) { record(error, bytes); }
    void ready() { lv_display_flush_ready(display); }
};

// Project-owned symbol: replaces the registered callback using LVGL's public
// setter; it neither overrides nor duplicates Arduino_H7_Video's symbol.
void projectDisplayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const int saved = errno;
    Request request;
    Parameters parameters;
    request.source = pixels;
    request.strideAlignment = LV_DRAW_BUF_STRIDE_ALIGN;
    request.framebufferWidth = dsi_getDisplayXSize();
    request.framebufferHeight = dsi_getDisplayYSize();
    Hardware hardware{display, dsi_getActiveFrameBuffer()};
    parameters.framebuffer = hardware.framebuffer;
    if (area) parameters.area = parameters.destination = *area;
    if (display) parameters.mode = lv_display_get_render_mode(display);
    if (!display || !area || lv_display_get_render_mode(display) != LV_DISPLAY_RENDER_MODE_PARTIAL) {
        request.preflight = Error::Display;
    } else {
        request.rgb565 = lv_display_get_color_format(display) == LV_COLOR_FORMAT_RGB565;
        parameters.format = lv_display_get_color_format(display);
        if (!request.rgb565) request.preflight = Error::Format;
        const lv_draw_buf_t *buffer = lv_display_get_buf_active(display);
        const int32_t logicalWidth = lv_display_get_horizontal_resolution(display);
        const int32_t logicalHeight = lv_display_get_vertical_resolution(display);
        const int32_t originalWidth = lv_display_get_original_horizontal_resolution(display);
        const int32_t originalHeight = lv_display_get_original_vertical_resolution(display);
        parameters.logicalWidth = logicalWidth; parameters.logicalHeight = logicalHeight;
        parameters.originalWidth = originalWidth; parameters.originalHeight = originalHeight;
        parameters.offsetX = lv_display_get_offset_x(display);
        parameters.offsetY = lv_display_get_offset_y(display);
        request.rotation = static_cast<unsigned>(lv_display_get_rotation(display));
        parameters.offsetPhysicalWidth = lv_display_get_physical_horizontal_resolution(display);
        parameters.offsetPhysicalHeight = lv_display_get_physical_vertical_resolution(display);
        parameters.rawOffsetZero = zeroRawOffsetFromPublic(request.rotation, parameters.offsetX, parameters.offsetY,
            parameters.offsetPhysicalWidth, parameters.offsetPhysicalHeight);
        parameters.request = request;
        parameters.rejectMask = initialGeometryMask(parameters);
        // Bound coordinates before lv_area_get_width/height or rotate_area can
        // perform signed arithmetic. Nonzero offsets/fullscreen modes require
        // a different source origin and are deliberately not interpreted here.
        if (parameters.rejectMask) {
            request.preflight = Error::Geometry;
        } else {
            request.width = uint32_t(area->x2 - area->x1) + 1u;
            request.height = uint32_t(area->y2 - area->y1) + 1u;
            lv_area_t destination = *area;
            lv_display_rotate_area(display, &destination);
            parameters.destination = destination;
            parameters.mapped = true;
            if (destination.x1 < 0 || destination.y1 < 0) parameters.rejectMask |= 1u << 11;
            if (destination.x2 < destination.x1 || destination.y2 < destination.y1) parameters.rejectMask |= 1u << 12;
            if (destination.x2 >= 0 && uint32_t(destination.x2) >= request.framebufferWidth) parameters.rejectMask |= 1u << 13;
            if (destination.y2 >= 0 && uint32_t(destination.y2) >= request.framebufferHeight) parameters.rejectMask |= 1u << 14;
            if (parameters.rejectMask) request.preflight = Error::Geometry;
            else { request.x = destination.x1; request.y = destination.y1; }
        }
        if (buffer) {
            parameters.bufferWidth = buffer->header.w;
            parameters.bufferHeight = buffer->header.h;
            parameters.bufferSize = buffer->data_size;
        }
        if (!buffer || !buffer->data || !pixels ||
            buffer->header.cf != LV_COLOR_FORMAT_RGB565 ||
            buffer->header.w < request.width || buffer->header.h < request.height) {
            if (request.preflight == Error::None) request.preflight = Error::Source;
        } else {
            request.sourceStride = buffer->header.stride;
            const uintptr_t base = reinterpret_cast<uintptr_t>(buffer->unaligned_data ? buffer->unaligned_data : buffer->data);
            const uintptr_t data = reinterpret_cast<uintptr_t>(buffer->data);
            const uintptr_t source = reinterpret_cast<uintptr_t>(pixels);
            if (data < base || source < data || source % alignof(uint16_t) ||
                source - base > buffer->data_size) {
                if (request.preflight == Error::None) request.preflight = Error::Source;
            }
            else request.sourceAvailable = buffer->data_size - (source - base);
        }
        size_t framePixels = 0, frameBytes = 0;
        if (!multiply(request.framebufferWidth, request.framebufferHeight, framePixels) ||
            !multiply(framePixels, 2, frameBytes) || frameBytes > UINTPTR_MAX - hardware.framebuffer) {
            parameters.rejectMask |= 1u << 15;
            request.preflight = Error::Geometry;
        }
    }
    const bool first = !firstParameters.valid;
    if (first) {
        parameters.request = request;
        parameters.valid = true;
        firstParameters = parameters;
    }
    Plan details;
    const Error result = flush(rotationBuffer, request, hardware, first ? &details : nullptr);
    if (first) {
        firstParameters.result = result;
        firstParameters.policy = details.geometryReject;
    }
    errno = saved;
}
} // namespace

bool install() {
    lv_display_t *display = lv_display_get_default();
    if (!display) { record(Error::Display, 0); return false; }
    lv_display_set_flush_cb(display, projectDisplayFlush);
    return true;
}

void reportErrors()
{
    // Same display-thread owner as flush: no mutex or ISR work. One short
    // fixed line per five seconds at most, and no print inside a flush.
    const int saved = errno;
    static unsigned parameterPart = 0;
    while (firstParameters.valid && parameterPart < 3) {
        if (!reportParameters(parameterPart)) { errno = saved; return; }
        ++parameterPart;
    }
    static bool transferReported = false;
    if (firstTransfer.returned && !transferReported && parameterPart == 3) {
        Message message;
        message.text("[DISPLAY ROT DIAG] checked-transfer w="); message.number(firstTransfer.width);
        message.text(" h="); message.number(firstTransfer.height);
        message.text(" pixels="); message.number(firstTransfer.pixels);
        message.text(" result="); message.number(static_cast<unsigned>(firstTransfer.result));
        message.text(" pinned="); message.number(rotationBuffer.pinned);
        message.text(" visible-check=required");
        transferReported = message.send();
    }
    static uint32_t reported[static_cast<unsigned>(Error::Count)] = {};
    static bool printed = false;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (printed && uint32_t(now - last) < 5000) return;
    for (unsigned i = 1; i < static_cast<unsigned>(Error::Count); ++i) {
        if (errors[i] == reported[i]) continue;
        Message message;
        message.text("[DISPLAY ROT DIAG] reason="); message.number(i);
        message.text(" count="); message.number(errors[i]);
        message.text(" need="); message.number(requested[i]);
        message.text(" capacity="); message.number(rotationBuffer.capacity);
        if (message.send()) {
            reported[i] = errors[i]; printed = true; last = now;
        }
        break;
    }
    errno = saved;
}
} // namespace display_rotation
