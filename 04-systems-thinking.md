# Systems Thinking & Engineering Decisions

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 6. Systems Thinking & Engineering Decisions

### 6.1 How the subsystems interact

```
SENSING          →  DECISION        →  ACTUATION
ultrasonic ×3       navigator PID      PCA9685 → servo → Ackermann linkage
MPU-6050            statemachine       L298N   → motor → rear axle
pack voltage        fault logic
        ↑                                          |
        └────────── physical vehicle motion ───────┘
```

The critical coupling is that **actuation changes what the sensors see**, which changes the next decision. Three specific consequences shaped the design:

1. **Sensor refresh rate caps drive speed** (§3.3). The subsystems are not independent — the electrical choice of ultrasonic over ToF sensors directly constrained the mechanical gearing choice.
2. **Servo current spikes could reset the MCU.** Isolating the servo onto its own buck-fed rail (§4.4) is a *power* decision made to protect a *software* guarantee.
3. **Mount rigidity is a sensing spec, not a mechanical one** (§3.4). A flexing bracket produces data indistinguishable from a moving wall.

### 6.2 Explicit constraints

| Constraint | Source | Effect on design |
|---|---|---|
| Vehicle must fit WRO size envelope | Competition rules — `[verify 2026 figures]` | Set maximum wheelbase, forced compact battery placement |
| Mass limit | Competition rules — `[verify]` | Ruled out a metal chassis plate |
| Single steering actuator | Design choice for simplicity | Required a mechanical Ackermann linkage rather than per-wheel actuation |
| Sensor budget | Available parts | 3 ultrasonics, not 5 — hence the `left+right` validity check to compensate |
| I²C bus shared | PCA9685 + IMU | Single point of failure, mitigated in §6.5 |
| Pack ≥9.0 V for safe operation | Li-ion cell chemistry | Firmware cutoff, reduced usable capacity |

### 6.3 Iteration log

| # | Version | Problem observed | Change made | Result |
|---|---|---|---|---|
| 1 | v0.1 | `[e.g. steering linkage bound at full lock]` | `[change]` | `[result]` |
| 2 | v0.2 | `[e.g. side ultrasonics read long on angled approach]` | `[change]` | `[result]` |
| 3 | v0.3 | Servo rail at 7.8 V exceeded servo rating | Buck adjusted to 6.0 V | Servo temperature after 10 min run dropped from `[FILL]` to `[FILL]` |
| 4 | v0.4 | Corner exit overshoot | Integral term frozen during `TURNING` | Turn angle error reduced from `[FILL]°` to `[FILL]°` |
| 5 | `[FILL]` | | | |

> Fill these with **your actual runs**, with dates and photos in `docs/iteration_photos/`. The rubric rewards evidence of iteration far more than a description of the final state. A problem you found and fixed scores better than a design that appears to have worked first time.

### 6.4 Decisions: why X instead of Y

| Decision | Chosen | Rejected alternative | Reasoning |
|---|---|---|---|
| Steering architecture | Ackermann | Differential/skid | Deterministic turn radius, valid odometry, matches category intent (§3.1) |
| Servo control | PCA9685 over I²C | Direct ESP32 LEDC PWM | Timing independent of CPU load, servo current isolated from MCU, GPIO freed (§4.4) |
| Motor driver | L298N | TB6612FNG / DRV8871 | L298N was on hand and proven; **acknowledged cost:** ~2 V drop wastes roughly `[FILL] W` as heat and reduces usable pack voltage. TB6612FNG is the identified upgrade path if we need more speed from the same pack |
| Distance sensing | HC-SR04 ×3 | VL53L0X ToF | Ultrasonics are robust to surface colour and cost far less; **cost:** specular reflection at high yaw angles (§4.4) |
| Heading | MPU-6050 | Wheel-encoder-derived heading | IMU measures heading directly rather than inferring it, so wheel slip doesn't corrupt it; **cost:** gyro drift, handled by zeroing per straight |
| Battery | 3S Li-ion | 2S Li-ion / NiMH | Voltage headroom above L298N drop; low internal resistance prevents brown-out (§4.2) |
| 5 V sensor rail | L298N onboard 78M05 | Dedicated second buck | Fewer parts; **cost:** running near its input voltage limit at 12.6 V. Second buck is the mitigation if we add sensors |

### 6.5 Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Specular reflection causes phantom readings | High | Run failure | `left+right` cross-check; hold-last-good ≤3 frames; `RECOVER` state |
| I²C fault kills steering + IMU together | Low | Total run failure | Short shielded leads, correct pull-ups, firmware I²C timeout → `FAULT` (motor off, servo centred) |
| Battery sag under motor load resets MCU | Medium | Run failure | Separate servo rail; pack with low internal resistance; voltage monitoring |
| Gyro drift accumulates over 3 laps | Medium | Turn miscount | Yaw zeroed each straight; only relative angles used |
| Servo degradation over season | Medium | Progressive steering slop | Rail voltage corrected to 6.0 V; spare servo carried |
| Solder joint failure on perfboard | Medium | Intermittent sensor loss | Strain relief; pre-run continuity check in the checklist |
| Wire caught in steering linkage | Low | Immediate steering loss | Full-lock clearance verified; harness routed clear (found in iteration #1) |

---

---

← [Software Architecture & Obstacle Strategy](03-software-architecture.md) · [Main README](../README.md) · [Build & Reproduction Guide](05-build-guide.md) →
