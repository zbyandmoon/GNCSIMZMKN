#include <yyz/scheduled_stuck_actuator.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace gnc::packages::yyz {
namespace {

using gnc::foundation::AlgorithmIdentity;
using gnc::foundation::NumericalEvidence;
using gnc::foundation::NumericalOutcome;
using gnc::foundation::NumericalStatus;
using gnc::model_sdk::CanonicalConfigBlock;
using gnc::model_sdk::CanonicalConfigValueKind;
using gnc::model_sdk::CoarsePhase;
using gnc::model_sdk::RuntimeCellProfile;
using gnc::model_sdk::RuntimeExecutionObligation;
using gnc::model_sdk::StaticEntryKind;
using gnc::model_sdk::StaticModelDescriptor;
using gnc::model_sdk::StaticPackageImplementation;
using gnc::model_sdk::StaticPortDescriptor;
using gnc::model_sdk::StaticRuntimeObligationEntryDescriptor;

constexpr AlgorithmIdentity kQualificationAlgorithm{
    "gnc.algorithm.yyz.stuck-actuator-qualification@1", "0.1.0"};

[[nodiscard]] NumericalEvidence evidence(std::string_view detail) {
    NumericalEvidence result;
    result.algorithm = kQualificationAlgorithm;
    result.detail = detail;
    return result;
}

template <typename Value>
[[nodiscard]] NumericalOutcome<Value> success(Value value,
                                              std::string_view detail) {
    return NumericalOutcome<Value>::with_value(
        NumericalStatus::Success, std::move(value), evidence(detail));
}

template <typename Value>
[[nodiscard]] NumericalOutcome<Value> failure(
    NumericalStatus status, std::string_view detail) {
    return NumericalOutcome<Value>::failure(status, evidence(detail));
}

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] const gnc::model_sdk::CanonicalConfigField* field(
    const CanonicalConfigBlock& configuration,
    std::string_view field_id) noexcept {
    const auto found = std::find_if(
        configuration.fields.begin(), configuration.fields.end(),
        [field_id](const auto& candidate) {
            return candidate.field_id == field_id;
        });
    return found == configuration.fields.end() ? nullptr : &*found;
}

[[nodiscard]] const double* floating(
    const CanonicalConfigBlock& configuration,
    std::string_view field_id) noexcept {
    const auto* value = field(configuration, field_id);
    return value == nullptr ? nullptr : std::get_if<double>(&value->value);
}

[[nodiscard]] const std::int64_t* integer(
    const CanonicalConfigBlock& configuration,
    std::string_view field_id) noexcept {
    const auto* value = field(configuration, field_id);
    return value == nullptr ? nullptr
                            : std::get_if<std::int64_t>(&value->value);
}

[[nodiscard]] bool exact_fields(
    const CanonicalConfigBlock& configuration,
    std::string_view schema_id,
    const std::vector<std::string_view>& expected) noexcept {
    if (configuration.schema_id != schema_id ||
        configuration.schema_version != 1U ||
        configuration.fields.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (configuration.fields[index].field_id != expected[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string identity(std::string_view model,
                                   std::string_view role) {
    return "gnc.package.yyz.stuck-qualification." + std::string(model) +
           "." + std::string(role) + "@1";
}

[[nodiscard]] gnc::model_sdk::StaticSlotCodecDescriptor slot_codec(
    std::string_view model, std::string_view role,
    std::string_view layout) {
    const auto prefix = identity(model, role);
    return {std::string(layout),
            prefix + ".codec",
            "1.0.0",
            prefix + ".codec-call-shape",
            prefix + ".copy",
            prefix + ".validate",
            prefix + ".project"};
}

[[nodiscard]] gnc::model_sdk::StaticRuntimeScheduleDescriptor
every_boundary() {
    return {gnc::model_sdk::StaticScheduleTrigger::EveryBoundary, 1U, 0U,
            gnc::model_sdk::HoldPolicy::ZeroOrderHold, 0U};
}

[[nodiscard]] std::vector<gnc::model_sdk::RuntimeLifecycleCapability>
lifecycle() {
    return {gnc::model_sdk::RuntimeLifecycleCapability::Instantiate,
            gnc::model_sdk::RuntimeLifecycleCapability::Resettable,
            gnc::model_sdk::RuntimeLifecycleCapability::Dispose};
}

void configure_runtime(
    gnc::model_sdk::StaticRuntimeComponentDescriptor& runtime,
    std::string_view model, RuntimeCellProfile profile) {
    runtime.recipe_id = identity(model, "recipe");
    runtime.profile = profile;
    runtime.schedule = every_boundary();
    runtime.lifecycle_capabilities = lifecycle();
    runtime.definition_builder_id = identity(model, "definition-builder");
    runtime.definition_builder_version = "1.0.0";
    runtime.definition_builder_call_shape_id =
        identity(model, "definition-builder-call-shape");
    runtime.runtime_cell_factory_id =
        identity(model, "runtime-cell-factory");
    runtime.runtime_cell_factory_version = "1.0.0";
    runtime.runtime_cell_factory_call_shape_id =
        identity(model, "runtime-cell-factory-call-shape");
    runtime.resource_plan_id = "gnc.resource-plan.none@1";
    runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
}

[[nodiscard]] StaticRuntimeObligationEntryDescriptor runtime_entry(
    std::string_view model, std::string_view role,
    RuntimeExecutionObligation obligation, CoarsePhase phase,
    std::string request, std::string result,
    std::vector<std::string> inputs,
    std::vector<std::string> outputs,
    gnc::model_sdk::StaticStateReadKind state_read =
        gnc::model_sdk::StaticStateReadKind::None,
    gnc::model_sdk::StaticStateWriteKind state_write =
        gnc::model_sdk::StaticStateWriteKind::None) {
    StaticRuntimeObligationEntryDescriptor entry;
    entry.obligation = obligation;
    entry.phase = phase;
    entry.entry_id = identity(model, role);
    entry.entry_version = "1.0.0";
    entry.request_contract_id = std::move(request);
    entry.result_contract_id = std::move(result);
    entry.workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    entry.input_port_ids = std::move(inputs);
    entry.output_port_ids = std::move(outputs);
    entry.state_read = state_read;
    entry.state_write = state_write;
    entry.call_shape_id = identity(model, std::string(role) + "-call-shape");
    return entry;
}

[[nodiscard]] gnc::model_sdk::StaticStateOwnerDescriptor actuator_owner() {
    gnc::model_sdk::StaticStateOwnerDescriptor owner;
    owner.schema = {
        std::string(kActuatorStateSchemaId), 1U,
        std::string(kActuatorStateLayoutId),
        {{"actual_position_radians", "float64", "rad",
          "actuator-owner"},
         {"fault.mode", "uint8", "1", "fault-fragment"},
         {"fault.locked_position_radians", "float64", "rad",
          "fault-fragment"},
         {"fault.revision", "uint64", "1", "fault-fragment"}}};
    owner.initial_state_builder_id =
        identity("actuator", "initial-state-builder");
    owner.initial_state_builder_version = "1.0.0";
    owner.initial_state_input_schema = {
        std::string(kActuatorInitialStateSchemaId), 1U,
        {{"actual_position_radians", CanonicalConfigValueKind::Float64}}};
    owner.evolution = gnc::model_sdk::StaticStateEvolution::InstantPatch;
    owner.initial_state_builder_call_shape_id =
        identity("actuator", "initial-state-builder-call-shape");
    owner.codec = {
        identity("actuator", "state-codec"), "1.0.0",
        identity("actuator", "state-codec-call-shape"),
        identity("actuator", "state-clone"),
        identity("actuator", "state-validate"),
        identity("actuator", "state-finite"),
        identity("actuator", "state-invariant"),
        identity("actuator", "state-swap"),
        identity("actuator", "state-project")};
    return owner;
}

[[nodiscard]] gnc::model_sdk::StaticStateOwnerDescriptor rigid_owner() {
    gnc::model_sdk::StaticStateOwnerDescriptor owner;
    owner.schema = {
        std::string(kVerticalPitchRigidStateSchemaId), 1U,
        std::string(kVerticalPitchRigidStateLayoutId),
        {{"altitude_meters", "float64", "m", "vertical-rigid"},
         {"vertical_velocity_meters_per_second", "float64", "m/s",
          "vertical-rigid"},
         {"pitch_radians", "float64", "rad", "pitch-rigid"},
         {"pitch_rate_radians_per_second", "float64", "rad/s",
          "pitch-rigid"},
         {"last_surface_position_radians", "float64", "rad",
          "surface-load"},
         {"last_lift_force_newtons", "float64", "N", "surface-load"},
         {"last_pitch_moment_newton_meters", "float64", "N*m",
          "surface-load"},
         {"revision", "uint64", "1", "vertical-rigid"}}};
    owner.initial_state_builder_id =
        identity("rigid", "initial-state-builder");
    owner.initial_state_builder_version = "1.0.0";
    owner.initial_state_input_schema = {
        std::string(kRigidInitialStateSchemaId), 1U,
        {{"altitude_meters", CanonicalConfigValueKind::Float64},
         {"pitch_radians", CanonicalConfigValueKind::Float64},
         {"pitch_rate_radians_per_second",
          CanonicalConfigValueKind::Float64},
         {"vertical_velocity_meters_per_second",
          CanonicalConfigValueKind::Float64}}};
    owner.evolution = gnc::model_sdk::StaticStateEvolution::IntervalCandidate;
    owner.initial_state_builder_call_shape_id =
        identity("rigid", "initial-state-builder-call-shape");
    owner.codec = {
        identity("rigid", "state-codec"), "1.0.0",
        identity("rigid", "state-codec-call-shape"),
        identity("rigid", "state-clone"),
        identity("rigid", "state-validate"),
        identity("rigid", "state-finite"),
        identity("rigid", "state-invariant"),
        identity("rigid", "state-swap"),
        identity("rigid", "state-project")};
    return owner;
}

[[nodiscard]] const StaticModelDescriptor* find_model(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::string_view model_id) noexcept {
    const auto found = std::find_if(
        package.models.begin(), package.models.end(),
        [model_id](const auto& model) {
            return model.definition.model_id == model_id;
        });
    return found == package.models.end() ? nullptr : &*found;
}

[[nodiscard]] const StaticPortDescriptor* find_port(
    const StaticModelDescriptor& model, std::string_view port_id) noexcept {
    const auto found = std::find_if(
        model.ports.begin(), model.ports.end(),
        [port_id](const auto& port) { return port.port_id == port_id; });
    return found == model.ports.end() ? nullptr : &*found;
}

[[nodiscard]] const StaticRuntimeObligationEntryDescriptor* find_entry(
    const StaticModelDescriptor& model,
    RuntimeExecutionObligation obligation) noexcept {
    if (!model.runtime_component.has_value()) return nullptr;
    const auto& entries = model.runtime_component->obligation_entries;
    const auto found = std::find_if(
        entries.begin(), entries.end(), [obligation](const auto& entry) {
            return entry.obligation == obligation;
        });
    return found == entries.end() ? nullptr : &*found;
}

template <typename ExpectedCallable, auto Callable>
void append_definition(StaticPackageImplementation& implementation,
                       const StaticModelDescriptor& model) {
    const auto& runtime = *model.runtime_component;
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            runtime.definition_builder_id,
            runtime.definition_builder_version,
            StaticEntryKind::DefinitionBuilder,
            gnc::model_sdk::canonical_definition_builder_signature(model),
            gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
                runtime.definition_builder_call_shape_id)));
}

template <typename ExpectedCallable, auto Callable>
void append_factory(StaticPackageImplementation& implementation,
                    const StaticModelDescriptor& model) {
    const auto& runtime = *model.runtime_component;
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            runtime.runtime_cell_factory_id,
            runtime.runtime_cell_factory_version,
            StaticEntryKind::RuntimeCellFactory,
            gnc::model_sdk::canonical_runtime_cell_factory_signature(model),
            gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
                runtime.runtime_cell_factory_call_shape_id)));
}

template <typename ExpectedCallable, auto Callable>
void append_runtime(StaticPackageImplementation& implementation,
                    const StaticModelDescriptor& model,
                    RuntimeExecutionObligation obligation,
                    StaticEntryKind kind, std::string state_layout = {},
                    const gnc::model_sdk::StaticEvaluatorHistoryWitness*
                        history = nullptr) {
    const auto* entry = find_entry(model, obligation);
    if (entry == nullptr) return;
    auto linked = gnc::model_sdk::make_static_implementation_entry<
        Callable, ExpectedCallable>(
        entry->entry_id, entry->entry_version, kind,
        gnc::model_sdk::canonical_runtime_entry_signature(model, *entry),
        gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
            entry->call_shape_id),
        std::move(state_layout));
    if (history != nullptr) {
        linked = gnc::model_sdk::with_static_evaluator_history_witness(
            std::move(linked), *history);
    }
    implementation.entries.push_back(std::move(linked));
}

template <typename ExpectedCallable, auto Callable>
void append_initial(StaticPackageImplementation& implementation,
                    const StaticModelDescriptor& model) {
    const auto& owner = *model.runtime_component->state_owner;
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            owner.initial_state_builder_id,
            owner.initial_state_builder_version,
            StaticEntryKind::InitialState,
            gnc::model_sdk::canonical_initial_state_signature(model),
            gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
                owner.initial_state_builder_call_shape_id),
            owner.schema.layout_id));
}

template <typename ExpectedCallable, auto Callable>
void append_state_codec(StaticPackageImplementation& implementation,
                        const StaticModelDescriptor& model) {
    const auto& owner = *model.runtime_component->state_owner;
    auto linked = gnc::model_sdk::make_static_implementation_entry<
        Callable, ExpectedCallable>(
        owner.codec.entry_id, owner.codec.entry_version,
        StaticEntryKind::StateCodec,
        gnc::model_sdk::canonical_state_codec_signature(model),
        gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
            owner.codec.call_shape_id),
        owner.schema.layout_id);
    linked = gnc::model_sdk::with_static_state_codec_witness(
        std::move(linked),
        {owner.schema.layout_id, owner.codec.clone_operation_id,
         owner.codec.validate_operation_id,
         owner.codec.finite_validation_operation_id,
         owner.codec.invariant_validation_operation_id,
         owner.codec.noexcept_swap_operation_id,
         owner.codec.project_operation_id, true});
    implementation.entries.push_back(std::move(linked));
}

template <typename Value>
void append_slot_codec(StaticPackageImplementation& implementation,
                       const StaticModelDescriptor& model,
                       std::string_view port_id) {
    const auto* port = find_port(model, port_id);
    if (port == nullptr || !port->slot_codec.has_value()) return;
    const auto& codec = *port->slot_codec;
    using Getter = gnc::model_sdk::InProcessCodecGetter<
        gnc::model_sdk::TypedInProcessSlotCodec<Value>>;
    auto linked = gnc::model_sdk::make_static_implementation_entry<
        &gnc::model_sdk::typed_in_process_slot_codec<Value>, Getter>(
        codec.entry_id, codec.entry_version, StaticEntryKind::SlotCodec,
        gnc::model_sdk::canonical_slot_codec_signature(model, *port),
        gnc::model_sdk::make_static_callable_contract<Getter>(
            codec.call_shape_id));
    linked = gnc::model_sdk::with_static_slot_codec_witness(
        std::move(linked),
        {port->contract_id, codec.layout_id, codec.copy_operation_id,
         codec.validate_operation_id, codec.project_operation_id});
    implementation.entries.push_back(std::move(linked));
}

template <typename RuntimeCell, typename Definition>
[[nodiscard]] NumericalOutcome<RuntimeCell> create_runtime_cell(
    const Definition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context) {
    if (context.runtime_instance_id.value == 0U ||
        context.runtime_component_handle == 0U ||
        context.resources.handle == 0U ||
        context.resources.workspace_layout_id.empty()) {
        return failure<RuntimeCell>(NumericalStatus::DomainError,
                                    "runtime cell context is invalid");
    }
    return success(RuntimeCell{definition, context},
                   "runtime cell created");
}

} // namespace

gnc::model_sdk::StaticPackageDescriptor
describe_scheduled_stuck_actuator_package() {
    gnc::model_sdk::StaticPackageDescriptor package;
    package.package_id = std::string(kStuckQualificationPackageId);
    package.package_version = std::string(kStuckQualificationPackageVersion);

    StaticModelDescriptor demand;
    demand.definition = {std::string(kScheduledSurfaceDemandModelId),
                         std::string(kStuckQualificationModelVersion),
                         gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    demand.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    demand.configuration = {
        std::string(kScheduledDemandConfigSchemaId), 1U,
        {{"first_change_tick", CanonicalConfigValueKind::Integer},
         {"first_position_radians", CanonicalConfigValueKind::Float64},
         {"initial_position_radians", CanonicalConfigValueKind::Float64},
         {"later_change_tick", CanonicalConfigValueKind::Integer},
         {"later_position_radians", CanonicalConfigValueKind::Float64}}};
    demand.ports = {
        {"surface-demand", std::string(kScheduledSurfaceDemandContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("demand", "surface-demand",
                    kScheduledSurfaceDemandLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor demand_runtime;
    configure_runtime(demand_runtime, "demand",
                      RuntimeCellProfile::SampledTransform);
    demand_runtime.obligations = {RuntimeExecutionObligation::BoundaryEvaluation};
    demand_runtime.obligation_entries = {
        runtime_entry("demand", "evaluate",
                      RuntimeExecutionObligation::BoundaryEvaluation,
                      CoarsePhase::Output,
                      "gnc.request.yyz.scheduled-demand@1",
                      std::string(kScheduledSurfaceDemandContractId), {},
                      {"surface-demand"})};
    demand.runtime_component = std::move(demand_runtime);

    StaticModelDescriptor actuator;
    actuator.definition = {std::string(kStatefulStuckActuatorModelId),
                           std::string(kStuckQualificationModelVersion),
                           gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    actuator.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    actuator.configuration = {
        std::string(kStuckActuatorConfigSchemaId), 1U,
        {{"maximum_position_radians", CanonicalConfigValueKind::Float64},
         {"minimum_position_radians", CanonicalConfigValueKind::Float64}}};
    actuator.ports = {
        {"surface-demand", std::string(kScheduledSurfaceDemandContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"actuator-state",
         std::string(kActuatorStateObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("actuator", "state-observation",
                    kActuatorStateObservationLayoutId)},
        {"actual-surface", std::string(kActualSurfaceOutputContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("actuator", "actual-surface",
                    kActualSurfaceOutputLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor actuator_runtime;
    configure_runtime(actuator_runtime, "actuator",
                      RuntimeCellProfile::DiscreteStateProcessor);
    actuator_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::BoundaryEvaluation,
        RuntimeExecutionObligation::CommandReduction,
        RuntimeExecutionObligation::EventConsumption};
    actuator_runtime.obligation_entries = {
        runtime_entry("actuator", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.actuator-state-projection@1",
                      std::string(kActuatorStateObservationContractId), {},
                      {"actuator-state"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("actuator", "evaluate",
                      RuntimeExecutionObligation::BoundaryEvaluation,
                      CoarsePhase::Output,
                      "gnc.request.yyz.stateful-actuator@1",
                      std::string(kActualSurfaceOutputContractId),
                      {"surface-demand"}, {"actual-surface"},
                      gnc::model_sdk::StaticStateReadKind::Candidate),
        runtime_entry("actuator", "reduce-stuck-command",
                      RuntimeExecutionObligation::CommandReduction,
                      CoarsePhase::Process,
                      std::string(kStuckActuatorCommandSchemaId),
                      std::string(kStuckActuatorEventSchemaId), {}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::InstantPatch),
        runtime_entry("actuator", "consume-stuck-event",
                      RuntimeExecutionObligation::EventConsumption,
                      CoarsePhase::Output,
                      std::string(kStuckActuatorEventSchemaId),
                      std::string(kStuckActuatorEventConsumedSchemaId), {}, {})};
    actuator_runtime.state_owner = actuator_owner();
    actuator.runtime_component = std::move(actuator_runtime);

    StaticModelDescriptor load;
    load.definition = {std::string(kPressureSurfaceLoadModelId),
                       std::string(kStuckQualificationModelVersion),
                       gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    load.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    load.configuration = {
        std::string(kPressureSurfaceLoadConfigSchemaId), 1U,
        {{"dynamic_pressure_pascals", CanonicalConfigValueKind::Float64},
         {"lift_coefficient_slope_per_radian",
          CanonicalConfigValueKind::Float64},
         {"moment_arm_meters", CanonicalConfigValueKind::Float64},
         {"reference_area_square_meters",
          CanonicalConfigValueKind::Float64}}};
    load.ports = {
        {"actual-surface", std::string(kActualSurfaceOutputContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"surface-load", std::string(kPressureSurfaceLoadContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         slot_codec("load", "surface-load",
                    kPressureSurfaceLoadLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor load_runtime;
    configure_runtime(load_runtime, "load",
                      RuntimeCellProfile::SampledTransform);
    load_runtime.obligations = {RuntimeExecutionObligation::BoundaryEvaluation};
    load_runtime.obligation_entries = {
        runtime_entry("load", "evaluate",
                      RuntimeExecutionObligation::BoundaryEvaluation,
                      CoarsePhase::Output,
                      std::string(kActualSurfaceOutputContractId),
                      std::string(kPressureSurfaceLoadContractId),
                      {"actual-surface"}, {"surface-load"})};
    load.runtime_component = std::move(load_runtime);

    StaticModelDescriptor rigid;
    rigid.definition = {std::string(kVerticalPitchRigidModelId),
                        std::string(kStuckQualificationModelVersion),
                        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    rigid.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    rigid.configuration = {
        std::string(kVerticalPitchRigidConfigSchemaId), 1U,
        {{"fixed_step_seconds", CanonicalConfigValueKind::Float64},
         {"gravity_meters_per_second_squared",
          CanonicalConfigValueKind::Float64},
         {"mass_kilograms", CanonicalConfigValueKind::Float64},
         {"pitch_inertia_kilogram_meters_squared",
          CanonicalConfigValueKind::Float64}}};
    rigid.ports = {
        {"surface-load", std::string(kPressureSurfaceLoadContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel},
        {"rigid-observation",
         std::string(kVerticalPitchRigidObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("rigid", "observation",
                    kVerticalPitchRigidObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor rigid_runtime;
    configure_runtime(rigid_runtime, "rigid",
                      RuntimeCellProfile::DiscreteStateProcessor);
    rigid_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::IntervalEvolution};
    rigid_runtime.obligation_entries = {
        runtime_entry("rigid", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.vertical-pitch-projection@1",
                      std::string(kVerticalPitchRigidObservationContractId), {},
                      {"rigid-observation"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("rigid", "evolve",
                      RuntimeExecutionObligation::IntervalEvolution,
                      CoarsePhase::Form,
                      std::string(kPressureSurfaceLoadContractId),
                      "gnc.result.yyz.vertical-pitch-evolution@1",
                      {"surface-load"}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::IntervalCandidate)};
    rigid_runtime.state_owner = rigid_owner();
    rigid.runtime_component = std::move(rigid_runtime);

    StaticModelDescriptor evaluator;
    evaluator.definition = {std::string(kContactImpactEvaluatorModelId),
                            std::string(kStuckQualificationModelVersion),
                            gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    evaluator.placement = gnc::model_sdk::ModelPlacement::Evaluation;
    evaluator.configuration = {
        std::string(kContactImpactConfigSchemaId), 1U,
        {{"ground_altitude_meters", CanonicalConfigValueKind::Float64}}};
    evaluator.ports = {
        {"committed-rigid-contact-sequence",
         std::string(kCommittedRigidContactSequenceContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"contact-impact-result", std::string(kContactImpactResultContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("contact", "result", kContactImpactResultLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor evaluator_runtime;
    configure_runtime(evaluator_runtime, "contact",
                      RuntimeCellProfile::Evaluator);
    evaluator_runtime.obligations = {
        RuntimeExecutionObligation::BoundaryEvaluation};
    evaluator_runtime.obligation_entries = {
        runtime_entry("contact", "evaluate",
                      RuntimeExecutionObligation::BoundaryEvaluation,
                      CoarsePhase::Evaluation,
                      std::string(kCommittedRigidContactSequenceContractId),
                      std::string(kContactImpactResultContractId),
                      {"committed-rigid-contact-sequence"},
                      {"contact-impact-result"})};
    evaluator_runtime.schedule.trigger =
        gnc::model_sdk::StaticScheduleTrigger::TerminalSequenceReady;
    evaluator_runtime.schedule.step_interval = 0U;
    evaluator_runtime.evaluator_history_shape =
        gnc::model_sdk::StaticEvaluatorHistoryShapeDescriptor{
            std::string(kCommittedRigidContactSequenceContractId),
            kRigidContactHistoryDepth,
            {{std::string(kRigidContactHistoryMemberId),
              std::string(kVerticalPitchRigidStateSchemaId),
              std::string(kVerticalPitchRigidStateLayoutId)}}};
    evaluator.runtime_component = std::move(evaluator_runtime);

    package.models = {std::move(demand), std::move(actuator),
                      std::move(load), std::move(rigid),
                      std::move(evaluator)};
    return package;
}

gnc::model_sdk::StaticPackageImplementation
describe_scheduled_stuck_actuator_implementation(
    std::string build_fingerprint) {
    const auto package = describe_scheduled_stuck_actuator_package();
    StaticPackageImplementation implementation;
    implementation.package_id = package.package_id;
    implementation.package_version = package.package_version;
    implementation.build_fingerprint = std::move(build_fingerprint);

    if (const auto* model = find_model(package, kScheduledSurfaceDemandModelId)) {
        append_definition<ScheduledDemandDefinitionBuilderCall,
                          &build_scheduled_demand_definition>(implementation,
                                                              *model);
        append_factory<ScheduledDemandRuntimeCellFactoryCall,
                       &create_scheduled_demand_runtime_cell>(implementation,
                                                              *model);
        append_runtime<ScheduledDemandCall,
                       &evaluate_scheduled_surface_demand>(
            implementation, *model,
            RuntimeExecutionObligation::BoundaryEvaluation,
            StaticEntryKind::BoundaryEvaluation);
        append_slot_codec<ScheduledSurfaceDemand>(implementation, *model,
                                                  "surface-demand");
    }
    if (const auto* model = find_model(package, kStatefulStuckActuatorModelId)) {
        append_definition<StatefulActuatorDefinitionBuilderCall,
                          &build_stateful_actuator_definition>(implementation,
                                                               *model);
        append_factory<StatefulActuatorRuntimeCellFactoryCall,
                       &create_stateful_actuator_runtime_cell>(implementation,
                                                               *model);
        append_initial<StatefulActuatorInitialStateCall,
                       &build_stateful_actuator_initial_state>(implementation,
                                                               *model);
        append_state_codec<StatefulActuatorStateCodecGetter,
                           &stateful_actuator_state_codec>(implementation,
                                                           *model);
        append_runtime<ActuatorStateProjectionCall, &project_actuator_state>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection,
            std::string(kActuatorStateLayoutId));
        append_runtime<StatefulActuatorCall, &evaluate_stateful_actuator>(
            implementation, *model,
            RuntimeExecutionObligation::BoundaryEvaluation,
            StaticEntryKind::BoundaryEvaluation,
            std::string(kActuatorStateLayoutId));
        append_runtime<StuckActuatorReductionCall,
                       &reduce_stuck_actuator_command>(
            implementation, *model,
            RuntimeExecutionObligation::CommandReduction,
            StaticEntryKind::CommandReduction,
            std::string(kActuatorStateLayoutId));
        append_runtime<StuckActuatorEventConsumptionCall,
                       &consume_stuck_actuator_event>(
            implementation, *model,
            RuntimeExecutionObligation::EventConsumption,
            StaticEntryKind::EventConsumption);
        append_slot_codec<ActuatorStateObservation>(implementation, *model,
                                                    "actuator-state");
        append_slot_codec<ActualSurfaceOutput>(implementation, *model,
                                               "actual-surface");
    }
    if (const auto* model = find_model(package, kPressureSurfaceLoadModelId)) {
        append_definition<PressureSurfaceLoadDefinitionBuilderCall,
                          &build_pressure_surface_load_definition>(
            implementation, *model);
        append_factory<PressureSurfaceLoadRuntimeCellFactoryCall,
                       &create_pressure_surface_load_runtime_cell>(
            implementation, *model);
        append_runtime<PressureSurfaceLoadCall,
                       &evaluate_pressure_surface_load>(
            implementation, *model,
            RuntimeExecutionObligation::BoundaryEvaluation,
            StaticEntryKind::BoundaryEvaluation);
        append_slot_codec<PressureSurfaceLoad>(implementation, *model,
                                               "surface-load");
    }
    if (const auto* model = find_model(package, kVerticalPitchRigidModelId)) {
        append_definition<VerticalPitchRigidDefinitionBuilderCall,
                          &build_vertical_pitch_rigid_definition>(
            implementation, *model);
        append_factory<VerticalPitchRigidRuntimeCellFactoryCall,
                       &create_vertical_pitch_rigid_runtime_cell>(
            implementation, *model);
        append_initial<VerticalPitchRigidInitialStateCall,
                       &build_vertical_pitch_rigid_initial_state>(
            implementation, *model);
        append_state_codec<VerticalPitchRigidStateCodecGetter,
                           &vertical_pitch_rigid_state_codec>(
            implementation, *model);
        append_runtime<VerticalPitchRigidProjectionCall,
                       &project_vertical_pitch_rigid>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection,
            std::string(kVerticalPitchRigidStateLayoutId));
        append_runtime<VerticalPitchRigidEvolutionCall,
                       &evolve_vertical_pitch_rigid>(
            implementation, *model,
            RuntimeExecutionObligation::IntervalEvolution,
            StaticEntryKind::IntervalEvolution,
            std::string(kVerticalPitchRigidStateLayoutId));
        append_slot_codec<VerticalPitchRigidObservation>(
            implementation, *model, "rigid-observation");
    }
    if (const auto* model = find_model(package, kContactImpactEvaluatorModelId)) {
        const gnc::model_sdk::StaticEvaluatorHistoryWitness history{
            std::string(kCommittedRigidContactSequenceContractId),
            kRigidContactHistoryDepth,
            {{std::string(kRigidContactHistoryMemberId),
              std::string(kVerticalPitchRigidStateSchemaId),
              std::string(kVerticalPitchRigidStateLayoutId)}}};
        append_definition<ContactImpactDefinitionBuilderCall,
                          &build_contact_impact_definition>(implementation,
                                                            *model);
        append_factory<ContactImpactRuntimeCellFactoryCall,
                       &create_contact_impact_runtime_cell>(implementation,
                                                            *model);
        append_runtime<ContactImpactEvaluationCall, &evaluate_contact_impact>(
            implementation, *model,
            RuntimeExecutionObligation::BoundaryEvaluation,
            StaticEntryKind::BoundaryEvaluation, {}, &history);
        append_slot_codec<ContactImpactResult>(implementation, *model,
                                               "contact-impact-result");
    }

    implementation.state_layouts = {
        {std::string(kActuatorStateLayoutId), sizeof(StatefulActuatorState),
         alignof(StatefulActuatorState)},
        {std::string(kVerticalPitchRigidStateLayoutId),
         sizeof(VerticalPitchRigidState), alignof(VerticalPitchRigidState)}};
    implementation.value_layouts = {
        {std::string(kScheduledSurfaceDemandContractId),
         sizeof(ScheduledSurfaceDemand), alignof(ScheduledSurfaceDemand),
         std::string(kScheduledSurfaceDemandLayoutId)},
        {std::string(kActuatorStateObservationContractId),
         sizeof(ActuatorStateObservation), alignof(ActuatorStateObservation),
         std::string(kActuatorStateObservationLayoutId)},
        {std::string(kActualSurfaceOutputContractId),
         sizeof(ActualSurfaceOutput), alignof(ActualSurfaceOutput),
         std::string(kActualSurfaceOutputLayoutId)},
        {std::string(kPressureSurfaceLoadContractId),
         sizeof(PressureSurfaceLoad), alignof(PressureSurfaceLoad),
         std::string(kPressureSurfaceLoadLayoutId)},
        {std::string(kVerticalPitchRigidObservationContractId),
         sizeof(VerticalPitchRigidObservation),
         alignof(VerticalPitchRigidObservation),
         std::string(kVerticalPitchRigidObservationLayoutId)},
        {std::string(kContactImpactResultContractId),
         sizeof(ContactImpactResult), alignof(ContactImpactResult),
         std::string(kContactImpactResultLayoutId)}};
    return implementation;
}

NumericalOutcome<ScheduledDemandDefinition>
build_scheduled_demand_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "first_change_tick", "first_position_radians",
        "initial_position_radians", "later_change_tick",
        "later_position_radians"};
    if (!exact_fields(configuration, kScheduledDemandConfigSchemaId, fields)) {
        return failure<ScheduledDemandDefinition>(
            NumericalStatus::DomainError,
            "scheduled demand configuration shape is invalid");
    }
    const auto* first_tick = integer(configuration, fields[0]);
    const auto* first = floating(configuration, fields[1]);
    const auto* initial = floating(configuration, fields[2]);
    const auto* later_tick = integer(configuration, fields[3]);
    const auto* later = floating(configuration, fields[4]);
    if (initial == nullptr || first == nullptr || later == nullptr ||
        first_tick == nullptr || later_tick == nullptr || !finite(*initial) ||
        !finite(*first) || !finite(*later) || *first_tick < 0 ||
        *later_tick <= *first_tick) {
        return failure<ScheduledDemandDefinition>(
            NumericalStatus::DomainError,
            "scheduled demand configuration values are invalid");
    }
    return success(ScheduledDemandDefinition{*initial, *first, *later,
                                             *first_tick, *later_tick},
                   "scheduled demand definition built");
}

NumericalOutcome<StatefulActuatorDefinition>
build_stateful_actuator_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "maximum_position_radians", "minimum_position_radians"};
    if (!exact_fields(configuration, kStuckActuatorConfigSchemaId, fields)) {
        return failure<StatefulActuatorDefinition>(
            NumericalStatus::DomainError,
            "actuator configuration shape is invalid");
    }
    const auto* maximum = floating(configuration, fields[0]);
    const auto* minimum = floating(configuration, fields[1]);
    if (minimum == nullptr || maximum == nullptr || !finite(*minimum) ||
        !finite(*maximum) || *minimum >= *maximum) {
        return failure<StatefulActuatorDefinition>(
            NumericalStatus::DomainError,
            "actuator mechanical range is invalid");
    }
    return success(StatefulActuatorDefinition{*minimum, *maximum},
                   "stateful actuator definition built");
}

NumericalOutcome<PressureSurfaceLoadDefinition>
build_pressure_surface_load_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "dynamic_pressure_pascals", "lift_coefficient_slope_per_radian",
        "moment_arm_meters", "reference_area_square_meters"};
    if (!exact_fields(configuration, kPressureSurfaceLoadConfigSchemaId,
                      fields)) {
        return failure<PressureSurfaceLoadDefinition>(
            NumericalStatus::DomainError,
            "surface load configuration shape is invalid");
    }
    const auto* pressure = floating(configuration, fields[0]);
    const auto* slope = floating(configuration, fields[1]);
    const auto* arm = floating(configuration, fields[2]);
    const auto* area = floating(configuration, fields[3]);
    if (pressure == nullptr || area == nullptr || slope == nullptr ||
        arm == nullptr || !finite(*pressure) || !finite(*area) ||
        !finite(*slope) || !finite(*arm) || *pressure <= 0.0 ||
        *area <= 0.0 || *slope <= 0.0 || *arm <= 0.0) {
        return failure<PressureSurfaceLoadDefinition>(
            NumericalStatus::DomainError,
            "surface load configuration values are invalid");
    }
    return success(PressureSurfaceLoadDefinition{*pressure, *area, *slope,
                                                 *arm},
                   "pressure surface load definition built");
}

NumericalOutcome<VerticalPitchRigidDefinition>
build_vertical_pitch_rigid_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "fixed_step_seconds", "gravity_meters_per_second_squared",
        "mass_kilograms", "pitch_inertia_kilogram_meters_squared"};
    if (!exact_fields(configuration, kVerticalPitchRigidConfigSchemaId,
                      fields)) {
        return failure<VerticalPitchRigidDefinition>(
            NumericalStatus::DomainError,
            "rigid configuration shape is invalid");
    }
    const auto* step = floating(configuration, fields[0]);
    const auto* gravity = floating(configuration, fields[1]);
    const auto* mass = floating(configuration, fields[2]);
    const auto* inertia = floating(configuration, fields[3]);
    if (mass == nullptr || inertia == nullptr || gravity == nullptr ||
        step == nullptr || !finite(*mass) || !finite(*inertia) ||
        !finite(*gravity) || !finite(*step) || *mass <= 0.0 ||
        *inertia <= 0.0 || *gravity <= 0.0 || *step <= 0.0) {
        return failure<VerticalPitchRigidDefinition>(
            NumericalStatus::DomainError,
            "rigid configuration values are invalid");
    }
    return success(VerticalPitchRigidDefinition{*mass, *inertia, *gravity,
                                                *step},
                   "vertical pitch rigid definition built");
}

NumericalOutcome<ContactImpactDefinition>
build_contact_impact_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "ground_altitude_meters"};
    if (!exact_fields(configuration, kContactImpactConfigSchemaId, fields)) {
        return failure<ContactImpactDefinition>(
            NumericalStatus::DomainError,
            "contact evaluator configuration shape is invalid");
    }
    const auto* ground = floating(configuration, fields[0]);
    if (ground == nullptr || !finite(*ground)) {
        return failure<ContactImpactDefinition>(
            NumericalStatus::DomainError,
            "contact evaluator ground altitude is invalid");
    }
    return success(ContactImpactDefinition{*ground},
                   "contact impact definition built");
}

NumericalOutcome<ScheduledDemandRuntimeCell>
create_scheduled_demand_runtime_cell(
    const ScheduledDemandDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings&) {
    return create_runtime_cell<ScheduledDemandRuntimeCell>(definition,
                                                            context);
}

NumericalOutcome<StatefulActuatorRuntimeCell>
create_stateful_actuator_runtime_cell(
    const StatefulActuatorDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings&) {
    return create_runtime_cell<StatefulActuatorRuntimeCell>(definition,
                                                             context);
}

NumericalOutcome<PressureSurfaceLoadRuntimeCell>
create_pressure_surface_load_runtime_cell(
    const PressureSurfaceLoadDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings&) {
    return create_runtime_cell<PressureSurfaceLoadRuntimeCell>(definition,
                                                                context);
}

NumericalOutcome<VerticalPitchRigidRuntimeCell>
create_vertical_pitch_rigid_runtime_cell(
    const VerticalPitchRigidDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings&) {
    return create_runtime_cell<VerticalPitchRigidRuntimeCell>(definition,
                                                               context);
}

NumericalOutcome<ContactImpactRuntimeCell>
create_contact_impact_runtime_cell(
    const ContactImpactDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const QualificationRuntimeCellBindings&) {
    return create_runtime_cell<ContactImpactRuntimeCell>(definition, context);
}

NumericalOutcome<ScheduledSurfaceDemand>
evaluate_scheduled_surface_demand(
    const ScheduledDemandDefinition& definition, std::int64_t tick) {
    if (tick < 0) {
        return failure<ScheduledSurfaceDemand>(
            NumericalStatus::DomainError, "demand tick is negative");
    }
    const double position =
        tick >= definition.later_change_tick
            ? definition.later_position_radians
            : (tick >= definition.first_change_tick
                   ? definition.first_position_radians
                   : definition.initial_position_radians);
    if (!finite(position)) {
        return failure<ScheduledSurfaceDemand>(
            NumericalStatus::NonFiniteOutput,
            "scheduled demand produced a non-finite position");
    }
    return success(ScheduledSurfaceDemand{tick, position},
                   "scheduled surface demand evaluated");
}

ActuatorStateObservation project_actuator_state(
    const StatefulActuatorState& state) {
    return {state.actual_position_radians, state.fault.mode,
            state.fault.locked_position_radians, state.fault.revision};
}

NumericalOutcome<ActualSurfaceOutput> evaluate_stateful_actuator(
    const StatefulActuatorDefinition& definition,
    const StatefulActuatorState& effective_state,
    const ScheduledSurfaceDemand& demand) {
    if (!validate_stateful_actuator_state(effective_state) ||
        !finite(demand.demanded_position_radians) || demand.tick < 0) {
        return failure<ActualSurfaceOutput>(
            NumericalStatus::DomainError,
            "actuator state or demand is invalid");
    }
    const double actual =
        effective_state.fault.mode == ActuatorFaultMode::Stuck
            ? effective_state.fault.locked_position_radians
            : std::clamp(demand.demanded_position_radians,
                         definition.minimum_position_radians,
                         definition.maximum_position_radians);
    if (!finite(actual) || actual < definition.minimum_position_radians ||
        actual > definition.maximum_position_radians) {
        return failure<ActualSurfaceOutput>(
            NumericalStatus::OutOfRange,
            "actuator output is outside its mechanical range");
    }
    return success(ActualSurfaceOutput{
                       demand.tick, demand.demanded_position_radians, actual,
                       effective_state.fault.mode,
                       effective_state.fault.revision},
                   "stateful actuator evaluated");
}

bool validate_stuck_actuator_command(
    const StatefulActuatorDefinition& definition,
    const StuckActuatorCommand& command) noexcept {
    return finite(command.locked_position_radians) &&
           command.locked_position_radians >=
               definition.minimum_position_radians &&
           command.locked_position_radians <=
               definition.maximum_position_radians;
}

NumericalOutcome<StuckActuatorReduction> reduce_stuck_actuator_command(
    const StatefulActuatorDefinition& definition,
    const StatefulActuatorState& prior,
    const StuckActuatorCommand& command) {
    if (!validate_stateful_actuator_state(prior) ||
        !validate_stuck_actuator_command(definition, command) ||
        prior.fault.mode != ActuatorFaultMode::Healthy) {
        return failure<StuckActuatorReduction>(
            NumericalStatus::DomainError,
            "stuck command cannot be applied to the actuator state");
    }
    StatefulActuatorState candidate = prior;
    candidate.actual_position_radians = command.locked_position_radians;
    candidate.fault.mode = ActuatorFaultMode::Stuck;
    candidate.fault.locked_position_radians =
        command.locked_position_radians;
    ++candidate.fault.revision;
    return success(
        StuckActuatorReduction{
            candidate,
            {prior.fault.mode, candidate.fault.mode,
             command.locked_position_radians, candidate.fault.revision}},
        "stuck command reduced to a complete actuator state replacement");
}

StuckActuatorEventConsumed consume_stuck_actuator_event(
    const StuckActuatorEvent& event) {
    return {event.committed_mode, event.locked_position_radians,
            event.revision};
}

NumericalOutcome<PressureSurfaceLoad> evaluate_pressure_surface_load(
    const PressureSurfaceLoadDefinition& definition,
    const ActualSurfaceOutput& actuator) {
    if (!finite(actuator.actual_position_radians) || actuator.tick < 0) {
        return failure<PressureSurfaceLoad>(
            NumericalStatus::DomainError,
            "surface load actuator input is invalid");
    }
    const double lift = definition.dynamic_pressure_pascals *
                        definition.reference_area_square_meters *
                        definition.lift_coefficient_slope_per_radian *
                        actuator.actual_position_radians;
    const double moment = lift * definition.moment_arm_meters;
    if (!finite(lift) || !finite(moment)) {
        return failure<PressureSurfaceLoad>(
            NumericalStatus::NonFiniteOutput,
            "pressure surface load is non-finite");
    }
    return success(PressureSurfaceLoad{actuator.tick,
                                       actuator.actual_position_radians,
                                       lift, moment},
                   "pressure surface load evaluated");
}

VerticalPitchRigidObservation project_vertical_pitch_rigid(
    const VerticalPitchRigidState& state, std::int64_t tick) {
    return {tick, state};
}

NumericalOutcome<VerticalPitchRigidState> evolve_vertical_pitch_rigid(
    const VerticalPitchRigidDefinition& definition,
    const VerticalPitchRigidState& prior,
    const PressureSurfaceLoad& load) {
    if (!validate_vertical_pitch_rigid_state(prior) ||
        !finite(load.lift_force_newtons) ||
        !finite(load.pitch_moment_newton_meters)) {
        return failure<VerticalPitchRigidState>(
            NumericalStatus::DomainError,
            "rigid evolution input is invalid");
    }
    const double vertical_acceleration =
        load.lift_force_newtons / definition.mass_kilograms -
        definition.gravity_meters_per_second_squared;
    const double pitch_acceleration =
        load.pitch_moment_newton_meters /
        definition.pitch_inertia_kilogram_meters_squared;
    VerticalPitchRigidState next = prior;
    next.vertical_velocity_meters_per_second +=
        vertical_acceleration * definition.fixed_step_seconds;
    next.altitude_meters += next.vertical_velocity_meters_per_second *
                            definition.fixed_step_seconds;
    next.pitch_rate_radians_per_second +=
        pitch_acceleration * definition.fixed_step_seconds;
    next.pitch_radians += next.pitch_rate_radians_per_second *
                          definition.fixed_step_seconds;
    next.last_surface_position_radians = load.actual_position_radians;
    next.last_lift_force_newtons = load.lift_force_newtons;
    next.last_pitch_moment_newton_meters =
        load.pitch_moment_newton_meters;
    ++next.revision;
    if (!validate_vertical_pitch_rigid_state(next)) {
        return failure<VerticalPitchRigidState>(
            NumericalStatus::NonFiniteOutput,
            "rigid evolution produced an invalid state");
    }
    return success(std::move(next), "vertical pitch rigid state evolved");
}

NumericalOutcome<ContactImpactResult> evaluate_contact_impact(
    const ContactImpactDefinition& definition,
    const VerticalPitchRigidState& state, std::int64_t tick) {
    if (!validate_vertical_pitch_rigid_state(state) || tick < 0 ||
        !finite(definition.ground_altitude_meters)) {
        return failure<ContactImpactResult>(
            NumericalStatus::DomainError,
            "contact impact evaluation input is invalid");
    }
    const bool impact =
        state.altitude_meters <= definition.ground_altitude_meters;
    return success(ContactImpactResult{
                       tick, impact, state.altitude_meters,
                       state.vertical_velocity_meters_per_second,
                       state.pitch_radians,
                       impact ? "ground-impact" : "clearance-maintained"},
                   "contact impact evaluated");
}

NumericalOutcome<StatefulActuatorState>
build_stateful_actuator_initial_state(
    const StatefulActuatorDefinition& definition,
    const StatefulActuatorInitialStateInput& input) {
    if (!finite(input.actual_position_radians) ||
        input.actual_position_radians < definition.minimum_position_radians ||
        input.actual_position_radians > definition.maximum_position_radians) {
        return failure<StatefulActuatorState>(
            NumericalStatus::OutOfRange,
            "initial actuator position is outside the mechanical range");
    }
    return success(StatefulActuatorState{
                       input.actual_position_radians,
                       {ActuatorFaultMode::Healthy,
                        input.actual_position_radians, 0U}},
                   "healthy actuator initial state built");
}

NumericalOutcome<VerticalPitchRigidState>
build_vertical_pitch_rigid_initial_state(
    const VerticalPitchRigidDefinition&,
    const VerticalPitchRigidInitialStateInput& input) {
    VerticalPitchRigidState state;
    state.altitude_meters = input.altitude_meters;
    state.vertical_velocity_meters_per_second =
        input.vertical_velocity_meters_per_second;
    state.pitch_radians = input.pitch_radians;
    state.pitch_rate_radians_per_second =
        input.pitch_rate_radians_per_second;
    if (!validate_vertical_pitch_rigid_state(state)) {
        return failure<VerticalPitchRigidState>(
            NumericalStatus::DomainError,
            "initial rigid state is invalid");
    }
    return success(std::move(state), "vertical pitch rigid initial state built");
}

StatefulActuatorState clone_stateful_actuator_state(
    const StatefulActuatorState& state) {
    return state;
}

bool validate_stateful_actuator_state(
    const StatefulActuatorState& state) noexcept {
    return finite(state.actual_position_radians) &&
           finite(state.fault.locked_position_radians) &&
           (state.fault.mode == ActuatorFaultMode::Healthy ||
            state.fault.mode == ActuatorFaultMode::Stuck) &&
           (state.fault.mode != ActuatorFaultMode::Stuck ||
            state.actual_position_radians ==
                state.fault.locked_position_radians);
}

void swap_stateful_actuator_state(
    StatefulActuatorState& lhs, StatefulActuatorState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const StatefulActuatorStateCodec& stateful_actuator_state_codec() noexcept {
    static const StatefulActuatorStateCodec codec{
        &clone_stateful_actuator_state, &validate_stateful_actuator_state,
        &validate_stateful_actuator_state, &validate_stateful_actuator_state,
        &swap_stateful_actuator_state, &project_actuator_state};
    return codec;
}

VerticalPitchRigidState clone_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state) {
    return state;
}

bool validate_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state) noexcept {
    return finite(state.altitude_meters) &&
           finite(state.vertical_velocity_meters_per_second) &&
           finite(state.pitch_radians) &&
           finite(state.pitch_rate_radians_per_second) &&
           finite(state.last_surface_position_radians) &&
           finite(state.last_lift_force_newtons) &&
           finite(state.last_pitch_moment_newton_meters);
}

void swap_vertical_pitch_rigid_state(
    VerticalPitchRigidState& lhs, VerticalPitchRigidState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

VerticalPitchRigidState project_vertical_pitch_rigid_state(
    const VerticalPitchRigidState& state) {
    return state;
}

const VerticalPitchRigidStateCodec&
vertical_pitch_rigid_state_codec() noexcept {
    static const VerticalPitchRigidStateCodec codec{
        &clone_vertical_pitch_rigid_state,
        &validate_vertical_pitch_rigid_state,
        &validate_vertical_pitch_rigid_state,
        &validate_vertical_pitch_rigid_state,
        &swap_vertical_pitch_rigid_state,
        &project_vertical_pitch_rigid_state};
    return codec;
}

} // namespace gnc::packages::yyz
