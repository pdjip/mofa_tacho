/** @file main.cpp
 * Application wiring and scheduling. ISR captures pulse timing only; logger owns filesystem calls, display owns LVGL, and dataMutex protects RPM/warning/temperature. Serial policy never gates fault handling or persistent records.
 *
 * Dependencies: Arduino/Mbed GPIO, Wire, FAT/USBHostMSD, DallasTemperature and LVGL adapters. Inputs are pulse edges, temperature and GNSS snapshots; outputs are pin-2 warning, UI model and checked USB records. Setup dispatches independent tasks; shutdown is not implemented, so recovery is task-local and boot-lifetime state is retained.
 */
#include "diagnostics_config.h"
/******************************************************************************
  Arduino GIGA R1 – Multi‐Sensor Data Logger with RTOS, USB‐Stick & Safe Mount
  Corrections for compile errors & deprecation warnings:
    • Use std::chrono::milliseconds for sleep_for()
    • Replace gnss.poll()/receive() with gnss.getPVT()
    • Drop msd.release(); unmount with FATFileSystem only
    • Use getAltitudeMSL() and getGroundSpeed()
******************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_USBHostMbed5.h>
#include <FATFileSystem.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <mbed.h>
#include <platform/mbed_critical.h>
#include <mbed_mktime.h>
#include <time.h>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <platform/mbed_retarget.h>
#include <sys/stat.h>

#include <lvgl.h>
#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"
#include "vars.h"
#include "rpm_display_filter.h"
#include "gnss_service.h"
#include "log_clock.h"
#include "usb_io_diag.h"
#include "usb_mount_state.h"
#include "heap_diag.h"
#include "display_rotation.h"

#include "ui/ui.h"
#include "ui/vars.h"

using namespace rtos;
using namespace std::chrono;

//=============================================================================
// 1. CONFIGURATION
//=============================================================================
#define PIN_RPM_IN 3
#define PIN_RPM_OUT 2
#define PIN_ONEWIRE_BUS 4
#define I2C_CLOCK_HZ 400000
#define I2C_POLL_WAIT_MS 10
#define RPM_HYST_ON 15000
#define RPM_HYST_OFF 14500
#define RPM_PULSE_PER_REV 2
#define TEMP_READ_HZ 1
#define SENSOR_LOG_HZ 10
#define USB_WRITE_BLOCK 20

#define FILE_RPM_PATH "/usb/DRZ.txt"
#define FILE_SENSOR_PATH "/usb/SENSOR.txt"

//=============================================================================
// 2. GLOBALS
//=============================================================================

constexpr uint32_t MINIMUM_RPM_PULSE_DELTA = 1000;
constexpr uint32_t RPM_PULSE_TIMEOUT_US = 150000;

USBHostMSD msd;
mbed::FATFileSystem fs("usb");
// Callback state uses lock-free atomics; callbacks never touch the filesystem.
std::atomic<bool> usbAttachedFlag{false};
std::atomic<bool> usbMountedFlag{false};
std::atomic<bool> usbFaultFlag{false};
std::atomic<uint32_t> usbDetachGeneration{0};
static_assert(ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2,
              "USB callbacks require lock-free atomics");
uint32_t mountedGeneration = 0; // Logger-owned below.
bool unmountPending = false;
usb_mount::State usbMount;
/** Logger-owned boot-lifetime active segment path. Empty disables GNSS consumption; the monotonically advancing candidate cursor is not reset by reconnect. Work/line buffers below must never escape into asynchronous callbacks. */
char gnssPath[32] = {};
uint32_t nextGnssSegment = 0;
GnssLogRecord gnssWork[gnss_log::BLOCK]; // Not on the 4-KiB logger stack.
char gnssLine[384];
uint64_t legacyUncertain = 0;
constexpr const char *GNSS_HEADER =
    "# GNSS_LOG_V1\n"
    "sequence\tsessionId\tsampleId\tiTOW_ms\treceivedMs\tepochDeltaMs\tUTC_ms\ttimeValid\tpositionValid\taltitudeValid\tfixType\tlat_e7\tlon_e7\thMSL_mm\tgSpeed_mm_s\theadMot_e5\thAcc_mm\n";

OneWire oneWire(PIN_ONEWIRE_BUS);
DallasTemperature sensors_DS(&oneWire);
bool tempOK = false;

volatile uint32_t lastPulseTime = 0;
volatile uint32_t delta_rpm = 0;

/** Short-copy lock for current RPM/warning/temperature. Writers are sensor/RPM tasks; display/logger readers release before formatting, rendering or I/O. */
rtos::Mutex dataMutex;
float curRPM = 0.0f;
bool curRpmWarning = false; // Published with curRPM under dataMutex.
float curTempC = 0.0f;

/** Logger-only heap-backed legacy rows, retained while unattempted. A failed attempted block is counted uncertain and cleared; these arrays are not a historical GNSS queue. */
String rpmBuffer[USB_WRITE_BLOCK];
uint8_t rpmBufCount = 0;
String sensorBuffer[USB_WRITE_BLOCK];
uint8_t sensorBufCount = 0;

rtos::Thread sensorThread;
rtos::Thread loggingThread;
// LVGL draws synchronously inside each widget's draw event. Measured peak on
// the board (RTX watermark, self-test + live UI): 4.6 KB, more than the 4 KB
// mbed default. 8 KB leaves headroom for UI changes in EEZ Studio.
rtos::Thread displayThread(osPriorityNormal, 8 * 1024);
rtos::Thread rpmSensorThread;

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch TouchDetector;

//=============================================================================
// 3. PROTOTYPES
//=============================================================================
/** ISR entry: accept rising-edge periods above the noise threshold. Only volatile timestamps are written; the RPM task copies the pair with interrupts masked. No serial, allocation or RTOS lock is permitted here. */
void rpmISR();
/** USB-host callback: publish attachment only. It does not establish FAT usability; the logger discovers and mounts synchronously on a later round. */
void usbAttachHandler();
/** USB-host callback: clear attachment and advance generation so a fast reattach cannot validate an older I/O attempt. Never unmount from the callback. */
void usbDetachHandler();
/** Temperature thread, never returns. Perform sensor-0 conversions nominally once per second, publish Celsius under dataMutex, and sleep between polls. Conversion latency extends the period. */
void sensorTask();
/** Sole filesystem owner, never returns. Perform bounded mount recovery, current-value legacy sampling, statistics and one reserved GNSS block per round. Blocking driver time is additional to the 100 ms sleep; no lossless timing guarantee is made. */
void loggingTask();
/** Logger only, with no open application streams. Connect and attempt FAT registration; success is usable only if attachment/generation still match. A partial failure requires unmount cleanup before retry. */
void mountUSB();
/** Logger only, after stream cleanup. Retire the segment and clear usability; unresolved FAT cleanup keeps registration pending and prevents writes/remount. Driver-level cancellation is not proved by this call. */
void unmountUSB();
/** Synchronously attempt count NUL-terminated legacy lines at path. Return true only for checked open/write/flush/close and unchanged medium. Caller accounts an attempted failed block as uncertain and never replays it. */
bool safeFileWrite(const char *path, String lines[], uint8_t count);
/** Sole runtime LVGL/model owner. Copy shared measurements under their locks, release locks before UI work, apply the one-shot sweep, render and sleep 2..20 ms. DMA/error reporting runs outside the flush callback. */
void displayTask();
/** RPM thread: convert pulse periods to raw RPM, expire after 150 ms and drive hysteretic pin-2 warning. First pulse after timeout is a reference only. Display filtering never affects this warning or logged RPM. */
void rpmSensorTask();

float microsToRPM(unsigned long deltaMicros, int pulsesPerRev);


//=============================================================================
// 4. RPM INTERRUPT
//=============================================================================
void rpmISR()
{
  const uint32_t now = static_cast<uint32_t>(micros());
  const uint32_t delta = now - lastPulseTime;
  if (delta > MINIMUM_RPM_PULSE_DELTA)
  {
    lastPulseTime = now;
    delta_rpm = delta;
  }
}

//=============================================================================
// 5. SETUP
//=============================================================================
/** Arduino startup thread: initialize hardware/UI before dispatching application threads. Peripheral libraries may already own threads. No wait for GNSS fix, USB mount or serial monitor; READY reports dispatch attempted, not universal hardware readiness. */
void setup()
{
  Serial.begin(115200);
  MOFA_DIAG(MOFA_SUBSYS_RTOS, 0, Serial.println("[BOOT] mofa_tacho base=e025292 " __DATE__ " " __TIME__));
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);

  pinMode(PIN_RPM_IN, INPUT_PULLDOWN);
  pinMode(PIN_RPM_OUT, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_IN), rpmISR, RISING);

  sensors_DS.begin();

  // initialisieren des Displays
  if (Display.begin() == 0) {
    if (!display_rotation::install())
      MOFA_DIAG(MOFA_SUBSYS_DISPLAY, 0, Serial.println("[F_DISPLAY_CALLBACK] no LVGL display"));
  } else {
    MOFA_DIAG(MOFA_SUBSYS_DISPLAY, 0, Serial.println("[F_DISPLAY_INIT] display initialization failed"));
  }
  TouchDetector.begin();
  // Display.begin() already ran lv_init(). Its own tick thread advances time in
  // 16 ms steps plus delay jitter; millis() is exact and takes precedence.
  lv_tick_set_cb(millis);

  set_var_gps_online(false);
  set_ui_velocity(-1);
  set_ui_time_unavailable();
  ui_init();

  msd.attach_detected_callback(usbAttachHandler);
  msd.attach_removed_callback(usbDetachHandler);
  // The logger alone calls connect/mount/unmount and filesystem operations.

  heap_diag::initialize(); // Peripheral/UI init is complete; USB/logger not started.
  displayThread.start(displayTask);
  sensorThread.start(sensorTask);
  loggingThread.start(loggingTask);
  rpmSensorThread.start(rpmSensorTask);
  if (!startGnss())
    MOFA_DIAG(MOFA_SUBSYS_RTOS, 0, Serial.println("GNSS thread could not be started; other tasks remain active"));
  MOFA_DIAG(MOFA_SUBSYS_RTOS, 0, Serial.println("[READY] application tasks dispatched; peripheral readiness is independent"));
}

/** Arduino background entry after setup. Application work belongs to RTOS threads; this function sleeps instead of duplicating their polling. */
void loop()
{
  // use chrono::milliseconds to silence deprecation warning
  rtos::ThisThread::sleep_for(milliseconds(10000));
}

//=============================================================================
// 7. USB MOUNT/UNMOUNT & SAFE WRITE
//=============================================================================
void usbAttachHandler()
{
  usbAttachedFlag.store(true);
}

void usbDetachHandler()
{
  usbAttachedFlag.store(false);
  usbDetachGeneration.fetch_add(1);
}

// Diagnostic ownership is the logger's; the pointer observes its original
// local header gate without moving or changing the recovery state machine.
const bool *diagnosticHeadersReady = nullptr;
usb_io_diag::FirstFailure usbIoFirst;
usb_io_diag::Repeats usbIoRepeats;

struct PreserveErrno {
  int saved = errno;
  ~PreserveErrno() { errno = saved; }
};

usb_io_diag::State usbIoState()
{
  return {usbAttachedFlag.load(), usbMountedFlag.load(), unmountPending,
          usbFaultFlag.load(), diagnosticHeadersReady && *diagnosticHeadersReady,
          usbDetachGeneration.load(), mountedGeneration};
}

const char *usbIoOperation(usb_io_diag::Operation operation)
{
  static const char *const names[] = {"fopen", "fwrite", "fflush", "fclose", "stat",
      "open", "fdopen", "close-fd", "mount", "unmount", "medium", "header"};
  return names[static_cast<unsigned>(operation)];
}

const char *usbIoContext(usb_io_diag::Context context)
{
  static const char *const names[] = {"file", "stat-error", "existing-nonempty",
      "header-write", "medium-changed", "mount", "unmount"};
  return names[static_cast<unsigned>(context)];
}

void usbIoReport(const usb_io_diag::Event &event)
{
  PreserveErrno preserve;
  uint32_t repeats = 0;
  if (!usbIoRepeats.emit(event, millis(), repeats)) return;
#if MOFA_DEBUG_USB >= 3
  static char line[384], sizeText[21];
  gnss_log::decimalSigned(sizeText, sizeof(sizeText), event.fileSize);
  snprintf(line, sizeof(line),
      "[USB IO DIAG] op=%s path=%s mode=%s ctx=%s branch=%s rc=%ld errno=%d ferror=%d ferrPhase=%s row=%lu requested=%lu actual=%lu repeat=%lu stat=%u size=%s type=%lu io=%u medium=%u\n",
      usbIoOperation(event.operation), event.path, event.mode, usbIoContext(event.context), usbIoContext(event.branch),
      event.result, event.error, event.streamError,
      event.streamError < 0 ? "n/a" : (event.operation == usb_io_diag::Operation::Close ? "pre-close" : "after-call"),
      (unsigned long)event.row,
      (unsigned long)event.requested, (unsigned long)event.actual, (unsigned long)repeats,
      unsigned(event.hasStat), sizeText, (unsigned long)event.fileType,
      unsigned(event.ioOk), unsigned(event.mediumOk));
  Serial.print(line);
  snprintf(line, sizeof(line), "[USB IO DIAG] suppressed=%lu classSuppressed=%lu registered=%u\n",
      (unsigned long)usbIoRepeats.suppressed, (unsigned long)usbIoRepeats.classSuppressed,
      unsigned(usbMount.registered));
  Serial.print(line);
  const auto states = [&](const char *phase, const usb_io_diag::State &s) {
    snprintf(line, sizeof(line),
        "[USB IO DIAG] %s attached=%u mounted=%u unmountPending=%u fault=%u detachGeneration=%lu mountedGeneration=%lu headersReady=%u\n",
        phase, unsigned(s.attached), unsigned(s.mounted), unsigned(s.pending), unsigned(s.fault),
        (unsigned long)s.detached, (unsigned long)s.mountedGeneration, unsigned(s.headers));
    Serial.print(line);
  };
  states("pre", event.before);
  states("post", event.after);
#elif MOFA_DEBUG_USB >= 1
  Serial.println("[USB:E_IO] operation failed; detailed context requires USB level 3");
#endif
}

/** Logger-thread adapter for the allocation-free checked-write policies. Own no FILE beyond one synchronous operation; preserve first failure through cleanup. Path/mode pointers are borrowed for the operation; captured event strings are owned copies. */
struct FileOps {
  using Handle = FILE *;
  const char *path;
  const char *mode;
  usb_io_diag::Context context;
  size_t row = 0;
  heap_diag::Token heapToken;
  FileOps(const char *p, const char *m, usb_io_diag::Context c = usb_io_diag::Context::File)
      : path(p), mode(m), context(c) { usbIoFirst.reset(); }
  void capture(usb_io_diag::Operation operation, long result, int error,
               int streamError = -1, size_t requested = 0, size_t actual = 0) {
    PreserveErrno preserve;
    usb_io_diag::Event event;
    event.operation = operation;
    event.context = context;
    event.branch = context;
    usb_io_diag::copy(event.path, path);
    usb_io_diag::copy(event.mode, mode);
    event.result = result; event.error = error; event.streamError = streamError;
    event.requested = requested; event.actual = actual; event.row = row;
    event.before = event.after = usbIoState();
    usbIoFirst.capture(event);
    heap_diag::ioError(path, operation == usb_io_diag::Operation::Medium ? 0 : error, heapToken);
  }
  Handle open(const char *p, const char *m) {
    heapToken = heap_diag::beforeOpen(p);
    const Handle file = fopen(p, m);
    const int error = errno;
    if (!file) {
      capture(usb_io_diag::Operation::Open, 0, error);
      heap_diag::openFailed(heapToken, p, error);
    }
    errno = error;
    return file;
  }
  bool write(Handle f, const char *data, size_t size) {
    const size_t actual = fwrite(data, 1, size, f);
    const int error = errno;
    const int streamError = ferror(f);
    if (actual != size || streamError)
      capture(usb_io_diag::Operation::Write, static_cast<long>(actual), error, streamError, size, actual);
    ++row;
    errno = error;
    return actual == size && !streamError;
  }
  bool flush(Handle f) {
    const int result = fflush(f);
    const int error = errno;
    const int streamError = ferror(f);
    if (result || streamError) capture(usb_io_diag::Operation::Flush, result, error, streamError);
    errno = error;
    return result == 0 && !streamError;
  }
  bool close(Handle f) {
    const int beforeCloseError = errno;
    const int streamError = ferror(f); // Valid pre-close status, never post-close.
    errno = beforeCloseError;
    const int result = fclose(f);
    const int error = errno;
    if (result) capture(usb_io_diag::Operation::Close, result, error, streamError);
    heap_diag::afterClose(heapToken, path);
    errno = error; // Never inspect f after fclose, including a failed close.
    return result == 0;
  }
  void finish(bool ioOk, bool mediumOk) {
    PreserveErrno preserve;
    heap_diag::operationFinished(ioOk && mediumOk);
    if (!mediumOk && !usbIoFirst.valid) {
      capture(usb_io_diag::Operation::Medium, ioOk, errno);
      usbIoFirst.event.context = usb_io_diag::Context::MediumChanged;
    }
    if (usbIoFirst.valid) {
      // Keep the failure-time snapshot; the post state shows cleanup completion.
      usbIoFirst.event.after = usbIoState();
      usbIoFirst.event.ioOk = ioOk;
      usbIoFirst.event.mediumOk = mediumOk;
      usbIoReport(usbIoFirst.event);
    }
    heap_diag::reportPending(); // All cleanup has returned, no library locks held.
  }
};

// Logger-thread only. Trace the first six calls of each operation, then one
// paired sample per five seconds. A blocked call therefore leaves a before
// marker. No callback, ISR or FIFO-mutex region calls this instrumentation.
struct LoggerTrace {
  unsigned initial = 0;
  uint32_t last = 0;
  bool sample() {
    if (MOFA_DEBUG_LOGGER < 4 && MOFA_DEBUG_RTOS < 3) return false;
    const uint32_t now = millis();
    if (initial < 6 || uint32_t(now - last) >= 5000) {
      if (initial < 6) ++initial;
      last = now;
      return true;
    }
    return false;
  }
};

void loggerDiagnostic(const char *operation, const char *phase, long result = 0)
{
#if MOFA_DEBUG_LOGGER >= 4 || MOFA_DEBUG_RTOS >= 3
  PreserveErrno preserve;
  // Mbed Thread.h exposes these byte counts. max_stack is a watermark,
  // free_stack a current/saved-SP estimate, not a bound on the next I/O call.
  const uint32_t size = loggingThread.stack_size();
  const uint32_t free = loggingThread.free_stack();
  const uint32_t peak = loggingThread.max_stack();
  static char line[160]; // Single logger owner; keep diagnostics off its stack.
  snprintf(line, sizeof(line),
      "[LOGGER DIAG] %s %s result=%ld stack=%lu free=%lu peak=%lu reserve=%lu\n",
      operation, phase, result, (unsigned long)size, (unsigned long)free,
      (unsigned long)peak, (unsigned long)(peak <= size ? size - peak : 0));
  Serial.print(line);

#endif
}

bool mediumUnchanged()
{
  return usbAttachedFlag.load() && usbDetachGeneration.load() == mountedGeneration;
}

// Logger only. A callback may signal removal during a blocking driver call;
// the stream is closed before unmount, and that operation is never confirmed.
bool safeFileWrite(const char *path, String lines[], uint8_t count)
{
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic(path, "before-write", count);
  FileOps io(path, "a");
  const bool ok = gnss_log::writeFile(io, path, "a", count,
      [&](size_t i) { return lines[i].c_str(); });
  if (diagnostic) loggerDiagnostic(path, "after-write", ok);
  const bool mediumOk = mediumUnchanged();
  io.finish(ok, mediumOk);
  return ok && mediumOk;
}

/** Logger-only non-destructive header preparation. Preserve a nonempty file, create/append the existing header only when needed, and check medium continuity. False triggers shared recovery without truncating user data. */
bool ensureLegacyHeader(const char *path, const char *header)
{
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic(path, "before-header");
  // Preserve the original stat branch and errno gate. A missing file is an
  // ordinary header-write path, not an I/O failure.
  const bool ok = [&]() {
    struct stat info;
    const int result = stat(path, &info);
    const int error = errno;
    usb_io_diag::Event event;
    event.operation = usb_io_diag::Operation::Stat;
    usb_io_diag::copy(event.path, path);
    usb_io_diag::copy(event.mode, "a");
    event.result = result; event.error = error;
    event.before = event.after = usbIoState();
    event.hasStat = result == 0;
    if (event.hasStat) { event.fileSize = info.st_size; event.fileType = info.st_mode & S_IFMT; }
    if (result == 0 && info.st_size != 0) {
      event.context = usb_io_diag::Context::ExistingNonempty;
      event.branch = event.context;
      event.ioOk = true; event.mediumOk = mediumUnchanged();
      if (!event.mediumOk) event.context = usb_io_diag::Context::MediumChanged;
      usbIoReport(event);
      errno = error;
      return event.mediumOk;
    } else if (result != 0 && error != ENOENT) {
      event.context = usb_io_diag::Context::StatError;
      event.branch = event.context;
      event.mediumOk = mediumUnchanged();
      heap_diag::ioError(path, error);
      usbIoReport(event);
      heap_diag::reportPending();
      errno = error;
      return false;
    }
    FileOps io(path, "a", usb_io_diag::Context::HeaderWrite);
    errno = error;
    const bool ioOk = gnss_log::writeFile(io, path, "a", 1,
        [&](size_t) { return header; });
    const bool mediumOk = mediumUnchanged();
    if (!mediumOk && !usbIoFirst.valid) {
      io.capture(usb_io_diag::Operation::Medium, ioOk, errno);
      usbIoFirst.event.context = usb_io_diag::Context::MediumChanged;
    }
    if (usbIoFirst.valid) {
      usbIoFirst.event.hasStat = event.hasStat;
      usbIoFirst.event.fileSize = event.fileSize;
      usbIoFirst.event.fileType = event.fileType;
    }
    io.finish(ioOk, mediumOk);
    if (ioOk && mediumOk) {
      event.context = usb_io_diag::Context::HeaderWrite;
      event.branch = event.context;
      event.ioOk = true; event.mediumOk = true;
      event.after = usbIoState();
      usbIoReport(event);
    }
    return ioOk && mediumOk;
  }();
  if (diagnostic) loggerDiagnostic(path, "after-header", ok);
  return ok;
}

// Bounded name search: at most 32 candidates per logger round, 10000 per boot.
// O_EXCL avoids overwriting any existing segment, including one from a reboot.
/** Logger-only bounded exclusive candidate search. O_EXCL preserves existing files; each attempted index is consumed for boot lifetime. Return Pending/Exhausted without draining FIFO, and retire failed partial creations instead of replaying them. */
gnss_log::SegmentResult createGnssSegment()
{
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic("segment", "before", nextGnssSegment);
  const auto result = gnss_log::findSegment(nextGnssSegment, [&](uint32_t index) {
    snprintf(gnssPath, sizeof(gnssPath), "/usb/GN%06lu.tsv", (unsigned long)index);
    FileOps io(gnssPath, "w");
    io.heapToken = heap_diag::beforeOpen(gnssPath);
    const int fd = ::open(gnssPath, O_WRONLY | O_CREAT | O_EXCL, 0666);
    const int openError = errno;
    if (fd < 0) {
      if (openError != EEXIST) {
        io.capture(usb_io_diag::Operation::ExclusiveOpen, fd, openError);
        heap_diag::openFailed(io.heapToken, gnssPath, openError);
        io.finish(false, mediumUnchanged());
      }
      if (openError == EEXIST) heap_diag::openFailed(io.heapToken, gnssPath, openError);
      gnssPath[0] = 0;
      errno = openError;
      if (errno == EEXIST) return gnss_log::SegmentResult::Exists;
      return gnss_log::SegmentResult::IoError;
    }
    FILE *file = fdopen(fd, "w");
    const int fdopenError = errno;
    if (!file) {
      io.capture(usb_io_diag::Operation::Fdopen, 0, fdopenError);
      const int closeResult = ::close(fd);
      const int closeError = errno;
      if (closeResult) io.capture(usb_io_diag::Operation::Fdclose, closeResult, closeError);
      heap_diag::afterClose(io.heapToken, gnssPath);
      io.finish(false, mediumUnchanged());
      gnssPath[0] = 0;
      errno = closeError;
      return gnss_log::SegmentResult::IoError;
    }
    const bool ok = gnss_log::writeOpened(io, file, 1, [&](size_t) { return GNSS_HEADER; });
    const bool mediumOk = mediumUnchanged();
    io.finish(ok, mediumOk);
    if (ok && mediumOk) return gnss_log::SegmentResult::Ready;
    gnssPath[0] = 0;
    return gnss_log::SegmentResult::IoError;
  });
  if (diagnostic) loggerDiagnostic("segment", "after", static_cast<long>(result));
  return result;
}

void mountUSB()
{
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic("mount", "before");
  // One bounded attempt per second; no busy retry loop in the logger.
  const uint32_t generation = usbDetachGeneration.load();
  if (!msd.connect()) {
    if (diagnostic) loggerDiagnostic("mount", "connect-unavailable", -1);
    return;
  }
  usbAttachedFlag.store(true);
  const auto before = usbIoState();
  const heap_diag::Token heapToken = heap_diag::beforeMount();
  usbMount.begin();
  const int code = fs.mount(&msd);
  const int error = errno;
  if (code) heap_diag::ioError("/usb", code < 0 ? -code : 0);
  heap_diag::afterMount(heapToken, code == 0);
  if (diagnostic) loggerDiagnostic("mount", code ? "error" : "after", code);
  mountedGeneration = generation;
  usbMount.mounted(code == 0, mediumUnchanged());
  unmountPending = usbMount.pending;
  if (code == 0) {
    mountedGeneration = generation;
    usbMountedFlag.store(usbMount.usable);
    usbFaultFlag.store(!mediumUnchanged());
    MOFA_DIAG(MOFA_SUBSYS_USB, 2, Serial.println("USB mounted; existing files preserved"));
  } else {
    usbFaultFlag.store(true);
    static usb_io_diag::Event event;
    event.operation = usb_io_diag::Operation::Mount;
    event.context = usb_io_diag::Context::Mount;
    event.branch = event.context;
    usb_io_diag::copy(event.path, "/usb");
    event.result = code; event.error = error;
    event.before = before; event.after = usbIoState();
    event.mediumOk = mediumUnchanged();
    usbIoReport(event); // Raw Mbed return code is authoritative, errno a snapshot.
    heap_diag::reportPending();
  }
  errno = error;
}

void unmountUSB()
{
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic("unmount", "before");
  // Invoked only by loggingTask, after every open stream has been closed.
  const auto before = usbIoState();
  usbMount.lastCleanup = millis();
  const int code = fs.unmount();
  const int error = errno;
  if (code) heap_diag::ioError("/usb", code < 0 ? -code : 0);
  heap_diag::afterUnmount(code == 0);
  if (diagnostic) loggerDiagnostic("unmount", code ? "error" : "after", code);
  usbMount.cleaned(code);
  unmountPending = usbMount.pending;
  usbMountedFlag.store(false);
  gnssPath[0] = 0;
  if (code != 0) {
    usbFaultFlag.store(true);
    static usb_io_diag::Event event;
    event.operation = usb_io_diag::Operation::Unmount;
    event.context = usb_io_diag::Context::Unmount;
    event.branch = event.context;
    usb_io_diag::copy(event.path, "/usb");
    event.result = code; event.error = error;
    event.before = before; event.after = usbIoState();
    event.mediumOk = mediumUnchanged();
    usbIoReport(event);
    heap_diag::reportPending();
  }
  errno = error;
}

/** Logger-only formatter; returns the shared gnssLine buffer or nullptr on truncation/conversion failure. Consume before the next format/stats call. Use validity flags to leave fields empty; receipt time never replaces UTC. */
const char *formatGnssRecord(const GnssLogRecord &r)
{
  char sequence[21];
  char utc[24] = {}, lat[16] = {}, lon[16] = {}, alt[16] = {};
  char speed[16] = {}, course[16] = {}, accuracy[16] = {};
  if (!gnss_log::decimalUnsigned(sequence, sizeof(sequence), r.sequence)) return nullptr;
  if (r.timeValid) {
    if (!gnss_log::decimalSigned(utc, sizeof(utc), r.utcMs)) return nullptr;
  }
  if (r.positionValid) {
    snprintf(lat, sizeof(lat), "%ld", (long)r.latitude);
    snprintf(lon, sizeof(lon), "%ld", (long)r.longitude);
    snprintf(speed, sizeof(speed), "%ld", (long)r.speedMmS);
    snprintf(course, sizeof(course), "%ld", (long)r.courseE5);
    snprintf(accuracy, sizeof(accuracy), "%lu", (unsigned long)r.horizontalAccuracyMm);
  }
  if (r.altitudeValid) snprintf(alt, sizeof(alt), "%ld", (long)r.altitudeMslMm);
  const int n = snprintf(gnssLine, sizeof(gnssLine),
      "%s\t%lu\t%lu\t%lu\t%lu\t%lu\t%s\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%s\t%s\n",
      sequence, (unsigned long)r.sessionId,
      (unsigned long)r.sampleId, (unsigned long)r.iTowMs, (unsigned long)r.receivedMs,
      (unsigned long)r.epochDeltaMs, utc, unsigned(r.timeValid), unsigned(r.positionValid),
      unsigned(r.altitudeValid), unsigned(r.fixType), lat, lon, alt, speed, course, accuracy);
  return n >= 0 && size_t(n) < sizeof(gnssLine) ? gnssLine : nullptr;
}

// Status records are periodic even with no subsequent epoch. Serial output
// remains available without a medium; after recovery the new segment receives
// the boot-wide totals, including all overflow and uncertain blocks.
/** Logger-only accounting snapshot and optional persistent STATS row. Return false on format or attempted I/O failure. The serial copy may be disabled while file output and counters remain active. */
bool reportGnssStats(bool toFile)
{
  const GnssLogStats s = readGnssLogStats();
  char accepted[21], written[21], overflow[21], uncertain[21], legacy[21];
  if (!gnss_log::decimalUnsigned(accepted, sizeof(accepted), s.accepted) ||
      !gnss_log::decimalUnsigned(written, sizeof(written), s.written) ||
      !gnss_log::decimalUnsigned(overflow, sizeof(overflow), s.overflow) ||
      !gnss_log::decimalUnsigned(uncertain, sizeof(uncertain), s.uncertain) ||
      !gnss_log::decimalUnsigned(legacy, sizeof(legacy), legacyUncertain)) return false;
  const int n = snprintf(gnssLine, sizeof(gnssLine),
      "# STATS accepted=%s written=%s queued=%lu highWater=%lu overflow=%s uncertain=%s legacyUncertain=%s gnssSegmentNext=%lu gnssSegmentExhausted=%u\n",
      accepted, written,
      (unsigned long)s.queued, (unsigned long)s.highWater,
      overflow, uncertain, legacy, (unsigned long)nextGnssSegment,
      unsigned(!gnssPath[0] && nextGnssSegment >= gnss_log::SEGMENT_LIMIT));
  if (n < 0 || size_t(n) >= sizeof(gnssLine)) return false;
  MOFA_DIAG(MOFA_SUBSYS_LOGGER, 2, Serial.print(gnssLine));
  if (!toFile) return true;
  static LoggerTrace trace;
  const bool diagnostic = trace.sample();
  if (diagnostic) loggerDiagnostic("stats-write", "before");
  FileOps io(gnssPath, "a");
  const bool ioOk = gnss_log::writeFile(io, gnssPath, "a", 1,
      [&](size_t) { return gnssLine; });
  const bool mediumOk = mediumUnchanged();
  io.finish(ioOk, mediumOk);
  const bool ok = ioOk && mediumOk;
  if (diagnostic) loggerDiagnostic("stats-write", "after", ok);
  return ok;
}

//=============================================================================
// 8. TEMPERATURE TASK (GNSS runs independently)
//=============================================================================
void sensorTask()
{
  uint32_t lastTempMs = 0;

  while (true)
  {
    uint32_t now = millis();

    // Temperature @1Hz
    if (now - lastTempMs >= 1000 / TEMP_READ_HZ)
    {
      lastTempMs = now;
      sensors_DS.requestTemperatures();
      float tC = sensors_DS.getTempCByIndex(0);
      tempOK = (tC > -55 && tC < 125);
      dataMutex.lock();
      curTempC = tC;
      dataMutex.unlock();
    }

    rtos::ThisThread::sleep_for(milliseconds(I2C_POLL_WAIT_MS));
  }
}

//=============================================================================
// 9. LOGGING TASK (USB writes @10Hz + RPM streaming)
//=============================================================================
void loggingTask()
{
  const char *rpmHeader = "Time_UTC\tRPM_Umin-1\n";
  const char *sensorHeader = "Time_UTC\tTemp_C\tLat_deg\tLon_deg\tAlt_m\tSpeed_kmh\tCourse_deg\tAcc_ms-1\tgpsOK_1\tgpsFix3D_1\n";
  log_clock::Clock logClock; // Current legacy time only; never feed old FIFO times.
  uint32_t lastMount = millis() - 1000;
  uint32_t lastStats = millis() - 1000;
  uint32_t rpmBufferStart = 0, sensorBufferStart = 0;
  bool headersReady = false;
  diagnosticHeadersReady = &headersReady;

  while (true) {
    heap_diag::startRound();
    const uint32_t now = millis();
    if (usbMount.cleanupDue(now, mediumUnchanged())) {
      unmountUSB();
      headersReady = false;
      if (!usbAttachedFlag.load()) usbFaultFlag.store(false);
    }
    if (usbMount.mountDue(now, lastMount)) {
      lastMount = now;
      mountUSB(); // Also discovers a stick present at startup.
      headersReady = false;
    }
    bool failed = false;
    bool newSegment = false;
    if (usbMountedFlag.load() && !unmountPending && mediumUnchanged()) {
      if (!headersReady) {
        headersReady = ensureLegacyHeader(FILE_RPM_PATH, rpmHeader) &&
                       ensureLegacyHeader(FILE_SENSOR_PATH, sensorHeader);
        failed = !headersReady;
      }
      if (!failed && !gnssPath[0] && nextGnssSegment < gnss_log::SEGMENT_LIMIT) {
        const auto segment = createGnssSegment();
        newSegment = segment == gnss_log::SegmentResult::Ready;
        failed = gnss_log::sharedSegmentFailure(segment, mediumUnchanged());
        if (segment == gnss_log::SegmentResult::Exhausted)
          MOFA_DIAG(MOFA_SUBSYS_LOGGER, 1, Serial.println("GNSS segment names exhausted; FIFO retained, legacy logging continues"));
        if (segment == gnss_log::SegmentResult::IoError)
          MOFA_DIAG(MOFA_SUBSYS_LOGGER, 1, Serial.println("GNSS segment I/O failed; FIFO retained, legacy attempted if medium unchanged"));
      }
    }
    // Stats have a deadline independent of new GNSS data, including final loss.
    if (newSegment || uint32_t(now - lastStats) >= 1000) {
      lastStats = now;
      const bool toFile = !failed && !unmountPending && usbMountedFlag.load() && gnssPath[0] && mediumUnchanged();
      if (!reportGnssStats(toFile)) {
        gnssPath[0] = 0; // Isolate GNSS; never append behind a partial stats row.
        failed = !mediumUnchanged();
        MOFA_DIAG(MOFA_SUBSYS_LOGGER, 1, Serial.println("GNSS statistics I/O failed; segment retired"));
      }
    }
    if (!failed && !unmountPending && headersReady && usbMountedFlag.load() && mediumUnchanged()) {
      // Legacy sampling remains current-value, nominally 10 Hz. Full buffers
      // retained during a detach are processed before admitting another sample.
      if (rpmBufCount < USB_WRITE_BLOCK && sensorBufCount < USB_WRITE_BLOCK) {
        if (!rpmBufCount) rpmBufferStart = now;
        if (!sensorBufCount) sensorBufferStart = now;
        // timestamp
        const GnssSnapshot gps = readGnssSnapshot();
        const bool gpsOK = gps.connection == GnssConnection::Receiving;
        const bool gpsFix3D = gps.positionValid && gps.altitudeValid;
        char timeBuf[64];
        {
          tm t = {};
          // Seconds and milliseconds both come from one clock, anchored on the
          // first GNSS time sample. Before that it is 1970 + uptime, an explicit
          // placeholder that keeps the calendar-shaped column, not trusted UTC.
          if (gps.timeValid)
            logClock.sync(int64_t(gps.utcSeconds) * 1000 + gps.utcNanoseconds / 1000000, gps.receivedMs);
          const int64_t logMs = logClock.timeMs(millis());
          // _rtc_localtime converts seconds since epoch to a calendar structure (UTC)
          if (_rtc_localtime(static_cast<time_t>(logMs / 1000), &t, RTC_4_YEAR_LEAP_YEAR_SUPPORT)) {
            snprintf(timeBuf, sizeof(timeBuf),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03ld\t",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec,
                  (long)(logMs % 1000));
          } else {
            snprintf(timeBuf, sizeof(timeBuf), "1970-01-01 00:00:00.000\t");
          }
        }

        // RPM buffer
        dataMutex.lock();
        const float rpm = curRPM;
        const float temperature = curTempC;
        dataMutex.unlock();
        String rline = String(timeBuf) + rpm + "\n";
        rpmBuffer[rpmBufCount++] = rline;
        // SENSOR buffer @10Hz
        // Preserve the legacy ten-column schema; validity and epoch diagnostics
        // remain in the in-memory GNSS snapshot, not additional log columns.
        // Unavailable values (no fix, sensor fault) are left empty instead of
        // writing "nan" or the sensor's -127 error code into the data column.
        String sline = String(timeBuf) + (temperature > -120.0f ? String(temperature) : String()) + "\t" +
            (gps.positionValid ? String(gps.latitudeDeg, 7) : String()) + "\t" +
            (gps.positionValid ? String(gps.longitudeDeg, 7) : String()) + "\t" +
            (gps.altitudeValid ? String(gps.altitudeM) : String()) + "\t" +
            (gps.positionValid ? String(gps.speedKph) : String()) + "\t" +
            (gps.positionValid ? String(gps.courseDeg) : String()) + "\t" +
            (gps.positionValid ? String(gps.horizontalAccuracyM) : String()) + "\t" +
            gpsOK + "\t" + gpsFix3D + "\n";
        sensorBuffer[sensorBufCount++] = sline;
      }
      if (rpmBufCount && (rpmBufCount == USB_WRITE_BLOCK || uint32_t(now - rpmBufferStart) >= 1000)) {
        const bool ok = safeFileWrite(FILE_RPM_PATH, rpmBuffer, rpmBufCount);
        if (!ok) legacyUncertain += rpmBufCount;
        rpmBufCount = 0; // Attempted blocks are never replayed after uncertainty.
        failed = !ok;
      }
      if (!failed && sensorBufCount && (sensorBufCount == USB_WRITE_BLOCK || uint32_t(now - sensorBufferStart) >= 1000)) {
        const bool ok = safeFileWrite(FILE_SENSOR_PATH, sensorBuffer, sensorBufCount);
        if (!ok) legacyUncertain += sensorBufCount;
        sensorBufCount = 0;
        failed = !ok;
      }
      if (!failed && gnssPath[0] && mediumUnchanged()) {
        // At most 20 per 100-ms round: up to 200/s before I/O delays, so a
        // backlog can drain faster than 5 Hz while legacy work gets each turn.
        static LoggerTrace reserveTrace, writeTrace;
        const bool reserveDiagnostic = reserveTrace.sample();
        if (reserveDiagnostic) loggerDiagnostic("fifo-reserve", "before");
        const size_t count = reserveGnssLog(gnssWork, gnss_log::BLOCK, millis());
        if (reserveDiagnostic) loggerDiagnostic("fifo-reserve", "after", count);
        if (count) {
          const bool writeDiagnostic = writeTrace.sample();
          if (writeDiagnostic) loggerDiagnostic("gnss-block", "before", count);
          FileOps io(gnssPath, "a");
          const bool ioOk = gnss_log::writeFile(io, gnssPath, "a", count,
              [&](size_t i) { return formatGnssRecord(gnssWork[i]); });
          const bool mediumOk = mediumUnchanged();
          io.finish(ioOk, mediumOk);
          const bool ok = ioOk && mediumOk;
          if (writeDiagnostic) loggerDiagnostic("gnss-block", "after", ok);
          const bool completed = completeGnssLog(gnssWork[0].sequence, count, ok);
          if (!ok) {
            gnssPath[0] = 0; // Uncertain block is accounted once; rotate, no replay.
            MOFA_DIAG(MOFA_SUBSYS_LOGGER, 1, Serial.println("GNSS block I/O failed; segment retired"));
          }
          failed = !completed || !mediumUnchanged();
          if (!completed) MOFA_DIAG(MOFA_SUBSYS_LOGGER, 0, Serial.println("GNSS FIFO completion invariant failed"));
        }
      }
      if (!failed) usbFaultFlag.store(false);
    }
    if (failed) {
      usbFaultFlag.store(true);
      // Close/flush already finished. Rotate the GNSS segment on recovery;
      // do not append behind any possibly partial row from a failed block.
      if (usbMountedFlag.load()) unmountUSB();
      headersReady = false;
      reportGnssStats(false);
    }
    heap_diag::finishRound(!failed && headersReady && gnssPath[0] && !unmountPending &&
        usbMountedFlag.load() && mediumUnchanged());
    rtos::ThisThread::sleep_for(milliseconds(1000 / SENSOR_LOG_HZ));
  }
}

//=============================================================================
// 10. Display Task
//=============================================================================
void displayTask()
{
  uint32_t sleep_ms = 0;
  RpmDisplayFilter rpmDisplayFilter;
  const uint32_t selfTestStartMs = millis();
  bool selfTestRunning = true;
  while (true) {
    // Only this thread touches UI model buffers once the threads have started.
    const GnssSnapshot gps = readGnssSnapshot();
    dataMutex.lock();
    const float rpm = curRPM;
    const bool rpmWarning = curRpmWarning;
    const float temperature = curTempC;
    dataMutex.unlock();
    set_var_motor_rpm(rpmDisplayFilter.update(rpm, millis()));
    set_ui_rpm_warning(rpmWarning);
    set_ui_temperature(temperature);
    set_var_gps_online(gps.connection == GnssConnection::Receiving);
    set_ui_gps_status(gps);
    set_ui_usb_status(!usbAttachedFlag ? UsbState::NoStick
                      : usbFaultFlag   ? UsbState::Error
                      : usbMountedFlag ? UsbState::Logging
                                       : UsbState::Mounting);
    set_ui_velocity(gps.positionValid ? static_cast<int32_t>(gps.speedKph) : -1);
    if (gps.clockSynchronized)
      set_ui_time(time(nullptr)); // RTC holds UTC; the helper applies CET/CEST.
    else
      set_ui_time_unavailable();
    // Startup sweep over every display element; overrides the live values above.
    // One-shot: the millis() difference wraps after ~49.7 days and must not
    // restart the sweep while riding.
    if (selfTestRunning) {
      const uint32_t selfTestMs = millis() - selfTestStartMs;
      // A real over-rev (reset while riding) must not be hidden by the sweep.
      selfTestRunning = ui_self_test::active(selfTestMs) && !rpmWarning;
      if (selfTestRunning)
        set_ui_self_test(ui_self_test::frame(selfTestMs));
    }
    sleep_ms = lv_task_handler();
    display_rotation::reportErrors();
    ui_tick();
    // Refresh live values and the clock even if LVGL currently has no timers.
    // lv_task_handler() returns 0 while a timer is due; never spin without a pause.
    rtos::ThisThread::sleep_for(milliseconds(sleep_ms < 2 ? 2 : (sleep_ms < 20 ? sleep_ms : 20)));
  }
}

void rpmSensorTask()
{
    uint32_t lastPulseTimeSaved = 0;
    bool last_zero = true;
    bool warning = false;
    digitalWrite(PIN_RPM_OUT, LOW);
    dataMutex.lock();
    curRPM = 0;
    curRpmWarning = warning;
    dataMutex.unlock();

    while (true)
    {
        uint32_t pulseTime;
        uint32_t pulseDelta;

        // Copy the ISR's pair consistently; all calculations and RTOS calls
        // remain outside the interrupt-masked section.
        core_util_critical_section_enter();
        pulseTime = lastPulseTime;
        pulseDelta = delta_rpm;
        core_util_critical_section_exit();

        // Read now after the snapshot, using the same wrapping clock as the ISR.
        const uint32_t pulseAgeUs = static_cast<uint32_t>(micros()) - pulseTime;
        if (lastPulseTimeSaved != pulseTime)
        {
            lastPulseTimeSaved = pulseTime;
            const bool intervalValid = !last_zero && pulseDelta < RPM_PULSE_TIMEOUT_US;
            last_zero = false;
            // The first pulse after start/timeout is a new reference, not an RPM.
            const float rpm = intervalValid && pulseAgeUs < RPM_PULSE_TIMEOUT_US
                ? microsToRPM(pulseDelta, RPM_PULSE_PER_REV) : 0.0f;
            if (rpm > RPM_HYST_ON)
                warning = true;
            else if (rpm < RPM_HYST_OFF)
                warning = false;
            digitalWrite(PIN_RPM_OUT, warning ? HIGH : LOW);
            dataMutex.lock();
            curRPM = rpm;
            curRpmWarning = warning;
            dataMutex.unlock();
        } else if (pulseAgeUs >= RPM_PULSE_TIMEOUT_US && !last_zero) {
            last_zero = true;
            warning = false;
            digitalWrite(PIN_RPM_OUT, LOW);
            dataMutex.lock();
            curRPM = 0;
            curRpmWarning = warning;
            dataMutex.unlock();
        }
        rtos::ThisThread::sleep_for(milliseconds(1));
    }
}

// helper
/** Convert microseconds per pulse to revolutions/minute. Return zero for zero period or pulse count; production supplies positive two pulses/revolution. Pure arithmetic, no shared state. */
float microsToRPM(unsigned long deltaMicros, int pulsesPerRev) {
    if (deltaMicros == 0 || pulsesPerRev == 0) return 0;  // avoid division by zero
    return (60.0f * 1'000'000.0f) / (deltaMicros * pulsesPerRev);
}
