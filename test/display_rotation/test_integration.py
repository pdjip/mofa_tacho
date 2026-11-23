"""Static adapter/integration contracts; no LVGL/DMA hardware execution."""
from pathlib import Path
import hashlib
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
ADAPTER = (ROOT / "src/display_rotation.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/display_rotation_buffer.h").read_text(encoding="utf-8")


def body(source, name):
    match = re.search(r"\b" + name + r"\([^;{}]*\)\s*\{", source)
    if not match:
        raise AssertionError("Missing body " + name)
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RotationIntegration(unittest.TestCase):
    def test_setup_callback_registration_and_unique_project_name(self):
        setup = body(MAIN, "setup")
        # STEP 230 adds failure status while keeping installation conditional on begin success.
        self.assertIn("if (Display.begin() == 0) {", setup)
        self.assertIn("if (!display_rotation::install())", setup)
        self.assertIn("[F_DISPLAY_CALLBACK]", setup)
        self.assertIn("[F_DISPLAY_INIT]", setup)
        self.assertLess(setup.index("display_rotation::install()"), setup.index("ui_init();"))
        for thread in ["displayThread.start", "sensorThread.start", "loggingThread.start", "rpmSensorThread.start", "startGnss()"]:
            self.assertLess(setup.index("display_rotation::install()"), setup.index(thread))
        install = body(ADAPTER, "install")
        self.assertIn("lv_display_get_default()", install)
        self.assertIn("if (!display)", install)
        self.assertIn("lv_display_set_flush_cb(display, projectDisplayFlush)", install)
        self.assertEqual(ADAPTER.count("void projectDisplayFlush("), 1)
        self.assertNotIn("lvgl_displayFlushing", ADAPTER)

    def test_adapter_rgb565_partial_and_source_bounds(self):
        callback = body(ADAPTER, "projectDisplayFlush")
        self.assertIn("LV_DISPLAY_RENDER_MODE_PARTIAL", callback)
        self.assertIn("request.preflight = Error::Display;", callback)
        self.assertIn("request.rgb565 = lv_display_get_color_format(display) == LV_COLOR_FORMAT_RGB565", callback)
        self.assertIn("if (!request.rgb565) request.preflight = Error::Format;", callback)
        self.assertIn("buffer->header.cf != LV_COLOR_FORMAT_RGB565", callback)
        self.assertIn("if (request.preflight == Error::None) request.preflight = Error::Source", callback)
        for expression in ["lv_display_get_offset_x(display)", "lv_display_get_offset_y(display)",
                           "lv_display_get_original_horizontal_resolution(display)",
                           "lv_display_get_original_vertical_resolution(display)",
                           "buffer->header.w < request.width", "buffer->header.h < request.height",
                           "request.sourceStride = buffer->header.stride", "source < data", "data < base",
                           "source % alignof(uint16_t)", "source - base > buffer->data_size",
                           "request.sourceAvailable = buffer->data_size - (source - base)",
                           "frameBytes > UINTPTR_MAX - hardware.framebuffer"]:
            self.assertIn(expression, callback)
        # The same original coordinate gates now live in a diagnostic bitmask.
        mask = body(ADAPTER, "initialGeometryMask")
        conditions = ["p.area.x1 < 0", "p.area.y1 < 0", "p.area.x2 < p.area.x1", "p.area.y2 < p.area.y1",
                      "p.logicalWidth <= 0 || p.logicalHeight <= 0", "p.area.x2 >= p.logicalWidth",
                      "p.area.y2 >= p.logicalHeight", "p.originalWidth <= 0 || p.originalHeight <= 0",
                      "p.originalWidth > 0 && uint32_t(p.originalWidth) > p.request.framebufferWidth",
                      "p.originalHeight > 0 && uint32_t(p.originalHeight) > p.request.framebufferHeight",
                      "!p.rawOffsetZero"]
        for bit, condition in enumerate(conditions):
            self.assertIn(f"({condition} ? 1u << {bit} : 0)", mask)
        self.assertEqual(re.findall(r"1u << (\d+)", mask), [str(i) for i in range(11)])
        for assignment in ["parameters.logicalWidth = logicalWidth", "parameters.logicalHeight = logicalHeight",
                           "parameters.originalWidth = originalWidth", "parameters.originalHeight = originalHeight",
                           "parameters.offsetX = lv_display_get_offset_x(display)",
                           "parameters.offsetY = lv_display_get_offset_y(display)",
                           "parameters.offsetPhysicalWidth = lv_display_get_physical_horizontal_resolution(display)",
                           "parameters.offsetPhysicalHeight = lv_display_get_physical_vertical_resolution(display)"]:
            self.assertIn(assignment, callback)
        self.assertIn("parameters.rawOffsetZero = zeroRawOffsetFromPublic(request.rotation, parameters.offsetX, parameters.offsetY,", callback)
        self.assertIn("parameters.offsetPhysicalWidth, parameters.offsetPhysicalHeight);", callback)
        self.assertIn("parameters.rejectMask = initialGeometryMask(parameters)", callback)
        self.assertLess(callback.index("if (parameters.rejectMask)"), callback.index("area->x2 - area->x1"))
        mapped = {
            11: "destination.x1 < 0 || destination.y1 < 0",
            12: "destination.x2 < destination.x1 || destination.y2 < destination.y1",
            13: "destination.x2 >= 0 && uint32_t(destination.x2) >= request.framebufferWidth",
            14: "destination.y2 >= 0 && uint32_t(destination.y2) >= request.framebufferHeight",
        }
        for bit, condition in mapped.items():
            self.assertIn(f"if ({condition}) parameters.rejectMask |= 1u << {bit};", callback)
        self.assertIn("parameters.rejectMask |= 1u << 15;", callback)
        self.assertIn("lv_display_rotate_area(display, &destination)", callback)
        self.assertIn("flush(rotationBuffer, request, hardware, first ? &details : nullptr)", callback)
        self.assertIn("errno = saved;", callback)

    def test_only_ensure_resizes_and_every_flush_completes_once(self):
        self.assertEqual(len(re.findall(r"\brealloc\(", ADAPTER)), 1)
        self.assertIn("return static_cast<uint8_t *>(realloc(old, bytes));", ADAPTER)
        ensure = body(POLICY, "ensure")
        self.assertIn("if (data && required <= capacity) return true;", ensure)
        self.assertLess(ensure.index("uint8_t *replacement = ops.resize(data, required)"), ensure.index("if (!replacement)"))
        self.assertLess(ensure.index("if (!replacement)"), ensure.index("data = replacement"))
        flush = body(POLICY, "flush")
        self.assertEqual(flush.count("ops.ready();"), 1)
        self.assertNotIn("return", flush[:flush.index("ops.ready();")])
        self.assertLess(flush.index("buffer.ensure(plan.bytes, ops)"), flush.index("ops.rotate("))
        self.assertIn("if (error == Error::None) error = ops.dma(pixels, plan);", flush)
        self.assertIn("if (buffer.pinned) error = Error::Transfer;", flush)
        self.assertIn("request.sourceStride, plan.destinationStride", ADAPTER)
        self.assertIn("checkedDma.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565", ADAPTER)
        self.assertIn("memcpy(destination, request.source, plan.bytes)", ADAPTER)
        for name in ["projectDisplayFlush", "record", "failed"]:
            callback = body(ADAPTER, name)
            self.assertNotRegex(callback, r"\b(?:Serial|printf|snprintf|malloc|realloc|calloc)\b")
        self.assertIn("if (errors[index] != UINT32_MAX) ++errors[index];", body(ADAPTER, "record"))

    def test_diagnostics_after_handler_are_short_and_rate_limited(self):
        display = body(MAIN, "displayTask")
        self.assertIn("sleep_ms = lv_task_handler();\n    display_rotation::reportErrors();", display)
        report = body(ADAPTER, "reportErrors")
        self.assertIn("if (printed && uint32_t(now - last) < 5000) return;", report)
        sender = body(ADAPTER, "send")
        self.assertEqual(sender.count("Serial.write("), 1)
        self.assertIn("break;", report)
        self.assertIn("reported[i] = errors[i]; printed = true; last = now;", report)
        self.assertIn("char diagnosticLine[256];", ADAPTER)
        self.assertIn("if (!Serial) return false;", sender)
        self.assertIn("char digits[10]", body(ADAPTER, "number"))
        self.assertNotRegex(report, r"\b(?:String|malloc|realloc|calloc|printf|snprintf)\s*\(")
        self.assertNotIn("availableForWrite()", re.sub(r"//[^\n]*", "", sender))
        fixed = "[DISPLAY ROT DIAG] reason=" + " count=" + " need=" + " capacity=" + "\n"
        # All four values are ARM uint32; Serial.write uses length, not NUL.
        self.assertEqual(len(fixed) + 4 * 10, 90)
        self.assertLessEqual(len(fixed) + 4 * 10, 256)
        for method in ["text", "number"]:
            self.assertIn("length + 1 < sizeof(diagnosticLine)", body(ADAPTER, method))
        self.assertIn("diagnosticLine[length++] = '\\n'", sender)
        self.assertIn("Serial.write(reinterpret_cast<const uint8_t *>(diagnosticLine), length)", sender)

    def test_first_parameters_are_owned_and_reported_once_outside_flush(self):
        callback = body(ADAPTER, "projectDisplayFlush")
        report = body(ADAPTER, "reportErrors")
        self.assertIn("const bool first = !firstParameters.valid;", callback)
        self.assertIn("if (area) parameters.area = parameters.destination = *area;", callback)
        self.assertIn("lv_area_t area = {}, destination = {};", ADAPTER)
        self.assertIn("parameters.request = request;", callback)
        self.assertIn("parameters.valid = true;", callback)
        self.assertLess(callback.index("firstParameters = parameters;"), callback.index("flush(rotationBuffer"))
        self.assertLess(callback.index("flush(rotationBuffer"), callback.index("firstParameters.result = result;"))
        self.assertIn("firstParameters.policy = details.geometryReject;", callback)
        self.assertIn("static unsigned parameterPart = 0;", report)
        self.assertIn("while (firstParameters.valid && parameterPart < 3)", report)
        self.assertIn("if (!reportParameters(parameterPart))", report)
        self.assertIn("++parameterPart;", report)
        self.assertNotIn("parameterPart = 0", report[report.index("while ("):])
        parameters = body(ADAPTER, "reportParameters")
        for field in ["reason=", "mask=", "policy=", "rotation=", "mode=", "cf=", "logical=", "original=", "dsi=",
                      "area=", "mapped=", "rotated=", "offsetPublic=", "offsetPhysical=", "rawOffsetZero=",
                      "source=", "stride=", "bytes=", "available=",
                      "framebuffer=", "request=", "dest="]:
            self.assertIn(field, parameters)
        self.assertNotIn("display", parameters)
        self.assertNotIn("lv_", parameters)
        self.assertIn("return message.send();", parameters)
        for function in ["projectDisplayFlush", "dma", "record"]:
            self.assertNotIn("message.send", body(ADAPTER, function))
        # Generic writer bounds all parameter parts to 255 payload bytes plus
        # newline, including unexpected/corrupt full-width integer values.
        self.assertIn("while (*value && length + 1 < sizeof(diagnosticLine))", body(ADAPTER, "text"))
        self.assertIn("while (count && length + 1 < sizeof(diagnosticLine))", body(ADAPTER, "number"))

    def test_mapping_fixture_is_real_installed_lvgl_not_a_second_formula(self):
        folder = ROOT / ".pio/gnss-fifo-p3/tests"
        metadata = json.loads((folder / "lvgl_mapping_fixture.json").read_text(encoding="utf-8"))
        upstream_path = Path(metadata["source"])
        self.assertEqual(metadata["version"], "9.6.0")
        self.assertEqual(metadata["source_sha256"], hashlib.sha256(upstream_path.read_bytes()).hexdigest())
        upstream = upstream_path.read_text(encoding="utf-8")
        fixture = (folder / "lvgl_mapping_fixture.h").read_text(encoding="utf-8")
        self.assertEqual(len(metadata["body_sha256"]), 10)
        for name in metadata["body_sha256"]:
            self.assertEqual(body(upstream, name), body(fixture, name), name)
        self.assertIn("lv_display_rotate_area", metadata["body_sha256"])
        self.assertIn("lv_display_get_offset_x", metadata["body_sha256"])
        self.assertIn("lv_display_get_offset_y", metadata["body_sha256"])
        helper = body(POLICY, "zeroRawOffsetFromPublic")
        self.assertIn("rotation > 3 || physicalWidth <= 0 || physicalHeight <= 0", helper)
        self.assertIn("if (rotation < 2) return offsetX == 0 && offsetY == 0;", helper)
        self.assertIn("return offsetX == physicalWidth && offsetY == physicalHeight;", helper)
        self.assertNotIn("physicalWidth -", helper)
        self.assertNotIn("physicalHeight -", helper)

    def test_policy_acceptance_message_does_not_claim_dma_or_visible_success(self):
        dma = body(ADAPTER, "dma")
        self.assertLess(dma.index("dmaOwnership.transfer(transfer)"), dma.index("firstTransfer.returned = true;"))
        self.assertIn("if (!firstTransfer.returned)", dma)
        for assignment in ["firstTransfer.width = plan.width", "firstTransfer.height = plan.height", "firstTransfer.pixels = plan.bytes / 2"]:
            self.assertIn(assignment, dma)
        report = body(ADAPTER, "reportErrors")
        self.assertIn("firstTransfer.returned && !transferReported && parameterPart == 3", report)
        self.assertIn("checked-transfer w=", report)
        self.assertIn("firstTransfer.result", report)
        self.assertIn("visible-check=required", report)
        self.assertIn("transferReported = message.send();", report)
        self.assertLess(report.index("parameterPart < 3"), report.index("checked-transfer"))
        flush = body(POLICY, "flush")
        self.assertIn("if (details) *details = plan;", flush)
        self.assertLess(flush.index("if (details) *details = plan;"), flush.index("buffer.ensure("))
        kinds = ["Input", "WidthOverflow", "DestinationBounds", "OutputSizeOverflow", "FramebufferOverflow"]
        for kind in kinds:
            self.assertIn("geometryFailure(plan, GeometryReject::" + kind + ")", POLICY)


if __name__ == "__main__":
    unittest.main(verbosity=2)
