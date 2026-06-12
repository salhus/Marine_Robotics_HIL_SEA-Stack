// sea_stack_hydro.hpp
//
// SeaStackHydroAdapter — thin PIMPL wrapper around SEA-Stack HydroSystem
// for the chrono_flap_sim node. Computes 6-DOF hydrodynamic wrench on the flap
// body each tick, projects onto the pivot axis (Y), clips to a configured limit,
// applies to the flap body, and returns telemetry for ROS publication.
//
// This header has zero SEA-Stack type dependencies — implementation hides them all
// in src/sea_stack_hydro.cpp behind an Impl struct. The chrono_flap_node TU
// stays clean and the file compiles without seastack headers in CMake search path.

#pragma once

#include <memory>
#include <string>

namespace chrono {
class ChSystemNSC;
class ChBody;
}  // namespace chrono

namespace chrono_flap_sim {

struct SeaStackHydroParams
{
  std::string h5_path;             // REQUIRED — path to OSWEC-style BEM .h5
  double wave_hs_m         = 0.0;  // JONSWAP significant wave height (0 = calm)
  double wave_tp_s         = 4.0;  // JONSWAP peak period
  int    wave_seed         = 42;
  int    wave_n_components = 50;
  double torque_clip_nm    = 0.2;  // Hard clip post-SEA-Stack
};

struct HydroStepResult
{
  double torque_raw_nm  = 0.0;  // What SEA-Stack wanted to apply (pre-clip)
  double torque_clip_nm = 0.0;  // What was actually applied (post-clip)
  double wave_eta_m     = 0.0;  // Wave elevation at flap origin
  bool   was_clipped    = false;
};

class SeaStackHydroAdapter
{
public:
  // Constructor builds SEA-Stack HydroSystem from the .h5 file, configures
  // JONSWAP waves (or zero-amplitude calm sea if wave_hs_m == 0.0), and binds
  // to the supplied flap and base Chrono bodies. May throw on missing h5 file
  // or initialization failure.
  SeaStackHydroAdapter(
    ::chrono::ChSystemNSC * system,
    std::shared_ptr<::chrono::ChBody> flap_body,
    std::shared_ptr<::chrono::ChBody> base_body,
    const SeaStackHydroParams & params);

  ~SeaStackHydroAdapter();

  // Non-copyable, non-movable (owns SEA-Stack handles).
  SeaStackHydroAdapter(const SeaStackHydroAdapter &) = delete;
  SeaStackHydroAdapter & operator=(const SeaStackHydroAdapter &) = delete;

  // Compute hydrodynamic wrench, project onto pivot Y-axis, clip, apply to flap
  // body via Accumulate_torque, and return telemetry. dt is the publish_dt of
  // the outer loop (the SEA-Stack call is per-publish, not per-sub-step).
  HydroStepResult step(double sim_time, double dt);

  // Runtime-reconfigurable clip (for rqt_reconfigure).
  void set_torque_clip(double clip_nm);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chrono_flap_sim
