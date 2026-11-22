"""Static integration contracts; these do not simulate RTOS, USB or the RTC."""
from pathlib import Path
import ast
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
GNSS = (ROOT / "src/gnss_service.cpp").read_text(encoding="utf-8")
BASE = "2673b4559fcc56c4f883bb2d2d4791dec234ae77"

def original(path):
    return subprocess.check_output(["git", "show", BASE + ":" + path], cwd=ROOT).decode("utf-8")

def function(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", source)
    if not match:
        raise AssertionError("Missing function " + name)
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

def compact(text):
    return re.sub(r"\s+", "", text)

def literal(source, variable):
    match = re.search(r"\b" + variable + r"\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);", source)
    if not match:
        raise AssertionError("Missing literal " + variable)
    return "".join(ast.literal_eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))

class SourceContracts(unittest.TestCase):
    def test_acceptance_path_is_unchanged_except_fifo_admission(self):
        current = function(GNSS, "gnssTask")
        self.assertEqual(current.count("logFifo.push(record);"), 1)
        self.assertIn("publish(current);\n                            GnssLogRecord record;", current)
        without_fifo = re.sub(r"\s*GnssLogRecord record;.*?fifoMutex.unlock\(\);", "", current, flags=re.S)
        self.assertEqual(compact(without_fifo), compact(function(original("src/gnss_service.cpp"), "gnssTask")))

    def test_existing_utc_rate_snapshot_and_ui_code_are_preserved(self):
        old_gnss = original("src/gnss_service.cpp")
        for name in ["pvtUtc", "verifyRate", "setupGPS", "invalidate", "publish", "startGnss", "readGnssSnapshot"]:
            self.assertEqual(function(GNSS, name), function(old_gnss, name), name)
        for name in ["displayTask", "sensorTask", "rpmISR", "rpmSensorTask", "microsToRPM"]:
            self.assertEqual(function(MAIN, name), function(original("src/main.cpp"), name), name)

    def test_legacy_schema_and_measurement_expressions(self):
        previous = original("src/main.cpp")
        self.assertEqual(literal(MAIN, "rpmHeader"), literal(previous, "rpmHeader"))
        self.assertEqual(literal(MAIN, "sensorHeader"), literal(previous, "sensorHeader"))
        self.assertEqual(len(literal(MAIN, "sensorHeader").rstrip("\n").split("\t")), 10)
        for variable in ["rline", "sline"]:
            pattern = r"String " + variable + r"\s*=\s*(.*?);"
            self.assertEqual(compact(re.search(pattern, MAIN, re.S)[1]),
                             compact(re.search(pattern, previous, re.S)[1]))
        self.assertNotIn('fopen(path, "w', MAIN)
        self.assertIn('if (info.st_size != 0) return mediumUnchanged();', function(MAIN, "ensureLegacyHeader"))

    def test_additive_versioned_format_and_original_units(self):
        header = literal(MAIN, "GNSS_HEADER").splitlines()
        self.assertEqual(header[0], "# GNSS_LOG_V1")
        self.assertEqual(len(header[1].split("\t")), 17)
        formatter = function(MAIN, "formatGnssRecord")
        self.assertIn("if (r.timeValid)", formatter)
        self.assertIn("if (r.positionValid)", formatter)
        self.assertIn("if (r.altitudeValid)", formatter)
        self.assertNotIn("logClock", formatter)
        self.assertIn("record.utcMs = int64_t(current.utcSeconds) * 1000 + current.utcNanoseconds / 1000000;", GNSS)

    def test_callbacks_only_publish_atomic_events(self):
        self.assertEqual(compact(function(MAIN, "usbAttachHandler")), "{usbAttachedFlag.store(true);}")
        self.assertEqual(compact(function(MAIN, "usbDetachHandler")),
                         "{usbAttachedFlag.store(false);usbDetachGeneration.fetch_add(1);}")
        self.assertIn("ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2", MAIN)
        self.assertIn("usbDetachGeneration.load() == mountedGeneration", function(MAIN, "mediumUnchanged"))
        self.assertNotIn("msd.connect", function(MAIN, "setup"))

    def test_mount_gate_exclusive_names_and_recovery(self):
        mount = function(MAIN, "mountUSB")
        self.assertIn("if (code == 0)", mount)
        self.assertNotIn("ensureLegacyHeader", mount)
        segment = function(MAIN, "createGnssSegment")
        self.assertIn("gnss_log::findSegment(nextGnssSegment", segment)
        self.assertIn("O_WRONLY | O_CREAT | O_EXCL", segment)
        self.assertIn("if (errno == EEXIST) return gnss_log::SegmentResult::Exists;", segment)
        self.assertIn("return gnss_log::SegmentResult::IoError;", segment)
        self.assertIn("gnss_log::writeOpened", segment)
        self.assertNotIn("O_TRUNC", segment)
        self.assertIn("gnssPath[0] = 0;", function(MAIN, "unmountUSB"))
        logger = function(MAIN, "loggingTask")
        self.assertIn("if (usbMountedFlag.load() && !unmountPending && mediumUnchanged())", logger)
        self.assertIn("if (!failed && !gnssPath[0] && nextGnssSegment < gnss_log::SEGMENT_LIMIT)", logger)
        self.assertIn("failed = gnss_log::sharedSegmentFailure(segment, mediumUnchanged());", logger)
        self.assertNotIn("failed = !newSegment", logger)
        self.assertIn("if (!failed && gnssPath[0] && mediumUnchanged())", logger)
        self.assertIn("reserveGnssLog(gnssWork, gnss_log::BLOCK, millis())", logger)
        self.assertNotIn("reserveGnssLog(gnssWork, gnss_log::BLOCK, now)", logger)
        legacy_gate = "if (!failed && !unmountPending && headersReady && usbMountedFlag.load() && mediumUnchanged())"
        self.assertIn(legacy_gate, logger)
        for call in ["safeFileWrite(FILE_RPM_PATH", "safeFileWrite(FILE_SENSOR_PATH"]:
            self.assertLess(logger.index(legacy_gate), logger.index(call))
            self.assertLess(logger.index(call), logger.index("reserveGnssLog("))
        self.assertIn("completeGnssLog(gnssWork[0].sequence, count, ok)", logger)
        self.assertIn("}) && mediumUnchanged();", logger)
        self.assertIn("failed = !completed || !mediumUnchanged();", logger)
        self.assertRegex(logger, r"if \(!ok\) \{\s*gnssPath\[0\] = 0;")
        self.assertRegex(logger, r"if \(!reportGnssStats\(toFile\)\) \{\s*gnssPath\[0\] = 0;[^\n]*\n\s*failed = !mediumUnchanged\(\);")
        # Neither failed GNSS creation nor GNSS stats may skip legacy sampling.
        before_legacy = logger[logger.index("const auto segment"):logger.index(legacy_gate)]
        self.assertNotIn("continue;", before_legacy)
        self.assertNotIn("unmountUSB();", before_legacy)
        self.assertIn("rpmBuffer[rpmBufCount++] = rline;", logger)
        self.assertIn("sensorBuffer[sensorBufCount++] = sline;", logger)
        self.assertLess(logger.index("rpmBuffer[rpmBufCount++]"), logger.index("safeFileWrite(FILE_RPM_PATH"))
        self.assertLess(logger.index("sensorBuffer[sensorBufCount++]"), logger.index("safeFileWrite(FILE_SENSOR_PATH"))
        # A mount failure cannot publish mounted=true or reach header/write gates.
        mount_success, mount_error = mount.split("} else {", 1)
        self.assertIn("usbMountedFlag.store(true)", mount_success)
        self.assertNotIn("usbMountedFlag.store(true)", mount_error)
        self.assertIn("usbFaultFlag.store(true)", mount_error)
        self.assertIn("usbMountedFlag.load() && (unmountPending || !mediumUnchanged())", logger)
        self.assertIn("!usbMountedFlag.load() && uint32_t(now - lastMount) >= 1000", logger)
        self.assertIn("unmountPending = code != 0;", function(MAIN, "unmountUSB"))
        self.assertIn("usbMountedFlag.store(unmountPending)", function(MAIN, "unmountUSB"))
        # Checked legacy failures still set shared failure and suppress SENSOR.
        for path, count in [("FILE_RPM_PATH", "rpmBufCount"), ("FILE_SENSOR_PATH", "sensorBufCount")]:
            block = logger[logger.index("const bool ok = safeFileWrite(" + path):]
            block = block[:block.index("failed = !ok;") + len("failed = !ok;")]
            self.assertIn("if (!ok) legacyUncertain += " + count, block)
            self.assertIn(count + " = 0;", block)
        self.assertIn("if (!failed && sensorBufCount", logger)
        shared_recovery = logger[logger.index("if (failed) {"):]
        self.assertIn("if (usbMountedFlag.load()) unmountUSB();", shared_recovery)
        self.assertIn("headersReady = false;", shared_recovery)
        checked_legacy = function(MAIN, "safeFileWrite")
        self.assertIn("gnss_log::writeFile", checked_legacy)
        self.assertIn("return ok && mediumUnchanged();", checked_legacy)
        self.assertRegex(logger, r"if \(failed\) \{\s*usbFaultFlag.store\(true\);")

    def test_statistics_and_partial_buffers_have_independent_deadlines(self):
        logger = function(MAIN, "loggingTask")
        self.assertIn("newSegment || uint32_t(now - lastStats) >= 1000", logger)
        self.assertIn("uint32_t(now - rpmBufferStart) >= 1000", logger)
        self.assertIn("uint32_t(now - sensorBufferStart) >= 1000", logger)
        self.assertLess(logger.index("reportGnssStats(toFile)"), logger.index("reserveGnssLog("))
        stats = function(MAIN, "reportGnssStats")
        for field in ["accepted", "written", "queued", "highWater", "overflow", "uncertain"]:
            self.assertIn("s." + field, stats)
        self.assertIn("Serial.print(gnssLine)", stats)
        self.assertIn("if (!toFile) return true;", stats)
        self.assertIn("gnssSegmentNext=%lu gnssSegmentExhausted=%u", stats)

    def test_fifo_lock_regions_do_not_include_io_or_snapshot_lock(self):
        self.assertEqual(GNSS.count("gnss_log::Fifo logFifo;"), 1)
        for region in re.findall(r"fifoMutex.lock\(\);(.*?)fifoMutex.unlock\(\);", GNSS, re.S):
            for forbidden in ["snapshotMutex", "Serial", "fopen", "snprintf", "sleep_for"]:
                self.assertNotIn(forbidden, region)
        self.assertNotIn("logFifo =", GNSS)

if __name__ == "__main__":
    unittest.main(verbosity=2)
