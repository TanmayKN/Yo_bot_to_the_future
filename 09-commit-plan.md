# Commit Plan & Message Convention

*WRO Future Engineers 2026 — [back to main README](../README.md)*

---

## Message convention

Every commit message states **what was added and why it matters**, in the imperative.
No numbering — "Commit 4" tells a reader nothing, and the rubric's level-6 descriptor
asks specifically for *meaningful* commit messages.

| Good | Why it works |
|---|---|
| `Add ESP32 pin map and system block diagram` | Names the artefact and its purpose |
| `Add rendered previews of the 3D printed parts` | A reader knows what changed without opening the diff |
| `Add software architecture and detection overlay images` | Specific — "images" alone would not be |
| `Freeze PID integral during turns to stop corner overshoot` | States the fix *and* the problem it solves |
| `Reduce servo rail to 6.0 V, was above servo voltage rating` | Captures the reasoning, not just the value |

| Avoid | Why |
|---|---|
| `Commit 5` | Carries no information |
| `update code` | Every commit updates code |
| `fixes` | Fixes what? |
| `final version` | There is always another version |

**Prefixes are optional but help.** `Add:` for new material, `Fix:` for corrections,
`Tune:` for parameter changes, `Docs:` for documentation only.

---

## The three required commits

The rules require a minimum of three commits, timed backwards from **your** competition
date — not a fixed calendar date. Fill in the dates below the moment your event date is
confirmed.

| Commit | Deadline | Must contain |
|---|---|---|
| 1st | no later than **2 months** before | at least **1/5 of the final code** |
| 2nd | no later than **1 month** before | — |
| 3rd | no later than **2 weeks** before | **everything** — this is the commit judges score |

More commits are allowed and encouraged. Later changes may not be counted, so anything
that needs to be seen must be in by the third commit.

| | Date |
|---|---|
| Competition date | `[FILL]` |
| 1st commit deadline | `[FILL]` |
| 2nd commit deadline | `[FILL]` |
| 3rd commit deadline | `[FILL]` |

---

## Suggested sequence

Work down this list. Each row is one commit. The point of splitting it this way is that
the history itself becomes evidence of the engineering process — which is exactly what
Criterion 5 is looking for.

### Milestone 1 — foundation *(satisfies the 1/5-of-code requirement)*

```
Add repository structure and engineering documentation
Add ESP32 pin map and system block diagram
Add drive and sensor acquisition layer
Add wall-following PID and state machine
```

### Milestone 2 — hardware evidence

```
Add rendered previews of the 3D printed parts
Add Fusion 360 source and STL exports for chassis and knuckles
Add vehicle photos from all six required angles
Add team photos
```

### Milestone 3 — iteration *(the highest-value commits)*

```
Reduce servo rail to 6.0 V, was above servo voltage rating
Freeze PID integral during turns to stop corner overshoot
Reject sensor frames where left plus right deviates from lane width
Add testing log for open challenge runs 1 to 20
```

### Milestone 4 — completion

```
Add software architecture and detection overlay images
Add obstacle challenge strategy and parking routine
Add driving demonstration video links
Rewrite the README as a full engineering write-up
```

---

## Releases

Tag a release at each competition-ready state so the repository shows versioning, which
the level-6 descriptor names explicitly:

```bash
git tag -a v1.0-regional -m "Regional competition build"
git push origin v1.0-regional
```

| Tag | State |
|---|---|
| `v1.0-regional` | `[FILL: date]` |
| `v2.0-national` | `[FILL: date]` |

---

## Repository visibility

The repository must be **public from the moment it is submitted** and stay public for at
least 12 months afterwards. A repository that is private when a judge opens it scores as
missing — the worst outcome on Criterion 5, and entirely avoidable.

---

← [Annex A — Full-size Diagrams](08-annex-a-diagrams.md) · [Main README](../README.md)
