#include "chrono_flap_sim/sea_stack_hydro.hpp"

#if defined(CHRONO_FLAP_USE_SEASTACK)

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include <chrono/core/ChVector3.h>
#include <chrono/physics/ChBody.h>
#include <chrono/physics/ChSystemNSC.h>

#include <seastack/adapters/chrono/chrono_coupler.h>
#include <seastack/hydro/hydro_model_builder.h>
#include <seastack/hydro/waves/component_sampler.h>
#include <seastack/hydro/waves/linear_directional_wave_field.h>
#include <seastack/hydro/waves/wave_component.h>
#include <seastack/hydro/waves/wave_base.h>
#include <seastack/hydro_io/h5_reader.h>

namespace chrono_flap_sim {

struct SeaStackHydroAdapter::Impl
{
  std::shared_ptr<::chrono::ChBody> flap_body;
  std::shared_ptr<::chrono::ChBody> base_body;
  std::shared_ptr<seastack::hydro::WaveBase> wave_model;
  std::unique_ptr<seastack::hydro::HydroModel> hydro_model;
  std::unique_ptr<seastack::chrono::ChronoHydroCoupler> coupler;
  double torque_clip_nm{0.2};
};

SeaStackHydroAdapter::SeaStackHydroAdapter(
  ::chrono::ChSystemNSC * system,
  std::shared_ptr<::chrono::ChBody> flap_body,
  std::shared_ptr<::chrono::ChBody> base_body,
  const SeaStackHydroParams & params)
: impl_(std::make_unique<Impl>())
{
  if (system == nullptr) {
    throw std::runtime_error("SEA-Stack hydro init failed: Chrono system pointer is null.");
  }
  if (!flap_body || !base_body) {
    throw std::runtime_error("SEA-Stack hydro init failed: flap/base body pointer is null.");
  }
  if (params.h5_path.empty()) {
    throw std::runtime_error("SEA-Stack hydro init failed: parameter 'seastack_h5_path' is empty.");
  }
  if (!std::filesystem::exists(params.h5_path)) {
    throw std::runtime_error(
      "SEA-Stack hydro init failed: H5 file not found: '" + params.h5_path + "'.");
  }

  impl_->flap_body = std::move(flap_body);
  impl_->base_body = std::move(base_body);
  impl_->torque_clip_nm = params.torque_clip_nm;

  auto hydro_data = seastack::hydro_io::H5FileInfo(params.h5_path, 2).ReadH5Data();

  if (params.wave_hs_m > 0.0) {
    seastack::hydro::SeaStateDefinition sea_state;
    sea_state.type = "irregular";
    sea_state.depth = hydro_data.GetSimulationInfo().water_depth;
    sea_state.g = hydro_data.GetSimulationInfo().g;
    sea_state.n_omega = std::max(1, params.wave_n_components);
    sea_state.seed = params.wave_seed;

    seastack::hydro::SeaStatePartition partition;
    partition.spectrum.type = "jonswap";
    partition.spectrum.Hs = params.wave_hs_m;
    partition.spectrum.Tp = params.wave_tp_s;
    partition.spectrum.gamma = 3.3;
    sea_state.partitions.push_back(partition);

    auto components = seastack::hydro::ComponentSampler::Build(sea_state);
    impl_->wave_model = std::make_shared<seastack::hydro::LinearDirectionalWaveField>(
      std::move(components), sea_state.depth);
  } else {
    impl_->wave_model = std::make_shared<seastack::hydro::NoWave>();
  }

  std::vector<std::shared_ptr<::chrono::ChBody>> bodies{impl_->flap_body, impl_->base_body};

  seastack::hydro::HydroModelBuilder builder;
  builder.FromHydroData(std::move(hydro_data))
    .WithWave(impl_->wave_model)
    .EnableHydrostatics()
    .EnableRadiation()
    .EnableExcitation();

  auto model = builder.Build();
  impl_->hydro_model = std::make_unique<seastack::hydro::HydroModel>(std::move(model));
  impl_->coupler = std::make_unique<seastack::chrono::ChronoHydroCoupler>(
    impl_->hydro_model->GetForces(), bodies);
}

SeaStackHydroAdapter::~SeaStackHydroAdapter() = default;

HydroStepResult SeaStackHydroAdapter::step(double sim_time, double dt)
{
  (void)dt;
  HydroStepResult result;

  if (!impl_ || !impl_->coupler) {
    return result;
  }

  const auto body_forces = impl_->coupler->Evaluate(sim_time);
  if (!body_forces.empty()) {
    result.torque_raw_nm = body_forces[0].moment.y();
  }

  result.torque_clip_nm = std::clamp(
    result.torque_raw_nm,
    -impl_->torque_clip_nm,
    impl_->torque_clip_nm);
  result.was_clipped = (result.torque_raw_nm != result.torque_clip_nm);

  impl_->flap_body->Accumulate_torque(
    ::chrono::ChVector3d(0.0, result.torque_clip_nm, 0.0),
    false);

  if (impl_->wave_model) {
    const auto p = impl_->flap_body->GetPos();
    result.wave_eta_m = impl_->wave_model->GetElevation(
      Eigen::Vector3d(p.x(), p.y(), p.z()), sim_time);
  }

  return result;
}

void SeaStackHydroAdapter::set_torque_clip(double clip_nm)
{
  if (!impl_) {
    return;
  }
  impl_->torque_clip_nm = std::max(1e-9, clip_nm);
}

}  // namespace chrono_flap_sim

#endif  // defined(CHRONO_FLAP_USE_SEASTACK)
