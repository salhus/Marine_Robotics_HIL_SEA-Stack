# Project Status — End of Week 2026-06-12

**Repo:** [`salhus/Marine_Robotics_HIL_SEA-Stack`](https://github.com/salhus/Marine_Robotics_HIL_SEA-Stack)
**HEAD:** `4ea782d` on `main`
**Tagline:** Wave Energy Converter HIL dynamometer with SEA-Stack hydrodynamics — ROS 2 Jazzy + Project Chrono + SEA-Stack integration

---

## TL;DR

The SEA-Stack ↔ Chrono ↔ ROS 2 hydrodynamic pipeline is **verified end-to-end in SIL** at OSWEC reference scale, with reproducible-from-clean-checkout build, full ROS 2 ecosystem integration (PlotJuggler, rqt_reconfigure, RViz2 all working live), and the HIL plumbing scaffolded and built. Bench-scale validation pending toy-flap BEM (Capytaine), which is the next session's focus.

---

## ✅ What is verified working

### SEA-Stack hydrodynamics in Chrono via ROS 2 (SIL)

- **1,261 SEA-Stack symbols** linked into `chrono_flap_node` (proves real integration, not stubs)
- Hydrodynamic forces computed by SEA-Stack BEM (added mass, radiation damping, excitation, hydrostatics) drive Chrono multibody dynamics each tick
- JONSWAP irregular-wave spectrum confirmed visually clean in PlotJuggler (~1 Hz oscillation, smooth waveform, statistical irregularity present)
- Safety gate (`~/enable_hydro` `SetBool` service) verified: pre-engage `hydro_torque = 0.0`, post-engage non-zero
- Torque clip (`hydro_torque_clip_nm`, default 0.2 N·m) verified: raw ±10⁵ N·m clipped to ±0.2 N·m on OSWEC-scale BEM (100% clip engagement, exactly as designed — see `docs/sea-stack-integration.md` §7)
- Wave elevation publishing live, finite, oscillating

### ROS 2 conventions

| Surface | Count | Notes |
|---|---|---|
| Publishers | 8 | 4 SEA-Stack telemetry + 4 sim state + standard `/joint_states`, `/parameter_events`, `/rosout` |
| Subscribers | 3 | velocity_pid commands, parameter_events |
| Services | 8 | `enable_hydro` + 7 standard ROS param services |
| Parameters | 9 | `seastack_h5_path`, `hydro_torque_clip_nm`, `wave_hs_m`, `wave_tp_s`, `wave_seed`, `wave_n_components`, `hydro_engaged_default`, `hil_wave_amp_nm`, `hil_wave_omega_rad_s` |

### Reproducible build

`source scripts/setup_env.sh && colcon build` succeeds from a clean clone, in a bare environment (`env -i` verified). No `--cmake-args` ceremony required. Idempotent env helper, documented install-vs-build-tree gotcha, CMake env-var injection for `CMAKE_PREFIX_PATH` propagation through colcon.

### Live introspection (screenshot-verified)

- **VSG window** rendering flap with proper physics (151 s model time, RTF 1.17, 95 FPS, OSWEC topology: 1 body / 1 link / 6 states / 5 constraints → 1 free DOF)
- **PlotJuggler** subscribing to `~/hydro_torque_raw` in real time, showing clean JONSWAP-driven oscillation
- **RViz2** rendering URDF via `/robot_description` with proper TF tree (`base_link` fixed frame)
- **rqt_reconfigure** live-tuning the cascade-PID controller (kp/ki/kd, kff/kaff, integral limits, saturations) on `velocity_pid_node`

All four GUIs running concurrently against the same `chrono_flap_node` without conflict.

### Controls infrastructure (already in place, validated alongside SEA-Stack)

- Cascade PID (position outer loop → velocity inner loop) at 100 Hz
- Feedforward (velocity FF `kff=0.4`, acceleration FF `kaff=0.2`)
- Integral windup limits + output saturation + torque limit (defense in depth)
- Runtime-tunable via `rqt_reconfigure`

---

## 🟡 What is plumbed but not validated this week

### HIL launch (packages built, launch artifact exists)

- `hil_odrive_ros2_control`, `hil_torque_mixer`, `odrive_ros2_control` all build cleanly
- `install/hil_odrive_ros2_control/share/.../launch/hil_mode.launch.py` present and launchable
- **Gap:** `hil_mode.launch.py` does not currently propagate SEA-Stack launch args (`seastack_h5_path`, `wave_*`, `hydro_torque_clip_nm`). Workaround: pass `-p seastack_h5_path:=...` as ROS args directly. Documented in `docs/sea-stack-integration.md` §4.
- **Follow-up:** ~10-LOC PR mirroring SEA-Stack args from `sil_mode.launch.py` into `hil_mode.launch.py`. Non-blocking.

### Live-motor HIL

- The plumbing exists and would launch, but **end-to-end HIL with live ODrive motors was not validated this week**. Queued for a future session once bench-scale BEM (Capytaine) is in hand and motor-side characterization can begin.

---

## ❌ What is explicitly out of scope (today)

- Water-surface visualization in VSG (investigated, deferred — see "Decisions & open paths" below)
- Multi-body / N-body H5 support (hardcoded to 2 bodies in `H5FileInfo(path, 2)`; sufficient for current OSWEC + future toy flap; generalizes later)
- CI on GitHub Actions for the SEA-Stack code path (existing CI builds without SEA-Stack; SEA-Stack-enabled build is local-only for now)
- Bench-scale BEM coefficients (next session — Capytaine on toy flap)
- Hydro scale-factor parameter for runtime down-scaling (future PR; the clip is the bench safety mechanism today)
- Lighter/translucent QoS for telemetry topics to silence DDS chatter ("A message was lost!!!" info messages on BEST_EFFORT topics — benign, would clean up in a small QoS-tightening PR)

---

## 📚 Documentation captured this week

- [`docs/sea-stack-integration.md`](docs/sea-stack-integration.md) — full SEA-Stack integration guide with troubleshooting, the install-vs-build-tree gotcha, the 100%-clip-at-OSWEC-scale explanation (§7), and the `setup_env.sh` quickstart (§11)
- [`scripts/setup_env.sh`](scripts/setup_env.sh) — idempotent env helper exporting `Chrono_DIR`, `SEAStack_DIR`, prepending `CMAKE_PREFIX_PATH`
- [`README.md`](README.md) — updated with SEA-Stack-enabled quickstart

---

## 🗺 H5 schema reference (captured during this session, not yet committed)

SEA-Stack reads the WEC-Sim/BEMIO H5 schema. Brief reference:

### Top-level structure

```
/
├── bem_data/code                      # solver name (string)
├── simulation_parameters/
│   ├── w                              # frequencies [rad/s], shape (N, 1)
│   ├── T                              # periods    [s]
│   ├── g, rho, water_depth            # scalars, shape (1, 1)
│   ├── wave_dir                       # directions [deg], usually [0]
│   └── scaled                         # int flag
└── bodyN/                             # N = 1, 2, … (repeat per body)
    ├── properties/
    │   ├── name                       # string
    │   ├── body_number                # int
    │   ├── cg, cb                     # 3-vec, shape (3, 1) [m]
    │   ├── disp_vol                   # scalar [m³]
    │   ├── dof                        # int, typically 6
    │   ├── dof_start, dof_end         # 1-based DOF indices in stacked system
    └── hydro_coeffs/
        ├── linear_restoring_stiffness # [DOF, DOF]
        ├── added_mass/
        │   ├── inf_freq               # [DOF, DOF]
        │   ├── all                    # [DOF, DOF, N_freq]
        │   └── components/i_j         # per-element slices
        ├── radiation_damping/
        │   ├── all, components/i_j
        │   ├── impulse_response_fun/  # K, t, w, components/K/i_j
        │   └── state_space/A,B,C,D    # SS realization
        └── excitation/
            ├── re, im, mag, phase     # [DOF, N_freq, N_dir]
            ├── froude-krylov/         # F-K only
            └── scattering/            # scattering only
```

### Key shape conventions

- Even scalars are 2-D arrays of shape `(1, 1)`
- Frequency vector is `(N, 1)` column, not `(N,)`
- DOF indexing is **1-based** (MATLAB legacy from WEC-Sim)
- All datasets have `description` and `units` attributes
- Body count is the integer passed to `H5FileInfo(path, num_bod)` — defaults to 1 in upstream, hardcoded to 2 in this repo

### Reference fixtures on the local SEA-Stack checkout

| Demo | Bodies | Best for |
|---|---|---|
| `iea_sphere/assets/hydroData/sphere.h5` | 1 | Simplest valid template |
| `oswec/assets/hydroData/oswec.h5` | 2 | Bench-topology match (flap + base) |
| `rm3/assets/hydroData/rm3.h5` | 2 | Another 2-body reference |
| `wigley/.../wigley_directional.h5` | 1 | Single-body, directional waves |
| `5sa/assets/hydroData/5sa.h5` | 5 | Multi-body extreme |

---

## 🧭 Decisions made this week (with rationale)

- **Build now, vis later.** Established build/run/topic-flow correctness before any visualization work.
- **Vendoring rejected.** SEA-Stack's `apps/seastack/gui/` water-surface visualizer would have required vendoring ~135K of code (mooring + radiation hard-deps included). Decided to defer water vis entirely; if revisited later, will write a minimal ~200-LOC `WaterSurfaceViz` using public Chrono + `WaveBase::GetElevation` APIs (no vendoring).
- **100% clip on OSWEC is correct, not a bug.** The clip is the bench safety mechanism. OSWEC torques (±10⁵ N·m) being clipped to ±0.2 N·m is exactly the deliberate scale-mismatch behavior. Clip percentage will drop naturally when toy-flap BEM is swapped in. Documented in §7.
- **Capytaine over a custom toy-H5 generator.** Standard WEC-Sim H5 schema is well-supported by `capytaine.io.bemio` / `bemio-py`. Conversion is mechanical, not research.
- **Pure-keyword `target_link_libraries`** (PR #3). The plain-vs-keyword conflict was the actual root cause of the earlier build failures, not VSG misconfiguration.
- **Indexed accumulator pattern** for Chrono `AccumulateTorque` (PR #3). The old `(vec, bool)` signature was deprecated; the correct API is `(idx, vec, bool)` after `AddAccumulator()`, with `EmptyAccumulator(idx)` per tick to prevent drift.

---

## ⏭ Next session

1. **Capytaine BEM** on toy-flap geometry (acrylic plate + pivot housing as two free 6-DOF bodies, freshwater `rho=1000`, depth = bench tank depth)
2. **Convert Capytaine NetCDF → WEC-Sim H5** matching the `oswec.h5` schema above (use `capytaine.io.bemio` or `bemio-py`)
3. **Validate with `h5diff`** against `oswec.h5` (shapes should match; values will differ)
4. **Drop in `toy_flap.h5`** with `wave_hs_m:=0.02`, expect `~/hydro_clip_engaged_pct` to drop from ~100% toward 0%
5. **Tune PID gains** for bench-scale forces in `rqt_reconfigure`
6. **Then HIL** with live ODrives — actual research begins

### Optional consolidation PRs (low priority, can wait)

- Bump hardcoded `H5FileInfo(path, 2)` → `H5FileInfo(path, num_bod)` driven by new `seastack_num_bodies` ROS param (default 2). ~5 LOC. Unlocks `sphere.h5` for sanity-checking the pipeline without Capytaine compute.
- Add `docs/h5-schema-reference.md` with the schema captured above
- `scripts/inspect_h5.py` (~50 LOC `h5py`) to summarize any H5 (freq range, body count, NaN check) for debugging Capytaine output
- Propagate SEA-Stack launch args from `sil_mode.launch.py` into `hil_mode.launch.py`. ~10 LOC.
- Tighten QoS on telemetry topics to silence DDS "message lost" info messages

---

## 📜 Merged PRs this week

| # | Title | Effect |
|---|---|---|
| #1 | Initial repo seed | Working Chrono HIL baseline (pre-SEA-Stack) |
| #2 | `feat(chrono_flap_sim): integrate SEA-Stack hydrodynamics with engage gate and torque clip` | Wired SEA-Stack into `chrono_flap_node`: 4 new topics, 1 service, 9 params, hydro forces driving Chrono dynamics |
| #3 | `fix(chrono_flap_sim): post-PR-#2 SEA-Stack build/runtime fixes + env helper + docs` | Build reproducibility from clean clone: CMake fixes, indexed-accumulator API, `setup_env.sh`, +167 lines of docs |

---

## 🏁 Honest claim, as of `4ea782d` on `2026-06-12`

> SEA-Stack hydrodynamic forces are integrated into the ROS 2 + Chrono pipeline (verified in SIL with OSWEC BEM: 1,261 linked symbols, full topic flow, parameterized launch, service-gated engagement, with clip-engagement behavior matching design intent). The node is a fully-instrumented professional controls bench — cascade PID with feedforward, runtime-tunable via `rqt_reconfigure`, live-observable via PlotJuggler + RViz2 + Chrono VSG. Build reproduces from clean clone via `setup_env.sh + colcon build`. HIL packages compile and HIL launch artifacts exist on the same code path; full HIL validation with live motors is queued for once bench-scale BEM (Capytaine on toy flap) is available.

---

## 📝 Operational lessons from 2026-06-16 SIL bring-up

Four days after the end-of-week status above, a fresh SIL bring-up session rediscovered several operational gotchas not previously documented. These are not bugs in the verified SIL pipeline — that pipeline still works exactly as `2026-06-12` reported. They are workflow / DX issues worth capturing so future sessions do not lose time on them again.

Full bring-up procedure now lives in [`docs/hil_bringup_checklist.md`](docs/hil_bringup_checklist.md). Highlights:

### Three-gate model (not previously written down explicitly)

SEA-Stack reaching the Chrono body (SIL) or the motor (HIL) passes through three gates, only the last of which is the runtime engage service:

| # | Gate | Type | Controls |
|---|---|---|---|
| 1 | `CHRONO_FLAP_USE_SEASTACK` | Build-time | Whether SEA-Stack is compiled in at all |
| 2 | `seastack_h5_path != ""` | Launch-time | Whether the adapter and wave field are constructed |
| 3 | `~/enable_hydro` | Runtime | Whether `τ_hydro` is published as non-zero |

HIL adds a fourth runtime gate, `~/engage_hil`, gating the mixer's load input independently.

Practical implication: `hydro_torque_raw = 0.0` after launch can mean any of three different problems. Check the `chrono_flap_node` startup banner (`SEA-Stack hydro ENABLED / DISABLED / FAILED / built WITHOUT SEA-Stack`) to identify which gate is closed.

### `ros2 service call` from the launching terminal hangs

Single-threaded executor + VSG render + 1 kHz solver + 8 publishers can starve the service callback queue. Calling `/chrono_flap_node/enable_hydro` from the same shell that started the launch can sit indefinitely at "waiting for service to become available...". Calling from a separate terminal completes in milliseconds.

Fix (not in this PR): switch to `MultiThreadedExecutor` with a dedicated service callback group. ~15 LOC.

### Frozen-at-construction parameters look mutable but are not

`wave_hs_m`, `wave_tp_s`, `wave_seed`, `wave_n_components`, `hydro_torque_clip_nm`, `seastack_h5_path` all show up in `rqt_reconfigure` and accept `ros2 param set` writes (the parameter value updates), but the wave field and adapter are constructed once and never rebuilt. To change any of these, kill the launch and relaunch.

Documentation discrepancy worth fixing: `docs/sea-stack-integration.md` §8 lists `hydro_torque_clip_nm` as runtime-mutable, but the current implementation freezes it in the adapter constructor. Either the doc or the code needs to change. Not in scope for this PR.

### `hydro_torque_raw = 0.0` with adapter loaded is not necessarily broken

If the body is stationary AND `wave_hs_m = 0.0`, all four hydro components genuinely evaluate to ~0. Easiest fix: set `amplitude_rad_s = 0.1`, `omega_rad_s = 1.0` on `/velocity_pid_node` via `rqt_reconfigure` to start the body moving. Radiation + hydrostatic terms become non-zero immediately. For an obvious wave-driven signal, relaunch with `wave_hs_m:=0.05`.

### Suggested follow-up PRs from this session

These augment the existing "Optional consolidation PRs" list above and are similarly low priority:

- `MultiThreadedExecutor` + service callback group in `chrono_flap_node` (~15 LOC). Fixes the service-hang from the launching terminal.
- Make wave parameters live-reconfigurable by rebuilding the wave field inside an `on_apply_parameters` branch (~50 LOC). Enables wave-sweep experiments without relaunching.
- Reconcile `hydro_torque_clip_nm` mutability between code and `sea-stack-integration.md` §8 (either implement the runtime swap or update the doc — pick one).
- `~/hydro_status` topic publishing a single-byte enum (`DISABLED_NO_H5` / `DISABLED_BUILD` / `DISABLED_GATE` / `ENGAGED`) so the next `hydro_torque_raw = 0` debugging session takes one `ros2 topic echo` instead of digging through startup logs.

---

*Appended 2026-06-16 (Denver / MDT). See [`docs/hil_bringup_checklist.md`](docs/hil_bringup_checklist.md) for the full HIL bring-up procedure.*
