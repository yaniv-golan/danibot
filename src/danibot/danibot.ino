
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

// Ranges - from-hour,from-minute, to-hour, to-minute, trigger-hour, trigger-minute
const byte RANGE_FROM_H = 0;
const byte RANGE_FROM_M = 1;
const byte RANGE_TO_H = 2;
const byte RANGE_TO_M = 3;
const byte RANGE_TRIGGER_H = 4;
const byte RANGE_TRIGGER_M = 5;
const byte morningRange[] = {5, 0, 10, 0, 7, 45};
const byte noonRange[] = {15, 0, 18, 0, 16, 45};
const byte eveningRange[] = {19, 0, 22, 0, 19, 45};

int hourMinToDayMin(uint8_t h, uint8_t m) {
  return h * 60 + m;
}

const int morningRange_Min[] = {
  hourMinToDayMin(morningRange[RANGE_FROM_H], morningRange[RANGE_FROM_M]),
  hourMinToDayMin(morningRange[RANGE_TO_H], morningRange[RANGE_TO_M]),
  hourMinToDayMin(morningRange[RANGE_TRIGGER_H], morningRange[RANGE_TRIGGER_M]),
};

const int noonRange_Min[] = {
  hourMinToDayMin(noonRange[RANGE_FROM_H], noonRange[RANGE_FROM_M]),
  hourMinToDayMin(noonRange[RANGE_TO_H], noonRange[RANGE_TO_M]),
  hourMinToDayMin(noonRange[RANGE_TRIGGER_H], noonRange[RANGE_TRIGGER_M]),
};

const int eveningRange_Min[] = {
  hourMinToDayMin(eveningRange[RANGE_FROM_H], eveningRange[RANGE_FROM_M]),
  hourMinToDayMin(eveningRange[RANGE_TO_H], eveningRange[RANGE_TO_M]),
  hourMinToDayMin(eveningRange[RANGE_TRIGGER_H], eveningRange[RANGE_TRIGGER_M]),
};

const byte RANGE_FROM = 0;
const byte RANGE_TO = 1;
const byte RANGE_TRIGGER = 2;


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


void validateRangeMin(int range[]) {
  checkFatalRange(range[RANGE_FROM] < range[RANGE_TO]);
  checkFatalRange(range[RANGE_TRIGGER] > range[RANGE_FROM]);
  checkFatalRange(range[RANGE_TRIGGER] < range[RANGE_TO]);
}

void validateRanges() {
  checkFatalRange(morningRange_Min[RANGE_TO] < noonRange_Min[RANGE_FROM]); // morning.to < noon.from
  checkFatalRange(noonRange_Min[RANGE_TO] < eveningRange_Min[RANGE_FROM]); // noon.to < evening.from
  checkFatalRange(eveningRange_Min[RANGE_FROM] > morningRange_Min[RANGE_FROM]); // evening.from > morning.from
  validateRangeMin(morningRange_Min);
  validateRangeMin(noonRange_Min);
  validateRangeMin(eveningRange_Min);
}

int currentTimeMin;
uint8_t lastCheckDate;
const int TIME_NONE = -1;
int snoozeTime_Min = TIME_NONE;
int morningTimeDogOut_Min = TIME_NONE;
int noonTimeDogOut_Min = TIME_NONE;
int eveningTimeDogOut_Min = TIME_NONE;
String mostRecentDogOutTime = "";

void resetStatus() {
  snoozeTime_Min = TIME_NONE;
  morningTimeDogOut_Min = TIME_NONE;
  noonTimeDogOut_Min = TIME_NONE;
  eveningTimeDogOut_Min = TIME_NONE;
  mostRecentDogOutTime = "";
}

void beepError() {
  // todo
}

bool isInRange(int time_Min, int range[]) {
  return ((time_Min >= range[RANGE_FROM]) && (time_Min <= range[RANGE_TO]));
}

void updateStatus() {
  int currentTime_h = rtc.getHours();
  int currentTime_m = rtc.getMinutes();
  int currentTime_Min = hourMinToDayMin(rtc.getHours(), rtc.getMinutes());

  uint8_t currentDate = rtc.getDate();
  if (currentDate != lastCheckDate) {
    debug("Day rollover");
    resetStatus();
    lastCheckDate = currentDate;
  }

  if (mainButton.hasBeenClicked()) {
    mainButton.clearEventBits();
    debug("Main button clicked");
    if (isInRange(currentTime_Min, morningRange_Min)) {
      debug("morning");
      morningTimeDogOut_Min = currentTime_Min;
    } else if (isInRange(currentTime_Min, noonRange_Min)) {
      debug("noon");
      noonTimeDogOut_Min = currentTime_Min;
    } else if (isInRange(currentTime_Min, eveningRange_Min)) {
      debug("evening");
      eveningTimeDogOut_Min = currentTime_Min;
    } else {
      debug("NOT IN ANY RANGE");
    }
    
    mostRecentDogOutTime = String(currentTime_h) + ":" + String(currentTime_m);
    
    // h:m, h:mm -> 0h:m, 0h:mm
    if (mostRecentDogOutTime.indexOf(":") == 1)
      mostRecentDogOutTime = "0" + mostRecentDogOutTime;
    // hh:m -> hh:0m
    if (mostRecentDogOutTime.length() == 4)
      mostRecentDogOutTime = mostRecentDogOutTime.substring(0, 3) + "0" + mostRecentDogOutTime.charAt(3);
      
    debug("Most recent set to " + mostRecentDogOutTime);
  }

  if (snoozeButton.hasBeenClicked()) {
    snoozeButton.clearEventBits();
    debug("Snooze button clicked");
    snoozeTime_Min = currentTime_Min;
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
  if (mostRecentDogOutTime.length() > 0) {
    s = s + " " + mostRecentDogOutTime;
  }

  lcdOut(0, s);

  s = "";
  if (morningTimeDogOut_Min == TIME_NONE) {
    s = "0";
  } else {
    s = "X";
  }
  if (noonTimeDogOut_Min == TIME_NONE) {
    s = s + "0";
  } else {
    s = s + "X";
  }
  if (eveningTimeDogOut_Min == TIME_NONE) {
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
