# Bill of Materials

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 8. Bill of Materials

| Item | Qty | Spec | Notes |
|---|---|---|---|
| ESP32 dev board | 1 | `[FILL variant]` | Main controller |
| PCA9685 | 1 | 16-ch, 12-bit, I²C | Steering servo on CH0 |
| L298N module | 1 | Dual H-bridge | Drive motor + 5 V logic rail |
| Buck converter | 1 | `[FILL model]`, set to 6.0 V | Servo rail |
| Steering servo | 1 | `[FILL model]` | Verify voltage rating |
| Drive motor | 1 | `[FILL model + gear ratio]` | |
| HC-SR04 | 3 | 5 V ultrasonic | Left, right, front |
| MPU-6050 | 1 | 6-axis IMU, I²C `0x68` | |
| 18650 cells | 3 | `[FILL mAh]`, matched | Series 3S |
| Fuse + holder | 1 | `[FILL] A` | Pack positive |
| Master switch | 1 | Rated ≥ `[FILL] A` | |
| Perfboard | 1 | | 5 V distribution + dividers |
| Resistor 1 kΩ | 3 | 1/4 W | ECHO divider R1 |
| Resistor 2 kΩ | 3 | 1/4 W | ECHO divider R2 |
| Resistor 100 kΩ | 1 | 1/4 W | Battery sense R1 |
| Resistor 22 kΩ | 1 | 1/4 W | Battery sense R2 |
| Capacitor 100 µF | 1 | electrolytic, ≥16 V | 5 V rail bulk decoupling |
| Capacitor 0.1 µF | 4 | ceramic | Local decoupling at each sensor |
| Chassis parts | — | `[FILL material]` | See `models/` |

---

---

← [Build & Reproduction Guide](05-build-guide.md) · [Main README](../README.md) · [Testing Log & Results](07-testing-log.md) →
