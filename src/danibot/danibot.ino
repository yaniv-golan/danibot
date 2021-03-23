
#include <Wire.h>
#include <SparkFun_Qwiic_Button.h>
#include <SerLCD.h> //http://librarymanager/All#SparkFun_SerLCD
#include <SparkFun_RV1805.h>
#include <EasyBuzzer.h>
#include <Adafruit_NeoPixel.h>
#include "WS2812_Definitions.h"


const uint8_t MainButtonDeviceID = 0x10;
const uint8_t SnoozeButtonDeviceID = 0x20;
const unsigned int BuzzerPin = 4;
const unsigned int LedsPin = 6;
const unsigned int LedsCount = 3;

RV1805 rtc;
QwiicButton mainButton;
QwiicButton snoozeButton;
SerLCD lcd; // Initialize the library with default I2C address 0x72
Adafruit_NeoPixel leds = Adafruit_NeoPixel(LedsCount, LedsPin, NEO_GRB + NEO_KHZ800);


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


void validateRange(struct DogOutRange r) {
  checkFatalRange(toMinutes(r.from) < toMinutes(r.to));
  checkFatalRange(toMinutes(r.trigger) > toMinutes(r.from));
  checkFatalRange(toMinutes(r.trigger) < toMinutes(r.to));
}



int toMinutes(Time tid) {
  return tid.hour * 60 + tid.minutes;
}

Time toTime(int minutes) {
  Time t;
  t.hour = minutes / 60;
  t.minutes = minutes % 60;
  t.seconds = 0;
  return t;
}


void debugSetup() {
  Serial.begin(115200);
}

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

void checkFatal(bool f, String msg) {
  if (!f)
    fatalError(msg);
}

void buttonsSetup() {
  debug("buttonsSetup BEGIN");
  checkFatal(mainButton.begin(MainButtonDeviceID), "Main button did not acknowledge");
  checkFatal(snoozeButton.begin(SnoozeButtonDeviceID), "Snooze button did not acknowledge");
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

  lcdOutClear("I am Danibot.");
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

  checkFatal(rtc.begin(), "Failed to initate clock");

  //Use the time from the Arduino compiler (build time) to set the RTC
  //Keep in mind that Arduino does not get the new compiler time every time it compiles. to ensure the proper time is loaded, open up a fresh version of the IDE and load the sketch.
  checkFatal(rtc.setToCompilerTime(), "Failed to set initial time");
  rtc.set24Hour();

  debug("clockSetup END");
}

void checkFatalRange(bool f) {
  checkFatal(f, "Invalid time range definitions");
}

void buzzerSetup() {
  EasyBuzzer.setPin(BuzzerPin);
}

void validateRanges() {
  checkFatalRange(toMinutes(dogOutRangesDP[dpMorning].to) < toMinutes(dogOutRangesDP[dpNoon].from)); // morning.to < noon.from
  checkFatalRange(toMinutes(dogOutRangesDP[dpNoon].to) < toMinutes(dogOutRangesDP[dpEvening].from)); // noon.to < evening.from
  checkFatalRange(toMinutes(dogOutRangesDP[dpEvening].from) > toMinutes(dogOutRangesDP[dpMorning].from)); // evening.from > morning.fro
  validateRange(dogOutRangesDP[dpMorning]);
  validateRange(dogOutRangesDP[dpNoon]);
  validateRange(dogOutRangesDP[dpEvening]);
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
  memcpy(dogOutRangesDP, defaultDogOutRangesDP, sizeof(DogOutRange));
}

void beepError() {
  // todo
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
  int secondsBetweenBeeps;
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
    0, // beepFreq
    0 // secondsBetweenBeeps
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

/*const int flashIntervalPerReminderLevel[] = {
  0,

  }*/


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
  for (int i = 0; i < LedsCount; i++)
  {
    leds.setPixelColor(i, 0);
  }
}

void ledsSetup() {
  leds.begin();
  clearLEDs();
  leds.show();
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
  resetStatus();
  lcdOutClear("All systems OK, good to go.");
  delay(1000);
  lcd.clear();
  debug("setup END");
}


void updateLed(DayPart dp) {
  switch (ledStatusDP[dp]) {
    case lsNo:
      leds.setPixelColor(dp, RED);
      break;
    case lsYes:
      leds.setPixelColor(dp, GREEN);
      break;
    case lsFlashing:
      leds.setPixelColor(dp, YELLOW);
      break;
  }  
}

void beepIfNeeded() {
  if (reminderLevel = rl0)
    return;

  
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
  leds.show();

  s = "";
  if (reminderLevel > rlNone) {
    s = reminderMessage;
  }
  lcdOut(1, s);

  beepIfNeeded();
}

// Read from serial a time hh:mm, sets the RTC to this time
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
