#pragma once

#include "gnc/contracts/sample_context.hpp"
#include "gnc/foundation/linear_algebra.hpp"
#include "gnc/foundation/numerical_outcome.hpp"
#include "gnc/foundation/numerical_policy.hpp"
#include "gnc/foundation/passive_quaternion.hpp"
#include "gnc/foundation/trilinear_table.hpp"
#include "gnc/model_sdk/algorithm_evaluation.hpp"
#include "gnc/model_sdk/in_process_codec.hpp"
#include "gnc/model_sdk/model_metadata.hpp"
#include "gnc/model_sdk/runtime_cell_factory.hpp"
#include "gnc/model_sdk/static_descriptor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gnc::packages::yyz {

inline constexpr std::string_view kRigidStepContractIdentity =
    "gnc.package.yyz.rigid-step.contract.experimental@1";
inline constexpr std::string_view kYyzRigidStepPackageIdentity =
    "gnc.package.yyz-rigid-step.experimental@1";
inline constexpr std::string_view kYyzRigidStepPackageVersion = "0.1.0";
inline constexpr std::string_view kRigidStepModelIdentity =
    "gnc.package.yyz.rigid-step.frozen-interval.experimental@1";
inline constexpr std::string_view kRigidStepModelVersion = "0.1.0";
inline constexpr std::string_view kRigidStepConfigSchemaIdentity =
    "gnc.package.yyz.rigid-step.frozen-interval.config@1";
inline constexpr std::uint32_t kRigidStepConfigSchemaVersion = 1U;
inline constexpr std::string_view kRigidStepRecipeIdentity =
    "gnc.package.yyz.rigid-body-6dof.recipe@1";
inline constexpr std::string_view kRigidStateSchemaIdentity =
    "gnc.state-schema.yyz.rigid-body-6dof@1";
inline constexpr std::string_view kRigidStateLayoutIdentity =
    "gnc.package.yyz.rigid-body-6dof.state-layout@1";
inline constexpr std::string_view kRigidInitialStateInputSchemaIdentity =
    "gnc.package.yyz.rigid-body-6dof.initial-state-input@1";
inline constexpr std::uint32_t kRigidInitialStateInputSchemaVersion = 1U;
inline constexpr std::string_view kRigidObservationContractIdentity =
    "gnc.contract.yyz.committed-rigid-observation@1";
inline constexpr std::string_view
    kRigidPublishProjectionInputContractIdentity =
        "gnc.contract.yyz.rigid-publish-projection-input@1";
inline constexpr std::string_view
    kControlledRigidBoundaryInputContractIdentity =
        "gnc.contract.yyz.controlled-rigid-boundary-input@1";
inline constexpr std::string_view kRigidDerivativeInputContractIdentity =
    "gnc.contract.yyz.rigid-derivative-input@1";
inline constexpr std::string_view kRigidDerivativeOutputContractIdentity =
    "gnc.contract.yyz.rigid-derivative-output@1";
inline constexpr std::string_view kEnvironmentQueryContractIdentity =
    "gnc.contract.yyz.environment-query@1";
inline constexpr std::string_view kEnvironmentSampleContractIdentity =
    "gnc.contract.yyz.environment-sample@1";
inline constexpr std::string_view kMassPropertiesContractIdentity =
    "gnc.contract.yyz.mass-properties@1";
inline constexpr std::string_view kAppliedBodyWrenchContractIdentity =
    "gnc.contract.yyz.applied-body-wrench-interval@1";
inline constexpr std::string_view kForceMomentClosureInputContractIdentity =
    "gnc.contract.yyz.force-moment-closure-input@1";
inline constexpr std::string_view
    kAerodynamicOperatingPointContractIdentity =
        "gnc.contract.yyz.aerodynamic-operating-point@1";
inline constexpr std::string_view kForceMomentClosureModelIdentity =
    "gnc.package.yyz.force-moment-closure.frozen-interval.experimental@1";
inline constexpr std::string_view kForceMomentClosureModelVersion = "0.1.0";
inline constexpr std::string_view kAerodynamicTableModelIdentity =
    "gnc.package.yyz.aerodynamic-table.multiaffine.experimental@1";
inline constexpr std::string_view kAerodynamicTableModelVersion = "0.1.0";
inline constexpr std::string_view kForceMomentClosureConfigSchemaIdentity =
    "gnc.package.yyz.force-moment-closure.config@1";
inline constexpr std::uint32_t kForceMomentClosureConfigSchemaVersion = 1U;
inline constexpr std::string_view kAerodynamicTableConfigSchemaIdentity =
    "gnc.package.yyz.aerodynamic-table.config@1";
inline constexpr std::uint32_t kAerodynamicTableConfigSchemaVersion = 1U;
inline constexpr std::string_view kAerodynamicTableAssetSchemaIdentity =
    "gnc.asset.yyz.aerodynamic-table.multiaffine@1";
inline constexpr std::string_view kAerodynamicCoefficientsContractIdentity =
    "gnc.contract.yyz.aerodynamic-coefficients@1";
inline constexpr std::string_view kRigidFormInputContractIdentity =
    "gnc.contract.yyz.rigid-form-input@1";
inline constexpr std::string_view kRigidObservationLayoutIdentity =
    "gnc.layout.yyz.committed-rigid-observation@1";
inline constexpr std::string_view kRigidFormInputLayoutIdentity =
    "gnc.layout.yyz.rigid-form-input@1";
inline constexpr std::string_view kYyzNoWorkspaceResourcePlanIdentity =
    "gnc.resource-plan.yyz.no-workspace@1";
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidStepPreparationIdentity{
        "gnc.package.yyz.rigid-step.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kForceMomentClosurePreparationIdentity{
        "gnc.package.yyz.force-moment-closure.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kForceMomentClosureKernelIdentity{
        "gnc.package.yyz.force-moment-closure.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kAerodynamicTablePreparationIdentity{
        "gnc.package.yyz.aerodynamic-table.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kAerodynamicTableQueryIdentity{
        "gnc.package.yyz.aerodynamic-table.query@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity kRigidStepKernelIdentity{
    "gnc.package.yyz.rigid-step.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidInitialStateBuilderIdentity{
        "gnc.package.yyz.rigid-body-6dof.initial-state@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidPublishProjectionIdentity{
        "gnc.package.yyz.rigid-body-6dof.committed-observation@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidFrozenFormKernelIdentity{
        "gnc.package.yyz.rigid-body-6dof.frozen-form@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidDerivativeKernelIdentity{
        "gnc.package.yyz.rigid-body-6dof.derivative@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity kRigidStateCodecIdentity{
    "gnc.package.yyz.rigid-body-6dof.state-codec@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidObservationSlotCodecIdentity{
        "gnc.package.yyz.committed-rigid-observation.slot-codec@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kRigidFormInputSlotCodecIdentity{
        "gnc.package.yyz.rigid-form-input.slot-codec@1", "0.1.0"};

inline constexpr std::string_view kUniformEnvironmentModelIdentity =
    "gnc.package.yyz.environment.uniform-supplied.experimental@1";
inline constexpr std::string_view kUniformEnvironmentModelVersion = "0.1.0";
inline constexpr std::string_view kUniformEnvironmentConfigSchemaIdentity =
    "gnc.package.yyz.environment.uniform-supplied.config@1";
inline constexpr std::uint32_t kUniformEnvironmentConfigSchemaVersion = 1U;
inline constexpr gnc::foundation::AlgorithmIdentity
    kUniformEnvironmentPreparationIdentity{
        "gnc.package.yyz.environment.uniform-supplied.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kUniformEnvironmentQueryIdentity{
        "gnc.package.yyz.environment.uniform-supplied.query@1", "0.1.0"};

// Stable package-authored C++ prototype identities. These identify only the
// exact in-process call shapes below; they are not wire or cross-compiler ABI
// claims and contain no RTTI spelling.
inline constexpr std::string_view
    kUniformEnvironmentPreparationCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.uniform-environment.prepare@1";
inline constexpr std::string_view kUniformEnvironmentQueryCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.uniform-environment.query@1";
inline constexpr std::string_view
    kAerodynamicTablePreparationCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.aerodynamic-table.prepare@1";
inline constexpr std::string_view kAerodynamicTableQueryCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.aerodynamic-table.query@1";
inline constexpr std::string_view
    kForceMomentClosurePreparationCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.force-moment-closure.prepare@1";
inline constexpr std::string_view kForceMomentClosureCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.force-moment-closure.evaluate@1";
inline constexpr std::string_view kRigidInitialStateCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid.initial-state@1";
inline constexpr std::string_view kRigidPublishProjectionCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid.publish-projection@1";
inline constexpr std::string_view kRigidDerivativeCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid.derivative@1";
inline constexpr std::string_view kRigidStateCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid.state-codec.getter@1";
inline constexpr std::string_view kRigidObservationSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.committed-rigid-observation.slot-codec.getter@1";
inline constexpr std::string_view kRigidFormInputSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid-form-input.slot-codec.getter@1";

[[nodiscard]] inline gnc::model_sdk::StaticSlotCodecDescriptor
make_yyz_slot_codec_descriptor(
    const gnc::foundation::AlgorithmIdentity& identity,
    std::string_view call_shape_id, std::string_view layout_id,
    std::string_view operation_prefix) {
    return {std::string(layout_id), std::string(identity.id),
            std::string(identity.version), std::string(call_shape_id),
            std::string(operation_prefix) + ".copy@1",
            std::string(operation_prefix) + ".validate@1",
            std::string(operation_prefix) + ".project@1"};
}

struct InertialPositionMeters {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct InertialVelocityMetersPerSecond {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyVelocityMetersPerSecond {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct InertialAccelerationMetersPerSecondSquared {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct PassiveAttitudeIFromB {
    gnc::foundation::QuaternionStorage value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
};

struct BodyAngularRateRadiansPerSecond {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyAngularAccelerationRadiansPerSecondSquared {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyPointMeters {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyForceNewtons {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct InertialForceNewtons {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyMomentNewtonMeters {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct BodyInertiaKilogramMetersSquared {
    gnc::foundation::Mat3 value = gnc::foundation::Mat3::Zero();
};

struct BodyAngularMomentumKilogramMetersSquaredPerSecond {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct PassiveAttitudeDerivativeIFromBPerSecond {
    gnc::foundation::QuaternionStorage value =
        gnc::foundation::quaternion_from_wxyz(0.0, 0.0, 0.0, 0.0);
};

struct RigidState {
    InertialPositionMeters position;
    InertialVelocityMetersPerSecond velocity;
    PassiveAttitudeIFromB attitude;
    BodyAngularRateRadiansPerSecond angular_rate;
};

struct CommittedRigidObservation {
    gnc::contracts::SampleContext context;
    RigidState state;
};

struct RigidStepContext {
    gnc::contracts::FrameIdentity inertial_frame;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    gnc::contracts::SimulationInstant interval_start;
    gnc::contracts::SimulationInstant interval_end;
    std::int64_t configuration_revision = 0;
    gnc::contracts::DataQuality quality =
        gnc::contracts::DataQuality::Invalid;
};

struct EnvironmentInput {
    gnc::contracts::SampleContext context;
    InertialAccelerationMetersPerSecondSquared gravity;
    InertialVelocityMetersPerSecond velocity_airmass;
    double density_kilograms_per_cubic_meter = 0.0;
    double speed_of_sound_meters_per_second = 0.0;
};

struct UniformEnvironmentDefinition {
    gnc::model_sdk::ModelDefinitionMetadata metadata;
    gnc::contracts::FrameIdentity inertial_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = 0;
    InertialAccelerationMetersPerSecondSquared gravity;
    InertialVelocityMetersPerSecond velocity_airmass;
    double density_kilograms_per_cubic_meter = 0.0;
    double speed_of_sound_meters_per_second = 0.0;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_uniform_environment_config(
    const UniformEnvironmentDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    UniformEnvironmentDefinition>
build_uniform_environment_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

class PreparedUniformEnvironmentModel {
  public:
    PreparedUniformEnvironmentModel(
        const PreparedUniformEnvironmentModel&) = default;
    PreparedUniformEnvironmentModel(
        PreparedUniformEnvironmentModel&&) noexcept = default;
    PreparedUniformEnvironmentModel& operator=(
        const PreparedUniformEnvironmentModel&) = default;
    PreparedUniformEnvironmentModel& operator=(
        PreparedUniformEnvironmentModel&&) noexcept = default;

    [[nodiscard]] const UniformEnvironmentDefinition& definition()
        const noexcept;
    [[nodiscard]] const gnc::model_sdk::PreparedModelMetadata& metadata()
        const noexcept;

  private:
    PreparedUniformEnvironmentModel(
        std::shared_ptr<const UniformEnvironmentDefinition> definition,
        gnc::model_sdk::PreparedModelMetadata metadata) noexcept;

    std::shared_ptr<const UniformEnvironmentDefinition> definition_;
    gnc::model_sdk::PreparedModelMetadata metadata_;

    friend gnc::foundation::NumericalOutcome<
        PreparedUniformEnvironmentModel>
    prepare_uniform_environment_model(
        UniformEnvironmentDefinition definition);
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    PreparedUniformEnvironmentModel>
prepare_uniform_environment_model(
    UniformEnvironmentDefinition definition);

struct UniformEnvironmentQueryInput {
    gnc::contracts::SampleContext context;
    InertialPositionMeters position;
};

struct UniformEnvironmentQueryTelemetry {};

using UniformEnvironmentQueryEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<EnvironmentInput,
                                        UniformEnvironmentQueryTelemetry>;

class UniformEnvironmentQueryKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        UniformEnvironmentQueryEvaluation>
    evaluate(const PreparedUniformEnvironmentModel& model,
             const UniformEnvironmentQueryInput& input);
};

struct MassPropertiesInput {
    gnc::contracts::IntervalSampleContext context;
    std::string mass_state_id;
    double mass_kilograms = 0.0;
    BodyPointMeters body_origin_to_center_of_mass;
    BodyInertiaKilogramMetersSquared inertia_about_center_of_mass;
};

// This input is an already evaluated source contribution. The package does
// not model propulsion, actuation, or source state in this slice.
struct AppliedBodyWrenchInput {
    gnc::contracts::IntervalSampleContext context;
    std::string source_id;
    BodyForceNewtons force;
    BodyPointMeters body_origin_to_application;
    BodyMomentNewtonMeters intrinsic_moment_at_application;
};

struct RigidStepAlgorithmDefinition {
    double fixed_step_seconds = 0.0;
    gnc::foundation::NumericalPolicy numerical_policy;
    gnc::foundation::QuaternionPolicy attitude_evaluation_policy;
    gnc::foundation::QuaternionPolicy candidate_attitude_policy;
};

// Runtime-owned configuration required by the rigid form/derivative
// algorithms. Prepared query/closure providers remain separate occurrences
// and are supplied through the invocation set below.
struct RigidFrozenFormRuntimeDefinition {
    gnc::contracts::FrameIdentity inertial_frame;
    RigidStepAlgorithmDefinition algorithm;
};

struct RigidInitialStateInput {
    RigidState state;
};

class RigidInitialStateBuilder {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<RigidState>
    build(const RigidStepAlgorithmDefinition& algorithm,
          const RigidInitialStateInput& input);
};

[[nodiscard]] CommittedRigidObservation project_committed_rigid_observation(
    const gnc::contracts::SampleContext& context,
    const RigidState& state);

struct AerodynamicTableAsset {
    std::string asset_schema_id;
    std::string asset_id;
    std::vector<double> mach_axis;
    std::vector<double> alpha_axis_radians;
    std::vector<double> beta_axis_radians;
    std::vector<std::array<double, 6U>>
        coefficient_rows_ca_cy_cn_cl_cm_cn;
};

struct AerodynamicTableDefinition {
    gnc::model_sdk::ModelDefinitionMetadata metadata;
    std::string source_id;
    std::string configuration_id;
    double reference_area_square_meters = 0.0;
    double reference_span_meters = 0.0;
    double reference_chord_meters = 0.0;
    BodyPointMeters body_origin_to_application;
    std::string table_asset_id;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_aerodynamic_table_config(
    const AerodynamicTableDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    AerodynamicTableDefinition>
build_aerodynamic_table_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration,
    std::string table_asset_id);

class PreparedAerodynamicTableModel {
  public:
    PreparedAerodynamicTableModel(
        const PreparedAerodynamicTableModel&) = default;
    PreparedAerodynamicTableModel(
        PreparedAerodynamicTableModel&&) noexcept = default;
    PreparedAerodynamicTableModel& operator=(
        const PreparedAerodynamicTableModel&) = default;
    PreparedAerodynamicTableModel& operator=(
        PreparedAerodynamicTableModel&&) noexcept = default;

    [[nodiscard]] const AerodynamicTableDefinition& definition()
        const noexcept;
    [[nodiscard]] const AerodynamicTableAsset& asset() const noexcept;
    [[nodiscard]] const gnc::model_sdk::PreparedModelMetadata& metadata()
        const noexcept;

  private:
    PreparedAerodynamicTableModel(
        std::shared_ptr<const AerodynamicTableDefinition> definition,
        std::shared_ptr<const AerodynamicTableAsset> asset,
        gnc::foundation::PreparedTrilinearTableView<6U> table,
        gnc::model_sdk::PreparedModelMetadata metadata) noexcept;

    std::shared_ptr<const AerodynamicTableDefinition> definition_;
    std::shared_ptr<const AerodynamicTableAsset> asset_;
    gnc::foundation::PreparedTrilinearTableView<6U> table_;
    gnc::model_sdk::PreparedModelMetadata metadata_;

    friend gnc::foundation::NumericalOutcome<
        PreparedAerodynamicTableModel>
    prepare_aerodynamic_table_model(
        AerodynamicTableDefinition definition,
        AerodynamicTableAsset asset);
    friend class AerodynamicTableQueryKernel;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    PreparedAerodynamicTableModel>
prepare_aerodynamic_table_model(
    AerodynamicTableDefinition definition,
    AerodynamicTableAsset asset);

struct AerodynamicTableQueryInput {
    double mach = 0.0;
    double alpha_radians = 0.0;
    double beta_radians = 0.0;
};

struct AerodynamicTableQueryOutput {
    std::array<double, 6U> coefficients_ca_cy_cn_cl_cm_cn{};
};

struct AerodynamicTableQueryTelemetry {
    gnc::foundation::InterpolationDomainStatus domain_status =
        gnc::foundation::InterpolationDomainStatus::Inside;
    std::array<double, 3U> weights{};
};

using AerodynamicTableQueryEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<AerodynamicTableQueryOutput,
                                        AerodynamicTableQueryTelemetry>;

class AerodynamicTableQueryKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        AerodynamicTableQueryEvaluation>
    evaluate(const PreparedAerodynamicTableModel& model,
             const AerodynamicTableQueryInput& input);
};

struct ForceMomentClosureDefinition {
    gnc::model_sdk::ModelDefinitionMetadata metadata;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = -1;
    gnc::foundation::NumericalPolicy numerical_policy;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_force_moment_closure_config(
    const ForceMomentClosureDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    ForceMomentClosureDefinition>
build_force_moment_closure_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

class PreparedForceMomentClosureModel {
  public:
    PreparedForceMomentClosureModel(
        const PreparedForceMomentClosureModel&) = default;
    PreparedForceMomentClosureModel(
        PreparedForceMomentClosureModel&&) noexcept = default;
    PreparedForceMomentClosureModel& operator=(
        const PreparedForceMomentClosureModel&) = default;
    PreparedForceMomentClosureModel& operator=(
        PreparedForceMomentClosureModel&&) noexcept = default;

    [[nodiscard]] const ForceMomentClosureDefinition& definition()
        const noexcept;
    [[nodiscard]] const gnc::model_sdk::PreparedModelMetadata& metadata()
        const noexcept;

  private:
    PreparedForceMomentClosureModel(
        std::shared_ptr<const ForceMomentClosureDefinition> definition,
        gnc::model_sdk::PreparedModelMetadata metadata) noexcept;

    std::shared_ptr<const ForceMomentClosureDefinition> definition_;
    gnc::model_sdk::PreparedModelMetadata metadata_;

    friend gnc::foundation::NumericalOutcome<
        PreparedForceMomentClosureModel>
    prepare_force_moment_closure_model(
        ForceMomentClosureDefinition definition);
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    PreparedForceMomentClosureModel>
prepare_force_moment_closure_model(
    ForceMomentClosureDefinition definition);

struct RigidStepModelDefinition {
    gnc::contracts::FrameIdentity inertial_frame;
    ForceMomentClosureDefinition force_moment_closure;
    RigidStepAlgorithmDefinition algorithm;
    AerodynamicTableDefinition aerodynamics;
    AerodynamicTableAsset aerodynamic_table;
};

namespace detail {

[[nodiscard]] inline gnc::model_sdk::StaticPackageDescriptor
describe_yyz_rigid_step_base_package() {
    gnc::model_sdk::StaticModelDescriptor closure;
    closure.definition = {
        std::string(kForceMomentClosureModelIdentity),
        std::string(kForceMomentClosureModelVersion),
        gnc::model_sdk::ModelExecutionForm::Closure};
    closure.placement =
        gnc::model_sdk::ModelPlacement::InteractionClosure;
    closure.preparation_algorithm_id =
        std::string(kForceMomentClosurePreparationIdentity.id);
    closure.preparation_algorithm_version =
        std::string(kForceMomentClosurePreparationIdentity.version);
    closure.preparation_call_shape_id = std::string(
        kForceMomentClosurePreparationCallShapeIdentity);
    closure.configuration.schema_id =
        std::string(kForceMomentClosureConfigSchemaIdentity);
    closure.configuration.schema_version =
        kForceMomentClosureConfigSchemaVersion;
    closure.configuration.fields = {
        {"body_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    closure.closure = gnc::model_sdk::StaticClosureDescriptor{
        std::string(kForceMomentClosureKernelIdentity.id),
        std::string(kForceMomentClosureKernelIdentity.version),
        gnc::contracts::ClosureStrategy::FrozenInterval,
        gnc::model_sdk::StaticWorkspaceRequirement::None,
        std::string(kForceMomentClosureInputContractIdentity),
        std::string(kForceMomentClosureCallShapeIdentity)};
    closure.ports.push_back(
        {"form-input", std::string(kRigidFormInputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::ContinuousClosureLink,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         make_yyz_slot_codec_descriptor(
             kRigidFormInputSlotCodecIdentity,
             kRigidFormInputSlotCodecCallShapeIdentity,
             kRigidFormInputLayoutIdentity,
             "gnc.operation.yyz.rigid-form-input")});

    gnc::model_sdk::StaticModelDescriptor aerodynamics;
    aerodynamics.definition = {
        std::string(kAerodynamicTableModelIdentity),
        std::string(kAerodynamicTableModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    aerodynamics.placement =
        gnc::model_sdk::ModelPlacement::VehicleOutput;
    aerodynamics.preparation_algorithm_id =
        std::string(kAerodynamicTablePreparationIdentity.id);
    aerodynamics.preparation_algorithm_version =
        std::string(kAerodynamicTablePreparationIdentity.version);
    aerodynamics.preparation_call_shape_id = std::string(
        kAerodynamicTablePreparationCallShapeIdentity);
    aerodynamics.configuration.schema_id =
        std::string(kAerodynamicTableConfigSchemaIdentity);
    aerodynamics.configuration.schema_version =
        kAerodynamicTableConfigSchemaVersion;
    aerodynamics.configuration.fields = {
        {"body_origin_to_application.x_m",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"body_origin_to_application.y_m",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"body_origin_to_application.z_m",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"configuration_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"reference_area_square_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"reference_chord_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"reference_span_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"source_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
    };
    aerodynamics.asset_slots.push_back(
        {"aerodynamics",
         std::string(kAerodynamicTableAssetSchemaIdentity),
         gnc::model_sdk::PortCardinality::ExactlyOne});
    aerodynamics.pure_query =
        gnc::model_sdk::StaticPureQueryDescriptor{
            std::string(kAerodynamicTableQueryIdentity.id),
            std::string(kAerodynamicTableQueryIdentity.version),
            gnc::model_sdk::StaticWorkspaceRequirement::None,
            std::string(kAerodynamicOperatingPointContractIdentity),
            std::string(kAerodynamicTableQueryCallShapeIdentity)};
    aerodynamics.ports.push_back(
        {"coefficients",
         std::string(kAerodynamicCoefficientsContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::NotApplicable});

    gnc::model_sdk::StaticModelDescriptor environment;
    environment.definition = {
        std::string(kUniformEnvironmentModelIdentity),
        std::string(kUniformEnvironmentModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    environment.placement = gnc::model_sdk::ModelPlacement::Environment;
    environment.preparation_algorithm_id =
        std::string(kUniformEnvironmentPreparationIdentity.id);
    environment.preparation_algorithm_version =
        std::string(kUniformEnvironmentPreparationIdentity.version);
    environment.preparation_call_shape_id = std::string(
        kUniformEnvironmentPreparationCallShapeIdentity);
    environment.configuration.schema_id =
        std::string(kUniformEnvironmentConfigSchemaIdentity);
    environment.configuration.schema_version =
        kUniformEnvironmentConfigSchemaVersion;
    environment.configuration.fields = {
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"density_kilograms_per_cubic_meter",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"gravity.x_meters_per_second_squared",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"gravity.y_meters_per_second_squared",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"gravity.z_meters_per_second_squared",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"inertial_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"speed_of_sound_meters_per_second",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"velocity_airmass.x_meters_per_second",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"velocity_airmass.y_meters_per_second",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"velocity_airmass.z_meters_per_second",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    environment.pure_query =
        gnc::model_sdk::StaticPureQueryDescriptor{
            std::string(kUniformEnvironmentQueryIdentity.id),
            std::string(kUniformEnvironmentQueryIdentity.version),
            gnc::model_sdk::StaticWorkspaceRequirement::None,
            std::string(kEnvironmentQueryContractIdentity),
            std::string(kUniformEnvironmentQueryCallShapeIdentity)};
    environment.ports.push_back(
        {"environment-sample",
         std::string(kEnvironmentSampleContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::NotApplicable});

    gnc::model_sdk::StaticModelDescriptor rigid;
    rigid.definition = {
        std::string(kRigidStepModelIdentity),
        std::string(kRigidStepModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    rigid.placement = gnc::model_sdk::ModelPlacement::VehicleForm;
    rigid.configuration.schema_id =
        std::string(kRigidStepConfigSchemaIdentity);
    rigid.configuration.schema_version = kRigidStepConfigSchemaVersion;
    rigid.configuration.fields = {
        {"attitude.candidate.normalization",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.candidate.numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.candidate.numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.candidate.numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.candidate.numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.candidate.numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.evaluation.normalization",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.evaluation.numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.evaluation.numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.evaluation.numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.evaluation.numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.evaluation.numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"fixed_step_seconds",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"inertial_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    rigid.ports = {
        {"mass-properties",
         std::string(kMassPropertiesContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel},
        {"supplied-body-wrench",
         std::string(kAppliedBodyWrenchContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel},
        {"environment-sample",
         std::string(kEnvironmentSampleContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::NotApplicable},
        {"aerodynamic-coefficients",
         std::string(kAerodynamicCoefficientsContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::NotApplicable},
        {"form-input",
         std::string(kRigidFormInputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::ContinuousClosureLink,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel},
        {"committed-rigid-observation",
         std::string(kRigidObservationContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         make_yyz_slot_codec_descriptor(
             kRigidObservationSlotCodecIdentity,
             kRigidObservationSlotCodecCallShapeIdentity,
             kRigidObservationLayoutIdentity,
             "gnc.operation.yyz.committed-rigid-observation")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor rigid_runtime;
    rigid_runtime.recipe_id = std::string(kRigidStepRecipeIdentity);
    rigid_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::ContinuousStateOwner;
    rigid_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
        gnc::model_sdk::RuntimeExecutionObligation::DerivativeEvaluation,
    };
    rigid_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
         gnc::model_sdk::CoarsePhase::Publish,
         std::string(kRigidPublishProjectionIdentity.id),
         std::string(kRigidPublishProjectionIdentity.version),
         std::string(kRigidPublishProjectionInputContractIdentity),
         std::string(kRigidObservationContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {}, {"committed-rigid-observation"},
         gnc::model_sdk::StaticStateReadKind::Committed,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kRigidPublishProjectionCallShapeIdentity)},
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Form,
         std::string(kRigidFrozenFormKernelIdentity.id),
         std::string(kRigidFrozenFormKernelIdentity.version),
         std::string(kControlledRigidBoundaryInputContractIdentity),
         std::string(kRigidFormInputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"mass-properties", "supplied-body-wrench",
          "environment-sample", "aerodynamic-coefficients",
          "form-input"}, {},
         gnc::model_sdk::StaticStateReadKind::Committed,
         gnc::model_sdk::StaticStateWriteKind::None,
         {{"environment-query",
           gnc::model_sdk::StaticInvocationKind::PureQuery,
           std::string(kEnvironmentQueryContractIdentity),
           gnc::model_sdk::PortCardinality::ExactlyOne},
          {"aerodynamic-table-query",
           gnc::model_sdk::StaticInvocationKind::PureQuery,
           std::string(kAerodynamicOperatingPointContractIdentity),
           gnc::model_sdk::PortCardinality::ExactlyOne},
          {"force-moment-closure",
           gnc::model_sdk::StaticInvocationKind::Closure,
           std::string(kForceMomentClosureInputContractIdentity),
           gnc::model_sdk::PortCardinality::ExactlyOne}},
         std::string{}},
        {gnc::model_sdk::RuntimeExecutionObligation::DerivativeEvaluation,
         gnc::model_sdk::CoarsePhase::Form,
         std::string(kRigidDerivativeKernelIdentity.id),
         std::string(kRigidDerivativeKernelIdentity.version),
         std::string(kRigidDerivativeInputContractIdentity),
         std::string(kRigidDerivativeOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"mass-properties", "environment-sample", "form-input"}, {},
         gnc::model_sdk::StaticStateReadKind::Candidate,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kRigidDerivativeCallShapeIdentity)},
    };
    rigid_runtime.schedule.trigger =
        gnc::model_sdk::StaticScheduleTrigger::EveryBoundary;
    rigid_runtime.schedule.step_interval = 1U;
    rigid_runtime.schedule.offset = 0U;
    rigid_runtime.schedule.output_hold =
        gnc::model_sdk::HoldPolicy::ZeroOrderHold;
    rigid_runtime.schedule.max_input_age_steps = 0U;
    rigid_runtime.lifecycle_capabilities = {
        gnc::model_sdk::RuntimeLifecycleCapability::Instantiate,
        gnc::model_sdk::RuntimeLifecycleCapability::Dispose};
    rigid_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    rigid_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    rigid_runtime.state_owner =
        gnc::model_sdk::StaticStateOwnerDescriptor{
            {std::string(kRigidStateSchemaIdentity), 1U,
             std::string(kRigidStateLayoutIdentity),
             {{"position", "vec3.float64", "m", "inertial"},
              {"velocity", "vec3.float64", "m/s", "inertial"},
              {"attitude", "quaternion.wxyz.float64", "1",
               "passive-inertial-from-body"},
              {"angular-rate", "vec3.float64", "rad/s", "body"}}},
            std::string(kRigidInitialStateBuilderIdentity.id),
            std::string(kRigidInitialStateBuilderIdentity.version),
            {std::string(kRigidInitialStateInputSchemaIdentity),
             kRigidInitialStateInputSchemaVersion,
             {{"angular_rate.x_radians_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"angular_rate.y_radians_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"angular_rate.z_radians_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"attitude.w",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"attitude.x",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"attitude.y",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"attitude.z",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"position.x_meters",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"position.y_meters",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"position.z_meters",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"velocity.x_meters_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"velocity.y_meters_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64},
              {"velocity.z_meters_per_second",
               gnc::model_sdk::CanonicalConfigValueKind::Float64}}},
            gnc::model_sdk::StaticStateEvolution::ContinuousCandidate,
            std::string(kRigidInitialStateCallShapeIdentity),
            {std::string(kRigidStateCodecIdentity.id),
             std::string(kRigidStateCodecIdentity.version),
             std::string(kRigidStateCodecCallShapeIdentity),
             "gnc.operation.yyz.rigid-state.clone@1",
             "gnc.operation.yyz.rigid-state.validate@1",
             "gnc.operation.yyz.rigid-state.validate-finite@1",
             "gnc.operation.yyz.rigid-state.validate-invariants@1",
             "gnc.operation.yyz.rigid-state.noexcept-swap@1",
             std::string(kRigidPublishProjectionIdentity.id)}};
    rigid.runtime_component = std::move(rigid_runtime);

    gnc::model_sdk::StaticAlgorithmDescriptor rigid_step;
    rigid_step.algorithm_id = std::string(kRigidStepKernelIdentity.id);
    rigid_step.algorithm_version =
        std::string(kRigidStepKernelIdentity.version);
    rigid_step.ports.push_back(
        {"aerodynamic-coefficients",
         std::string(kAerodynamicCoefficientsContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::NotApplicable});
    rigid_step.ports.push_back(
        {"form-input", std::string(kRigidFormInputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::ContinuousClosureLink,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel});

    gnc::model_sdk::StaticPackageDescriptor package;
    package.package_id = std::string(kYyzRigidStepPackageIdentity);
    package.package_version = std::string(kYyzRigidStepPackageVersion);
    package.models.push_back(std::move(closure));
    package.models.push_back(std::move(aerodynamics));
    package.models.push_back(std::move(environment));
    package.models.push_back(std::move(rigid));
    package.algorithms.push_back(std::move(rigid_step));
    return package;
}

} // namespace detail

// The complete package contribution is defined with the adjacent R1 product
// kernels so callers keep one stable descriptor entry point.
[[nodiscard]] gnc::model_sdk::StaticPackageDescriptor
describe_yyz_rigid_step_package();

class PreparedRigidStepModel {
  public:
    PreparedRigidStepModel(const PreparedRigidStepModel&) = default;
    PreparedRigidStepModel(PreparedRigidStepModel&&) noexcept = default;
    PreparedRigidStepModel& operator=(const PreparedRigidStepModel&) = default;
    PreparedRigidStepModel& operator=(PreparedRigidStepModel&&) noexcept =
        default;

    [[nodiscard]] const RigidStepModelDefinition& definition() const noexcept;
    [[nodiscard]] const PreparedForceMomentClosureModel&
    force_moment_closure_model()
        const noexcept;
    [[nodiscard]] const PreparedAerodynamicTableModel&
    aerodynamic_table_model() const noexcept;

  private:
    PreparedRigidStepModel(
        std::shared_ptr<const RigidStepModelDefinition> definition,
        PreparedAerodynamicTableModel aerodynamic_table_model,
        PreparedForceMomentClosureModel force_moment_closure_model) noexcept;

    std::shared_ptr<const RigidStepModelDefinition> definition_;
    PreparedAerodynamicTableModel aerodynamic_table_model_;
    PreparedForceMomentClosureModel force_moment_closure_model_;

    friend gnc::foundation::NumericalOutcome<PreparedRigidStepModel>
    prepare_rigid_step_model(RigidStepModelDefinition definition);
    friend class RigidStepKernel;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<PreparedRigidStepModel>
prepare_rigid_step_model(RigidStepModelDefinition definition);

struct RigidStepInput {
    RigidStepContext context;
    RigidState committed_state;
    EnvironmentInput environment;
    MassPropertiesInput mass_properties;
    AppliedBodyWrenchInput supplied_wrench;
};

struct AirDataOutput {
    InertialVelocityMetersPerSecond velocity_relative_inertial;
    BodyVelocityMetersPerSecond velocity_relative_body;
    double airspeed_meters_per_second = 0.0;
    double alpha_radians = 0.0;
    double beta_radians = 0.0;
    double dynamic_pressure_pascals = 0.0;
    double mach = 0.0;
};

struct AerodynamicLookupOutput {
    gnc::foundation::InterpolationDomainStatus domain_status =
        gnc::foundation::InterpolationDomainStatus::Inside;
    std::array<double, 3U> weights{};
    std::array<double, 6U> coefficients_ca_cy_cn_cl_cm_cn{};
};

struct BodyWrenchContribution {
    std::string source_id;
    BodyForceNewtons force;
    BodyMomentNewtonMeters moment_about_center_of_mass;
};

struct RigidFormInput {
    BodyForceNewtons force_total;
    BodyMomentNewtonMeters moment_total_about_center_of_mass;

    [[nodiscard]] RigidFormInput form_input() const noexcept {
        return *this;
    }
};

struct ForceMomentClosureInput {
    BodyPointMeters body_origin_to_center_of_mass;
    std::vector<AppliedBodyWrenchInput> contributions;
};

using ForceMomentClosureOutput = RigidFormInput;

struct ForceMomentClosureTelemetry {
    std::vector<BodyWrenchContribution> contributions;
};

using ForceMomentClosureEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<ForceMomentClosureOutput,
                                        ForceMomentClosureTelemetry>;

class ForceMomentClosureKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ForceMomentClosureEvaluation>
    evaluate(const PreparedForceMomentClosureModel& model,
             const ForceMomentClosureInput& input);
};

struct RigidFrozenFormOutput {
    RigidFormInput form_input;
};

struct RigidFrozenFormTelemetry {
    AirDataOutput air_data;
    AerodynamicLookupOutput aerodynamic_lookup;
    AerodynamicTableQueryEvaluation aerodynamic_query;
    ForceMomentClosureEvaluation force_moment_closure;
};

using RigidFrozenFormEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<RigidFrozenFormOutput,
                                        RigidFrozenFormTelemetry>;

using UniformEnvironmentQueryEntry =
    decltype(&UniformEnvironmentQueryKernel::evaluate);
using AerodynamicTableQueryEntry =
    decltype(&AerodynamicTableQueryKernel::evaluate);
using ForceMomentClosureEntry =
    decltype(&ForceMomentClosureKernel::evaluate);

using BoundAerodynamicTableQuery = gnc::model_sdk::BoundQueryHandle<
    PreparedAerodynamicTableModel, AerodynamicTableQueryEntry>;
using BoundForceMomentClosure = gnc::model_sdk::BoundClosureHandle<
    PreparedForceMomentClosureModel, ForceMomentClosureEntry>;

// R3 materializes these exact process-local references from the authorized
// invocation entries in ExecutionPlanImage. They are never serialized or
// included in a stable fingerprint.
struct RigidFrozenFormInvocationSet {
    BoundAerodynamicTableQuery aerodynamic;
    BoundForceMomentClosure closure;
};

struct RigidFrozenFormQuerySet {
    BoundAerodynamicTableQuery aerodynamic;
};

// Authoritative caller-local PureQuery response returned by the typed
// package composition. Telemetry is explanatory only.
struct RigidFrozenFormInvocationResults {
    AerodynamicTableQueryOutput aerodynamic_coefficients;
};

struct RigidFrozenFormPreparationOutput {
    EnvironmentInput environment;
    ForceMomentClosureInput closure_request;
};

struct RigidFrozenFormPreparationTelemetry {
    AirDataOutput air_data;
    AerodynamicLookupOutput aerodynamic_lookup;
    AerodynamicTableQueryEvaluation aerodynamic_query;
};

using RigidFrozenFormPreparationEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<
        RigidFrozenFormPreparationOutput,
        RigidFrozenFormPreparationTelemetry>;

struct RigidFrozenFormPreparationInvocationEvaluation {
    RigidFrozenFormPreparationEvaluation evaluation;
    RigidFrozenFormInvocationResults invocation_results;
};

struct RigidFrozenFormInvocationEvaluation {
    RigidFrozenFormEvaluation evaluation;
    RigidFrozenFormInvocationResults invocation_results;
};

class RigidFrozenFormKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        RigidFrozenFormPreparationInvocationEvaluation>
    prepare_closure_request(
        const RigidFrozenFormRuntimeDefinition& definition,
        const RigidFrozenFormQuerySet& queries,
        const RigidStepInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        RigidFrozenFormEvaluation>
    evaluate(const PreparedRigidStepModel& model,
             const RigidStepInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        RigidFrozenFormEvaluation>
    evaluate(const RigidFrozenFormRuntimeDefinition& definition,
             const RigidFrozenFormInvocationSet& invocations,
             const RigidStepInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        RigidFrozenFormInvocationEvaluation>
    evaluate_with_invocation_results(
        const RigidFrozenFormRuntimeDefinition& definition,
        const RigidFrozenFormInvocationSet& invocations,
        const RigidStepInput& input);
};

struct RigidDerivativeOutput {
    InertialForceNewtons force_total_inertial;
    InertialAccelerationMetersPerSecondSquared acceleration;
    BodyAngularMomentumKilogramMetersSquaredPerSecond angular_momentum;
    BodyMomentNewtonMeters gyroscopic_moment;
    BodyMomentNewtonMeters net_moment;
    BodyAngularAccelerationRadiansPerSecondSquared angular_acceleration;
    PassiveAttitudeDerivativeIFromBPerSecond attitude_derivative;
};

struct RigidDerivativeInput {
    RigidState candidate_state;
    double mass_kilograms = 0.0;
    BodyInertiaKilogramMetersSquared inertia_about_center_of_mass;
    RigidFormInput frozen_form_input;
    InertialAccelerationMetersPerSecondSquared frozen_gravity;
};

class RigidDerivativeKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        RigidDerivativeOutput>
    evaluate(const RigidStepAlgorithmDefinition& algorithm,
             const RigidDerivativeInput& input);
};

struct RigidStateCandidate {
    gnc::contracts::SimulationInstant effective_at;
    RigidState state;
};

struct RigidStepOutput {
    RigidStateCandidate candidate;
};

struct RigidStepTelemetry {
    AirDataOutput air_data;
    AerodynamicLookupOutput aerodynamic_lookup;
    ForceMomentClosureEvaluation force_moment_closure;
    RigidDerivativeOutput derivative_at_interval_start;
};

using RigidStepEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<RigidStepOutput,
                                        RigidStepTelemetry>;

class RigidStepKernel {
  public:
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<RigidStepEvaluation>
    evaluate(const PreparedRigidStepModel& model,
             const RigidStepInput& input);

    // Compatibility composition hook: the caller must provide the frozen
    // form produced for this exact interval input. Runtime state ownership and
    // commit remain outside this pure package kernel.
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<RigidStepEvaluation>
    evaluate_held_form(
        const PreparedRigidStepModel& model,
        const RigidStepInput& input,
        const RigidFrozenFormEvaluation& frozen_form,
        gnc::foundation::NumericalStatus frozen_form_status,
        const gnc::foundation::NumericalEvidence& frozen_form_evidence);
};

// Independent product contracts for the exact callable pointers contributed
// by describe_yyz_rigid_step_implementation(). The implementation factory
// statically compares each real function address against these aliases.
using UniformEnvironmentPreparationCall =
    gnc::foundation::NumericalOutcome<PreparedUniformEnvironmentModel> (*)(
        UniformEnvironmentDefinition);
using UniformEnvironmentQueryCall =
    gnc::foundation::NumericalOutcome<UniformEnvironmentQueryEvaluation> (*)(
        const PreparedUniformEnvironmentModel&,
        const UniformEnvironmentQueryInput&);
using AerodynamicTablePreparationCall =
    gnc::foundation::NumericalOutcome<PreparedAerodynamicTableModel> (*)(
        AerodynamicTableDefinition, AerodynamicTableAsset);
using AerodynamicTableQueryCall =
    gnc::foundation::NumericalOutcome<AerodynamicTableQueryEvaluation> (*)(
        const PreparedAerodynamicTableModel&,
        const AerodynamicTableQueryInput&);
using ForceMomentClosurePreparationCall =
    gnc::foundation::NumericalOutcome<PreparedForceMomentClosureModel> (*)(
        ForceMomentClosureDefinition);
using ForceMomentClosureCall =
    gnc::foundation::NumericalOutcome<ForceMomentClosureEvaluation> (*)(
        const PreparedForceMomentClosureModel&,
        const ForceMomentClosureInput&);
using RigidInitialStateCall =
    gnc::foundation::NumericalOutcome<RigidState> (*)(
        const RigidStepAlgorithmDefinition&, const RigidInitialStateInput&);
using RigidPublishProjectionCall = CommittedRigidObservation (*)(
    const gnc::contracts::SampleContext&, const RigidState&);
using RigidDerivativeCall =
    gnc::foundation::NumericalOutcome<RigidDerivativeOutput> (*)(
        const RigidStepAlgorithmDefinition&, const RigidDerivativeInput&);

using RigidStateCloneCall = RigidState (*)(const RigidState&);
using RigidStateValidateCall = bool (*)(const RigidState&) noexcept;
using RigidStateSwapCall = void (*)(RigidState&, RigidState&) noexcept;
using RigidStateCodec = gnc::model_sdk::InProcessStateCodec<
    RigidStateCloneCall, RigidStateValidateCall,
    RigidStateValidateCall, RigidStateValidateCall,
    RigidStateSwapCall, RigidPublishProjectionCall>;
using RigidStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<RigidStateCodec>;

[[nodiscard]] RigidState clone_rigid_state(const RigidState& state);
[[nodiscard]] bool validate_rigid_state(const RigidState& state) noexcept;
[[nodiscard]] bool validate_rigid_state_finite(
    const RigidState& state) noexcept;
[[nodiscard]] bool validate_rigid_state_invariants(
    const RigidState& state) noexcept;
void swap_rigid_state(RigidState& lhs, RigidState& rhs) noexcept;
[[nodiscard]] const RigidStateCodec& rigid_state_codec() noexcept;

using RigidObservationSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<CommittedRigidObservation>;
using RigidObservationSlotCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<RigidObservationSlotCodec>;
using RigidFormInputSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<RigidFormInput>;
using RigidFormInputSlotCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<RigidFormInputSlotCodec>;

} // namespace gnc::packages::yyz
