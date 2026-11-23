"""Static integration contracts; these do not simulate RTOS, USB or the RTC."""
from pathlib import Path
import ast
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
GNSS = (ROOT / "src/gnss_service.cpp").read_text(encoding="utf-8")
IO_DIAG = (ROOT / "src/usb_io_diag.h").read_text(encoding="utf-8")
HEAP_CPP = (ROOT / "src/heap_diag.cpp").read_text(encoding="utf-8")
HEAP_HEADER = (ROOT / "src/heap_diag.h").read_text(encoding="utf-8")
BASE = "2673b4559fcc56c4f883bb2d2d4791dec234ae77"

def original(path, revision=BASE):
    return subprocess.check_output(["git", "show", revision + ":" + path], cwd=ROOT).decode("utf-8")

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

def cpp_tokens(text):
    """Yield code tokens with spans; delimiters in C++ literals/comments are opaque."""
    i = 0
    while i < len(text):
        if text[i].isspace():
            i += 1
            continue
        start = i
        if text.startswith('//', i):
            end = text.find('\n', i + 2)
            i = len(text) if end < 0 else end
            continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('Unterminated C++ comment')
            i = end + 2
            continue
        raw = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[i:])
        if raw:
            closing = ')' + raw[1] + '"'
            end = text.find(closing, i + raw.end())
            if end < 0:
                raise ValueError('Unterminated raw string')
            i = end + len(closing)
        else:
            quoted = re.match(r'''(?:u8|u|U|L)?(["'])''', text[i:])
            if quoted:
                quote = quoted[1]
                i += quoted.end()
                while i < len(text):
                    if text[i] == '\\':
                        i += 2
                    elif text[i] == quote:
                        i += 1
                        break
                    else:
                        i += 1
                else:
                    raise ValueError('Unterminated quoted literal')
            else:
                word = re.match(r'[A-Za-z_][A-Za-z_0-9]*|[0-9]+', text[i:])
                i += word.end() if word else 1
        yield text[start:i], start, i


def without_serial_gates(text):
    """Unwrap only GNSS level-1 single println calls; preserve their entire payload.

    This is not a preprocessor: other macros/levels are intentionally retained so
    the historical functional comparison rejects any unexpected change in scope.
    """
    tokens = list(cpp_tokens(text))
    edits = []
    for index, (token, start, _) in enumerate(tokens):
        if token != 'MOFA_DIAG' or index + 1 == len(tokens) or tokens[index + 1][0] != '(':
            continue
        stack = ['(']
        boundaries = [tokens[index + 1][2]]
        closing = None
        for j in range(index + 2, len(tokens)):
            value, begin, end = tokens[j]
            if value in ('(', '[', '{'):
                stack.append(value)
            elif value in (')', ']', '}'):
                if not stack or stack.pop() != {')': '(', ']': '[', '}': '{'}[value]:
                    raise ValueError('Unbalanced MOFA_DIAG invocation')
                if not stack:
                    closing = (begin, end)
                    break
            elif value == ',' and len(stack) == 1:
                boundaries.extend([begin, end])
        if closing is None:
            raise ValueError('Unterminated MOFA_DIAG invocation')
        # Exactly three arguments; variadic payload commas must be nested.
        if len(boundaries) != 5:
            continue
        first = text[boundaries[0]:boundaries[1]]
        level = text[boundaries[2]:boundaries[3]]
        if [t[0] for t in cpp_tokens(first)] != ['MOFA_SUBSYS_GNSS'] or [t[0] for t in cpp_tokens(level)] != ['1']:
            continue
        payload = text[boundaries[4]:closing[0]].strip()
        code = [t[0] for t in cpp_tokens(payload)]
        if code[:4] != ['Serial', '.', 'println', '('] or code[-1:] != [')']:
            raise ValueError('Only the authorized GNSS println wrapper can be normalized')
        # Its opening parenthesis must close at the end: no extra statements.
        depth = 0
        for n, value in enumerate(code[3:], 3):
            if value == '(':
                depth += 1
            elif value == ')':
                depth -= 1
                if depth == 0 and n != len(code) - 1:
                    raise ValueError('Extra code after GNSS println')
        edits.append((start, closing[1], payload))
    for start, end, payload in reversed(edits):
        text = text[:start] + payload + text[end:]
    # Only this exact CFG-RATE serial gate is authorized; retain all inner bytes.
    return re.sub(r'^#if MOFA_DEBUG_GNSS >= 2\n((?:(?!^#).)*?)^#endif\n',
                  lambda match: match[1], text, flags=re.M | re.S)


def compact(text):
    return re.sub(r"\s+", "", without_serial_gates(text))

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
            self.assertEqual(without_serial_gates(function(GNSS, name)), function(old_gnss, name), name)
        for name in ["displayTask", "sensorTask", "rpmISR", "rpmSensorTask", "microsToRPM"]:
            current = function(MAIN, name)
            if name == "displayTask":
                # Authorized display-only diagnostic is the sole addition;
                # compare every remaining byte against the original task.
                self.assertEqual(current.count("display_rotation::reportErrors();"), 1)
                self.assertIn("sleep_ms = lv_task_handler();\n    display_rotation::reportErrors();", current)
                current = current.replace("    display_rotation::reportErrors();\n", "")
            self.assertEqual(current, function(original("src/main.cpp"), name), name)

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
        headers = function(MAIN, "ensureLegacyHeader")
        self.assertIn("if (result == 0 && info.st_size != 0)", headers)
        existing = headers[headers.index("if (result == 0 && info.st_size != 0)"):
                           headers.index("} else if (result != 0 && error != ENOENT)")]
        self.assertIn("event.mediumOk = mediumUnchanged();", existing)
        self.assertIn("return event.mediumOk;", existing)
        self.assertNotIn("gnss_log::writeFile", existing)
        self.assertIn("return ioOk && mediumOk;", headers)
        for branch in ["ExistingNonempty", "StatError", "HeaderWrite", "MediumChanged"]:
            self.assertIn("usb_io_diag::Context::" + branch, headers)
        self.assertIn("event.hasStat = result == 0;", headers)
        self.assertIn("event.fileSize = info.st_size;", headers)
        self.assertIn("event.fileType = info.st_mode & S_IFMT;", headers)
        self.assertIn("usbIoFirst.event.fileSize = event.fileSize;", headers)
        self.assertIn("usbIoFirst.event.fileType = event.fileType;", headers)
        self.assertRegex(headers, r"const int result = stat\(path, &info\);\s*const int error = errno;")
        stat_error = headers[headers.index("} else if (result != 0 && error != ENOENT)"):
                             headers.index('FileOps io(path, "a",')]
        self.assertIn("return false;", stat_error)
        self.assertNotIn("gnss_log::writeFile", stat_error)

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
        self.assertIn("decimalUnsigned(sequence, sizeof(sequence), r.sequence)", formatter)
        self.assertIn("decimalSigned(utc, sizeof(utc), r.utcMs)", formatter)
        record_format = ast.literal_eval(re.search(
            r'const int n = snprintf\(gnssLine, sizeof\(gnssLine\),\s*("[^"\n]+")', formatter)[1])
        self.assertEqual(record_format.rstrip("\n").split("\t"),
                         ["%s"] + ["%lu"] * 5 + ["%s"] + ["%u"] * 4 + ["%s"] * 6)
        self.assertIn("char utc[24] = {}, lat[16] = {}, lon[16] = {}, alt[16] = {};", formatter)
        self.assertIn("char speed[16] = {}, course[16] = {}, accuracy[16] = {};", formatter)
        self.assertIn("n >= 0 && size_t(n) < sizeof(gnssLine) ? gnssLine : nullptr", formatter)
        # All 17 columns at maximum decimal widths still fit the bounded line.
        widths = [20] + [10] * 5 + [20] + [3] * 4 + [11] * 5 + [10]
        self.assertLess(sum(widths) + 16 + 1, 384)
        self.assertNotRegex(MAIN, r'%[-+ #0-9.*]*(?:ll|j)[diuoxX]')

    def test_callbacks_only_publish_atomic_events(self):
        self.assertEqual(compact(function(MAIN, "usbAttachHandler")), "{usbAttachedFlag.store(true);}")
        self.assertEqual(compact(function(MAIN, "usbDetachHandler")),
                         "{usbAttachedFlag.store(false);usbDetachGeneration.fetch_add(1);}")
        self.assertIn("ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2", MAIN)
        self.assertIn("usbDetachGeneration.load() == mountedGeneration", function(MAIN, "mediumUnchanged"))
        self.assertNotIn("msd.connect", function(MAIN, "setup"))
        setup = function(MAIN, "setup")
        self.assertLess(setup.index("ui_init();"), setup.index("heap_diag::initialize();"))
        for start in ["displayThread.start", "sensorThread.start", "loggingThread.start", "rpmSensorThread.start", "startGnss()"]:
            self.assertLess(setup.index("heap_diag::initialize();"), setup.index(start))
        initialize = function(HEAP_CPP, "initialize")
        self.assertLess(initialize.index("mbed_mem_trace_lock();"),
                        initialize.index("mbed_mem_trace_set_callback(allocationTrace);"))
        self.assertLess(initialize.index("mbed_mem_trace_set_callback(allocationTrace);"),
                        initialize.index("mbed_mem_trace_unlock();"))
        callback = re.sub(r"//[^\n]*", "", function(HEAP_CPP, "allocationTrace"))
        for forbidden in [r"\bSerial\b", r"\bmillis\s*\(", r"\breadStats\s*\(",
                          r"\bmbed_stats_heap_get\s*\(", r"\b(?:malloc|calloc|realloc|free|new|delete)\b",
                          r"\b(?:lock|unlock|sleep_for|snprintf|printSnapshot)\s*\("]:
            self.assertNotRegex(callback, forbidden)
        self.assertIn("if (result || operation == MBED_MEM_TRACE_FREE) return;", callback)
        self.assertIn("if (!failure.count || !failure.size)", callback)
        for operation in ["MALLOC", "REALLOC", "CALLOC"]:
            self.assertIn("MBED_MEM_TRACE_" + operation, callback)
        self.assertIn("const uint32_t head = failureHead.load(std::memory_order_relaxed)", callback)
        self.assertIn("failureTail.load(std::memory_order_acquire)", callback)
        self.assertLess(callback.index("failures[head % FAILURES] = failure;"),
                        callback.index("failureHead.store(head + 1, std::memory_order_release);"))
        self.assertIn("if (lost != UINT32_MAX) failureLost.store(lost + 1, std::memory_order_relaxed);", callback)
        self.assertIn("ATOMIC_INT_LOCK_FREE == 2", HEAP_CPP)

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
        self.assertIn("const bool mediumOk = mediumUnchanged();", logger)
        self.assertIn("const bool ok = ioOk && mediumOk;", logger)
        self.assertLess(logger.index("const bool mediumOk = mediumUnchanged();"),
                        logger.index("completeGnssLog("))
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
        self.assertIn("usbMountedFlag.store(usbMount.usable)", mount_success)
        self.assertNotIn("usbMountedFlag.store(true)", mount_error)
        self.assertIn("usbFaultFlag.store(true)", mount_error)
        self.assertIn("usbMount.cleanupDue(now, mediumUnchanged())", logger)
        self.assertIn("usbMount.mountDue(now, lastMount)", logger)
        self.assertIn("usbMount.cleaned(code);", function(MAIN, "unmountUSB"))
        self.assertIn("unmountPending = usbMount.pending;", function(MAIN, "unmountUSB"))
        self.assertIn("usbMountedFlag.store(false)", function(MAIN, "unmountUSB"))
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
        self.assertIn("const bool mediumOk = mediumUnchanged();", checked_legacy)
        self.assertIn("return ok && mediumOk;", checked_legacy)
        self.assertRegex(logger, r"if \(failed\) \{\s*usbFaultFlag.store\(true\);")
        # Instrumentation may observe these operations but adds no I/O attempts.
        for expression in [r"\bfopen\(", r"\bfwrite\(", r"\bfflush\(", r"\bfclose\(",
                           r"\bstat\(", r"\bfdopen\(", r"::open\(", r"::close\(",
                           r"fs\.mount\(", r"fs\.unmount\(", r"msd\.connect\("]:
            self.assertEqual(len(re.findall(expression, MAIN)),
                             len(re.findall(expression, original("src/main.cpp", "7b85488"))), expression)
        for name, call in [("mountUSB", "fs.mount(&msd)"), ("unmountUSB", "fs.unmount()")]:
            body = function(MAIN, name)
            self.assertIn(f"const int code = {call};\n  const int error = errno;", body)
            self.assertLess(body.index("event.after = usbIoState()"), body.index("usbIoReport(event)"))
            self.assertIn("errno = error;", body)
            self.assertLess(body.index("const int error = errno;"), body.index("heap_diag::ioError("))
        opened = function(MAIN, "open")
        self.assertLess(opened.index("heap_diag::beforeOpen(p)"), opened.index("fopen(p, m)"))
        failed_open = opened[opened.index("if (!file)"):opened.index("errno = error;")]
        self.assertIn("heap_diag::openFailed(heapToken, p, error);", failed_open)
        self.assertNotIn("afterClose", failed_open)
        closed = function(MAIN, "close")
        self.assertLess(closed.index("fclose(f)"), closed.index("heap_diag::afterClose(heapToken, path)"))
        self.assertLess(closed.index("const int error = errno;"), closed.index("heap_diag::afterClose("))
        self.assertLess(segment.index("heap_diag::beforeOpen(gnssPath)"), segment.index("::open("))
        self.assertLess(segment.index("::close(fd)"), segment.index("heap_diag::afterClose(io.heapToken, gnssPath)"))
        self.assertIn("heap_diag::openFailed(io.heapToken, gnssPath, openError)", segment)
        mount = function(MAIN, "mountUSB")
        self.assertLess(mount.index("heap_diag::beforeMount()"), mount.index("fs.mount(&msd)"))
        self.assertLess(mount.index("fs.mount(&msd)"), mount.index("heap_diag::afterMount(heapToken, code == 0)"))
        self.assertIn("heap_diag::afterUnmount(code == 0)", function(MAIN, "unmountUSB"))
        self.assertIn("heap_diag::startRound();", logger)
        self.assertIn("heap_diag::finishRound(!failed && headersReady && gnssPath[0] && !unmountPending &&", logger)
        finish_round = function(HEAP_CPP, "finishRound")
        self.assertIn("if (!baselineSet && ready && roundGood && roundClosed)", finish_round)
        self.assertIn("roundBaseline = tokenFor(snapshot);", finish_round)
        self.assertIn("roundClosed = true;", function(HEAP_CPP, "afterClose"))
        self.assertIn("roundGood = roundGood && success;", function(HEAP_CPP, "operationFinished"))

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
        for variable, field in [("accepted", "s.accepted"), ("written", "s.written"),
                                ("overflow", "s.overflow"), ("uncertain", "s.uncertain"),
                                ("legacy", "legacyUncertain")]:
            self.assertIn(f"decimalUnsigned({variable}, sizeof({variable}), {field})", stats)
        for field in ["accepted", "written", "overflow", "uncertain", "legacyUncertain"]:
            self.assertIn(field + "=%s", stats)
        stats_format = ast.literal_eval(re.search(
            r'const int n = snprintf\(gnssLine, sizeof\(gnssLine\),\s*("[^"\n]+")', stats)[1])
        stats_max = re.sub(r"%s", "18446744073709551615", stats_format)
        stats_max = re.sub(r"%lu", "4294967295", stats_max).replace("%u", "1")
        self.assertLess(len(stats_max), 384)
        self.assertIn('loggerDiagnostic("stats-write", "before")', stats)
        self.assertIn('loggerDiagnostic("stats-write", "after", ok)', stats)
        report = function(MAIN, "usbIoReport")
        report_format = ast.literal_eval(re.search(
            r'snprintf\(line, sizeof\(line\),\s*("[^"\n]+")', report)[1])
        # Longest names, owned path/mode capacities, full ARM integer ranges.
        values = iter(["close-fd", "p" * 31, "m" * 3, "existing-nonempty", "existing-nonempty",
                       "-2147483648", "-2147483648", "-2147483648", "after-call"] +
                      ["4294967295"] * 4 + ["1", "-9223372036854775808", "4294967295", "1", "1"])
        longest = re.sub(r"%lu|%ld|%[sdu]", lambda _: next(values), report_format)
        self.assertEqual(len(longest) + 1, 328)
        self.assertLessEqual(len(longest) + 1, 384)
        self.assertIn("static char line[384], sizeText[21]", report)
        self.assertIn("decimalSigned(sizeText, sizeof(sizeText), event.fileSize)", report)
        for field in ["op", "path", "mode", "ctx", "branch", "rc", "errno", "ferror", "ferrPhase",
                      "row", "requested", "actual", "repeat", "stat", "size", "type", "io", "medium"]:
            self.assertIn(field + "=", report_format)
        for field in ["attached", "mounted", "unmountPending", "fault", "detachGeneration", "mountedGeneration", "headersReady"]:
            self.assertIn(field + "=", report)
        self.assertIn('states("pre", event.before);', report)
        self.assertIn('states("post", event.after);', report)
        self.assertIn('event.streamError < 0 ? "n/a"', report)
        self.assertIn('Operation::Close ? "pre-close" : "after-call"', report)
        heap_stats = function(HEAP_CPP, "readStats")
        self.assertIn("mbed_stats_heap_get(&stats);", heap_stats)
        self.assertIn("return {stats.current_size, stats.max_size, stats.alloc_cnt, stats.alloc_fail_cnt,", heap_stats)
        self.assertIn("stats.overhead_size, stats.reserved_size};", heap_stats)
        for forbidden in ["mallinfo", "__malloc_free_list", "malloc_addblock", "#define MBED_"]:
            self.assertNotIn(forbidden, HEAP_CPP + HEAP_HEADER)
        printer = function(HEAP_CPP, "printSnapshot")
        for field in ["current", "max", "alloc", "fail", "overhead", "reserved", "delta", "deltaAlloc", "baselineDelta"]:
            self.assertIn(field + "=", printer)
        for expression in ["snapshot.delta", "snapshot.allocationDelta", "roundBaseline.current"]:
            self.assertIn(expression, printer)
        self.assertNotRegex(HEAP_CPP, r'%[-+ #0-9.*]*(?:ll|j)[diuoxX]')
        snapshot_format = ast.literal_eval(re.search(
            r'snprintf\(line, sizeof\(line\),\s*("[^"\n]+")', printer)[1])
        values = iter(["first-ENOMEM-post-cleanup", "4294967295", "first-successful-round", "p" * 31] +
                      ["4294967295"] * 7 + ["-9223372036854775808"] * 2 +
                      ["1", "-9223372036854775808", "-2147483648"])
        longest_snapshot = re.sub(r"%lu|%[sdu]", lambda _: next(values), snapshot_format)
        self.assertLessEqual(len(longest_snapshot) + 1, 384)
        pending = function(HEAP_CPP, "reportPending")
        allocation_format = ast.literal_eval(re.search(
            r'snprintf\(line, sizeof\(line\),\s*("[^"\n]+")', pending)[1])
        values = iter(["255", "18446744073709551615", "4294967295", "4294967295", "ffffffff", "4294967295"])
        longest_allocation = re.sub(r"%lu|%lx|%[su]", lambda _: next(values), allocation_format)
        self.assertLessEqual(len(longest_allocation) + 1, 384)
        self.assertIn("char line[384]", HEAP_CPP)
        after_close = function(HEAP_CPP, "afterClose")
        self.assertIn("printedPairs < 2 || uint32_t(now - lastPairPrint) >= 30000", after_close)
        self.assertIn("recent.at(i).id == token.id && recent.at(i).point == Point::BeforeOpen", after_close)
        for name in ["afterMount", "afterUnmount"]:
            self.assertIn("printed < 2 || uint32_t(now - last) >= 5000", function(HEAP_CPP, name))
        self.assertIn("if (printed && uint32_t(now - last) < 5000) return;", pending)
        self.assertIn("tail != head && n < FAILURES", pending)

    def test_fifo_lock_regions_do_not_include_io_or_snapshot_lock(self):
        self.assertEqual(GNSS.count("gnss_log::Fifo logFifo;"), 1)
        for region in re.findall(r"fifoMutex.lock\(\);(.*?)fifoMutex.unlock\(\);", GNSS, re.S):
            for forbidden in ["snapshotMutex", "Serial", "fopen", "snprintf", "sleep_for", "loggerDiagnostic", "heap_diag"]:
                self.assertNotIn(forbidden, region)
        self.assertNotIn("logFifo =", GNSS)
        diagnostic = function(MAIN, "loggerDiagnostic")
        self.assertIn("[LOGGER DIAG]", diagnostic)
        for api in ["stack_size", "free_stack", "max_stack"]:
            self.assertIn("loggingThread." + api + "()", diagnostic)
        trace = MAIN[MAIN.index("struct LoggerTrace"):MAIN.index("void loggerDiagnostic")]
        self.assertIn("initial < 6", trace)
        self.assertIn("uint32_t(now - last) >= 5000", trace)
        for name, operation in [("createGnssSegment", "segment"), ("mountUSB", "mount"),
                                ("unmountUSB", "unmount")]:
            body = function(MAIN, name)
            self.assertIn(f'loggerDiagnostic("{operation}", "before"', body)
            self.assertIn('"after"', body)
        for name in ["safeFileWrite", "ensureLegacyHeader"]:
            body = function(MAIN, name)
            self.assertIn("trace.sample()", body)
            self.assertIn("before-", body)
            self.assertIn("after-", body)
        logger = function(MAIN, "loggingTask")
        for operation in ["fifo-reserve", "gnss-block"]:
            self.assertIn(f'loggerDiagnostic("{operation}", "before"', logger)
            self.assertIn(f'loggerDiagnostic("{operation}", "after"', logger)
        self.assertLess(logger.index('loggerDiagnostic("fifo-reserve", "before")'), logger.index("reserveGnssLog("))
        self.assertLess(logger.index("reserveGnssLog("), logger.index('loggerDiagnostic("fifo-reserve", "after"'))
        self.assertIn("diagnosticHeadersReady = &headersReady;", logger)
        state = function(MAIN, "usbIoState")
        for expression in ["usbAttachedFlag.load()", "usbMountedFlag.load()", "unmountPending",
                           "usbFaultFlag.load()", "diagnosticHeadersReady && *diagnosticHeadersReady",
                           "usbDetachGeneration.load()", "mountedGeneration"]:
            self.assertIn(expression, state)
        for name in ["usbIoReport", "loggerDiagnostic", "capture", "finish"]:
            self.assertIn("PreserveErrno preserve;", function(MAIN, name))
        self.assertIn("int saved = errno;", MAIN)
        self.assertIn("~PreserveErrno() { errno = saved; }", MAIN)
        for name, call in [("open", "const Handle file = fopen(p, m);"),
                           ("write", "const size_t actual = fwrite(data, 1, size, f);"),
                           ("flush", "const int result = fflush(f);"),
                           ("close", "const int result = fclose(f);")]:
            body = function(MAIN, name)
            self.assertIn(call + "\n    const int error = errno;", body)
            self.assertIn("errno = error;", body)
        for name in ["write", "flush"]:
            body = function(MAIN, name)
            self.assertLess(body.index("const int error = errno;"), body.index("ferror(f)"))
            self.assertLess(body.index("ferror(f)"), body.index("capture("))
        writer = function(MAIN, "write")
        self.assertLess(writer.index("capture("), writer.index("++row;"))
        self.assertIn("return actual == size && !streamError;", writer)
        closer = function(MAIN, "close")
        self.assertLess(closer.index("ferror(f)"), closer.index("fclose(f)"))
        after_close = re.sub(r"//[^\n]*", "", closer[closer.index("fclose(f)") + len("fclose(f)"):])
        self.assertNotRegex(after_close, r"\bf\b")
        self.assertIn("return result == 0;", closer)
        self.assertIn("usbIoFirst.reset();", MAIN)
        finish = function(MAIN, "finish")
        self.assertIn("if (!mediumOk && !usbIoFirst.valid)", finish)
        self.assertIn("usbIoFirst.event.after = usbIoState();", finish)
        self.assertNotIn("usbIoFirst.event.before =", finish)
        self.assertIn("usb_io_diag::copy(event.path, path)", function(MAIN, "capture"))
        self.assertIn("if (entry.count != UINT32_MAX) ++entry.count;", IO_DIAG)
        self.assertIn("static constexpr size_t SLOTS = 8;", IO_DIAG)
        segment = function(MAIN, "createGnssSegment")
        self.assertRegex(segment, r"const int fd = ::open\([^;]+;\s*const int openError = errno;")
        self.assertRegex(segment, r'FILE \*file = fdopen\(fd, "w"\);\s*const int fdopenError = errno;')
        self.assertRegex(segment, r"const int closeResult = ::close\(fd\);\s*const int closeError = errno;")
        self.assertIn("if (openError != EEXIST)", segment)
        capture = function(MAIN, "capture")
        self.assertIn("heap_diag::ioError(path, operation == usb_io_diag::Operation::Medium ? 0 : error, heapToken)", capture)
        self.assertIn("heap_diag::reportPending();", function(MAIN, "finish"))
        errors = function(HEAP_CPP, "ioError")
        self.assertIn("if (!errorFrozen)", errors)
        self.assertIn("if (error == ENOMEM && !enomemFrozen)", errors)
        self.assertLess(errors.index("capture(Point::IoError"), errors.index("firstError = recent;"))
        self.assertLess(errors.index("capture(Point::Enomem"), errors.index("firstEnomem = recent;"))
        self.assertNotIn("print", errors)
        pending = function(HEAP_CPP, "reportPending")
        for group in ["first-error", "first-ENOMEM"]:
            self.assertIn(group + "-frozen", pending)
            self.assertIn(group + "-post-cleanup", pending)
        self.assertIn("capture(Point::AfterErrorCleanup", pending)
        self.assertNotIn("firstError.push", HEAP_CPP)
        self.assertNotIn("firstEnomem.push", HEAP_CPP)
        self.assertIn("failureHead.load(std::memory_order_acquire)", pending)
        self.assertLess(pending.index("const AllocationFailure failure = failures[tail % FAILURES];"),
                        pending.index("failureTail.store(tail + 1, std::memory_order_release);"))
        self.assertIn("uint64_t(failure.count) * failure.size", pending)
        self.assertIn("callerReliable=0", pending)
        self.assertIn("constexpr size_t HISTORY = 8, FAILURES = 8;", HEAP_CPP)
        heap_capture = HEAP_CPP[HEAP_CPP.index("Snapshot capture("):HEAP_CPP.index("Token tokenFor(")]
        self.assertIn("PreserveErrno preserve;", heap_capture)
        self.assertIn("i + 1 < sizeof(snapshot.path)", heap_capture)
        self.assertIn("snapshot.path[i] = '\\0';", heap_capture)


class SerialGateNormalization(unittest.TestCase):
    def test_literals_nested_calls_comments_and_multiple_wrappers(self):
        payloads = [
            'Serial.println("configuration failed; retrying")',
            r"""Serial.println(format("comma, semi; parens() escaped\" tail", nested((1), '(')))""",
            'Serial.println(R"tag(raw );, \\" text)tag")',
            'Serial.println(/* ), ; */ nested(1, 2))',
        ]
        wrapped = '\n'.join('MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, ' + p + ');' for p in payloads)
        self.assertEqual(without_serial_gates(wrapped), '\n'.join(p + ';' for p in payloads))

    def test_strings_comments_and_other_scopes_are_not_unwrapped(self):
        for text in ['"MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println(0))"',
                     '// MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println(0));\n',
                     'MOFA_DIAG(MOFA_SUBSYS_USB, 1, Serial.println(0));',
                     'MOFA_DIAG(MOFA_SUBSYS_GNSS, 2, Serial.println(0));']:
            self.assertEqual(without_serial_gates(text), text)

    def test_payload_mutation_remains_visible_and_extra_statements_are_rejected(self):
        old = 'Serial.println("old");'
        changed = 'MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println("new"));'
        self.assertNotEqual(without_serial_gates(changed), old)
        for text in ['MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, updateState());',
                     'MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println(0); mutate());',
                     'MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println("bad));']:
            with self.assertRaises(ValueError):
                without_serial_gates(text)

if __name__ == "__main__":
    unittest.main(verbosity=2)
