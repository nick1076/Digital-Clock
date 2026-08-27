
//   Encoder : brightness, 5 % per detent, 0-100 %
//   Encoder push switch (ONOFF PC2) not used yet

#include <Arduino.h>
#include <Wire.h>

const uint8_t DIN      = PIN_PA3;
const uint8_t CLK      = PIN_PA2;
const uint8_t LATCH    = PIN_PA4;
const uint8_t OE       = PIN_PA6;
const uint8_t BL_PWM   = PIN_PA5;  // HIGH = on, PWM = dim
const uint8_t ENC_A    = PIN_PB4;  // encoder A, RC filtered, external pull-up
const uint8_t ENC_B    = PIN_PB5;  // encoder B
const uint8_t BTN_SET  = PIN_PC0;
const uint8_t BTN_WAKE = PIN_PC1;

const uint8_t RTC_ADDR = 0x52;     // RV-3028-C7

const uint8_t DOT1 = 1 << 0, DOT2 = 1 << 1, AM = 1 << 2, PM = 1 << 3;
const uint8_t BLANK = 15;          // 4511 blanks the digit for 10-15

enum Mode { RUN, SET_HOUR, SET_MIN };
struct Button { uint8_t pin; bool down; uint32_t tChange, tRepeat; };

Mode    mode = RUN;
uint8_t hour = 0, minute = 0, second = 0;  // last RTC read, 24-hour
uint8_t setHour, setMinute;                // values being edited
uint8_t brightness = 100;                  // percent
Button  setBtn  = { BTN_SET };
Button  wakeBtn = { BTN_WAKE };


//Raw binary for 7 segments, BCD to 7-segment on hardware
void show(uint8_t s1, uint8_t s2, uint8_t s3, uint8_t s4, uint8_t leds) {
  digitalWrite(LATCH, LOW);
  shiftOut(DIN, CLK, MSBFIRST, leds);                              // U8: indicators
  shiftOut(DIN, CLK, MSBFIRST, ((s4 & 0x0F) << 4) | (s3 & 0x0F));  // U6: SEG4, SEG3
  shiftOut(DIN, CLK, MSBFIRST, ((s2 & 0x0F) << 4) | (s1 & 0x0F));  // U4: SEG2, SEG1
  digitalWrite(LATCH, HIGH);                                       // all 24 outputs update at once
}

// 24-hour in, 12-hour + AM/PM out
// hideHours / hideMinutes blank that field (used for the set-mode blink)
void showTime(uint8_t hour24, uint8_t minute, bool dotsOn, bool hideHours, bool hideMinutes) {
  uint8_t h = hour24 % 12;
  if (h == 0) h = 12;
  uint8_t leds = (hour24 >= 12 ? PM : AM) | (dotsOn ? DOT1 | DOT2 : 0);
  uint8_t s1 = hideHours   ? BLANK : (h / 10 ? h / 10 : BLANK);
  uint8_t s2 = hideHours   ? BLANK : h % 10;
  uint8_t s3 = hideMinutes ? BLANK : minute / 10;
  uint8_t s4 = hideMinutes ? BLANK : minute % 10;
  show(s1, s2, s3, s4, leds);
}

void setBrightness(uint8_t percent) {
  analogWrite(BL_PWM, (uint16_t)percent * 255 / 100);   // 0 = off, 100 = full on
}

// ================= RTC (RV-3028, 24-hour mode; BCD regs 0x00 sec, 0x01 min, 0x02 hour) =================
uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t bin2bcd(uint8_t v) { return (v / 10) << 4 | (v % 10); }

// Returns false (and leaves the values alone) if the RTC doesn't answer
bool rtcRead(uint8_t &h, uint8_t &m, uint8_t &s) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)3) != 3) return false;
  s = bcd2bin(Wire.read());
  m = bcd2bin(Wire.read());
  h = bcd2bin(Wire.read());
  return true;
}

void rtcWrite(uint8_t h, uint8_t m) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0);       // seconds -> 0
  Wire.write(bin2bcd(m));
  Wire.write(bin2bcd(h));
  Wire.endTransmission();
}


// Quadrature decoder: +1/-1 once per detent (four state changes), 0 otherwise.
// Bounce only walks back and forth between neighbouring states, so it can't produce a count.
int8_t readEncoder() {
  static const int8_t dir[16] = { 0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0 };
  static uint8_t prev = 3;      // both contacts open at a detent
  static int8_t  acc  = 0;
  uint8_t cur = digitalRead(ENC_A) << 1 | digitalRead(ENC_B);
  acc += dir[prev << 2 | cur];
  prev = cur;
  if (acc >= 4)  { acc -= 4; return +1; }
  if (acc <= -4) { acc += 4; return -1; }
  return 0;
}

// True once on a debounced press - with repeat also true every 120 ms once held for 500 ms
bool pressed(Button &b, bool repeat) {
  bool raw = !digitalRead(b.pin);   // active low
  uint32_t now = millis();
  if (raw != b.down && now - b.tChange > 30) {
    b.down = raw;
    b.tChange = b.tRepeat = now;
    return raw;
  }
  if (repeat && b.down && now - b.tChange > 500 && now - b.tRepeat > 120) {
    b.tRepeat = now;
    return true;
  }
  return false;
}





void setup() {
  pinMode(DIN, OUTPUT);  pinMode(CLK, OUTPUT);  pinMode(LATCH, OUTPUT);
  pinMode(OE, OUTPUT);   pinMode(BL_PWM, OUTPUT);
  pinMode(ENC_A, INPUT); pinMode(ENC_B, INPUT);
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_WAKE, INPUT_PULLUP);

  Wire.begin();
  rtcRead(hour, minute, second);

  show(BLANK, BLANK, BLANK, BLANK, 0);   // clean frame before enabling outputs
  digitalWrite(OE, LOW);
  setBrightness(brightness);
}

void loop() {
  uint32_t now = millis();

  // Encoder -> brightness
  int8_t step = readEncoder();
  if (step) {
    brightness = constrain(brightness + 5 * step, 0, 100);
    setBrightness(brightness);
  }

  // SET: RUN -> hours -> minutes -> save
  if (pressed(setBtn, false)) {
    if (mode == RUN)           { setHour = hour; setMinute = minute; mode = SET_HOUR; }
    else if (mode == SET_HOUR) { mode = SET_MIN; }
    else {
      rtcWrite(setHour, setMinute);
      hour = setHour; minute = setMinute; second = 0;
      mode = RUN;
    }
  }

  // WAKE: +1 on the field being edited
  if (pressed(wakeBtn, true) && mode != RUN) {
    if (mode == SET_HOUR) setHour   = (setHour + 1) % 24;
    else                  setMinute = (setMinute + 1) % 60;
  }

  // Poll the RTC every 100 ms, refresh the display every 50 ms
  static uint32_t tRtc = 0, tDisp = 0;
  if (now - tRtc >= 100) { tRtc = now; rtcRead(hour, minute, second); }
  if (now - tDisp >= 50) {
    tDisp = now;
    if (mode == RUN) {
      showTime(hour, minute, second & 1, false, false);   // colon blinks with the seconds
    } else {
      bool blink = (now / 250) & 1;                       // 2 Hz on the field being set
      showTime(setHour, setMinute, true, mode == SET_HOUR && blink, mode == SET_MIN && blink);
    }
  }
}
