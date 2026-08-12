# Build & Reproduction Guide

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 7. Build & Reproduction Guide

### 7.1 Mechanical

1. Print/cut chassis parts from `models/`. Print settings used: `[FILL: material, layer height, infill, walls]`.
2. Assemble front axle: kingpins → steering knuckles → tie rod. **Verify Ackermann convergence** by eye before tightening: lines through each kingpin and tie-rod ball joint should meet near the rear axle centre.
3. Check full-lock clearance on both sides — no tyre-to-chassis contact, no wire in the swept path.
4. Mount rear axle and drive motor. Confirm the drivetrain spins freely by hand.
5. Fit sensor brackets at the heights in §4.4. Check each sensor face is perpendicular to its intended aim direction.

### 7.2 Electrical

1. Build the perfboard 5 V distribution rail **and the three ECHO dividers plus the battery-sense divider** (§4.6). Verify divider outputs with a multimeter — feed 5 V in, confirm ~3.3 V out — **before** connecting anything to the ESP32.
2. **Set the buck converter output to 6.0 V with no load connected.** Confirm with a multimeter. Only then connect the PCA9685 V+.
3. **Remove the L298N ENA jumper** (§4.7) before wiring ENA to GPIO 25.
4. Wire per the pin table in §4.5 and the module diagrams in §4.7.
5. Common ground: ESP32, PCA9685, L298N, perfboard and buck must all share a ground reference. **Skipping this is the most common cause of "the I²C device won't appear" and erratic servo behaviour.**
6. Fit the fuse and master switch on the pack positive line.
7. Before first power-up: continuity check, and confirm no short between pack + and −.

### 7.3 Firmware

```bash
git clone [REPO URL]
cd [REPO]
pio run -t upload        # PlatformIO
pio device monitor -b 115200
```

Run an I²C scan first — expect `0x40` (PCA9685) and `0x68` (MPU-6050). If either is missing, stop and check wiring and grounds before proceeding.

All tunable values live in `src/config.h`. Start from the committed defaults, then re-tune Kp/Kd for your surface (§5.3).

### 7.4 Pre-run checklist

- [ ] Pack voltage ≥ `[FILL] V`
- [ ] All cells within `[FILL] mV` of each other
- [ ] Buck output confirmed at 6.0 V
- [ ] I²C scan shows both devices
- [ ] All three ultrasonics returning plausible values
- [ ] Steering sweeps full range without binding
- [ ] Gyro calibration completed (vehicle stationary at power-up)
- [ ] No loose wires in the steering path

---

---

← [Systems Thinking & Engineering Decisions](04-systems-thinking.md) · [Main README](../README.md) · [Bill of Materials](06-bill-of-materials.md) →
