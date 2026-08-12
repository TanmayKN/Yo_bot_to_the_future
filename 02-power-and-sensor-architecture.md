# Power & Sensor Architecture

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 4. Power & Sensor Architecture

### 4.1 Power tree

```
                   3S Li-ion PACK
              3 x 18650 cells in series
        12.6 V full | 11.1 V nom | 9.0 V CUTOFF
                         |
                    [ FUSE __ A ]
                         |
                  [ MASTER SWITCH ]
                         |
         +---------------+----------------+
         |                                |
         v                                v
  +--------------+                 +---------------+
  |    L298N     |                 |     BUCK      |
  |  H-Bridge    |                 |  CONVERTER    |
  |  (12.6 V in) |                 |  set to 6.0 V |
  +--------------+                 +---------------+
     |         |                          |
     |         |                          v
     |         |                   +---------------+
     |         |                   |   PCA9685     |
     v         v                   |   V+  rail    |
  +-------+  +------------+        +---------------+
  | DRIVE |  |  78M05     |               |
  | MOTOR |  |  onboard   |               v
  +-------+  |  5 V reg   |        +---------------+
             +------------+        | STEERING SERVO|
                    |              |   channel 0   |
                    v              +---------------+
            +----------------+
            |   PERFBOARD    |
            |  5 V rail bus  |
            +----------------+
              |      |      |
              v      v      v
           +-----++-----++-----+
           | US  || US  || US  |
           |LEFT ||RIGHT||FRONT|
           +-----++-----++-----+

  +-----------------------------------------------+
  |  ESP32   (own 3.3 V regulator)                |
  |    3.3 V  -->  PCA9685 VCC (logic only)       |
  |    3.3 V  -->  MPU-6050 VCC                   |
  |    GPIO21 -->  I2C SDA  ---+--> PCA9685       |
  |    GPIO22 -->  I2C SCL  ---+--> MPU-6050      |
  +-----------------------------------------------+

  *** ALL GROUNDS COMMON: pack -, L298N GND, buck GND,
      perfboard GND, PCA9685 GND, ESP32 GND ***
```

**Read the two rails separately.** The PCA9685 has *two* power inputs and they are not interchangeable:

| PCA9685 pin | Fed from | Carries |
|---|---|---|
| `VCC` | ESP32 3.3 V | Chip logic + I²C only — a few mA |
| `V+` | Buck converter, 6.0 V | Servo current — up to ~1.2 A on stall |

Wiring the servo rail into `VCC` is the classic mistake here: it back-feeds 6 V into the ESP32's 3.3 V rail and destroys the board.

![System wiring and power distribution diagram](../schemes/wiring_system.png)

*Source: `schemes/wiring_system.svg`. Every ground symbol ties to the single common ground bar — see §4.7.*

### 4.2 Why 3S Li-ion

| Option | Verdict |
|---|---|
| 3 × 18650 in series (chosen) | 11.1 V nominal gives adequate headroom above the L298N's ~2 V internal drop, so the motor still sees usable voltage; high discharge capability handles motor inrush without voltage sag that would brown out the MCU |
| 2S (7.4 V) | After L298N drop the motor sees only ~5.4 V — insufficient torque at our gearing |
| 6 × AA NiMH | Higher internal resistance → voltage collapses under motor stall current, resetting the ESP32 |
| Single-cell + boost | Boost converter adds a high-current failure point and switching noise onto the sensor rail |

**Safety requirements for the pack** (document these — judges notice):
- Cells must be matched capacity and charge state; series packs with mismatched cells over-discharge the weakest cell.
- Hard cutoff at **9.0 V total (3.0 V/cell)**. Firmware reads pack voltage via a divider on `[FILL: ADC pin]` and enters `FAULT` state below threshold.
- Fuse rated `[FILL] A` immediately after the pack positive terminal.
- Charge only with a proper 3S balance charger — never a single-cell charger across the pack.

### 4.3 Voltage rails and current budget

| Rail | Source | Loads | Typical | Peak |
|---|---|---|---|---|
| 12.6 V raw | Pack | L298N, buck input | — | — |
| Motor rail | L298N output | Drive motor | `[FILL] mA` | stall `[FILL] mA` |
| Servo rail (set to **6.0 V** — see note) | Buck converter | Steering servo | 250 mA moving | ~1200 mA stall |
| 5 V logic | L298N 78M05 | 3 × HC-SR04 | 3 × 15 mA = 45 mA | 3 × 20 mA = 60 mA |
| 3.3 V | ESP32 onboard reg | MPU-6050, PCA9685 logic | ~7 mA | ~10 mA |
| ESP32 itself | USB or pack | — | ~80 mA (Wi-Fi off) | ~250 mA |

**Estimated average draw:** `[FILL] mA` → with a `[FILL] mAh` pack, predicted runtime `[FILL] minutes`. Measured runtime: `[FILL]`.

> ⚠️ **Servo rail voltage.** The buck was initially set to 7.8 V. Standard servos are rated 4.8–7.2 V and even high-voltage servos top out at 7.4 V, so 7.8 V is above every common rating and shortens servo life. **Rail reset to 6.0 V.** Record your actual servo's datasheet range here: `[FILL]`.

> ⚠️ **L298N onboard regulator.** The 78M05 on the L298N board is only rated for input up to about 12 V, and our full pack is 12.6 V. At our 60 mA sensor load it dissipates (12.6 − 5) × 0.06 ≈ **0.46 W**, which the small package handles, but it is running at the edge of its input spec. Mitigation options recorded in §6.4.

### 4.4 Sensor selection, placement and justification

#### Ultrasonic ×3 (HC-SR04)

**Placement is driven by field geometry, not convenience:**

| Sensor | Aim | Purpose |
|---|---|---|
| LEFT | 90° to vehicle axis | Lateral distance to left wall → PID centring |
| RIGHT | 90° to vehicle axis | Lateral distance to right wall → PID centring |
| FRONT | 0°, along axis | Corner detection: a sudden drop in front distance means an approaching wall |

Two side sensors rather than one because **the sum `left + right` is a check on validity**. On a straight section that sum should equal the lane width (minus vehicle width) and stay constant. If it deviates, one reading is bad — a specular reflection or an echo from the wrong surface — and we reject that frame rather than steer on it. A single side sensor gives you no way to detect that it's lying to you.

**Mounting height:** all three sensors are mounted at `[FILL] mm` above the mat, chosen to sit clearly above the mat surface (avoiding ground echoes) while remaining below the top edge of the field walls so the beam always strikes wall material.

**Known limitation — specular reflection.** Ultrasonic beams reflect like light off a mirror. When the vehicle is yawed more than roughly 15–20° relative to a wall, the echo bounces away instead of returning, and the sensor reports a spuriously large distance or times out. **This is our single largest sensing weakness.** Mitigations in §6.5.

**Timing.** Each HC-SR04 needs its trigger-to-echo cycle to finish before the next fires, or one sensor hears another's ping (cross-talk). Sensors are polled **round-robin** with a fixed inter-sensor gap of `[FILL] ms`, giving a full three-sensor refresh every `[FILL] ms` (`[FILL] Hz`). That refresh rate is what caps our safe top speed (§3.3).

#### IMU (MPU-6050)

Used for **relative heading**, specifically to detect and confirm 90° corner turns and to count total heading change across the run (12 × 90° = 3 laps).

**Honest limitation:** the MPU-6050 has no magnetometer, so yaw comes from integrating the Z gyro. Integration drifts — typically several degrees per minute. Therefore we **never treat yaw as absolute**. Instead:
- Yaw is **zeroed at the start of each straight section**, once the ultrasonics confirm the vehicle is wall-parallel.
- The turn state only uses **relative** yaw change since that zero, so drift has no time to accumulate.
- Accelerometer + complementary filter stabilise pitch and roll but **cannot** correct yaw (gravity gives no yaw reference).

**Calibration procedure** (must be run and documented):
1. Place the vehicle stationary and level on the mat for 5 seconds at power-up.
2. Average 1000 gyro samples → store as bias offsets, subtract from every subsequent reading.
3. Verify: leave stationary 60 s, record integrated yaw drift. Ours: `[FILL]°/min`.

#### Why the PCA9685 for one servo

The vehicle only has one servo, so a dedicated PWM driver looks like overkill. The justification is **not** raw resolution — the ESP32's LEDC peripheral is 16-bit, actually finer than the PCA9685's 12-bit. The real reasons:

1. **Timing independence.** The PCA9685 generates pulses from its own 25 MHz oscillator. Once a value is written over I²C, the pulse train is rock-solid regardless of what the ESP32 is doing. When the main loop is busy with ultrasonic timing or sensor fusion, MCU-generated PWM can jitter — and servo jitter reads as steering twitch.
2. **Electrical isolation of the servo rail.** The servo's current spikes (up to ~1.2 A on stall) stay on the buck-fed V+ rail and never travel through the MCU board. This removes a real brown-out path.
3. **Bus economy and expansion.** Steering, IMU and any future I²C device share two wires. Adding a camera gimbal or second servo later costs zero additional GPIO.

**Trade-off accepted:** any I²C bus fault now takes out steering *and* IMU together. This is logged as a risk with mitigation in §6.5.

**I²C bus map:**

| Device | Address | Notes |
|---|---|---|
| PCA9685 | `0x40` | default, A0–A5 unjumpered |
| MPU-6050 | `0x68` | AD0 tied low |

No address conflict. Bus runs at 400 kHz with `[FILL] kΩ` pull-ups.

### 4.5 ESP32 pin assignments

Board assumed: **ESP32 DevKit V1 (30-pin, WROOM-32)**. If yours differs, check the silkscreen before wiring — pin *positions* vary between boards even when GPIO *numbers* don't.

![ESP32 DevKit V1 pin assignment diagram](../schemes/pinout_esp32.png)

*Source: `schemes/pinout_esp32.svg` — edit and re-export if your pin allocation changes.*

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| I²C SDA | **21** | bidirectional | Shared: PCA9685 + MPU-6050 |
| I²C SCL | **22** | output | Shared: PCA9685 + MPU-6050 |
| Ultrasonic LEFT — TRIG | **5** | output | 10 µs pulse |
| Ultrasonic LEFT — ECHO | **18** | input | **Via divider — see §4.6** |
| Ultrasonic RIGHT — TRIG | **17** | output | |
| Ultrasonic RIGHT — ECHO | **16** | input | **Via divider — see §4.6** |
| Ultrasonic FRONT — TRIG | **4** | output | |
| Ultrasonic FRONT — ECHO | **19** | input | **Via divider — see §4.6** |
| L298N ENA (speed PWM) | **25** | output | LEDC channel, `[FILL] Hz` |
| L298N IN1 (direction) | **26** | output | |
| L298N IN2 (direction) | **27** | output | |
| Pack voltage sense | **34** | input only | ADC1_CH6, via divider — see §4.6 |
| Start button | **13** | input | `INPUT_PULLUP`, button to GND |
| Status LED | **2** | output | Onboard LED on most DevKits |

**Pins deliberately avoided, and why:**

| Pins | Reason |
|---|---|
| GPIO 6–11 | Hard-wired to the SPI flash chip. Using them bricks boot. Not optional. |
| GPIO 0, 12, 15 | Strapping pins — their level at reset selects boot mode. A sensor pulling one of these the wrong way at power-up means the board won't start, and it looks like a dead board. |
| GPIO 34–39 | **Input only.** No output drive, and no internal pull-up/pull-down. Fine for the ADC battery sense, useless for TRIG. |
| ADC2 pins for analogue reads | ADC2 is unavailable whenever Wi-Fi is active. Battery sense therefore uses GPIO 34 (ADC1). Using ADC2 pins as plain digital I/O is fine — which is what 25/26/27/4 are doing. |

**Spare capacity.** Nine GPIO are committed to sensing and motor control, two to I²C. That leaves **GPIO 32, 33, 14 and 23** free, plus GPIO 35/36/39 for additional analogue inputs. This matters for the obstacle round: if a camera is added later, the I²C bus already has room (§4.4) and these four pins cover any additional control lines. Verify against the camera module's own clock/data requirements before committing them.

`[INSERT: v-photos/wiring_detail.jpg — photo of your actual wired board, for comparison against the diagram above]`

### 4.6 Level shifting and analogue conditioning

#### HC-SR04 ECHO → ESP32 (mandatory)

The HC-SR04 runs on 5 V and its ECHO pin outputs **5 V logic**. The ESP32's GPIO are **3.3 V and not 5 V tolerant**. Connecting ECHO directly stresses the input protection diodes — sometimes it survives for months, then fails mid-competition. Every ECHO line gets a divider:

```
   HC-SR04 ECHO (5 V)
          |
          |
         [R1] 1 kΩ
          |
          +--------------> ESP32 GPIO (3.33 V)
          |
         [R2] 2 kΩ
          |
         GND  (common)

   V_out = 5 V x R2 / (R1 + R2)
         = 5 x 2000 / 3000
         = 3.33 V     ✓ safe
```

Build three of these on the perfboard, one per sensor.

**TRIG direction is fine unshifted.** The ESP32 drives 3.3 V into the HC-SR04's TRIG input, which registers as logic HIGH on most modules. If a sensor is unreliable, this is the first thing to suspect — some clone boards want a firmer 5 V trigger.

#### Pack voltage sense → GPIO 34

The ADC reads 0–3.3 V; the pack is up to 12.6 V. Divide it down:

```
   PACK + (12.6 V max)
          |
         [R1] 100 kΩ
          |
          +--------------> GPIO 34 (ADC1_CH6)
          |
         [R2] 22 kΩ
          |
         GND

   V_adc at 12.6 V = 12.6 x 22 / 122 = 2.27 V   ✓ within range
   Scale factor in firmware: V_pack = V_adc x (122 / 22) = V_adc x 5.545
```

High resistor values are used deliberately so this divider drains only ~0.1 mA from the pack when idle. **Calibrate it:** measure the real pack voltage with a multimeter, compare against the firmware reading, and store the correction factor in `config.h`. The ESP32's ADC is not linear near its rails, so a single measured correction is worth more than the theoretical number.

### 4.7 Module-by-module wiring

#### PCA9685

```
  PCA9685              connects to
  ---------------------------------------------------
  VCC   -------------> ESP32 3.3 V        (logic only)
  GND   -------------> COMMON GROUND
  SDA   -------------> ESP32 GPIO 21
  SCL   -------------> ESP32 GPIO 22
  OE    -------------> GND  (output enable, active low)
  V+    -------------> BUCK 6.0 V         (servo power)
  GND   -------------> BUCK GND

  Servo header, channel 0:
     signal (orange/white) -> CH0 PWM
     power  (red)          -> CH0 V+
     ground (brown/black)  -> CH0 GND

  I2C address: 0x40  (all address jumpers A0-A5 open)
  PWM frequency: 50 Hz for standard servos
```

#### MPU-6050 (GY-521 module)

```
  MPU-6050             connects to
  ---------------------------------------------------
  VCC   -------------> ESP32 3.3 V
  GND   -------------> COMMON GROUND
  SDA   -------------> ESP32 GPIO 21   (same bus as PCA9685)
  SCL   -------------> ESP32 GPIO 22
  AD0   -------------> GND             (selects address 0x68)
  INT   -------------> not connected   (polled, not interrupt-driven)

  I2C address: 0x68
```

Most GY-521 breakouts carry an onboard 3.3 V regulator and accept 3–5 V on VCC, but the I²C lines are pulled to whatever VCC is. Feeding it 3.3 V keeps the bus at 3.3 V logic and matches the PCA9685's levels. **Don't power it from 5 V.**

#### L298N

```
  L298N                connects to
  ---------------------------------------------------
  +12V  -------------> PACK + (after fuse and switch)
  GND   -------------> COMMON GROUND
  +5V   -------------> PERFBOARD 5 V rail (output from 78M05)
  5V-EN jumper -------> FITTED (enables onboard regulator)

  ENA   -------------> ESP32 GPIO 25   (PWM, remove the ENA jumper!)
  IN1   -------------> ESP32 GPIO 26
  IN2   -------------> ESP32 GPIO 27

  OUT1  -------------> drive motor terminal A
  OUT2  -------------> drive motor terminal B
```

> ⚠️ **Remove the ENA jumper.** Boards ship with a jumper tying ENA to 5 V, which forces the motor to full speed and ignores your PWM entirely. If your motor only knows "off" and "flat out", this is why.

**Direction truth table:**

| IN1 | IN2 | Result |
|---|---|---|
| LOW | LOW | Coast (free-wheel) |
| HIGH | LOW | Forward |
| LOW | HIGH | Reverse |
| HIGH | HIGH | Brake |

`RECOVER` uses reverse; `FAULT` and `FINISH` use brake, not coast, so the vehicle stops where it's told.

#### Perfboard 5 V distribution

```
   from L298N +5V
        |
   +----+----------------+----------------+
   |                     |                |
  VCC                   VCC              VCC
 [US LEFT]           [US RIGHT]       [US FRONT]
  GND                   GND              GND
   |                     |                |
   +----+----------------+----------------+
        |
   to COMMON GROUND

   Plus 3 x ECHO dividers (§4.6), one per sensor.
   Plus 1 x battery sense divider to GPIO 34.
```

Add a **100 µF electrolytic** across the 5 V rail at the perfboard and a **0.1 µF ceramic** next to each sensor's VCC/GND pins. Ultrasonic modules draw a current spike each time they fire; without local decoupling that spike appears as noise on the shared rail and shows up as jitter in the *other* sensors' readings.

### 4.8 Known failure points

| Failure point | Consequence | Mitigation |
|---|---|---|
| Pack over-discharge | Cell damage, sudden power loss mid-run | Firmware voltage cutoff at 9.0 V |
| L298N 78M05 at 12.6 V input | Regulator overheat → sensor rail collapse | Load kept ≤60 mA; alternative dedicated buck specified in §6.4 |
| I²C bus fault | Loses steering **and** IMU simultaneously | Short leads, proper pull-ups, servo centred + motor stopped on I²C timeout |
| Ultrasonic specular reflection | Phantom "wall disappeared" reading | Reject frames where `left+right` deviates from expected lane width; hold last-good value for ≤3 frames |
| Perfboard solder joint fatigue | Intermittent sensor dropout | Every connection strain-relieved; continuity re-checked before each run |
| Servo over-voltage | Progressive servo failure across the season | Buck rail reduced to 6.0 V |

---

---

← [Mobility & Mechanical Design](01-mobility-and-mechanical-design.md) · [Main README](../README.md) · [Software Architecture & Obstacle Strategy](03-software-architecture.md) →
