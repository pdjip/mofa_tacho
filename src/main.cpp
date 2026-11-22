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

rtos::Mutex dataMutex;
float curRPM = 0.0f;
bool curRpmWarning = false; // Published with curRPM under dataMutex.
float curTempC = 0.0f;

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
void rpmISR();
void usbAttachHandler();
void usbDetachHandler();
void sensorTask();
void loggingTask();
void mountUSB();
void unmountUSB();
bool safeFileWrite(const char *path, String lines[], uint8_t count);
void displayTask();
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
void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);

  pinMode(PIN_RPM_IN, INPUT_PULLDOWN);
  pinMode(PIN_RPM_OUT, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_IN), rpmISR, RISING);

  sensors_DS.begin();

  // initialisieren des Displays
  Display.begin();
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

  displayThread.start(displayTask);
  sensorThread.start(sensorTask);
  loggingThread.start(loggingTask);
  rpmSensorThread.start(rpmSensorTask);
  if (!startGnss())
    Serial.println("GNSS thread could not be started; other tasks remain active");
}

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

struct FileOps {
  using Handle = FILE *;
  Handle open(const char *path, const char *mode) { return fopen(path, mode); }
  bool write(Handle f, const char *data, size_t size) { return fwrite(data, 1, size, f) == size && !ferror(f); }
  bool flush(Handle f) { return fflush(f) == 0 && !ferror(f); }
  bool close(Handle f) { return fclose(f) == 0; }
};

bool mediumUnchanged()
{
  return usbAttachedFlag.load() && usbDetachGeneration.load() == mountedGeneration;
}

// Logger only. A callback may signal removal during a blocking driver call;
// the stream is closed before unmount, and that operation is never confirmed.
bool safeFileWrite(const char *path, String lines[], uint8_t count)
{
  FileOps io;
  const bool ok = gnss_log::writeFile(io, path, "a", count,
      [&](size_t i) { return lines[i].c_str(); });
  return ok && mediumUnchanged();
}

bool ensureLegacyHeader(const char *path, const char *header)
{
  struct stat info;
  if (stat(path, &info) == 0) {
    if (info.st_size != 0) return mediumUnchanged();
  } else if (errno != ENOENT) {
    return false;
  }
  FileOps io;
  return gnss_log::writeFile(io, path, "a", 1,
      [&](size_t) { return header; }) && mediumUnchanged();
}

// Bounded name search: at most 32 candidates per logger round, 10000 per boot.
// O_EXCL avoids overwriting any existing segment, including one from a reboot.
gnss_log::SegmentResult createGnssSegment()
{
  return gnss_log::findSegment(nextGnssSegment, [&](uint32_t index) {
    snprintf(gnssPath, sizeof(gnssPath), "/usb/GN%06lu.tsv", (unsigned long)index);
    const int fd = ::open(gnssPath, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
      gnssPath[0] = 0;
      if (errno == EEXIST) return gnss_log::SegmentResult::Exists;
      return gnss_log::SegmentResult::IoError;
    }
    FILE *file = fdopen(fd, "w");
    if (!file) { ::close(fd); gnssPath[0] = 0; return gnss_log::SegmentResult::IoError; }
    FileOps io;
    const bool ok = gnss_log::writeOpened(io, file, 1, [&](size_t) { return GNSS_HEADER; });
    if (ok && mediumUnchanged()) return gnss_log::SegmentResult::Ready;
    gnssPath[0] = 0;
    return gnss_log::SegmentResult::IoError;
  });
}

void mountUSB()
{
  // One bounded attempt per second; no busy retry loop in the logger.
  const uint32_t generation = usbDetachGeneration.load();
  if (!msd.connect()) return;
  usbAttachedFlag.store(true);
  const int code = fs.mount(&msd);
  if (code == 0) {
    mountedGeneration = generation;
    usbMountedFlag.store(true);
    usbFaultFlag.store(!mediumUnchanged());
    Serial.println("USB mounted; existing files preserved");
  } else {
    usbFaultFlag.store(true);
    Serial.print("USB mount error: ");
    Serial.println(code);
  }
}

void unmountUSB()
{
  // Invoked only by loggingTask, after every open stream has been closed.
  const int code = fs.unmount();
  unmountPending = code != 0;
  usbMountedFlag.store(unmountPending); // No remount until unmount is resolved.
  gnssPath[0] = 0;
  if (code != 0) {
    usbFaultFlag.store(true);
    Serial.print("USB unmount error: ");
    Serial.println(code);
  }
}

const char *formatGnssRecord(const GnssLogRecord &r)
{
  char utc[24] = {}, lat[16] = {}, lon[16] = {}, alt[16] = {};
  char speed[16] = {}, course[16] = {}, accuracy[16] = {};
  if (r.timeValid) snprintf(utc, sizeof(utc), "%lld", (long long)r.utcMs);
  if (r.positionValid) {
    snprintf(lat, sizeof(lat), "%ld", (long)r.latitude);
    snprintf(lon, sizeof(lon), "%ld", (long)r.longitude);
    snprintf(speed, sizeof(speed), "%ld", (long)r.speedMmS);
    snprintf(course, sizeof(course), "%ld", (long)r.courseE5);
    snprintf(accuracy, sizeof(accuracy), "%lu", (unsigned long)r.horizontalAccuracyMm);
  }
  if (r.altitudeValid) snprintf(alt, sizeof(alt), "%ld", (long)r.altitudeMslMm);
  const int n = snprintf(gnssLine, sizeof(gnssLine),
      "%llu\t%lu\t%lu\t%lu\t%lu\t%lu\t%s\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%s\t%s\n",
      (unsigned long long)r.sequence, (unsigned long)r.sessionId,
      (unsigned long)r.sampleId, (unsigned long)r.iTowMs, (unsigned long)r.receivedMs,
      (unsigned long)r.epochDeltaMs, utc, unsigned(r.timeValid), unsigned(r.positionValid),
      unsigned(r.altitudeValid), unsigned(r.fixType), lat, lon, alt, speed, course, accuracy);
  return n >= 0 && size_t(n) < sizeof(gnssLine) ? gnssLine : nullptr;
}

// Status records are periodic even with no subsequent epoch. Serial output
// remains available without a medium; after recovery the new segment receives
// the boot-wide totals, including all overflow and uncertain blocks.
bool reportGnssStats(bool toFile)
{
  const GnssLogStats s = readGnssLogStats();
  const int n = snprintf(gnssLine, sizeof(gnssLine),
      "# STATS accepted=%llu written=%llu queued=%lu highWater=%lu overflow=%llu uncertain=%llu legacyUncertain=%llu gnssSegmentNext=%lu gnssSegmentExhausted=%u\n",
      (unsigned long long)s.accepted, (unsigned long long)s.written,
      (unsigned long)s.queued, (unsigned long)s.highWater,
      (unsigned long long)s.overflow, (unsigned long long)s.uncertain,
      (unsigned long long)legacyUncertain, (unsigned long)nextGnssSegment,
      unsigned(!gnssPath[0] && nextGnssSegment >= gnss_log::SEGMENT_LIMIT));
  if (n < 0 || size_t(n) >= sizeof(gnssLine)) return false;
  Serial.print(gnssLine);
  if (!toFile) return true;
  FileOps io;
  return gnss_log::writeFile(io, gnssPath, "a", 1,
      [&](size_t) { return gnssLine; }) && mediumUnchanged();
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

  while (true) {
    const uint32_t now = millis();
    if (usbMountedFlag.load() && (unmountPending || !mediumUnchanged())) {
      unmountUSB();
      headersReady = false;
      if (!usbAttachedFlag.load()) usbFaultFlag.store(false);
    }
    if (!usbMountedFlag.load() && uint32_t(now - lastMount) >= 1000) {
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
          Serial.println("GNSS segment names exhausted; FIFO retained, legacy logging continues");
        if (segment == gnss_log::SegmentResult::IoError)
          Serial.println("GNSS segment I/O failed; FIFO retained, legacy attempted if medium unchanged");
      }
    }
    // Stats have a deadline independent of new GNSS data, including final loss.
    if (newSegment || uint32_t(now - lastStats) >= 1000) {
      lastStats = now;
      const bool toFile = !failed && !unmountPending && usbMountedFlag.load() && gnssPath[0] && mediumUnchanged();
      if (!reportGnssStats(toFile)) {
        gnssPath[0] = 0; // Isolate GNSS; never append behind a partial stats row.
        failed = !mediumUnchanged();
        Serial.println("GNSS statistics I/O failed; segment retired");
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
        const size_t count = reserveGnssLog(gnssWork, gnss_log::BLOCK, millis());
        if (count) {
          FileOps io;
          const bool ok = gnss_log::writeFile(io, gnssPath, "a", count,
              [&](size_t i) { return formatGnssRecord(gnssWork[i]); }) && mediumUnchanged();
          const bool completed = completeGnssLog(gnssWork[0].sequence, count, ok);
          if (!ok) {
            gnssPath[0] = 0; // Uncertain block is accounted once; rotate, no replay.
            Serial.println("GNSS block I/O failed; segment retired");
          }
          failed = !completed || !mediumUnchanged();
          if (!completed) Serial.println("GNSS FIFO completion invariant failed");
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
float microsToRPM(unsigned long deltaMicros, int pulsesPerRev) {
    if (deltaMicros == 0 || pulsesPerRev == 0) return 0;  // avoid division by zero
    return (60.0f * 1'000'000.0f) / (deltaMicros * pulsesPerRev);
}
