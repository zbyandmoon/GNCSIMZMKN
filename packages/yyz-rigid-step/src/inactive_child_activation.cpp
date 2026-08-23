#include <yyz/inactive_child_activation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

constexpr AlgorithmIdentity kActivationAlgorithm{
    "gnc.algorithm.yyz.inactive-child-activation-qualification@1",
    "0.1.0"};

[[nodiscard]] NumericalEvidence evidence(std::string_view detail) {
    NumericalEvidence result;
    result.algorithm = kActivationAlgorithm;
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
    return "gnc.package.yyz.inactive-child." + std::string(model) + "." +
           std::string(role) + "@1";
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
    runtime.runtime_cell_factory_id = identity(model, "runtime-cell-factory");
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

[[nodiscard]] gnc::model_sdk::StaticStateOwnerDescriptor state_owner(
    std::string_view model, std::string_view schema_id,
    std::string_view layout_id,
    std::vector<gnc::model_sdk::StaticStateFieldDescriptor> fields,
    std::string_view initial_schema,
    std::vector<gnc::model_sdk::StaticConfigFieldDescriptor> initial_fields) {
    gnc::model_sdk::StaticStateOwnerDescriptor owner;
    owner.schema = {std::string(schema_id), 1U, std::string(layout_id),
                    std::move(fields)};
    owner.initial_state_builder_id = identity(model, "initial-state-builder");
    owner.initial_state_builder_version = "1.0.0";
    owner.initial_state_input_schema = {
        std::string(initial_schema), 1U, std::move(initial_fields)};
    owner.evolution = gnc::model_sdk::StaticStateEvolution::InstantPatch;
    owner.initial_state_builder_call_shape_id =
        identity(model, "initial-state-builder-call-shape");
    owner.codec = {
        identity(model, "state-codec"), "1.0.0",
        identity(model, "state-codec-call-shape"),
        identity(model, "state-clone"),
        identity(model, "state-validate"),
        identity(model, "state-finite"),
        identity(model, "state-invariant"),
        identity(model, "state-swap"),
        identity(model, "state-project")};
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
                    StaticEntryKind kind) {
    const auto* entry = find_entry(model, obligation);
    if (entry == nullptr) return;
    const auto state_layout =
        model.runtime_component->state_owner.has_value() &&
                (entry->state_read !=
                     gnc::model_sdk::StaticStateReadKind::None ||
                 entry->state_write !=
                     gnc::model_sdk::StaticStateWriteKind::None)
            ? model.runtime_component->state_owner->schema.layout_id
            : std::string{};
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            entry->entry_id, entry->entry_version, kind,
            gnc::model_sdk::canonical_runtime_entry_signature(model, *entry),
            gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
                entry->call_shape_id),
            state_layout));
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
                                    "runtime cell context is incomplete");
    }
    return success(RuntimeCell{definition, context},
                   "inactive-child runtime cell contract materialized");
}

} // namespace

gnc::model_sdk::StaticPackageDescriptor
describe_inactive_child_activation_package() {
    gnc::model_sdk::StaticPackageDescriptor package;
    package.package_id = std::string(kInactiveChildActivationPackageId);
    package.package_version =
        std::string(kInactiveChildActivationPackageVersion);

    StaticModelDescriptor relationship;
    relationship.definition = {
        std::string(kActivationRelationshipModelId),
        std::string(kInactiveChildActivationModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    relationship.placement = gnc::model_sdk::ModelPlacement::VehicleProcess;
    relationship.configuration = {
        std::string(kActivationRelationshipConfigSchemaId), 1U,
        {{"exact_transfer_mass", CanonicalConfigValueKind::Float64}}};
    relationship.ports = {
        {"relationship-observation",
         std::string(kActivationRelationshipObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("relationship", "observation",
                    kActivationRelationshipObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor relationship_runtime;
    configure_runtime(relationship_runtime, "relationship",
                      RuntimeCellProfile::ModeOwner);
    relationship_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::CommandReduction,
        RuntimeExecutionObligation::EventConsumption};
    relationship_runtime.obligation_entries = {
        runtime_entry(
            "relationship", "publish",
            RuntimeExecutionObligation::PublishProjection,
            CoarsePhase::Publish,
            "gnc.request.yyz.activation-relationship-projection@1",
            std::string(kActivationRelationshipObservationContractId), {},
            {"relationship-observation"},
            gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry(
            "relationship", "reduce",
            RuntimeExecutionObligation::CommandReduction,
            CoarsePhase::Process, std::string(kActivateChildCommandSchemaId),
            std::string(kActivationRequestedEventSchemaId), {}, {},
            gnc::model_sdk::StaticStateReadKind::Committed,
            gnc::model_sdk::StaticStateWriteKind::InstantPatch),
        runtime_entry(
            "relationship", "consume-audit",
            RuntimeExecutionObligation::EventConsumption,
            CoarsePhase::Output, std::string(kActivationAuditEventSchemaId),
            std::string(kActivationAuditConsumedSchemaId), {}, {})};
    relationship_runtime.state_owner = state_owner(
        "relationship", kActivationRelationshipStateSchemaId,
        kActivationRelationshipStateLayoutId,
        {{"child_active", "bool", "1", "relationship"},
         {"transferred_mass", "float64", "kg", "relationship"},
         {"relationship_revision", "uint64", "1", "relationship"}},
        kActivationRelationshipInitialSchemaId,
        {{"child_active", CanonicalConfigValueKind::Integer},
         {"relationship_revision", CanonicalConfigValueKind::Integer},
         {"transferred_mass", CanonicalConfigValueKind::Float64}});
    relationship.runtime_component = std::move(relationship_runtime);

    StaticModelDescriptor parent;
    parent.definition = {
        std::string(kActivationParentModelId),
        std::string(kInactiveChildActivationModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    parent.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    parent.configuration = {
        std::string(kActivationParentConfigSchemaId), 1U,
        {{"child_inertia_per_mass", CanonicalConfigValueKind::Float64},
         {"child_position_meters", CanonicalConfigValueKind::Float64},
         {"child_velocity_meters_per_second",
          CanonicalConfigValueKind::Float64}}};
    parent.ports = {
        {"parent-observation",
         std::string(kActivationParentObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("parent", "observation",
                    kActivationParentObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor parent_runtime;
    configure_runtime(parent_runtime, "parent",
                      RuntimeCellProfile::DiscreteStateProcessor);
    parent_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::EventConsumption};
    parent_runtime.obligation_entries = {
        runtime_entry("parent", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.activation-parent-projection@1",
                      std::string(kActivationParentObservationContractId), {},
                      {"parent-observation"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("parent", "map-activation",
                      RuntimeExecutionObligation::EventConsumption,
                      CoarsePhase::Output,
                      std::string(kActivationRequestedEventSchemaId),
                      std::string(kActivationParentMappedEventSchemaId), {},
                      {}, gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::InstantPatch)};
    parent_runtime.state_owner = state_owner(
        "parent", kActivationParentStateSchemaId,
        kActivationParentStateLayoutId,
        {{"available_mass", "float64", "kg", "parent"},
         {"transferred_mass", "float64", "kg", "parent"},
         {"revision", "uint64", "1", "parent"}},
        kActivationParentInitialSchemaId,
        {{"available_mass", CanonicalConfigValueKind::Float64},
         {"revision", CanonicalConfigValueKind::Integer},
         {"transferred_mass", CanonicalConfigValueKind::Float64}});
    parent.runtime_component = std::move(parent_runtime);

    StaticModelDescriptor child;
    child.definition = {
        std::string(kActivationChildModelId),
        std::string(kInactiveChildActivationModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    child.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    child.configuration = {
        std::string(kActivationChildConfigSchemaId), 1U,
        {{"expected_inertia_per_mass", CanonicalConfigValueKind::Float64}}};
    child.ports = {
        {"child-observation",
         std::string(kActivationChildObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("child", "observation",
                    kActivationChildObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor child_runtime;
    configure_runtime(child_runtime, "child",
                      RuntimeCellProfile::DiscreteStateProcessor);
    child_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::EventConsumption};
    child_runtime.obligation_entries = {
        runtime_entry("child", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.activation-child-projection@1",
                      std::string(kActivationChildObservationContractId), {},
                      {"child-observation"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("child", "map-activation",
                      RuntimeExecutionObligation::EventConsumption,
                      CoarsePhase::Output,
                      std::string(kActivationParentMappedEventSchemaId),
                      std::string(kActivationCompletedEventSchemaId), {}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::InstantPatch)};
    child_runtime.state_owner = state_owner(
        "child", kActivationChildStateSchemaId,
        kActivationChildStateLayoutId,
        {{"initialized", "bool", "1", "child"},
         {"position_meters", "float64", "m", "child"},
         {"velocity_meters_per_second", "float64", "m/s", "child"},
         {"mass", "float64", "kg", "child"},
         {"inertia", "float64", "kg*m^2", "child"},
         {"revision", "uint64", "1", "child"}},
        kActivationChildInitialSchemaId,
        {{"inertia", CanonicalConfigValueKind::Float64},
         {"initialized", CanonicalConfigValueKind::Integer},
         {"mass", CanonicalConfigValueKind::Float64},
         {"position_meters", CanonicalConfigValueKind::Float64},
         {"revision", CanonicalConfigValueKind::Integer},
         {"velocity_meters_per_second",
          CanonicalConfigValueKind::Float64}});
    child.runtime_component = std::move(child_runtime);

    StaticModelDescriptor consumer;
    consumer.definition = {
        std::string(kActivationChildConsumerModelId),
        std::string(kInactiveChildActivationModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    consumer.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    consumer.configuration = {
        std::string(kActivationChildConsumerConfigSchemaId), 1U,
        {{"influence_gain", CanonicalConfigValueKind::Float64}}};
    consumer.ports = {
        {"child-observation",
         std::string(kActivationChildObservationContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"child-influence", std::string(kActivationChildInfluenceContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("consumer", "influence",
                    kActivationChildInfluenceLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor consumer_runtime;
    configure_runtime(consumer_runtime, "consumer",
                      RuntimeCellProfile::SampledTransform);
    consumer_runtime.obligations = {
        RuntimeExecutionObligation::BoundaryEvaluation};
    consumer_runtime.obligation_entries = {
        runtime_entry("consumer", "evaluate",
                      RuntimeExecutionObligation::BoundaryEvaluation,
                      CoarsePhase::Output,
                      std::string(kActivationChildObservationContractId),
                      std::string(kActivationChildInfluenceContractId),
                      {"child-observation"}, {"child-influence"})};
    consumer.runtime_component = std::move(consumer_runtime);

    package.models = {std::move(relationship), std::move(parent),
                      std::move(child), std::move(consumer)};
    return package;
}

gnc::model_sdk::StaticPackageImplementation
describe_inactive_child_activation_implementation(
    std::string build_fingerprint) {
    const auto package = describe_inactive_child_activation_package();
    StaticPackageImplementation implementation;
    implementation.package_id = package.package_id;
    implementation.package_version = package.package_version;
    implementation.build_fingerprint = std::move(build_fingerprint);

    if (const auto* model =
            find_model(package, kActivationRelationshipModelId)) {
        append_definition<ActivationRelationshipDefinitionBuilderCall,
                          &build_activation_relationship_definition>(
            implementation, *model);
        append_factory<ActivationRelationshipRuntimeCellFactoryCall,
                       &create_activation_relationship_runtime_cell>(
            implementation, *model);
        append_initial<ActivationRelationshipInitialStateCall,
                       &build_activation_relationship_initial_state>(
            implementation, *model);
        append_state_codec<ActivationRelationshipStateCodecGetter,
                           &activation_relationship_state_codec>(
            implementation, *model);
        append_runtime<ActivationRelationshipProjectionCall,
                       &project_activation_relationship>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<ActivationRelationshipReductionCall,
                       &reduce_activate_child>(
            implementation, *model,
            RuntimeExecutionObligation::CommandReduction,
            StaticEntryKind::CommandReduction);
        append_runtime<ActivationRelationshipEventConsumptionCall,
                       &consume_activation_audit>(
            implementation, *model,
            RuntimeExecutionObligation::EventConsumption,
            StaticEntryKind::EventConsumption);
        append_slot_codec<ActivationRelationshipObservation>(
            implementation, *model, "relationship-observation");
    }
    if (const auto* model = find_model(package, kActivationParentModelId)) {
        append_definition<ActivationParentDefinitionBuilderCall,
                          &build_activation_parent_definition>(
            implementation, *model);
        append_factory<ActivationParentRuntimeCellFactoryCall,
                       &create_activation_parent_runtime_cell>(
            implementation, *model);
        append_initial<ActivationParentInitialStateCall,
                       &build_activation_parent_initial_state>(
            implementation, *model);
        append_state_codec<ActivationParentStateCodecGetter,
                           &activation_parent_state_codec>(
            implementation, *model);
        append_runtime<ActivationParentProjectionCall,
                       &project_activation_parent>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<ActivationParentMappingCall, &map_activation_parent>(
            implementation, *model,
            RuntimeExecutionObligation::EventConsumption,
            StaticEntryKind::EventConsumption);
        append_slot_codec<ActivationParentObservation>(
            implementation, *model, "parent-observation");
    }
    if (const auto* model = find_model(package, kActivationChildModelId)) {
        append_definition<ActivationChildDefinitionBuilderCall,
                          &build_activation_child_definition>(
            implementation, *model);
        append_factory<ActivationChildRuntimeCellFactoryCall,
                       &create_activation_child_runtime_cell>(
            implementation, *model);
        append_initial<ActivationChildInitialStateCall,
                       &build_activation_child_initial_state>(
            implementation, *model);
        append_state_codec<ActivationChildStateCodecGetter,
                           &activation_child_state_codec>(
            implementation, *model);
        append_runtime<ActivationChildProjectionCall,
                       &project_activation_child>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<ActivationChildMappingCall, &map_activation_child>(
            implementation, *model,
            RuntimeExecutionObligation::EventConsumption,
            StaticEntryKind::EventConsumption);
        append_slot_codec<ActivationChildObservation>(
            implementation, *model, "child-observation");
    }
    if (const auto* model =
            find_model(package, kActivationChildConsumerModelId)) {
        append_definition<ActivationChildConsumerDefinitionBuilderCall,
                          &build_activation_child_consumer_definition>(
            implementation, *model);
        append_factory<ActivationChildConsumerRuntimeCellFactoryCall,
                       &create_activation_child_consumer_runtime_cell>(
            implementation, *model);
        append_runtime<ActivationChildConsumerCall,
                       &evaluate_activation_child_consumer>(
            implementation, *model,
            RuntimeExecutionObligation::BoundaryEvaluation,
            StaticEntryKind::BoundaryEvaluation);
        append_slot_codec<ActivationChildInfluence>(
            implementation, *model, "child-influence");
    }

    implementation.state_layouts = {
        {std::string(kActivationRelationshipStateLayoutId),
         sizeof(ActivationRelationshipState),
         alignof(ActivationRelationshipState)},
        {std::string(kActivationParentStateLayoutId),
         sizeof(ActivationParentState), alignof(ActivationParentState)},
        {std::string(kActivationChildStateLayoutId),
         sizeof(ActivationChildState), alignof(ActivationChildState)}};
    implementation.value_layouts = {
        {std::string(kActivationRelationshipObservationContractId),
         sizeof(ActivationRelationshipObservation),
         alignof(ActivationRelationshipObservation),
         std::string(kActivationRelationshipObservationLayoutId)},
        {std::string(kActivationParentObservationContractId),
         sizeof(ActivationParentObservation),
         alignof(ActivationParentObservation),
         std::string(kActivationParentObservationLayoutId)},
        {std::string(kActivationChildObservationContractId),
         sizeof(ActivationChildObservation),
         alignof(ActivationChildObservation),
         std::string(kActivationChildObservationLayoutId)},
        {std::string(kActivationChildInfluenceContractId),
         sizeof(ActivationChildInfluence),
         alignof(ActivationChildInfluence),
         std::string(kActivationChildInfluenceLayoutId)}};
    return implementation;
}

NumericalOutcome<ActivationRelationshipDefinition>
build_activation_relationship_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "exact_transfer_mass"};
    const auto* mass = floating(configuration, fields[0]);
    if (!exact_fields(configuration, kActivationRelationshipConfigSchemaId,
                      fields) ||
        mass == nullptr || !finite(*mass) || *mass <= 0.0) {
        return failure<ActivationRelationshipDefinition>(
            NumericalStatus::DomainError,
            "activation relationship configuration is invalid");
    }
    return success(ActivationRelationshipDefinition{*mass},
                   "activation relationship definition built");
}

NumericalOutcome<ActivationParentDefinition>
build_activation_parent_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "child_inertia_per_mass", "child_position_meters",
        "child_velocity_meters_per_second"};
    const auto* inertia = floating(configuration, fields[0]);
    const auto* position = floating(configuration, fields[1]);
    const auto* velocity = floating(configuration, fields[2]);
    if (!exact_fields(configuration, kActivationParentConfigSchemaId,
                      fields) ||
        inertia == nullptr || position == nullptr || velocity == nullptr ||
        !finite(*inertia) || !finite(*position) || !finite(*velocity) ||
        *inertia <= 0.0) {
        return failure<ActivationParentDefinition>(
            NumericalStatus::DomainError,
            "activation parent configuration is invalid");
    }
    return success(ActivationParentDefinition{*position, *velocity, *inertia},
                   "activation parent definition built");
}

NumericalOutcome<ActivationChildDefinition>
build_activation_child_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "expected_inertia_per_mass"};
    const auto* inertia = floating(configuration, fields[0]);
    if (!exact_fields(configuration, kActivationChildConfigSchemaId,
                      fields) ||
        inertia == nullptr || !finite(*inertia) || *inertia <= 0.0) {
        return failure<ActivationChildDefinition>(
            NumericalStatus::DomainError,
            "activation child configuration is invalid");
    }
    return success(ActivationChildDefinition{*inertia},
                   "activation child definition built");
}

NumericalOutcome<ActivationChildConsumerDefinition>
build_activation_child_consumer_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{"influence_gain"};
    const auto* gain = floating(configuration, fields[0]);
    if (!exact_fields(configuration, kActivationChildConsumerConfigSchemaId,
                      fields) ||
        gain == nullptr || !finite(*gain)) {
        return failure<ActivationChildConsumerDefinition>(
            NumericalStatus::DomainError,
            "activation child consumer configuration is invalid");
    }
    return success(ActivationChildConsumerDefinition{*gain},
                   "activation child consumer definition built");
}

NumericalOutcome<ActivationRelationshipRuntimeCell>
create_activation_relationship_runtime_cell(
    const ActivationRelationshipDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings&) {
    return create_runtime_cell<ActivationRelationshipRuntimeCell>(definition,
                                                                  context);
}

NumericalOutcome<ActivationParentRuntimeCell>
create_activation_parent_runtime_cell(
    const ActivationParentDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings&) {
    return create_runtime_cell<ActivationParentRuntimeCell>(definition,
                                                            context);
}

NumericalOutcome<ActivationChildRuntimeCell>
create_activation_child_runtime_cell(
    const ActivationChildDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings&) {
    return create_runtime_cell<ActivationChildRuntimeCell>(definition,
                                                           context);
}

NumericalOutcome<ActivationChildConsumerRuntimeCell>
create_activation_child_consumer_runtime_cell(
    const ActivationChildConsumerDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings&) {
    return create_runtime_cell<ActivationChildConsumerRuntimeCell>(definition,
                                                                   context);
}

ActivationRelationshipObservation project_activation_relationship(
    const ActivationRelationshipState& state, std::int64_t) {
    return {state.child_active, state.transferred_mass,
            state.relationship_revision};
}

ActivationParentObservation project_activation_parent(
    const ActivationParentState& state, std::int64_t) {
    return {state.available_mass, state.transferred_mass, state.revision};
}

ActivationChildObservation project_activation_child(
    const ActivationChildState& state, std::int64_t) {
    return {state.initialized,
            state.position_meters,
            state.velocity_meters_per_second,
            state.mass,
            state.inertia,
            state.revision};
}

NumericalOutcome<ActivationRelationshipReduction> reduce_activate_child(
    const ActivationRelationshipDefinition& definition,
    const ActivationRelationshipState& prior,
    const ActivateChildCommand& command) {
    if (!validate_activation_relationship_state(prior) ||
        prior.child_active || !finite(command.transfer_mass) ||
        command.transfer_mass != definition.exact_transfer_mass ||
        prior.relationship_revision ==
            (std::numeric_limits<std::uint64_t>::max)()) {
        return failure<ActivationRelationshipReduction>(
            NumericalStatus::DomainError,
            "activation command or relationship state is invalid");
    }
    ActivationRelationshipReduction result;
    result.candidate = {true, command.transfer_mass,
                        prior.relationship_revision + 1U};
    result.event = {command.transfer_mass,
                    result.candidate.relationship_revision};
    return success(result, "activation relationship replacement produced");
}

NumericalOutcome<ActivationAuditConsumed> consume_activation_audit(
    const ActivationRelationshipDefinition&,
    const ActivationRelationshipState& state,
    const ActivationAuditEvent& event) {
    if (!validate_activation_relationship_state(state) ||
        event.relationship_revision != state.relationship_revision) {
        return failure<ActivationAuditConsumed>(
            NumericalStatus::DomainError,
            "activation audit event is inconsistent");
    }
    return success(ActivationAuditConsumed{event.relationship_revision},
                   "activation audit consumed");
}

NumericalOutcome<ActivationParentMapping> map_activation_parent(
    const ActivationParentDefinition& definition,
    const ActivationParentState& prior,
    const ActivationRequested& request) {
    if (!validate_activation_parent_state(prior) ||
        !finite(request.transfer_mass) || request.transfer_mass <= 0.0 ||
        request.transfer_mass > prior.available_mass ||
        request.relationship_revision == 0U ||
        prior.revision == (std::numeric_limits<std::uint64_t>::max)()) {
        return failure<ActivationParentMapping>(
            NumericalStatus::DomainError,
            "activation parent mapping input is invalid");
    }
    ActivationParentMapping result;
    result.candidate = prior;
    result.candidate.available_mass -= request.transfer_mass;
    result.candidate.transferred_mass += request.transfer_mass;
    ++result.candidate.revision;
    result.event = {
        definition.child_position_meters,
        definition.child_velocity_meters_per_second,
        request.transfer_mass,
        request.transfer_mass * definition.child_inertia_per_mass,
        result.candidate.revision,
        request.relationship_revision};
    if (!validate_activation_parent_state(result.candidate) ||
        !finite(result.event.child_inertia) ||
        result.event.child_inertia <= 0.0) {
        return failure<ActivationParentMapping>(
            NumericalStatus::NonFiniteOutput,
            "activation parent mapping produced invalid output");
    }
    return success(result, "activation parent replacement produced");
}

NumericalOutcome<ActivationChildMapping> map_activation_child(
    const ActivationChildDefinition& definition,
    const ActivationChildState& prior,
    const ActivationParentMapped& mapped) {
    const double expected_inertia =
        mapped.child_mass * definition.expected_inertia_per_mass;
    if (!validate_activation_child_state(prior) || prior.initialized ||
        !finite(mapped.child_position_meters) ||
        !finite(mapped.child_velocity_meters_per_second) ||
        !finite(mapped.child_mass) || !finite(mapped.child_inertia) ||
        mapped.child_mass <= 0.0 || mapped.child_inertia <= 0.0 ||
        mapped.child_inertia != expected_inertia ||
        mapped.parent_revision == 0U ||
        mapped.relationship_revision == 0U) {
        return failure<ActivationChildMapping>(
            NumericalStatus::DomainError,
            "activation child mapping input is invalid");
    }
    ActivationChildMapping result;
    result.candidate = {
        true,
        mapped.child_position_meters,
        mapped.child_velocity_meters_per_second,
        mapped.child_mass,
        mapped.child_inertia,
        prior.revision + 1U};
    result.event = {result.candidate.revision, mapped.parent_revision,
                    mapped.relationship_revision};
    if (!validate_activation_child_state(result.candidate)) {
        return failure<ActivationChildMapping>(
            NumericalStatus::NonFiniteOutput,
            "activation child mapping produced invalid output");
    }
    return success(result, "activation child replacement produced");
}

NumericalOutcome<ActivationChildInfluence>
evaluate_activation_child_consumer(
    const ActivationChildConsumerDefinition& definition,
    const ActivationChildObservation& observation) {
    const double weighted = definition.influence_gain * observation.mass *
                            observation.velocity_meters_per_second;
    if (!observation.initialized || !finite(observation.position_meters) ||
        !finite(observation.velocity_meters_per_second) ||
        !finite(observation.mass) || !finite(observation.inertia) ||
        observation.mass <= 0.0 || observation.inertia <= 0.0 ||
        observation.revision == 0U || !finite(weighted)) {
        return failure<ActivationChildInfluence>(
            NumericalStatus::DomainError,
            "inactive or invalid child observation reached consumer");
    }
    return success(ActivationChildInfluence{weighted, observation.revision},
                   "active child influence evaluated");
}

NumericalOutcome<ActivationRelationshipState>
build_activation_relationship_initial_state(
    const ActivationRelationshipDefinition&,
    const ActivationRelationshipInitialInput& input) {
    const ActivationRelationshipState state{
        input.child_active == 1, input.transferred_mass,
        input.relationship_revision < 0
            ? 0U
            : static_cast<std::uint64_t>(input.relationship_revision)};
    if (input.child_active != 0 || input.relationship_revision != 0 ||
        input.transferred_mass != 0.0 ||
        !validate_activation_relationship_state(state)) {
        return failure<ActivationRelationshipState>(
            NumericalStatus::DomainError,
            "activation relationship initial state is not dormant");
    }
    return success(state, "dormant activation relationship initialized");
}

NumericalOutcome<ActivationParentState>
build_activation_parent_initial_state(
    const ActivationParentDefinition&,
    const ActivationParentInitialInput& input) {
    const ActivationParentState state{
        input.available_mass, input.transferred_mass,
        input.revision < 0 ? 0U
                           : static_cast<std::uint64_t>(input.revision)};
    if (input.revision != 0 || input.transferred_mass != 0.0 ||
        !validate_activation_parent_state(state)) {
        return failure<ActivationParentState>(
            NumericalStatus::DomainError,
            "activation parent initial state is invalid");
    }
    return success(state, "activation parent initialized");
}

NumericalOutcome<ActivationChildState>
build_activation_child_initial_state(
    const ActivationChildDefinition&,
    const ActivationChildInitialInput& input) {
    const ActivationChildState state{
        input.initialized == 1,
        input.position_meters,
        input.velocity_meters_per_second,
        input.mass,
        input.inertia,
        input.revision < 0 ? 0U
                           : static_cast<std::uint64_t>(input.revision)};
    if (input.initialized != 0 || input.revision != 0 ||
        input.position_meters != 0.0 ||
        input.velocity_meters_per_second != 0.0 || input.mass != 0.0 ||
        input.inertia != 0.0 || !validate_activation_child_state(state)) {
        return failure<ActivationChildState>(
            NumericalStatus::DomainError,
            "activation child initial state is not dormant");
    }
    return success(state, "dormant activation child initialized");
}

ActivationRelationshipState clone_activation_relationship_state(
    const ActivationRelationshipState& state) {
    return state;
}

bool validate_activation_relationship_state(
    const ActivationRelationshipState& state) noexcept {
    if (!finite(state.transferred_mass) || state.transferred_mass < 0.0) {
        return false;
    }
    return state.child_active
               ? state.transferred_mass > 0.0 &&
                     state.relationship_revision > 0U
               : state.transferred_mass == 0.0 &&
                     state.relationship_revision == 0U;
}

void swap_activation_relationship_state(
    ActivationRelationshipState& lhs,
    ActivationRelationshipState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const ActivationRelationshipStateCodec&
activation_relationship_state_codec() noexcept {
    static const ActivationRelationshipStateCodec codec{
        &clone_activation_relationship_state,
        &validate_activation_relationship_state,
        &validate_activation_relationship_state,
        &validate_activation_relationship_state,
        &swap_activation_relationship_state,
        &clone_activation_relationship_state};
    return codec;
}

ActivationParentState clone_activation_parent_state(
    const ActivationParentState& state) {
    return state;
}

bool validate_activation_parent_state(
    const ActivationParentState& state) noexcept {
    return finite(state.available_mass) && state.available_mass >= 0.0 &&
           finite(state.transferred_mass) && state.transferred_mass >= 0.0;
}

void swap_activation_parent_state(ActivationParentState& lhs,
                                  ActivationParentState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const ActivationParentStateCodec& activation_parent_state_codec() noexcept {
    static const ActivationParentStateCodec codec{
        &clone_activation_parent_state,
        &validate_activation_parent_state,
        &validate_activation_parent_state,
        &validate_activation_parent_state,
        &swap_activation_parent_state,
        &clone_activation_parent_state};
    return codec;
}

ActivationChildState clone_activation_child_state(
    const ActivationChildState& state) {
    return state;
}

bool validate_activation_child_state(
    const ActivationChildState& state) noexcept {
    if (!finite(state.position_meters) ||
        !finite(state.velocity_meters_per_second) || !finite(state.mass) ||
        !finite(state.inertia)) {
        return false;
    }
    return state.initialized
               ? state.mass > 0.0 && state.inertia > 0.0 &&
                     state.revision > 0U
               : state.position_meters == 0.0 &&
                     state.velocity_meters_per_second == 0.0 &&
                     state.mass == 0.0 && state.inertia == 0.0 &&
                     state.revision == 0U;
}

void swap_activation_child_state(ActivationChildState& lhs,
                                 ActivationChildState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const ActivationChildStateCodec& activation_child_state_codec() noexcept {
    static const ActivationChildStateCodec codec{
        &clone_activation_child_state,
        &validate_activation_child_state,
        &validate_activation_child_state,
        &validate_activation_child_state,
        &swap_activation_child_state,
        &clone_activation_child_state};
    return codec;
}

} // namespace gnc::packages::yyz
