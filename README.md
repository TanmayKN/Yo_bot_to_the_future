# WRO Future Engineers — Open Round

Main vehicle code: [`wro_simple.ino`](wro_simple.ino)

This is the program that runs the car for the **Open Round** — three autonomous laps
around the track with no obstacles to avoid, then a return to the starting section.
The car drives on an ESP32, steers with a single front servo (Ackermann-style), and
navigates using three ultrasonic sensors plus a BNO055 IMU for heading control.

---

## 1. Round summary

| | |
|---|---|
| Round | Open Round (no traffic signs / obstacles) |
| Laps | 3 |
| Start | Waits for a `START` command over serial from the Raspberry Pi |
| Finish | Returns to the starting section and stops |
| Turn direction | Decided on the fly — whichever side is open |

The car does **not** need to know the track direction in advance. It detects on
each corner whether the open side is left or right, and it learns how many turns
make up one lap during the first lap.

---

## 2. Hardware

### Wiring

| Signal | GPIO | Notes |
|---|---|---|
| Motor IN1 | 13 | direction |
| Motor IN2 | 12 | direction |
| Motor ENA | 25 | PWM speed |
| Steering servo | 4 | ESP32Servo |
| TRIG front | 5 | HC-SR04 |
| ECHO front | 18 | HC-SR04 |
| TRIG right | 17 | HC-SR04 |
| ECHO right | 16 | HC-SR04 |
| TRIG left | 26 | HC-SR04 |
| ECHO left | 27 | HC-SR04 |
| IMU SDA | 21 | BNO055, `Wire.begin(21, 22)` |
| IMU SCL | 22 | BNO055 |
| Start button | 15 | `BUTTON_PIN`, currently commented out |

> The HC-SR04 echo lines are 5 V. Step them down to 3.3 V with a divider (or use
> 3.3 V-native sensors) before they reach the ESP32.

### Servo geometry

```
SERVO_LEFT      25    full left lock
SERVO_STRAIGHT  94    centre — trim this if the car pulls to one side
SERVO_RIGHT    145    full right lock
```

`SERVO_STRAIGHT` is the single most important mechanical number in the file. If
the car drifts while the PI controller reports near-zero error, the servo centre
is wrong, not the code.

### Libraries

- `Wire`
- `Adafruit_Sensor`
- `Adafruit_BNO055`
- `ESP32Servo`
- `WiFi` / `WiFiClient` / `WiFiServer` (debug logging only)

---

## 3. How it works

### 3.1 Startup sequence

1. Serial opens at 115200 baud.
2. All motor / sensor pins are configured.
3. The ESP32 **blocks** waiting for the Raspberry Pi to send `START` over serial.
4. Servo attaches and centres.
5. BNO055 initialises in `OPERATION_MODE_NDOF` (fused 9-DOF absolute heading).
6. `yawOffset` and `targetYaw` are captured — the car's current heading becomes
   "zero", so the code always steers relative to where it was placed on the mat.
7. `recordStartingPosition()` stores the front distance at the start line. This
   value is what the car later matches against to know it is home.

The physical push-button path is written but commented out; serial `START` from
the Pi is the active trigger.

### 3.2 Driving straight — the PI heading controller

`maintainHeading()` runs every loop while the car is not mid-turn.

```
error      = targetYaw - currentYaw          (wrapped to ±180°)
correction = Kp * error + Ki * integral      (clamped to ±MAX_CORRECTION)
servo      = SERVO_STRAIGHT + correction     (clamped to the lock limits)
```

| Constant | Value | Meaning |
|---|---|---|
| `Kp` | 6.0 | how hard it reacts to the current error |
| `Ki` | 0.2 | removes long-term drift (e.g. a slightly bent axle) |
| `MAX_CORRECTION` | 22.0 | biggest steering nudge the controller may ask for |
| `INTEGRAL_LIMIT` | 150.0 | anti-windup cap |
| `DEADBAND` | 0.6 | error smaller than this is treated as straight |

Inside the deadband, the integral decays by 10 % per loop instead of being zeroed
— that keeps a genuine standing bias while preventing twitchy corrections.

**Tuning:** if the car weaves, drop `Kp`. If it settles off-heading and never
recovers, raise `Ki` slightly. Change one at a time.

### 3.3 Turning

A corner is detected in `loop()`:

```
front < 65 cm  AND  right > 90 cm  AND  left  < 90 cm   →  turn right
front < 65 cm  AND  left  > 90 cm  AND  right < 90 cm   →  turn left
```

Requiring the *opposite* side to be closed as well as the target side open is what
prevents a false turn on an open stretch of wall.

Each turn then:

1. `reverseBeforeTurn()` — backs up for `REVERSE_DURATION_MS` (700 ms) to buy
   room for the turning circle, then stops and settles for 200 ms.
2. Sets full lock, drives forward at `TURN_SPEED`, and **watches the IMU** until
   the heading is within 8° of `targetYaw ± 89.5`, with a 4-second safety timeout.
3. Stops, re-centres the servo, waits 400 ms to settle.
4. Updates `targetYaw` by exactly ±90° — **not** from the measured heading. This
   is the key detail: taking the new target from the sensor would let each turn's
   small error accumulate over 12 corners. Anchoring to the previous target keeps
   the car square to the track all race.
5. Resets `yawErrorIntegral` so the straight-line controller starts clean.
6. Increments `turn_count`.

### 3.4 Emergency reverse

If the front sensor reads under 25 cm at any point, `moveBackwardToSafeDistance()`
reverses until there is 40 cm of clearance (2-second timeout) and the loop restarts.
This runs *before* the turn logic, so a wall closing in always wins.

### 3.5 Lap counting

- The first lap is measured, not assumed: after 4 turns, `turns_per_lap` is set to
  whatever `turn_count` reached, and `first_lap` goes false.
- After that, a lap completes whenever `turn_count >= turns_per_lap * (lap_count + 1)`.
- At `lap_count >= 3`, `returning_to_start` flips true.

### 3.6 Returning to start

`returnToStartNavigation()` uses the same wall-following rules, but on every loop
it compares the current front distance to `starting_front_distance`. Within 5 cm,
the car stops and parks permanently in an infinite loop — mission complete.

---

## 4. Speeds and tuning constants

| Constant | Value | Notes |
|---|---|---|
| `DRIVE_SPEED` | 150 | straight-line PWM (0–255) |
| `TURN_SPEED` | 160 | slightly higher — turning loads the motor |
| `REVERSE_SPEED` | 120 | slower for control |
| `REVERSE_DURATION_MS` | 700 | **calibrate this on your car** — target ≈10 cm |
| front turn trigger | 65 cm | in `loop()` |
| side "open" threshold | 90 cm | in `loop()` |
| emergency reverse | 25 cm | in `loop()` |

`REVERSE_DURATION_MS` is the number most worth re-measuring on competition day —
it depends on battery charge, and a flat pack reverses less far in the same time.

---

## 5. Debugging

`logPrint()` writes to both Serial and a Telnet client, so you can watch the car
think while it drives untethered.

To enable it, uncomment two lines:

```cpp
setupWiFiTelnet();       // in setup()
handleTelnetClient();    // at the top of loop()
```

Then connect with `telnet <ip-shown-on-serial> 23`. The IP is printed over Serial
during connection.

> `ssid` and `password` near the top of the file are placeholders
> (`YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD`). Fill in your own network locally, and
> keep real credentials out of commits — this repo is public for the engineering
> documentation score.

Telnet is **off by default** — WiFi association costs several seconds of startup
and is not wanted during a scored run.

---

## 6. Other files in this repo

| File | Purpose |
|---|---|
| [`wro_simple.ino`](wro_simple.ino) | **Main open-round program** (this README) |
| [`v1.ino`](v1.ino) | Cut-down early version — no IMU, timed turns |
| [`ultrasonic_test.ino`](ultrasonic_test.ino) | Bench test: prints all three distances |
| [`blink.ino`](blink.ino) | Scratch file |

---

## 7. Pre-run checklist

1. Battery charged — every timing constant assumes full voltage.
2. Car placed square to the wall; the start heading becomes the reference.
3. BNO055 detected (`FATAL: No BNO055 detected!` halts the program if not).
4. Servo centred — car rolls straight when pushed with the servo powered.
5. All three ultrasonics returning sane values (run `ultrasonic_test.ino`).
6. Raspberry Pi ready to send `START`.
