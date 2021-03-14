
#include <Wire.h>
#include <SparkFun_Qwiic_Button.h>
#include <SerLCD.h> //http://librarymanager/All#SparkFun_SerLCD
#include <SparkFun_RV1805.h>

RV1805 rtc;
QwiicButton mainButton;
QwiicButton snoozeButton;
SerLCD lcd; // Initialize the library with default I2C address 0x72

const byte RTC_ALARM_MODE_ONCE_PER_DAY = 4;

// Time to wait before becoming more annoying, counted from the alarm time
const byte MinutesToLevel2Annoying = 5;
const byte MinutesToLevel3Annoying = 15;
const byte SecondsBetweenLevel2Annoying = 60;
const byte SecondsBetweenLevel3Annoying = 30;

enum DayPart {
  dpMorning, 
  dpNoon, 
  dpEvening
};

struct TimeInDay {
  byte hour;
  byte minutes;
};

const TimeInDay tidNULL = {99,99};

bool tidIsNull(TimeInDay& tid) {
  return ((tid.hour == tidNULL.hour) && (tid.minutes == tidNULL.minutes));
}

String timeInDayToString(TimeInDay tid) {
  String s = "";
  if (tid.hour <= 9)
    s = "0";
  s += String(tid.hour);
  s += ":";
  if (tid.minutes <= 9)
     s += "0";
  s += String(tid.minutes);
  return s;
}

struct DogOutRange {
  TimeInDay from;
  TimeInDay to;
  TimeInDay trigger;
};

void validateRange(struct DogOutRange r) {
  checkFatalRange(toMinutes(r.from) < toMinutes(r.to));
  checkFatalRange(toMinutes(r.trigger) > toMinutes(r.from));
  checkFatalRange(toMinutes(r.trigger) < toMinutes(r.to));
}

const DogOutRange dogOutRanges[] = {
  {
    {5,0},
    {10,0},
    {7, 45}
  },
  {
    {15,0},
    {18,0},
    {16, 45}
  },
  {
    {19,0},
    {22,0},
    {19, 45}
  }
};

int toMinutes(TimeInDay tid) {
  return tid.hour * 60 + tid.minutes;
}

int daytimeToDayMinutes(uint8_t h, uint8_t m) {
  return h * 60 + m;
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
  checkFatal(mainButton.begin(0x10), "Main button did not acknowledge");
  checkFatal(snoozeButton.begin(0x20), "Snooze button did not acknowledge");
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


void validateRanges() {
  checkFatalRange(toMinutes(dogOutRanges[dpMorning].to) < toMinutes(dogOutRanges[dpNoon].from)); // morning.to < noon.from
  checkFatalRange(toMinutes(dogOutRanges[dpNoon].to) < toMinutes(dogOutRanges[dpEvening].from)); // noon.to < evening.from
  checkFatalRange(toMinutes(dogOutRanges[dpEvening].from) > toMinutes(dogOutRanges[dpMorning].from)); // evening.from > morning.fro
  validateRange(dogOutRanges[dpMorning]);
  validateRange(dogOutRanges[dpNoon]);
  validateRange(dogOutRanges[dpEvening]);
}

uint8_t lastCheckDate;
TimeInDay currentTime;
TimeInDay snoozeTime = tidNULL;
TimeInDay dogOutTimesDP[] = {
  tidNULL,
  tidNULL,
  tidNULL
};
TimeInDay mostRecentDogOutTime = tidNULL;


void resetStatus() {
  snoozeTime = tidNULL;
  dogOutTimesDP[dpMorning] = tidNULL;
  dogOutTimesDP[dpNoon] = tidNULL;
  dogOutTimesDP[dpEvening] = tidNULL;
  mostRecentDogOutTime = tidNULL;
}

void beepError() {
  // todo
}

bool isInRange(struct TimeInDay t, struct DogOutRange r) {
  int tm = toMinutes(t);
  return ((tm >= toMinutes(r.from)) && (tm <= toMinutes(r.to)));
 } 


enum AnnoyingLevel {
  al1,
  al2,
  al3
};

AnnoyingLevel uiAnnoyingLevel;

enum LedStatus {
  lsNo, 
  lsYes,
  lsFlashing1,
  lsFlashing2
};

LedStatus ledStatus[] = {lsNo,lsNo,lsNo};

void updateStatus() {
  TimeInDay currentTime;
  currentTime.hour = rtc.getHours();
  currentTime.minutes = rtc.getMinutes();

  uint8_t currentDate = rtc.getDate();
  if (currentDate != lastCheckDate) {
    debug("Day rollover");
    resetStatus();
    lastCheckDate = currentDate;
  }

  if (mainButton.hasBeenClicked()) {
    mainButton.clearEventBits();
    debug("Main button clicked");
    if (isInRange(currentTime, dogOutRanges[dpMorning])) {
      debug("morning");
      dogOutTimesDP[dpMorning] = currentTime;
    } else if (isInRange(currentTime, dogOutRanges[dpNoon])) {
      debug("noon");
      dogOutTimesDP[dpNoon] = currentTime;
    } else if (isInRange(currentTime, dogOutRanges[dpEvening])) {
      debug("evening");
      dogOutTimesDP[dpEvening] = currentTime;
    } else {
      debug("NOT IN ANY RANGE");
    }
    
    mostRecentDogOutTime = currentTime;
  }

  if (snoozeButton.hasBeenClicked()) {
    snoozeButton.clearEventBits();
    debug("Snooze button clicked");
    snoozeTime = currentTime;
  }

  if (tidIsNull(dogOutTimesDP[dpMorning])) {
    if (isInRange(currentTime, dogOutRanges[dpMorning])) {
                  
    }
  } else {
    ledStatus[dpMorning] = lsYes;
  }
  

}

void setup() {
  debugSetup();
  debug("setup BEGIN");
  I2CSetup();
  lcdSetup();
  buttonsSetup();
  clockSetup();
  validateRanges();
  resetStatus();
  lcdOutClear("All systems OK, good to go.");
  delay(1000);
  lcd.clear();
  debug("setup END");
}



void updateUI() {
  String s;

  s = rtc.stringTime();
  if (!tidIsNull(mostRecentDogOutTime)) {
    s = s + " " + timeInDayToString(mostRecentDogOutTime);
  }

  lcdOut(0, s);

  s = "";
  if (tidIsNull(dogOutTimesDP[dpMorning])) {
    s = "0";
  } else {
    s = "X";
  }
  if (tidIsNull(dogOutTimesDP[dpNoon])) {
    s = s + "0";
  } else {
    s = s + "X";
  }
  if (tidIsNull(dogOutTimesDP[dpEvening])) {
    s = s + "0";
  } else {
    s = s + "X";
  }

  lcdOut(1, s);

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
  readTimeFromSerial();
  if (!rtc.updateTime())
    error("Failed to retrieve current time");
  updateStatus();
  updateUI();
 
  delay(20); //Don't hammer too hard on the I2C bus.
}
