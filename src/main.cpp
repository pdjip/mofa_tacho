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

#include <lvgl.h>
#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"
#include "vars.h"
#include "rpm_display_filter.h"
#include "gnss_service.h"

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
#define RPM_MIN_VALID 400
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
volatile bool usbAttachedFlag = false;
volatile bool usbMountedFlag = false;

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
String headerBuffer[USB_WRITE_BLOCK];
uint8_t headerBufCount = 0;

rtos::Thread sensorThread;
rtos::Thread loggingThread;
rtos::Thread displayThread;
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
void safeFileWrite(const char *path, String lines[], uint8_t count);
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
  lv_init();
  lv_tick_set_cb(millis);

  set_var_gps_online(false);
  set_ui_velocity(-1);
  set_ui_time_unavailable();
  ui_init();

  msd.attach_detected_callback(usbAttachHandler);
  msd.attach_removed_callback(usbDetachHandler);
  // Prüfe sofort, ob schon ein Medium da ist
  msd.connect();

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
  usbAttachedFlag = true;
}

void usbDetachHandler()
{
  usbAttachedFlag = false;
  usbMountedFlag = false;
  unmountUSB();
}

void mountUSB()
{
  int retries = 5;
  Serial.println("=== Trying to mount USB ===");
  while (retries-- > 0)
  {
    bool okConnect = msd.connect();
    if (okConnect)
    {
      Serial.print("msd.connect() → ");
      Serial.println(okConnect);
      int mountCode = fs.mount(&msd);
      mountCode = errno;
      Serial.print("ERR mount = ");
      Serial.println(strerror(mountCode));
      Serial.print("fs.mount() → ");
      Serial.println(mountCode);
      if (okConnect && mountCode == 0)
      {
        Serial.println("USB mounted & files reset");
        usbMountedFlag = true;
        safeFileWrite(FILE_RPM_PATH, nullptr, 0);
        safeFileWrite(FILE_SENSOR_PATH, nullptr, 0);
        break;
      }
      else
      {
        Serial.println("USB mount failed");
      }
    }
    else
    {
      Serial.println("USB connect failed");
    }
  }
}

void unmountUSB()
{
  fs.unmount();
  Serial.println("USB unmounted safely");
}

// count==0 → truncate; else append block
void safeFileWrite(const char *path, String lines[], uint8_t count)
{
  FILE *f;
  if (count == 0)
  {
    f = fopen(path, "w+");
    if (f)
      fclose(f);
    return;
  }
  f = fopen(path, "a");
  if (!f)
  {
    Serial.print("File open error: ");
    Serial.println(path);
    return;
  }
  for (uint8_t i = 0; i < count; ++i)
  {
    fputs(lines[i].c_str(), f);
  }
  fclose(f);
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

  String rpmHeader = "Time_UTC\tRPM_Umin-1\n";
  String sensorHeader = "Time_UTC\tTemp_C\tLat_deg\tLon_deg\tAlt_m\tSpeed_kmh\tCourse_deg\tAcc_ms-1\tgpsOK_1\tgpsFix3D_1\n";

  while (true)
  {
    if (usbAttachedFlag)
    {
      if (!usbMountedFlag)
      {
        mountUSB();
        // Header der Dateien schreiben
        headerBuffer[0] = rpmHeader;
        safeFileWrite(FILE_RPM_PATH, headerBuffer, 1);
        headerBuffer[0] = sensorHeader;
        safeFileWrite(FILE_SENSOR_PATH, headerBuffer, 1);
      }
      // timestamp
      const GnssSnapshot gps = readGnssSnapshot();
      const bool gpsOK = gps.connection == GnssConnection::Receiving;
      const bool gpsFix3D = gps.positionValid && gps.altitudeValid;
      char timeBuf[64];
      {
        tm t = {};
        const uint32_t uptimeMs = millis();
        // Preserve the original calendar-shaped column even before GNSS time
        // is available: 1970 + uptime is an explicit placeholder, not trusted UTC.
        const time_t logSeconds = gps.clockSynchronized ? time(nullptr) : uptimeMs / 1000;
        // _rtc_localtime uses the system time in seconds since epoch to get the local time structure
        if (_rtc_localtime(logSeconds, &t, RTC_4_YEAR_LEAP_YEAR_SUPPORT)) {
          snprintf(timeBuf, sizeof(timeBuf),
                "%04d-%02d-%02d %02d:%02d:%02d.%03ld\t",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec,
                (long)(uptimeMs % 1000));
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
      if (rpmBufCount >= USB_WRITE_BLOCK)
      {
        safeFileWrite(FILE_RPM_PATH, rpmBuffer, rpmBufCount);
        rpmBufCount = 0;
      }

      // SENSOR buffer @10Hz
      // Preserve the legacy ten-column schema; validity and epoch diagnostics
      // remain in the in-memory GNSS snapshot, not additional log columns.
      String sline = String(timeBuf) + temperature + "\t" + String(gps.latitudeDeg, 7) + "\t" +
          String(gps.longitudeDeg, 7) + "\t" + gps.altitudeM + "\t" + gps.speedKph + "\t" +
          gps.courseDeg + "\t" + gps.horizontalAccuracyM + "\t" + gpsOK + "\t" + gpsFix3D + "\n";
      sensorBuffer[sensorBufCount++] = sline;
      if (sensorBufCount >= USB_WRITE_BLOCK)
      {
        safeFileWrite(FILE_SENSOR_PATH, sensorBuffer, sensorBufCount);
        sensorBufCount = 0;
      }
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
    set_ui_velocity(gps.positionValid ? static_cast<int32_t>(gps.speedKph) : -1);
    tm utc = {};
    if (gps.clockSynchronized && _rtc_localtime(time(nullptr), &utc, RTC_4_YEAR_LEAP_YEAR_SUPPORT))
      set_ui_time(utc.tm_hour, utc.tm_min, utc.tm_sec);
    else
      set_ui_time_unavailable();
    sleep_ms = lv_task_handler();
    ui_tick();
    // Refresh live values and the clock even if LVGL currently has no timers.
    rtos::ThisThread::sleep_for(milliseconds(sleep_ms < 20 ? sleep_ms : 20));
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
