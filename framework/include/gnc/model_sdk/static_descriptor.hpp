#pragma once

#include "gnc/contracts/execution_semantics.hpp"
#include "gnc/model_sdk/model_metadata.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gnc::model_sdk {

enum class StaticPortDirection : std::uint8_t {
    Input,
    Output,
};

// Connection semantics owned by the package descriptor. SampledSignal is
// reserved for RuntimeComponent ports; the existing static composition path
// continues to accept only PureQuery and ContinuousClosureLink.
enum class BindingKind : std::uint8_t {
    Unspecified,
    AssetBinding,
    PureQuery,
    ContinuousClosureLink,
    SampledSignal,
    IntervalModel,
};

[[nodiscard]] constexpr std::string_view to_string(
    BindingKind kind) noexcept {
    switch (kind) {
    case BindingKind::Unspecified:
        return "Unspecified";
    case BindingKind::AssetBinding:
        return "AssetBinding";
    case BindingKind::PureQuery:
        return "PureQuery";
    case BindingKind::ContinuousClosureLink:
        return "ContinuousClosureLink";
    case BindingKind::SampledSignal:
        return "SampledSignal";
    case BindingKind::IntervalModel:
        return "IntervalModel";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_binding_kind(
    BindingKind kind) noexcept {
    return kind == BindingKind::AssetBinding ||
           kind == BindingKind::PureQuery ||
           kind == BindingKind::ContinuousClosureLink ||
           kind == BindingKind::SampledSignal ||
           kind == BindingKind::IntervalModel;
}

enum class PortCardinality : std::uint8_t {
    Unspecified,
    ExactlyOne,
    OneOrMore,
};

[[nodiscard]] constexpr std::string_view to_string(
    PortCardinality cardinality) noexcept {
    switch (cardinality) {
    case PortCardinality::Unspecified:
        return "Unspecified";
    case PortCardinality::ExactlyOne:
        return "exactly-one";
    case PortCardinality::OneOrMore:
        return "one-or-more";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_port_cardinality(
    PortCardinality cardinality) noexcept {
    return cardinality == PortCardinality::ExactlyOne ||
           cardinality == PortCardinality::OneOrMore;
}

using TemporalRelation = gnc::contracts::TemporalRelation;
using gnc::contracts::valid_temporal_relation;

[[nodiscard]] constexpr std::string_view to_string(
    TemporalRelation relation) noexcept {
    return gnc::contracts::to_string(relation);
}

// Package-owned placement policy. VehicleProcess identifies an independently
// scheduled RuntimeComponent; lifecycle remains in its tagged runtime facts.
enum class ModelPlacement : std::uint8_t {
    Unspecified,
    VehicleOutput,
    InteractionClosure,
    VehicleProcess,
    Environment,
    VehicleForm,
    Evaluation,
};

[[nodiscard]] constexpr std::string_view to_string(
    ModelPlacement placement) noexcept {
    switch (placement) {
    case ModelPlacement::Unspecified:
        return "Unspecified";
    case ModelPlacement::Environment:
        return "environment";
    case ModelPlacement::VehicleOutput:
        return "vehicle.output";
    case ModelPlacement::InteractionClosure:
        return "interaction/closure";
    case ModelPlacement::VehicleProcess:
        return "vehicle.process";
    case ModelPlacement::VehicleForm:
        return "vehicle.form";
    case ModelPlacement::Evaluation:
        return "evaluation";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_model_placement(
    ModelPlacement placement) noexcept {
    return placement == ModelPlacement::Environment ||
           placement == ModelPlacement::VehicleOutput ||
           placement == ModelPlacement::InteractionClosure ||
           placement == ModelPlacement::VehicleProcess ||
           placement == ModelPlacement::VehicleForm ||
           placement == ModelPlacement::Evaluation;
}

enum class RuntimeCellProfile : std::uint8_t {
    Unspecified,
    SampledTransform,
    DiscreteStateProcessor,
    ContinuousStateOwner,
    Evaluator,
};

[[nodiscard]] constexpr std::string_view to_string(
    RuntimeCellProfile profile) noexcept {
    switch (profile) {
    case RuntimeCellProfile::Unspecified:
        return "Unspecified";
    case RuntimeCellProfile::SampledTransform:
        return "SampledTransform";
    case RuntimeCellProfile::DiscreteStateProcessor:
        return "DiscreteStateProcessor";
    case RuntimeCellProfile::ContinuousStateOwner:
        return "ContinuousStateOwner";
    case RuntimeCellProfile::Evaluator:
        return "Evaluator";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_runtime_cell_profile(
    RuntimeCellProfile profile) noexcept {
    return profile == RuntimeCellProfile::SampledTransform ||
           profile == RuntimeCellProfile::DiscreteStateProcessor ||
           profile == RuntimeCellProfile::ContinuousStateOwner ||
           profile == RuntimeCellProfile::Evaluator;
}

using RuntimeExecutionObligation = gnc::contracts::ExecutionObligation;

[[nodiscard]] constexpr std::string_view to_string(
    RuntimeExecutionObligation obligation) noexcept {
    return gnc::contracts::to_string(obligation);
}

[[nodiscard]] constexpr bool valid_runtime_execution_obligation(
    RuntimeExecutionObligation obligation) noexcept {
    return gnc::contracts::valid_execution_obligation(obligation);
}

enum class RuntimeLifecycleCapability : std::uint8_t {
    Instantiate,
    Dispose,
};

[[nodiscard]] constexpr std::string_view to_string(
    RuntimeLifecycleCapability capability) noexcept {
    switch (capability) {
    case RuntimeLifecycleCapability::Instantiate:
        return "Instantiate";
    case RuntimeLifecycleCapability::Dispose:
        return "Dispose";
    }
    return "Unknown";
}

enum class CoarsePhase : std::uint8_t {
    Unspecified = 0U,
    // Process was the first published R2 phase value.
    Process = 1U,
    Publish = 2U,
    Evaluation = 3U,
    Output = 4U,
    Form = 5U,
};

[[nodiscard]] constexpr std::string_view to_string(
    CoarsePhase phase) noexcept {
    switch (phase) {
    case CoarsePhase::Unspecified:
        return "Unspecified";
    case CoarsePhase::Publish:
        return "publish";
    case CoarsePhase::Process:
        return "process";
    case CoarsePhase::Output:
        return "output";
    case CoarsePhase::Form:
        return "form";
    case CoarsePhase::Evaluation:
        return "evaluation";
    }
    return "Unknown";
}

enum class HoldPolicy : std::uint8_t {
    Unspecified,
    ZeroOrderHold,
};

enum class StaticScheduleTrigger : std::uint8_t {
    Unspecified,
    EveryBoundary,
    TerminalSequenceReady,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticScheduleTrigger trigger) noexcept {
    switch (trigger) {
    case StaticScheduleTrigger::Unspecified:
        return "Unspecified";
    case StaticScheduleTrigger::EveryBoundary:
        return "EveryBoundary";
    case StaticScheduleTrigger::TerminalSequenceReady:
        return "TerminalSequenceReady";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    HoldPolicy policy) noexcept {
    switch (policy) {
    case HoldPolicy::Unspecified:
        return "Unspecified";
    case HoldPolicy::ZeroOrderHold:
        return "ZeroOrderHold";
    }
    return "Unknown";
}

// Query/closure kernels in the current R1 products allocate no caller-visible
// scratch space. Keeping that fact explicit prevents a later linker from
// inventing a workspace layout or treating an omitted field as compatible.
enum class StaticWorkspaceRequirement : std::uint8_t {
    Unspecified,
    None,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticWorkspaceRequirement requirement) noexcept {
    switch (requirement) {
    case StaticWorkspaceRequirement::Unspecified:
        return "Unspecified";
    case StaticWorkspaceRequirement::None:
        return "None";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_static_workspace_requirement(
    StaticWorkspaceRequirement requirement) noexcept {
    return requirement == StaticWorkspaceRequirement::None;
}

enum class CanonicalConfigValueKind : std::uint8_t {
    String,
    Integer,
    Enum,
    Float64,
};

[[nodiscard]] constexpr std::string_view to_string(
    CanonicalConfigValueKind kind) noexcept {
    switch (kind) {
    case CanonicalConfigValueKind::String:
        return "string";
    case CanonicalConfigValueKind::Integer:
        return "integer";
    case CanonicalConfigValueKind::Enum:
        return "enum";
    case CanonicalConfigValueKind::Float64:
        return "float64";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool valid_canonical_config_value_kind(
    CanonicalConfigValueKind kind) noexcept {
    return kind == CanonicalConfigValueKind::String ||
           kind == CanonicalConfigValueKind::Integer ||
           kind == CanonicalConfigValueKind::Enum ||
           kind == CanonicalConfigValueKind::Float64;
}

struct CanonicalEnumValue {
    std::string token;
};

[[nodiscard]] inline bool operator==(const CanonicalEnumValue& lhs,
                                     const CanonicalEnumValue& rhs) {
    return lhs.token == rhs.token;
}

using CanonicalConfigValue =
    std::variant<std::string, std::int64_t, CanonicalEnumValue, double>;

[[nodiscard]] inline CanonicalConfigValueKind canonical_config_value_kind(
    const CanonicalConfigValue& value) noexcept {
    if (std::holds_alternative<std::string>(value)) {
        return CanonicalConfigValueKind::String;
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        return CanonicalConfigValueKind::Integer;
    }
    if (std::holds_alternative<CanonicalEnumValue>(value)) {
        return CanonicalConfigValueKind::Enum;
    }
    return CanonicalConfigValueKind::Float64;
}

struct CanonicalConfigField {
    std::string field_id;
    CanonicalConfigValue value;
};

[[nodiscard]] inline bool operator==(const CanonicalConfigField& lhs,
                                     const CanonicalConfigField& rhs) {
    return lhs.field_id == rhs.field_id && lhs.value == rhs.value;
}

struct CanonicalConfigBlock {
    std::string schema_id;
    std::uint32_t schema_version = 0U;
    std::vector<CanonicalConfigField> fields;
};

[[nodiscard]] inline bool operator==(const CanonicalConfigBlock& lhs,
                                     const CanonicalConfigBlock& rhs) {
    return lhs.schema_id == rhs.schema_id &&
           lhs.schema_version == rhs.schema_version &&
           lhs.fields == rhs.fields;
}

struct StaticConfigFieldDescriptor {
    std::string field_id;
    CanonicalConfigValueKind value_kind =
        CanonicalConfigValueKind::String;
};

struct StaticConfigSchemaDescriptor {
    std::string schema_id;
    std::uint32_t schema_version = 0U;
    std::vector<StaticConfigFieldDescriptor> fields;
};

struct StaticAssetSlotDescriptor {
    std::string role;
    std::string asset_schema_id;
    PortCardinality cardinality = PortCardinality::ExactlyOne;
};

struct StaticSlotCodecDescriptor {
    std::string layout_id;
    std::string entry_id;
    std::string entry_version;
    std::string call_shape_id;
    std::string copy_operation_id;
    std::string validate_operation_id;
    std::string project_operation_id;
};

struct StaticPortDescriptor {
    std::string port_id;
    std::string contract_id;
    StaticPortDirection direction = StaticPortDirection::Input;
    BindingKind binding_kind = BindingKind::Unspecified;
    PortCardinality cardinality = PortCardinality::Unspecified;
    TemporalRelation temporal_relation =
        TemporalRelation::NotApplicable;
    // Required only for a stored output. PureQuery outputs remain caller
    // local and deliberately do not acquire a CycleFrame codec or slot.
    std::optional<StaticSlotCodecDescriptor> slot_codec = std::nullopt;
};

struct StaticRuntimeScheduleDescriptor {
    StaticScheduleTrigger trigger = StaticScheduleTrigger::Unspecified;
    std::uint32_t step_interval = 0U;
    std::uint32_t offset = 0U;
    HoldPolicy output_hold = HoldPolicy::Unspecified;
    std::uint32_t max_input_age_steps = 0U;
};

enum class StaticStateEvolution : std::uint8_t {
    Unspecified,
    ContinuousCandidate,
    IntervalCandidate,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticStateEvolution evolution) noexcept {
    switch (evolution) {
    case StaticStateEvolution::Unspecified:
        return "Unspecified";
    case StaticStateEvolution::ContinuousCandidate:
        return "ContinuousCandidate";
    case StaticStateEvolution::IntervalCandidate:
        return "IntervalCandidate";
    }
    return "Unknown";
}

struct StaticStateFieldDescriptor {
    std::string field_id;
    std::string value_type;
    std::string unit;
    std::string frame_role;
};

struct StaticStateSchemaDescriptor {
    std::string schema_id;
    std::uint32_t schema_version = 0U;
    std::string layout_id;
    std::vector<StaticStateFieldDescriptor> fields;
};

struct StaticStateCodecDescriptor {
    std::string entry_id;
    std::string entry_version;
    std::string call_shape_id;
    std::string clone_operation_id;
    std::string validate_operation_id;
    std::string finite_validation_operation_id;
    std::string invariant_validation_operation_id;
    std::string noexcept_swap_operation_id;
    std::string project_operation_id;
};

struct StaticStateOwnerDescriptor {
    StaticStateSchemaDescriptor schema;
    std::string initial_state_builder_id;
    std::string initial_state_builder_version;
    StaticConfigSchemaDescriptor initial_state_input_schema;
    StaticStateEvolution evolution = StaticStateEvolution::Unspecified;
    // Stable package-authored C++ call shape. The implementation table must
    // independently contribute the same value before an Image can be linked.
    std::string initial_state_builder_call_shape_id;
    StaticStateCodecDescriptor codec;
};

enum class StaticInvocationKind : std::uint8_t {
    Unspecified,
    PureQuery,
    Closure,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticInvocationKind kind) noexcept {
    switch (kind) {
    case StaticInvocationKind::Unspecified:
        return "Unspecified";
    case StaticInvocationKind::PureQuery:
        return "PureQuery";
    case StaticInvocationKind::Closure:
        return "Closure";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_static_invocation_kind(
    StaticInvocationKind kind) noexcept {
    return kind == StaticInvocationKind::PureQuery ||
           kind == StaticInvocationKind::Closure;
}

struct StaticInvocationRequirementDescriptor {
    std::string requirement_id;
    StaticInvocationKind kind = StaticInvocationKind::Unspecified;
    std::string contract_id;
    PortCardinality cardinality = PortCardinality::ExactlyOne;
};

enum class StaticStateWriteKind : std::uint8_t {
    None,
    IntervalCandidate,
};

enum class StaticStateReadKind : std::uint8_t {
    None,
    Committed,
    Candidate,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticStateReadKind kind) noexcept {
    switch (kind) {
    case StaticStateReadKind::None:
        return "None";
    case StaticStateReadKind::Committed:
        return "Committed";
    case StaticStateReadKind::Candidate:
        return "Candidate";
    }
    return "Unknown";
}

struct StaticRuntimeObligationEntryDescriptor {
    RuntimeExecutionObligation obligation =
        RuntimeExecutionObligation::BoundaryEvaluation;
    CoarsePhase phase = CoarsePhase::Unspecified;
    std::string entry_id;
    std::string entry_version;
    std::string request_contract_id;
    std::string result_contract_id;
    StaticWorkspaceRequirement workspace_requirement =
        StaticWorkspaceRequirement::Unspecified;
    std::vector<std::string> input_port_ids;
    std::vector<std::string> output_port_ids;
    StaticStateReadKind state_read = StaticStateReadKind::None;
    StaticStateWriteKind state_write = StaticStateWriteKind::None;
    std::vector<StaticInvocationRequirementDescriptor>
        invocation_requirements;
    // Separate from the semantic request/result signature: this locks the
    // exact process-local C++ prototype expected from the package entry.
    std::string call_shape_id;
    // Optional stored formal result for coordinator-owned composition. It is
    // not a public port and is never used to materialize PureQuery output.
    std::optional<StaticSlotCodecDescriptor> result_codec = std::nullopt;
};

struct StaticEvaluatorHistoryMemberDescriptor {
    std::string member_id;
    std::string state_schema_id;
    std::string state_layout_id;
};

struct StaticEvaluatorHistoryShapeDescriptor {
    std::string request_contract_id;
    std::uint32_t depth = 0U;
    std::vector<StaticEvaluatorHistoryMemberDescriptor> ordered_members;
};

// Package-owned static facts for one independent runtime boundary. State and
// obligation entries remain immutable descriptor facts; Session storage and
// bound callable objects are created only after an ExecutionPlanImage exists.
struct StaticRuntimeComponentDescriptor {
    std::string recipe_id;
    RuntimeCellProfile profile = RuntimeCellProfile::Unspecified;
    std::vector<RuntimeExecutionObligation> obligations;
    std::vector<StaticRuntimeObligationEntryDescriptor>
        obligation_entries;
    StaticRuntimeScheduleDescriptor schedule;
    std::vector<RuntimeLifecycleCapability> lifecycle_capabilities;
    std::optional<StaticStateOwnerDescriptor> state_owner;
    // Builds the immutable, package-typed Definition from the occurrence's
    // already-canonical configuration. R2 links but never calls this entry.
    std::string definition_builder_id;
    std::string definition_builder_version;
    std::string definition_builder_call_shape_id;
    // Stable package-owned factory selected by R2 and invoked only by R3.
    // The factory receives the already-built typed Definition together with
    // the compiled numeric handles for this occurrence and constructs the
    // Session-local Runtime Cell. Descriptor data never contains that cell.
    std::string runtime_cell_factory_id;
    std::string runtime_cell_factory_version;
    std::string runtime_cell_factory_call_shape_id;
    // Current R2 facts are intentionally narrow: REF-YYZ declares one
    // immutable no-workspace resource plan per RuntimeComponent. R3 later
    // supplies the Session-local view when it invokes the linked factory.
    std::string resource_plan_id;
    StaticWorkspaceRequirement resource_workspace_requirement =
        StaticWorkspaceRequirement::Unspecified;
    // Present only for terminal Evaluator profiles whose exact package entry
    // consumes a bounded committed-state history.
    std::optional<StaticEvaluatorHistoryShapeDescriptor>
        evaluator_history_shape;
};

// Stable implementation facts for the existing PreparedModel-backed forms.
// These are descriptor/link identities only: no function address or prepared
// instance is stored here.
struct StaticPureQueryDescriptor {
    std::string query_entry_id;
    std::string query_entry_version;
    StaticWorkspaceRequirement workspace_requirement =
        StaticWorkspaceRequirement::Unspecified;
    std::string request_contract_id;
    std::string query_call_shape_id;
};

struct StaticClosureDescriptor {
    std::string closure_entry_id;
    std::string closure_entry_version;
    gnc::contracts::ClosureStrategy strategy =
        gnc::contracts::ClosureStrategy::Unspecified;
    StaticWorkspaceRequirement workspace_requirement =
        StaticWorkspaceRequirement::Unspecified;
    std::string request_contract_id;
    std::string closure_call_shape_id;
};

// Package-owned description of a model that can be read without preparing or
// instantiating it. execution_form is the closed tag: exactly one matching
// PureQuery, Closure, or RuntimeComponent payload may be present.
struct StaticModelDescriptor {
    ModelDefinitionMetadata definition;
    ModelPlacement placement = ModelPlacement::Unspecified;
    std::string preparation_algorithm_id;
    std::string preparation_algorithm_version;
    StaticConfigSchemaDescriptor configuration;
    std::vector<StaticAssetSlotDescriptor> asset_slots;
    std::vector<StaticPortDescriptor> ports;
    std::optional<StaticPureQueryDescriptor> pure_query;
    std::optional<StaticClosureDescriptor> closure;
    std::optional<StaticRuntimeComponentDescriptor> runtime_component;
    std::string preparation_call_shape_id;
};

// A stateless AlgorithmKernel composition is a binding consumer. It has no
// execution-form tag, runtime instance, lifecycle, or mutable state.
struct StaticAlgorithmDescriptor {
    std::string algorithm_id;
    std::string algorithm_version;
    std::vector<StaticPortDescriptor> ports;
    StaticWorkspaceRequirement workspace_requirement =
        StaticWorkspaceRequirement::None;
    std::vector<StaticInvocationRequirementDescriptor>
        invocation_requirements;
};

struct StaticPackageDescriptor {
    std::string package_id;
    std::string package_version;
    std::vector<StaticModelDescriptor> models;
    std::vector<StaticAlgorithmDescriptor> algorithms;
};

} // namespace gnc::model_sdk
