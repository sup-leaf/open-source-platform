// Example testing sketch for various DHT humidity/temperature sensors
// Written by ladyada, public domain

// REQUIRES the following Arduino libraries:
// - DHT Sensor Library: https://github.com/adafruit/DHT-sensor-library
// - Adafruit Unified Sensor Lib: https://github.com/adafruit/Adafruit_Sensor

#include "DHT.h"

#define DHTPIN 2     // Digital pin connected to the DHT sensor
// Feather HUZZAH ESP8266 note: use pins 3, 4, 5, 12, 13 or 14 --
// Pin 15 can work but DHT must be disconnected during program upload.

// Uncomment whatever type you're using!
#define DHTTYPE DHT11   // DHT 11
// #define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

// Connect pin 1 (on the left) of the sensor to +5V
// NOTE: If using a board with 3.3V logic like an Arduino Due connect pin 1
// to 3.3V instead of 5V!
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

// Initialize DHT sensor.
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
DHT dht(DHTPIN, DHTTYPE);

// 实验成员：每条串口消息都会先输出这两个姓名。
const char MEMBERS[] = "徐健智、程建翔";
const unsigned long SENSOR_INTERVAL_MS = 2000;

// true：湿度控制闪灯；false：温度控制闪灯。修改后重新上传。
const bool USE_HUMIDITY_FOR_BLINK = true;

// 所选读数越高，灯态切换间隔越短；超出范围时保持端点速度。
const float TEMP_LOW_C = 20.0;
const float TEMP_HIGH_C = 35.0;
const float HUMIDITY_LOW_PERCENT = 30.0;
const float HUMIDITY_HIGH_PERCENT = 80.0;
const unsigned long SLOW_BLINK_MS = 1000;
const unsigned long FAST_BLINK_MS = 100;

unsigned long lastSensorMs = 0;
unsigned long lastBlinkMs = 0;
unsigned long blinkIntervalMs = SLOW_BLINK_MS;
bool ledOn = false;
bool sensorReady = false;

void printMembers() {
  Serial.print(F("["));
  Serial.print(MEMBERS);
  Serial.print(F("] "));
}

unsigned long readingToBlinkInterval(float value, float low, float high) {
  if (value <= low) {
    return SLOW_BLINK_MS;
  }
  if (value >= high) {
    return FAST_BLINK_MS;
  }

  const float ratio = (value - low) / (high - low);
  return SLOW_BLINK_MS -
         (unsigned long)(ratio * (SLOW_BLINK_MS - FAST_BLINK_MS));
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  dht.begin();
  lastSensorMs = millis();
  printMembers();
  Serial.println(F("DHT11 test started. DATA=D2, LED=D13, baud=9600."));
  printMembers();
  Serial.print(F("LED control: "));
  Serial.println(USE_HUMIDITY_FOR_BLINK ? F("Humidity") : F("Temperature"));
}

void loop() {
  // 闪灯与采样分别计时，避免 delay(2000) 阻碍 LED 更新。
  const unsigned long now = millis();
  if (sensorReady && now - lastBlinkMs >= blinkIntervalMs) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
  }

  if (now - lastSensorMs < SENSOR_INTERVAL_MS) {
    return;
  }
  lastSensorMs = now;

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    // 读数无效时熄灭 LED，避免旧读数继续控制闪灯。
    sensorReady = false;
    ledOn = false;
    digitalWrite(LED_BUILTIN, LOW);
    printMembers();
    Serial.println(F("DHT11 read failed! Check 5V, GND and DATA=D2."));
    return;
  }

  if (USE_HUMIDITY_FOR_BLINK) {
    blinkIntervalMs = readingToBlinkInterval(h, HUMIDITY_LOW_PERCENT,
                                           HUMIDITY_HIGH_PERCENT);
  } else {
    blinkIntervalMs = readingToBlinkInterval(t, TEMP_LOW_C, TEMP_HIGH_C);
  }
  if (!sensorReady) {
    lastBlinkMs = millis();
  }
  sensorReady = true;

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  printMembers();
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C "));
  Serial.print(f);
  Serial.print(F("°F  Heat index: "));
  Serial.print(hic);
  Serial.print(F("°C "));
  Serial.print(hif);
  Serial.print(F("°F  LED control: "));
  Serial.print(USE_HUMIDITY_FOR_BLINK ? F("Humidity") : F("Temperature"));
  Serial.print(F("  LED toggle interval: "));
  Serial.print(blinkIntervalMs);
  Serial.println(F(" ms"));
}
