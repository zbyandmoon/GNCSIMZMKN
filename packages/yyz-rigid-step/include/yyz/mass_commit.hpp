#pragma once

#include <yyz/rigid_step.hpp>

#include <gnc/model_sdk/static_implementation.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace gnc::packages::yyz {

[[nodiscard]] gnc::model_sdk::StaticPackageImplementation
describe_yyz_rigid_step_implementation(std::string build_fingerprint);

inline constexpr std::string_view kScalarBurnMassContractIdentity =
    "gnc.package.yyz.mass.scalar-burn.contract.experimental@1";
inline constexpr std::string_view kScalarBurnMassModelIdentity =
    "gnc.package.yyz.mass.scalar-burn-constant-geometry.experimental@1";
inline constexpr std::string_view kScalarBurnMassModelVersion = "0.1.0";
inline constexpr std::string_view kScalarBurnMassConfigSchemaIdentity =
    "gnc.package.yyz.mass.scalar-burn-constant-geometry.config@1";
inline constexpr std::uint32_t kScalarBurnMassConfigSchemaVersion = 1U;
inline constexpr std::string_view kScalarBurnMassRecipeIdentity =
    "gnc.package.yyz.mass.scalar-burn-constant-geometry.recipe@1";
inline constexpr std::string_view kMassStateSchemaIdentity =
    "gnc.state-schema.yyz.scalar-burn-mass@1";
inline constexpr std::string_view kMassStateLayoutIdentity =
    "gnc.package.yyz.scalar-burn-mass.state-layout@1";
inline constexpr std::string_view kMassInitialStateInputSchemaIdentity =
    "gnc.package.yyz.scalar-burn-mass.initial-state-input@1";
inline constexpr std::uint32_t kMassInitialStateInputSchemaVersion = 1U;
inline constexpr std::string_view kMassFlowIntervalContractIdentity =
    "gnc.contract.yyz.mass-flow-interval@1";
inline constexpr std::string_view
    kMassPublishProjectionInputContractIdentity =
        "gnc.contract.yyz.mass-publish-projection-input@1";
inline constexpr std::string_view kScalarBurnMassOutputContractIdentity =
    "gnc.contract.yyz.scalar-burn-mass-output@1";
inline constexpr std::string_view kTwoIntervalMassCommitContractIdentity =
    "gnc.package.yyz.rigid-mass-boundary.contract.experimental@1";
inline constexpr std::string_view kTwoIntervalMassCommitModelIdentity =
    "gnc.package.yyz.two-interval-mass-commit.experimental@1";
inline constexpr std::string_view kSuppliedPropulsionContractIdentity =
    "gnc.package.yyz.propulsion-response.contract.experimental@1";
inline constexpr std::string_view kSuppliedPropulsionModelIdentity =
    "gnc.package.yyz.propulsion-response.supplied.experimental@1";
inline constexpr std::string_view kSuppliedPropulsionModelVersion = "0.1.0";
inline constexpr std::string_view kSuppliedPropulsionConfigSchemaIdentity =
    "gnc.package.yyz.propulsion-response.supplied.config@1";
inline constexpr std::uint32_t kSuppliedPropulsionConfigSchemaVersion = 1U;
inline constexpr std::string_view kSuppliedPropulsionRecipeIdentity =
    "gnc.package.yyz.propulsion-response.supplied.recipe@1";
inline constexpr std::string_view
    kFixedSuppliedPropulsionRequestContractIdentity =
        "gnc.contract.yyz.fixed-supplied-propulsion-boundary-request@1";
inline constexpr std::string_view
    kSuppliedPropulsionBodyWrenchContractIdentity =
        "gnc.contract.yyz.supplied-propulsion-body-wrench@1";
inline constexpr std::string_view kSuppliedPropulsionOutputContractIdentity =
    "gnc.contract.yyz.supplied-propulsion-output@1";
inline constexpr std::string_view kAltitudePitchGuidanceModelIdentity =
    "gnc.package.yyz.guidance.altitude-pitch.experimental@1";
inline constexpr std::string_view kAltitudePitchGuidanceModelVersion =
    "0.1.0";
inline constexpr std::string_view
    kAltitudePitchGuidanceConfigSchemaIdentity =
        "gnc.package.yyz.guidance.altitude-pitch.config@1";
inline constexpr std::uint32_t
    kAltitudePitchGuidanceConfigSchemaVersion = 1U;
inline constexpr std::string_view
    kAltitudePitchGuidanceRecipeIdentity =
        "gnc.package.yyz.guidance.altitude-pitch.recipe@1";
inline constexpr std::string_view
    kCommittedRigidObservationContractIdentity =
        kRigidObservationContractIdentity;
inline constexpr std::string_view
    kAltitudePitchGuidanceOutputContractIdentity =
        "gnc.contract.yyz.altitude-pitch-guidance-output@1";
inline constexpr std::string_view kPitchMomentControllerModelIdentity =
    "gnc.package.yyz.controller.pitch-moment.experimental@1";
inline constexpr std::string_view kPitchMomentControllerModelVersion =
    "0.1.0";
inline constexpr std::string_view
    kPitchMomentControllerConfigSchemaIdentity =
        "gnc.package.yyz.controller.pitch-moment.config@1";
inline constexpr std::uint32_t
    kPitchMomentControllerConfigSchemaVersion = 1U;
inline constexpr std::string_view kPitchMomentControllerRecipeIdentity =
    "gnc.package.yyz.controller.pitch-moment.recipe@1";
inline constexpr std::string_view
    kPitchMomentControllerOutputContractIdentity =
        "gnc.contract.yyz.pitch-moment-controller-output@1";
inline constexpr std::string_view kIdealBodyMomentActuatorModelIdentity =
    "gnc.package.yyz.actuator.ideal-body-moment.experimental@1";
inline constexpr std::string_view kIdealBodyMomentActuatorModelVersion =
    "0.1.0";
inline constexpr std::string_view
    kIdealBodyMomentActuatorConfigSchemaIdentity =
        "gnc.package.yyz.actuator.ideal-body-moment.config@1";
inline constexpr std::uint32_t
    kIdealBodyMomentActuatorConfigSchemaVersion = 1U;
inline constexpr std::string_view kIdealBodyMomentActuatorRecipeIdentity =
    "gnc.package.yyz.actuator.ideal-body-moment.recipe@1";
inline constexpr std::string_view
    kIdealBodyMomentActuatorOutputContractIdentity =
        "gnc.contract.yyz.ideal-body-moment-actuator-output@1";
inline constexpr std::string_view
    kIdealBodyMomentActuatorRequestContractIdentity =
        "gnc.contract.yyz.ideal-body-moment-actuator-request@1";
inline constexpr std::string_view
    kControlledPropelledRigidMassStepModelIdentity =
        "gnc.package.yyz.controlled-propelled-rigid-mass-step.experimental@1";
inline constexpr std::string_view
    kTwoIntervalControlledPropelledCommitModelIdentity =
        "gnc.package.yyz.two-interval-controlled-propelled-commit.experimental@1";
inline constexpr std::string_view kCommittedMissionResultModelIdentity =
    "gnc.package.yyz.committed-mission-result.experimental@1";
inline constexpr std::string_view kCommittedMissionResultModelVersion =
    "0.1.0";
inline constexpr std::string_view
    kCommittedMissionResultConfigSchemaIdentity =
        "gnc.package.yyz.committed-mission-result.config@1";
inline constexpr std::uint32_t
    kCommittedMissionResultConfigSchemaVersion = 1U;
inline constexpr std::string_view kCommittedMissionResultRecipeIdentity =
    "gnc.package.yyz.committed-mission-result.recipe@1";
inline constexpr std::string_view
    kCommittedRigidMassSequenceContractIdentity =
        "gnc.contract.yyz.committed-rigid-mass-sequence@1";
inline constexpr std::string_view kCommittedMissionResultContractIdentity =
    "gnc.contract.yyz.committed-mission-result@1";
inline constexpr std::string_view
    kControlledRigidBoundaryPreparationContractIdentity =
        "gnc.contract.yyz.controlled-rigid-boundary-preparation@1";
inline constexpr std::string_view kMassPropertiesLayoutIdentity =
    "gnc.layout.yyz.mass-properties@1";
inline constexpr std::string_view kGuidanceOutputLayoutIdentity =
    "gnc.layout.yyz.altitude-pitch-guidance-output@1";
inline constexpr std::string_view kControllerOutputLayoutIdentity =
    "gnc.layout.yyz.pitch-moment-controller-output@1";
inline constexpr std::string_view kActuatorOutputLayoutIdentity =
    "gnc.layout.yyz.ideal-body-moment-actuator-output@1";
inline constexpr std::string_view kPropulsionWrenchLayoutIdentity =
    "gnc.layout.yyz.supplied-propulsion-body-wrench@1";
inline constexpr std::string_view kMassFlowLayoutIdentity =
    "gnc.layout.yyz.mass-flow-interval@1";
inline constexpr std::string_view kMissionResultLayoutIdentity =
    "gnc.layout.yyz.committed-mission-result@1";
inline constexpr std::string_view
    kControlledRigidBoundaryPreparationLayoutIdentity =
        "gnc.layout.yyz.controlled-rigid-boundary-preparation@1";
inline constexpr std::uint32_t kCommittedMissionHistoryDepth = 3U;
inline constexpr std::string_view kCommittedMissionRigidHistoryMemberId =
    "rigid_states";
inline constexpr std::string_view kCommittedMissionMassHistoryMemberId =
    "mass_states";

inline constexpr gnc::foundation::AlgorithmIdentity
    kScalarBurnMassKernelIdentity{
        "gnc.package.yyz.mass.scalar-burn.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kMassInitialStateBuilderIdentity{
        "gnc.package.yyz.mass.scalar-burn.initial-state@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kMassPublishProjectionIdentity{
        "gnc.package.yyz.mass.scalar-burn.committed-properties@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kFrozenRigidMassStepKernelIdentity{
        "gnc.package.yyz.rigid-mass.frozen-step.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kTwoIntervalMassCommitKernelIdentity{
        "gnc.package.yyz.rigid-mass.two-interval.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kSuppliedPropulsionKernelIdentity{
        "gnc.package.yyz.propulsion-response.supplied.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kFixedSuppliedPropulsionBoundaryIdentity{
        "gnc.package.yyz.propulsion-response.fixed-boundary@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kPropelledFrozenRigidMassStepKernelIdentity{
        "gnc.package.yyz.rigid-mass.propelled-frozen-step.kernel@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kAltitudePitchGuidanceKernelIdentity{
        "gnc.package.yyz.guidance.altitude-pitch.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kPitchMomentControllerKernelIdentity{
        "gnc.package.yyz.controller.pitch-moment.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kIdealBodyMomentActuatorKernelIdentity{
        "gnc.package.yyz.actuator.ideal-body-moment.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledPropelledRigidMassStepKernelIdentity{
        "gnc.package.yyz.rigid-mass.controlled-propelled-step.kernel@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kTwoIntervalControlledPropelledCommitKernelIdentity{
        "gnc.package.yyz.rigid-mass.two-interval-controlled-propelled.kernel@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCommittedMissionResultKernelIdentity{
        "gnc.package.yyz.committed-mission-result.kernel@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCommittedMissionHistoryEvaluationIdentity{
        "gnc.package.yyz.committed-mission-result.history-adapter@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledBodyWrenchAdapterIdentity{
        "gnc.package.yyz.controlled-body-wrench.adapter@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledRigidBoundaryEvaluationIdentity{
        "gnc.package.yyz.rigid-body-6dof.controlled-boundary@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledRigidDefinitionBuilderIdentity{
        "gnc.package.yyz.rigid-body-6dof.definition-builder@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kScalarBurnMassDefinitionBuilderIdentity{
        "gnc.package.yyz.mass.scalar-burn.definition-builder@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kAltitudePitchGuidanceDefinitionBuilderIdentity{
        "gnc.package.yyz.guidance.altitude-pitch.definition-builder@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kPitchMomentControllerDefinitionBuilderIdentity{
        "gnc.package.yyz.controller.pitch-moment.definition-builder@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kIdealBodyMomentActuatorDefinitionBuilderIdentity{
        "gnc.package.yyz.actuator.ideal-body-moment.definition-builder@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kFixedSuppliedPropulsionDefinitionBuilderIdentity{
        "gnc.package.yyz.propulsion-response.fixed.definition-builder@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCommittedMissionResultDefinitionBuilderIdentity{
        "gnc.package.yyz.committed-mission-result.definition-builder@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledRigidRuntimeCellFactoryIdentity{
        "gnc.package.yyz.rigid-body-6dof.runtime-cell-factory@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kScalarBurnMassRuntimeCellFactoryIdentity{
        "gnc.package.yyz.mass.scalar-burn.runtime-cell-factory@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kAltitudePitchGuidanceRuntimeCellFactoryIdentity{
        "gnc.package.yyz.guidance.altitude-pitch.runtime-cell-factory@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kPitchMomentControllerRuntimeCellFactoryIdentity{
        "gnc.package.yyz.controller.pitch-moment.runtime-cell-factory@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kIdealBodyMomentActuatorRuntimeCellFactoryIdentity{
        "gnc.package.yyz.actuator.ideal-body-moment.runtime-cell-factory@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kFixedSuppliedPropulsionRuntimeCellFactoryIdentity{
        "gnc.package.yyz.propulsion-response.fixed.runtime-cell-factory@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCommittedMissionResultRuntimeCellFactoryIdentity{
        "gnc.package.yyz.committed-mission-result.runtime-cell-factory@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity kMassStateCodecIdentity{
    "gnc.package.yyz.mass.scalar-burn.state-codec@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kMassPropertiesSlotCodecIdentity{
        "gnc.package.yyz.mass-properties.slot-codec@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kGuidanceOutputSlotCodecIdentity{
        "gnc.package.yyz.altitude-pitch-guidance-output.slot-codec@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControllerOutputSlotCodecIdentity{
        "gnc.package.yyz.pitch-moment-controller-output.slot-codec@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kActuatorOutputSlotCodecIdentity{
        "gnc.package.yyz.ideal-body-moment-actuator-output.slot-codec@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kPropulsionWrenchSlotCodecIdentity{
        "gnc.package.yyz.supplied-propulsion-body-wrench.slot-codec@1",
        "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kMassFlowSlotCodecIdentity{
        "gnc.package.yyz.mass-flow-interval.slot-codec@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kMissionResultSlotCodecIdentity{
        "gnc.package.yyz.committed-mission-result.slot-codec@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kControlledRigidBoundaryPreparationSlotCodecIdentity{
        "gnc.package.yyz.controlled-rigid-boundary-preparation.slot-codec@1",
        "0.1.0"};

inline constexpr std::string_view kMassInitialStateCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass.initial-state@1";
inline constexpr std::string_view kMassPublishProjectionCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass.publish-projection@1";
inline constexpr std::string_view kMassIntervalEvolutionCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass.interval-evolution@1";
inline constexpr std::string_view kControlledRigidBoundaryCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.rigid.controlled-boundary@1";
inline constexpr std::string_view
    kControlledRigidBoundaryRuntimeCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.rigid.controlled-boundary-with-invocation-results@1";
inline constexpr std::string_view kAltitudePitchGuidanceCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.guidance.altitude-pitch@1";
inline constexpr std::string_view kPitchMomentControllerCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.controller.pitch-moment@1";
inline constexpr std::string_view
    kIdealBodyMomentActuatorCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.actuator.ideal-body-moment@1";
inline constexpr std::string_view
    kFixedSuppliedPropulsionCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.propulsion.fixed-supplied@1";
inline constexpr std::string_view
    kCommittedMissionHistoryEvaluationCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.evaluator.committed-mission-history@1";
inline constexpr std::string_view
    kControlledRigidDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.rigid.definition-builder@1";
inline constexpr std::string_view
    kScalarBurnMassDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.mass.definition-builder@1";
inline constexpr std::string_view
    kAltitudePitchGuidanceDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.guidance.altitude-pitch.definition-builder@1";
inline constexpr std::string_view
    kPitchMomentControllerDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.controller.pitch-moment.definition-builder@1";
inline constexpr std::string_view
    kIdealBodyMomentActuatorDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.actuator.ideal-body-moment.definition-builder@1";
inline constexpr std::string_view
    kFixedSuppliedPropulsionDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.propulsion.fixed-supplied.definition-builder@1";
inline constexpr std::string_view
    kCommittedMissionResultDefinitionBuilderCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.evaluator.committed-mission.definition-builder@1";
inline constexpr std::string_view
    kControlledRigidRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.rigid.runtime-cell-factory@1";
inline constexpr std::string_view
    kScalarBurnMassRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.mass.runtime-cell-factory@1";
inline constexpr std::string_view
    kAltitudePitchGuidanceRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.guidance.altitude-pitch.runtime-cell-factory@1";
inline constexpr std::string_view
    kPitchMomentControllerRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.controller.pitch-moment.runtime-cell-factory@1";
inline constexpr std::string_view
    kIdealBodyMomentActuatorRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.actuator.ideal-body-moment.runtime-cell-factory@1";
inline constexpr std::string_view
    kFixedSuppliedPropulsionRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.propulsion.fixed-supplied.runtime-cell-factory@1";
inline constexpr std::string_view
    kCommittedMissionResultRuntimeCellFactoryCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.evaluator.committed-mission.runtime-cell-factory@1";
inline constexpr std::string_view kMassStateCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass.state-codec.getter@1";
inline constexpr std::string_view kMassPropertiesSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass-properties.slot-codec.getter@1";
inline constexpr std::string_view kGuidanceOutputSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.guidance-output.slot-codec.getter@1";
inline constexpr std::string_view kControllerOutputSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.controller-output.slot-codec.getter@1";
inline constexpr std::string_view kActuatorOutputSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.actuator-output.slot-codec.getter@1";
inline constexpr std::string_view kPropulsionWrenchSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.propulsion-wrench.slot-codec.getter@1";
inline constexpr std::string_view kMassFlowSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mass-flow.slot-codec.getter@1";
inline constexpr std::string_view kMissionResultSlotCodecCallShapeIdentity =
    "gnc.cpp-call-shape.yyz.mission-result.slot-codec.getter@1";
inline constexpr std::string_view
    kControlledRigidBoundaryPreparationSlotCodecCallShapeIdentity =
        "gnc.cpp-call-shape.yyz.controlled-rigid-preparation.slot-codec.getter@1";

struct ScalarBurnMassDefinition {
    std::string model_id;
    std::string model_version;
    std::string mass_state_id;
    gnc::foundation::NumericalPolicy numerical_policy;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_scalar_burn_mass_config(
    const ScalarBurnMassDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    ScalarBurnMassDefinition>
build_scalar_burn_mass_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct MassState {
    gnc::contracts::SampleContext context;
    std::string mass_state_id;
    double mass_kilograms = 0.0;
    BodyPointMeters body_origin_to_center_of_mass;
    BodyInertiaKilogramMetersSquared inertia_about_center_of_mass;
};

struct MassInitialStateInput {
    MassState state;
};

class MassInitialStateBuilder {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<MassState>
    build(const ScalarBurnMassDefinition& definition,
          const MassInitialStateInput& input,
          const gnc::foundation::NumericalPolicy& policy);
};

// Runtime-facing entry: the numerical policy is an immutable part of the
// canonical Definition, so Session materialization does not invent or inject
// a second policy value.
[[nodiscard]] gnc::foundation::NumericalOutcome<MassState>
build_scalar_burn_mass_initial_state(
    const ScalarBurnMassDefinition& definition,
    const MassInitialStateInput& input);

struct MassFlowIntervalInput {
    gnc::contracts::IntervalSampleContext context;
    std::string mass_state_id;
    double fuel_consumption_rate_kilograms_per_second = 0.0;
};

[[nodiscard]] MassPropertiesInput project_committed_mass_properties(
    const gnc::contracts::IntervalSampleContext& interval_context,
    const MassState& state);

struct BodyThrustDirectionUnit {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::UnitX();
};

struct BodyCenterOfMassToApplicationMeters {
    gnc::foundation::Vec3 value = gnc::foundation::Vec3::Zero();
};

struct SuppliedPropulsionDefinition {
    std::string model_id;
    std::string model_version;
    std::string source_id;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::string mass_state_id;
    gnc::foundation::NumericalPolicy numerical_policy;
};

struct SuppliedPropulsionInput {
    gnc::contracts::IntervalSampleContext context;
    double thrust_magnitude_newtons = 0.0;
    BodyThrustDirectionUnit thrust_direction;
    BodyCenterOfMassToApplicationMeters
        center_of_mass_to_application;
    BodyMomentNewtonMeters intrinsic_moment_at_application;
    double fuel_consumption_rate_kilograms_per_second = 0.0;
};

// This response intentionally keeps its application geometry relative to the
// committed center of mass. The rigid closure remains the sole wrench-
// transport owner after the atomic-boundary adapter supplies body-origin
// geometry from the committed MassState.
struct SuppliedPropulsionBodyWrench {
    gnc::contracts::IntervalSampleContext context;
    std::string source_id;
    BodyForceNewtons force;
    BodyCenterOfMassToApplicationMeters
        center_of_mass_to_application;
    BodyMomentNewtonMeters intrinsic_moment_at_application;
};

struct SuppliedPropulsionOutput {
    SuppliedPropulsionBodyWrench supplied_body_wrench;
    BodyMomentNewtonMeters lever_arm_moment;
    BodyMomentNewtonMeters moment_about_center_of_mass;
    MassFlowIntervalInput mass_flow;
};

class SuppliedPropulsionKernel {
  public:
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<SuppliedPropulsionOutput>
        evaluate(const SuppliedPropulsionDefinition& definition,
                 const SuppliedPropulsionInput& input);
};

struct FixedSuppliedPropulsionDefinition {
    SuppliedPropulsionDefinition propulsion;
    double thrust_magnitude_newtons = 0.0;
    BodyThrustDirectionUnit thrust_direction;
    BodyCenterOfMassToApplicationMeters center_of_mass_to_application;
    BodyMomentNewtonMeters intrinsic_moment_at_application;
    double fuel_consumption_rate_kilograms_per_second = 0.0;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_fixed_supplied_propulsion_config(
    const FixedSuppliedPropulsionDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    FixedSuppliedPropulsionDefinition>
build_fixed_supplied_propulsion_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

class FixedSuppliedPropulsionBoundaryKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        SuppliedPropulsionOutput>
    evaluate(const FixedSuppliedPropulsionDefinition& definition,
             const gnc::contracts::IntervalSampleContext& context);
};

struct MassStateCandidate {
    gnc::contracts::SimulationInstant effective_at;
    MassState state;
};

struct ScalarBurnMassOutput {
    double current_committed_mass_kilograms = 0.0;
    double integration_mass_kilograms = 0.0;
    double consumed_mass_kilograms = 0.0;
    MassStateCandidate candidate;
};

class ScalarBurnMassKernel {
  public:
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<ScalarBurnMassOutput>
        evaluate(const ScalarBurnMassDefinition& definition,
                 const MassState& committed_state,
                 const MassFlowIntervalInput& flow,
                 const gnc::foundation::NumericalPolicy& policy);
};

// Runtime-facing entry paired with the Definition builder above. The existing
// policy-taking kernel remains the R1 compatibility/science implementation.
[[nodiscard]] gnc::foundation::NumericalOutcome<ScalarBurnMassOutput>
evaluate_scalar_burn_mass_interval(
    const ScalarBurnMassDefinition& definition,
    const MassState& committed_state,
    const MassFlowIntervalInput& flow);

struct CommittedRigidMassBoundary {
    gnc::contracts::SampleContext rigid_context;
    RigidState rigid_state;
    MassState mass_state;
};

struct RigidMassIntervalInput {
    RigidStepContext context;
    EnvironmentInput environment;
    AppliedBodyWrenchInput supplied_wrench;
    MassFlowIntervalInput mass_flow;
};

struct AtomicRigidMassCandidate {
    gnc::contracts::SimulationInstant effective_at;
    RigidStateCandidate rigid;
    MassStateCandidate mass;
};

struct FrozenRigidMassStepOutput {
    CommittedRigidMassBoundary opening_boundary;
    MassPropertiesInput projected_committed_mass;
    RigidStepEvaluation rigid_step;
    ScalarBurnMassOutput mass_evolution;
    AtomicRigidMassCandidate candidate;
};

class FrozenRigidMassStepKernel {
  public:
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<FrozenRigidMassStepOutput>
        evaluate(const PreparedRigidStepModel& rigid_model,
                 const ScalarBurnMassDefinition& mass_definition,
                 const CommittedRigidMassBoundary& opening_boundary,
                 const RigidMassIntervalInput& interval);

    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<FrozenRigidMassStepOutput>
        evaluate_held_form(
            const PreparedRigidStepModel& rigid_model,
            const ScalarBurnMassDefinition& mass_definition,
            const CommittedRigidMassBoundary& opening_boundary,
            const RigidMassIntervalInput& interval,
            const RigidFrozenFormEvaluation& frozen_form,
            gnc::foundation::NumericalStatus frozen_form_status,
            const gnc::foundation::NumericalEvidence&
                frozen_form_evidence);
};

struct PropelledRigidMassIntervalInput {
    RigidStepContext context;
    EnvironmentInput environment;
    SuppliedPropulsionInput propulsion;
};

struct PropelledFrozenRigidMassStepOutput {
    SuppliedPropulsionOutput propulsion;
    FrozenRigidMassStepOutput atomic_boundary;
};

class PropelledFrozenRigidMassStepKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        PropelledFrozenRigidMassStepOutput>
        evaluate(const PreparedRigidStepModel& rigid_model,
                 const ScalarBurnMassDefinition& mass_definition,
                 const SuppliedPropulsionDefinition& propulsion_definition,
                 const CommittedRigidMassBoundary& opening_boundary,
                 const PropelledRigidMassIntervalInput& interval);
};

struct AltitudePitchGuidanceDefinition {
    std::string model_id;
    std::string model_version;
    gnc::contracts::FrameIdentity inertial_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = 0;
    double target_altitude_meters = 0.0;
    double altitude_error_gain_radians_per_meter = 0.0;
    double vertical_speed_gain_radian_seconds_per_meter = 0.0;
    double pitch_command_limit_radians = 0.0;
    gnc::foundation::QuaternionPolicy attitude_policy;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_altitude_pitch_guidance_config(
    const AltitudePitchGuidanceDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    AltitudePitchGuidanceDefinition>
build_altitude_pitch_guidance_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct AltitudePitchGuidanceOutput {
    CommittedRigidObservation source_observation;
    double measured_pitch_radians = 0.0;
    double measured_pitch_rate_radians_per_second = 0.0;
    double altitude_error_meters = 0.0;
    double altitude_feedback_radians = 0.0;
    double vertical_speed_feedback_radians = 0.0;
    double raw_pitch_command_radians = 0.0;
    double pitch_command_radians = 0.0;
    bool saturated = false;
};

class AltitudePitchGuidanceKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        AltitudePitchGuidanceOutput>
        evaluate(const AltitudePitchGuidanceDefinition& definition,
                 const CommittedRigidObservation& observation);
};

struct PitchMomentControllerDefinition {
    std::string model_id;
    std::string model_version;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = 0;
    double pitch_error_gain_newton_meters_per_radian = 0.0;
    double pitch_rate_gain_newton_meter_seconds_per_radian = 0.0;
    double moment_command_limit_newton_meters = 0.0;
    gnc::foundation::NumericalPolicy numerical_policy;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_pitch_moment_controller_config(
    const PitchMomentControllerDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    PitchMomentControllerDefinition>
build_pitch_moment_controller_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct PitchMomentControllerOutput {
    gnc::contracts::SampleContext context;
    double pitch_error_radians = 0.0;
    double proportional_moment_newton_meters = 0.0;
    double rate_damping_moment_newton_meters = 0.0;
    double raw_moment_command_newton_meters = 0.0;
    double moment_command_newton_meters = 0.0;
    bool saturated = false;
};

class PitchMomentControllerKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        PitchMomentControllerOutput>
        evaluate(const PitchMomentControllerDefinition& definition,
                 const AltitudePitchGuidanceOutput& guidance);
};

struct IdealBodyMomentActuatorDefinition {
    std::string model_id;
    std::string model_version;
    std::string source_id;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = 0;
    double realization_gain = 0.0;
    gnc::foundation::NumericalPolicy numerical_policy;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_ideal_body_moment_actuator_config(
    const IdealBodyMomentActuatorDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    IdealBodyMomentActuatorDefinition>
build_ideal_body_moment_actuator_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct IdealBodyMomentActuatorOutput {
    gnc::contracts::IntervalSampleContext context;
    std::string source_id;
    BodyMomentNewtonMeters moment_about_center_of_mass;
};

class IdealBodyMomentActuatorKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        IdealBodyMomentActuatorOutput>
        evaluate(const IdealBodyMomentActuatorDefinition& definition,
                 const gnc::contracts::IntervalSampleContext& context,
                 const PitchMomentControllerOutput& controller);
};

struct ControlledBodyWrenchAdapterDefinition {
    std::string combined_wrench_source_id;
};

struct ControlledRigidBoundaryEvaluationDefinition {
    RigidFrozenFormRuntimeDefinition rigid;
    ControlledBodyWrenchAdapterDefinition wrench_adapter;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_controlled_rigid_boundary_config(
    const ControlledRigidBoundaryEvaluationDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    ControlledRigidBoundaryEvaluationDefinition>
build_controlled_rigid_boundary_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct ControlledBodyWrenchAdapterInput {
    BodyPointMeters body_origin_to_center_of_mass;
    SuppliedPropulsionBodyWrench propulsion;
    IdealBodyMomentActuatorOutput actuator;
};

class ControlledBodyWrenchAdapterKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        AppliedBodyWrenchInput>
    evaluate(const ControlledBodyWrenchAdapterDefinition& definition,
             const ControlledBodyWrenchAdapterInput& input);
};

struct ControlledRigidBoundaryEvaluationInput {
    RigidStepContext context;
    RigidState committed_state;
    MassPropertiesInput mass_properties;
    SuppliedPropulsionBodyWrench propulsion;
    IdealBodyMomentActuatorOutput actuator;
};

struct ControlledRigidBoundaryResolvedEnvironmentInput {
    RigidStepContext context;
    RigidState committed_state;
    EnvironmentInput environment;
    MassPropertiesInput mass_properties;
    SuppliedPropulsionBodyWrench propulsion;
    IdealBodyMomentActuatorOutput actuator;
};

struct ControlledRigidBoundaryInvocationSet {
    gnc::model_sdk::BoundQueryHandle<
        PreparedUniformEnvironmentModel,
        UniformEnvironmentQueryEntry> environment;
    RigidFrozenFormInvocationSet frozen_form;
};

struct ControlledRigidBoundaryPreparationOutput {
    EnvironmentInput environment_response;
    AerodynamicTableQueryOutput aerodynamic_coefficients;
    ForceMomentClosureInput closure_request;
};

struct ControlledRigidBoundaryPreparationTelemetry {
    AppliedBodyWrenchInput controlled_wrench;
    UniformEnvironmentQueryTelemetry environment_query;
    RigidFrozenFormPreparationTelemetry frozen_form;
};

using ControlledRigidBoundaryPreparationEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<
        ControlledRigidBoundaryPreparationOutput,
        ControlledRigidBoundaryPreparationTelemetry>;

struct ControlledRigidBoundaryEvaluationOutput {
    AppliedBodyWrenchInput controlled_wrench;
    EnvironmentInput environment_response;
    RigidFrozenFormEvaluation frozen_form;
    RigidFrozenFormInvocationResults frozen_form_invocation_results;
    gnc::foundation::NumericalStatus frozen_form_status =
        gnc::foundation::NumericalStatus::InternalFailure;
    gnc::foundation::NumericalEvidence frozen_form_evidence;
};

struct ControlledRigidBoundaryTelemetry {
    AppliedBodyWrenchInput controlled_wrench;
    EnvironmentInput environment_response;
    RigidFrozenFormTelemetry frozen_form;
};

using ControlledRigidBoundaryEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<
        RigidFormInput, ControlledRigidBoundaryTelemetry>;

struct ControlledRigidBoundaryInvocationResults {
    EnvironmentInput environment_response;
    AerodynamicTableQueryOutput aerodynamic_coefficients;
};

struct ControlledRigidBoundaryInvocationEvaluation {
    ControlledRigidBoundaryEvaluation evaluation;
    ControlledRigidBoundaryInvocationResults invocation_results;
};

class ControlledRigidBoundaryEvaluationKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryPreparationEvaluation>
    prepare_for_integration(
        const ControlledRigidBoundaryEvaluationDefinition& definition,
        const ControlledRigidBoundaryInvocationSet& invocations,
        const ControlledRigidBoundaryEvaluationInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryEvaluation>
    evaluate(const ControlledRigidBoundaryEvaluationDefinition& definition,
             const ControlledRigidBoundaryInvocationSet& invocations,
             const ControlledRigidBoundaryEvaluationInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryInvocationEvaluation>
    evaluate_with_invocation_results(
        const ControlledRigidBoundaryEvaluationDefinition& definition,
        const ControlledRigidBoundaryInvocationSet& invocations,
        const ControlledRigidBoundaryEvaluationInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryEvaluationOutput>
    evaluate_resolved_environment(
        const ControlledRigidBoundaryEvaluationDefinition& definition,
        const RigidFrozenFormInvocationSet& invocations,
        const ControlledRigidBoundaryResolvedEnvironmentInput& input);
};

struct ControlledPropelledRigidMassStepDefinition {
    std::string model_id;
    std::string model_version;
    std::string combined_wrench_source_id;
    AltitudePitchGuidanceDefinition guidance;
    PitchMomentControllerDefinition controller;
    IdealBodyMomentActuatorDefinition actuator;
};

struct ControlledPropelledRigidMassStepOutput {
    CommittedRigidObservation observation;
    AltitudePitchGuidanceOutput guidance;
    PitchMomentControllerOutput controller;
    IdealBodyMomentActuatorOutput actuator;
    SuppliedPropulsionOutput propulsion;
    FrozenRigidMassStepOutput atomic_boundary;
};

class ControlledPropelledRigidMassStepKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        ControlledPropelledRigidMassStepOutput>
        evaluate(
            const PreparedRigidStepModel& rigid_model,
            const ScalarBurnMassDefinition& mass_definition,
            const SuppliedPropulsionDefinition& propulsion_definition,
            const ControlledPropelledRigidMassStepDefinition& definition,
            const CommittedRigidMassBoundary& opening_boundary,
            const PropelledRigidMassIntervalInput& interval);
};

struct TwoIntervalControlledPropelledCommitInput {
    CommittedRigidMassBoundary opening_boundary;
    std::array<PropelledRigidMassIntervalInput, 2U> intervals;
};

struct TwoIntervalControlledPropelledCommitIntervalOutput {
    ControlledPropelledRigidMassStepOutput staged;
    CommittedRigidMassBoundary closing_commit;
};

struct TwoIntervalControlledPropelledCommitOutput {
    std::array<TwoIntervalControlledPropelledCommitIntervalOutput, 2U>
        intervals;
    CommittedRigidMassBoundary terminal_boundary;
};

class TwoIntervalControlledPropelledCommitKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        TwoIntervalControlledPropelledCommitOutput>
        evaluate(
            const PreparedRigidStepModel& rigid_model,
            const ScalarBurnMassDefinition& mass_definition,
            const SuppliedPropulsionDefinition& propulsion_definition,
            const ControlledPropelledRigidMassStepDefinition& definition,
            const TwoIntervalControlledPropelledCommitInput& input);
};

enum class MissionMetric : std::uint8_t {
    DurationSeconds,
    DownrangeMeters,
    RemainingMassKilograms,
};

enum class MissionRelation : std::uint8_t {
    LessThanOrEqual,
    GreaterThanOrEqual,
};

enum class MissionAction : std::uint8_t {
    Complete,
    Abort,
};

enum class MissionResultStatus : std::uint8_t {
    Completed,
    Aborted,
};

struct MissionTerminationPredicate {
    std::string predicate_id;
    MissionMetric metric = MissionMetric::DurationSeconds;
    MissionRelation relation = MissionRelation::GreaterThanOrEqual;
    double threshold = 0.0;
    MissionAction action = MissionAction::Complete;
    std::string reason_code;
    std::int64_t priority = 0;
};

struct CommittedMissionResultDefinition {
    std::string model_id;
    std::string model_version;
    std::string subject;
    gnc::contracts::FrameIdentity inertial_frame;
    gnc::contracts::FrameIdentity body_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::string mass_state_id;
    std::int64_t configuration_revision = 0;
    gnc::foundation::NumericalPolicy numerical_policy;
    std::array<MissionTerminationPredicate, 3U> predicates;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_committed_mission_result_config(
    const CommittedMissionResultDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    CommittedMissionResultDefinition>
build_committed_mission_result_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

struct MissionMetrics {
    double duration_seconds = 0.0;
    double downrange_meters = 0.0;
    double vertical_displacement_meters = 0.0;
    double remaining_mass_kilograms = 0.0;
    double consumed_mass_kilograms = 0.0;
    double speed_meters_per_second = 0.0;
};

struct MissionPredicateEvaluation {
    std::string predicate_id;
    double observed = 0.0;
    bool met = false;
    MissionAction action = MissionAction::Complete;
    std::string reason_code;
    std::int64_t priority = 0;
};

struct MissionDecision {
    MissionAction action = MissionAction::Complete;
    std::string reason_code;
    double trigger_time_seconds = 0.0;
    std::int64_t priority = 0;
};

struct MissionMetricSummary {
    std::size_t evaluated_sample_count = 0U;
    MissionMetrics terminal;
    double peak_speed_meters_per_second = 0.0;
    std::int64_t peak_speed_tick = 0;
    double maximum_downrange_meters = 0.0;
    std::int64_t maximum_downrange_tick = 0;
    double minimum_remaining_mass_kilograms = 0.0;
    std::int64_t minimum_remaining_mass_tick = 0;
};

struct CommittedMissionResultInput {
    std::array<CommittedRigidMassBoundary, kCommittedMissionHistoryDepth>
        committed_samples;
};

// R3 supplies chronological committed histories for the two state layouts in
// the evaluator history plan. This adapter is the package-owned, non-science
// assembly boundary into the existing mission-result kernel.
struct CommittedMissionStateHistoryInput {
    std::array<RigidState, kCommittedMissionHistoryDepth> rigid_states;
    std::array<MassState, kCommittedMissionHistoryDepth> mass_states;
};

struct CommittedMissionResultOutput {
    MissionResultStatus status = MissionResultStatus::Completed;
    std::int64_t initial_tick = 0;
    std::int64_t final_tick = 0;
    double final_time_seconds = 0.0;
    MissionDecision termination;
    MissionMetricSummary metrics;
    std::array<MissionPredicateEvaluation, 3U> terminal_predicates;
    CommittedRigidMassBoundary terminal_boundary;
};

class CommittedMissionResultKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        CommittedMissionResultOutput>
        evaluate(const CommittedMissionResultDefinition& definition,
                 const CommittedMissionResultInput& input);
};

class CommittedMissionHistoryEvaluationKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        CommittedMissionResultOutput>
    evaluate(const CommittedMissionResultDefinition& definition,
             const CommittedMissionStateHistoryInput& input);
};

struct TwoIntervalMassCommitInput {
    CommittedRigidMassBoundary opening_boundary;
    std::array<RigidMassIntervalInput, 2U> intervals;
};

struct TwoIntervalMassCommitIntervalOutput {
    FrozenRigidMassStepOutput staged;
    CommittedRigidMassBoundary closing_commit;
};

struct TwoIntervalMassCommitOutput {
    std::array<TwoIntervalMassCommitIntervalOutput, 2U> intervals;
    CommittedRigidMassBoundary terminal_boundary;
};

class TwoIntervalMassCommitKernel {
  public:
    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<TwoIntervalMassCommitOutput>
        evaluate(const PreparedRigidStepModel& rigid_model,
                 const ScalarBurnMassDefinition& mass_definition,
                 const TwoIntervalMassCommitInput& input);
};

using ControlledRigidDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryEvaluationDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using ScalarBurnMassDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ScalarBurnMassDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using AltitudePitchGuidanceDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<AltitudePitchGuidanceDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using PitchMomentControllerDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<PitchMomentControllerDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using IdealBodyMomentActuatorDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<IdealBodyMomentActuatorDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using FixedSuppliedPropulsionDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<FixedSuppliedPropulsionDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using CommittedMissionResultDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<CommittedMissionResultDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);

using MassInitialStateCall =
    gnc::foundation::NumericalOutcome<MassState> (*)(
        const ScalarBurnMassDefinition&, const MassInitialStateInput&);
using MassPublishProjectionCall = MassPropertiesInput (*)(
    const gnc::contracts::IntervalSampleContext&, const MassState&);
using MassIntervalEvolutionCall =
    gnc::foundation::NumericalOutcome<ScalarBurnMassOutput> (*)(
        const ScalarBurnMassDefinition&, const MassState&,
        const MassFlowIntervalInput&);
using ControlledRigidBoundaryCall =
    gnc::foundation::NumericalOutcome<ControlledRigidBoundaryEvaluation> (*)(
        const ControlledRigidBoundaryEvaluationDefinition&,
        const ControlledRigidBoundaryInvocationSet&,
        const ControlledRigidBoundaryEvaluationInput&);
using ControlledRigidBoundaryRuntimeCall =
    gnc::foundation::NumericalOutcome<
        ControlledRigidBoundaryPreparationEvaluation> (*)(
        const ControlledRigidBoundaryEvaluationDefinition&,
        const ControlledRigidBoundaryInvocationSet&,
        const ControlledRigidBoundaryEvaluationInput&);
using AltitudePitchGuidanceCall =
    gnc::foundation::NumericalOutcome<AltitudePitchGuidanceOutput> (*)(
        const AltitudePitchGuidanceDefinition&,
        const CommittedRigidObservation&);
using PitchMomentControllerCall =
    gnc::foundation::NumericalOutcome<PitchMomentControllerOutput> (*)(
        const PitchMomentControllerDefinition&,
        const AltitudePitchGuidanceOutput&);
using IdealBodyMomentActuatorCall =
    gnc::foundation::NumericalOutcome<IdealBodyMomentActuatorOutput> (*)(
        const IdealBodyMomentActuatorDefinition&,
        const gnc::contracts::IntervalSampleContext&,
        const PitchMomentControllerOutput&);
using FixedSuppliedPropulsionCall =
    gnc::foundation::NumericalOutcome<SuppliedPropulsionOutput> (*)(
        const FixedSuppliedPropulsionDefinition&,
        const gnc::contracts::IntervalSampleContext&);
using CommittedMissionHistoryEvaluationCall =
    gnc::foundation::NumericalOutcome<CommittedMissionResultOutput> (*)(
        const CommittedMissionResultDefinition&,
        const CommittedMissionStateHistoryInput&);

// Package-owned Runtime Cell factory contracts. These values contain only an
// immutable typed Definition, exact process-local entries, and plan-local
// numeric references supplied by a future R3 package composition boundary.
// They contain no Session state, workspace, scheduler, store, or generic
// callback/dispatch framework; their fixed typed function references are
// package facts. R2 only exact-links the factory entries without invoking
// them.
struct ControlledRigidRuntimeCellBindings {
    std::uint32_t state_block_handle = 0U;
    std::uint32_t publish_projection_callsite_handle = 0U;
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    std::uint32_t derivative_evaluation_callsite_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<CommittedRigidObservation>
        observation_output;
    gnc::model_sdk::CompiledOutputWriter<
        ControlledRigidBoundaryPreparationOutput>
        boundary_preparation_output;
    std::uint32_t mass_properties_input_slot_handle = 0U;
    std::uint32_t propulsion_body_wrench_input_slot_handle = 0U;
    std::uint32_t actuator_output_input_slot_handle = 0U;
    std::uint32_t held_form_result_slot_handle = 0U;
    RigidPublishProjectionCall publish_projection = nullptr;
    ControlledRigidBoundaryRuntimeCall boundary_evaluation = nullptr;
    RigidDerivativeCall derivative_evaluation = nullptr;
    ControlledRigidBoundaryInvocationSet bound_invocations;
};

struct ControlledRigidRuntimeCell {
    const ControlledRigidBoundaryEvaluationDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const ControlledRigidRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<ControlledRigidRuntimeCell>
create_controlled_rigid_runtime_cell(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ControlledRigidRuntimeCellBindings& bindings);

using ControlledRigidRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ControlledRigidRuntimeCell,
        ControlledRigidBoundaryEvaluationDefinition,
        ControlledRigidRuntimeCellBindings>;

struct ScalarBurnMassRuntimeCellBindings {
    std::uint32_t state_block_handle = 0U;
    std::uint32_t publish_projection_callsite_handle = 0U;
    std::uint32_t interval_evolution_callsite_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<MassPropertiesInput>
        mass_properties_output;
    gnc::model_sdk::OutputWriterTokenId candidate_state_writer;
    std::uint32_t mass_flow_input_slot_handle = 0U;
    MassPublishProjectionCall publish_projection = nullptr;
    MassIntervalEvolutionCall interval_evolution = nullptr;
};

struct ScalarBurnMassRuntimeCell {
    const ScalarBurnMassDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const ScalarBurnMassRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<ScalarBurnMassRuntimeCell>
create_scalar_burn_mass_runtime_cell(
    const ScalarBurnMassDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ScalarBurnMassRuntimeCellBindings& bindings);

using ScalarBurnMassRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ScalarBurnMassRuntimeCell, ScalarBurnMassDefinition,
        ScalarBurnMassRuntimeCellBindings>;

struct AltitudePitchGuidanceRuntimeCellBindings {
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    std::uint32_t observation_input_slot_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<AltitudePitchGuidanceOutput>
        guidance_output;
    AltitudePitchGuidanceCall boundary_evaluation = nullptr;
};

struct AltitudePitchGuidanceRuntimeCell {
    const AltitudePitchGuidanceDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const AltitudePitchGuidanceRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    AltitudePitchGuidanceRuntimeCell>
create_altitude_pitch_guidance_runtime_cell(
    const AltitudePitchGuidanceDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const AltitudePitchGuidanceRuntimeCellBindings& bindings);

using AltitudePitchGuidanceRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        AltitudePitchGuidanceRuntimeCell,
        AltitudePitchGuidanceDefinition,
        AltitudePitchGuidanceRuntimeCellBindings>;

struct PitchMomentControllerRuntimeCellBindings {
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    std::uint32_t guidance_input_slot_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<PitchMomentControllerOutput>
        controller_output;
    PitchMomentControllerCall boundary_evaluation = nullptr;
};

struct PitchMomentControllerRuntimeCell {
    const PitchMomentControllerDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const PitchMomentControllerRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    PitchMomentControllerRuntimeCell>
create_pitch_moment_controller_runtime_cell(
    const PitchMomentControllerDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const PitchMomentControllerRuntimeCellBindings& bindings);

using PitchMomentControllerRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        PitchMomentControllerRuntimeCell,
        PitchMomentControllerDefinition,
        PitchMomentControllerRuntimeCellBindings>;

struct IdealBodyMomentActuatorRuntimeCellBindings {
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    std::uint32_t controller_input_slot_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<IdealBodyMomentActuatorOutput>
        actuator_output;
    IdealBodyMomentActuatorCall boundary_evaluation = nullptr;
};

struct IdealBodyMomentActuatorRuntimeCell {
    const IdealBodyMomentActuatorDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const IdealBodyMomentActuatorRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    IdealBodyMomentActuatorRuntimeCell>
create_ideal_body_moment_actuator_runtime_cell(
    const IdealBodyMomentActuatorDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const IdealBodyMomentActuatorRuntimeCellBindings& bindings);

using IdealBodyMomentActuatorRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        IdealBodyMomentActuatorRuntimeCell,
        IdealBodyMomentActuatorDefinition,
        IdealBodyMomentActuatorRuntimeCellBindings>;

struct FixedSuppliedPropulsionRuntimeCellBindings {
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<SuppliedPropulsionBodyWrench>
        propulsion_wrench_output;
    gnc::model_sdk::CompiledOutputWriter<MassFlowIntervalInput>
        mass_flow_output;
    FixedSuppliedPropulsionCall boundary_evaluation = nullptr;
};

struct FixedSuppliedPropulsionRuntimeCell {
    const FixedSuppliedPropulsionDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const FixedSuppliedPropulsionRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    FixedSuppliedPropulsionRuntimeCell>
create_fixed_supplied_propulsion_runtime_cell(
    const FixedSuppliedPropulsionDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const FixedSuppliedPropulsionRuntimeCellBindings& bindings);

using FixedSuppliedPropulsionRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        FixedSuppliedPropulsionRuntimeCell,
        FixedSuppliedPropulsionDefinition,
        FixedSuppliedPropulsionRuntimeCellBindings>;

struct CommittedMissionResultRuntimeCellBindings {
    std::uint32_t boundary_evaluation_callsite_handle = 0U;
    std::uint32_t committed_history_handle = 0U;
    gnc::model_sdk::CompiledOutputWriter<CommittedMissionResultOutput>
        mission_result_output;
    CommittedMissionHistoryEvaluationCall boundary_evaluation = nullptr;
};

struct CommittedMissionResultRuntimeCell {
    const CommittedMissionResultDefinition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
    const CommittedMissionResultRuntimeCellBindings bindings;
};

[[nodiscard]] gnc::foundation::NumericalOutcome<
    CommittedMissionResultRuntimeCell>
create_committed_mission_result_runtime_cell(
    const CommittedMissionResultDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const CommittedMissionResultRuntimeCellBindings& bindings);

using CommittedMissionResultRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        CommittedMissionResultRuntimeCell,
        CommittedMissionResultDefinition,
        CommittedMissionResultRuntimeCellBindings>;

using MassStateCloneCall = MassState (*)(const MassState&);
using MassStateValidateCall = bool (*)(const MassState&) noexcept;
using MassStateSwapCall = void (*)(MassState&, MassState&) noexcept;
using MassStateCodec = gnc::model_sdk::InProcessStateCodec<
    MassStateCloneCall, MassStateValidateCall,
    MassStateValidateCall, MassStateValidateCall,
    MassStateSwapCall, MassPublishProjectionCall>;
using MassStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<MassStateCodec>;

[[nodiscard]] MassState clone_mass_state(const MassState& state);
[[nodiscard]] bool validate_mass_state(const MassState& state) noexcept;
[[nodiscard]] bool validate_mass_state_finite(
    const MassState& state) noexcept;
[[nodiscard]] bool validate_mass_state_invariants(
    const MassState& state) noexcept;
void swap_mass_state(MassState& lhs, MassState& rhs) noexcept;
[[nodiscard]] const MassStateCodec& mass_state_codec() noexcept;

using MassPropertiesSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<MassPropertiesInput>;
using GuidanceOutputSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<AltitudePitchGuidanceOutput>;
using ControllerOutputSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<PitchMomentControllerOutput>;
using ActuatorOutputSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<IdealBodyMomentActuatorOutput>;
using PropulsionWrenchSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<SuppliedPropulsionBodyWrench>;
using MassFlowSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<MassFlowIntervalInput>;
using MissionResultSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<CommittedMissionResultOutput>;
using ControlledRigidBoundaryPreparationSlotCodec =
    gnc::model_sdk::TypedInProcessSlotCodec<
        ControlledRigidBoundaryPreparationOutput>;

} // namespace gnc::packages::yyz
