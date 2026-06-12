# SEA-Stack integration in `chrono_flap_sim`

## 1) What this enables

This integration wires the full hydrodynamics pipeline:

**SEA-Stack hydrodynamic forces (optional waves) → Chrono multibody flap dynamics → ROS 2 topics → ros2_control/HIL torque path**.

It works in both SIL and HIL modes, with runtime engage gating and safety clipping.

## 2) Prerequisites

- SEA-Stack built locally (or installed) with exported CMake config.
- `SEAStack_DIR` set to the SEA-Stack build/install tree that contains `SEAStackConfig.cmake`.
- An OSWEC-compatible BEM `.h5` file.
- Recommended validation first: run SEA-Stack standalone OSWEC demo (`demos/oswec/demo_oswec_irreg_waves.cpp`) in SEA-Stack.

## 3) Build

`chrono_flap_sim/CMakeLists.txt` now uses:

```cmake
find_package(SEAStack QUIET CONFIG)
```

If found, build output prints that SEA-Stack was detected and compiles with `CHRONO_FLAP_USE_SEASTACK`. If not found, the package still compiles without SEA-Stack code paths.

For local Chrono workflow details, see [`docs/local-chrono-build.md`](docs/local-chrono-build.md).

## 4) Launching with SEA-Stack

`seastack_h5_path` defaults to empty (`""`), so SEA-Stack is **opt-in**.

### SIL

```bash
ros2 launch chrono_flap_sim sil_mode.launch.py \
  enable_visualization:=true \
  seastack_h5_path:=$HOME/SEA-Stack/build/data/demos/oswec/hydroData/oswec.h5
```

Optional calm/irregular toggles:

```bash
ros2 launch chrono_flap_sim sil_mode.launch.py \
  seastack_h5_path:=$HOME/SEA-Stack/build/data/demos/oswec/hydroData/oswec.h5 \
  wave_hs_m:=0.0 \
  hydro_torque_clip_nm:=0.2
```

### HIL

```bash
ros2 launch hil_odrive_ros2_control hil_mode.launch.py
```

Then pass SEA-Stack params directly to the node when launching (or via launch command overrides) using ROS args:

```bash
ros2 run chrono_flap_sim chrono_flap_node --ros-args \
  -p mode:=hil \
  -p seastack_h5_path:=$HOME/SEA-Stack/build/data/demos/oswec/hydroData/oswec.h5 \
  -p wave_hs_m:=0.0 \
  -p hydro_torque_clip_nm:=0.2
```

## 5) Engage gate and safety

SEA-Stack torque is gated by `~/enable_hydro`:

```bash
ros2 service call /chrono_flap_node/enable_hydro std_srvs/srv/SetBool "{data: true}"
```

Safety design:

1. Start disengaged by default.
2. Inspect raw hydrodynamic output first (`~/hydro_torque_raw`).
3. Engage when values look sane.
4. Hard-clip torque at `hydro_torque_clip_nm` before applying to Chrono body.

This is separate from the existing HIL engage and clamp path (defense in depth).

## 6) Telemetry topics

| Topic | Type | Publish behavior | Interpretation |
|---|---|---|---|
| `~/hydro_torque_raw` | `std_msgs/msg/Float64` | Every tick | SEA-Stack-requested Y-axis moment before clipping |
| `~/hydro_torque` | `std_msgs/msg/Float64` | Every tick | Applied Y-axis moment after clip |
| `~/wave_elevation` | `std_msgs/msg/Float64` | Every tick | Wave elevation at flap origin (or 0 for calm/no-wave) |
| `~/hydro_clip_engaged_pct` | `std_msgs/msg/Float64` | Every tick | Percent of last 100 ticks where clip activated |

## 7) Deliberate scale mismatch

The provided OSWEC `.h5` corresponds to a full-scale (~13-ton-class) OSWEC model, while this bench flap is ~0.21 kg acrylic. Raw SEA-Stack torques are therefore intentionally treated as structurally mismatched for direct bench application.

The hard clip is required for safe bench operation. A future follow-up is toy-scale BEM generation (e.g., Capytaine-based) so clipping can be reduced while preserving physical similarity.

## 8) Parameter reference

| Parameter | Type | Default | Mutable at runtime | Notes |
|---|---|---:|---|---|
| `seastack_h5_path` | string | `""` | No | Empty disables SEA-Stack |
| `hydro_torque_clip_nm` | double | `0.2` | Yes | Must be `> 0`; forwarded to adapter |
| `wave_hs_m` | double | `0.0` | No | Calm sea default |
| `wave_tp_s` | double | `4.0` | No | JONSWAP peak period |
| `wave_seed` | int | `42` | No | Random phase seed |
| `wave_n_components` | int | `50` | No | Spectral discretization |
| `hydro_engaged_default` | bool | `false` | No | Initial state for `~/enable_hydro` gate |

## 9) Troubleshooting matrix

| Symptom | Likely cause | Action |
|---|---|---|
| SEA-Stack init says H5 not found | Bad/missing `seastack_h5_path` | Use absolute file path and verify file exists |
| `~/hydro_clip_engaged_pct` ~100% constantly | Clip too low for selected H5/waves | Lower waves (`wave_hs_m`) or increase clip cautiously |
| Hydro torque sign seems inverted | Body orientation/pivot-sign convention mismatch | Verify flap joint axis (`+Y`) and compare against known displacement tests |
| Build says SEA-Stack not found | `SEAStack_DIR` not exported to tree with `SEAStackConfig.cmake` | Export `SEAStack_DIR` and rebuild |

## 10) Pipeline verification recipe

1. **Baseline without hydro**: launch SIL with empty `seastack_h5_path`; verify normal sim topics.
2. **Enable plumbing only**: launch SIL with `.h5`; keep hydro disengaged; verify non-zero `~/hydro_torque_raw` appears.
3. **Engage hydro**: call `~/enable_hydro true`; verify flap response tracks `~/hydro_torque` (clipped value).
4. **Enable waves**: relaunch with `wave_hs_m:=0.05`; verify `~/wave_elevation` oscillates.

