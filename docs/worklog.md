# Integration worklog

Running log of the moving parts across **Chrono**, **SEA-Stack** and **ROS 2**.
Newest entry first. Append an entry whenever a dependency, branch or
environment assumption changes — this file is the only place where the
three-way coordination is written down.

---

## Current state (2026-09-16)

### Priority

ROS 2 is the priority stream. Chrono and SEA-Stack are treated as **pinned
local dependencies**, not active development targets, until SEA-Stack's next
tagged release lands.

### Local tree layout

| Tree | Role | Notes |
|---|---|---|
| `~/project-chrono` | latest upstream Chrono, tracked for its own sake | no `install/`, no `data/vsg` — not currently built |
| `~/project-chrono_sh` | actuator feature development | has `install/`; version not yet identified |
| `~/project-chrono_v10` | **pinned Chrono 10.0.0** — what this repo builds against | has `install/`; the default `CHRONO_ROOT` |
| `~/SEA-Stack` | SEA-Stack | on branch `salhus/linux-support-fixes` — see below |
| `~/Packages/vsg` | shared VSG install | genuinely shared across Chrono builds |

Naming convention: `_v<N>` = pinned by version, `_sh` = pinned by purpose
(personal dev), bare name = moving upstream. A `SEA-Stack_sh` may follow.

> None of the Chrono trees are git repositories, so there is no `git log` to
> fall back on for provenance. Consider a one-line `PROVENANCE` file in each
> recording source tarball/tag, date and purpose.

### SEA-Stack is pinned to a branch, not a release

`~/SEA-Stack` is on `salhus/linux-support-fixes` at **`a71f658`** (pushed to
origin). This is **8 commits ahead of `origin/main`** (`c02556b`, tag
`v1.0.0-beta.4`), and the working hydro pipeline depends on those commits:

```
a71f658  Merge PR #17 from dav-og/dav/pr7-chrono10-followups
4861a0f  fix(linux): allow HydroIO when HDF5Dir is unset
51505c3  Revert "use opaque water surface to avoid DepthSorted crash"
79aa0c9  fix(chrono10): keep m_script_directory for Chrono 10.0.0 baseline
5c2b908  chore(scripts): mark unix shell scripts as executable
2172d0b  fix(gui/vsg): use opaque water surface to avoid DepthSorted crash
f85e175  fix(5sa/bimodal): add heave/pitch/yaw damping + freq-domain excitation
4b12d7d  fix(export): log joint reactions for all ChLink subclasses
a0d34cc  build(linux): prefer Chrono's bundled yaml-cpp, adapt to Chrono 10 API
```

**Consequence:** a checkout of SEA-Stack `main` will not build against
Chrono 10 and will not produce hydro. Do not `git checkout main` or reset
`~/SEA-Stack` without rebuilding and reinstalling.

**Risk to watch:** losing these fixes does *not* fail the build. This repo's
`find_package(SEAStack QUIET CONFIG)` degrades silently to a no-hydro build,
and `chrono_flap_node` logs an ERROR and continues when hydro init fails. Both
failure modes look like a successful run. See the verification snippet in
`docs/sea-stack-integration.md` §3.

Note `2172d0b` adds the opaque-water-surface VSG fix and `51505c3` reverts it,
so the `DepthSorted` crash may still be live upstream.

### Plan

1. Wait for Dave to merge and stabilise; SEA-Stack cuts a new tagged release.
2. Re-point `~/SEA-Stack` at that tag, rebuild, reinstall, re-verify hydro.
3. Clean up: drop dead trees, identify `project-chrono_sh`'s version, decide
   whether bare `project-chrono` gets populated or removed.
4. Revisit whether the eight fixes are upstream by then; if not, keep the
   branch pin documented here.

### Done

- `scripts/setup_env.sh` refreshed for the Chrono v10 install tree; now exports
  `CHRONO_DATA_DIR`, `VSG_FILE_PATH` and `LD_LIBRARY_PATH`, supports both
  install- and build-tree layouts, and warns on the stray `/usr/local`
  yaml-cpp and missing VSG font.
- `docs/sea-stack-integration.md` updated: full GUI launch sequence,
  `find`-based H5 lookup, `cmake --install` failure modes, build verification,
  expanded troubleshooting.
- Verified end to end: Chrono 10.0.0 + SEA-Stack + ROS 2 Jazzy, hydro active,
  three GUIs (Chrono VSG, RViz2, rqt) running together at RTF ≈ 1.0.
- Deleted `~/SEA-Stack-pr17` (commit `4861a0f` reachable from a pushed branch,
  clean working tree — nothing unique lost).

### Known-good launch

```bash
source /opt/ros/jazzy/setup.bash
source scripts/setup_env.sh
source install/setup.bash

OSWEC_H5=$(find $HOME/SEA-Stack -name "oswec.h5" 2>/dev/null | head -1)

ros2 launch chrono_flap_sim sil_mode.launch.py \
  seastack_h5_path:="$OSWEC_H5" \
  wave_hs_m:=0.1 \
  enable_visualization:=true \
  enable_rqt:=true \
  enable_rviz:=true \
  enable_plotjuggler:=false
```

### Open items

- `velocity_pid_node` runs mostly saturated (`torque=0.400 sat=1`) with the
  full-scale OSWEC H5 on a ~0.2 kg bench flap. Expected given the scale
  mismatch (see `docs/sea-stack-integration.md` §7), but toy-scale BEM
  (Capytaine) or a `hydro_scale_factor` is the real fix. Capytaine is already
  checked out at `~/capytaine`.
- `docs/VSG_SETUP.md` still needs the `VSG_FILE_PATH` correction (must be the
  Chrono data dir, not `data/vsg`).
- `docs/local-chrono-build.md` likely still references the pre-v10 tree.
- SEA-Stack upstream issues worth filing: `cmake --install` yaml-cpp conflict,
  and `GET_RUNTIME_DEPENDENCIES` missing `DIRECTORIES` for Chrono/urdfdom.
- Consider a `require_hydro` parameter on `chrono_flap_node` to make a failed
  hydro init fatal instead of a silent degradation.

---
