# Software Architecture & Obstacle Strategy

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 5. Software Architecture & Obstacle Strategy

### 5.1 Module structure

| Module | Responsibility |
|---|---|
| `config.h` | Every tunable constant — no magic numbers anywhere else |
| `sensors` | Triggers ultrasonics round-robin, reads IMU, applies filtering, exposes clean values |
| `drive` | Only module that touches the L298N or PCA9685. Converts "steer to angle X, drive at speed Y" into hardware writes |
| `navigator` | Wall-following PID. Consumes sensor values, outputs a steering angle |
| `statemachine` | Owns run logic — which behaviour is active, when to transition |
| `main` | Fixed-rate scheduler; calls the above in order |

**Why this split:** hardware access is confined to `drive` and `sensors`. If we swap the L298N for a different driver, exactly one file changes. `navigator` and `statemachine` contain no pin numbers at all and can be reasoned about — or unit-tested — independently of hardware.

### 5.2 State machine

```
        (power on)
             |
             v
        +---------+   pack voltage low / I2C scan fails
        |  INIT   |-----------------------------------+
        +---------+                                   |
             | power good                             |
             v                                        |
        +-----------+                                 |
        | CALIBRATE |  5 s stationary, gyro bias      |
        +-----------+                                 |
             | bias stored                            |
             v                                        |
        +------------+                                |
        | WAIT_START |  servo centred, motor off      |
        +------------+                                |
             | button press                           |
             v                                        |
   +---> +-------------+                              |
   |     | FOLLOW_WALL |<------------+                |
   |     +-------------+             |                |
   |       |    |    |               |                |
   |       |    |    | front < corner threshold       |
   |       |    |    v               |                |
   |       |    | +------------------+                |
   |       |    | | APPROACH_CORNER  | slow down      |
   |       |    | +------------------+                |
   |       |    |         | front < turn trigger      |
   |       |    |         v                           |
   |       |    |    +---------+                      |
   |       |    |    | TURNING | hold until           |
   |       |    |    +---------+ rel. yaw ~ 90 deg    |
   |       |    |         |     then turn_count++     |
   |       |    |         +---------------------------+
   |       |    |                    (back to FOLLOW_WALL)
   |       |    |
   |       |    | both sides invalid OR front < min
   |       |    v
   |    +---------+
   +----| RECOVER |  stop, reverse, counter-steer
        +---------+
             ^
             |
             |  turn_count == 12 (3 laps)
             |         |
             |         v
             |    +--------+
             |    | FINISH |--> controlled stop --> (end)
             |    +--------+
             |
             |  I2C timeout OR pack voltage low
             |         |
             |         v
             +--> +-------+
                  | FAULT |--> motor off, servo centred,
                  +-------+    LED pattern, TERMINAL
```

**State descriptions:**

- **INIT** — bring up I²C, verify both devices ACK at their addresses, read pack voltage. Any failure → `FAULT`.
- **CALIBRATE** — 5 s stationary gyro bias capture (§4.4).
- **WAIT_START** — hold still, servo centred, motor off, waiting on the start button.
- **FOLLOW_WALL** — the main behaviour. PID centring between walls (§5.3).
- **APPROACH_CORNER** — front distance has dropped below the corner threshold. Reduce speed so the turn is entered at a controlled velocity; this makes turn radius repeatable.
- **TURNING** — apply a fixed steering angle, hold until the IMU reports the target relative yaw change. Increment the turn counter.
- **RECOVER** — Ackermann can't pivot, so recovery is: stop, reverse at reduced speed with steering counter-turned, then re-enter `FOLLOW_WALL` once side readings become valid again.
- **FINISH** — after 12 counted 90° turns (3 laps), come to a controlled stop within the section.
- **FAULT** — motor off, servo centred, status LED pattern. Deliberately terminal: a vehicle behaving unpredictably is worse than a stopped one.

### 5.3 Wall-following PID

**Error signal:**

```
error = distance_left − distance_right
```

Zero error means centred. This is preferred over following a single wall because it self-corrects for lane width changes and doesn't require knowing which wall you're near.

**Control law:**

```
steer = Kp·error + Ki·∫error·dt + Kd·(d error/dt)
steer = constrain(steer, −δ_max, +δ_max)
```

**Gain tuning method and results:**

| Gain | Value | How it was chosen |
|---|---|---|
| Kp | `[FILL]` | Raised until the vehicle oscillated steadily about centreline, then halved |
| Kd | `[FILL]` | Raised from zero until oscillation damped without sluggishness |
| Ki | `[FILL]` | Kept small/zero — steady-state offset was already negligible and integral windup during corners caused overshoot |

**Integral windup guard:** the integral term is frozen while in `TURNING`, because error is meaningless mid-corner and would otherwise accumulate a large false correction that whips the vehicle on corner exit. `[Record the run where you saw this happen.]`

**Loop rate:** the PID runs at `[FILL] Hz`, matched to the sensor refresh rate. Running the controller faster than the sensors update just re-processes stale data and amplifies noise through the derivative term.

### 5.4 Edge cases handled

| Edge case | Handling |
|---|---|
| Ultrasonic timeout (no echo returned) | Reading marked invalid; last good value held for ≤3 cycles, then treated as "wall absent" |
| `left + right` ≠ expected lane width | Frame rejected as physically impossible; both readings discarded |
| Both side sensors invalid | → `RECOVER` |
| Corner detected while error is large | Straighten first, then turn — entering a corner already off-centre compounds the error |
| Turn under-rotates (yaw short of 90°) | `TURNING` re-enters with a corrective sub-turn rather than exiting on a timeout |
| Vehicle nosed into a wall | `RECOVER` reverses (Ackermann limitation, §3.1) |
| Battery sags mid-run | `FAULT` on voltage cutoff — prevents erratic behaviour from a browning-out MCU |

### 5.5 Metrics used to validate performance

We do not tune by eye. Each configuration is scored on:

| Metric | Target | Measured |
|---|---|---|
| Lap completion rate (out of 10 runs) | ≥ 9/10 | `[FILL]` |
| Mean lateral deviation from centreline | < `[FILL] mm` | `[FILL]` |
| Wall contacts per 3-lap run | 0 | `[FILL]` |
| Mean lap time | `[FILL] s` | `[FILL]` |
| Turn angle error (mean \|actual − 90°\|) | < `[FILL]°` | `[FILL]` |

---

---

← [Power & Sensor Architecture](02-power-and-sensor-architecture.md) · [Main README](../README.md) · [Systems Thinking & Engineering Decisions](04-systems-thinking.md) →
