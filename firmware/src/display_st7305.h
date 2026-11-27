#pragma once
#include <Arduino.h>
#include <SPI.h>

// ── Waveshare ESP32-S3-RLCD-4.2 pin assignments (from ESPHome device page) ──
static constexpr int LCD_CLK  = 11;
static constexpr int LCD_MOSI = 12;
static constexpr int LCD_CS   = 40;
static constexpr int LCD_DC   =  5;
static constexpr int LCD_RST  = 41;

// On-board push buttons (both active-low → INPUT_PULLUP, pressed reads LOW)
static constexpr int BTN_BOOT = 0;    // BOOT (strapping pin) — force /stats refresh
static constexpr int BTN_USER = 18;   // KEY — cycle view (scaffolded no-op)

static constexpr int LCD_W = 400;
static constexpr int LCD_H = 300;

// drawText size argument: 1 = 5×8 px/char, N = N×5 wide, N×8 tall per char
static constexpr int FONT_SM = 1;   // labels
static constexpr int FONT_MD = 2;   // counter values  (10×16)
static constexpr int FONT_LG = 4;   // big headline    (20×32)

class DisplayST7305 {
public:
  void begin();
  void clear();                                      // fill white
  void setPixel(int x, int y, bool on);             // on=true → black
  void drawText(int x, int y, const char* txt, int size = 1);
  void drawRect(int x, int y, int w, int h, bool on);
  void drawHLine(int x, int y, int w, bool on);
  void fillRect(int x, int y, int w, int h, bool on);
  void refresh();                                    // push fb → display

private:
  uint8_t* fb = nullptr;

  void writeCmd(uint8_t cmd);
  void writeData(uint8_t data);
  void initSequence();
};
