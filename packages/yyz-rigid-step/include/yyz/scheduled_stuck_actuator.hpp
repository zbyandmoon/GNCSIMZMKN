#pragma once

#include <gnc/model_sdk/in_process_codec.hpp>
#include <gnc/model_sdk/runtime_cell_factory.hpp>
#include <gnc/model_sdk/static_implementation.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace gnc::packages::yyz {

inline constexpr std::string_view kStuckQualificationPackageId =
    "gnc.package.yyz.stuck-actuator-qualification@1";
inline constexpr std::string_view kStuckQualificationPackageVersion =
    "0.1.0";
inline constexpr std::string_view kStuckQualificationBuildFingerprint =
    "build.yyz.stuck-actuator-qualification.release";

inline constexpr std::string_view kScheduledSurfaceDemandModelId =
    "gnc.package.yyz.demand.scheduled-surface.qualification@1";
inline constexpr std::string_view kStatefulStuckActuatorModelId =
    "gnc.package.yyz.actuator.stateful-stuck.qualification@1";
inline constexpr std::string_view kPressureSurfaceLoadModelId =
    "gnc.package.yyz.aero.pressure-surface-load.qualification@1";
inline constexpr std::string_view kVerticalPitchRigidModelId =
    "gnc.package.yyz.rigid.vertical-pitch.qualification@1";
inline constexpr std::string_view kContactImpactEvaluatorModelId =
    "gnc.package.yyz.contact-impact.qualification@1";
inline constexpr std::string_view kStuckQualificationModelVersion =
    "0.1.0";

inline constexpr std::string_view kScheduledDemandConfigSchemaId =
    "gnc.config.yyz.scheduled-surface-demand@1";
inline constexpr std::string_view kStuckActuatorConfigSchemaId =
    "gnc.config.yyz.stateful-stuck-actuator@1";
inline constexpr std::string_view kPressureSurfaceLoadConfigSchemaId =
    "gnc.config.yyz.pressure-surface-load@1";
inline constexpr std::string_view kVerticalPitchRigidConfigSchemaId =
    "gnc.config.yyz.vertical-pitch-rigid@1";
inline constexpr std::string_view kContactImpactConfigSchemaId =
    "gnc.config.yyz.contact-impact@1";
inline constexpr std::string_view kActuatorInitialStateSchemaId =
    "gnc.initial.yyz.stateful-stuck-actuator@1";
inline constexpr std::string_view kRigidInitialStateSchemaId =
    "gnc.initial.yyz.vertical-pitch-rigid@1";

inline constexpr std::string_view kScheduledSurfaceDemandContractId =
    "gnc.contract.yyz.scheduled-surface-demand@1";
inline constexpr std::string_view kActuatorStateObservationContractId =
    "gnc.contract.yyz.actuator-state-observation@1";
inline constexpr std::string_view kActualSurfaceOutputContractId =
    "gnc.contract.yyz.actual-surface-output@1";
inline constexpr std::string_view kPressureSurfaceLoadContractId =
    "gnc.contract.yyz.pressure-surface-load@1";
inline constexpr std::string_view kVerticalPitchRigidObservationContractId =
    "gnc.contract.yyz.vertical-pitch-rigid-observation@1";
inline constexpr std::string_view kCommittedRigidContactSequenceContractId =
    "gnc.contract.yyz.committed-rigid-contact-sequence@1";
inline constexpr std::string_view kContactImpactResultContractId =
    "gnc.contract.yyz.contact-impact-result@1";
inline constexpr std::string_view kTerminalBranchDecisionContractId =
    "gnc.contract.runtime.terminal-branch-decision@1";
inline constexpr std::string_view kStuckActuatorCommandSchemaId =
    "gnc.command.yyz.actuator-stuck@1";
inline constexpr std::string_view kStuckActuatorEventSchemaId =
    "gnc.event.yyz.actuator-stuck-applied@1";
inline constexpr std::string_view kStuckActuatorEventConsumedSchemaId =
    "gnc.event-result.yyz.actuator-stuck-consumed@1";

inline constexpr std::string_view kActuatorStateSchemaId =
    "gnc.state-schema.yyz.stateful-stuck-actuator@1";
inline constexpr std::string_view kActuatorStateLayoutId =
    "gnc.layout.yyz.stateful-stuck-actuator@1";
inline constexpr std::string_view kVerticalPitchRigidStateSchemaId =
    "gnc.state-schema.yyz.vertical-pitch-rigid@1";
inline constexpr std::string_view kVerticalPitchRigidStateLayoutId =
    "gnc.layout.yyz.vertical-pitch-rigid@1";
inline constexpr std::string_view kScheduledSurfaceDemandLayoutId =
    "gnc.layout.yyz.scheduled-surface-demand@1";
inline constexpr std::string_view kActuatorStateObservationLayoutId =
    "gnc.layout.yyz.actuator-state-observation@1";
inline constexpr std::string_view kActualSurfaceOutputLayoutId =
    "gnc.layout.yyz.actual-surface-output@1";
inline constexpr std::string_view kPressureSurfaceLoadLayoutId =
    "gnc.layout.yyz.pressure-surface-load@1";
inline constexpr std::string_view kVerticalPitchRigidObservationLayoutId =
    "gnc.layout.yyz.vertical-pitch-rigid-observation@1";
inline constexpr std::string_view kContactImpactResultLayoutId =
    "gnc.layout.yyz.contact-impact-result@1";
inline constexpr std::string_view kTerminalBranchDecisionLayoutId =
    "gnc.layout.runtime.terminal-branch-decision@1";

inline constexpr std::string_view kRigidContactHistoryMemberId =
    "rigid-contact-state";
inline constexpr std::uint32_t kRigidContactHistoryDepth = 1U;
inline constexpr std::uint32_t kStuckActuatorDecisionAuthority = 73U;

enum class ActuatorFaultMode : std::uint8_t {
    Healthy,
    Stuck,
};

struct FaultStateFragment {
    ActuatorFaultMode mode = ActuatorFaultMode::Healthy;
    double locked_position_radians = 0.0;
    std::uint64_t revision = 0U;
};

// This fragment is the actuator owner's complete replacement block. Healthy
// surface position is a per-boundary formal output, never a second state
// authority. The locked position is relevant only after the fault latches.

struct StuckActuatorCommand {
    double locked_position_radians = 0.0;
};

struct StuckActuatorEvent {
    ActuatorFaultMode prior_mode = ActuatorFaultMode::Healthy;
    ActuatorFaultMode committed_mode = ActuatorFaultMode::Stuck;
    double locked_position_radians = 0.0;
    std::uint64_t revision = 0U;
};

struct StuckActuatorEventConsumed {
    ActuatorFaultMode observed_mode = ActuatorFaultMode::Healthy;
    double locked_position_radians = 0.0;
    std::uint64_t revision = 0U;
};

struct ScheduledSurfaceDemand {
    std::int64_t tick = 0;
    double demanded_position_radians = 0.0;
};

struct ActuatorStateObservation {
    ActuatorFaultMode mode = ActuatorFaultMode::Healthy;
    double locked_position_radians = 0.0;
    std::uint64_t fault_revision = 0U;
};

struct ActualSurfaceOutput {
    std::int64_t tick = 0;
    double demanded_position_radians = 0.0;
    double actual_position_radians = 0.0;
    ActuatorFaultMode mode = ActuatorFaultMode::Healthy;
    std::uint64_t fault_revision = 0U;
};

struct PressureSurfaceLoad {
    std::int64_t tick = 0;
    double actual_position_radians = 0.0;
    double lift_force_newtons = 0.0;
    double pitch_moment_newton_meters = 0.0;
};

struct VerticalPitchRigidState {
    double altitude_meters = 0.0;
    double vertical_velocity_meters_per_second = 0.0;
    double pitch_radians = 0.0;
    double pitch_rate_radians_per_second = 0.0;
    double last_surface_position_radians = 0.0;
    double last_lift_force_newtons = 0.0;
    double last_pitch_moment_newton_meters = 0.0;
    std::uint64_t revision = 0U;
};

struct VerticalPitchRigidObservation {
    std::int64_t tick = 0;
    VerticalPitchRigidState state;
};

struct ContactImpactResult {
    std::int64_t tick = 0;
    bool impact = false;
    double altitude_meters = 0.0;
    double vertical_velocity_meters_per_second = 0.0;
    double pitch_radians = 0.0;
    std::string reason_code;
};

struct ContactImpactEvaluation {
    ContactImpactResult result;
    gnc::contracts::TransactionBranch branch =
        gnc::contracts::TransactionBranch::Continue;
};

struct ScheduledDemandDefinition {
    double initial_position_radians = 0.0;
    double first_position_radians = 0.0;
    double later_position_radians = 0.0;
    std::int64_t first_change_tick = 0;
    std::int64_t later_change_tick = 0;
};

struct StatefulActuatorDefinition {
    double minimum_position_radians = 0.0;
    double maximum_position_radians = 0.0;
};

struct PressureSurfaceLoadDefinition {
    double dynamic_pressure_pascals = 0.0;
    double reference_area_square_meters = 0.0;
    double lift_coefficient_slope_per_radian = 0.0;
    double moment_arm_meters = 0.0;
};

struct VerticalPitchRigidDefinition {
    double mass_kilograms = 0.0;
    double pitch_inertia_kilogram_meters_squared = 0.0;
    double gravity_meters_per_second_squared = 0.0;
    double fixed_step_seconds = 0.0;
};

struct ContactImpactDefinition {
    double ground_altitude_meters = 0.0;
};

struct FaultStateInitialInput {
    double locked_position_radians = 0.0;
};

struct VerticalPitchRigidInitialStateInput {
    double altitude_meters = 0.0;
    double vertical_velocity_meters_per_second = 0.0;
    double pitch_radians = 0.0;
    double pitch_rate_radians_per_second = 0.0;
};

struct StuckActuatorReduction {
    FaultStateFragment candidate;
    StuckActuatorEvent event;
};

template <typename Definition>
struct QualificationRuntimeCell {
    Definition definition;
    gnc::model_sdk::RuntimeCellFactoryContext context;
};

struct QualificationRuntimeCellBindings {};

using ScheduledDemandRuntimeCell =
    QualificationRuntimeCell<ScheduledDemandDefinition>;
using StatefulActuatorRuntimeCell =
    QualificationRuntimeCell<StatefulActuatorDefinition>;
using PressureSurfaceLoadRuntimeCell =
    QualificationRuntimeCell<PressureSurfaceLoadDefinition>;
using VerticalPitchRigidRuntimeCell =
    QualificationRuntimeCell<VerticalPitchRigidDefinition>;
using ContactImpactRuntimeCell =
    QualificationRuntimeCell<ContactImpactDefinition>;

using ScheduledDemandDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ScheduledDemandDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using StatefulActuatorDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<StatefulActuatorDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using PressureSurfaceLoadDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<PressureSurfaceLoadDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using VerticalPitchRigidDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<VerticalPitchRigidDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using ContactImpactDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ContactImpactDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);

using ScheduledDemandRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ScheduledDemandRuntimeCell, ScheduledDemandDefinition,
        QualificationRuntimeCellBindings>;
using StatefulActuatorRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        StatefulActuatorRuntimeCell, StatefulActuatorDefinition,
        QualificationRuntimeCellBindings>;
using PressureSurfaceLoadRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        PressureSurfaceLoadRuntimeCell, PressureSurfaceLoadDefinition,
        QualificationRuntimeCellBindings>;
using VerticalPitchRigidRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        VerticalPitchRigidRuntimeCell, VerticalPitchRigidDefinition,
        QualificationRuntimeCellBindings>;
using ContactImpactRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ContactImpactRuntimeCell, ContactImpactDefinition,
        QualificationRuntimeCellBindings>;

using ScheduledDemandCall =
    gnc::foundation::NumericalOutcome<ScheduledSurfaceDemand> (*)(
        const ScheduledDemandDefinition&, std::int64_t);
using ActuatorStateProjectionCall = ActuatorStateObservation (*)(
    const FaultStateFragment&);
using StatefulActuatorCall =
    gnc::foundation::NumericalOutcome<ActualSurfaceOutput> (*)(
        const StatefulActuatorDefinition&, const FaultStateFragment&,
        const ScheduledSurfaceDemand&);
using StuckActuatorReductionCall =
    gnc::foundation::NumericalOutcome<StuckActuatorReduction> (*)(
        const StatefulActuatorDefinition&, const FaultStateFragment&,
        const StuckActuatorCommand&);
using StuckActuatorEventConsumptionCall =
    StuckActuatorEventConsumed (*)(const StuckActuatorEvent&);
using PressureSurfaceLoadCall =
    gnc::foundation::NumericalOutcome<PressureSurfaceLoad> (*)(
        const PressureSurfaceLoadDefinition&, const ActualSurfaceOutput&);
using VerticalPitchRigidProjectionCall =
    VerticalPitchRigidObservation (*)(const VerticalPitchRigidState&,
                                      std::int64_t);
using VerticalPitchRigidEvolutionCall =
    gnc::foundation::NumericalOutcome<VerticalPitchRigidState> (*)(
        const VerticalPitchRigidDefinition&,
        const VerticalPitchRigidState&, const PressureSurfaceLoad&);
using ContactImpactEvaluationCall =
    gnc::foundation::NumericalOutcome<ContactImpactEvaluation> (*)(
        const ContactImpactDefinition&, const VerticalPitchRigidState&,
        std::int64_t);
using StatefulActuatorInitialStateCall =
    gnc::foundation::NumericalOutcome<FaultStateFragment> (*)(
        const StatefulActuatorDefinition&,
        const FaultStateInitialInput&);
using VerticalPitchRigidInitialStateCall =
    gnc::foundation::NumericalOutcome<VerticalPitchRigidState> (*)(
        const VerticalPitchRigidDefinition&,
        const VerticalPitchRigidInitialStateInput&);

using FaultStateFragmentCloneCall =
    FaultStateFragment (*)(const FaultStateFragment&);
using FaultStateFragmentValidateCall =
    bool (*)(const FaultStateFragment&) noexcept;
using FaultStateFragmentSwapCall =
    void (*)(FaultStateFragment&, FaultStateFragment&) noexcept;
using FaultStateFragmentCodec = gnc::model_sdk::InProcessStateCodec<
    FaultStateFragmentCloneCall, FaultStateFragmentValidateCall,
    FaultStateFragmentValidateCall, FaultStateFragmentValidateCall,
    FaultStateFragmentSwapCall, ActuatorStateProjectionCall>;
using FaultStateFragmentCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<FaultStateFragmentCodec>;

using VerticalPitchRigidStateCloneCall =
    VerticalPitchRigidState (*)(const VerticalPitchRigidState&);
using VerticalPitchRigidStateValidateCall =
    bool (*)(const VerticalPitchRigidState&) noexcept;
using VerticalPitchRigidStateSwapCall =
    void (*)(VerticalPitchRigidState&, VerticalPitchRigidState&) noexcept;
using VerticalPitchRigidStateProjectCall =
    VerticalPitchRigidState (*)(const VerticalPitchRigidState&);
using VerticalPitchRigidStateCodec = gnc::model_sdk::InProcessStateCodec<
    VerticalPitchRigidStateCloneCall, VerticalPitchRigidStateValidateCall,
    VerticalPitchRigidStateValidateCall, VerticalPitchRigidStateValidateCall,
    VerticalPitchRigidStateSwapCall, VerticalPitchRigidStateProjectCall>;
using VerticalPitchRigidStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<VerticalPitchRigidStateCodec>;

[[nodiscard]] gnc::model_sdk::StaticPackageDescriptor
describe_scheduled_stuck_actuator_package();
[[nodiscard]] gnc::model_sdk::StaticPackageImplementation
describe_scheduled_stuck_actuator_implementation(
    std::string build_fingerprint =
        std::string(kStuckQualificationBuildFingerprint));

[[nodiscard]] gnc::foundation::NumericalOutcome<ScheduledDemandDefinition>
build_scheduled_demand_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<StatefulActuatorDefinition>
build_stateful_actuator_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<PressureSurfaceLoadDefinition>
build_pressure_surface_load_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<VerticalPitchRigidDefinition>
build_vertical_pitch_rigid_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<ContactImpactDefinition>
build_contact_impact_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

[[nodiscard]] gnc::foundation::NumericalOutcome<ScheduledDemandRuntimeCell>
create_scheduled_demand_runtime_cell(
    const ScheduledDemandDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<StatefulActuatorRuntimeCell>
create_stateful_actuator_runtime_cell(
    const StatefulActuatorDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<PressureSurfaceLoadRuntimeCell>
create_pressure_surface_load_runtime_cell(
    const PressureSurfaceLoadDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<VerticalPitchRigidRuntimeCell>
create_vertical_pitch_rigid_runtime_cell(
    const VerticalPitchRigidDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<ContactImpactRuntimeCell>
create_contact_impact_runtime_cell(
    const ContactImpactDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings& bindings);

[[nodiscard]] gnc::foundation::NumericalOutcome<ScheduledSurfaceDemand>
evaluate_scheduled_surface_demand(
    const ScheduledDemandDefinition& definition, std::int64_t tick);
[[nodiscard]] ActuatorStateObservation project_actuator_state(
    const FaultStateFragment& state);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActualSurfaceOutput>
evaluate_stateful_actuator(
    const StatefulActuatorDefinition& definition,
    const FaultStateFragment& effective_state,
    const ScheduledSurfaceDemand& demand);
[[nodiscard]] bool validate_stuck_actuator_command(
    const StatefulActuatorDefinition& definition,
    const StuckActuatorCommand& command) noexcept;
[[nodiscard]] gnc::foundation::NumericalOutcome<StuckActuatorReduction>
reduce_stuck_actuator_command(
    const StatefulActuatorDefinition& definition,
    const FaultStateFragment& prior,
    const StuckActuatorCommand& command);
[[nodiscard]] StuckActuatorEventConsumed consume_stuck_actuator_event(
    const StuckActuatorEvent& event);
[[nodiscard]] gnc::foundation::NumericalOutcome<PressureSurfaceLoad>
evaluate_pressure_surface_load(
    const PressureSurfaceLoadDefinition& definition,
    const ActualSurfaceOutput& actuator);
[[nodiscard]] VerticalPitchRigidObservation project_vertical_pitch_rigid(
    const VerticalPitchRigidState& state, std::int64_t tick);
[[nodiscard]] gnc::foundation::NumericalOutcome<VerticalPitchRigidState>
evolve_vertical_pitch_rigid(
    const VerticalPitchRigidDefinition& definition,
    const VerticalPitchRigidState& prior,
    const PressureSurfaceLoad& load);
[[nodiscard]] gnc::foundation::NumericalOutcome<ContactImpactEvaluation>
evaluate_contact_impact(
    const ContactImpactDefinition& definition,
    const VerticalPitchRigidState& state, std::int64_t tick);

[[nodiscard]] gnc::foundation::NumericalOutcome<FaultStateFragment>
build_stateful_actuator_initial_state(
    const StatefulActuatorDefinition& definition,
    const FaultStateInitialInput& input);
[[nodiscard]] gnc::foundation::NumericalOutcome<VerticalPitchRigidState>
build_vertical_pitch_rigid_initial_state(
    const VerticalPitchRigidDefinition& definition,
    const VerticalPitchRigidInitialStateInput& input);

[[nodiscard]] FaultStateFragment clone_stateful_actuator_state(
    const FaultStateFragment& state);
[[nodiscard]] bool validate_stateful_actuator_state(
    const FaultStateFragment& state) noexcept;
void swap_stateful_actuator_state(
    FaultStateFragment& lhs, FaultStateFragment& rhs) noexcept;
[[nodiscard]] const FaultStateFragmentCodec&
stateful_actuator_state_codec() noexcept;

[[nodiscard]] VerticalPitchRigidState clone_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state);
[[nodiscard]] bool validate_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state) noexcept;
void swap_vertical_pitch_rigid_state(
    VerticalPitchRigidState& lhs, VerticalPitchRigidState& rhs) noexcept;
[[nodiscard]] VerticalPitchRigidState project_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state);
[[nodiscard]] const VerticalPitchRigidStateCodec&
vertical_pitch_rigid_state_codec() noexcept;

} // namespace gnc::packages::yyz
