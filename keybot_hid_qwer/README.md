# KeyBot ESP32-S3 예제 — 버튼 4개 → USB 키보드 (q, w, e, r)

버튼을 누르면 PC가 **일반 USB 키보드**로 인식해 `q w e r`을 입력하고, OLED에 상태를 표시합니다.

## 동작

| 항목 | 내용 |
|---|---|
| USB ID | ESP32 기본값 그대로 (VID `0x303A`, PID `0x0002`). 코드에서 바꾸지 않음 |
| HID | 표준 키보드 인터페이스 1개 (Arduino-ESP32 `USBHIDKeyboard`, 6키 동시 입력) |
| 입력 | GPIO4=`q`, GPIO5=`w`, GPIO6=`e`, GPIO7=`r` (버튼 한쪽 GND, 내부 풀업) |
| 키 동작 | 실제 키보드와 동일: 누르면 key down, 떼면 key up. 누르고 있는 동안의 반복 입력은 PC가 만듦. 여러 버튼 동시 입력 가능 |
| 키 코드 | HID Usage ID(`HID_KEY_Q` …)를 보내므로 PC 입력 언어(한/영)와 상관없이 같은 키 위치로 동작 |
| 디바운스 | 8ms |

## 디스플레이 (SSD1306 128x64, SW SPI)

```
KeyBot HID        USB OK     ← USB 연결 상태 (NO USB / SLEEP / USB OK)
─────────────────────────────
[ Q ] [ W ] [ E ] [ R ]      ← 눌린 키는 반전 표시
Q DOWN    3s ago             ← 마지막 동작
q5 w2 e0 r1            CAP   ← 키별 누른 횟수 / 호스트 LED(Caps·Num)
```

OLED 핀은 KeyBot 펌웨어와 같습니다: SCK=12, MOSI=11, RES=14, DC=9, CS=10.
OLED가 없어도 키보드 기능은 정상 동작합니다.

## 빌드 설정 (Arduino IDE, 보드 `ESP32S3 Dev Module`)

- USB Mode: **USB-OTG (TinyUSB)**
- USB CDC On Boot: **Disabled** (시리얼 로그는 CH343P UART 포트 115200bps)
- Flash Size: 16MB, PSRAM: OPI PSRAM
- 라이브러리: **U8g2**
- PC는 보드의 **USB-OTG(GPIO19/20) 포트**에 연결합니다. (UART 포트가 아님)

업로드는 UART 포트로 하세요. USB-OTG 포트가 키보드로 동작 중이면 업로드 포트로 잡히지 않을 수 있습니다.
