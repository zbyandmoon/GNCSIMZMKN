#pragma once

#include "gnc/contracts/execution_semantics.hpp"

#include <algorithm>
#include <any>
#include <cstdint>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace gnc::contracts {

// Optimized-build symbol-retention evidence only. The exact process-local,
// type-preserving callable reference lives in PlanImageEntry::typed_entry.
// R2 may copy this inert anchor but must never invoke it or describe it as an
// executable trampoline.
using StaticLinkAnchor = void (*)() noexcept;

enum class PlanImageEntryKind : std::uint8_t {
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

enum class PlanImageSlotKind : std::uint8_t {
    PortValue,
    CommittedState,
    CandidateState,
    HeldIntervalValue,
};

enum class PlanImageDagNodeKind : std::uint8_t {
    RuntimeCallsite,
    IntegrationScope,
};

enum class PlanImageValueKind : std::uint8_t {
    String,
    Enum,
    Integer,
    Float64,
};

struct PlanImagePackage {
    std::uint32_t handle = 0U;
    std::string package_id;
    std::string package_version;
    std::string build_fingerprint;
};

struct PlanImageClock {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string clock_id;
    double base_step_seconds = 0.0;
    std::int64_t initial_tick = 0;
    std::int64_t terminal_tick = 0;
};

struct PlanImageEntry {
    std::uint32_t handle = 0U;
    std::uint32_t package_handle = 0U;
    std::string requirement_id;
    std::string entry_id;
    std::string entry_version;
    PlanImageEntryKind kind = PlanImageEntryKind::Prepare;
    std::string signature_id;
    std::string call_shape_id;
    std::string state_layout_id;
    std::string workspace_layout_id;
    // A process-local, type-preserving reference to the exact package entry.
    // R2 only copies it. Future package/generated R3 composition may recover
    // it with std::any_cast<decltype(&ExactCallable)>; this field alone is not
    // a RuntimeCell factory/materialization contract, and no untyped address
    // or cross-process ABI is implied.
    std::any typed_entry;
    // Retains an explicit relocation to the callable in optimized builds.
    // This anchor is inert and is not an executable trampoline.
    StaticLinkAnchor link_anchor = nullptr;
};

struct PlanImageOccurrence {
    std::uint32_t handle = 0U;
    std::uint32_t package_handle = 0U;
    std::string plan_element_id;
    std::string occurrence_id;
    std::string definition_id;
    std::string definition_version;
    std::string execution_form;
    std::string placement;
    std::string subject_entity_id;
    bool has_scope = false;
    std::string scope_kind;
    std::string scope_subject_entity_id;
    struct ConfigBlock {
        std::string schema_id;
        std::uint32_t schema_version = 0U;
        struct Field {
            std::string field_id;
            PlanImageValueKind kind = PlanImageValueKind::String;
            std::string string_value;
            std::int64_t integer_value = 0;
            double float64_value = 0.0;
        };
        std::vector<Field> fields;
    } canonical_configuration;
    struct AssetBinding {
        std::string role;
        std::string asset_schema_id;
        std::string asset_id;
    };
    std::vector<AssetBinding> asset_bindings;
};

struct PlanImagePreparation {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t occurrence_handle = 0U;
    std::uint32_t prepare_entry_handle = 0U;
    std::string definition_id;
    std::string definition_version;
    std::string prepared_artifact_id;
    PreparationOwnership ownership = PreparationOwnership::Unspecified;
    PreparationPhase phase = PreparationPhase::Unspecified;
    PreparedModelCachePolicy cache_policy =
        PreparedModelCachePolicy::Unspecified;
    PreparationFailurePolicy failure_policy =
        PreparationFailurePolicy::Unspecified;
    bool allows_partial_session = false;
    std::uint32_t order = 0U;
};

struct PlanImageQuery {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t occurrence_handle = 0U;
    std::uint32_t preparation_handle = 0U;
    std::uint32_t query_entry_handle = 0U;
    std::string workspace_requirement;
    std::vector<std::uint32_t> authorized_invocation_handles;
};

struct PlanImageClosure {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t occurrence_handle = 0U;
    std::uint32_t preparation_handle = 0U;
    std::uint32_t closure_entry_handle = 0U;
    std::string strategy;
    std::string workspace_requirement;
    std::vector<std::uint32_t> authorized_invocation_handles;
};

struct PlanImagePort {
    std::uint32_t handle = 0U;
    std::uint32_t occurrence_handle = 0U;
    std::string plan_element_id;
    std::string port_id;
    std::string contract_id;
    std::string direction;
    std::string binding_kind;
    std::string temporal_relation;
};

struct PlanImageSlot {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string slot_id;
    PlanImageSlotKind kind = PlanImageSlotKind::PortValue;
    std::uint32_t owner_occurrence_handle = 0U;
    std::uint32_t port_handle = 0U;
    std::string contract_id;
    std::string layout_id;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    std::uint32_t storage_layout_handle = 0U;
    std::uint64_t offset_bytes = 0U;
    std::uint32_t codec_entry_handle = 0U;
    std::uint32_t writer_token_handle = 0U;
    std::vector<std::uint32_t> reader_handles;
    SlotStorageClass storage_class = SlotStorageClass::Unspecified;
    SlotHoldPolicy hold_policy = SlotHoldPolicy::Unspecified;
};

// One allocation extent per runtime storage class. Slot offsets are relative
// to this extent, so R3 can allocate exact aligned arenas without consulting
// source or the Compiler. R2 only validates and freezes these numbers.
struct PlanImageStorageLayout {
    std::uint32_t handle = 0U;
    SlotStorageClass storage_class = SlotStorageClass::Unspecified;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 1U;
    std::vector<std::uint32_t> ordered_slot_handles;
};

enum class PlanImageWriterOwnerKind : std::uint8_t {
    RuntimeCallsite,
    IntegrationCoordinator,
    InitialStateBuilder,
};

struct PlanImageWriterToken {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t slot_handle = 0U;
    PlanImageWriterOwnerKind owner_kind =
        PlanImageWriterOwnerKind::RuntimeCallsite;
    std::uint32_t owner_handle = 0U;
};

struct PlanImageStateBlock {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t owner_occurrence_handle = 0U;
    std::string schema_id;
    std::uint32_t schema_version = 0U;
    std::string layout_id;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    std::uint32_t codec_entry_handle = 0U;
    std::string evolution;
    std::uint32_t committed_slot_handle = 0U;
    std::uint32_t candidate_slot_handle = 0U;
};

struct PlanImageValue {
    std::string field_id;
    PlanImageValueKind kind = PlanImageValueKind::String;
    std::string string_value;
    std::int64_t integer_value = 0;
    double float64_value = 0.0;
};

struct PlanImageInitialBinding {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t owner_occurrence_handle = 0U;
    std::uint32_t committed_state_slot_handle = 0U;
    std::uint32_t builder_entry_handle = 0U;
    std::string input_schema_id;
    std::uint32_t input_schema_version = 0U;
    std::vector<PlanImageValue> values;
};

struct PlanImageBinding {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string binding_id;
    std::uint32_t provider_port_handle = 0U;
    std::uint32_t provider_slot_handle = 0U;
    std::uint32_t consumer_port_handle = 0U;
};

struct PlanImageCallsite {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string callsite_id;
    std::uint32_t occurrence_handle = 0U;
    std::uint32_t entry_handle = 0U;
    std::string obligation;
    std::string request_contract_id;
    std::string result_contract_id;
    std::string region_id;
    std::vector<std::uint32_t> input_slot_handles;
    std::vector<std::uint32_t> output_slot_handles;
    std::vector<std::uint32_t> output_writer_token_handles;
    std::vector<std::uint32_t> authorized_invocation_handles;
};

struct PlanImageRuntimeComponent {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t occurrence_handle = 0U;
    // Exact package definition builder linked for this occurrence. R2 never
    // invokes it and does not instantiate a Runtime Cell.
    std::uint32_t definition_builder_entry_handle = 0U;
    // Exact package-owned factory. R2 links this handle but never calls it or
    // creates the Session-local Runtime Cell returned by the typed entry.
    std::uint32_t runtime_cell_factory_entry_handle = 0U;
    std::uint32_t runtime_instance_id = 0U;
    std::uint32_t resource_plan_handle = 0U;
    std::string recipe_id;
    std::string profile;
    std::string schedule_trigger;
    std::uint32_t step_interval = 0U;
    std::uint32_t offset = 0U;
    std::string output_hold;
    std::uint32_t max_input_age_steps = 0U;
    std::vector<std::string> lifecycle_capabilities;
    std::vector<std::uint32_t> callsite_handles;
    // Direct numeric ownership references used to construct the factory's
    // compiled binding input without an R3 catalog/name lookup.
    std::vector<std::uint32_t> state_block_handles;
    std::vector<std::uint32_t> preparation_handles;
    std::vector<std::uint32_t> provider_plan_handles;
    std::vector<std::uint32_t> input_slot_handles;
    std::vector<std::uint32_t> output_slot_handles;
    std::vector<std::uint32_t> output_writer_token_handles;
    std::vector<std::uint32_t> invocation_handles;
    std::vector<std::uint32_t> integration_scope_handles;
    std::vector<std::uint32_t> interval_model_slot_handles;
    std::vector<std::uint32_t> transaction_handles;
    std::vector<std::uint32_t> evaluator_history_handles;
};

struct PlanImageResourcePlan {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t runtime_component_handle = 0U;
    std::string resource_plan_id;
    std::string workspace_layout_id;
};

struct PlanImageInvocation {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string invocation_id;
    std::uint32_t caller_callsite_handle = 0U;
    std::uint32_t provider_occurrence_handle = 0U;
    std::uint32_t provider_plan_handle = 0U;
    std::uint32_t provider_preparation_handle = 0U;
    std::uint32_t entry_handle = 0U;
    std::string requirement_id;
    // Zero-based position in the caller entry's package-authored invocation
    // requirements. This freezes typed factory wiring without an R3 string
    // lookup or an order inferred from source-authored invocation ids.
    std::uint32_t requirement_ordinal = 0U;
    std::string invocation_kind;
    std::string contract_id;
    std::string requirement_cardinality;
    // Authorization and logical result flow are distinct from storage. Query
    // results remain CallerLocal; FrozenInterval closure has one coordinator
    // writer and one held interval slot.
    InvocationResultRoute result_route = InvocationResultRoute::Unspecified;
    std::uint32_t result_binding_handle = 0U;
    std::uint32_t result_storage_slot_handle = 0U;
    std::uint32_t result_writer_token_handle = 0U;
    std::uint32_t consumer_port_handle = 0U;
};

struct PlanImageRegion {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string region_id;
    std::uint32_t ordinal = 0U;
    std::string phase;
    std::vector<std::uint32_t> callsite_handles;
};

struct PlanImageDagNode {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string node_id;
    PlanImageDagNodeKind kind = PlanImageDagNodeKind::RuntimeCallsite;
    std::uint32_t target_handle = 0U;
    std::string phase;
};

struct PlanImageDagEdge {
    std::string plan_element_id;
    std::uint32_t predecessor_node_handle = 0U;
    std::uint32_t successor_node_handle = 0U;
};

struct PlanImageIntegrationScope {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string integration_scope_id;
    std::uint32_t owner_occurrence_handle = 0U;
    std::uint32_t committed_state_slot_handle = 0U;
    std::uint32_t candidate_state_slot_handle = 0U;
    std::uint32_t projection_callsite_handle = 0U;
    std::uint32_t form_callsite_handle = 0U;
    std::uint32_t form_preparation_slot_handle = 0U;
    std::uint32_t derivative_callsite_handle = 0U;
    std::uint32_t held_form_slot_handle = 0U;
    std::string held_form_contract_id;
    std::string derivative_request_contract_id;
    std::string derivative_result_contract_id;
    std::vector<std::uint32_t> form_invocation_handles;
    std::vector<std::uint32_t> closure_invocation_handles;
    std::vector<std::uint32_t> member_owner_occurrence_handles;
    std::string integrator_id;
    std::string integrator_version;
    std::uint32_t clock_handle = 0U;
    std::uint32_t step_ticks = 0U;
    double fixed_step_seconds = 0.0;
    double absolute_tolerance = 0.0;
    double relative_tolerance = 0.0;
    std::string check_finiteness;
    double zero_threshold = 0.0;
    double condition_limit = 0.0;
    std::string numerical_policy_id;
    std::uint32_t numerical_policy_configuration_occurrence_handle = 0U;
    std::string workspace_layout_id;
    std::uint64_t workspace_size_bytes = 0U;
    std::uint64_t workspace_alignment_bytes = 0U;
    SlotHoldPolicy held_slot_hold_policy = SlotHoldPolicy::Unspecified;
    std::uint32_t candidate_codec_entry_handle = 0U;
    std::string candidate_project_operation_id;
    std::string candidate_finite_validation_operation_id;
    std::string candidate_invariant_validation_operation_id;
};

struct PlanImageTransactionCandidateMember {
    std::uint32_t owner_occurrence_handle = 0U;
    std::uint32_t candidate_state_slot_handle = 0U;
    std::string producer_kind;
    std::uint32_t producer_handle = 0U;
    std::uint32_t writer_token_handle = 0U;
};

struct PlanImageTransactionBranch {
    TransactionBranch branch = TransactionBranch::Continue;
    std::vector<std::uint32_t> committed_candidate_slot_handles;
    std::vector<std::uint32_t> discarded_candidate_slot_handles;
    std::vector<std::uint32_t> retained_held_slot_handles;
    std::vector<std::uint32_t> discarded_held_slot_handles;
    std::vector<std::uint32_t> published_output_slot_handles;
    std::vector<std::uint32_t> sealed_output_slot_handles;
    std::vector<std::uint32_t> discarded_output_slot_handles;
    TransactionOutputVisibility output_visibility =
        TransactionOutputVisibility::Unspecified;
    HeldIntervalEndPolicy held_interval_end_policy =
        HeldIntervalEndPolicy::Unspecified;
    bool committed_state_preserved = false;
    TransactionFailureOwner failure_owner =
        TransactionFailureOwner::Unspecified;
    TransactionFailureRoute failure_route =
        TransactionFailureRoute::Unspecified;
    bool model_commit = false;
    bool observation_seal = false;
    bool result_seal_after_observation = false;
    std::int64_t epoch_delta = 0;
    std::int64_t tick_delta = 0;
};

struct PlanImageTransaction {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::string transaction_id;
    std::vector<PlanImageTransactionCandidateMember> candidates;
    std::vector<std::uint32_t> held_slot_handles;
    std::vector<PlanImageTransactionBranch> branches;
};

[[nodiscard]] inline const PlanImageTransactionBranch*
find_transaction_branch(const PlanImageTransaction& transaction,
                        TransactionBranch branch) noexcept {
    const auto found = std::find_if(
        transaction.branches.begin(), transaction.branches.end(),
        [branch](const auto& candidate) { return candidate.branch == branch; });
    return found == transaction.branches.end() ? nullptr : &*found;
}

[[nodiscard]] inline TransactionSlotDisposition transaction_slot_disposition(
    const PlanImageTransactionBranch& branch,
    std::uint32_t slot_handle) noexcept {
    const auto contains = [slot_handle](const auto& handles) {
        return std::find(handles.begin(), handles.end(), slot_handle) !=
               handles.end();
    };
    const bool committed = contains(branch.committed_candidate_slot_handles);
    const bool retained = contains(branch.retained_held_slot_handles);
    const bool published = contains(branch.published_output_slot_handles);
    const bool sealed = contains(branch.sealed_output_slot_handles);
    const bool discarded =
        contains(branch.discarded_candidate_slot_handles) ||
        contains(branch.discarded_held_slot_handles) ||
        contains(branch.discarded_output_slot_handles);
    const unsigned int exclusive_roles =
        static_cast<unsigned int>(committed) +
        static_cast<unsigned int>(retained) +
        static_cast<unsigned int>(published || sealed) +
        static_cast<unsigned int>(discarded);
    if (exclusive_roles > 1U) {
        return TransactionSlotDisposition::Conflict;
    }
    if (committed) {
        return TransactionSlotDisposition::CommitCandidate;
    }
    if (retained) {
        return TransactionSlotDisposition::RetainHeld;
    }
    if (discarded) {
        return TransactionSlotDisposition::Discard;
    }
    if (published && sealed) {
        return TransactionSlotDisposition::PublishAndSealOutput;
    }
    if (published) {
        return TransactionSlotDisposition::PublishOutput;
    }
    if (sealed) {
        return TransactionSlotDisposition::SealOutput;
    }
    return TransactionSlotDisposition::NotManaged;
}

struct PlanImageLifecycle {
    std::vector<std::uint32_t> preparation_handles;
    std::vector<std::uint32_t> runtime_component_handles;
    std::vector<std::uint32_t> initial_binding_handles;
    std::vector<std::uint32_t> bound_provider_dispose_handles;
    std::vector<std::uint32_t> runtime_component_dispose_handles;
    std::vector<std::uint32_t> preparation_dispose_handles;
};

struct PlanImageEvaluatorHistoryMember {
    std::string member_id;
    std::string state_schema_id;
    std::string state_layout_id;
    std::uint32_t committed_state_slot_handle = 0U;
};

struct PlanImageEvaluatorHistory {
    std::uint32_t handle = 0U;
    std::string plan_element_id;
    std::uint32_t evaluator_callsite_handle = 0U;
    std::string request_contract_id;
    std::uint32_t history_depth = 0U;
    std::vector<PlanImageEvaluatorHistoryMember> ordered_members;
};

// Diagnostic/provenance information is deliberately not part of the stable
// image fingerprint. It proves source-to-image traceability without making a
// source file relocation semantic.
struct PlanImageSourceRef {
    std::string document_uri;
    std::string node_path;
};

struct PlanImageConformance {
    std::string plan_element_id;
    std::vector<PlanImageSourceRef> source_refs;
    std::vector<std::string> proof_ids;
    std::vector<std::uint32_t> image_handles;
};

struct ExecutionPlanImageData {
    std::uint32_t revision = 3U;
    std::string plan_id;
    std::string mission_id;
    std::string source_semantic_hash;
    std::string descriptor_semantic_hash;
    std::string proof_index_hash;
    std::string image_fingerprint;
    std::vector<PlanImagePackage> packages;
    PlanImageClock clock;
    std::vector<PlanImageEntry> entries;
    std::vector<PlanImageOccurrence> occurrences;
    std::vector<PlanImagePreparation> preparations;
    std::vector<PlanImageQuery> queries;
    std::vector<PlanImageClosure> closures;
    std::vector<PlanImagePort> ports;
    std::vector<PlanImageSlot> slots;
    std::vector<PlanImageStorageLayout> storage_layouts;
    std::vector<PlanImageWriterToken> writer_tokens;
    std::vector<PlanImageStateBlock> state_blocks;
    std::vector<PlanImageInitialBinding> initial_bindings;
    std::vector<PlanImageBinding> bindings;
    std::vector<PlanImageCallsite> callsites;
    std::vector<PlanImageRuntimeComponent> runtime_components;
    std::vector<PlanImageResourcePlan> resource_plans;
    std::vector<PlanImageInvocation> invocations;
    std::vector<PlanImageRegion> regions;
    std::vector<PlanImageDagNode> dag_nodes;
    std::vector<PlanImageDagEdge> dag_edges;
    std::vector<PlanImageIntegrationScope> integration_scopes;
    std::vector<PlanImageTransaction> transactions;
    std::vector<PlanImageEvaluatorHistory> evaluator_histories;
    PlanImageLifecycle lifecycle;
    std::vector<PlanImageConformance> conformance;
};

// An image is immutable after freeze(). These planning/proof/link tables
// contain exact package RuntimeCellFactory and codec
// handles, but no Session-owned state/workspace/runtime objects. R2 never
// invokes a linked entry; R3 owns materialization and execution.
class ExecutionPlanImage final {
  public:
    [[nodiscard]] static ExecutionPlanImage freeze(
        ExecutionPlanImageData data) {
        return ExecutionPlanImage(std::move(data));
    }

    [[nodiscard]] std::uint32_t revision() const noexcept {
        return data_.revision;
    }
    [[nodiscard]] const std::string& plan_id() const noexcept {
        return data_.plan_id;
    }
    [[nodiscard]] const std::string& mission_id() const noexcept {
        return data_.mission_id;
    }
    [[nodiscard]] const std::string& source_semantic_hash() const noexcept {
        return data_.source_semantic_hash;
    }
    [[nodiscard]] const std::string& descriptor_semantic_hash() const noexcept {
        return data_.descriptor_semantic_hash;
    }
    [[nodiscard]] const std::string& proof_index_hash() const noexcept {
        return data_.proof_index_hash;
    }
    [[nodiscard]] const std::string& fingerprint() const noexcept {
        return data_.image_fingerprint;
    }
    [[nodiscard]] const std::vector<PlanImagePackage>& packages() const noexcept {
        return data_.packages;
    }
    [[nodiscard]] const PlanImageClock& clock() const noexcept {
        return data_.clock;
    }
    [[nodiscard]] const std::vector<PlanImageEntry>& entries() const noexcept {
        return data_.entries;
    }
    [[nodiscard]] const std::vector<PlanImageOccurrence>& occurrences() const noexcept {
        return data_.occurrences;
    }
    [[nodiscard]] const std::vector<PlanImagePreparation>& preparations() const noexcept {
        return data_.preparations;
    }
    [[nodiscard]] const std::vector<PlanImageQuery>& queries() const noexcept {
        return data_.queries;
    }
    [[nodiscard]] const std::vector<PlanImageClosure>& closures() const noexcept {
        return data_.closures;
    }
    [[nodiscard]] const std::vector<PlanImagePort>& ports() const noexcept {
        return data_.ports;
    }
    [[nodiscard]] const std::vector<PlanImageSlot>& slots() const noexcept {
        return data_.slots;
    }
    [[nodiscard]] const std::vector<PlanImageStorageLayout>& storage_layouts()
        const noexcept {
        return data_.storage_layouts;
    }
    [[nodiscard]] const std::vector<PlanImageWriterToken>& writer_tokens() const noexcept {
        return data_.writer_tokens;
    }
    [[nodiscard]] const std::vector<PlanImageStateBlock>& state_blocks() const noexcept {
        return data_.state_blocks;
    }
    [[nodiscard]] const std::vector<PlanImageInitialBinding>& initial_bindings() const noexcept {
        return data_.initial_bindings;
    }
    [[nodiscard]] const std::vector<PlanImageBinding>& bindings() const noexcept {
        return data_.bindings;
    }
    [[nodiscard]] const std::vector<PlanImageCallsite>& callsites() const noexcept {
        return data_.callsites;
    }
    [[nodiscard]] const std::vector<PlanImageRuntimeComponent>& runtime_components() const noexcept {
        return data_.runtime_components;
    }
    [[nodiscard]] const std::vector<PlanImageResourcePlan>& resource_plans() const noexcept {
        return data_.resource_plans;
    }
    [[nodiscard]] const std::vector<PlanImageInvocation>& invocations() const noexcept {
        return data_.invocations;
    }
    [[nodiscard]] const std::vector<PlanImageRegion>& regions() const noexcept {
        return data_.regions;
    }
    [[nodiscard]] const std::vector<PlanImageDagNode>& dag_nodes() const noexcept {
        return data_.dag_nodes;
    }
    [[nodiscard]] const std::vector<PlanImageDagEdge>& dag_edges() const noexcept {
        return data_.dag_edges;
    }
    [[nodiscard]] const std::vector<PlanImageIntegrationScope>& integration_scopes() const noexcept {
        return data_.integration_scopes;
    }
    [[nodiscard]] const std::vector<PlanImageTransaction>& transactions() const noexcept {
        return data_.transactions;
    }
    [[nodiscard]] const std::vector<PlanImageEvaluatorHistory>& evaluator_histories() const noexcept {
        return data_.evaluator_histories;
    }
    [[nodiscard]] const PlanImageLifecycle& lifecycle() const noexcept {
        return data_.lifecycle;
    }
    [[nodiscard]] const std::vector<PlanImageConformance>& conformance() const noexcept {
        return data_.conformance;
    }
    [[nodiscard]] const ExecutionPlanImageData& data() const noexcept {
        return data_;
    }

  private:
    explicit ExecutionPlanImage(ExecutionPlanImageData data)
        : data_(std::move(data)) {}

    ExecutionPlanImageData data_;
};

} // namespace gnc::contracts
