// Minimal display driver for the ATtiny1616 clock board
// Board: ATtiny1616, megaTinyCore (Arduino IDE or PlatformIO)
//
// Chain: MCU -> U4 (SEG1/SEG2 BCD) -> U6 (SEG3/SEG4 BCD) -> U8 (indicator LEDs)
// Each 74HC595 nibble feeds a CD74HCT4511 with LE tied low (transparent), so the
// digits change the instant the 595s latch. BL_PWM drives all four 4511 /BL pins
// and the LED MOSFET, so one PWM pin sets brightness for everything.

#include <Arduino.h>

// --- MCU pins (from schematic) ---
const uint8_t DIN    = PIN_PA3;  // 74HC595 DS
const uint8_t CLK    = PIN_PA2;  // 74HC595 SHCP
const uint8_t LATCH  = PIN_PA4;  // 74HC595 STCP
const uint8_t OE     = PIN_PA6;  // 74HC595 /OE, has 10k pull-up -> must drive LOW
const uint8_t BL_PWM = PIN_PA5;  // 4511 /BL + Q1 gate: HIGH = on, PWM = dim

// --- Indicator bits (U8 Q0..Q3) ---
const uint8_t DOT1 = 1 << 0;   // CenterDotLED1 (top)
const uint8_t DOT2 = 1 << 1;   // CenterDotLED2 (bottom)
const uint8_t AM   = 1 << 2;   // AMIndicator
const uint8_t PM   = 1 << 3;   // PMIndicator

const uint8_t BLANK = 15;      // 4511 blanks the digit for any code 10-15

// Low level: push one 24-bit frame. s1..s4 = SEG1..SEG4, each 0-9 or BLANK.
void show(uint8_t s1, uint8_t s2, uint8_t s3, uint8_t s4, uint8_t leds) {
  digitalWrite(LATCH, LOW);
  shiftOut(DIN, CLK, MSBFIRST, leds);                              // U8: indicators
  shiftOut(DIN, CLK, MSBFIRST, ((s4 & 0x0F) << 4) | (s3 & 0x0F));  // U6: SEG4, SEG3
  shiftOut(DIN, CLK, MSBFIRST, ((s2 & 0x0F) << 4) | (s1 & 0x0F));  // U4: SEG2, SEG1
  digitalWrite(LATCH, HIGH);   // rising edge updates all 24 outputs at once
}

// Friendly: 12-hour time. hour 1-12, minute 0-59.
// isPM: true lights PM, false lights AM.   dotsOn: both center dots.
void showTime(uint8_t hour, uint8_t minute, bool isPM, bool dotsOn) {
  uint8_t leds = (isPM ? PM : AM) | (dotsOn ? (DOT1 | DOT2) : 0);
  uint8_t h10  = hour / 10;
  show(h10 ? h10 : BLANK, hour % 10, minute / 10, minute % 10, leds);  // no leading zero
}

void setup() {
  pinMode(DIN, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(LATCH, OUTPUT);
  pinMode(OE, OUTPUT);
  pinMode(BL_PWM, OUTPUT);

  show(BLANK, BLANK, BLANK, BLANK, 0);  // clean frame before enabling outputs
  digitalWrite(OE, LOW);                // 595 outputs on
  analogWrite(BL_PWM, 255);             // brightness 0-255 (255 = full on)
}

// Demo: 12:34 PM, center dots blink once per second
void loop() {
  static bool dots = false;
  dots = !dots;
  showTime(12, 34, true, dots);
  delay(1500);
}
