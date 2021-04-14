
#include <Wire.h>
#include <SparkFun_Qwiic_Button.h>
#include <SerLCD.h> //http://librarymanager/All#SparkFun_SerLCD
#include <SparkFun_RV1805.h>
#include <EasyBuzzer.h>
#include <ChainableLED.h>
#include "WS2812_Definitions.h"


const uint8_t MainButtonDeviceID = 0x10;
const uint8_t SnoozeButtonDeviceID = 0x20;
const unsigned int BuzzerPin = 4;
const unsigned int LedsPin1 = 2;
const unsigned int LedsPin2 = 3;
const unsigned int LedsCount = 3;

RV1805 rtc;
QwiicButton mainButton;
QwiicButton snoozeButton;
SerLCD lcd; // Initialize the library with default I2C address 0x72
ChainableLED leds(LedsPin1, LedsPin2, LedsCount);

// Time to wait before becoming more annoying, counted from the alarm time
const byte MinutesWaitBeforeReminderLevel2 = 5;
const byte MinutesWaitBeforeReminderLevel3 = 15;
const byte MinutesToSnooze = 5;

enum DayPart {
  dpMorning,
  dpNoon,
  dpEvening
};

struct Time {
  byte hour;
  byte minutes;
  byte seconds;
};

struct DogOutRange {
  Time from;
  Time to;
  Time trigger;
};

const DogOutRange defaultDogOutRangesDP[] = {
  {
    {5, 0},
    {10, 0},
    {7, 45}
  },
  {
    {15, 0},
    {18, 0},
    {16, 45}
  },
  {
    {19, 0},
    {22, 0},
    {19, 45}
  }
};

DogOutRange dogOutRangesDP[] = {
  {
    {5, 0},
    {10, 0},
    {7, 45}
  },
  {
    {15, 0},
    {18, 0},
    {16, 45}
  },
  {
    {19, 0},
    {22, 0},
    {19, 45}
  }
};


const Time tidNULL = {99, 99, 99};
Time currentTime;
long lastBeepSeconds = 0;




void validateRange(struct DogOutRange r) {
  checkFatalRange(toMinutes(r.from) < toMinutes(r.to));
  checkFatalRange(toMinutes(r.trigger) > toMinutes(r.from));
  checkFatalRange(toMinutes(r.trigger) < toMinutes(r.to));
}



/**************************************************
 Generic Utility functions 
**************************************************/

void debug(String msg) {
  Serial.println(msg);
}

void lcdOutClear(String s) {
  lcd.clear();
  lcd.print(s);
}

void lcdOut(int row, String s) {
  // pad strings to fill an entire row
  const String pad = "                "; // 16 spaces
  String ss = s + pad.substring(0, pad.length() - s.length());
  lcd.setCursor(0, row);
  lcd.print(ss);
}

void error(String msg) {
  debug("ERROR " + msg);
  lcdOutClear(msg);
}

void fatalError(String msg) {
  error("FATAL: " + msg);
  while (1); // loop forever
}

void fatalError(bool f, String msg) {
  if (!f)
    fatalError(msg);
}


/**************************************************
 Color Utility functions 
**************************************************/


uint32_t   Color(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}

uint32_t   Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}

void unpackColor(uint32_t color, byte& r, byte& g, byte &b, byte &w) {
  w = (color >> 24) & 0xff; // white
  r = (color >> 16) & 0xff; // red
  g = (color >> 8) & 0xff; // green
  b = color  & 0xff; // blue
}

void unpackColor(uint32_t color, byte& r, byte& g, byte& b) {
  byte w;
  unpackColor(color, r, g, b, w);
}

// Adapted from https://stackoverflow.com/questions/39118528/rgb-to-hsl-conversion
float rgb2hue(byte r, byte g, byte b) {
  r /= 255;
  g /= 255;
  b /= 255;
  byte maxRGB = max(max(r, g), b);
  byte minRGB = min(min(r, g), b);
  byte c   = maxRGB - minRGB;
  float hue;
  if (c == 0) {
    hue = 0;
  } else {
    float segment;
    float shift;
    if (maxRGB = r) {
      segment = (g - b) / c;
      shift   = 0 / 60;       // R° / (360° / hex sides)
      if (segment < 0) {          // hue > 180, full rotation
        shift = 360 / 60;         // R° / (360° / hex sides)
      }
      hue = segment + shift;
    } else if (maxRGB = g) {
      segment = (b - r) / c;
      shift   = 120 / 60;     // G° / (360° / hex sides)
      hue = segment + shift;
    } else if (maxRGB = b) {
      segment = (r - g) / c;
      shift   = 240 / 60;     // B° / (360° / hex sides)
      hue = segment + shift;
    }
  }
  return hue * 60; // hue is in [0,6], scale it up
}

/**************************************************
 Time functions 
**************************************************/

bool tidIsNull(Time& tid) {
  return ((tid.hour == tidNULL.hour) && (tid.minutes == tidNULL.minutes) && (tid.seconds == tidNULL.seconds));
}

String timeToString(Time tid, bool withSeconds = false) {
  String s = "";
  if (tid.hour <= 9)
    s = "0";
  s += String(tid.hour);
  s += ":";
  if (tid.minutes <= 9)
    s += "0";
  s += String(tid.minutes);
  if (withSeconds) {
    s += ":";
    if (tid.seconds <= 9)
      s += "0";
    s += String(tid.seconds);
  }
  return s;
}

int toMinutes(Time tid) {
  return tid.hour * 60 + tid.minutes;
}

long toSeconds(Time tid) {
  long s = (long)toMinutes(tid) * 60 + tid.seconds;
  return s;
}

Time toTime(int minutes) {
  Time t;
  t.hour = minutes / 60;
  t.minutes = minutes % 60;
  t.seconds = 0;
  return t;
}

/**************************************************
 Setup functions 
**************************************************/
 
void debugSetup() {
  Serial.begin(115200);
}


void buttonsSetup() {
  debug("buttonsSetup BEGIN");
  fatalError(mainButton.begin(MainButtonDeviceID), "Main button did not acknowledge");
  fatalError(snoozeButton.begin(SnoozeButtonDeviceID), "Snooze button did not acknowledge");
  mainButton.LEDoff();
  snoozeButton.LEDoff();
  debug("buttonsSetup END");
}

void lcdSetup() {
  debug("lcdSetup");
  lcd.begin(Wire); //Set up the LCD for I2C communication

  lcd.disableSystemMessages();

  lcd.setBacklight(255, 255, 255); //Set backlight to bright white
  lcd.setContrast(5); //Set contrast. Lower to 0 for higher contrast.

  lcdOutClear("I am the Danibot.");
  delay(1000);

  lcd.saveSplash(); //Save this current text as the splash screen at next power on

  lcd.enableSplash(); //This will cause the splash to be displayed at power on
}

void I2CSetup() {
  debug("I2CSetup");
  Wire.begin(); //Join I2C bus
}

void clockSetup() {
  debug("clockSetup BEGIN");

  fatalError(rtc.begin(), "Failed to initate clock");

  //Use the time from the Arduino compiler (build time) to set the RTC
  //Keep in mind that Arduino does not get the new compiler time every time it compiles. to ensure the proper time is loaded, open up a fresh version of the IDE and load the sketch.
  fatalError(rtc.setToCompilerTime(), "Failed to set initial time");
  rtc.set24Hour();

  debug("clockSetup END");
}

void buzzerSetup() {
  EasyBuzzer.setPin(BuzzerPin);
}

void ledsSetup() {
  //leds.init();
}



void checkFatalRange(bool f) {
  fatalError(f, "Invalid time range definitions");
}


void validateRanges() {
  validateRange(dogOutRangesDP[dpMorning]);
  validateRange(dogOutRangesDP[dpNoon]);
  validateRange(dogOutRangesDP[dpEvening]);
  
  checkFatalRange(toMinutes(dogOutRangesDP[dpMorning].to) < toMinutes(dogOutRangesDP[dpNoon].from)); // morning.to < noon.from
  checkFatalRange(toMinutes(dogOutRangesDP[dpNoon].to) < toMinutes(dogOutRangesDP[dpEvening].from)); // noon.to < evening.from
  checkFatalRange(toMinutes(dogOutRangesDP[dpEvening].from) > toMinutes(dogOutRangesDP[dpMorning].from)); // evening.from > morning.fro
}

uint8_t lastCheckDate;
Time snoozeTime = tidNULL;
Time dogOutTimesDP[] = {
  tidNULL,
  tidNULL,
  tidNULL
};
Time mostRecentDogOutTime = tidNULL;


void resetStatus() {
  snoozeTime = tidNULL;
  dogOutTimesDP[dpMorning] = tidNULL;
  dogOutTimesDP[dpNoon] = tidNULL;
  dogOutTimesDP[dpEvening] = tidNULL;
  mostRecentDogOutTime = tidNULL;
  lastBeepSeconds = 0;
  memcpy(dogOutRangesDP, defaultDogOutRangesDP, sizeof(DogOutRange));
}

bool isInRange(struct Time t, struct DogOutRange r) {
  int tm = toMinutes(t);
  return ((tm >= toMinutes(r.from)) && (tm <= toMinutes(r.to)));
}


enum ReminderLevel {
  rlNone, // nothing to remind
  rl0,  // In the dog-out period, pre-trigger. led - gentle red pulse, screen - msg, beep - none
  rl1, // We just passed the trigger time. led - blink red/orange , screen - msg
  rl2, // led - blink red/orange , screen - msg, beep - 3 beeps every <SecondsBetweenLevel2Annoying>
  rl3 // // led - blink red/yellow  double speed, screen - msg, beep - 3 beeps every <SecondsBetweenLevel3Annoying>
};

ReminderLevel reminderLevel = rlNone;
String reminderMessage = "";

struct ReminderLevelParams {
  bool pulse;
  uint32_t ledOnColor;
  uint32_t ledOffColor;
  byte ledOnMsecs;
  byte ledOffMsecs;
  unsigned int beepFreq;
  unsigned int secondsBetweenBeeps;
};

const ReminderLevelParams reminderLevelParams_RL[] = {
  // rlNone
  {
  },
  // rl0
  {
    true, // pulse
    RED, // ledOnColor
    0, // ledOffColor
    500, // ledOnMsecs
    100, // ledOffMsecs
  },
  // rl1
  {
    false, // pulse
    RED, // ledOnColor
    ORANGE, // ledOffColor
    500, // ledOnMsecs
    100, // ledOffMsecs
    0, // beepFreq
    0 // secondsBetweenBeeps
  },
  // rl2
  {
    false, // pulse
    RED, // ledOnColor
    ORANGE, // ledOffColor
    500, // ledOnMsecs
    300, // ledOffMsecs
    1000, // beepFreq
    60 // secondsBetweenBeeps
  },
  // rl3
  {
    false, // pulse
    RED, // ledOnColor
    YELLOW, // ledOffColor
    500, // ledOnMsecs
    300, // ledOffMsecs
    1500, // beepFreq
    30 // secondsBetweenBeeps
  }
};

enum LedStatus {
  lsNo,
  lsYes,
  lsFlashing,
};

LedStatus ledStatusDP[] = {lsNo, lsNo, lsNo};

// Called when main button is clicked for each one of the 3 day-parts
// If currentTime is within the range of the relevant day-part,
// notes the time, and returns true.
bool isInRangeUpdateDogOut(Time currentTime, DayPart dp) {
  if (isInRange(currentTime, dogOutRangesDP[dp])) {
    dogOutTimesDP[dp] = currentTime;
    return true;
  } else
    return false;
}

// Update ledStatusDP[dp], and global reminderLevel.
// Note - before this is called (once for each DayPart), reminderLevel is set to rlNone
// Upon return, ledStatusDP[dp] will be set to
//    lsYes if the dog was let out in this period
//    lsNo if the dog was NOT let out in this period, but the time is before thr trigger time
//        (also set reminderLevel to rl1)
//    lsFlashing is the dog was NOT let out in this period, and we're past the trigger time. In that case, reminderLevel will also be set:
//        (also set reminderLevel to rl2 or rl3, depending on how much time passed since the trigger time)
void updateUIStatus(Time currentTime, DayPart dp) {
  if (tidIsNull(dogOutTimesDP[dp])) { // was the dog let out in this period?
    // NO - dog was not let out

    if (isInRange(currentTime, dogOutRangesDP[dp])) {
      // dog was NOT let out in thie period, and we're in the range

      reminderMessage = "> " + timeToString(dogOutRangesDP[dp].trigger);
      ledStatusDP[dp] = lsFlashing;

      int minutesSinceTrigger = toMinutes(currentTime) - toMinutes(dogOutRangesDP[dp].trigger);

      if (minutesSinceTrigger < 0) { // in period, but before trigger time
        reminderLevel = rl0;
      } else {
        if (minutesSinceTrigger > MinutesWaitBeforeReminderLevel3) {
          reminderLevel = rl3;
          reminderMessage += "!!!";
        } else if (minutesSinceTrigger > MinutesWaitBeforeReminderLevel2) {
          reminderLevel = rl2;
          reminderMessage += "!!";
        } else {
          reminderLevel = rl1;
          reminderMessage += "!";
        }
      }

    } else {
      // dog was NOT let out in thie period, but we're not in the range
      ledStatusDP[dp] = lsNo;
    }
  } else {
    // YES - dog was let out
    ledStatusDP[dp] = lsYes;
  }
}

void updateStatus() {
  currentTime.hour = rtc.getHours();
  currentTime.minutes = rtc.getMinutes();
  currentTime.seconds = rtc.getSeconds();

  // If new day, reset status
  uint8_t currentDate = rtc.getDate();
  if (currentDate != lastCheckDate) {
    debug("Day rollover");
    resetStatus();
    lastCheckDate = currentDate;
  }

  // Main button clicked -
  if (mainButton.hasBeenClicked()) {
    mainButton.clearEventBits();
    debug("Main button clicked");
    if (isInRangeUpdateDogOut(currentTime, dpMorning)) {
      debug("morning");
    } else if (isInRangeUpdateDogOut(currentTime, dpNoon)) {
      debug("noon");
    } else if (isInRangeUpdateDogOut(currentTime, dpEvening)) {
      debug("evening");
    } else {
      debug("NOT IN ANY RANGE");
    }

    mostRecentDogOutTime = currentTime;
  }

  if (snoozeButton.hasBeenClicked()) {
    snoozeButton.clearEventBits();
    debug("Snooze button clicked");
    snoozeTime = currentTime;
    EasyBuzzer.stopBeep();
  }

  reminderLevel = rlNone;

  updateUIStatus(currentTime, dpMorning);
  updateUIStatus(currentTime, dpNoon);
  updateUIStatus(currentTime, dpEvening);
}

bool isInSnooze() {
  if (tidIsNull(snoozeTime))
    return false;
  return (toMinutes(currentTime) - toMinutes(snoozeTime) <= MinutesToSnooze);
}

// Sets all LEDs to off, but DOES NOT update the display;
// call leds.show() to actually turn them off after this.
void clearLEDs()
{
  setDayPartLed(dpMorning, 0);
  setDayPartLed(dpNoon, 0);
  setDayPartLed(dpEvening, 0);
}


void setup() {
  debugSetup();
  debug("setup BEGIN");
  
  I2CSetup();
  lcdSetup();
  buttonsSetup();
  clockSetup();
  validateRanges();
  buzzerSetup();
  ledsSetup();
  
  clearLEDs();
  
  resetStatus();
  lcdOutClear("All systems OK, good to go.");
  delay(1000);
  lcd.clear();
  debug("setup END");
}



// I wired the LEDs in the wrong order, this is easier than rewiring them.
byte DayPartToLed(DayPart dp) {
  byte led;
  switch (dp) {
    case dpMorning:
      led = 2;
      break;
    case dpNoon:
      led = 1;
      break;
    case dpEvening:
      led = 0;
      break;
  };
  return led;
};

void setDayPartLed(DayPart dp, byte R, byte G, byte B) {
  leds.setColorRGB(DayPartToLed(dp), R, G, B);
}

void setDayPartLed(DayPart dp, float hue, float saturation, float brightness) {
  leds.setColorHSB(DayPartToLed(dp), hue, saturation, brightness);
}

void setDayPartLed(DayPart dp, byte R, byte G, byte B, float saturation, float brightness) {
  float hue = rgb2hue(R, G, B);
  setDayPartLed(dp, hue, saturation, brightness);
}

void setDayPartLed(DayPart dp, uint32_t color) {
  byte r;
  byte g;
  byte b;
  unpackColor(color, r, g, b);
  setDayPartLed(dp, r, g, b);
}

void setDayPartLed(DayPart dp, uint32_t color, float saturation, float brightness) {
  byte r;
  byte g;
  byte b;
  unpackColor(color, r, g, b);
  float hue = rgb2hue(r, g, b);
  setDayPartLed(dp, hue, saturation, brightness);
}

void updateLed(DayPart dp) {
  switch (ledStatusDP[dp]) {
    case lsNo:
      setDayPartLed(dp, RED);
      break;
    case lsYes:
      setDayPartLed(dp, GREEN);
      break;
    case lsFlashing:
      setDayPartLed(dp, YELLOW);
      break;
  }
}


void beepIfNeeded() {
  if (reminderLevel == rlNone)
    return;

  unsigned int beepFreq;
  unsigned int secondsBetweenBeeps;

  beepFreq = reminderLevelParams_RL[reminderLevel].beepFreq;
  secondsBetweenBeeps = reminderLevelParams_RL[reminderLevel].secondsBetweenBeeps;

  if (beepFreq == 0)
    return;

  long currentSeconds = toSeconds(currentTime);

  if (lastBeepSeconds != 0) {

    if (currentSeconds - lastBeepSeconds < secondsBetweenBeeps)
      return;
  }

  debug("beeping");

  EasyBuzzer.beep(beepFreq, 3);
  lastBeepSeconds = currentSeconds;

}

void updateUI() {
  String s;

  s = rtc.stringTime();
  if (!tidIsNull(mostRecentDogOutTime)) {
    s = s + " " + timeToString(mostRecentDogOutTime);
  }

  lcdOut(0, s);

  updateLed(dpMorning);
  updateLed(dpNoon);
  updateLed(dpEvening);

  s = "";
  if (reminderLevel > rlNone) {
    s = reminderMessage;
  }
  lcdOut(1, s);

  beepIfNeeded();
}

// Read from serial a time hh:mm, sets the RTC to this time
// This is useful for testing what happens at certain times
// Time format hh:mm
void readTimeFromSerial() {
  if (!Serial)
    return;
  if (Serial.available() > 0) {
    String s = Serial.readStringUntil('\n');
    debug("Request to set time to " + s);
    s.trim();
    int i = s.indexOf(":");
    if (i == -1) {
      debug("Invalid time format, should be hh:mm");
      return;
    }
    String h = s.substring(0, i);
    String m = s.substring(i + 1);
    debug("Parsed h = " + h + ", m = " + m);
    rtc.setHours(h.toInt());
    rtc.setMinutes(m.toInt());
  }
}

void loop() {
  EasyBuzzer.update();
  readTimeFromSerial();
  if (!rtc.updateTime())
    error("Failed to retrieve current time");
  updateStatus();
  updateUI();

  delay(20); //Don't hammer too hard on the I2C bus.
}
