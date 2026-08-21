// ===================================================================
// WRO Future Engineers - Open Round
//
// ESP32 vehicle: 3x HC-SR04 ultrasonic + BNO055 IMU, servo steering,
// single DC drive motor. Runs 3 laps, then returns to the start.
// Started by a "START" command sent over the USB serial link.
//
// See README.md for wiring, tuning constants and calibration notes.
// ===================================================================

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

// ==== PIN Definitions ====
#define PIN_START_BTN 15   // push button, active low

#define PIN_MOTOR_A 13
#define PIN_MOTOR_B 12
#define PIN_MOTOR_PWM 25

#define PIN_STEER 4
#define STEER_FULL_LEFT 25
#define STEER_FULL_RIGHT 145
#define STEER_CENTRE 94    // TODO: trim per car, see README

// Front Ultrasonic
#define PIN_TRIG_F 5
#define PIN_ECHO_F 18

// Right Ultrasonic
#define PIN_TRIG_R 17
#define PIN_ECHO_R 16

// Left Ultrasonic
#define PIN_TRIG_L 26
#define PIN_ECHO_L 27

// ==== Tuning Parameters ====
const int BACKUP_MS = 700;          // TODO: calibrate, should back up ~10cm

// PI controller on heading
float GAIN_P = 6.0;                 // reaction to current error
float GAIN_I = 0.2;                 // removes long-term drift
const float CORRECTION_CAP = 22.0;  // largest steering nudge allowed
const float WINDUP_CAP = 150.0;     // anti-windup limit on the integral
const float SLOP = 0.6;             // error below this counts as straight

// Throttle levels (0-255)
const int PWM_CRUISE = 150;
const int PWM_CORNER = 160;
const int PWM_BACKUP = 120;

// Range thresholds, cm
const float RANGE_BLOCKED = 25.0;   // panic reverse below this
const float RANGE_CORNER = 65.0;    // front wall close enough to turn
const float RANGE_OPEN = 90.0;      // side gap that counts as open
const float HOME_TOLERANCE = 5.0;   // how close to the start line is "home"

// ==== Global Variables ====
Adafruit_BNO055 imu = Adafruit_BNO055(55, 0x28);
Servo steerServo;

// Lap bookkeeping
int cornerCount = 0;
int lapsDone = 0;
int cornersPerLap = 4;              // placeholder, measured on the scouting lap
bool scoutingLap = true;

// State flags
bool homingMode = false;
bool cornering = false;             // disables the PI controller mid-turn

// Heading and position
float headingZero = 0;
float headingSetpoint = 0;
float errorSum = 0;
float homeFrontRange = 0;
bool homeRecorded = false;

// WiFi & Telnet for Debugging
// Fill in your own network before enabling startDebugLink().
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
WiFiServer debugServer(23);
WiFiClient debugClient;

// ==== WiFi and Logging Functions ====
void startDebugLink() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  debugServer.begin();
  debugServer.setNoDelay(true);
  Serial.println("Telnet server started");
}

void serviceDebugLink() {
  if (debugServer.hasClient()) {
    if (!debugClient || !debugClient.connected()) {
      if (debugClient) debugClient.stop();
      debugClient = debugServer.available();
      Serial.println("New Telnet client connected");
    } else {
      debugServer.available().stop();
    }
  }
}

void debugLog(String msg) {
  Serial.println(msg);
  if (debugClient && debugClient.connected()) {
    debugClient.println(msg);
  }
}

// ==== Sensor and Motor Functions ====
float pingRange(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long echoUs = pulseIn(echoPin, HIGH, 30000);
  if (echoUs == 0) return 999.0;
  float cm = echoUs * 0.034 / 2.0;
  return (cm > 0 && cm < 400) ? cm : 999.0;
}

float readHeading() {
  sensors_event_t imuEvent;
  imu.getEvent(&imuEvent, Adafruit_BNO055::VECTOR_EULER);
  float heading = imuEvent.orientation.x;
  // Convert 0-360 range to -180 to 180 for easier calculations
  if (heading > 180) heading -= 360;
  return heading - headingZero;
}

void motorForward(int pwm) {
  digitalWrite(PIN_MOTOR_A, LOW);
  digitalWrite(PIN_MOTOR_B, HIGH);
  analogWrite(PIN_MOTOR_PWM, pwm);
}

void motorReverse(int pwm) {
  digitalWrite(PIN_MOTOR_A, HIGH);
  digitalWrite(PIN_MOTOR_B, LOW);
  analogWrite(PIN_MOTOR_PWM, pwm);
}

void motorStop() {
  digitalWrite(PIN_MOTOR_A, HIGH);
  digitalWrite(PIN_MOTOR_B, HIGH);
  analogWrite(PIN_MOTOR_PWM, 0);
}

// ==== Navigation Logic ====
void backUpForCorner() {
  debugLog("Reversing before turn...");

  // Drive backward for a fixed duration
  motorReverse(PWM_BACKUP);
  delay(BACKUP_MS);

  // Stop the vehicle and pause briefly to settle before turning
  motorStop();
  delay(200);

  debugLog("Reversing complete. Initiating turn.");
}

void holdHeading(float heading) {
  if (cornering) return; // PI controller is off mid-turn

  float err = headingSetpoint - heading;
  // Handle angle wrap-around
  if (err > 180.0) err -= 360.0;
  if (err < -180.0) err += 360.0;

  // If we're close enough, just go straight
  if (fabs(err) < SLOP) {
    errorSum *= 0.9; // decay the integral slowly
    steerServo.write(STEER_CENTRE);
    return;
  }

  errorSum += err;
  // Prevent integral wind-up
  if (errorSum > WINDUP_CAP) errorSum = WINDUP_CAP;
  if (errorSum < -WINDUP_CAP) errorSum = -WINDUP_CAP;

  float nudge = (GAIN_P * err) + (GAIN_I * errorSum);

  // Clamp the correction value
  if (nudge > CORRECTION_CAP) nudge = CORRECTION_CAP;
  if (nudge < -CORRECTION_CAP) nudge = -CORRECTION_CAP;

  int steerAngle = STEER_CENTRE + (int)round(nudge);
  if (steerAngle < STEER_FULL_LEFT) steerAngle = STEER_FULL_LEFT;
  if (steerAngle > STEER_FULL_RIGHT) steerAngle = STEER_FULL_RIGHT;

  steerServo.write(steerAngle);
}

void cornerRight() {
  cornering = true;
  debugLog("=== STARTING RIGHT TURN ===");

  steerServo.write(STEER_FULL_RIGHT);
  delay(300);
  motorForward(PWM_CORNER);

  // Wait until the turn is mostly complete
  float aim = headingSetpoint + 89.5;
  if (aim > 180.0) aim -= 360.0;

  unsigned long started = millis();
  while(true) {
    float err = aim - readHeading();
    if (err > 180.0) err -= 360.0;
    if (err < -180.0) err += 360.0;

    if (fabs(err) < 8.0) break; // close enough, exit
    if (millis() - started > 4000) { // safety timeout
      debugLog("!! Turn timeout !!");
      break;
    }
    delay(20);
  }

  // Stop and stabilize
  motorStop();
  delay(200);
  steerServo.write(STEER_CENTRE);
  delay(400);

  // Step the target off the PREVIOUS target, not off the measured heading,
  // so per-corner error cannot accumulate across the race.
  headingSetpoint += 90.0;
  if (headingSetpoint > 180.0) headingSetpoint -= 360.0;

  errorSum = 0; // reset integral so the straight run starts clean
  cornerCount++;
  cornering = false;

  debugLog("=== RIGHT TURN COMPLETED! New Target: " + String(headingSetpoint, 1) + " ===");
}

void cornerLeft() {
  cornering = true;
  debugLog("=== STARTING LEFT TURN ===");

  steerServo.write(STEER_FULL_LEFT);
  delay(300);
  motorForward(PWM_CORNER);

  float aim = headingSetpoint - 89.5;
  if (aim < -180.0) aim += 360.0;

  unsigned long started = millis();
  while(true) {
    float err = aim - readHeading();
    if (err > 180.0) err -= 360.0;
    if (err < -180.0) err += 360.0;

    if (fabs(err) < 8.0) break;
    if (millis() - started > 4000) {
      debugLog("!! Turn timeout !!");
      break;
    }
    delay(20);
  }

  motorStop();
  delay(200);
  steerServo.write(STEER_CENTRE);
  delay(400);

  // Same anchoring rule as the right turn.
  headingSetpoint -= 90.0;
  if (headingSetpoint < -180.0) headingSetpoint += 360.0;

  errorSum = 0; // reset integral
  cornerCount++;
  cornering = false;

  debugLog("=== LEFT TURN COMPLETED! New Target: " + String(headingSetpoint, 1) + " ===");
}

void backOffFromWall() {
  debugLog("Too close! Moving backward...");
  motorReverse(PWM_BACKUP);

  unsigned long started = millis();
  while (pingRange(PIN_TRIG_F, PIN_ECHO_F) < 40) {
    if(millis() - started > 2000) break; // safety timeout
    delay(50);
  }

  motorStop();
  delay(200);
  debugLog("Safe distance reached.");
}

// ==== Lap and Mission Logic ====
void finishScoutingLap() {
  if (scoutingLap) {
    cornersPerLap = cornerCount;
    scoutingLap = false;
    lapsDone = 1;
    debugLog("========================");
    debugLog("FIRST LAP COMPLETED! Detected " + String(cornersPerLap) + " turns per lap.");
    debugLog("========================");
  }
}

void updateLapCount() {
  if (scoutingLap && cornerCount >= 4) {
    finishScoutingLap();
    return;
  }
  if (scoutingLap) return;

  if (cornerCount >= cornersPerLap * (lapsDone + 1)) {
    lapsDone++;
    debugLog("========================");
    debugLog("LAP " + String(lapsDone) + " COMPLETED!");
    debugLog("========================");

    if (lapsDone >= 3) {
      debugLog("TARGET LAPS COMPLETED! Now returning to start...");
      homingMode = true;
    }
  }
}

void markStartLine() {
  homeFrontRange = pingRange(PIN_TRIG_F, PIN_ECHO_F);
  homeRecorded = true;
  debugLog("========================");
  debugLog("STARTING POSITION RECORDED: " + String(homeFrontRange, 1) + "cm");
  debugLog("========================");
}

void navigateHome(float rangeF, float rangeR) {
  if (fabs(rangeF - homeFrontRange) <= HOME_TOLERANCE) {
    debugLog("========================");
    debugLog("STARTING POSITION REACHED! MISSION COMPLETE!");
    debugLog("========================");
    motorStop();
    while (true) { delay(1000); } // end of mission
  }

  // Same wall-following rules, slightly looser thresholds
  if (rangeF < 75 && rangeR > 90) {
    cornerRight();
  } else if (rangeF < 70 && rangeR < 75) {
    cornerLeft();
  } else {
    motorForward(PWM_CRUISE);
    holdHeading(readHeading());
  }
}

// ===================================================================
// ============================= SETUP ===============================
// ===================================================================
void setup() {
  Serial.begin(115200);
  //startDebugLink();

  //pinMode(PIN_START_BTN, INPUT_PULLUP);
  pinMode(PIN_MOTOR_A, OUTPUT);
  pinMode(PIN_MOTOR_B, OUTPUT);
  pinMode(PIN_MOTOR_PWM, OUTPUT);
  pinMode(PIN_TRIG_F, OUTPUT);
  pinMode(PIN_ECHO_F, INPUT);
  pinMode(PIN_TRIG_R, OUTPUT);
  pinMode(PIN_ECHO_R, INPUT);
  pinMode(PIN_TRIG_L, OUTPUT);
  pinMode(PIN_ECHO_L, INPUT);

  debugLog("Waiting for START command over serial...");

  // === Wait for the START command over serial ===
  String rxBuffer = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;
      rxBuffer += c;
    }
    if (rxBuffer == "START") break;
  }

  debugLog("START command received!");
  /*debugLog("Press and hold the button for 1 second to start...");
  unsigned long heldSince = millis();
  while (true) {
    if (digitalRead(PIN_START_BTN) == LOW) {
      if (millis() - heldSince > 100) break;
    } else {
      heldSince = millis();
    }
    delay(10);
  }
  debugLog("Button pressed! Initializing...");*/

  steerServo.attach(PIN_STEER);
  steerServo.write(STEER_CENTRE);

  Wire.begin(21, 22);
  if (!imu.begin()) {
    debugLog("FATAL: No BNO055 detected!");
    while (1);
  }
  imu.setMode(OPERATION_MODE_NDOF);
  delay(1000);

  debugLog("Calibrating... Please wait.");
  // Wait for the sensor to settle into fusion mode
  while(imu.getMode() != OPERATION_MODE_NDOF) {
      delay(100);
  }

  headingZero = readHeading();
  headingSetpoint = readHeading();
  errorSum = 0;

  delay(1000);
  markStartLine();

  debugLog("System initialized and ready to run!");
}

// ===================================================================
// ============================== LOOP ===============================
// ===================================================================
void loop() {
  //serviceDebugLink();

  float heading = readHeading();
  float rangeF = pingRange(PIN_TRIG_F, PIN_ECHO_F);
  float rangeR = pingRange(PIN_TRIG_R, PIN_ECHO_R);
  float rangeL = pingRange(PIN_TRIG_L, PIN_ECHO_L);

  if (homingMode) {
    navigateHome(rangeF, rangeR);
    return;
  }

  updateLapCount();

  if (rangeF < RANGE_BLOCKED) {
    backOffFromWall();
    //cornerRight(); // optionally turn after reversing to avoid getting stuck
    return;
  }

  if (rangeF < RANGE_CORNER && rangeR > RANGE_OPEN && rangeL < RANGE_OPEN) {
    backUpForCorner();
    cornerRight();

  } else if (rangeF < RANGE_CORNER && rangeL > RANGE_OPEN && rangeR < RANGE_OPEN) {
    backUpForCorner();
    cornerLeft();
  } else {
    motorForward(PWM_CRUISE);
    holdHeading(heading);
  }

  delay(50);
}
