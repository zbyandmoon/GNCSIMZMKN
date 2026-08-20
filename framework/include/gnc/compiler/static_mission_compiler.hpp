#pragma once

#include "gnc/model_sdk/static_descriptor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace gnc::compiler {

inline constexpr std::string_view kTypedStaticCompositionSourceVersion =
    "gnc.typed-static-composition-source/2";

struct SourceRef {
    std::string document_uri;
    std::string node_path;
};

// This IR slice records only the initial lifecycle fact required by the
// accepted YYZ qualification source. Topology and activation transactions
// remain outside the current Compiler surface.
enum class EntityLifecycle : std::uint8_t {
    Unspecified,
    ActiveAtInitialize,
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case EntityLifecycle::Unspecified:
        return "Unspecified";
    case EntityLifecycle::ActiveAtInitialize:
        return "active_at_initialize";
    }
    return "Unknown";
}

enum class ScopeKind : std::uint8_t {
    Unspecified,
    Vehicle,
};

[[nodiscard]] constexpr std::string_view to_string(
    ScopeKind kind) noexcept {
    switch (kind) {
    case ScopeKind::Unspecified:
        return "Unspecified";
    case ScopeKind::Vehicle:
        return "Vehicle";
    }
    return "Unknown";
}

struct ScopeKey {
    ScopeKind kind = ScopeKind::Unspecified;
    std::string subject_entity_id;
};

[[nodiscard]] inline bool operator==(const ScopeKey& lhs,
                                     const ScopeKey& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.subject_entity_id == rhs.subject_entity_id;
}

[[nodiscard]] inline bool operator<(const ScopeKey& lhs,
                                    const ScopeKey& rhs) {
    if (lhs.kind != rhs.kind) {
        return static_cast<std::uint8_t>(lhs.kind) <
               static_cast<std::uint8_t>(rhs.kind);
    }
    return lhs.subject_entity_id < rhs.subject_entity_id;
}

enum class DiagnosticCode : std::uint8_t {
    InvalidCatalogDescriptor,
    DuplicateCatalogIdentity,
    InvalidStaticCompositionSource,
    InvalidEntity,
    DuplicateEntity,
    UnknownSubjectEntity,
    InvalidScope,
    DuplicateScope,
    UnknownScopeEntity,
    UnknownScope,
    PlacementMismatch,
    SubjectScopeMismatch,
    InvalidConfiguration,
    MissingAssetBinding,
    DuplicateAssetBinding,
    UnknownAssetRole,
    AssetSchemaMismatch,
    InvalidAssetIdentity,
    DuplicateOccurrence,
    UnknownDefinition,
    UnknownAlgorithm,
    RuntimeComponentPlanUnavailable,
    MissingSourceReference,
    UnknownEndpoint,
    PortDirectionMismatch,
    ContractMismatch,
    BindingKindMismatch,
    BindingScopeMismatch,
    BindingTemporalMismatch,
    MissingRequiredBinding,
    MultipleRequiredBindings,
    NonCanonicalIr,
};

[[nodiscard]] constexpr std::string_view to_string(
    DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::InvalidCatalogDescriptor:
        return "GNC-CAT-INVALID-DESCRIPTOR";
    case DiagnosticCode::DuplicateCatalogIdentity:
        return "GNC-CAT-DUPLICATE-IDENTITY";
    case DiagnosticCode::InvalidStaticCompositionSource:
        return "GNC-SRC-INVALID-STATIC-COMPOSITION";
    case DiagnosticCode::InvalidEntity:
        return "GNC-IR-INVALID-ENTITY";
    case DiagnosticCode::DuplicateEntity:
        return "GNC-IR-DUPLICATE-ENTITY";
    case DiagnosticCode::UnknownSubjectEntity:
        return "GNC-IR-UNKNOWN-SUBJECT-ENTITY";
    case DiagnosticCode::InvalidScope:
        return "GNC-IR-INVALID-SCOPE";
    case DiagnosticCode::DuplicateScope:
        return "GNC-IR-DUPLICATE-SCOPE";
    case DiagnosticCode::UnknownScopeEntity:
        return "GNC-IR-UNKNOWN-SCOPE-ENTITY";
    case DiagnosticCode::UnknownScope:
        return "GNC-IR-UNKNOWN-SCOPE";
    case DiagnosticCode::PlacementMismatch:
        return "GNC-IR-PLACEMENT-MISMATCH";
    case DiagnosticCode::SubjectScopeMismatch:
        return "GNC-IR-SUBJECT-SCOPE-MISMATCH";
    case DiagnosticCode::InvalidConfiguration:
        return "GNC-IR-INVALID-CONFIGURATION";
    case DiagnosticCode::MissingAssetBinding:
        return "GNC-IR-MISSING-ASSET-BINDING";
    case DiagnosticCode::DuplicateAssetBinding:
        return "GNC-IR-DUPLICATE-ASSET-BINDING";
    case DiagnosticCode::UnknownAssetRole:
        return "GNC-IR-UNKNOWN-ASSET-ROLE";
    case DiagnosticCode::AssetSchemaMismatch:
        return "GNC-IR-ASSET-SCHEMA-MISMATCH";
    case DiagnosticCode::InvalidAssetIdentity:
        return "GNC-IR-INVALID-ASSET-IDENTITY";
    case DiagnosticCode::DuplicateOccurrence:
        return "GNC-IR-DUPLICATE-OCCURRENCE";
    case DiagnosticCode::UnknownDefinition:
        return "GNC-CAT-UNKNOWN-DEFINITION";
    case DiagnosticCode::UnknownAlgorithm:
        return "GNC-CAT-UNKNOWN-ALGORITHM";
    case DiagnosticCode::RuntimeComponentPlanUnavailable:
        return "GNC-PLAN-RUNTIME-COMPONENT-UNAVAILABLE";
    case DiagnosticCode::MissingSourceReference:
        return "GNC-BIND-MISSING-SOURCE-REFERENCE";
    case DiagnosticCode::UnknownEndpoint:
        return "GNC-BIND-UNKNOWN-ENDPOINT";
    case DiagnosticCode::PortDirectionMismatch:
        return "GNC-BIND-PORT-DIRECTION";
    case DiagnosticCode::ContractMismatch:
        return "GNC-BIND-CONTRACT-MISMATCH";
    case DiagnosticCode::BindingKindMismatch:
        return "GNC-BIND-KIND-MISMATCH";
    case DiagnosticCode::BindingScopeMismatch:
        return "GNC-BIND-SCOPE-MISMATCH";
    case DiagnosticCode::BindingTemporalMismatch:
        return "GNC-BIND-TEMPORAL-MISMATCH";
    case DiagnosticCode::MissingRequiredBinding:
        return "GNC-BIND-MISSING-REQUIRED";
    case DiagnosticCode::MultipleRequiredBindings:
        return "GNC-BIND-MULTIPLE-REQUIRED";
    case DiagnosticCode::NonCanonicalIr:
        return "GNC-IR-NONCANONICAL";
    }
    return "GNC-COMPILER-UNKNOWN";
}

struct Diagnostic {
    DiagnosticCode code =
        DiagnosticCode::InvalidStaticCompositionSource;
    SourceRef source;
    std::string subject;
    std::string detail;
};

template <typename Value>
struct CompileOutcome {
    std::optional<Value> value;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept {
        return value.has_value() && diagnostics.empty();
    }
};

struct PackageLock {
    std::string package_id;
    std::string package_version;
    // Empty for portable Catalog records. Complete R2 source selection must
    // resolve this to one exact package build before proof/link.
    std::string build_fingerprint{};
};

struct CatalogModelRecord {
    PackageLock package;
    gnc::model_sdk::StaticModelDescriptor descriptor;
};

struct CatalogAlgorithmRecord {
    PackageLock package;
    gnc::model_sdk::StaticAlgorithmDescriptor descriptor;
};

namespace detail {

[[nodiscard]] inline std::string exact_key(std::string_view identity,
                                           std::string_view version) {
    std::string key(identity);
    key.push_back('\n');
    key.append(version);
    return key;
}

[[nodiscard]] inline SourceRef catalog_source(std::string_view package_id,
                                              std::string_view subject) {
    return {"catalog://" + std::string(package_id), std::string(subject)};
}

inline void validate_ports(
    const std::vector<gnc::model_sdk::StaticPortDescriptor>& ports,
    std::string_view package_id, std::string_view owner,
    gnc::model_sdk::StaticPortDirection supported_direction,
    std::vector<Diagnostic>& diagnostics) {
    if (ports.empty()) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor,
             catalog_source(package_id, owner), std::string(owner),
             "current static composition requires at least one port"});
    }
    std::set<std::string> port_ids;
    for (const auto& port : ports) {
        if (port.port_id.empty() || port.contract_id.empty()) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), std::string(owner),
                 "port identity and contract identity are required"});
        }
        if (!port_ids.insert(port.port_id).second) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), port.port_id,
                 "duplicate port identity within descriptor"});
        }
        if (port.direction != supported_direction) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), port.port_id,
                 "current static composition supports model Output ports "
                 "and algorithm Input ports only"});
        }
        if ((port.binding_kind !=
                 gnc::model_sdk::BindingKind::PureQuery &&
             port.binding_kind !=
                 gnc::model_sdk::BindingKind::ContinuousClosureLink) ||
            !gnc::model_sdk::valid_port_cardinality(port.cardinality) ||
            !gnc::model_sdk::valid_temporal_relation(
                port.temporal_relation)) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), port.port_id,
                 "port binding kind, cardinality, and temporal relation "
                 "must use the supported typed descriptor values"});
        }
        const auto expected_cardinality =
            supported_direction ==
                    gnc::model_sdk::StaticPortDirection::Output
                ? gnc::model_sdk::PortCardinality::OneOrMore
                : gnc::model_sdk::PortCardinality::ExactlyOne;
        if (port.cardinality != expected_cardinality) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), port.port_id,
                 "model outputs require one-or-more consumers and current "
                 "algorithm inputs require exactly one provider"});
        }
        const bool valid_relation =
            (port.binding_kind ==
                 gnc::model_sdk::BindingKind::PureQuery &&
             port.temporal_relation ==
                 gnc::model_sdk::TemporalRelation::NotApplicable) ||
            (port.binding_kind ==
                 gnc::model_sdk::BindingKind::ContinuousClosureLink &&
             (port.temporal_relation ==
                  gnc::model_sdk::TemporalRelation::IntervalModel ||
              port.temporal_relation ==
                  gnc::model_sdk::TemporalRelation::
                      CandidateStateQuery));
        if (!valid_relation) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor,
                 catalog_source(package_id, owner), port.port_id,
                 "PureQuery has no compiled temporal relation while a "
                 "ContinuousClosureLink requires one"});
        }
    }
}

inline void validate_runtime_component(
    const gnc::model_sdk::StaticModelDescriptor& model,
    std::string_view package_id, std::vector<Diagnostic>& diagnostics) {
    const auto& definition = model.definition;
    const auto source =
        catalog_source(package_id, definition.model_id);
    if (!model.runtime_component.has_value()) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "RuntimeComponent requires package-owned runtime facts"});
        return;
    }
    if (model.pure_query.has_value() || model.closure.has_value()) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "RuntimeComponent cannot carry PureQuery or Closure execution "
             "facts"});
    }
    const auto& runtime = *model.runtime_component;
    const auto& schedule = runtime.schedule;
    if (!model.preparation_algorithm_id.empty() ||
        !model.preparation_algorithm_version.empty() ||
        !model.preparation_call_shape_id.empty() ||
        runtime.recipe_id.empty() ||
        runtime.definition_builder_id.empty() ||
        runtime.definition_builder_version.empty() ||
        runtime.definition_builder_call_shape_id.empty() ||
        runtime.runtime_cell_factory_id.empty() ||
        runtime.runtime_cell_factory_version.empty() ||
        runtime.runtime_cell_factory_call_shape_id.empty() ||
        !gnc::model_sdk::valid_runtime_cell_profile(runtime.profile)) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
              "RuntimeComponent recipe, profile, definition builder, and "
              "RuntimeCellFactory must be exact; "
              "prepare-only fields must be empty"});
    }

    std::set<gnc::model_sdk::RuntimeExecutionObligation> obligations;
    for (const auto obligation : runtime.obligations) {
        if (!gnc::model_sdk::valid_runtime_execution_obligation(obligation) ||
            !obligations.insert(obligation).second) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 definition.model_id,
                 "runtime obligations must be supported and unique"});
        }
    }
    std::set<gnc::model_sdk::RuntimeExecutionObligation> entry_obligations;
    std::set<std::string> entry_ids;
    for (const auto& entry : runtime.obligation_entries) {
        if (!gnc::model_sdk::valid_runtime_execution_obligation(
                entry.obligation) ||
            entry.phase == gnc::model_sdk::CoarsePhase::Unspecified ||
            entry.entry_id.empty() || entry.entry_version.empty() ||
            entry.request_contract_id.empty() ||
            entry.result_contract_id.empty() ||
            entry.call_shape_id.empty() ||
            !gnc::model_sdk::valid_static_workspace_requirement(
                entry.workspace_requirement) ||
            !entry_obligations.insert(entry.obligation).second ||
            !entry_ids.insert(exact_key(entry.entry_id,
                                        entry.entry_version)).second) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 entry.entry_id,
                 "runtime obligation entries require unique exact identity, "
                 "request/result contracts, phase, obligation, and workspace "
                 "facts"});
        }
        std::set<std::string> requirement_ids;
        for (const auto& requirement : entry.invocation_requirements) {
            if (requirement.requirement_id.empty() ||
                !gnc::model_sdk::valid_static_invocation_kind(
                    requirement.kind) ||
                requirement.contract_id.empty() ||
                requirement.cardinality !=
                    gnc::model_sdk::PortCardinality::ExactlyOne ||
                !requirement_ids.insert(requirement.requirement_id).second) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     requirement.requirement_id,
                     "invocation requirements require unique identity, exact "
                     "contract, and exactly-one cardinality"});
            }
        }
    }
    if (obligations.empty() || obligations != entry_obligations) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "every runtime obligation requires one exact package entry"});
    }

    const bool sampled = runtime.profile ==
        gnc::model_sdk::RuntimeCellProfile::SampledTransform;
    const bool continuous = runtime.profile ==
        gnc::model_sdk::RuntimeCellProfile::ContinuousStateOwner;
    const bool discrete = runtime.profile ==
        gnc::model_sdk::RuntimeCellProfile::DiscreteStateProcessor;
    const bool evaluator = runtime.profile ==
        gnc::model_sdk::RuntimeCellProfile::Evaluator;
    const bool profile_contract =
        (sampled &&
         (model.placement ==
              gnc::model_sdk::ModelPlacement::VehicleProcess ||
          model.placement ==
              gnc::model_sdk::ModelPlacement::VehicleOutput) &&
         obligations ==
             std::set<gnc::model_sdk::RuntimeExecutionObligation>{
                 gnc::model_sdk::RuntimeExecutionObligation::
                     BoundaryEvaluation} &&
         !runtime.state_owner.has_value()) ||
        (continuous &&
         model.placement ==
             gnc::model_sdk::ModelPlacement::VehicleForm &&
         obligations ==
             std::set<gnc::model_sdk::RuntimeExecutionObligation>{
                 gnc::model_sdk::RuntimeExecutionObligation::
                     PublishProjection,
                 gnc::model_sdk::RuntimeExecutionObligation::
                     BoundaryEvaluation,
                 gnc::model_sdk::RuntimeExecutionObligation::
                     DerivativeEvaluation} &&
         runtime.state_owner.has_value()) ||
        (discrete &&
         model.placement ==
             gnc::model_sdk::ModelPlacement::VehicleOutput &&
         obligations ==
             std::set<gnc::model_sdk::RuntimeExecutionObligation>{
                 gnc::model_sdk::RuntimeExecutionObligation::
                     PublishProjection,
                 gnc::model_sdk::RuntimeExecutionObligation::
                     IntervalEvolution} &&
         runtime.state_owner.has_value()) ||
        (evaluator &&
         model.placement ==
             gnc::model_sdk::ModelPlacement::Evaluation &&
         obligations ==
             std::set<gnc::model_sdk::RuntimeExecutionObligation>{
                 gnc::model_sdk::RuntimeExecutionObligation::
                     BoundaryEvaluation} &&
         !runtime.state_owner.has_value());
    if (!profile_contract) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "runtime profile, placement, state ownership, and obligations "
             "must form one supported product-backed combination"});
    }

    if (runtime.state_owner.has_value()) {
        const auto& owner = *runtime.state_owner;
        const auto& schema = owner.schema;
        const auto expected_evolution = continuous
            ? gnc::model_sdk::StaticStateEvolution::ContinuousCandidate
            : gnc::model_sdk::StaticStateEvolution::IntervalCandidate;
        if (schema.schema_id.empty() || schema.schema_version == 0U ||
            schema.layout_id.empty() || schema.fields.empty() ||
            owner.initial_state_builder_id.empty() ||
            owner.initial_state_builder_version.empty() ||
            owner.initial_state_builder_call_shape_id.empty() ||
            owner.initial_state_input_schema.schema_id.empty() ||
            owner.initial_state_input_schema.schema_version == 0U ||
            owner.initial_state_input_schema.fields.empty() ||
            owner.evolution != expected_evolution) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 definition.model_id,
                 "state owners require exact schema/layout, initial builder, "
                 "and candidate evolution facts"});
        }
        std::set<std::string> field_ids;
        for (const auto& field : schema.fields) {
            if (field.field_id.empty() || field.value_type.empty() ||
                field.unit.empty() || field.frame_role.empty() ||
                !field_ids.insert(field.field_id).second) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     field.field_id,
                     "state schema fields require unique identity, type, "
                     "unit, and frame role"});
            }
        }
        std::set<std::string> initial_field_ids;
        for (const auto& field :
             owner.initial_state_input_schema.fields) {
            if (field.field_id.empty() ||
                !gnc::model_sdk::valid_canonical_config_value_kind(
                    field.value_kind) ||
                !initial_field_ids.insert(field.field_id).second) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     field.field_id,
                     "initial-state input fields require unique identity "
                     "and a supported canonical value kind"});
            }
        }
    }

    const bool periodic_schedule =
        !evaluator &&
        schedule.trigger ==
            gnc::model_sdk::StaticScheduleTrigger::EveryBoundary &&
        schedule.step_interval > 0U &&
        schedule.offset < schedule.step_interval &&
        schedule.output_hold ==
            gnc::model_sdk::HoldPolicy::ZeroOrderHold &&
        schedule.max_input_age_steps == 0U;
    const bool terminal_schedule =
        evaluator &&
        schedule.trigger ==
            gnc::model_sdk::StaticScheduleTrigger::TerminalSequenceReady &&
        schedule.step_interval == 0U && schedule.offset == 0U &&
        schedule.output_hold ==
            gnc::model_sdk::HoldPolicy::ZeroOrderHold &&
        schedule.max_input_age_steps == 0U;
    if (!periodic_schedule && !terminal_schedule) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "runtime schedule requires an exact periodic or "
             "terminal-sequence trigger with zero-order hold and "
             "current-cycle freshness"});
    }
    const std::vector<gnc::model_sdk::RuntimeLifecycleCapability>
        expected_lifecycle{
            gnc::model_sdk::RuntimeLifecycleCapability::Instantiate,
            gnc::model_sdk::RuntimeLifecycleCapability::Dispose};
    if (runtime.lifecycle_capabilities != expected_lifecycle) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "current R2 runtime lifecycle is exactly "
             "Instantiate and Dispose"});
    }
    if (evaluator) {
        if (!runtime.evaluator_history_shape.has_value()) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 definition.model_id,
                 "Evaluator requires one exact committed-history shape"});
        } else {
            const auto& history = *runtime.evaluator_history_shape;
            std::set<std::string> member_ids;
            std::set<std::string> schema_ids;
            bool valid_history = !history.request_contract_id.empty() &&
                                 history.depth > 0U &&
                                 !history.ordered_members.empty();
            for (const auto& member : history.ordered_members) {
                valid_history =
                    valid_history && !member.member_id.empty() &&
                    !member.state_schema_id.empty() &&
                    !member.state_layout_id.empty() &&
                    member_ids.insert(member.member_id).second &&
                    schema_ids.insert(member.state_schema_id).second;
            }
            const auto evaluator_entry = std::find_if(
                runtime.obligation_entries.begin(),
                runtime.obligation_entries.end(), [](const auto& entry) {
                    return entry.obligation ==
                           gnc::model_sdk::RuntimeExecutionObligation::
                               BoundaryEvaluation;
                });
            valid_history =
                valid_history &&
                evaluator_entry != runtime.obligation_entries.end() &&
                evaluator_entry->request_contract_id ==
                    history.request_contract_id;
            if (!valid_history) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     definition.model_id,
                     "Evaluator history requires a positive depth, ordered "
                     "unique state schema/layout members, and the exact "
                     "boundary request contract"});
            }
        }
    } else if (runtime.evaluator_history_shape.has_value()) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "Only an Evaluator may declare a committed-history shape"});
    }
    if (!model.asset_slots.empty()) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "the current RuntimeComponent cannot own prepare-time "
             "asset slots"});
    }

    std::set<std::string> port_ids;
    std::size_t input_count = 0U;
    std::size_t output_count = 0U;
    for (const auto& port : model.ports) {
        const bool valid_semantics =
            (port.binding_kind ==
                 gnc::model_sdk::BindingKind::SampledSignal &&
             port.temporal_relation ==
                 gnc::model_sdk::TemporalRelation::CurrentCycle) ||
            (port.binding_kind ==
                 gnc::model_sdk::BindingKind::IntervalModel &&
             port.temporal_relation ==
                 gnc::model_sdk::TemporalRelation::IntervalModel) ||
            (port.direction ==
                 gnc::model_sdk::StaticPortDirection::Input &&
             port.binding_kind ==
                 gnc::model_sdk::BindingKind::PureQuery &&
             port.temporal_relation ==
                 gnc::model_sdk::TemporalRelation::NotApplicable) ||
            (port.binding_kind ==
                 gnc::model_sdk::BindingKind::ContinuousClosureLink &&
             (port.temporal_relation ==
                  gnc::model_sdk::TemporalRelation::IntervalModel ||
              port.temporal_relation ==
                  gnc::model_sdk::TemporalRelation::CandidateStateQuery));
        if (port.port_id.empty() || port.contract_id.empty() ||
            !port_ids.insert(port.port_id).second || !valid_semantics) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 port.port_id,
                 "runtime ports require unique identities and exact "
                 "sampled, interval, or closure temporal semantics"});
            continue;
        }
        if (port.direction ==
            gnc::model_sdk::StaticPortDirection::Input) {
            ++input_count;
            if (port.cardinality !=
                gnc::model_sdk::PortCardinality::ExactlyOne) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     port.port_id,
                     "runtime input requires exactly-one provider"});
            }
        } else if (port.direction ==
                   gnc::model_sdk::StaticPortDirection::Output) {
            ++output_count;
            if (port.cardinality !=
                gnc::model_sdk::PortCardinality::OneOrMore) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     port.port_id,
                     "runtime output requires one-or-more consumers"});
            }
        } else {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 port.port_id, "runtime port direction is invalid"});
        }
    }
    const bool config_driven_output_source =
        sampled &&
        model.placement == gnc::model_sdk::ModelPlacement::VehicleOutput;
    if ((!config_driven_output_source && input_count == 0U) ||
        output_count == 0U) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidCatalogDescriptor, source,
             definition.model_id,
             "RuntimeComponent requires typed outputs and, unless it is a "
             "config-driven output source, at least one typed input"});
    }

    for (const auto& entry : runtime.obligation_entries) {
        const auto validate_entry_port = [&](const std::string& port_id,
                                             auto direction) {
            const auto port = std::find_if(
                model.ports.begin(), model.ports.end(),
                [&port_id](const auto& candidate) {
                    return candidate.port_id == port_id;
                });
            if (port == model.ports.end() ||
                port->direction != direction) {
                diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, source,
                     port_id,
                     "obligation entry references an absent or wrong-"
                     "direction port"});
            }
        };
        for (const auto& port_id : entry.input_port_ids) {
            validate_entry_port(
                port_id, gnc::model_sdk::StaticPortDirection::Input);
        }
        for (const auto& port_id : entry.output_port_ids) {
            validate_entry_port(
                port_id, gnc::model_sdk::StaticPortDirection::Output);
        }
        gnc::model_sdk::StaticStateReadKind expected_read =
            gnc::model_sdk::StaticStateReadKind::None;
        gnc::model_sdk::StaticStateWriteKind expected_write =
            gnc::model_sdk::StaticStateWriteKind::None;
        auto expected_phase = gnc::model_sdk::CoarsePhase::Evaluation;
        if (continuous) {
            expected_phase = entry.obligation ==
                                     gnc::model_sdk::
                                         RuntimeExecutionObligation::
                                             PublishProjection
                                 ? gnc::model_sdk::CoarsePhase::Publish
                                 : gnc::model_sdk::CoarsePhase::Form;
            expected_read = entry.obligation ==
                                    gnc::model_sdk::
                                        RuntimeExecutionObligation::
                                            DerivativeEvaluation
                                ? gnc::model_sdk::StaticStateReadKind::Candidate
                                : gnc::model_sdk::StaticStateReadKind::Committed;
        } else if (discrete) {
            expected_phase = entry.obligation ==
                                     gnc::model_sdk::
                                         RuntimeExecutionObligation::
                                             PublishProjection
                                 ? gnc::model_sdk::CoarsePhase::Publish
                                 : gnc::model_sdk::CoarsePhase::Form;
            expected_read = gnc::model_sdk::StaticStateReadKind::Committed;
            if (entry.obligation ==
                gnc::model_sdk::RuntimeExecutionObligation::
                    IntervalEvolution) {
                expected_write =
                    gnc::model_sdk::StaticStateWriteKind::IntervalCandidate;
            }
        } else if (sampled) {
            expected_phase = model.placement ==
                                     gnc::model_sdk::ModelPlacement::
                                         VehicleProcess
                                 ? gnc::model_sdk::CoarsePhase::Process
                                 : gnc::model_sdk::CoarsePhase::Output;
        }
        if (entry.phase != expected_phase ||
            entry.state_read != expected_read ||
            entry.state_write != expected_write) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidCatalogDescriptor, source,
                 entry.entry_id,
                 "obligation phase plus state reads/writes must match its "
                 "profile and execution boundary"});
        }
    }
}

template <typename Port>
[[nodiscard]] inline const Port* find_port(
    const std::vector<Port>& ports, std::string_view port_id) {
    const auto found = std::find_if(
        ports.begin(), ports.end(),
        [port_id](const Port& port) {
            return port.port_id == port_id;
        });
    return found == ports.end() ? nullptr : &*found;
}

[[nodiscard]] inline std::string endpoint_key(
    std::string_view occurrence_id, std::string_view port_id) {
    std::string key(occurrence_id);
    key.push_back('\n');
    key.append(port_id);
    return key;
}

} // namespace detail

// An immutable, process-local view assembled from package-owned static
// descriptors. It performs exact lookup only and owns no runtime instances.
class Catalog {
  public:
    [[nodiscard]] static CompileOutcome<Catalog> build(
        std::vector<gnc::model_sdk::StaticPackageDescriptor> packages) {
        CompileOutcome<Catalog> outcome;
        std::vector<CatalogModelRecord> models;
        std::vector<CatalogAlgorithmRecord> algorithms;
        std::set<std::string> package_keys;

        for (auto& package : packages) {
            const auto package_source = detail::catalog_source(
                package.package_id, "package");
            if (package.package_id.empty() || package.package_version.empty()) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor, package_source,
                     package.package_id,
                     "package identity and version are required"});
            }
            if (!package_keys
                     .insert(detail::exact_key(package.package_id,
                                               package.package_version))
                     .second) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::DuplicateCatalogIdentity, package_source,
                     package.package_id,
                     "duplicate exact package contribution"});
            }

            PackageLock lock{package.package_id, package.package_version};
            for (auto& model : package.models) {
                const auto& definition = model.definition;
                if (definition.model_id.empty() ||
                    definition.model_version.empty() ||
                    !gnc::model_sdk::valid_model_execution_form(
                        definition.execution_form)) {
                    outcome.diagnostics.push_back(
                        {DiagnosticCode::InvalidCatalogDescriptor,
                         detail::catalog_source(package.package_id,
                                                definition.model_id),
                         definition.model_id,
                         "model identity and execution form are required"});
                }
                if (!gnc::model_sdk::valid_model_placement(
                        model.placement)) {
                    outcome.diagnostics.push_back(
                        {DiagnosticCode::InvalidCatalogDescriptor,
                         detail::catalog_source(package.package_id,
                                                definition.model_id),
                         definition.model_id,
                         "a supported package-owned placement policy is "
                         "required"});
                }
                const auto& configuration = model.configuration;
                if (configuration.schema_id.empty() ||
                    configuration.schema_version == 0U ||
                    configuration.fields.empty()) {
                    outcome.diagnostics.push_back(
                        {DiagnosticCode::InvalidCatalogDescriptor,
                         detail::catalog_source(package.package_id,
                                                definition.model_id),
                         definition.model_id,
                         "an exact nonempty configuration schema is "
                         "required"});
                }
                std::string previous_config_field;
                for (const auto& field : configuration.fields) {
                    if (field.field_id.empty() ||
                        !gnc::model_sdk::valid_canonical_config_value_kind(
                            field.value_kind) ||
                        (!previous_config_field.empty() &&
                         field.field_id <= previous_config_field)) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             detail::catalog_source(package.package_id,
                                                    definition.model_id),
                             field.field_id,
                             "configuration fields must use supported kinds "
                             "and have unique canonical order"});
                    }
                    previous_config_field = field.field_id;
                }
                std::string previous_asset_role;
                for (const auto& slot : model.asset_slots) {
                    if (slot.role.empty() ||
                        slot.asset_schema_id.empty() ||
                        slot.cardinality !=
                            gnc::model_sdk::PortCardinality::ExactlyOne ||
                        (!previous_asset_role.empty() &&
                         slot.role <= previous_asset_role)) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             detail::catalog_source(package.package_id,
                                                    definition.model_id),
                             slot.role,
                             "asset slots must have identities, exactly-one "
                             "cardinality, and unique canonical order"});
                    }
                    previous_asset_role = slot.role;
                }
                if (definition.execution_form ==
                    gnc::model_sdk::ModelExecutionForm::RuntimeComponent) {
                    detail::validate_runtime_component(
                        model, package.package_id, outcome.diagnostics);
                } else {
                    if (model.preparation_algorithm_id.empty() ||
                        model.preparation_algorithm_version.empty() ||
                        model.preparation_call_shape_id.empty() ||
                        model.runtime_component.has_value()) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             detail::catalog_source(package.package_id,
                                                    definition.model_id),
                             definition.model_id,
                             "PureQuery/Closure require preparation identity "
                             "and cannot carry RuntimeComponent facts"});
                    }
                    if (definition.execution_form ==
                        gnc::model_sdk::ModelExecutionForm::PureQuery) {
                        if (!model.pure_query.has_value() ||
                            model.closure.has_value()) {
                            outcome.diagnostics.push_back(
                                {DiagnosticCode::InvalidCatalogDescriptor,
                                 detail::catalog_source(
                                     package.package_id,
                                     definition.model_id),
                                 definition.model_id,
                                 "PureQuery requires exactly one matching "
                                 "execution descriptor"});
                        } else {
                            const auto& query = *model.pure_query;
                            if (query.query_entry_id.empty() ||
                                query.query_entry_version.empty() ||
                                query.request_contract_id.empty() ||
                                query.query_call_shape_id.empty() ||
                                !gnc::model_sdk::
                                    valid_static_workspace_requirement(
                                        query.workspace_requirement)) {
                                outcome.diagnostics.push_back(
                                    {DiagnosticCode::InvalidCatalogDescriptor,
                                     detail::catalog_source(
                                         package.package_id,
                                         definition.model_id),
                                     definition.model_id,
                                     "PureQuery requires an exact kernel "
                                      "entry, request contract, and explicit "
                                      "workspace fact"});
                            }
                        }
                    } else if (definition.execution_form ==
                               gnc::model_sdk::ModelExecutionForm::Closure) {
                        if (!model.closure.has_value() ||
                            model.pure_query.has_value()) {
                            outcome.diagnostics.push_back(
                                {DiagnosticCode::InvalidCatalogDescriptor,
                                 detail::catalog_source(
                                     package.package_id,
                                     definition.model_id),
                                 definition.model_id,
                                 "Closure requires exactly one matching "
                                 "execution descriptor"});
                        } else {
                            const auto& closure = *model.closure;
                            if (closure.closure_entry_id.empty() ||
                                closure.closure_entry_version.empty() ||
                                closure.request_contract_id.empty() ||
                                closure.closure_call_shape_id.empty() ||
                                !gnc::contracts::valid_closure_strategy(
                                    closure.strategy) ||
                                closure.strategy !=
                                    gnc::contracts::ClosureStrategy::
                                        FrozenInterval ||
                                !gnc::model_sdk::
                                    valid_static_workspace_requirement(
                                        closure.workspace_requirement)) {
                                outcome.diagnostics.push_back(
                                    {DiagnosticCode::InvalidCatalogDescriptor,
                                     detail::catalog_source(
                                         package.package_id,
                                         definition.model_id),
                                     definition.model_id,
                                     "the current Closure slice requires an "
                                      "exact kernel entry, request contract, "
                                      "FrozenInterval strategy, and explicit "
                                      "workspace fact"});
                            }
                        }
                    }
                    detail::validate_ports(
                        model.ports, package.package_id,
                        definition.model_id,
                        gnc::model_sdk::StaticPortDirection::Output,
                        outcome.diagnostics);
                    const auto expected_binding_kind =
                        definition.execution_form ==
                                gnc::model_sdk::ModelExecutionForm::PureQuery
                            ? gnc::model_sdk::BindingKind::PureQuery
                            : gnc::model_sdk::BindingKind::
                                  ContinuousClosureLink;
                    const bool valid_placement =
                        definition.execution_form ==
                                gnc::model_sdk::ModelExecutionForm::PureQuery
                            ? (model.placement ==
                                   gnc::model_sdk::ModelPlacement::
                                       VehicleOutput ||
                               model.placement ==
                                   gnc::model_sdk::ModelPlacement::Environment)
                            : model.placement ==
                                  gnc::model_sdk::ModelPlacement::
                                      InteractionClosure;
                    if (!valid_placement) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             detail::catalog_source(package.package_id,
                                                    definition.model_id),
                             definition.model_id,
                             "PureQuery/Closure placement differs from its "
                             "execution form"});
                    }
                    for (const auto& port : model.ports) {
                        if (port.binding_kind != expected_binding_kind) {
                            outcome.diagnostics.push_back(
                                {DiagnosticCode::InvalidCatalogDescriptor,
                                 detail::catalog_source(
                                     package.package_id,
                                     definition.model_id),
                                 port.port_id,
                                 "model output binding kind differs from its "
                                 "execution form"});
                        }
                        if (definition.execution_form ==
                                gnc::model_sdk::ModelExecutionForm::Closure &&
                            model.closure.has_value()) {
                            const auto strategy = model.closure->strategy;
                            const bool relation_matches_strategy =
                                strategy == gnc::contracts::
                                                ClosureStrategy::
                                                    FrozenInterval &&
                                port.temporal_relation ==
                                    gnc::model_sdk::TemporalRelation::
                                        IntervalModel;
                            if (!relation_matches_strategy) {
                                outcome.diagnostics.push_back(
                                    {DiagnosticCode::InvalidCatalogDescriptor,
                                     detail::catalog_source(
                                         package.package_id,
                                         definition.model_id),
                                     port.port_id,
                                     "Closure strategy and output temporal "
                                     "relation must describe the same "
                                     "evaluation boundary"});
                            }
                        }
                    }
                }
                models.push_back({lock, std::move(model)});
            }

            for (auto& algorithm : package.algorithms) {
                if (algorithm.algorithm_id.empty() ||
                    algorithm.algorithm_version.empty() ||
                    !gnc::model_sdk::valid_static_workspace_requirement(
                        algorithm.workspace_requirement)) {
                    outcome.diagnostics.push_back(
                        {DiagnosticCode::InvalidCatalogDescriptor,
                         detail::catalog_source(package.package_id,
                                                algorithm.algorithm_id),
                         algorithm.algorithm_id,
                         "algorithm identity, version, and workspace facts "
                         "are required"});
                }
                std::set<std::string> invocation_requirement_ids;
                for (const auto& requirement :
                     algorithm.invocation_requirements) {
                    if (requirement.requirement_id.empty() ||
                        !gnc::model_sdk::valid_static_invocation_kind(
                            requirement.kind) ||
                        requirement.contract_id.empty() ||
                        requirement.cardinality !=
                            gnc::model_sdk::PortCardinality::ExactlyOne ||
                        !invocation_requirement_ids
                             .insert(requirement.requirement_id)
                             .second) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             detail::catalog_source(package.package_id,
                                                    algorithm.algorithm_id),
                             requirement.requirement_id,
                             "algorithm invocation requirements require "
                             "unique identity, supported kind, exact "
                             "contract, and exactly-one cardinality"});
                    }
                }
                detail::validate_ports(
                    algorithm.ports, package.package_id,
                    algorithm.algorithm_id,
                    gnc::model_sdk::StaticPortDirection::Input,
                    outcome.diagnostics);
                algorithms.push_back({lock, std::move(algorithm)});
            }
        }

        const auto model_less = [](const CatalogModelRecord& lhs,
                                   const CatalogModelRecord& rhs) {
            return detail::exact_key(lhs.descriptor.definition.model_id,
                                     lhs.descriptor.definition.model_version) <
                   detail::exact_key(rhs.descriptor.definition.model_id,
                                     rhs.descriptor.definition.model_version);
        };
        std::sort(models.begin(), models.end(), model_less);
        for (std::size_t index = 1U; index < models.size(); ++index) {
            if (!model_less(models[index - 1U], models[index]) &&
                !model_less(models[index], models[index - 1U])) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::DuplicateCatalogIdentity,
                     detail::catalog_source(
                         models[index].package.package_id,
                         models[index].descriptor.definition.model_id),
                     models[index].descriptor.definition.model_id,
                     "duplicate exact model definition"});
            }
        }

        const auto algorithm_less = [](const CatalogAlgorithmRecord& lhs,
                                       const CatalogAlgorithmRecord& rhs) {
            return detail::exact_key(lhs.descriptor.algorithm_id,
                                     lhs.descriptor.algorithm_version) <
                   detail::exact_key(rhs.descriptor.algorithm_id,
                                     rhs.descriptor.algorithm_version);
        };
        std::sort(algorithms.begin(), algorithms.end(), algorithm_less);
        for (std::size_t index = 1U; index < algorithms.size(); ++index) {
            if (!algorithm_less(algorithms[index - 1U], algorithms[index]) &&
                !algorithm_less(algorithms[index], algorithms[index - 1U])) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::DuplicateCatalogIdentity,
                     detail::catalog_source(
                         algorithms[index].package.package_id,
                         algorithms[index].descriptor.algorithm_id),
                     algorithms[index].descriptor.algorithm_id,
                     "duplicate exact algorithm descriptor"});
            }
        }

        if (outcome.diagnostics.empty()) {
            outcome.value = Catalog(std::move(models),
                                    std::move(algorithms));
        }
        return outcome;
    }

    [[nodiscard]] const CatalogModelRecord* find_model(
        std::string_view model_id, std::string_view version) const {
        const auto key = detail::exact_key(model_id, version);
        const auto found = std::lower_bound(
            models_.begin(), models_.end(), key,
            [](const CatalogModelRecord& record, const std::string& value) {
                return detail::exact_key(
                           record.descriptor.definition.model_id,
                           record.descriptor.definition.model_version) < value;
            });
        if (found == models_.end() ||
            detail::exact_key(found->descriptor.definition.model_id,
                              found->descriptor.definition.model_version) !=
                key) {
            return nullptr;
        }
        return &*found;
    }

    [[nodiscard]] const CatalogAlgorithmRecord* find_algorithm(
        std::string_view algorithm_id,
        std::string_view version) const {
        const auto key = detail::exact_key(algorithm_id, version);
        const auto found = std::lower_bound(
            algorithms_.begin(), algorithms_.end(), key,
            [](const CatalogAlgorithmRecord& record,
               const std::string& value) {
                return detail::exact_key(record.descriptor.algorithm_id,
                                         record.descriptor.algorithm_version) <
                       value;
            });
        if (found == algorithms_.end() ||
            detail::exact_key(found->descriptor.algorithm_id,
                              found->descriptor.algorithm_version) != key) {
            return nullptr;
        }
        return &*found;
    }

  private:
    Catalog(std::vector<CatalogModelRecord> models,
            std::vector<CatalogAlgorithmRecord> algorithms) noexcept
        : models_(std::move(models)), algorithms_(std::move(algorithms)) {}

    std::vector<CatalogModelRecord> models_;
    std::vector<CatalogAlgorithmRecord> algorithms_;
};

struct SourceEntity {
    std::string entity_id;
    EntityLifecycle lifecycle = EntityLifecycle::Unspecified;
    SourceRef identity_source;
    SourceRef lifecycle_source;
};

struct SourceScope {
    ScopeKey key;
    SourceRef source;
};

struct SourceConfigFieldProvenance {
    std::string field_id;
    SourceRef source;
};

struct SourceAssetBinding {
    std::string role;
    std::string asset_schema_id;
    std::string asset_id;
    SourceRef source;
};

struct SourceModelOccurrence {
    std::string occurrence_id;
    std::string model_id;
    std::string model_version;
    SourceRef source;
    std::string subject_entity_id;
    SourceRef subject_source;
    std::optional<ScopeKey> scope;
    SourceRef scope_source;
    gnc::model_sdk::ModelPlacement placement =
        gnc::model_sdk::ModelPlacement::Unspecified;
    SourceRef placement_source;
    gnc::model_sdk::CanonicalConfigBlock configuration;
    SourceRef configuration_source;
    std::vector<SourceConfigFieldProvenance>
        configuration_field_sources;
    std::vector<SourceAssetBinding> asset_bindings;

    SourceModelOccurrence() = default;

    SourceModelOccurrence(std::string occurrence_identity,
                          std::string model_identity,
                          std::string version,
                          SourceRef occurrence_source,
                          std::string subject_identity,
                          SourceRef subject_provenance)
        : occurrence_id(std::move(occurrence_identity)),
          model_id(std::move(model_identity)),
          model_version(std::move(version)),
          source(std::move(occurrence_source)),
          subject_entity_id(std::move(subject_identity)),
          subject_source(std::move(subject_provenance)) {}
};

// A stateless kernel used as a binding consumer in the current conformance
// slice. It is not a ModelDefinition-backed model occurrence.
struct SourceAlgorithmConsumer {
    std::string consumer_id;
    std::string algorithm_id;
    std::string algorithm_version;
    SourceRef source;
    std::optional<ScopeKey> scope;
    SourceRef scope_source;
};

struct SourceBinding {
    std::string binding_id;
    std::string provider_occurrence_id;
    std::string provider_port_id;
    std::string consumer_id;
    std::string consumer_port_id;
    SourceRef source;
};

// Programmatic entity/subject/identity/binding input for the current R2
// slices. This is not the syntax-neutral SourceTree defined by the target
// Source Frontend.
struct TypedStaticCompositionSource {
    std::string source_version;
    std::string mission_id;
    SourceRef mission_source;
    std::string plan_id;
    std::vector<SourceEntity> entities;
    std::vector<SourceScope> scopes;
    std::vector<SourceModelOccurrence> model_occurrences;
    std::vector<SourceAlgorithmConsumer> algorithm_consumers;
    std::vector<SourceBinding> binding_intents;
};

struct CanonicalPort {
    std::string port_id;
    std::string contract_id;
    gnc::model_sdk::BindingKind binding_kind =
        gnc::model_sdk::BindingKind::Unspecified;
    gnc::model_sdk::PortCardinality cardinality =
        gnc::model_sdk::PortCardinality::Unspecified;
    gnc::model_sdk::TemporalRelation temporal_relation =
        gnc::model_sdk::TemporalRelation::NotApplicable;
};

struct CanonicalEntity {
    std::string entity_id;
    EntityLifecycle lifecycle = EntityLifecycle::Unspecified;
    SourceRef identity_source;
    SourceRef lifecycle_source;
};

struct CanonicalScope {
    ScopeKey key;
    SourceRef source;
};

struct CanonicalConfigFieldProvenance {
    std::string field_id;
    SourceRef source;
};

struct CanonicalAssetBinding {
    std::string role;
    std::string asset_schema_id;
    std::string asset_id;
    gnc::model_sdk::PortCardinality cardinality =
        gnc::model_sdk::PortCardinality::ExactlyOne;
    SourceRef source;
};

struct CanonicalModelOccurrence {
    std::string occurrence_id;
    PackageLock package;
    std::string model_id;
    std::string model_version;
    gnc::model_sdk::ModelExecutionForm execution_form =
        gnc::model_sdk::ModelExecutionForm::Unspecified;
    std::string preparation_algorithm_id;
    std::string preparation_algorithm_version;
    std::vector<CanonicalPort> output_ports;
    SourceRef source;
    std::string subject_entity_id;
    SourceRef subject_source;
    std::optional<ScopeKey> scope;
    SourceRef scope_source;
    gnc::model_sdk::ModelPlacement placement =
        gnc::model_sdk::ModelPlacement::Unspecified;
    SourceRef placement_source;
    gnc::model_sdk::CanonicalConfigBlock configuration;
    SourceRef configuration_source;
    std::vector<CanonicalConfigFieldProvenance>
        configuration_field_sources;
    std::vector<CanonicalAssetBinding> asset_bindings;
};

struct CanonicalAlgorithmConsumer {
    std::string consumer_id;
    PackageLock package;
    std::string algorithm_id;
    std::string algorithm_version;
    std::vector<CanonicalPort> input_ports;
    SourceRef source;
    std::optional<ScopeKey> scope;
    SourceRef scope_source;
};

struct CanonicalBindingIntent {
    std::string binding_id;
    std::string provider_occurrence_id;
    std::string provider_port_id;
    std::string consumer_id;
    std::string consumer_port_id;
    SourceRef source;
};

// The executable R2 canonical graph. Source locations remain as provenance,
// while semantic encoding excludes representation-specific locations and
// plan identity. Topology, activation and runtime instances remain outside.
struct CanonicalMissionIr {
    std::uint32_t revision = 2U;
    std::string mission_id;
    SourceRef mission_source;
    std::vector<CanonicalEntity> entities;
    std::vector<CanonicalScope> scopes;
    std::vector<CanonicalModelOccurrence> model_occurrences;
    std::vector<CanonicalAlgorithmConsumer> algorithm_consumers;
    std::vector<CanonicalBindingIntent> binding_intents;
};

enum class BindingProofResult : std::uint8_t {
    Proven,
};

enum class BindingEndpointKind : std::uint8_t {
    Asset,
    PreparedModel,
    ModelOccurrence,
    AlgorithmConsumer,
};

[[nodiscard]] constexpr std::string_view to_string(
    BindingEndpointKind kind) noexcept {
    switch (kind) {
    case BindingEndpointKind::Asset:
        return "Asset";
    case BindingEndpointKind::PreparedModel:
        return "PreparedModel";
    case BindingEndpointKind::ModelOccurrence:
        return "ModelOccurrence";
    case BindingEndpointKind::AlgorithmConsumer:
        return "AlgorithmConsumer";
    }
    return "Unknown";
}

struct BindingEndpoint {
    BindingEndpointKind kind = BindingEndpointKind::ModelOccurrence;
    std::string owner_id;
    std::string port_or_role_id;
};

enum class BindingPhase : std::uint8_t {
    PrepareTime,
    Evaluation,
};

[[nodiscard]] constexpr std::string_view to_string(
    BindingPhase phase) noexcept {
    switch (phase) {
    case BindingPhase::PrepareTime:
        return "prepare-time";
    case BindingPhase::Evaluation:
        return "evaluation";
    }
    return "Unknown";
}

struct BindingScopeResolution {
    ScopeKey resolved_scope;
};

struct AssetBindingFacts {
    std::string role;
    std::string asset_schema_id;
    std::string asset_id;
};

struct BindingPlanEntry {
    std::string binding_id;
    gnc::model_sdk::BindingKind binding_kind =
        gnc::model_sdk::BindingKind::Unspecified;
    BindingEndpoint provider_endpoint;
    BindingEndpoint consumer_endpoint;
    std::string exact_contract_id;
    gnc::model_sdk::PortCardinality provider_cardinality =
        gnc::model_sdk::PortCardinality::Unspecified;
    gnc::model_sdk::PortCardinality consumer_cardinality =
        gnc::model_sdk::PortCardinality::Unspecified;
    BindingPhase phase = BindingPhase::Evaluation;
    std::optional<BindingScopeResolution> scope_resolution;
    std::optional<AssetBindingFacts> asset_binding;
    SourceRef source;
};

struct BindingPlan {
    std::vector<BindingPlanEntry> entries;
};

struct TemporalBindingPlanEntry {
    std::string binding_id;
    gnc::model_sdk::TemporalRelation relation =
        gnc::model_sdk::TemporalRelation::NotApplicable;
    SourceRef source;
};

struct TemporalBindingPlan {
    std::vector<TemporalBindingPlanEntry> entries;
};

enum class BindingProofAssertion : std::uint8_t {
    EndpointsResolved,
    KindCompatible,
    ContractExact,
    CardinalitySatisfied,
    SourceSelectedAssetIdentityPreserved,
    ScopeExact,
    TemporalCompatible,
    SourceLocated,
};

[[nodiscard]] constexpr std::string_view to_string(
    BindingProofAssertion assertion) noexcept {
    switch (assertion) {
    case BindingProofAssertion::EndpointsResolved:
        return "EndpointsResolved";
    case BindingProofAssertion::KindCompatible:
        return "KindCompatible";
    case BindingProofAssertion::ContractExact:
        return "ContractExact";
    case BindingProofAssertion::CardinalitySatisfied:
        return "CardinalitySatisfied";
    case BindingProofAssertion::SourceSelectedAssetIdentityPreserved:
        return "SourceSelectedAssetIdentityPreserved";
    case BindingProofAssertion::ScopeExact:
        return "ScopeExact";
    case BindingProofAssertion::TemporalCompatible:
        return "TemporalCompatible";
    case BindingProofAssertion::SourceLocated:
        return "SourceLocated";
    }
    return "Unknown";
}

struct BindingProof {
    std::string proof_id;
    std::string binding_id;
    gnc::model_sdk::BindingKind binding_kind =
        gnc::model_sdk::BindingKind::Unspecified;
    std::string exact_contract_id;
    std::vector<BindingProofAssertion> assertions;
    std::vector<SourceRef> source_refs;
    BindingProofResult result = BindingProofResult::Proven;
};

struct PreparedModelPreparationInputs {
    std::string preparation_input_id;
    std::string occurrence_id;
    PackageLock package;
    std::string model_id;
    std::string model_version;
    gnc::model_sdk::ModelExecutionForm execution_form =
        gnc::model_sdk::ModelExecutionForm::Unspecified;
    std::string preparation_algorithm_id;
    std::string preparation_algorithm_version;
    gnc::model_sdk::CanonicalConfigBlock canonical_configuration;
    std::vector<std::string> asset_binding_ids;
    std::vector<SourceRef> source_refs;
};

// These facts describe result flow through an already-resolved BindingPlan.
// They neither grant a BoundQuery handle nor identify the eventual invocation
// caller; that authority belongs to a future QueryPlan/link step.
struct QueryConsumerBindingFacts {
    std::string binding_id;
    BindingEndpoint provider_endpoint;
    BindingEndpoint consumer_endpoint;
    std::string exact_contract_id;
    std::optional<BindingScopeResolution> scope_resolution;
    SourceRef source;
};

struct QueryExecutionSpecInputs {
    std::string query_execution_input_id;
    std::string occurrence_id;
    std::string preparation_input_ref;
    std::string query_entry_id;
    std::string query_entry_version;
    gnc::model_sdk::StaticWorkspaceRequirement workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::Unspecified;
    std::vector<QueryConsumerBindingFacts> consumer_bindings;
    std::vector<SourceRef> source_refs;
};

// A closure data consumer is not necessarily the code authorized to invoke a
// closure handle. Invocation context remains a ClosurePlan/IntegrationScope
// concern and is intentionally absent from this R2 slice.
struct ClosureConsumerBindingFacts {
    std::string binding_id;
    BindingEndpoint provider_endpoint;
    BindingEndpoint consumer_endpoint;
    std::string exact_contract_id;
    std::optional<BindingScopeResolution> scope_resolution;
    gnc::model_sdk::TemporalRelation temporal_relation =
        gnc::model_sdk::TemporalRelation::NotApplicable;
    SourceRef source;
};

struct ClosureExecutionSpecInputs {
    std::string closure_execution_input_id;
    std::string occurrence_id;
    std::string preparation_input_ref;
    std::string closure_entry_id;
    std::string closure_entry_version;
    gnc::contracts::ClosureStrategy strategy =
        gnc::contracts::ClosureStrategy::Unspecified;
    gnc::model_sdk::StaticWorkspaceRequirement workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::Unspecified;
    std::vector<ClosureConsumerBindingFacts> consumer_bindings;
    std::vector<SourceRef> source_refs;
};

struct AlgorithmConsumerPlan {
    std::string consumer_id;
    PackageLock package;
    std::string algorithm_id;
    std::string algorithm_version;
    SourceRef source;
    std::optional<ScopeKey> scope;
    SourceRef scope_source;
};

enum class CompiledObligationKind : std::uint8_t {
    PureQueryEvaluation,
    ClosureEvaluation,
};

[[nodiscard]] constexpr std::string_view to_string(
    CompiledObligationKind kind) noexcept {
    switch (kind) {
    case CompiledObligationKind::PureQueryEvaluation:
        return "PureQueryEvaluation";
    case CompiledObligationKind::ClosureEvaluation:
        return "ClosureEvaluation";
    }
    return "Unknown";
}

struct CompiledObligation {
    std::string obligation_id;
    std::size_t ordinal = 0U;
    CompiledObligationKind kind =
        CompiledObligationKind::PureQueryEvaluation;
    BindingEndpoint provider_endpoint;
    BindingEndpoint consumer_endpoint;
    std::string binding_id;
    std::string execution_input_ref;
};

// This portable descriptor shape freezes canonical preparation and execution
// inputs in addition to typed result-flow bindings and obligations. It does
// not claim a PreparedModelPlan, QueryPlan, ClosurePlan, handle authorization,
// numeric slot, ABI layout, linked entry, runtime instance, Session, or state.
struct ExecutionPlanDescriptor {
    std::uint32_t revision = 3U;
    std::string plan_id;
    std::string mission_id;
    std::vector<PackageLock> dependency_lock;
    std::vector<PreparedModelPreparationInputs> prepared_model_inputs;
    std::vector<QueryExecutionSpecInputs> query_execution_inputs;
    std::vector<ClosureExecutionSpecInputs> closure_execution_inputs;
    std::vector<AlgorithmConsumerPlan> algorithms;
    BindingPlan binding_plan;
    TemporalBindingPlan temporal_binding_plan;
    std::vector<BindingProof> binding_proofs;
    std::vector<CompiledObligation> obligations;
};

struct StaticCompilation {
    CanonicalMissionIr ir;
    ExecutionPlanDescriptor plan;
};

namespace detail {

[[nodiscard]] inline std::vector<CanonicalPort> canonical_ports(
    const std::vector<gnc::model_sdk::StaticPortDescriptor>& ports) {
    std::vector<CanonicalPort> result;
    result.reserve(ports.size());
    for (const auto& port : ports) {
        result.push_back({port.port_id, port.contract_id,
                          port.binding_kind, port.cardinality,
                          port.temporal_relation});
    }
    std::sort(result.begin(), result.end(),
              [](const CanonicalPort& lhs, const CanonicalPort& rhs) {
                  return lhs.port_id < rhs.port_id;
              });
    return result;
}

[[nodiscard]] inline bool valid_source_ref(
    const SourceRef& source) noexcept {
    return !source.document_uri.empty() && !source.node_path.empty();
}

[[nodiscard]] inline SourceRef diagnostic_source(
    const SourceRef& preferred, const SourceRef& fallback) {
    if (valid_source_ref(preferred)) {
        return preferred;
    }
    if (valid_source_ref(fallback)) {
        return fallback;
    }
    return {"typed://mission", "/"};
}

[[nodiscard]] inline bool valid_canonical_config_value(
    const gnc::model_sdk::CanonicalConfigValue& value) noexcept {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return !text->empty();
    }
    if (const auto* token =
            std::get_if<gnc::model_sdk::CanonicalEnumValue>(&value)) {
        return !token->token.empty();
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return std::isfinite(*number) &&
               !(*number == 0.0 && std::signbit(*number));
    }
    return true;
}

inline bool canonicalize_configuration(
    const SourceModelOccurrence& source,
    const gnc::model_sdk::StaticModelDescriptor& descriptor,
    gnc::model_sdk::CanonicalConfigBlock& configuration,
    std::vector<CanonicalConfigFieldProvenance>& provenance,
    std::vector<Diagnostic>& diagnostics) {
    const auto fail = [&](const SourceRef& location,
                          std::string detail) {
        diagnostics.push_back(
            {DiagnosticCode::InvalidConfiguration,
             diagnostic_source(location, source.source),
             source.occurrence_id, std::move(detail)});
    };
    configuration = source.configuration;
    if (configuration.schema_id !=
            descriptor.configuration.schema_id ||
        configuration.schema_version !=
            descriptor.configuration.schema_version ||
        !valid_source_ref(source.configuration_source)) {
        fail(source.configuration_source,
             "configuration schema identity/version or provenance differs "
             "from the package descriptor");
        return false;
    }
    std::sort(configuration.fields.begin(), configuration.fields.end(),
              [](const gnc::model_sdk::CanonicalConfigField& lhs,
                 const gnc::model_sdk::CanonicalConfigField& rhs) {
                  return lhs.field_id < rhs.field_id;
              });
    if (configuration.fields.size() !=
        descriptor.configuration.fields.size()) {
        fail(source.configuration_source,
             "configuration field set differs from the exact schema");
        return false;
    }
    for (std::size_t index = 0U;
         index < configuration.fields.size(); ++index) {
        const auto& actual = configuration.fields[index];
        const auto& expected = descriptor.configuration.fields[index];
        if (actual.field_id != expected.field_id ||
            gnc::model_sdk::canonical_config_value_kind(actual.value) !=
                expected.value_kind ||
            !valid_canonical_config_value(actual.value)) {
            fail(source.configuration_source,
                 "configuration field identity, type, or canonical value "
                 "differs from the exact schema");
            return false;
        }
    }

    auto field_sources = source.configuration_field_sources;
    std::sort(field_sources.begin(), field_sources.end(),
              [](const SourceConfigFieldProvenance& lhs,
                 const SourceConfigFieldProvenance& rhs) {
                  return lhs.field_id < rhs.field_id;
              });
    if (field_sources.size() != configuration.fields.size()) {
        fail(source.configuration_source,
             "every canonical configuration field requires one provenance "
             "reference");
        return false;
    }
    provenance.clear();
    provenance.reserve(field_sources.size());
    for (std::size_t index = 0U; index < field_sources.size(); ++index) {
        if (field_sources[index].field_id !=
                configuration.fields[index].field_id ||
            !valid_source_ref(field_sources[index].source)) {
            fail(field_sources[index].source,
                 "configuration field provenance is missing, duplicated, "
                 "or attached to another field");
            return false;
        }
        provenance.push_back(
            {field_sources[index].field_id,
             field_sources[index].source});
    }
    return true;
}

inline bool canonicalize_assets(
    const SourceModelOccurrence& source,
    const gnc::model_sdk::StaticModelDescriptor& descriptor,
    std::vector<CanonicalAssetBinding>& assets,
    std::vector<Diagnostic>& diagnostics) {
    auto bindings = source.asset_bindings;
    std::sort(bindings.begin(), bindings.end(),
              [](const SourceAssetBinding& lhs,
                 const SourceAssetBinding& rhs) {
                  return lhs.role < rhs.role;
              });
    for (std::size_t index = 1U; index < bindings.size(); ++index) {
        if (bindings[index - 1U].role == bindings[index].role) {
            diagnostics.push_back(
                {DiagnosticCode::DuplicateAssetBinding,
                 diagnostic_source(bindings[index].source, source.source),
                 source.occurrence_id,
                 "an asset role is bound more than once"});
            return false;
        }
    }
    if (bindings.size() < descriptor.asset_slots.size()) {
        diagnostics.push_back(
            {DiagnosticCode::MissingAssetBinding, source.source,
             source.occurrence_id,
             "a required package asset role is unbound"});
        return false;
    }
    if (bindings.size() > descriptor.asset_slots.size()) {
        diagnostics.push_back(
            {DiagnosticCode::UnknownAssetRole,
             diagnostic_source(
                 bindings[descriptor.asset_slots.size()].source,
                 source.source),
             source.occurrence_id,
             "the source binds an asset role absent from the package "
             "descriptor"});
        return false;
    }
    assets.clear();
    assets.reserve(bindings.size());
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        const auto& slot = descriptor.asset_slots[index];
        if (binding.role != slot.role) {
            diagnostics.push_back(
                {DiagnosticCode::UnknownAssetRole,
                 diagnostic_source(binding.source, source.source),
                 source.occurrence_id,
                 "the source asset role differs from the package slot"});
            return false;
        }
        if (binding.asset_schema_id != slot.asset_schema_id) {
            diagnostics.push_back(
                {DiagnosticCode::AssetSchemaMismatch,
                 diagnostic_source(binding.source, source.source),
                 source.occurrence_id,
                 "the asset schema differs from the package slot"});
            return false;
        }
        if (binding.asset_id.empty()) {
            diagnostics.push_back(
                {DiagnosticCode::InvalidAssetIdentity,
                 diagnostic_source(binding.source, source.source),
                 source.occurrence_id,
                 "source-selected asset identity must be nonempty"});
            return false;
        }
        if (!valid_source_ref(binding.source)) {
            diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 diagnostic_source(binding.source, source.source),
                 source.occurrence_id,
                 "asset binding provenance is required"});
            return false;
        }
        assets.push_back({binding.role, binding.asset_schema_id,
                          binding.asset_id, slot.cardinality,
                          binding.source});
    }
    return true;
}

} // namespace detail

[[nodiscard]] inline CompileOutcome<CanonicalMissionIr>
build_canonical_mission_ir(const TypedStaticCompositionSource& source,
                           const Catalog& catalog) {
    CompileOutcome<CanonicalMissionIr> outcome;
    if (source.source_version != kTypedStaticCompositionSourceVersion ||
        source.mission_id.empty()) {
        outcome.diagnostics.push_back(
            {DiagnosticCode::InvalidStaticCompositionSource,
             {"typed://mission", "/"}, source.mission_id,
             "static composition source version and mission identity are "
             "required"});
        return outcome;
    }
    if (!detail::valid_source_ref(source.mission_source)) {
        outcome.diagnostics.push_back(
            {DiagnosticCode::MissingSourceReference,
             {"typed://mission", "/mission_source"}, source.mission_id,
             "mission source location is required"});
        return outcome;
    }
    if (source.model_occurrences.empty() &&
        source.algorithm_consumers.empty() &&
        source.binding_intents.empty()) {
        outcome.diagnostics.push_back(
            {DiagnosticCode::InvalidStaticCompositionSource,
             {"typed://mission", "/"}, source.mission_id,
             "static composition must contain at least one model, "
             "algorithm, or binding"});
        return outcome;
    }

    CanonicalMissionIr ir;
    ir.mission_id = source.mission_id;
    ir.mission_source = source.mission_source;

    std::set<std::string> entity_ids;
    auto entity_sources = source.entities;
    std::stable_sort(entity_sources.begin(), entity_sources.end(),
                     [](const SourceEntity& lhs,
                        const SourceEntity& rhs) {
                         return lhs.entity_id < rhs.entity_id;
                     });
    for (const auto& entity : entity_sources) {
        if (!detail::valid_source_ref(entity.identity_source) ||
            !detail::valid_source_ref(entity.lifecycle_source)) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 source.mission_source, entity.entity_id,
                 "entity identity and lifecycle source locations are "
                 "required"});
            continue;
        }
        if (entity.entity_id.empty()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::InvalidEntity, entity.identity_source,
                 entity.entity_id, "entity identity is required"});
            continue;
        }
        if (!entity_ids.insert(entity.entity_id).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::DuplicateEntity, entity.identity_source,
                 entity.entity_id, "entity identity is duplicated"});
            continue;
        }
        if (entity.lifecycle != EntityLifecycle::ActiveAtInitialize) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::InvalidEntity, entity.lifecycle_source,
                 entity.entity_id,
                 "current canonical IR requires active_at_initialize"});
            continue;
        }
        ir.entities.push_back(
            {entity.entity_id, entity.lifecycle, entity.identity_source,
             entity.lifecycle_source});
    }

    std::set<ScopeKey> scope_keys;
    auto scope_sources = source.scopes;
    std::sort(scope_sources.begin(), scope_sources.end(),
              [](const SourceScope& lhs, const SourceScope& rhs) {
                  return lhs.key < rhs.key;
              });
    for (const auto& scope : scope_sources) {
        if (!detail::valid_source_ref(scope.source)) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 source.mission_source, scope.key.subject_entity_id,
                 "scope source location is required"});
            continue;
        }
        if (scope.key.kind != ScopeKind::Vehicle ||
            scope.key.subject_entity_id.empty()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::InvalidScope, scope.source,
                 scope.key.subject_entity_id,
                 "current canonical IR supports a typed Vehicle scope "
                 "anchored by subject entity identity"});
            continue;
        }
        if (!scope_keys.insert(scope.key).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::DuplicateScope, scope.source,
                 scope.key.subject_entity_id,
                 "the exact Vehicle scope is declared more than once"});
            continue;
        }
        if (entity_ids.find(scope.key.subject_entity_id) ==
            entity_ids.end()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownScopeEntity, scope.source,
                 scope.key.subject_entity_id,
                 "Vehicle scope subject entity is absent from the IR"});
            continue;
        }
        ir.scopes.push_back({scope.key, scope.source});
    }

    std::set<std::string> composition_node_ids;

    auto model_sources = source.model_occurrences;
    std::sort(model_sources.begin(), model_sources.end(),
              [](const SourceModelOccurrence& lhs,
                 const SourceModelOccurrence& rhs) {
                  return lhs.occurrence_id < rhs.occurrence_id;
              });
    for (const auto& model : model_sources) {
        if (!detail::valid_source_ref(model.source) ||
            (!model.subject_entity_id.empty() &&
             !detail::valid_source_ref(model.subject_source)) ||
            (model.scope.has_value() &&
             !detail::valid_source_ref(model.scope_source)) ||
            (model.placement !=
                 gnc::model_sdk::ModelPlacement::Unspecified &&
             !detail::valid_source_ref(model.placement_source))) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 source.mission_source, model.occurrence_id,
                 "model, subject, scope, and explicit placement source "
                 "locations are required when those facts are present"});
            continue;
        }
        if (model.occurrence_id.empty() ||
            !composition_node_ids.insert(model.occurrence_id).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::DuplicateOccurrence, model.source,
                 model.occurrence_id,
                 "occurrence identity is empty or duplicated"});
            continue;
        }
        if (!model.subject_entity_id.empty() &&
            entity_ids.find(model.subject_entity_id) == entity_ids.end()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownSubjectEntity,
                 model.subject_source, model.occurrence_id,
                 "model occurrence subject entity is absent from the IR"});
            continue;
        }
        if (model.scope.has_value() &&
            scope_keys.find(*model.scope) == scope_keys.end()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownScope, model.scope_source,
                 model.occurrence_id,
                 "model occurrence references an undeclared typed scope"});
            continue;
        }
        if (model.scope.has_value() &&
            (model.scope->kind != ScopeKind::Vehicle ||
             model.subject_entity_id !=
                 model.scope->subject_entity_id)) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::SubjectScopeMismatch,
                 model.scope_source, model.occurrence_id,
                 "Vehicle scope and model subject entity differ"});
            continue;
        }
        const auto* catalog_model =
            catalog.find_model(model.model_id, model.model_version);
        if (catalog_model == nullptr) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownDefinition, model.source,
                 model.occurrence_id,
                 "exact model definition is absent from the Catalog"});
            continue;
        }
        const auto& descriptor = catalog_model->descriptor;
        if (descriptor.definition.execution_form ==
            gnc::model_sdk::ModelExecutionForm::RuntimeComponent) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::RuntimeComponentPlanUnavailable,
                 model.source, model.occurrence_id,
                 "Catalog resolved the RuntimeComponent definition, but "
                 "this compiler slice cannot freeze an unclosed runtime "
                 "component graph"});
            continue;
        }
        if (model.placement !=
                gnc::model_sdk::ModelPlacement::Unspecified &&
            model.placement != descriptor.placement) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::PlacementMismatch,
                 model.placement_source, model.occurrence_id,
                 "source placement differs from the package-owned model "
                 "placement policy"});
            continue;
        }
        gnc::model_sdk::CanonicalConfigBlock configuration;
        std::vector<CanonicalConfigFieldProvenance>
            configuration_provenance;
        if (!detail::canonicalize_configuration(
                model, descriptor, configuration,
                configuration_provenance, outcome.diagnostics)) {
            continue;
        }
        std::vector<CanonicalAssetBinding> assets;
        if (!detail::canonicalize_assets(
                model, descriptor, assets, outcome.diagnostics)) {
            continue;
        }

        CanonicalModelOccurrence canonical;
        canonical.occurrence_id = model.occurrence_id;
        canonical.package = catalog_model->package;
        canonical.model_id = descriptor.definition.model_id;
        canonical.model_version = descriptor.definition.model_version;
        canonical.execution_form =
            descriptor.definition.execution_form;
        canonical.preparation_algorithm_id =
            descriptor.preparation_algorithm_id;
        canonical.preparation_algorithm_version =
            descriptor.preparation_algorithm_version;
        canonical.output_ports =
            detail::canonical_ports(descriptor.ports);
        canonical.source = model.source;
        canonical.subject_entity_id = model.subject_entity_id;
        canonical.subject_source = model.subject_source;
        canonical.scope = model.scope;
        canonical.scope_source = model.scope_source;
        canonical.placement = descriptor.placement;
        canonical.placement_source =
            model.placement ==
                    gnc::model_sdk::ModelPlacement::Unspecified
                ? detail::catalog_source(
                      catalog_model->package.package_id,
                      descriptor.definition.model_id)
                : model.placement_source;
        canonical.configuration = std::move(configuration);
        canonical.configuration_source = model.configuration_source;
        canonical.configuration_field_sources =
            std::move(configuration_provenance);
        canonical.asset_bindings = std::move(assets);
        ir.model_occurrences.push_back(std::move(canonical));
    }

    auto algorithm_sources = source.algorithm_consumers;
    std::sort(algorithm_sources.begin(), algorithm_sources.end(),
              [](const SourceAlgorithmConsumer& lhs,
                 const SourceAlgorithmConsumer& rhs) {
                  return lhs.consumer_id < rhs.consumer_id;
              });
    for (const auto& algorithm : algorithm_sources) {
        if (!detail::valid_source_ref(algorithm.source) ||
            (algorithm.scope.has_value() &&
             !detail::valid_source_ref(algorithm.scope_source))) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 source.mission_source, algorithm.consumer_id,
                 "algorithm consumer and declared scope source locations "
                 "are required"});
            continue;
        }
        if (algorithm.consumer_id.empty() ||
            !composition_node_ids.insert(algorithm.consumer_id).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::DuplicateOccurrence, algorithm.source,
                 algorithm.consumer_id,
                 "composition node identity is empty or duplicated"});
            continue;
        }
        if (algorithm.scope.has_value() &&
            scope_keys.find(*algorithm.scope) == scope_keys.end()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownScope, algorithm.scope_source,
                 algorithm.consumer_id,
                 "algorithm consumer references an undeclared typed "
                 "scope"});
            continue;
        }
        const auto* catalog_algorithm = catalog.find_algorithm(
            algorithm.algorithm_id, algorithm.algorithm_version);
        if (catalog_algorithm == nullptr) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownAlgorithm, algorithm.source,
                 algorithm.consumer_id,
                 "exact algorithm descriptor is absent from the Catalog"});
            continue;
        }
        const auto& descriptor = catalog_algorithm->descriptor;
        ir.algorithm_consumers.push_back(
            {algorithm.consumer_id,
             catalog_algorithm->package,
             descriptor.algorithm_id,
             descriptor.algorithm_version,
             detail::canonical_ports(descriptor.ports),
             algorithm.source, algorithm.scope,
             algorithm.scope_source});
    }

    auto binding_sources = source.binding_intents;
    std::sort(binding_sources.begin(), binding_sources.end(),
              [](const SourceBinding& lhs, const SourceBinding& rhs) {
                  return lhs.binding_id < rhs.binding_id;
              });
    std::set<std::string> binding_ids;
    for (const auto& binding : binding_sources) {
        if (!detail::valid_source_ref(binding.source)) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::MissingSourceReference,
                 source.mission_source, binding.binding_id,
                 "every source-authored binding requires a direct source "
                 "location"});
            continue;
        }
        if (binding.binding_id.empty() ||
            !binding_ids.insert(binding.binding_id).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::InvalidStaticCompositionSource,
                 binding.source,
                 binding.binding_id,
                 "binding identity is empty or duplicated"});
            continue;
        }
        ir.binding_intents.push_back(
            {binding.binding_id,
             binding.provider_occurrence_id,
             binding.provider_port_id,
             binding.consumer_id,
             binding.consumer_port_id,
             binding.source});
    }

    if (outcome.diagnostics.empty()) {
        outcome.value.emplace(std::move(ir));
    }
    return outcome;
}

// Human-readable canonical semantic view for direct IR regression tests and
// dry-run inspection. This is not a persistence or wire serialization format.
[[nodiscard]] inline std::string explain_canonical_mission_ir(
    const CanonicalMissionIr& ir) {
    std::ostringstream stream;
    stream << "mission-ir " << ir.revision << " mission " << ir.mission_id
           << '\n';
    for (const auto& entity : ir.entities) {
        stream << "entity " << entity.entity_id << ' '
               << to_string(entity.lifecycle) << '\n';
    }
    for (const auto& scope : ir.scopes) {
        stream << "scope " << to_string(scope.key.kind) << " subject "
               << scope.key.subject_entity_id << '\n';
    }
    for (const auto& model : ir.model_occurrences) {
        stream << "model " << model.occurrence_id << ' '
               << model.package.package_id << '@'
               << model.package.package_version << ' ' << model.model_id
               << '@' << model.model_version << ' '
               << gnc::model_sdk::to_string(model.execution_form)
               << " preparation " << model.preparation_algorithm_id << '@'
               << model.preparation_algorithm_version << " placement "
               << gnc::model_sdk::to_string(model.placement);
        if (!model.subject_entity_id.empty()) {
            stream << " subject " << model.subject_entity_id;
        }
        if (model.scope.has_value()) {
            stream << " scope " << to_string(model.scope->kind) << ':'
                   << model.scope->subject_entity_id;
        }
        stream << '\n';
        stream << "config " << model.occurrence_id << ' '
               << model.configuration.schema_id << '@'
               << model.configuration.schema_version << '\n';
        for (const auto& field : model.configuration.fields) {
            stream << "config-field " << model.occurrence_id << '.'
                   << field.field_id << ' '
                   << gnc::model_sdk::to_string(
                          gnc::model_sdk::canonical_config_value_kind(
                              field.value))
                   << ' ';
            if (const auto* text =
                    std::get_if<std::string>(&field.value)) {
                stream << *text;
            } else if (const auto* integer =
                           std::get_if<std::int64_t>(&field.value)) {
                stream << *integer;
            } else if (const auto* token =
                           std::get_if<
                               gnc::model_sdk::CanonicalEnumValue>(
                               &field.value)) {
                stream << token->token;
            } else {
                stream << std::setprecision(17)
                       << std::get<double>(field.value);
            }
            stream << '\n';
        }
        for (const auto& asset : model.asset_bindings) {
            stream << "asset " << model.occurrence_id << '.'
                   << asset.role << ' ' << asset.asset_schema_id << ' '
                   << asset.asset_id << " cardinality "
                   << gnc::model_sdk::to_string(asset.cardinality)
                   << '\n';
        }
        for (const auto& port : model.output_ports) {
            stream << "output " << model.occurrence_id << '.'
                   << port.port_id << ' ' << port.contract_id << ' '
                   << gnc::model_sdk::to_string(port.binding_kind)
                   << " cardinality "
                   << gnc::model_sdk::to_string(port.cardinality);
            if (port.temporal_relation !=
                gnc::model_sdk::TemporalRelation::NotApplicable) {
                stream << " temporal "
                       << gnc::model_sdk::to_string(
                              port.temporal_relation);
            }
            stream << '\n';
        }
    }
    for (const auto& algorithm : ir.algorithm_consumers) {
        stream << "algorithm-consumer " << algorithm.consumer_id << ' '
               << algorithm.package.package_id << '@'
               << algorithm.package.package_version << ' '
               << algorithm.algorithm_id << '@'
               << algorithm.algorithm_version;
        if (algorithm.scope.has_value()) {
            stream << " scope " << to_string(algorithm.scope->kind)
                   << ':' << algorithm.scope->subject_entity_id;
        }
        stream << '\n';
        for (const auto& port : algorithm.input_ports) {
            stream << "input " << algorithm.consumer_id << '.'
                   << port.port_id << ' ' << port.contract_id << ' '
                   << gnc::model_sdk::to_string(port.binding_kind)
                   << " cardinality "
                   << gnc::model_sdk::to_string(port.cardinality);
            if (port.temporal_relation !=
                gnc::model_sdk::TemporalRelation::NotApplicable) {
                stream << " temporal "
                       << gnc::model_sdk::to_string(
                              port.temporal_relation);
            }
            stream << '\n';
        }
    }
    for (const auto& binding : ir.binding_intents) {
        stream << "intent " << binding.binding_id << ' '
               << binding.provider_occurrence_id << '.'
               << binding.provider_port_id << " -> "
               << binding.consumer_id << '.'
               << binding.consumer_port_id << '\n';
    }
    return stream.str();
}

[[nodiscard]] inline CompileOutcome<StaticCompilation> compile_static_plan(
    const TypedStaticCompositionSource& source, const Catalog& catalog) {
    CompileOutcome<StaticCompilation> outcome;
    auto ir_outcome = build_canonical_mission_ir(source, catalog);
    if (!ir_outcome.succeeded()) {
        outcome.diagnostics = std::move(ir_outcome.diagnostics);
        return outcome;
    }
    auto ir = std::move(*ir_outcome.value);
    if (source.plan_id.empty()) {
        outcome.diagnostics.push_back(
            {DiagnosticCode::InvalidStaticCompositionSource,
             {"typed://mission", "/plan_id"}, source.mission_id,
             "plan identity is required for static plan lowering"});
        return outcome;
    }

    std::map<std::string, const CanonicalModelOccurrence*> model_by_id;
    for (const auto& model : ir.model_occurrences) {
        model_by_id.emplace(model.occurrence_id, &model);
    }
    std::map<std::string, const CanonicalAlgorithmConsumer*>
        algorithm_by_id;
    for (const auto& algorithm : ir.algorithm_consumers) {
        algorithm_by_id.emplace(algorithm.consumer_id, &algorithm);
    }

    std::vector<BindingPlanEntry> resolved_bindings;
    std::vector<TemporalBindingPlanEntry> temporal_bindings;
    std::vector<BindingProof> proofs;
    std::map<std::string, std::size_t> provider_counts;
    std::map<std::string, std::size_t> consumer_counts;
    std::set<std::string> resolved_binding_ids;

    const auto append_source = [](std::vector<SourceRef>& refs,
                                  const SourceRef& source_ref) {
        if (!detail::valid_source_ref(source_ref)) {
            return;
        }
        const auto found = std::find_if(
            refs.begin(), refs.end(),
            [&source_ref](const SourceRef& existing) {
                return existing.document_uri == source_ref.document_uri &&
                       existing.node_path == source_ref.node_path;
            });
        if (found == refs.end()) {
            refs.push_back(source_ref);
        }
    };
    const auto sort_sources = [](std::vector<SourceRef>& refs) {
        std::sort(refs.begin(), refs.end(),
                  [](const SourceRef& lhs, const SourceRef& rhs) {
                      return std::tie(lhs.document_uri, lhs.node_path) <
                             std::tie(rhs.document_uri, rhs.node_path);
                  });
    };

    // Asset slots are first-class prepare-time bindings. Their source facts
    // are already canonicalized against package-owned role/schema metadata.
    for (const auto& model : ir.model_occurrences) {
        for (const auto& asset : model.asset_bindings) {
            const std::string binding_id =
                "asset." + model.occurrence_id + "." + asset.role;
            if (!resolved_binding_ids.insert(binding_id).second) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::InvalidStaticCompositionSource,
                     asset.source, binding_id,
                     "resolved asset binding identity is duplicated"});
                continue;
            }

            BindingPlanEntry entry;
            entry.binding_id = binding_id;
            entry.binding_kind =
                gnc::model_sdk::BindingKind::AssetBinding;
            entry.provider_endpoint = {
                BindingEndpointKind::Asset, asset.asset_id, asset.role};
            entry.consumer_endpoint = {
                BindingEndpointKind::PreparedModel,
                model.occurrence_id, asset.role};
            entry.exact_contract_id = asset.asset_schema_id;
            entry.provider_cardinality = asset.cardinality;
            entry.consumer_cardinality = asset.cardinality;
            entry.phase = BindingPhase::PrepareTime;
            entry.asset_binding = AssetBindingFacts{
                asset.role, asset.asset_schema_id, asset.asset_id};
            entry.source = asset.source;
            resolved_bindings.push_back(std::move(entry));

            std::vector<SourceRef> source_refs;
            append_source(source_refs, asset.source);
            append_source(source_refs, model.source);
            proofs.push_back(
                {"proof.binding." + binding_id,
                 binding_id,
                 gnc::model_sdk::BindingKind::AssetBinding,
                 asset.asset_schema_id,
                 {BindingProofAssertion::EndpointsResolved,
                  BindingProofAssertion::KindCompatible,
                  BindingProofAssertion::ContractExact,
                  BindingProofAssertion::CardinalitySatisfied,
                  BindingProofAssertion::
                      SourceSelectedAssetIdentityPreserved,
                  BindingProofAssertion::SourceLocated},
                 std::move(source_refs),
                 BindingProofResult::Proven});
        }
    }

    for (const auto& intent : ir.binding_intents) {
        const auto& binding = intent;
        if (!resolved_binding_ids.insert(binding.binding_id).second) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::InvalidStaticCompositionSource,
                 binding.source, binding.binding_id,
                 "binding identity collides with another resolved binding"});
            continue;
        }
        const auto model_found =
            model_by_id.find(binding.provider_occurrence_id);
        const auto algorithm_found =
            algorithm_by_id.find(binding.consumer_id);
        if (model_found == model_by_id.end() ||
            algorithm_found == algorithm_by_id.end()) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownEndpoint, binding.source,
                 binding.binding_id,
                 "binding must connect a model provider to an algorithm "
                 "consumer"});
            continue;
        }

        const auto* provider_port = detail::find_port(
            model_found->second->output_ports,
            binding.provider_port_id);
        const auto* consumer_port = detail::find_port(
            algorithm_found->second->input_ports,
            binding.consumer_port_id);
        if (provider_port == nullptr || consumer_port == nullptr) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownEndpoint, binding.source,
                 binding.binding_id,
                 "binding references an unknown provider or consumer port"});
            continue;
        }
        if (provider_port->contract_id != consumer_port->contract_id) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::ContractMismatch, binding.source,
                 binding.binding_id,
                 "provider and consumer contract identities differ"});
            continue;
        }
        if (provider_port->binding_kind !=
            consumer_port->binding_kind) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::BindingKindMismatch, binding.source,
                 binding.binding_id,
                 "provider and consumer binding kinds differ"});
            continue;
        }
        if (provider_port->binding_kind !=
                gnc::model_sdk::BindingKind::PureQuery &&
            provider_port->binding_kind !=
                gnc::model_sdk::BindingKind::ContinuousClosureLink) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::BindingKindMismatch, binding.source,
                 binding.binding_id,
                 "source-authored model-to-algorithm binding requires a "
                 "PureQuery or ContinuousClosureLink"});
            continue;
        }
        if (provider_port->temporal_relation !=
            consumer_port->temporal_relation) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::BindingTemporalMismatch, binding.source,
                 binding.binding_id,
                 "provider and consumer temporal relations differ"});
            continue;
        }

        std::optional<BindingScopeResolution> scope_resolution;
        const auto& provider_scope = model_found->second->scope;
        const auto& consumer_scope = algorithm_found->second->scope;
        if (provider_scope.has_value() != consumer_scope.has_value() ||
            (provider_scope.has_value() &&
             !(*provider_scope == *consumer_scope))) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::BindingScopeMismatch, binding.source,
                 binding.binding_id,
                 "provider and consumer endpoint scopes differ"});
            continue;
        }
        if (provider_scope.has_value()) {
            scope_resolution =
                BindingScopeResolution{*provider_scope};
        }

        const auto provider_key = detail::endpoint_key(
            binding.provider_occurrence_id, binding.provider_port_id);
        const auto consumer_key = detail::endpoint_key(
            binding.consumer_id, binding.consumer_port_id);
        ++provider_counts[provider_key];
        ++consumer_counts[consumer_key];
        BindingPlanEntry entry;
        entry.binding_id = binding.binding_id;
        entry.binding_kind = provider_port->binding_kind;
        entry.provider_endpoint = {
            BindingEndpointKind::ModelOccurrence,
            binding.provider_occurrence_id, binding.provider_port_id};
        entry.consumer_endpoint = {
            BindingEndpointKind::AlgorithmConsumer,
            binding.consumer_id, binding.consumer_port_id};
        entry.exact_contract_id = provider_port->contract_id;
        entry.provider_cardinality = provider_port->cardinality;
        entry.consumer_cardinality = consumer_port->cardinality;
        entry.phase = BindingPhase::Evaluation;
        entry.scope_resolution = scope_resolution;
        entry.source = binding.source;
        resolved_bindings.push_back(std::move(entry));

        std::vector<BindingProofAssertion> assertions{
            BindingProofAssertion::EndpointsResolved,
            BindingProofAssertion::KindCompatible,
            BindingProofAssertion::ContractExact,
            BindingProofAssertion::CardinalitySatisfied,
            BindingProofAssertion::SourceLocated,
        };
        std::vector<SourceRef> source_refs;
        append_source(source_refs, model_found->second->source);
        append_source(source_refs, binding.source);
        append_source(source_refs, algorithm_found->second->source);
        if (scope_resolution.has_value()) {
            assertions.push_back(BindingProofAssertion::ScopeExact);
            append_source(source_refs,
                          model_found->second->scope_source);
            append_source(source_refs,
                          algorithm_found->second->scope_source);
        }
        if (provider_port->binding_kind ==
            gnc::model_sdk::BindingKind::ContinuousClosureLink) {
            assertions.push_back(
                BindingProofAssertion::TemporalCompatible);
            temporal_bindings.push_back(
                {binding.binding_id,
                 provider_port->temporal_relation,
                 binding.source});
        }
        proofs.push_back(
            {"proof.binding." + binding.binding_id,
             binding.binding_id,
             provider_port->binding_kind,
             provider_port->contract_id,
             std::move(assertions), std::move(source_refs),
             BindingProofResult::Proven});
    }

    for (const auto& model : ir.model_occurrences) {
        for (const auto& port : model.output_ports) {
            if (provider_counts[detail::endpoint_key(
                    model.occurrence_id, port.port_id)] == 0U) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::MissingRequiredBinding, model.source,
                     model.occurrence_id + "." + port.port_id,
                     "required model output has no consumer"});
            }
        }
    }
    for (const auto& algorithm : ir.algorithm_consumers) {
        for (const auto& port : algorithm.input_ports) {
            const auto count = consumer_counts[detail::endpoint_key(
                algorithm.consumer_id, port.port_id)];
            if (count == 0U) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::MissingRequiredBinding, algorithm.source,
                     algorithm.consumer_id + "." + port.port_id,
                     "required algorithm input has no provider"});
            } else if (count > 1U) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::MultipleRequiredBindings,
                     algorithm.source,
                     algorithm.consumer_id + "." + port.port_id,
                     "required algorithm input has multiple providers"});
            }
        }
    }

    if (!outcome.diagnostics.empty()) {
        return outcome;
    }

    const auto by_binding_id = [](const auto& lhs, const auto& rhs) {
        return lhs.binding_id < rhs.binding_id;
    };
    std::sort(resolved_bindings.begin(), resolved_bindings.end(),
              by_binding_id);
    std::sort(temporal_bindings.begin(), temporal_bindings.end(),
              by_binding_id);
    std::sort(proofs.begin(), proofs.end(), by_binding_id);

    ExecutionPlanDescriptor plan;
    plan.plan_id = source.plan_id;
    plan.mission_id = source.mission_id;
    plan.binding_plan.entries = std::move(resolved_bindings);
    plan.temporal_binding_plan.entries =
        std::move(temporal_bindings);
    plan.binding_proofs = std::move(proofs);

    std::map<std::string, PackageLock> dependency_locks;
    for (const auto& model : ir.model_occurrences) {
        dependency_locks.emplace(
            detail::exact_key(model.package.package_id,
                              model.package.package_version),
            model.package);
        PreparedModelPreparationInputs prepared;
        prepared.preparation_input_id =
            "preparation-input." + model.occurrence_id;
        prepared.occurrence_id = model.occurrence_id;
        prepared.package = model.package;
        prepared.model_id = model.model_id;
        prepared.model_version = model.model_version;
        prepared.execution_form = model.execution_form;
        prepared.preparation_algorithm_id =
            model.preparation_algorithm_id;
        prepared.preparation_algorithm_version =
            model.preparation_algorithm_version;
        prepared.canonical_configuration = model.configuration;
        append_source(prepared.source_refs, model.source);
        append_source(prepared.source_refs, model.subject_source);
        if (model.scope.has_value()) {
            append_source(prepared.source_refs, model.scope_source);
        }
        append_source(prepared.source_refs, model.placement_source);
        append_source(prepared.source_refs, model.configuration_source);
        for (const auto& field : model.configuration_field_sources) {
            append_source(prepared.source_refs, field.source);
        }
        for (const auto& asset : model.asset_bindings) {
            prepared.asset_binding_ids.push_back(
                "asset." + model.occurrence_id + "." + asset.role);
            append_source(prepared.source_refs, asset.source);
        }
        append_source(
            prepared.source_refs,
            detail::catalog_source(model.package.package_id,
                                   model.model_id));
        sort_sources(prepared.source_refs);
        plan.prepared_model_inputs.push_back(std::move(prepared));

        const auto* catalog_model = catalog.find_model(
            model.model_id, model.model_version);
        if (catalog_model == nullptr) {
            outcome.diagnostics.push_back(
                {DiagnosticCode::UnknownDefinition, model.source,
                 model.occurrence_id,
                 "resolved model definition disappeared before plan "
                 "lowering"});
            continue;
        }
        const auto& static_descriptor = catalog_model->descriptor;
        if (model.execution_form ==
            gnc::model_sdk::ModelExecutionForm::PureQuery) {
            if (!static_descriptor.pure_query.has_value()) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor,
                     model.source, model.occurrence_id,
                     "PureQuery execution descriptor is unavailable during "
                     "plan lowering"});
                continue;
            }
            const auto& execution = *static_descriptor.pure_query;
            QueryExecutionSpecInputs query;
            query.query_execution_input_id =
                "query-execution-input." + model.occurrence_id;
            query.occurrence_id = model.occurrence_id;
            query.preparation_input_ref =
                "preparation-input." + model.occurrence_id;
            query.query_entry_id = execution.query_entry_id;
            query.query_entry_version = execution.query_entry_version;
            query.workspace_requirement =
                execution.workspace_requirement;
            query.source_refs =
                plan.prepared_model_inputs.back().source_refs;
            for (const auto& binding : plan.binding_plan.entries) {
                if (binding.binding_kind ==
                        gnc::model_sdk::BindingKind::PureQuery &&
                    binding.provider_endpoint.owner_id ==
                        model.occurrence_id) {
                    query.consumer_bindings.push_back(
                        {binding.binding_id,
                         binding.provider_endpoint,
                         binding.consumer_endpoint,
                         binding.exact_contract_id,
                         binding.scope_resolution,
                         binding.source});
                    append_source(query.source_refs, binding.source);
                }
            }
            sort_sources(query.source_refs);
            plan.query_execution_inputs.push_back(std::move(query));
        } else if (model.execution_form ==
                   gnc::model_sdk::ModelExecutionForm::Closure) {
            if (!static_descriptor.closure.has_value()) {
                outcome.diagnostics.push_back(
                    {DiagnosticCode::InvalidCatalogDescriptor,
                     model.source, model.occurrence_id,
                     "Closure execution descriptor is unavailable during "
                     "plan lowering"});
                continue;
            }
            const auto& execution = *static_descriptor.closure;
            ClosureExecutionSpecInputs closure;
            closure.closure_execution_input_id =
                "closure-execution-input." + model.occurrence_id;
            closure.occurrence_id = model.occurrence_id;
            closure.preparation_input_ref =
                "preparation-input." + model.occurrence_id;
            closure.closure_entry_id = execution.closure_entry_id;
            closure.closure_entry_version =
                execution.closure_entry_version;
            closure.strategy = execution.strategy;
            closure.workspace_requirement =
                execution.workspace_requirement;
            closure.source_refs =
                plan.prepared_model_inputs.back().source_refs;
            for (const auto& binding : plan.binding_plan.entries) {
                if (binding.binding_kind ==
                        gnc::model_sdk::BindingKind::
                            ContinuousClosureLink &&
                    binding.provider_endpoint.owner_id ==
                        model.occurrence_id) {
                    const auto temporal = std::find_if(
                        plan.temporal_binding_plan.entries.begin(),
                        plan.temporal_binding_plan.entries.end(),
                        [&binding](const auto& entry) {
                            return entry.binding_id == binding.binding_id;
                        });
                    if (temporal ==
                        plan.temporal_binding_plan.entries.end()) {
                        outcome.diagnostics.push_back(
                            {DiagnosticCode::InvalidCatalogDescriptor,
                             binding.source, binding.binding_id,
                             "resolved closure binding has no temporal "
                             "plan entry"});
                        continue;
                    }
                    closure.consumer_bindings.push_back(
                        {binding.binding_id,
                         binding.provider_endpoint,
                         binding.consumer_endpoint,
                         binding.exact_contract_id,
                         binding.scope_resolution,
                         temporal->relation,
                         binding.source});
                    append_source(closure.source_refs, binding.source);
                }
            }
            sort_sources(closure.source_refs);
            plan.closure_execution_inputs.push_back(std::move(closure));
        }
    }
    for (const auto& algorithm : ir.algorithm_consumers) {
        dependency_locks.emplace(
            detail::exact_key(algorithm.package.package_id,
                              algorithm.package.package_version),
            algorithm.package);
        plan.algorithms.push_back(
            {algorithm.consumer_id, algorithm.package,
             algorithm.algorithm_id,
             algorithm.algorithm_version,
             algorithm.source, algorithm.scope,
             algorithm.scope_source});
    }
    for (const auto& dependency : dependency_locks) {
        plan.dependency_lock.push_back(dependency.second);
    }

    if (!outcome.diagnostics.empty()) {
        return outcome;
    }
    std::size_t obligation_ordinal = 0U;
    for (const auto& binding : plan.binding_plan.entries) {
        if (binding.binding_kind ==
            gnc::model_sdk::BindingKind::AssetBinding) {
            continue;
        }
        const auto kind =
            binding.binding_kind ==
                    gnc::model_sdk::BindingKind::PureQuery
                ? CompiledObligationKind::PureQueryEvaluation
                : CompiledObligationKind::ClosureEvaluation;
        plan.obligations.push_back(
            {"obligation." + binding.binding_id,
             obligation_ordinal++, kind,
             binding.provider_endpoint,
             binding.consumer_endpoint,
             binding.binding_id,
             (kind == CompiledObligationKind::PureQueryEvaluation
                  ? "query-execution-input."
                  : "closure-execution-input.") +
                 binding.provider_endpoint.owner_id});
    }

    outcome.value.emplace(
        StaticCompilation{std::move(ir), std::move(plan)});
    return outcome;
}

[[nodiscard]] inline std::string explain_static_plan(
    const ExecutionPlanDescriptor& plan) {
    std::ostringstream stream;
    stream << "plan " << plan.plan_id << " mission " << plan.mission_id
           << '\n';
    for (const auto& dependency : plan.dependency_lock) {
        stream << "lock " << dependency.package_id << '@'
               << dependency.package_version << '\n';
    }
    for (const auto& model : plan.prepared_model_inputs) {
        stream << "prepare-input " << model.occurrence_id << ' '
               << model.model_id << '@' << model.model_version << ' '
               << gnc::model_sdk::to_string(model.execution_form)
               << " id " << model.preparation_input_id
               << " preparation "
               << model.preparation_algorithm_id << '@'
               << model.preparation_algorithm_version << " config "
               << model.canonical_configuration.schema_id << '@'
               << model.canonical_configuration.schema_version;
        for (const auto& asset_binding_id : model.asset_binding_ids) {
            stream << " asset-binding " << asset_binding_id;
        }
        stream << '\n';
    }
    for (const auto& algorithm : plan.algorithms) {
        stream << "consumer " << algorithm.consumer_id << ' '
               << algorithm.algorithm_id << '@'
               << algorithm.algorithm_version;
        if (algorithm.scope.has_value()) {
            stream << " scope " << to_string(algorithm.scope->kind)
                   << ':' << algorithm.scope->subject_entity_id;
        }
        stream << '\n';
    }
    const auto write_endpoint = [&stream](const BindingEndpoint& endpoint) {
        stream << to_string(endpoint.kind) << ':' << endpoint.owner_id
               << '.' << endpoint.port_or_role_id;
    };
    for (const auto& query : plan.query_execution_inputs) {
        stream << "query-execution-input "
               << query.query_execution_input_id << " occurrence "
               << query.occurrence_id << " preparation-input "
               << query.preparation_input_ref << " entry "
               << query.query_entry_id << '@'
               << query.query_entry_version << " workspace "
               << gnc::model_sdk::to_string(
                      query.workspace_requirement)
               << '\n';
        for (const auto& consumer : query.consumer_bindings) {
            stream << "query-consumer "
                   << query.query_execution_input_id << ' '
                   << consumer.binding_id << ' ';
            write_endpoint(consumer.provider_endpoint);
            stream << " -> ";
            write_endpoint(consumer.consumer_endpoint);
            stream << " contract " << consumer.exact_contract_id << '\n';
        }
    }
    for (const auto& closure : plan.closure_execution_inputs) {
        stream << "closure-execution-input "
               << closure.closure_execution_input_id
               << " occurrence " << closure.occurrence_id
               << " preparation-input "
               << closure.preparation_input_ref
               << " entry " << closure.closure_entry_id << '@'
               << closure.closure_entry_version << " strategy "
               << gnc::contracts::to_string(closure.strategy)
               << " workspace "
               << gnc::model_sdk::to_string(
                      closure.workspace_requirement)
               << '\n';
        for (const auto& consumer : closure.consumer_bindings) {
            stream << "closure-consumer "
                   << closure.closure_execution_input_id << ' '
                   << consumer.binding_id << ' ';
            write_endpoint(consumer.provider_endpoint);
            stream << " -> ";
            write_endpoint(consumer.consumer_endpoint);
            stream << " contract " << consumer.exact_contract_id
                   << " temporal "
                   << gnc::model_sdk::to_string(
                          consumer.temporal_relation)
                   << '\n';
        }
    }
    for (const auto& binding : plan.binding_plan.entries) {
        stream << "bind " << binding.binding_id << ' '
               << gnc::model_sdk::to_string(binding.binding_kind) << ' ';
        write_endpoint(binding.provider_endpoint);
        stream << " -> ";
        write_endpoint(binding.consumer_endpoint);
        stream << " contract " << binding.exact_contract_id
               << " cardinality "
               << gnc::model_sdk::to_string(
                      binding.provider_cardinality)
               << '/'
               << gnc::model_sdk::to_string(
                      binding.consumer_cardinality)
               << " phase " << to_string(binding.phase);
        if (binding.scope_resolution.has_value()) {
            stream << " scope "
                   << to_string(
                          binding.scope_resolution->resolved_scope.kind)
                   << ':'
                   << binding.scope_resolution->resolved_scope
                          .subject_entity_id;
        }
        if (binding.asset_binding.has_value()) {
            stream << " asset " << binding.asset_binding->role << ' '
                   << binding.asset_binding->asset_schema_id << ' '
                   << binding.asset_binding->asset_id;
        }
        stream << '\n';
    }
    for (const auto& temporal :
         plan.temporal_binding_plan.entries) {
        stream << "temporal " << temporal.binding_id << ' '
               << gnc::model_sdk::to_string(temporal.relation) << '\n';
    }
    for (const auto& proof : plan.binding_proofs) {
        stream << "prove " << proof.proof_id << ' '
               << gnc::model_sdk::to_string(proof.binding_kind)
               << " contract " << proof.exact_contract_id;
        for (const auto assertion : proof.assertions) {
            stream << ' ' << to_string(assertion);
        }
        stream << '\n';
    }
    for (const auto& obligation : plan.obligations) {
        stream << "obligation " << obligation.ordinal << ' '
               << obligation.obligation_id << ' '
               << to_string(obligation.kind) << ' ';
        write_endpoint(obligation.provider_endpoint);
        stream << " -> ";
        write_endpoint(obligation.consumer_endpoint);
        stream << " execution-input " << obligation.execution_input_ref
               << '\n';
    }
    return stream.str();
}

} // namespace gnc::compiler
