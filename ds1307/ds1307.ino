// 实验二：Arduino UNO + DS1307 实时时钟 + TM1637 四位数码管。
// 基于 RTClib 的 ds1307 示例修改。
// DS1307: VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5。
// TM1637: VCC -> 5V, GND -> GND, CLK -> D10, DIO -> D9。
#include <Wire.h>
#include "RTClib.h"
#include "TM1637.h"

const char MEMBERS[] = "徐健智、程建翔";
const uint8_t DISPLAY_CLK_PIN = 10;
const uint8_t DISPLAY_DIO_PIN = 9;
const unsigned long UPDATE_INTERVAL_MS = 1000;

// 按实验要求显示“编号:分钟”：左侧固定为组号 55，右侧为 RTC 分钟。
// 改为 false 可恢复“小时:分钟”；组号和姓名始终输出到串口。
const bool SHOW_GROUP_NUMBER = true;
const uint8_t GROUP_NUMBER = 55;
static_assert(GROUP_NUMBER <= 99, "GROUP_NUMBER must be between 0 and 99");

// 时钟停振时自动设置为本次编译时间。
// 若时钟在走但时间不对，改为 true 后重新编译上传以校时；
// 校时完成后改回 false 并再次上传，避免每次复位都重置时间。
// 编译时间不是实时网络时间，会有编译、上传耗时带来的偏差。
const bool SET_TIME_ON_START = false;

RTC_DS1307 rtc;
TM1637 display(DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
unsigned long lastUpdateMs = 0;
bool rtcReady = false;
bool forcedTimeSet = false;
bool firstUpdate = true;

void printMembers() {
  Serial.print(F("[组号："));
  Serial.print(GROUP_NUMBER);
  Serial.print(' ');
  Serial.print(MEMBERS);
  Serial.print(F("] "));
}

void printTwoDigits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

void showError() {
  // 此版本 TM1637 库用 0~15 表示十六进制字符，14 对应 E。
  int8_t digits[] = {14, 14, 14, 14};
  display.point(POINT_OFF);
  display.display(digits);
}

void updateClock() {
  // 每次刷新先检查 I2C 设备，连接失败时每秒重试。
  Wire.beginTransmission(0x68);
  if (Wire.endTransmission() != 0) {
    rtcReady = false;
    printMembers();
    Serial.println(F("DS1307 not found! Check 5V, GND, SDA=A4, SCL=A5."));
    showError();
    return;
  }

  if (!rtcReady) {
    if (!rtc.isrunning() || (SET_TIME_ON_START && !forcedTimeSet)) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      forcedTimeSet = true;
      printMembers();
      Serial.println(F("RTC set to sketch compile time."));
    }
    rtcReady = true;
  }

  const DateTime now = rtc.now();
  if (!now.isValid()) {
    printMembers();
    Serial.println(F("Invalid RTC time. Check wiring or enable SET_TIME_ON_START."));
    showError();
    return;
  }

  const uint8_t leftNumber = SHOW_GROUP_NUMBER ? GROUP_NUMBER : now.hour();
  int8_t digits[] = {
    (int8_t)(leftNumber / 10), (int8_t)(leftNumber % 10),
    (int8_t)(now.minute() / 10), (int8_t)(now.minute() % 10)
  };
  // 冒号随 RTC 秒数亮灭，便于观察时钟是否在走。
  display.point(now.second() % 2 == 0 ? POINT_ON : POINT_OFF);
  display.display(digits);

  printMembers();
  Serial.print(now.year());
  Serial.print('/');
  printTwoDigits(now.month());
  Serial.print('/');
  printTwoDigits(now.day());
  Serial.print(' ');
  printTwoDigits(now.hour());
  Serial.print(':');
  printTwoDigits(now.minute());
  Serial.print(':');
  printTwoDigits(now.second());
  Serial.print(SHOW_GROUP_NUMBER ? F("  Display(group:minute): ")
                               : F("  Display(hour:minute): "));
  printTwoDigits(leftNumber);
  Serial.print(':');
  printTwoDigits(now.minute());
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  printMembers();
  Serial.println(F("Experiment 2 started. DS1307 + TM1637, baud=9600."));

  // 旧版 TM1637 库要求先 set()，再 init() 清屏。
  display.set(BRIGHT_TYPICAL);
  display.point(POINT_OFF);
  display.init();
  // begin() 初始化 I2C；连接结果由 updateClock() 检查并报告。
  rtc.begin();
}

void loop() {
  const unsigned long nowMs = millis();
  if (!firstUpdate && nowMs - lastUpdateMs < UPDATE_INTERVAL_MS) {
    return;
  }
  firstUpdate = false;
  lastUpdateMs = nowMs;
  updateClock();
}
