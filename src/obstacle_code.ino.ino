/* =============================================================================
 * WRO FUTURE ENGINEERS — OBSTACLE CHALLENGE FIRMWARE
 * ESP32 | BNO055 | HuskyLens | PCA9685-style servo steering | 3x HC-SR04
 * ============================================================================= */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include "HUSKYLENS.h"

// ==============================
// Hardware Pins
// ==============================
#define IN1 13
#define IN2 12
#define ENA 25
#define SERVO_PIN 4

#define TRIG_FRONT 5
#define ECHO_FRONT 18
#define TRIG_RIGHT 17
#define ECHO_RIGHT 16
#define TRIG_LEFT 26
#define ECHO_LEFT 27

// ==============================
// Turn Stop Settings
// ==============================
int TURN = 0;
#define STOP_AFTER_TIME 4500
#define STOP_BOT_AFTER_TURNS 12
unsigned long turnStartTime = 0;

// ==============================
// Servo Constraints
// ==============================
#define SERVO_CENTER_DEG   90
#define SERVO_MIN_DEG      28
#define SERVO_MAX_DEG      142
#define SERVO_SHIFT_LEFT   40
#define SERVO_SHIFT_RIGHT  148

// ==============================
// Sensors
// ==============================
Servo steeringServo;
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire);
HUSKYLENS huskylens;

#define ID_GREEN 1
#define ID_RED 2
#define ID_MAGENTA 3

// Vision thresholds (Adjusted to trigger earlier to prevent dragging pillars)
#define HUSKY_SOFT_TRIGGER 150   
#define HUSKY_AREA_TRIGGER 3000  

// ==============================
// Drive params
// ==============================
#define BASE_SPEED   135
#define TURN_SPEED   120
#define SHIFT_SPEED  130
#define WALL_STOP_DIST 90
#define TURN_DONE_TOL  3

// ==============================
// Pillar cooldown / lockout
// ==============================
#define PILLAR_COOLDOWN_MS      1800   
#define PILLAR_COOLDOWN_MAX_MS  3500   
#define EST_CM_PER_S_AT_BASE    35.0f  
#define PILLAR_COOLDOWN_CM      45.0f  

// ==============================
// Camera gating & Creeping
// ==============================
#define PILLAR_VIEW_LATCH_MS    300    
#define CREEP_SPEED             110    
#define WALL_HOLD_DIST          18.0f  // Deadlock trigger distance

float Kp = 1.4, Ki = 0.05, Kd = 1.5;
float headingError, lastError = 0, integral = 0;
float targetHeading = 0;

enum State { STRAIGHT, TURNING };
State state = STRAIGHT;
float turnTarget = 0;

// ==============================
// Monitoring Variables
// ==============================
unsigned long lastSerialTime = 0;
int currentSteerDeg = SERVO_CENTER_DEG;
int currentMotorSpeed = 0;
float frontDist = 999, leftDist = 999, rightDist = 999;
bool isBlockInSight = false;

// ==============================
// State Variables
// ==============================
unsigned long lastPillarSeenMs      = 0;     
bool          pillarCooldownArmed   = false;
unsigned long pillarCooldownStartMs = 0;
float         cooldownTravelCm      = 0;
unsigned long lastTravelUpdateMs    = 0;

// ==============================
// Prototypes
// ==============================
void  setSteeringDeg(int angleDeg);
void  motorForward(int speed);
void  motorBackward(int speed);
void  motorStop();
float readDistanceCM(int trigPin, int echoPin);
float getHeading();
float headingDiff(float target, float current);
void  calibrateIMU();
void  driveStraightPID();
void  decideTurn();
void  executeTurn();
void  preTurnWideArc(bool isTurningRight);
void  printTelemetry();
void  executeGreenManeuver();
void  executeRedManeuver();
bool  handleVision();
bool  scanForPillar(int &id, int &x, int &area);
bool  pillarInView();
bool  pillarVisibleNow();
bool  pillarCooldownActive();
void  armPillarCooldown();
void  updateTravelEstimate();
bool  turnAllowed();
void  executeDeadlockRecovery();

// ==============================
// Setup
// ==============================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  delay(1000);

  pinMode(TRIG_FRONT, OUTPUT); pinMode(ECHO_FRONT, INPUT);
  pinMode(TRIG_LEFT, OUTPUT);  pinMode(ECHO_LEFT, INPUT);
  pinMode(TRIG_RIGHT, OUTPUT); pinMode(ECHO_RIGHT, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  steeringServo.attach(SERVO_PIN);
  setSteeringDeg(SERVO_CENTER_DEG);
  delay(500);

  bool bnoOK = false;
  for (int i = 0; i < 5 && !bnoOK; i++) {
    bnoOK = bno.begin();
    if (!bnoOK) { Serial.println("BNO055 retry..."); delay(500); }
  }
  if (!bnoOK) {
    Serial.println("BNO055 not detected after retries — check wiring/power.");
    while (1) delay(10);
  }
  bno.setExtCrystalUse(true);
  delay(800);

  delay(1500); // HuskyLens boot stabilization
  while (!huskylens.begin(Wire)) {
    Serial.println("HuskyLens failed! Check wiring.");
    delay(500);
  }
  huskylens.writeAlgorithm(ALGORITHM_COLOR_RECOGNITION);

  calibrateIMU();

  targetHeading = getHeading();
  lastTravelUpdateMs = millis();

  motorForward(0);
  Serial.printf("--- ROBOT READY FOR TRACK --- initial targetHeading %.1f\n", targetHeading);
  delay(2000);
}

// ==============================
// Main Loop
// ==============================
void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("green")) executeGreenManeuver();
    if (input.equalsIgnoreCase("red")) executeRedManeuver();
  }

  updateTravelEstimate();   

  if (TURN == STOP_BOT_AFTER_TURNS && millis() - turnStartTime >= STOP_AFTER_TIME) {
    motorStop();
    setSteeringDeg(SERVO_CENTER_DEG);
    Serial.println("\n--- BOT STOPPED ---");
    while (1) delay(10);
  }

  // ==========================================================
  // STRICT STATE MACHINE: Camera is BLIND during turns
  // ==========================================================
  if (state == STRAIGHT) {
    
    // 1. Process Vision ONLY when driving straight
    bool cameraInControl = handleVision();
    if (cameraInControl) {
      printTelemetry();
      return; // Skip wall detection while dodging
    }

    // 2. Normal Wall Tracking
    frontDist = readDistanceCM(TRIG_FRONT, ECHO_FRONT);
    driveStraightPID();   

    bool wallAhead = (frontDist > 5 && frontDist < WALL_STOP_DIST);
    if (!wallAhead) {
        printTelemetry();
        return;
    }

    // 3. Wall Detected - Check if we are allowed to turn
    if (!turnAllowed()) {
      if (frontDist <= WALL_HOLD_DIST) {
        // We are trapped against a wall by a cooldown. Recover physically!
        executeDeadlockRecovery();
      } else {
        motorForward(CREEP_SPEED); // Creep slowly until cooldown expires
      }
      printTelemetry();
      return;
    }

    // 4. Confirm Wall & Initiate Turn
    delay(30);
    float confirmDist = readDistanceCM(TRIG_FRONT, ECHO_FRONT);
    if (confirmDist > 5 && confirmDist < WALL_STOP_DIST) {
      motorStop();
      delay(200);

      // Final gate check before going blind
      if (pillarVisibleNow() || !turnAllowed()) {
        Serial.println("[GATE] turn ABORTED BEFORE START (Pillar/Cooldown Detected)");
        printTelemetry();
        return;
      }

      decideTurn();
    }
  } 
  else if (state == TURNING) {
    // Completely bypass camera and distance checks, trust the IMU exclusively
    executeTurn();
  }

  printTelemetry();
}

// =====================================================================
// Deadlock Recovery Failsafe
// =====================================================================
void executeDeadlockRecovery() {
  Serial.println("\n*** DEADLOCK DETECTED: Wall reached but turn suppressed. ***");
  Serial.println("*** REVERSING UNTIL 66CM CLEARANCE REACHED ***");
  
  setSteeringDeg(SERVO_CENTER_DEG);
  motorBackward(BASE_SPEED);

  unsigned long reverseStart = millis();
  float currentDist = readDistanceCM(TRIG_FRONT, ECHO_FRONT);

  // Reverses safely away from the wall
  while (currentDist < 66.0f && (millis() - reverseStart < 3000)) {
    currentDist = readDistanceCM(TRIG_FRONT, ECHO_FRONT);
    delay(20); 
  }

  motorStop();
  delay(200); 

  // Force the logic gates open since we have physical space now
  pillarCooldownArmed = false; 
  Serial.println("Deadlock cleared physically. Initiating standard cornering.");
  decideTurn(); 
}

// =====================================================================
// Pillar cooldown / lockout 
// =====================================================================
void armPillarCooldown() {
  pillarCooldownArmed   = true;
  pillarCooldownStartMs = millis();
  cooldownTravelCm      = 0;          
  lastTravelUpdateMs    = millis();   
}

void updateTravelEstimate() {
  unsigned long now = millis();
  unsigned long dtMs = now - lastTravelUpdateMs;
  lastTravelUpdateMs = now;

  if (dtMs == 0 || dtMs > 1000) return;   
  if (currentMotorSpeed <= 0) return;

  float dt = dtMs / 1000.0f;
  cooldownTravelCm += ((float)currentMotorSpeed / (float)BASE_SPEED)
                      * EST_CM_PER_S_AT_BASE * dt;
}

bool pillarCooldownActive() {
  if (!pillarCooldownArmed) return false;

  unsigned long elapsed = millis() - pillarCooldownStartMs;

  if (elapsed >= PILLAR_COOLDOWN_MAX_MS) {
    pillarCooldownArmed = false;
    return false;
  }

  if (elapsed < PILLAR_COOLDOWN_MS) return true;
  if (cooldownTravelCm < PILLAR_COOLDOWN_CM) return true;

  pillarCooldownArmed = false;
  return false;
}

// =====================================================================
// Pre-emptive turn gate
// =====================================================================
bool pillarInView() {
  if (lastPillarSeenMs == 0) return false;
  return (millis() - lastPillarSeenMs) < PILLAR_VIEW_LATCH_MS;
}

bool turnAllowed() {
  return (!pillarInView()) && (!pillarCooldownActive());
}

bool pillarVisibleNow() {
  int id = 0, x = 160, area = 0;
  if (!scanForPillar(id, x, area)) return false;
  if (area <= HUSKY_SOFT_TRIGGER) return false;
  if (id != ID_RED && id != ID_GREEN) return false;   

  lastPillarSeenMs = millis();
  armPillarCooldown();
  return true;
}

// =====================================================================
// Vision
// =====================================================================
bool scanForPillar(int &id, int &x, int &area) {
  huskylens.request();
  if (!huskylens.available()) return false;

  int largestArea = 0;
  int detectedID = 0;
  int detectedX = 160; 

  while (huskylens.available()) {
    HUSKYLENSResult result = huskylens.read();
    int a = result.width * result.height;
    if (a > largestArea) {
      largestArea = a;
      detectedID = result.ID;
      detectedX = result.xCenter;
    }
  }

  id = detectedID; x = detectedX; area = largestArea;
  return true;
}

bool handleVision() {
  int detectedID = 0, detectedX = 160, largestArea = 0;

  if (!scanForPillar(detectedID, detectedX, largestArea)) {
    isBlockInSight = false;
    return false;
  }

  isBlockInSight = (largestArea > HUSKY_SOFT_TRIGGER);
  if (!isBlockInSight) return false;

  if (detectedID == ID_MAGENTA) return false; 
  if (detectedID != ID_RED && detectedID != ID_GREEN) return false; 

  lastPillarSeenMs = millis();
  armPillarCooldown();

  if (largestArea > HUSKY_AREA_TRIGGER) {
    if (detectedID == ID_GREEN) executeGreenManeuver();
    else executeRedManeuver();
    return true;
  }

  int intensity = map(largestArea, HUSKY_SOFT_TRIGGER, HUSKY_AREA_TRIGGER, 8, 35);
  intensity = constrain(intensity, 8, 35);
  int steerAdj = 0;

  if (detectedID == ID_RED) {
    if (detectedX < 106) steerAdj = intensity;
    else if (detectedX < 213) steerAdj = (int)(intensity * 0.7);
    else steerAdj = (int)(intensity * 0.4);
  } else {
    if (detectedX > 213) steerAdj = -intensity;
    else if (detectedX > 106) steerAdj = (int)(-intensity * 0.7);
    else steerAdj = (int)(-intensity * 0.4);
  }

  setSteeringDeg(SERVO_CENTER_DEG + steerAdj);
  motorForward(BASE_SPEED);
  return true;
}

// ---------------- Maneuver Sequences ----------------
void executeGreenManeuver() {
  Serial.println("\n--- GREEN MANEUVER: SHIFT LEFT ---");
  motorStop(); delay(200);

  setSteeringDeg(SERVO_CENTER_DEG);
  motorBackward(BASE_SPEED);
  delay(400); // Back up slightly to clear the pillar
  motorStop(); delay(200);

  setSteeringDeg(SERVO_SHIFT_LEFT);
  motorForward(SHIFT_SPEED);
  delay(650); // Increased shift duration to widen the berth

  setSteeringDeg(SERVO_CENTER_DEG);
  motorForward(SHIFT_SPEED);
  delay(400);

  setSteeringDeg(SERVO_SHIFT_RIGHT);
  motorForward(SHIFT_SPEED);
  delay(650); // Matches the left shift

  setSteeringDeg(SERVO_CENTER_DEG);
  motorStop(); delay(150);

  integral = 0; lastError = 0;
  state = STRAIGHT;
  armPillarCooldown();
  lastPillarSeenMs = millis();
}

void executeRedManeuver() {
  Serial.println("\n--- RED MANEUVER: SHIFT RIGHT ---");
  motorStop(); delay(200);

  setSteeringDeg(SERVO_CENTER_DEG);
  motorBackward(BASE_SPEED);
  delay(400); // Back up slightly to clear the pillar
  motorStop(); delay(200);

  setSteeringDeg(SERVO_SHIFT_RIGHT);
  motorForward(SHIFT_SPEED);
  delay(650); // Increased shift duration to widen the berth

  setSteeringDeg(SERVO_CENTER_DEG);
  motorForward(SHIFT_SPEED);
  delay(400);

  setSteeringDeg(SERVO_SHIFT_LEFT);
  motorForward(SHIFT_SPEED);
  delay(650); // Matches the right shift

  setSteeringDeg(SERVO_CENTER_DEG);
  motorStop(); delay(150);

  integral = 0; lastError = 0;
  state = STRAIGHT;
  armPillarCooldown();
  lastPillarSeenMs = millis();
}

// ---------------- Serial Monitoring ----------------
void printTelemetry() {
  if (millis() - lastSerialTime < 150) return;
  lastSerialTime = millis();

  float currHeading = getHeading();
  Serial.print(state == STRAIGHT ? "[STR] " : "[TRN] ");
  Serial.printf("F:%4.1fcm L:%4.1fcm R:%4.1fcm | ", frontDist, leftDist, rightDist);
  Serial.printf("H_Cur:%5.1f H_Tgt:%5.1f Err:%5.1f | ", currHeading, targetHeading, headingError);
  Serial.printf("Steer:%3d MtrSpd:%3d BlkInSight:%d | CD:%d(%.0fcm)\n", 
                currentSteerDeg, currentMotorSpeed, isBlockInSight, pillarCooldownArmed ? 1 : 0, cooldownTravelCm);
}

// ---------------- Ultrasonic ----------------
float readDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 15000);
  if (duration == 0) return 999;
  return duration * 0.0343f / 2.0f;
}

// ---------------- IMU Calibration & Data ----------------
void calibrateIMU() {
  uint8_t system, gyro, accel, mag;
  system = gyro = accel = mag = 0;

  Serial.println("\n--- Calibrating IMU ---");
  while (gyro < 3) {
    bno.getCalibration(&system, &gyro, &accel, &mag);
    delay(200);
  }
  Serial.println("Gyroscope Calibrated Successfully!\n");
}

float getHeading() {
  imu::Vector<3> e = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  return e.x();
}

float headingDiff(float target, float current) {
  float d = target - current;
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}

// ---------------- Steering ----------------
void setSteeringDeg(int angleDeg) {
  currentSteerDeg = constrain(angleDeg, SERVO_MIN_DEG, SERVO_MAX_DEG);
  steeringServo.write(currentSteerDeg);
}

// ---------------- Motor ----------------
void motorForward(int speed) {
  currentMotorSpeed = speed;
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  analogWrite(ENA, speed);
}

void motorBackward(int speed) {
  currentMotorSpeed = -speed;
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  analogWrite(ENA, speed);
}

void motorStop() {
  currentMotorSpeed = 0;
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}

// ---------------- Straight-line heading-hold PID ----------------
void driveStraightPID() {
  float current = getHeading();
  headingError = headingDiff(targetHeading, current);

  integral += headingError;
  integral = constrain(integral, -30, 30);
  float derivative = headingError - lastError;
  lastError = headingError;

  float correction = Kp * headingError + Ki * integral + Kd * derivative;
  int steerDeg = SERVO_CENTER_DEG + constrain((int)correction, -30, 30);

  setSteeringDeg(steerDeg);
  motorForward(BASE_SPEED);
}

// =====================================================================
// PRE-TURN WIDE ARC MANEUVER (SCANDINAVIAN FLICK)
// =====================================================================
void preTurnWideArc(bool isTurningRight) {
  unsigned long arcStart = millis();
  unsigned long maxArcTime = 650; 
  float outerClearanceThreshold = 18.0f; 

  if (isTurningRight) {
    Serial.println("[ARC] Swinging LEFT to widen RIGHT turn...");
    setSteeringDeg(SERVO_MIN_DEG); 
    motorForward(TURN_SPEED);
    
    while (millis() - arcStart < maxArcTime) {
      float distL = readDistanceCM(TRIG_LEFT, ECHO_LEFT);
      if (distL > 0 && distL < outerClearanceThreshold) {
        Serial.printf("[ARC] Left wall close (%.1fcm) - cutting arc short.\n", distL);
        break;
      }
      delay(20);
    }
  } else {
    Serial.println("[ARC] Swinging RIGHT to widen LEFT turn...");
    setSteeringDeg(SERVO_MAX_DEG); 
    motorForward(TURN_SPEED);
    
    while (millis() - arcStart < maxArcTime) {
      float distR = readDistanceCM(TRIG_RIGHT, ECHO_RIGHT);
      if (distR > 0 && distR < outerClearanceThreshold) {
        Serial.printf("[ARC] Right wall close (%.1fcm) - cutting arc short.\n", distR);
        break;
      }
      delay(20);
    }
  }
  
  motorStop();
  delay(150); 
}

// ---------------- Turn decision & execution ----------------
void decideTurn() {
  leftDist  = readDistanceCM(TRIG_LEFT, ECHO_LEFT);
  delay(30);
  rightDist = readDistanceCM(TRIG_RIGHT, ECHO_RIGHT);
  delay(30);

  if (leftDist < 0) leftDist = 999;
  if (rightDist < 0) rightDist = 999;

  TURN++;
  turnStartTime = millis();

  bool isRightTurn = false;

  if (leftDist > rightDist) {
    turnTarget = targetHeading - 90; 
    isRightTurn = false;
  } else {
    turnTarget = targetHeading + 90; 
    isRightTurn = true;
  }

  while (turnTarget < 0)    turnTarget += 360;
  while (turnTarget >= 360) turnTarget -= 360;

  Serial.printf("[TURN %d] L:%.1f R:%.1f | Orthogonal Target: %.1f\n",
                TURN, leftDist, rightDist, turnTarget);

  preTurnWideArc(isRightTurn);

  state = TURNING;
}

void executeTurn() {
  float current = getHeading();
  float diff = headingDiff(turnTarget, current);
  headingError = diff;

  if (fabs(diff) <= TURN_DONE_TOL) {
    motorStop();
    setSteeringDeg(SERVO_CENTER_DEG);
    delay(300); 

    Serial.println("Turn complete. Active-reversing to stabilize, center, and gain vision clearance...");

    unsigned long revStart = millis();
    // INCREASED to 900ms to back up significantly further for vision clarity
    while(millis() - revStart < 900) { 
        float revHeading = getHeading();
        float revErr = headingDiff(turnTarget, revHeading);
        
        float correction = Kp * revErr; 
        int steerDeg = SERVO_CENTER_DEG - constrain((int)correction, -30, 30); 
        
        setSteeringDeg(steerDeg);
        motorBackward(TURN_SPEED);
        delay(20);
    }
    
    motorStop();
    setSteeringDeg(SERVO_CENTER_DEG);
    delay(200);

    targetHeading = turnTarget;
    integral = 0; lastError = 0;
    
    // Wipe latches so vision wakes up fresh
    lastPillarSeenMs = 0; 
    lastTravelUpdateMs = millis();
    
    state = STRAIGHT;
    Serial.printf("[TURN %d] complete, perfectly locked to orthogonal heading: %.1f\n", TURN, targetHeading);
    return;
  }

  int steerDeg = (diff > 0) ? SERVO_MAX_DEG : SERVO_MIN_DEG;
  setSteeringDeg(steerDeg);
  motorForward(TURN_SPEED);
}