#include <yyz/two_entity_causal.hpp>

#include <algorithm>
#include <cmath>
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

constexpr AlgorithmIdentity kTwoEntityAlgorithm{
    "gnc.algorithm.yyz.two-entity-causal-qualification@1", "0.1.0"};

[[nodiscard]] NumericalEvidence evidence(std::string_view detail) {
    NumericalEvidence result;
    result.algorithm = kTwoEntityAlgorithm;
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
    return "gnc.package.yyz.two-entity." + std::string(model) + "." +
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
    std::string_view model) {
    runtime.recipe_id = identity(model, "recipe");
    runtime.profile = RuntimeCellProfile::DiscreteStateProcessor;
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
    gnc::model_sdk::StaticStateReadKind state_read,
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
    owner.evolution = gnc::model_sdk::StaticStateEvolution::IntervalCandidate;
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
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            entry->entry_id, entry->entry_version, kind,
            gnc::model_sdk::canonical_runtime_entry_signature(model, *entry),
            gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
                entry->call_shape_id),
            model.runtime_component->state_owner->schema.layout_id));
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
                   "two-entity runtime cell contract materialized");
}

} // namespace

gnc::model_sdk::StaticPackageDescriptor describe_two_entity_causal_package() {
    gnc::model_sdk::StaticPackageDescriptor package;
    package.package_id = std::string(kTwoEntityCausalPackageId);
    package.package_version = std::string(kTwoEntityCausalPackageVersion);

    StaticModelDescriptor entity_a;
    entity_a.definition = {std::string(kEntityATruthModelId),
                           std::string(kTwoEntityCausalModelVersion),
                           gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    entity_a.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    entity_a.configuration = {
        std::string(kEntityATruthConfigSchemaId), 1U,
        {{"increment_per_interval", CanonicalConfigValueKind::Float64}}};
    entity_a.ports = {
        {"opening-truth", std::string(kEntityATruthObservationContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"truth-observation", std::string(kEntityATruthObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("entity-a", "truth-observation",
                    kEntityATruthObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor entity_a_runtime;
    configure_runtime(entity_a_runtime, "entity-a");
    entity_a_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::IntervalEvolution};
    entity_a_runtime.obligation_entries = {
        runtime_entry("entity-a", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.entity-a-truth-projection@1",
                      std::string(kEntityATruthObservationContractId), {},
                      {"truth-observation"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("entity-a", "evolve",
                      RuntimeExecutionObligation::IntervalEvolution,
                      CoarsePhase::Form,
                      "gnc.request.yyz.entity-a-truth-evolution@1",
                      "gnc.result.yyz.entity-a-truth-evolution@1",
                      {"opening-truth"}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::IntervalCandidate)};
    entity_a_runtime.state_owner = state_owner(
        "entity-a", kEntityATruthStateSchemaId,
        kEntityATruthStateLayoutId,
        {{"position", "float64", "m", "entity-a-truth"},
         {"revision", "uint64", "1", "entity-a-truth"}},
        kEntityATruthInitialSchemaId,
        {{"position", CanonicalConfigValueKind::Float64}});
    entity_a.runtime_component = std::move(entity_a_runtime);

    StaticModelDescriptor link;
    link.definition = {std::string(kOneTickTruthLinkModelId),
                       std::string(kTwoEntityCausalModelVersion),
                       gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    link.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    link.configuration = {
        std::string(kOneTickTruthLinkConfigSchemaId), 1U,
        {{"transfer_gain", CanonicalConfigValueKind::Float64}}};
    link.ports = {
        {"current-a", std::string(kEntityATruthObservationContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"delayed-a", std::string(kOneTickTruthLinkObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("link", "delayed-a",
                    kOneTickTruthLinkObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor link_runtime;
    configure_runtime(link_runtime, "link");
    link_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::IntervalEvolution};
    link_runtime.obligation_entries = {
        runtime_entry("link", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.one-tick-link-projection@1",
                      std::string(kOneTickTruthLinkObservationContractId), {},
                      {"delayed-a"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("link", "evolve",
                      RuntimeExecutionObligation::IntervalEvolution,
                      CoarsePhase::Form,
                      std::string(kEntityATruthObservationContractId),
                      "gnc.result.yyz.one-tick-link-evolution@1",
                      {"current-a"}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::IntervalCandidate)};
    link_runtime.state_owner = state_owner(
        "link", kOneTickTruthLinkStateSchemaId,
        kOneTickTruthLinkStateLayoutId,
        {{"valid", "bool", "1", "one-tick-link"},
         {"source_tick", "int64", "tick", "one-tick-link"},
         {"source_position", "float64", "m", "one-tick-link"},
         {"revision", "uint64", "1", "one-tick-link"}},
        kOneTickTruthLinkInitialSchemaId,
        {{"dormant_position", CanonicalConfigValueKind::Float64}});
    link.runtime_component = std::move(link_runtime);

    StaticModelDescriptor entity_b;
    entity_b.definition = {std::string(kEntityBTruthModelId),
                           std::string(kTwoEntityCausalModelVersion),
                           gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    entity_b.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    entity_b.configuration = {
        std::string(kEntityBTruthConfigSchemaId), 1U,
        {{"current_gain", CanonicalConfigValueKind::Float64},
         {"delayed_gain", CanonicalConfigValueKind::Float64}}};
    entity_b.ports = {
        {"current-a", std::string(kEntityATruthObservationContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"delayed-a", std::string(kOneTickTruthLinkObservationContractId),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"truth-observation", std::string(kEntityBTruthObservationContractId),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         slot_codec("entity-b", "truth-observation",
                    kEntityBTruthObservationLayoutId)}};
    gnc::model_sdk::StaticRuntimeComponentDescriptor entity_b_runtime;
    configure_runtime(entity_b_runtime, "entity-b");
    entity_b_runtime.obligations = {
        RuntimeExecutionObligation::PublishProjection,
        RuntimeExecutionObligation::IntervalEvolution};
    entity_b_runtime.obligation_entries = {
        runtime_entry("entity-b", "publish",
                      RuntimeExecutionObligation::PublishProjection,
                      CoarsePhase::Publish,
                      "gnc.request.yyz.entity-b-truth-projection@1",
                      std::string(kEntityBTruthObservationContractId), {},
                      {"truth-observation"},
                      gnc::model_sdk::StaticStateReadKind::Committed),
        runtime_entry("entity-b", "evolve",
                      RuntimeExecutionObligation::IntervalEvolution,
                      CoarsePhase::Form,
                      "gnc.request.yyz.entity-b-truth-evolution@1",
                      "gnc.result.yyz.entity-b-truth-evolution@1",
                      {"current-a", "delayed-a"}, {},
                      gnc::model_sdk::StaticStateReadKind::Committed,
                      gnc::model_sdk::StaticStateWriteKind::IntervalCandidate)};
    entity_b_runtime.state_owner = state_owner(
        "entity-b", kEntityBTruthStateSchemaId,
        kEntityBTruthStateLayoutId,
        {{"position", "float64", "m", "entity-b-truth"},
         {"last_current_a_position", "float64", "m", "entity-a-current"},
         {"last_delayed_a_position", "float64", "m", "entity-a-delayed"},
         {"last_delayed_valid", "bool", "1", "entity-a-delayed"},
         {"last_delayed_source_tick", "int64", "tick", "entity-a-delayed"},
         {"revision", "uint64", "1", "entity-b-truth"}},
        kEntityBTruthInitialSchemaId,
        {{"position", CanonicalConfigValueKind::Float64}});
    entity_b.runtime_component = std::move(entity_b_runtime);

    package.models = {std::move(entity_a), std::move(link),
                      std::move(entity_b)};
    return package;
}

gnc::model_sdk::StaticPackageImplementation
describe_two_entity_causal_implementation(std::string build_fingerprint) {
    const auto package = describe_two_entity_causal_package();
    StaticPackageImplementation implementation;
    implementation.package_id = package.package_id;
    implementation.package_version = package.package_version;
    implementation.build_fingerprint = std::move(build_fingerprint);

    if (const auto* model = find_model(package, kEntityATruthModelId)) {
        append_definition<EntityATruthDefinitionBuilderCall,
                          &build_entity_a_truth_definition>(implementation,
                                                            *model);
        append_factory<EntityATruthRuntimeCellFactoryCall,
                       &create_entity_a_truth_runtime_cell>(implementation,
                                                            *model);
        append_initial<EntityATruthInitialStateCall,
                       &build_entity_a_truth_initial_state>(implementation,
                                                            *model);
        append_state_codec<EntityATruthStateCodecGetter,
                           &entity_a_truth_state_codec>(implementation,
                                                       *model);
        append_runtime<EntityATruthProjectionCall, &project_entity_a_truth>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<EntityATruthEvolutionCall, &evolve_entity_a_truth>(
            implementation, *model,
            RuntimeExecutionObligation::IntervalEvolution,
            StaticEntryKind::IntervalEvolution);
        append_slot_codec<EntityATruthObservation>(implementation, *model,
                                                   "truth-observation");
    }
    if (const auto* model = find_model(package, kOneTickTruthLinkModelId)) {
        append_definition<OneTickTruthLinkDefinitionBuilderCall,
                          &build_one_tick_truth_link_definition>(
            implementation, *model);
        append_factory<OneTickTruthLinkRuntimeCellFactoryCall,
                       &create_one_tick_truth_link_runtime_cell>(
            implementation, *model);
        append_initial<OneTickTruthLinkInitialStateCall,
                       &build_one_tick_truth_link_initial_state>(
            implementation, *model);
        append_state_codec<OneTickTruthLinkStateCodecGetter,
                           &one_tick_truth_link_state_codec>(implementation,
                                                            *model);
        append_runtime<OneTickTruthLinkProjectionCall,
                       &project_one_tick_truth_link>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<OneTickTruthLinkEvolutionCall,
                       &evolve_one_tick_truth_link>(
            implementation, *model,
            RuntimeExecutionObligation::IntervalEvolution,
            StaticEntryKind::IntervalEvolution);
        append_slot_codec<OneTickTruthLinkObservation>(implementation, *model,
                                                       "delayed-a");
    }
    if (const auto* model = find_model(package, kEntityBTruthModelId)) {
        append_definition<EntityBTruthDefinitionBuilderCall,
                          &build_entity_b_truth_definition>(implementation,
                                                            *model);
        append_factory<EntityBTruthRuntimeCellFactoryCall,
                       &create_entity_b_truth_runtime_cell>(implementation,
                                                            *model);
        append_initial<EntityBTruthInitialStateCall,
                       &build_entity_b_truth_initial_state>(implementation,
                                                            *model);
        append_state_codec<EntityBTruthStateCodecGetter,
                           &entity_b_truth_state_codec>(implementation,
                                                       *model);
        append_runtime<EntityBTruthProjectionCall, &project_entity_b_truth>(
            implementation, *model,
            RuntimeExecutionObligation::PublishProjection,
            StaticEntryKind::PublishProjection);
        append_runtime<EntityBTruthEvolutionCall, &evolve_entity_b_truth>(
            implementation, *model,
            RuntimeExecutionObligation::IntervalEvolution,
            StaticEntryKind::IntervalEvolution);
        append_slot_codec<EntityBTruthObservation>(implementation, *model,
                                                   "truth-observation");
    }

    implementation.state_layouts = {
        {std::string(kEntityATruthStateLayoutId), sizeof(EntityATruthState),
         alignof(EntityATruthState)},
        {std::string(kOneTickTruthLinkStateLayoutId),
         sizeof(OneTickTruthLinkState), alignof(OneTickTruthLinkState)},
        {std::string(kEntityBTruthStateLayoutId), sizeof(EntityBTruthState),
         alignof(EntityBTruthState)}};
    implementation.value_layouts = {
        {std::string(kEntityATruthObservationContractId),
         sizeof(EntityATruthObservation), alignof(EntityATruthObservation),
         std::string(kEntityATruthObservationLayoutId)},
        {std::string(kOneTickTruthLinkObservationContractId),
         sizeof(OneTickTruthLinkObservation),
         alignof(OneTickTruthLinkObservation),
         std::string(kOneTickTruthLinkObservationLayoutId)},
        {std::string(kEntityBTruthObservationContractId),
         sizeof(EntityBTruthObservation), alignof(EntityBTruthObservation),
         std::string(kEntityBTruthObservationLayoutId)}};
    return implementation;
}

NumericalOutcome<EntityATruthDefinition> build_entity_a_truth_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "increment_per_interval"};
    const auto* increment = floating(configuration, fields[0]);
    if (!exact_fields(configuration, kEntityATruthConfigSchemaId, fields) ||
        increment == nullptr || !finite(*increment)) {
        return failure<EntityATruthDefinition>(
            NumericalStatus::DomainError,
            "entity A truth configuration is invalid");
    }
    return success(EntityATruthDefinition{*increment},
                   "entity A truth definition built");
}

NumericalOutcome<OneTickTruthLinkDefinition>
build_one_tick_truth_link_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{"transfer_gain"};
    const auto* gain = floating(configuration, fields[0]);
    if (!exact_fields(configuration, kOneTickTruthLinkConfigSchemaId, fields) ||
        gain == nullptr || !finite(*gain)) {
        return failure<OneTickTruthLinkDefinition>(
            NumericalStatus::DomainError,
            "one-tick truth link configuration is invalid");
    }
    return success(OneTickTruthLinkDefinition{*gain},
                   "one-tick truth link definition built");
}

NumericalOutcome<EntityBTruthDefinition> build_entity_b_truth_definition(
    const CanonicalConfigBlock& configuration) {
    static const std::vector<std::string_view> fields{
        "current_gain", "delayed_gain"};
    const auto* current = floating(configuration, fields[0]);
    const auto* delayed = floating(configuration, fields[1]);
    if (!exact_fields(configuration, kEntityBTruthConfigSchemaId, fields) ||
        current == nullptr || delayed == nullptr || !finite(*current) ||
        !finite(*delayed)) {
        return failure<EntityBTruthDefinition>(
            NumericalStatus::DomainError,
            "entity B truth configuration is invalid");
    }
    return success(EntityBTruthDefinition{*current, *delayed},
                   "entity B truth definition built");
}

NumericalOutcome<EntityATruthRuntimeCell> create_entity_a_truth_runtime_cell(
    const EntityATruthDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings&) {
    return create_runtime_cell<EntityATruthRuntimeCell>(definition, context);
}

NumericalOutcome<OneTickTruthLinkRuntimeCell>
create_one_tick_truth_link_runtime_cell(
    const OneTickTruthLinkDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings&) {
    return create_runtime_cell<OneTickTruthLinkRuntimeCell>(definition,
                                                            context);
}

NumericalOutcome<EntityBTruthRuntimeCell> create_entity_b_truth_runtime_cell(
    const EntityBTruthDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings&) {
    return create_runtime_cell<EntityBTruthRuntimeCell>(definition, context);
}

EntityATruthObservation project_entity_a_truth(
    const EntityATruthState& state, std::int64_t tick) {
    return {tick, state.position, state.revision};
}

NumericalOutcome<EntityATruthState> evolve_entity_a_truth(
    const EntityATruthDefinition& definition,
    const EntityATruthState& prior,
    const EntityATruthObservation& opening) {
    if (!validate_entity_a_truth_state(prior) ||
        !finite(definition.increment_per_interval) || opening.tick < 0 ||
        !finite(opening.position) || opening.revision != prior.revision ||
        opening.position != prior.position) {
        return failure<EntityATruthState>(NumericalStatus::DomainError,
                                          "entity A evolution input is invalid");
    }
    EntityATruthState next = prior;
    next.position += definition.increment_per_interval;
    ++next.revision;
    if (!validate_entity_a_truth_state(next)) {
        return failure<EntityATruthState>(
            NumericalStatus::NonFiniteOutput,
            "entity A evolution produced invalid state");
    }
    return success(next, "entity A truth evolved");
}

OneTickTruthLinkObservation project_one_tick_truth_link(
    const OneTickTruthLinkState& state, std::int64_t tick) {
    return {tick, state.valid, state.source_tick, state.source_position,
            state.revision};
}

NumericalOutcome<OneTickTruthLinkState> evolve_one_tick_truth_link(
    const OneTickTruthLinkDefinition& definition,
    const OneTickTruthLinkState& prior,
    const EntityATruthObservation& current_a) {
    if (!validate_one_tick_truth_link_state(prior) || current_a.tick < 0 ||
        !finite(current_a.position) || !finite(definition.transfer_gain)) {
        return failure<OneTickTruthLinkState>(
            NumericalStatus::DomainError,
            "one-tick link evolution input is invalid");
    }
    OneTickTruthLinkState next;
    next.valid = true;
    next.source_tick = current_a.tick;
    next.source_position = current_a.position * definition.transfer_gain;
    next.revision = prior.revision + 1U;
    if (!validate_one_tick_truth_link_state(next)) {
        return failure<OneTickTruthLinkState>(
            NumericalStatus::NonFiniteOutput,
            "one-tick link evolution produced invalid state");
    }
    return success(next, "one-tick truth link evolved");
}

EntityBTruthObservation project_entity_b_truth(
    const EntityBTruthState& state, std::int64_t tick) {
    return {tick, state};
}

NumericalOutcome<EntityBTruthState> evolve_entity_b_truth(
    const EntityBTruthDefinition& definition,
    const EntityBTruthState& prior,
    const EntityATruthObservation& current_a,
    const OneTickTruthLinkObservation& delayed_a) {
    const bool causal_link_valid =
        !delayed_a.valid ||
        (current_a.tick > 0 && delayed_a.tick == current_a.tick &&
         delayed_a.source_tick == current_a.tick - 1);
    if (!validate_entity_b_truth_state(prior) || current_a.tick < 0 ||
        !finite(current_a.position) || !finite(delayed_a.source_position) ||
        !finite(definition.current_gain) ||
        !finite(definition.delayed_gain) || !causal_link_valid) {
        return failure<EntityBTruthState>(
            NumericalStatus::DomainError,
            "entity B causal evolution input is invalid");
    }
    EntityBTruthState next = prior;
    next.last_current_a_position = current_a.position;
    next.last_delayed_a_position =
        delayed_a.valid ? delayed_a.source_position : 0.0;
    next.last_delayed_valid = delayed_a.valid;
    next.last_delayed_source_tick =
        delayed_a.valid ? delayed_a.source_tick : -1;
    next.position += definition.current_gain * current_a.position;
    if (delayed_a.valid) {
        next.position += definition.delayed_gain * delayed_a.source_position;
    }
    ++next.revision;
    if (!validate_entity_b_truth_state(next)) {
        return failure<EntityBTruthState>(
            NumericalStatus::NonFiniteOutput,
            "entity B evolution produced invalid state");
    }
    return success(next, "entity B truth evolved from current and delayed A");
}

NumericalOutcome<EntityATruthState> build_entity_a_truth_initial_state(
    const EntityATruthDefinition&,
    const EntityATruthInitialInput& input) {
    EntityATruthState state{input.position, 0U};
    if (!validate_entity_a_truth_state(state)) {
        return failure<EntityATruthState>(
            NumericalStatus::DomainError,
            "entity A initial state is invalid");
    }
    return success(state, "entity A initial state built");
}

NumericalOutcome<OneTickTruthLinkState>
build_one_tick_truth_link_initial_state(
    const OneTickTruthLinkDefinition&,
    const OneTickTruthLinkInitialInput& input) {
    OneTickTruthLinkState state{false, -1, input.dormant_position, 0U};
    if (!validate_one_tick_truth_link_state(state)) {
        return failure<OneTickTruthLinkState>(
            NumericalStatus::DomainError,
            "one-tick link initial state is invalid");
    }
    return success(state, "empty one-tick link initial state built");
}

NumericalOutcome<EntityBTruthState> build_entity_b_truth_initial_state(
    const EntityBTruthDefinition&,
    const EntityBTruthInitialInput& input) {
    EntityBTruthState state;
    state.position = input.position;
    if (!validate_entity_b_truth_state(state)) {
        return failure<EntityBTruthState>(
            NumericalStatus::DomainError,
            "entity B initial state is invalid");
    }
    return success(state, "entity B initial state built");
}

EntityATruthState clone_entity_a_truth_state(
    const EntityATruthState& state) {
    return state;
}

bool validate_entity_a_truth_state(const EntityATruthState& state) noexcept {
    return finite(state.position);
}

void swap_entity_a_truth_state(EntityATruthState& lhs,
                               EntityATruthState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const EntityATruthStateCodec& entity_a_truth_state_codec() noexcept {
    static const EntityATruthStateCodec codec{
        &clone_entity_a_truth_state, &validate_entity_a_truth_state,
        &validate_entity_a_truth_state, &validate_entity_a_truth_state,
        &swap_entity_a_truth_state, &clone_entity_a_truth_state};
    return codec;
}

OneTickTruthLinkState clone_one_tick_truth_link_state(
    const OneTickTruthLinkState& state) {
    return state;
}

bool validate_one_tick_truth_link_state(
    const OneTickTruthLinkState& state) noexcept {
    return finite(state.source_position) &&
           ((state.valid && state.source_tick >= 0) ||
            (!state.valid && state.source_tick == -1));
}

void swap_one_tick_truth_link_state(OneTickTruthLinkState& lhs,
                                    OneTickTruthLinkState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const OneTickTruthLinkStateCodec&
one_tick_truth_link_state_codec() noexcept {
    static const OneTickTruthLinkStateCodec codec{
        &clone_one_tick_truth_link_state,
        &validate_one_tick_truth_link_state,
        &validate_one_tick_truth_link_state,
        &validate_one_tick_truth_link_state,
        &swap_one_tick_truth_link_state,
        &clone_one_tick_truth_link_state};
    return codec;
}

EntityBTruthState clone_entity_b_truth_state(
    const EntityBTruthState& state) {
    return state;
}

bool validate_entity_b_truth_state(const EntityBTruthState& state) noexcept {
    return finite(state.position) && finite(state.last_current_a_position) &&
           finite(state.last_delayed_a_position) &&
           ((state.last_delayed_valid &&
             state.last_delayed_source_tick >= 0) ||
            (!state.last_delayed_valid &&
             state.last_delayed_source_tick == -1));
}

void swap_entity_b_truth_state(EntityBTruthState& lhs,
                               EntityBTruthState& rhs) noexcept {
    using std::swap;
    swap(lhs, rhs);
}

const EntityBTruthStateCodec& entity_b_truth_state_codec() noexcept {
    static const EntityBTruthStateCodec codec{
        &clone_entity_b_truth_state, &validate_entity_b_truth_state,
        &validate_entity_b_truth_state, &validate_entity_b_truth_state,
        &swap_entity_b_truth_state, &clone_entity_b_truth_state};
    return codec;
}

} // namespace gnc::packages::yyz
