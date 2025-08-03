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
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <Arduino_USBHostMbed5.h>
#include <FATFileSystem.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <mbed.h>
#include <mbed_mktime.h>
#include <RTOS.h>
#include <time.h>
#include <chrono>

#include <lvgl.h>
#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"
#include "vars.h"

#include "ui/ui.h"
#include "ui/vars.h"

using namespace std::chrono;

//=============================================================================
// 1. CONFIGURATION
//=============================================================================
#define PIN_RPM_IN 3
#define PIN_RPM_OUT 2
#define PIN_ONEWIRE_BUS 4
#define I2C_CLOCK_HZ 400000
#define I2C_POLL_WAIT_MS 10
#define GPS_MEAS_RATE_HZ 5
#define RPM_HYST_ON 15000
#define RPM_HYST_OFF 14500
#define RPM_PULSE_PER_REV 2
#define RPM_MIN_VALID 400
#define TEMP_READ_HZ 1
#define SENSOR_LOG_HZ 10
#define DEBUG_RATE_HZ 5
#define USB_WRITE_BLOCK 20

#define FILE_RPM_PATH "/usb/DRZ.txt"
#define FILE_SENSOR_PATH "/usb/SENSOR.txt"

//=============================================================================
// 2. GLOBALS
//=============================================================================

USBHostMSD msd;
mbed::FATFileSystem fs("usb");
volatile bool usbAttachedFlag = false;
volatile bool usbMountedFlag = false;

SFE_UBLOX_GNSS gnss;
bool gpsOK = false;
bool gpsFix3D = false;

OneWire oneWire(PIN_ONEWIRE_BUS);
DallasTemperature sensors_DS(&oneWire);
bool tempOK = false;

rtos::Queue<float, 64> rpmQueue;
volatile uint64_t lastPulseTime = 0;
volatile uint64_t delta_rpm = 0;

rtos::Mutex dataMutex;
float curRPM = 0.0f;
float curTempC = 0.0f;
float curLatDeg = 0.0f;
float curLonDeg = 0.0f;
float curAltM = 0.0f;
float curSpeedMps = 0.0f;
float curCourseDeg = 0.0f;
float curAccM = 0.0f;

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
void setupGPS();
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
  uint64_t now = micros();
  uint64_t delta = now - lastPulseTime;
  lastPulseTime = now;
  delta_rpm = delta;
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
  setupGPS();

  // initialisieren des Displays
  Display.begin();
  TouchDetector.begin();
  lv_init();
  lv_tick_set_cb(millis);

  ui_init();

  msd.attach_detected_callback(usbAttachHandler);
  msd.attach_removed_callback(usbDetachHandler);
  // Prüfe sofort, ob schon ein Medium da ist
  msd.connect();

  sensorThread.start(sensorTask);
  loggingThread.start(loggingTask);
  displayThread.start(displayTask);
  rpmSensorThread.start(rpmSensorTask);
}

void loop()
{
  // use chrono::milliseconds to silence deprecation warning
  rtos::ThisThread::sleep_for(milliseconds(1000));
}

//=============================================================================
// 6. GPS INIT & RTC SET
//=============================================================================
void setupGPS()
{
  if (!gnss.begin(Wire))
  {
    Serial.println("GPS begin failed");
    return;
  }
  gnss.setI2COutput(COM_TYPE_UBX);
  gnss.setMeasurementRate(1000 / GPS_MEAS_RATE_HZ);
  gnss.setNavigationFrequency(1);
  gpsOK = true;

  uint32_t start = millis();
  while (millis() - start < 5000)
  {
    // poll PVT message (NAV-PVT)
    if (gnss.getPVT())
    {
      gpsFix3D = (gnss.getFixType() == 3);
      struct tm tGPS;
      tGPS.tm_year = gnss.getYear() - 1900;
      tGPS.tm_mon = gnss.getMonth() - 1;
      tGPS.tm_mday = gnss.getDay();
      tGPS.tm_hour = gnss.getHour();
      tGPS.tm_min = gnss.getMinute();
      tGPS.tm_sec = gnss.getSecond();
      time_t gpsTime = mktime(&tGPS);
      set_time(gpsTime);
      break;
    }
    rtos::ThisThread::sleep_for(milliseconds(100));
  }
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
// 8. SENSOR TASK (RPM, Temp, GPS, Debug)
//=============================================================================
void sensorTask()
{
  uint32_t lastTempMs = 0;
  uint32_t lastDebugMs = 0;
  struct tm *ts;
  time_t t;

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
      set_ui_temperature(curTempC);
    }

    // GPS update via getPVT()
    if (gpsOK && gnss.getPVT())
    {
      t = time(NULL);
      ts = gmtime(&t);
      dataMutex.lock();
      curLatDeg = gnss.getLatitude();
      curLonDeg = gnss.getLongitude();
      curAltM = gnss.getAltitudeMSL();
      curSpeedMps = gnss.getGroundSpeed();
      curCourseDeg = gnss.getHeading();
      curAccM = gnss.getHorizontalAccEst() / 1000.0f;
      gpsFix3D = (gnss.getFixType() == 3);
      dataMutex.unlock();
      set_ui_velocity(curSpeedMps / 3600);
      set_ui_time(ts->tm_hour, ts->tm_min, ts->tm_sec);

    }

    // Serial debug @5Hz
    if (now - lastDebugMs >= 1000 / DEBUG_RATE_HZ && false)
    {
      lastDebugMs = now;
      char buf[64];
      snprintf(buf, sizeof(buf),
               "%02d:%02d:%02d.%03ld",
               ts->tm_hour, ts->tm_min, ts->tm_sec,
               (long)(millis() % 1000));
        
      dataMutex.lock();
      Serial.print("UTC ");
      Serial.print(buf);
      Serial.print(" °C ");
      Serial.print(curTempC);
      Serial.print(" Lat ");
      Serial.print(curLatDeg, 6);
      Serial.print(" Lon ");
      Serial.print(curLonDeg, 6);
      Serial.print(" Alt ");
      Serial.print(curAltM);
      Serial.print(" m/s ");
      Serial.print(curSpeedMps);
      Serial.print(" Crs ");
      Serial.print(curCourseDeg);
      Serial.print(" Acc ");
      Serial.print(curAccM);
      Serial.print(" GPSOK ");
      Serial.print(gpsOK);
      Serial.print(" 3D? ");
      Serial.println(gpsFix3D);
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
      time_t now = time(NULL);
      struct tm *ts = gmtime(&now);
      char timeBuf[64];
      snprintf(timeBuf, sizeof(timeBuf),
               "%04d-%02d-%02d %02d:%02d:%02d.%03ld\t",
               ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday,
               ts->tm_hour, ts->tm_min, ts->tm_sec,
               (long)(millis() % 1000));

      // RPM buffer
      dataMutex.lock();
      String rline = String(timeBuf) + curRPM + "\n";
      dataMutex.unlock();
      rpmBuffer[rpmBufCount++] = rline;
      if (rpmBufCount >= USB_WRITE_BLOCK)
      {
        safeFileWrite(FILE_RPM_PATH, rpmBuffer, rpmBufCount);
        rpmBufCount = 0;
      }

      // SENSOR buffer @10Hz
      dataMutex.lock();
      String sline = String(timeBuf) + curTempC + "\t" + curLatDeg + "\t" + curLonDeg + "\t" + curAltM + "\t" + curSpeedMps + "\t" + curCourseDeg + "\t" + curAccM + "\t" + gpsOK + "\t" + gpsFix3D + "\n";
      dataMutex.unlock();
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
  while (true) {
    sleep_ms = lv_task_handler();
    ui_tick();
    rtos::ThisThread::sleep_for(milliseconds(sleep_ms));
  }
}

void rpmSensorTask()
{
    uint64_t lastPulseTimeSaved = 0;
    bool last_zero = false;
    while (true)
    {
        uint32_t now = millis();
        int delta_last_rpm_pulse = now - lastPulseTime / 1000;
        if (lastPulseTimeSaved != lastPulseTime)
        {
            last_zero = false;
            float rpm = delta_last_rpm_pulse < 150 ? microsToRPM(delta_rpm, 2) : 0;
            
            rpmQueue.try_put(&rpm);

            set_var_motor_rpm(rpm);
            if (rpm > RPM_HYST_ON)
                digitalWrite(PIN_RPM_OUT, HIGH);
            if (rpm < RPM_HYST_OFF)
                digitalWrite(PIN_RPM_OUT, LOW);
        } else if (delta_last_rpm_pulse > 150 && !last_zero) {
            last_zero = true;
            float rpm = 0;
            
            rpmQueue.try_put(&rpm);
            set_var_motor_rpm(rpm);
            digitalWrite(PIN_RPM_OUT, LOW);
        }
        rtos::ThisThread::sleep_for(milliseconds(1));
    }
}

// helper
float microsToRPM(unsigned long deltaMicros, int pulsesPerRev) {
    if (deltaMicros == 0 || pulsesPerRev == 0) return 0;  // avoid division by zero
    return (60.0f * 1'000'000.0f) / (deltaMicros * pulsesPerRev);
}
