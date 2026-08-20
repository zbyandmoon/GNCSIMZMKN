#pragma once

#include "gnc/contracts/execution_plan_image.hpp"
#include "gnc/model_sdk/static_descriptor.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace gnc::model_sdk {

enum class StaticEntryKind : std::uint8_t {
    DefinitionBuilder,
    RuntimeCellFactory,
    Prepare,
    PureQuery,
    Closure,
    StateCodec,
    SlotCodec,
    InitialState,
    PublishProjection,
    BoundaryEvaluation,
    IntervalEvolution,
    DerivativeEvaluation,
};

[[nodiscard]] constexpr std::string_view to_string(
    StaticEntryKind kind) noexcept {
    switch (kind) {
    case StaticEntryKind::DefinitionBuilder:
        return "DefinitionBuilder";
    case StaticEntryKind::RuntimeCellFactory:
        return "RuntimeCellFactory";
    case StaticEntryKind::Prepare:
        return "Prepare";
    case StaticEntryKind::PureQuery:
        return "PureQuery";
    case StaticEntryKind::Closure:
        return "Closure";
    case StaticEntryKind::StateCodec:
        return "StateCodec";
    case StaticEntryKind::SlotCodec:
        return "SlotCodec";
    case StaticEntryKind::InitialState:
        return "InitialState";
    case StaticEntryKind::PublishProjection:
        return "PublishProjection";
    case StaticEntryKind::BoundaryEvaluation:
        return "BoundaryEvaluation";
    case StaticEntryKind::IntervalEvolution:
        return "IntervalEvolution";
    case StaticEntryKind::DerivativeEvaluation:
        return "DerivativeEvaluation";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_static_entry_kind(
    StaticEntryKind kind) noexcept {
    return kind >= StaticEntryKind::DefinitionBuilder &&
           kind <= StaticEntryKind::DerivativeEvaluation;
}

// Independent package-implementation witness for the exact committed-history
// object accepted by an Evaluator entry. This deliberately repeats the stable
// descriptor facts: the linker must compare two independently contributed
// sides instead of trusting a source/descriptor value as evidence about the
// linked C++ callable.
struct StaticEvaluatorHistoryMemberWitness {
    std::string member_id;
    std::string state_schema_id;
    std::string state_layout_id;
};

struct StaticEvaluatorHistoryWitness {
    std::string request_contract_id;
    std::uint32_t depth = 0U;
    std::vector<StaticEvaluatorHistoryMemberWitness> ordered_members;
};

struct StaticStateCodecWitness {
    std::string state_layout_id;
    std::string clone_operation_id;
    std::string validate_operation_id;
    std::string finite_validation_operation_id;
    std::string invariant_validation_operation_id;
    std::string noexcept_swap_operation_id;
    std::string project_operation_id;
    bool swap_is_noexcept = false;
};

struct StaticSlotCodecWitness {
    std::string contract_id;
    std::string value_layout_id;
    std::string copy_operation_id;
    std::string validate_operation_id;
    std::string project_operation_id;
};

// Taking the address of this specialization forces the real typed Callable to
// be referenced by the program while giving the linker one safe, uniform
// function-pointer type. The anchor is intentionally inert and must never be
// called by the R2 compiler/linker.
template <auto Callable>
inline void static_link_anchor() noexcept {
    // The volatile typed pointer materialization prevents an optimizing build
    // from erasing the relocation to Callable. It does not invoke Callable and
    // never erases it through void* or an integer representation.
    volatile auto retained_typed_callable = Callable;
    static_cast<void>(retained_typed_callable);
}

template <auto Callable>
[[nodiscard]] constexpr gnc::contracts::StaticLinkAnchor
make_static_link_anchor() noexcept {
    return &static_link_anchor<Callable>;
}

struct StaticImplementationEntry {
    std::string entry_id;
    std::string entry_version;
    StaticEntryKind kind = StaticEntryKind::Prepare;
    std::string signature_id;
    // Stable package-authored identifier for the exact C++ prototype. The
    // descriptor contributes the expected value independently; it is not an
    // RTTI name, wire ABI, or serialized type representation.
    std::string call_shape_id;
    std::string state_layout_id;
    std::string workspace_layout_id;
    // Exact process-local function-pointer type. The package is the only
    // layer expected to know the any_cast target; Compiler merely verifies
    // stable facts and copies this reference into the immutable image.
    std::any typed_entry;
    // Process-local type witness from StaticCallableContract, authored
    // independently of Callable. The factory statically rejects a Callable
    // whose pointer type differs from this contract.
    const std::type_info* callable_contract_type = nullptr;
    // Symbol-retention evidence only, never an invocation trampoline.
    gnc::contracts::StaticLinkAnchor link_anchor = nullptr;
    // Present only on the terminal Evaluator entry that consumes committed
    // history. It is process-independent stable evidence, but not a wire
    // schema, serializer, or runtime history object.
    std::optional<StaticEvaluatorHistoryWitness> evaluator_history_witness =
        std::nullopt;
    std::optional<StaticStateCodecWitness> state_codec_witness = std::nullopt;
    std::optional<StaticSlotCodecWitness> slot_codec_witness = std::nullopt;
};

template <typename ExpectedCallable>
struct StaticCallableContract {
    static_assert(std::is_pointer_v<ExpectedCallable> &&
                      std::is_function_v<
                          std::remove_pointer_t<ExpectedCallable>>,
                  "static callable contract requires a function pointer type");
    std::string call_shape_id;
};

template <typename ExpectedCallable>
[[nodiscard]] inline StaticCallableContract<ExpectedCallable>
make_static_callable_contract(std::string call_shape_id) {
    return {std::move(call_shape_id)};
}

[[nodiscard]] inline StaticImplementationEntry
with_static_evaluator_history_witness(
    StaticImplementationEntry entry,
    StaticEvaluatorHistoryWitness witness) {
    entry.evaluator_history_witness = std::move(witness);
    return entry;
}

[[nodiscard]] inline StaticImplementationEntry with_static_state_codec_witness(
    StaticImplementationEntry entry, StaticStateCodecWitness witness) {
    entry.state_codec_witness = std::move(witness);
    return entry;
}

[[nodiscard]] inline StaticImplementationEntry with_static_slot_codec_witness(
    StaticImplementationEntry entry, StaticSlotCodecWitness witness) {
    entry.slot_codec_witness = std::move(witness);
    return entry;
}

[[nodiscard]] inline bool matches_static_evaluator_history_witness(
    const StaticEvaluatorHistoryWitness& implementation,
    const StaticEvaluatorHistoryShapeDescriptor& descriptor) noexcept {
    if (implementation.request_contract_id != descriptor.request_contract_id ||
        implementation.depth != descriptor.depth ||
        implementation.ordered_members.size() !=
            descriptor.ordered_members.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < implementation.ordered_members.size(); ++index) {
        const auto& actual = implementation.ordered_members[index];
        const auto& expected = descriptor.ordered_members[index];
        if (actual.member_id != expected.member_id ||
            actual.state_schema_id != expected.state_schema_id ||
            actual.state_layout_id != expected.state_layout_id) {
            return false;
        }
    }
    return true;
}

// Process-local C++ object layout. These values are exact for the package
// build being linked, but are not advertised as a cross-compiler ABI.
struct StaticStateLayoutImplementation {
    std::string layout_id;
    std::size_t size_bytes = 0U;
    std::size_t alignment_bytes = 0U;
};

// Process-local layout of one concrete runtime value contract. This is
// allocation/validation information only: it is not a codec, serializer,
// schema registry, or cross-process ABI claim.
struct StaticValueLayoutImplementation {
    std::string contract_id;
    std::size_t size_bytes = 0U;
    std::size_t alignment_bytes = 0U;
    std::string layout_id;
};

struct StaticPackageImplementation {
    std::string package_id;
    std::string package_version;
    std::string build_fingerprint;
    std::vector<StaticImplementationEntry> entries;
    std::vector<StaticStateLayoutImplementation> state_layouts;
    std::vector<StaticValueLayoutImplementation> value_layouts;
};

[[nodiscard]] inline std::string canonical_static_entry_signature(
    StaticEntryKind kind, const StaticModelDescriptor& model,
    std::vector<std::string> input_port_ids = {},
    std::vector<std::string> output_port_ids = {},
    StaticStateReadKind state_read = StaticStateReadKind::None,
    StaticStateWriteKind state_write = StaticStateWriteKind::None) {
    std::sort(input_port_ids.begin(), input_port_ids.end());
    std::sort(output_port_ids.begin(), output_port_ids.end());
    std::map<std::string, const StaticPortDescriptor*> ports;
    for (const auto& port : model.ports) {
        ports.emplace(port.port_id, &port);
    }
    std::ostringstream stream;
    stream << "gnc.static-entry-signature@1|" << to_string(kind) << '|'
           << model.definition.model_id << '@'
           << model.definition.model_version << "|in";
    for (const auto& id : input_port_ids) {
        stream << '|' << id << ':';
        const auto found = ports.find(id);
        if (found != ports.end()) {
            stream << found->second->contract_id << ':'
                   << model_sdk::to_string(found->second->binding_kind) << ':'
                   << model_sdk::to_string(found->second->temporal_relation);
        }
    }
    stream << "|out";
    for (const auto& id : output_port_ids) {
        stream << '|' << id << ':';
        const auto found = ports.find(id);
        if (found != ports.end()) {
            stream << found->second->contract_id << ':'
                   << model_sdk::to_string(found->second->binding_kind) << ':'
                   << model_sdk::to_string(found->second->temporal_relation);
        }
    }
    stream << "|state-read=" << model_sdk::to_string(state_read)
           << "|state-write=" << static_cast<unsigned int>(state_write);
    if (model.runtime_component.has_value() &&
        model.runtime_component->state_owner.has_value()) {
        stream << "|layout="
               << model.runtime_component->state_owner->schema.layout_id;
    }
    if (kind == StaticEntryKind::PureQuery && model.pure_query.has_value()) {
        stream << "|request=" << model.pure_query->request_contract_id;
    }
    if (kind == StaticEntryKind::Closure && model.closure.has_value()) {
        stream << "|request=" << model.closure->request_contract_id;
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_prepare_signature(
    const StaticModelDescriptor& model) {
    return canonical_static_entry_signature(StaticEntryKind::Prepare, model);
}

[[nodiscard]] inline std::string canonical_definition_builder_signature(
    const StaticModelDescriptor& model) {
    std::ostringstream stream;
    stream << "gnc.static-definition-builder-signature@1|"
           << model.definition.model_id << '@'
           << model.definition.model_version << "|config="
           << model.configuration.schema_id << '@'
           << model.configuration.schema_version << "|recipe=";
    if (model.runtime_component.has_value()) {
        stream << model.runtime_component->recipe_id;
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_runtime_cell_factory_signature(
    const StaticModelDescriptor& model) {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    for (const auto& port : model.ports) {
        (port.direction == StaticPortDirection::Input ? inputs : outputs)
            .push_back(port.port_id);
    }
    std::ostringstream stream;
    stream << canonical_static_entry_signature(
                  StaticEntryKind::RuntimeCellFactory, model,
                  std::move(inputs), std::move(outputs))
           << "|config=" << model.configuration.schema_id << '@'
           << model.configuration.schema_version << "|recipe=";
    if (model.runtime_component.has_value()) {
        stream << model.runtime_component->recipe_id
               << "|profile="
               << model_sdk::to_string(model.runtime_component->profile)
               << "|obligations=";
        auto obligations = model.runtime_component->obligations;
        std::sort(obligations.begin(), obligations.end());
        for (const auto obligation : obligations) {
            stream << model_sdk::to_string(obligation) << ',';
        }
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_query_signature(
    const StaticModelDescriptor& model) {
    std::vector<std::string> outputs;
    for (const auto& port : model.ports) {
        if (port.direction == StaticPortDirection::Output) {
            outputs.push_back(port.port_id);
        }
    }
    return canonical_static_entry_signature(StaticEntryKind::PureQuery, model,
                                            {}, std::move(outputs));
}

[[nodiscard]] inline std::string canonical_closure_signature(
    const StaticModelDescriptor& model) {
    std::vector<std::string> outputs;
    for (const auto& port : model.ports) {
        if (port.direction == StaticPortDirection::Output) {
            outputs.push_back(port.port_id);
        }
    }
    return canonical_static_entry_signature(StaticEntryKind::Closure, model,
                                            {}, std::move(outputs));
}

[[nodiscard]] inline std::string canonical_state_codec_signature(
    const StaticModelDescriptor& model) {
    std::ostringstream stream;
    stream << canonical_static_entry_signature(StaticEntryKind::StateCodec,
                                                model);
    if (model.runtime_component.has_value() &&
        model.runtime_component->state_owner.has_value()) {
        const auto& owner = *model.runtime_component->state_owner;
        stream << "|schema=" << owner.schema.schema_id << '@'
               << owner.schema.schema_version << "|layout="
               << owner.schema.layout_id << "|clone="
               << owner.codec.clone_operation_id << "|validate="
               << owner.codec.validate_operation_id << "|finite="
               << owner.codec.finite_validation_operation_id
               << "|invariant="
               << owner.codec.invariant_validation_operation_id
               << "|swap="
               << owner.codec.noexcept_swap_operation_id << "|project="
               << owner.codec.project_operation_id;
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_slot_codec_signature(
    const StaticModelDescriptor& model, const StaticPortDescriptor& port) {
    std::ostringstream stream;
    stream << canonical_static_entry_signature(StaticEntryKind::SlotCodec,
                                                model, {}, {port.port_id});
    if (port.slot_codec.has_value()) {
        stream << "|contract=" << port.contract_id << "|layout="
               << port.slot_codec->layout_id << "|copy="
               << port.slot_codec->copy_operation_id << "|validate="
               << port.slot_codec->validate_operation_id << "|project="
               << port.slot_codec->project_operation_id;
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_result_slot_codec_signature(
    const StaticModelDescriptor& model,
    const StaticRuntimeObligationEntryDescriptor& entry) {
    std::ostringstream stream;
    stream << canonical_static_entry_signature(StaticEntryKind::SlotCodec,
                                                model)
           << "|result-contract=" << entry.result_contract_id;
    if (entry.result_codec.has_value()) {
        stream << "|layout=" << entry.result_codec->layout_id << "|copy="
               << entry.result_codec->copy_operation_id << "|validate="
               << entry.result_codec->validate_operation_id << "|project="
               << entry.result_codec->project_operation_id;
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_initial_state_signature(
    const StaticModelDescriptor& model) {
    return canonical_static_entry_signature(StaticEntryKind::InitialState,
                                            model);
}

[[nodiscard]] inline std::string
canonical_runtime_invocation_requirements_suffix(
    const std::vector<StaticInvocationRequirementDescriptor>& requirements) {
    std::ostringstream stream;
    stream << "|invocations=";
    for (std::size_t ordinal = 0U; ordinal < requirements.size();
         ++ordinal) {
        const auto& requirement = requirements[ordinal];
        stream << ordinal << ':' << requirement.requirement_id << ':'
               << model_sdk::to_string(requirement.kind) << ':'
               << requirement.contract_id << ':'
               << model_sdk::to_string(requirement.cardinality) << ',';
    }
    return stream.str();
}

[[nodiscard]] inline std::string canonical_runtime_entry_signature(
    const StaticModelDescriptor& model,
    const StaticRuntimeObligationEntryDescriptor& entry) {
    StaticEntryKind kind = StaticEntryKind::BoundaryEvaluation;
    switch (entry.obligation) {
    case gnc::contracts::ExecutionObligation::PublishProjection:
        kind = StaticEntryKind::PublishProjection;
        break;
    case gnc::contracts::ExecutionObligation::BoundaryEvaluation:
        kind = StaticEntryKind::BoundaryEvaluation;
        break;
    case gnc::contracts::ExecutionObligation::IntervalEvolution:
        kind = StaticEntryKind::IntervalEvolution;
        break;
    case gnc::contracts::ExecutionObligation::DerivativeEvaluation:
        kind = StaticEntryKind::DerivativeEvaluation;
        break;
    }
    auto signature = canonical_static_entry_signature(
        kind, model, entry.input_port_ids, entry.output_port_ids,
        entry.state_read, entry.state_write) +
           "|phase=" + std::string(model_sdk::to_string(entry.phase)) +
           "|request=" + entry.request_contract_id +
           "|result=" + entry.result_contract_id +
           canonical_runtime_invocation_requirements_suffix(
               entry.invocation_requirements);
    if (entry.result_codec.has_value()) {
        signature += "|result-layout=" + entry.result_codec->layout_id +
                     "|result-codec=" + entry.result_codec->entry_id;
    }
    return signature;
}

template <auto Callable, typename ExpectedCallable>
[[nodiscard]] inline StaticImplementationEntry make_static_implementation_entry(
    std::string entry_id, std::string entry_version, StaticEntryKind kind,
    std::string signature_id,
    StaticCallableContract<ExpectedCallable> callable_contract,
    std::string state_layout_id = {},
    std::string workspace_layout_id = "gnc.workspace.none@1") {
    static_assert(
        std::is_same_v<decltype(Callable), ExpectedCallable>,
        "linked callable pointer type differs from its independent static contract");
    return {std::move(entry_id), std::move(entry_version), kind,
            std::move(signature_id),
            std::move(callable_contract.call_shape_id),
            std::move(state_layout_id),
            std::move(workspace_layout_id),
            std::any(Callable),
            &typeid(ExpectedCallable),
            make_static_link_anchor<Callable>(),
            std::nullopt};
}

} // namespace gnc::model_sdk
