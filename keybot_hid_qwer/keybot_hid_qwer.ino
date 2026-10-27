// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// KeyBot ESP32-S3 예제 — 버튼 4개 → USB HID 키보드 (q, w, e, r)
//
//  - 보드      : YD-ESP32-S3 (ESP32-S3-N16R8)  USB-OTG(GPIO19/20) 포트에 PC 연결
//  - USB ID    : ESP32 기본값 그대로 (VID 0x303A / PID 0x0002) — USB.VID()/USB.PID() 호출 안 함
//  - HID       : 표준 키보드 인터페이스 1개 (Arduino-ESP32 USBHIDKeyboard, 6KRO)
//  - 입력 키   : GPIO4 = q, GPIO5 = w, GPIO6 = e, GPIO7 = r  (버튼 한쪽은 GND, INPUT_PULLUP)
//  - 키 동작   : 실제 키보드와 동일 — 버튼을 누르면 key down, 떼면 key up 리포트를 보낸다.
//                누르고 있는 동안 반복 입력은 PC(OS)가 만든다. 여러 버튼 동시 입력 가능.
//  - 디스플레이: SSD1306 128x64 (SW SPI, U8g2) — USB 상태, 4개 키 상태, 마지막 동작, 횟수, LED
//
//  Arduino IDE 설정 (보드: ESP32S3 Dev Module)
//    USB Mode = "USB-OTG (TinyUSB)",  USB CDC On Boot = Disabled,
//    Flash Size = 16MB, PSRAM = OPI PSRAM
//    (시리얼 로그는 CH343P 쪽 UART 포트로 115200bps 출력됨)
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

#include "USB.h"
#include "USBHIDKeyboard.h"
#include <U8g2lib.h>

// ── 핀 (KeyBot 펌웨어와 동일) ────────────────────────────
const uint8_t KEY_PINS[4] = {4, 5, 6, 7};

#define PIN_OLED_SCK  12
#define PIN_OLED_MOSI 11
#define PIN_OLED_RES  14
#define PIN_OLED_DC    9
#define PIN_OLED_CS   10

// ── 키 매핑 (HID Usage ID: 위치 기반이라 PC의 입력 언어와 무관) ──
const uint8_t KEY_HID[4]  = {HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R};
const char    KEY_CHAR[4] = {'q', 'w', 'e', 'r'};

const uint16_t DEBOUNCE_MS = 8;     // 접점 떨림 제거
const uint16_t UI_MIN_MS   = 40;    // 화면 최소 갱신 간격
const uint16_t UI_TICK_MS  = 500;   // 변화가 없어도 가동 시간 갱신

USBHIDKeyboard Keyboard;
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI oled(
    U8G2_R0, PIN_OLED_SCK, PIN_OLED_MOSI, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RES);

// ── 상태 ────────────────────────────────────────────────
struct KeyState {
  bool     down = false;        // 확정된 상태 (디바운스 후)
  bool     raw = false;         // 마지막 읽은 값
  uint32_t rawChangedAt = 0;
  uint32_t count = 0;           // 누른 횟수
};
KeyState keys[4];

volatile bool usbMounted = false;      // USB 스택 시작 + 호스트 연결
volatile bool usbSuspended = false;
volatile uint8_t hostLeds = 0;  // 호스트가 보낸 LED (bit0 Num, bit1 Caps, bit2 Scroll)
bool     usbWasMounted = false;
int8_t   lastKey = -1;          // 마지막 동작 키 (0~3)
bool     lastDown = false;
uint32_t lastActionAt = 0;
bool     uiDirty = true;
uint32_t lastUiAt = 0;

static void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
  switch ((arduino_usb_event_t)id) {
    case ARDUINO_USB_STARTED_EVENT: usbMounted = true;  usbSuspended = false; break;
    case ARDUINO_USB_STOPPED_EVENT: usbMounted = false; usbSuspended = false; break;
    case ARDUINO_USB_SUSPEND_EVENT: usbSuspended = true;  break;
    case ARDUINO_USB_RESUME_EVENT:  usbSuspended = false; break;
    default: break;
  }
  uiDirty = true;
}

static void onKeyboardEvent(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == ARDUINO_USB_HID_KEYBOARD_LED_EVENT) {
    hostLeds = ((arduino_usb_hid_keyboard_event_data_t*)data)->leds;
    uiDirty = true;
  }
}

// ── 디스플레이 ──────────────────────────────────────────
static void drawKeyBox(int i) {
  const int w = 29, h = 24, y = 17;
  int x = 2 + i * 31;
  if (keys[i].down) {
    oled.drawRBox(x, y, w, h, 3);        // 눌림: 채움 + 흰 글자 반전
    oled.setDrawColor(0);
  } else {
    oled.drawRFrame(x, y, w, h, 3);
  }
  oled.setFont(u8g2_font_logisoso16_tr);
  char c[2] = {(char)toupper(KEY_CHAR[i]), 0};
  oled.drawStr(x + (w - oled.getStrWidth(c)) / 2, y + 20, c);
  oled.setDrawColor(1);
}

static void drawUi() {
  const bool mounted = usbMounted && !usbSuspended;
  oled.clearBuffer();
  oled.setDrawColor(1);

  // 제목 + USB 상태
  oled.setFont(u8g2_font_6x10_tr);
  oled.drawStr(2, 9, "KeyBot HID");
  oled.drawStr(mounted ? 84 : 72, 9, mounted ? "USB OK" : (usbMounted ? "SLEEP " : "NO USB"));
  oled.drawHLine(0, 12, 128);

  // 4개 키
  for (int i = 0; i < 4; i++) drawKeyBox(i);

  // 마지막 동작
  oled.setFont(u8g2_font_6x10_tr);
  char line[28];
  if (lastKey >= 0) {
    snprintf(line, sizeof(line), "%c %-4s %5lus ago", toupper(KEY_CHAR[lastKey]),
             lastDown ? "DOWN" : "UP", (unsigned long)((millis() - lastActionAt) / 1000));
  } else {
    snprintf(line, sizeof(line), "Press a button");
  }
  oled.drawStr(2, 53, line);

  // 하단: 누른 횟수 + 호스트 LED
  char cnt[28];
  snprintf(cnt, sizeof(cnt), "q%lu w%lu e%lu r%lu",
           (unsigned long)keys[0].count, (unsigned long)keys[1].count,
           (unsigned long)keys[2].count, (unsigned long)keys[3].count);
  oled.drawStr(2, 63, cnt);
  if (hostLeds & 0x02) oled.drawStr(110, 63, "CAP");   // Caps Lock 켜짐 (대문자 입력됨)
  else if (hostLeds & 0x01) oled.drawStr(110, 63, "NUM");

  oled.sendBuffer();
}

// ── 키 처리 ─────────────────────────────────────────────
static void sendKey(int i, bool down) {
  if (down) Keyboard.pressRaw(KEY_HID[i]);
  else      Keyboard.releaseRaw(KEY_HID[i]);
  keys[i].down = down;
  if (down) keys[i].count++;
  lastKey = i;
  lastDown = down;
  lastActionAt = millis();
  uiDirty = true;
  Serial.printf("[KEY] %c %s\n", KEY_CHAR[i], down ? "DOWN" : "UP");
}

static void scanKeys() {
  const uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    bool pressed = (digitalRead(KEY_PINS[i]) == LOW);     // 풀업 → 누르면 LOW
    if (pressed != keys[i].raw) {
      keys[i].raw = pressed;
      keys[i].rawChangedAt = now;
    } else if (pressed != keys[i].down && now - keys[i].rawChangedAt >= DEBOUNCE_MS) {
      sendKey(i, pressed);
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);

  oled.begin();
  oled.setBusClock(8000000);

  USB.onEvent(onUsbEvent);
  Keyboard.onEvent(onKeyboardEvent);
  Keyboard.begin();      // 표준 키보드 HID (en-US 배열)
  USB.begin();           // VID/PID 는 ESP32 기본값

  Serial.println("KeyBot HID example: GPIO4/5/6/7 -> q/w/e/r");
  drawUi();
}

void loop() {
  scanKeys();

  const uint32_t now = millis();
  const bool mounted = usbMounted && !usbSuspended;
  if (mounted != usbWasMounted) {                 // 케이블 연결/분리 감지
    usbWasMounted = mounted;
    if (!mounted) for (int i = 0; i < 4; i++) keys[i].down = false;
    uiDirty = true;
  }

  if ((uiDirty && now - lastUiAt >= UI_MIN_MS) || now - lastUiAt >= UI_TICK_MS) {
    uiDirty = false;
    lastUiAt = now;
    drawUi();
  }
  delay(1);
}
