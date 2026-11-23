// 实验二：Arduino UNO + DS1307 实时时钟 + TM1637 四位数码管。
// 基于 RTClib 的 ds1307 示例修改。
// DS1307: VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5。
// TM1637: VCC -> 5V, GND -> GND, CLK -> D10, DIO -> D9。
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>  // 按老师说明保留；实验二不读取 DHT11。
#include <TM1637.h>

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

void reportDisplayStatus() {
  printMembers();
  if (display.lastWriteOK()) {
    Serial.println(F("[TM1637] ACK OK (CLK=D10, DIO=D9)."));
  } else {
    Serial.println(F("[TM1637] NO ACK: check VCC, GND, CLK=D10, DIO=D9. Retrying."));
  }
}

void reportI2CError(const __FlashStringHelper *stage, uint8_t code) {
  printMembers();
  Serial.print(F("[RTC] "));
  Serial.print(stage);
  Serial.print(F(" address=0x68, I2C code="));
  Serial.print(code);
  switch (code) {
    case 1: Serial.print(F(" (buffer overflow)")); break;
    case 2: Serial.print(F(" (address NACK: device did not acknowledge)")); break;
    case 3: Serial.print(F(" (data NACK)")); break;
    case 4: Serial.print(F(" (bus error)")); break;
    case 5: Serial.print(F(" (timeout: bus may be stuck)")); break;
    default: Serial.print(F(" (unknown error)")); break;
  }
  Serial.print(F("; SDA(A4)="));
  Serial.print(digitalRead(A4));
  Serial.print(F(", SCL(A5)="));
  Serial.println(digitalRead(A5));
}

bool readRtcRegisters(uint8_t data[7]) {
  Wire.clearWireTimeoutFlag();
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write((uint8_t)0);
  const uint8_t code = Wire.endTransmission();
  if (code != 0 || Wire.getWireTimeoutFlag()) {
    reportI2CError(F("select registers"), Wire.getWireTimeoutFlag() ? 5 : code);
    return false;
  }

  const uint8_t received = Wire.requestFrom((uint8_t)DS1307_ADDRESS, (uint8_t)7);
  if (Wire.getWireTimeoutFlag()) {
    reportI2CError(F("read registers"), 5);
    return false;
  }
  if (received != 7) {
    printMembers();
    Serial.print(F("[RTC] incomplete read: expected 7 bytes, got "));
    Serial.println(received);
    return false;
  }
  for (uint8_t i = 0; i < 7; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

uint8_t fromBcd(uint8_t value) {
  return (value >> 4) * 10 + (value & 0x0F);
}

void updateDisplay(int8_t digits[4], bool colonOn) {
  static int8_t previousDigits[4] = {-1, -1, -1, -1};
  static bool previousColon = false;
  static bool hasPreviousDisplay = false;
  bool unchanged = hasPreviousDisplay && previousColon == colonOn;
  for (uint8_t i = 0; i < 4; ++i) {
    if (digits[i] != previousDigits[i]) unchanged = false;
  }
  // TM1637 自行维持扫描显示，无需每秒重写相同内容。
  // 上次失败时不使用缓存，下次每秒继续尝试。
  if (unchanged && display.lastWriteOK()) return;

  display.point(colonOn ? POINT_ON : POINT_OFF);
  display.display(digits);
  if (display.lastWriteOK()) {
    for (uint8_t i = 0; i < 4; ++i) previousDigits[i] = digits[i];
    previousColon = colonOn;
    hasPreviousDisplay = true;
  } else {
    hasPreviousDisplay = false;
  }
  reportDisplayStatus();
}

void showError() {
  // 此版本 TM1637 库用 0~15 表示十六进制字符，14 对应 E。
  int8_t digits[] = {14, 14, 14, 14};
  updateDisplay(digits, false);
}

void updateClock() {
  // 每次刷新先检查 I2C 设备，连接失败时每秒重试。
  Wire.clearWireTimeoutFlag();
  Wire.beginTransmission(DS1307_ADDRESS);
  const uint8_t code = Wire.endTransmission();
  if (code != 0 || Wire.getWireTimeoutFlag()) {
    reportI2CError(F("probe"), Wire.getWireTimeoutFlag() ? 5 : code);
    showError();
    return;
  }

  uint8_t raw[7];
  if (!readRtcRegisters(raw)) {
    showError();
    return;
  }

  // 从已检查长度的寄存器数据读取停振位，避免旧库读取失败后仍解析。
  bool timeAdjusted = false;
  if ((raw[0] & 0x80) || (SET_TIME_ON_START && !forcedTimeSet)) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    if (Wire.getWireTimeoutFlag()) {
      reportI2CError(F("set time"), 5);
      showError();
      return;
    }
    if (!readRtcRegisters(raw)) {
      showError();
      return;
    }
    if (raw[0] & 0x80) {
      printMembers();
      Serial.println(F("[RTC] oscillator still stopped after setting time."));
      showError();
      return;
    }
    forcedTimeSet = true;
    timeAdjusted = true;
  }

  const uint8_t second = fromBcd(raw[0] & 0x7F);
  const uint8_t minute = fromBcd(raw[1] & 0x7F);
  uint8_t hour;
  if (raw[2] & 0x40) {
    // DS1307 也可能已设为 12 小时制，转换成 24 小时后显示。
    const uint8_t hour12 = fromBcd(raw[2] & 0x1F);
    hour = (hour12 >= 1 && hour12 <= 12)
             ? hour12 % 12 + ((raw[2] & 0x20) ? 12 : 0) : 255;
  } else {
    hour = fromBcd(raw[2] & 0x3F);
  }
  const uint8_t day = fromBcd(raw[4] & 0x3F);
  const uint8_t month = fromBcd(raw[5] & 0x1F);
  const uint8_t year = fromBcd(raw[6]);
  // 先检查范围，避免将无效月份交给旧版 RTClib 的日期计算函数。
  if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 ||
      month < 1 || month > 12 || year > 99) {
    printMembers();
    Serial.println(F("[RTC] ACK OK but invalid register values. Check wiring or enable SET_TIME_ON_START."));
    showError();
    return;
  }
  const DateTime now(2000U + year, month, day, hour, minute, second);
  if (!now.isValid()) {
    printMembers();
    Serial.println(F("[RTC] ACK OK but invalid calendar date. Enable SET_TIME_ON_START to reset."));
    showError();
    return;
  }
  if (timeAdjusted) {
    printMembers();
    Serial.println(F("[RTC] time initialized from sketch compile time."));
  }

  const uint8_t leftNumber = SHOW_GROUP_NUMBER ? GROUP_NUMBER : now.hour();
  int8_t digits[] = {
    (int8_t)(leftNumber / 10), (int8_t)(leftNumber % 10),
    (int8_t)(now.minute() / 10), (int8_t)(now.minute() % 10)
  };
  printMembers();
  Serial.print(F("[RTC] OK "));
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

  // 时间先打印到串口，数码管失败不会掩盖 RTC 的成功状态。
  // 验证版：冒号常亮，分钟变化时才重写显示；串口仍每秒输出时间。
  updateDisplay(digits, true);
}

void setup() {
  Serial.begin(9600);
  printMembers();
  Serial.println(F("Experiment 2 diagnostic v3: steady display, colon ON, baud=9600."));

  Wire.begin();
  Wire.setClock(100000);
  // UNO 当前安装的 AVR Wire 支持超时；超时后复位 I2C 外设并继续重试。
  Wire.setWireTimeout(25000UL, true);

  // 旧版 TM1637 库要求先 set()，再 init() 清屏。
  display.set(BRIGHT_TYPICAL);
  display.point(POINT_OFF);
  display.init();
  reportDisplayStatus();
  // Wire 已初始化，首次检测及具体错误由 updateClock() 输出。
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
