# Mobility & Mechanical Design

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## 3. Mobility & Mechanical Design

### 3.1 Why Ackermann

The obvious alternative was **differential (tank/skid) steering** with two independently driven wheels. Ackermann was chosen for three reasons:

1. **Rolling instead of scrubbing.** In skid steering, the inside and outside wheels must slip laterally during every turn. On the WRO mat surface this produces unpredictable, non-repeatable turn radii — the same motor command yields a different heading change depending on load, grip and battery voltage. Ackermann wheels roll along their own arcs, so **steering angle maps to turn radius deterministically**, which is what makes open-loop corner execution repeatable.
2. **Odometry stays valid.** Because the wheels don't scrub, wheel rotation still corresponds to distance travelled. Under skid steering, encoder-based distance estimates are corrupted every time you turn.
3. **Category intent.** Future Engineers is explicitly modelled on a road-going self-driving car. A steered-front-axle architecture matches the problem the challenge is posing.

**Trade-off accepted:** Ackermann cannot pivot in place. If the vehicle ends up nose-into a wall it must reverse to recover, so the software needs an explicit recovery state (see §5.4). Skid steering would have made recovery trivial. We judged deterministic cornering to be worth the added software complexity.

> ⚠️ **Rules check:** confirm the current WRO 2026 Future Engineers rules text on steering mechanism requirements before finalising. Note this in your engineering journal with the rules version and date checked.

### 3.2 Ackermann geometry

For a vehicle turning about a centre point, the inner and outer front wheels must trace different radii. The correct-Ackermann condition is:

```
cot(δ_outer) − cot(δ_inner) = W / L
```

where **W** = track width, **L** = wheelbase, **δ** = steer angle of each wheel.

We approximate this using a **trapezoidal linkage**: the steering arms are angled inward so that a line drawn through each kingpin and its tie-rod ball joint converges near the centre of the rear axle. This gives near-correct Ackermann across our working steering range without needing a full four-bar solver.

| Parameter | Value | Notes |
|---|---|---|
| Wheelbase L | `[FILL] mm` | |
| Track W | `[FILL] mm` | |
| Max steer angle δ_max | `[FILL]°` | limited by tyre-to-chassis clearance |
| Min turn radius R_min | `R = L / tan(δ_max)` = `[FILL] mm` | |
| Steering arm angle | `[FILL]°` | set by Ackermann convergence construction |

**Why R_min matters:** the corner of the WRO field requires the vehicle to change heading 90° within the lane width. If `R_min` exceeds roughly half the lane width minus vehicle half-width, the vehicle physically cannot make the corner without a multi-point turn. Measure your actual lane width from the 2026 field spec and record the margin here: `[FILL: measured clearance in mm]`.

`[INSERT: CAD screenshot of steering linkage, top view, with Ackermann construction lines]`

### 3.3 Drive & torque/speed reasoning

Rear axle is driven by a single geared DC motor through `[FILL: differential / solid axle / single-wheel]`.

**Torque requirement.** Force needed to accelerate the vehicle:

```
F = m·a + F_rolling
F_rolling = C_rr · m · g       (C_rr ≈ 0.015 for rubber on a smooth mat)
T_wheel = F · r_wheel
T_motor = T_wheel / gear_ratio / η
```

Worked example with our numbers:

| Term | Value |
|---|---|
| Mass m | `[FILL] kg` |
| Target acceleration a | `[FILL] m/s²` |
| Wheel radius r | `[FILL] m` |
| Gear ratio | `[FILL]:1` |
| Drivetrain efficiency η | ~0.8 assumed |
| **Required motor torque** | `[FILL] N·m` |
| **Motor rated stall torque** | `[FILL] N·m` |
| **Margin** | `[FILL]×` |

**Speed requirement.** Top speed `v = ω_motor · r / gear_ratio`. We target `[FILL] m/s`, chosen so that at maximum speed the ultrasonic sensing loop (≈`[FILL] Hz`, §4.4) still gives at least `[FILL]` distance samples before the vehicle would reach a wall from its detection range. **This is the coupling that sets our speed ceiling** — going faster than the sensors can refresh means driving blind between samples.

**Trade-off accepted:** we deliberately geared for torque over top speed. A faster vehicle scores no extra points if it fails to complete laps; reliability at moderate speed beat speed with wall strikes in testing (see §9).

### 3.4 Mechanical stability

- **Centre of mass:** the battery pack (heaviest single item) is mounted `[FILL: low / between axles]` to keep CoM low and rearward-biased, loading the drive wheels for traction while avoiding front-end lift.
- **Sensor mounts:** ultrasonics are on `[FILL: printed / laser-cut]` brackets rigidly bolted to the chassis plate, not cantilevered, because a mount that flexes changes the sensor's aim angle under acceleration and injects distance error that looks exactly like a real wall movement.
- **Wire management:** all harnesses are routed and strain-relieved so no wire can enter the steering linkage's swept volume at full lock. This was found the hard way — see iteration log §6.3.

---

---

[Main README](../README.md) · [Power & Sensor Architecture](02-power-and-sensor-architecture.md) →
