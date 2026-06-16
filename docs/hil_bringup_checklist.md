# HIL bring-up checklist

A staged procedure for bringing up the WEC HIL dynamometer with SEA-Stack hydrodynamics on real ODrive hardware. Designed around the two-gate safety model so that every subsystem is independently validated before the simulated hydrodynamic load reaches the motor.

> **When to use this doc:** when you are about to run `hil_mode.launch.py` on the live bench. For SIL-only development, use `sil_mode.launch.py` and the simpler single-gate flow in [`sea-stack-integration.md`](sea-stack-integration.md).

---

## The two-gate / three-gate safety model

SEA-Stack reaching the motor passes through **three gates**, only one of which is a runtime service. Understanding the distinction is the whole point of this checklist.

| # | Gate | Type | Where | Controls |
|---|---|---|---|---|
| 1 | `CHRONO_FLAP_USE_SEASTACK` | Build-time | `src/chrono_flap_sim/CMakeLists.txt` | Whether `sea_stack_hydro.cpp` is compiled into the binary at all |
| 2 | `seastack_h5_path != ""` | Launch-time | `chrono_flap_node` constructor | Whether the `SeaStackHydroAdapter` is constructed and wave field is built |
| 3 | `~/enable_hydro` service | **Runtime** | `chrono_flap_node` per-tick logic | Whether the per-tick `τ_hydro` is published as non-zero on `~/hydro_torque` (after clipping) |

HIL adds a **fourth gate** on top:

| # | Gate | Type | Where | Controls |
|---|---|---|---|---|
| 4 | `~/engage_hil` service | **Runtime** | `chrono_flap_node` per-tick logic (HIL mode only) | Whether `~/load_torque` is non-zero (with ramp-in over `hil_ramp_time_s`), which is what `hil_torque_mixer_node` sums into `/motor_effort_controller/commands` |

In SIL you open gate 3 only. In HIL you open gates 3 *and* 4, in that order, with verification between each.

### Why two runtime gates and not one

| Gate | What it lets you verify |
|---|---|
| `enable_hydro` | The BEM and wave model are sane (finite, oscillating, sign-changing torques) **without ever commanding the motor** |
| `engage_hil` | The motor + mixer + PID are sane **without exposing the motor to any hydro forces yet** |

If they were collapsed into one gate, you could not validate the BEM safely — you would have to open the motor path just to see if the hydro forces look reasonable.

The mixer adds a *fifth* defense-in-depth layer below both: an unconditional hard clamp at `±hard_clip_nm` (default 0.5 N·m) plus independent watchdogs on each input. Even if both engage gates are open and a bug feeds infinity into one input, the motor sees at most `±hard_clip_nm` for at most one watchdog period.

---

## Data flow (HIL)

```
SEA-Stack BEM (every solver tick)
  │
  ├─► ~/wave_elevation (published always)
  │
  └─► τ_raw ──► ~/hydro_torque_raw (published always)
                  │
                  ▼
            [enable_hydro?]  ◄─── /chrono_flap_node/enable_hydro (gate 3)
                  │
              yes │
                  ▼
            τ_clipped = clamp(τ_raw, ±hydro_torque_clip_nm)
                  │
                  ├─► ~/hydro_torque (published always; 0 when disengaged)
                  │
                  ▼
            [engage_hil?]    ◄─── /chrono_flap_node/engage_hil (gate 4)
                  │
              yes │ (with ramp-in over hil_ramp_time_s)
                  ▼
            ~/load_torque ─────────────────────────────────┐
                                                            │
/joint_states ──► velocity_pid_node ─► ~/torque_command ─► hil_torque_mixer_node
                                                            │ ┌──────────────────┐
                                                            │ │ τ_pid watchdog   │
                                                            │ │ τ_load watchdog  │
                                                            │ │ τ_total = clamp( │
                                                            │ │   τ_pid+τ_hydro, │
                                                            │ │   ±hard_clip_nm) │
                                                            │ └──────────────────┘
                                                            ▼
                                            /motor_effort_controller/commands
                                                            │
                                                            ▼
                                                       ODrive HW
```

In SIL, the path stops at `~/hydro_torque` and the value is applied directly to the Chrono body via `AccumulateTorque` (no mixer, no motor).

---

## Stage 0 — Hardware on the bench, software off

```bash
sudo ip link set can0 down 2&gt;/dev/null || true
sudo ip link set can0 up type can bitrate 250000
candump can0
```

If `candump` is silent, the motor will be silent. **Stop here and fix the bus** — wiring, termination, ODrive power, ODrive CAN bitrate configured to 250 kbps. See the troubleshooting matrix in the root [`README.md`](../README.md) and in [`src/hil_odrive_ros2_control/README.md`](../src/hil_odrive_ros2_control/README.md).

---

## Stage 1 — Launch with both runtime gates closed

```bash
source /opt/ros/jazzy/setup.bash
source scripts/setup_env.sh        # Chrono_DIR, SEAStack_DIR, CMAKE_PREFIX_PATH
source install/setup.bash

ros2 launch hil_odrive_ros2_control hil_mode.launch.py \
  enable_visualization:=true
```

> **Known gap (as of 2026-06-16):** `hil_mode.launch.py` does not currently propagate SEA-Stack args (`seastack_h5_path`, `wave_hs_m`, `wave_tp_s`, `hydro_torque_clip_nm`). See `STATUS.md` "Optional consolidation PRs" — a ~10-LOC PR mirroring the args from `sil_mode.launch.py` will close this gap. Until then, the workarounds are:
>
> 1. Run `chrono_flap_node` standalone with `--ros-args -p seastack_h5_path:=...` and the rest of the stack via the launch file, **or**
> 2. Edit `hil_mode.launch.py` locally to pass `seastack_h5_path` through.
>
> `ros2 param set` does **not** work for `seastack_h5_path` — it is read once in the constructor and frozen.

In the `chrono_flap_node` startup log you should see exactly one of:

| Log line | Meaning | Action |
|---|---|---|
| `SEA-Stack hydro ENABLED: h5='...', Hs=...m, Tp=...s, clip=±...Nm.` | Gates 1+2 open. Proceed. | Continue to Stage 2. |
| `SEA-Stack hydro DISABLED (seastack_h5_path is empty).` | Gate 2 closed — no H5 path. | Stop. Pass `seastack_h5_path:=...oswec.h5`. |
| `seastack_h5_path='...' provided but this node was built WITHOUT SEA-Stack` | Gate 1 closed — binary has no SEA-Stack. | Stop. Re-source `setup_env.sh`, rebuild `chrono_flap_sim`. |
| `SEA-Stack hydro init FAILED: ...` | H5 path bad / file unreadable / schema mismatch. | Stop. Verify file exists and is OSWEC-compatible. |

You should also see:

```
HIL mode: subscribing to /joint_states, publishing load on '~/load_torque'.
Engage via: ros2 service call ~/engage_hil std_srvs/srv/SetBool "{data: true}"
```

Both runtime gates are still closed at this point. The motor will move only from `velocity_pid_node`'s commanded trajectory.

---

## Stage 2 — Verify the control loop with hydro path quiescent

In a **separate terminal** (see "Common gotchas" below for why):

```bash
ros2 control list_controllers
# joint_state_broadcaster   active
# motor_effort_controller   active

ros2 topic echo /joint_states --once
# motor_joint position/velocity should be valid (not NaN)

ros2 topic hz /velocity_pid_node/torque_command
# 100 Hz

ros2 topic echo /motor_effort_controller/commands --once
# Float64MultiArray with one element from the mixer
```

In `rqt_reconfigure` on `/velocity_pid_node`, set a small trajectory:

- `amplitude_rad_s = 0.1`
- `omega_rad_s = 1.0`

Watch the motor track. The PID is the only thing driving torque now; the hydro path is producing exactly 0 because `engage_hil` is closed. Confirm:

- Motor tracks the sine cleanly
- `velocity_pid_node` log shows `sat=0` (not saturating)
- No runaway, no oscillation, no sign error

If anything misbehaves here, **kill the launch with Ctrl-C and fix it before going further**. Hydro forces stacking on top of a misbehaving PID is the worst-case scenario.

---

## Stage 3 — Inspect raw hydro signal *before* engaging anything

This is the safety insurance from `docs/sea-stack-integration.md`: validate the BEM produces finite, oscillating, sign-changing torques **before** they can ever reach the motor.

```bash
ros2 topic echo /chrono_flap_node/hydro_torque_raw --once
ros2 topic echo /chrono_flap_node/wave_elevation --once
```

Or drag both onto a PlotJuggler canvas and watch for 10–20 seconds. Confirm three things:

1. `hydro_torque_raw` is **finite** (not NaN, not ±inf)
2. `hydro_torque_raw` **changes over time** (the wave period if waves are on, or the body motion period if not)
3. `hydro_torque_raw` **changes sign** at least occasionally

These are the "alive" criteria from `STATUS.md`. If raw shows sane values, clipping at `hydro_torque_clip_nm` will be safe.

`hydro_torque` should still be exactly 0.0 — that confirms gate 3 is closed.

> **Expected scale mismatch on OSWEC BEM:** `hydro_torque_raw` will be on the order of ±10⁴ to ±10⁶ N·m because the OSWEC H5 describes a 26 m offshore structure, not a bench flap. This is **expected and documented** in [`sea-stack-integration.md` §7](sea-stack-integration.md). After engaging, `hydro_torque` will pin at `±hydro_torque_clip_nm` and `hydro_clip_engaged_pct` will sit near 100. That is the protective clip working, not a bug.

---

## Stage 4 — Open gate 3 (`enable_hydro`)

```bash
ros2 service call /chrono_flap_node/enable_hydro std_srvs/srv/SetBool "{data: true}"
# response: success=True, message='SEA-Stack hydro torque ENGAGED'
```

`~/hydro_torque` will now become ±`hydro_torque_clip_nm` instead of 0. **But the motor still does not feel it** because gate 4 is still closed — `~/load_torque` is still 0, so the mixer's sum is `τ_pid + 0 = τ_pid`.

Verify:

```bash
ros2 topic echo /chrono_flap_node/hydro_torque --once       # ±hydro_torque_clip_nm
ros2 topic echo /chrono_flap_node/load_torque --once        # still 0.0 (gate 4 closed)
ros2 topic echo /motor_effort_controller/commands --once    # = τ_pid only
```

The motor's commanded torque should be unchanged from Stage 2.

---

## Stage 5 — Open gate 4 (`engage_hil`) — now the motor feels hydro

```bash
ros2 service call /chrono_flap_node/engage_hil std_srvs/srv/SetBool "{data: true}"
# response: success=True, message='HIL load torque ENGAGED'
```

This triggers the **ramp-in** in `chrono_flap_node`: τ_load smoothly ramps from 0 to its current target over `hil_ramp_time_s` (default 1.0 s). No step changes hitting the motor.

After ~1 second:

```bash
ros2 topic echo /chrono_flap_node/load_torque --once        # = τ_hydro after ramp
ros2 topic echo /motor_effort_controller/commands --once    # = τ_pid + τ_hydro
```

The motor is now resisting the PID with the hydro load. **Hand on the kill switch from this point on.**

---

## Stage 6 — Disengage in this order if anything looks wrong

```bash
# 1) Drop the hydro load first — mixer immediately sees 0 load + watchdog timeout
ros2 service call /chrono_flap_node/engage_hil std_srvs/srv/SetBool "{data: false}"

# 2) Optionally also kill SEA-Stack publishing entirely
ros2 service call /chrono_flap_node/enable_hydro std_srvs/srv/SetBool "{data: false}"

# 3) If something is really wrong, kill the launch with Ctrl-C — both
#    chrono_flap_node and hil_torque_mixer_node publish a final 0.0 to the
#    motor effort topic on destruction (per README).
```

---

## Common gotchas (learned the hard way 2026-06-16)

### `ros2 service call` hangs as "waiting for service to become available..." from the launching terminal

The default single-threaded executor in `chrono_flap_node` is split between: timer callback (Chrono solver + 8 publishers), VSG render, parameter callbacks, and service callbacks. Under VSG+solver load it gets saturated, and a service call from the **same shell session** that started the launch can sit indefinitely while DDS discovery and the service round-trip wait for executor time.

**Workaround:** open a separate terminal, source the workspace, and call the service from there. It will complete in milliseconds.

**Fix:** see `STATUS.md` "Optional consolidation PRs" — `MultiThreadedExecutor` + dedicated service callback group, ~15 LOC. Not yet implemented.

### `wave_hs_m` and other wave params do not respond to `ros2 param set` or `rqt_reconfigure`

The wave field is constructed **once** in `SeaStackHydroAdapter::SeaStackHydroAdapter()` from the values read at node startup. There is no code path that rebuilds the wave field after construction. Changing `wave_hs_m` in `rqt` updates the parameter value (rqt confirms it) but has zero physical effect.

**Frozen at construction, require relaunch:**

- `seastack_h5_path`
- `wave_hs_m`
- `wave_tp_s`
- `wave_seed`
- `wave_n_components`
- `hydro_torque_clip_nm` *(note: `sea-stack-integration.md` documents this as mutable, but the current implementation actually freezes it — see the parameter-table discrepancy in the issues section of `STATUS.md`)*
- `mode` / `sil_mode`
- `rate_hz` / `solver_rate_hz`
- `joint_name`, `joint_state_topic`, `effort_topic`
- `enable_visualization`

**Genuinely live-mutable via `rqt_reconfigure`:**

- `bearing_friction`, `joint_damping`, `joint_stiffness`, `coulomb_friction`
- `flap_mass_kg`, `flap_length_m`, `flap_width_m`
- `use_shadow_pid`
- All `shadow_*` PID gains
- All `velocity_pid_node` PID gains and trajectory params

**Workaround:** to change wave parameters, kill the launch and relaunch with new args.

**Fix:** a future PR can wire an `on_apply_parameters` branch that rebuilds the SEA-Stack wave model in-place when `wave_*` params change. ~50 LOC and not in scope today.

### `hydro_torque_raw = 0.0` even after launching with an H5 path

If the body is stationary AND `wave_hs_m = 0.0`, all four hydro components (excitation, radiation, hydrostatic, added-mass) evaluate to ~0 — there is genuinely nothing to compute. This is **not a bug**, but it looks like one.

**Resolutions in order of effort:**

1. Drive the flap from `velocity_pid_node`: set `amplitude_rad_s = 0.1`, `omega_rad_s = 1.0` in `rqt_reconfigure`. Radiation + hydrostatic become non-zero immediately.
2. Relaunch with waves: `wave_hs_m:=0.05`. Excitation becomes non-zero immediately, even with a stationary body.

### `chrono_flap_node` running but not in PlotJuggler tree

PlotJuggler caches topic discoveries. If `chrono_flap_node` was started after PlotJuggler, click the "Streaming" refresh button or restart streaming to re-discover topics.

---

## Cross-references

- [`README.md`](../README.md) — repository overview, build, SIL/parallel/HIL quick starts
- [`docs/sea-stack-integration.md`](sea-stack-integration.md) — SEA-Stack adapter details, parameter reference, troubleshooting matrix
- [`STATUS.md`](../STATUS.md) — end-of-week status as of 2026-06-12, plus the "Operational lessons" appendix from 2026-06-16
- [`src/chrono_flap_sim/README.md`](../src/chrono_flap_sim/README.md) — `chrono_flap_node` physics model and full parameter reference
- [`src/hil_odrive_ros2_control/README.md`](../src/hil_odrive_ros2_control/README.md) — HIL hardware launch, ODrive setup, safety mechanisms table

---

*Captured during 2026-06-16 (Denver / MDT) SIL bring-up session — 4 days after the 2026-06-12 end-of-week status. See `STATUS.md` "Operational lessons from 2026-06-16 SIL bring-up" for the abbreviated changelog.*
