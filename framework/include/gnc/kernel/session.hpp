#pragma once

#include "gnc/contracts/execution_plan_image.hpp"
#include "gnc/contracts/outcome.hpp"
#include "gnc/contracts/sample_context.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gnc::kernel {

namespace qualification {
class SessionAccess;
}

enum class SessionState : std::uint8_t {
    Created,
    Initialized,
    Completed,
    Failed,
    Disposed,
};

enum class SessionError : std::uint8_t {
    None,
    NullImage,
    NullMaterializationProvider,
    EmptyRunId,
    DuplicateRunId,
    RunBindingMismatch,
    UnsupportedImageRevision,
    InvalidImageHandle,
    InvalidImageStructure,
    InvalidStorageLayout,
    StorageBoundsViolation,
    StorageOverlap,
    MissingMaterializer,
    InvalidMaterializerIdentity,
    ObjectSizeMismatch,
    ObjectAlignmentMismatch,
    ObjectLayoutMismatch,
    ObjectCodecMismatch,
    ObjectTypeMismatch,
    AllocationFailure,
    PreparationFailed,
    RuntimeCellFailed,
    SlotConstructionFailed,
    InitialStateFailed,
    ResetStateFailed,
    ResetPrecommitFailed,
    ObjectValidationFailed,
    InvalidLifecycleTransition,
    InvalidSchedule,
    FrameAlreadyOpen,
    FrameNotOpen,
    FrameSlotAbsent,
    StaleFrameView,
    StateAuthorizationFailure,
    ReaderAuthorizationFailure,
    WriterAuthorizationFailure,
    CandidateAuthorizationFailure,
    CandidateRearmFailed,
    CandidateValidationFailed,
    HistoryAuthorizationFailure,
    HistoryValidationFailed,
    ObservationSealFailed,
    TransactionPrecommitFailed,
    InvocationFailed,
    InternalFailure,
};

[[nodiscard]] std::string_view to_string(SessionError error) noexcept;

// Every detail is static storage. Error reporting therefore remains usable
// while handling allocation failure.
struct SessionResult {
    SessionError error = SessionError::None;
    std::uint32_t image_handle = 0U;
    std::string_view detail;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == SessionError::None;
    }
};

// The caller owns the opaque spelling. Copies share an immutable owned value,
// so failure outcomes can retain the id without allocating while unwinding.
class RunId final {
  public:
    RunId() noexcept = default;
    explicit RunId(std::string value)
        : value_(std::make_shared<const std::string>(std::move(value))) {}

    [[nodiscard]] std::string_view value() const noexcept {
        return value_ == nullptr ? std::string_view{}
                                 : std::string_view(*value_);
    }
    [[nodiscard]] bool empty() const noexcept { return value().empty(); }

    friend bool operator==(const RunId& lhs, const RunId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }
    friend bool operator!=(const RunId& lhs, const RunId& rhs) noexcept {
        return !(lhs == rhs);
    }

    void swap(RunId& other) noexcept { value_.swap(other.value_); }

  private:
    std::shared_ptr<const std::string> value_;
};

// R3 currently freezes initial values into the Image. The exact in-process
// binding therefore names every existing Image/plan identity involved in the
// run without introducing a new hash or serialized schema.
struct RunBinding {
    std::string image_fingerprint;
    std::string plan_id;
    std::string mission_id;
    std::string source_semantic_hash;
    std::string descriptor_semantic_hash;

    friend bool operator==(const RunBinding& lhs,
                           const RunBinding& rhs) noexcept {
        return lhs.image_fingerprint == rhs.image_fingerprint &&
               lhs.plan_id == rhs.plan_id &&
               lhs.mission_id == rhs.mission_id &&
               lhs.source_semantic_hash == rhs.source_semantic_hash &&
               lhs.descriptor_semantic_hash ==
                   rhs.descriptor_semantic_hash;
    }
    friend bool operator!=(const RunBinding& lhs,
                           const RunBinding& rhs) noexcept {
        return !(lhs == rhs);
    }

    void swap(RunBinding& other) noexcept {
        image_fingerprint.swap(other.image_fingerprint);
        plan_id.swap(other.plan_id);
        mission_id.swap(other.mission_id);
        source_semantic_hash.swap(other.source_semantic_hash);
        descriptor_semantic_hash.swap(other.descriptor_semantic_hash);
    }
};

[[nodiscard]] RunBinding exact_run_binding(
    const contracts::ExecutionPlanImage& image);

struct InitializationRequest {
    RunId run_id;
    RunBinding binding;
};

enum class RuntimeDiagnosticCode : std::uint8_t {
    None,
    InitializationRequestInvalid,
    ImageBindingMismatch,
    ImageValidationFailed,
    MaterializationFailed,
    ScheduleFailed,
    FrameFailed,
    AuthorizationFailed,
    HistoryFailed,
    CandidateFailed,
    ObservationSealFailed,
    TransactionPrecommitFailed,
    InvocationFailed,
    ObjectValidationFailed,
    AllocationFailed,
    InternalFailure,
    LifecycleTransitionRejected,
    ResetRequestInvalid,
    ResetStateRebuildFailed,
    ResetPrecommitFailed,
};

enum class RuntimeDiagnosticStage : std::uint8_t {
    InitializationRequest,
    InitializationValidation,
    Materialization,
    InitialState,
    Schedule,
    History,
    BoundaryInvocation,
    CandidateProduction,
    ObservationSeal,
    Precommit,
    Finalization,
    Lifecycle,
    ResetRequest,
    ResetState,
    ResetPrecommit,
};

enum class RuntimeFailureDisposition : std::uint8_t {
    FailOperation,
};

[[nodiscard]] std::string_view to_string(
    RuntimeDiagnosticCode code) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeDiagnosticStage stage) noexcept;

struct RuntimeDiagnostic {
    RuntimeDiagnosticCode code = RuntimeDiagnosticCode::None;
    RuntimeDiagnosticStage stage =
        RuntimeDiagnosticStage::InitializationRequest;
    std::uint32_t subject_handle = 0U;
    RunId run_id;
    std::int64_t tick = 0;
    std::uint64_t base_epoch = 0U;
    SessionError cause_code = SessionError::None;
    std::uint32_t cause_ref = 0U;
    contracts::EvidenceValidity validity_effect =
        contracts::EvidenceValidity::Unknown;
    RuntimeFailureDisposition disposition =
        RuntimeFailureDisposition::FailOperation;
    std::string_view message_key;
    std::string_view detail;
};

enum class InitializationStatus : std::uint8_t {
    Committed,
    Failed,
};

struct InitializationOutcome {
    InitializationStatus status = InitializationStatus::Failed;
    SessionResult result;
    RunId run_id;
    std::uint64_t proposed_run_sequence = 0U;
    bool binding_matched = false;
    bool initialization_commit = false;
    std::uint64_t committed_epoch = 0U;
    std::int64_t committed_tick = 0;
    std::optional<RuntimeDiagnostic> primary_diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == InitializationStatus::Committed &&
               initialization_commit && result;
    }
};

struct ResetRequest {
    RunId run_id;
    RunBinding binding;
};

enum class ResetStatus : std::uint8_t {
    Committed,
    Failed,
};

struct ResetOutcome {
    ResetStatus status = ResetStatus::Failed;
    SessionResult result;
    RunId run_id;
    std::uint64_t proposed_run_sequence = 0U;
    bool binding_matched = false;
    bool reset_commit = false;
    std::uint64_t committed_epoch = 0U;
    std::int64_t committed_tick = 0;
    std::optional<RuntimeDiagnostic> primary_diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == ResetStatus::Committed && reset_commit && result;
    }
};

enum class StepStatus : std::uint8_t {
    Committed,
    Terminated,
    Failed,
};

struct StepCandidateSummary {
    std::size_t planned_count = 0U;
    std::size_t present_count = 0U;
    std::size_t valid_count = 0U;
};

struct StepHistorySummary {
    bool staged = false;
    std::size_t history_count = 0U;
    std::size_t prospective_sample_count = 0U;
};

struct StepObservationSealSummary {
    bool staged = false;
    std::size_t output_count = 0U;
};

struct StepResultSealSummary {
    bool staged = false;
    bool result_present = false;
};

struct StepOutcome {
    StepStatus status = StepStatus::Failed;
    SessionResult result;
    RunId run_id;
    std::uint64_t run_sequence = 0U;
    bool branch_selected = false;
    std::uint32_t transaction_handle = 0U;
    contracts::TransactionBranch branch =
        contracts::TransactionBranch::Continue;
    std::uint64_t base_epoch = 0U;
    std::uint64_t committed_epoch = 0U;
    std::int64_t tick_before = 0;
    std::int64_t tick_after = 0;
    std::uint32_t last_region_handle = 0U;
    std::uint32_t last_callsite_handle = 0U;
    std::uint32_t last_image_handle = 0U;
    StepCandidateSummary candidates;
    StepHistorySummary histories;
    StepObservationSealSummary observation_seal;
    StepResultSealSummary result_seal;
    std::optional<RuntimeDiagnostic> primary_diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status != StepStatus::Failed && result;
    }
};

enum class RunFinalStatus : std::uint8_t {
    Completed,
    Failed,
};

enum class RunFinalizationStatus : std::uint8_t {
    NotStarted,
    Succeeded,
};

enum class RunStartKind : std::uint8_t {
    Initialize,
    Reset,
    RestoreBranch,
};

struct RunOutcome {
    RunId run_id;
    std::uint64_t run_sequence = 0U;
    std::string image_fingerprint;
    std::string plan_id;
    std::string mission_id;
    std::string source_semantic_hash;
    std::string descriptor_semantic_hash;
    RunStartKind run_start_kind = RunStartKind::Initialize;
    bool run_start_committed = false;
    RunFinalStatus final_status = RunFinalStatus::Failed;
    contracts::EvidenceValidity validity =
        contracts::EvidenceValidity::Unknown;
    std::int64_t initial_tick = 0;
    std::int64_t final_tick = 0;
    std::uint64_t initial_committed_epoch = 0U;
    std::uint64_t final_committed_epoch = 0U;
    std::uint64_t committed_step_count = 0U;
    bool terminal_branch_committed = false;
    bool mission_result_available = false;
    std::optional<RuntimeDiagnostic> primary_diagnostic;
    std::vector<RuntimeDiagnostic> related_diagnostics;
    RunFinalizationStatus finalization_status =
        RunFinalizationStatus::NotStarted;
};

struct InProcessObjectLayout {
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    std::string_view layout_identity;
    std::uint32_t codec_entry_handle = 0U;
    const void* type_identity = nullptr;
    bool trivially_copyable = false;
    bool trivially_destructible = false;
};

class InProcessObjectOperations {
  public:
    virtual ~InProcessObjectOperations() = default;

    [[nodiscard]] virtual InProcessObjectLayout layout() const noexcept = 0;
    [[nodiscard]] virtual bool copy_construct(
        const void* source, void* destination) const noexcept = 0;
    [[nodiscard]] virtual bool replace(
        void* destination, const void* source) const noexcept = 0;
    [[nodiscard]] virtual bool validate(const void* object) const noexcept = 0;
    [[nodiscard]] virtual bool supports_nofail_swap() const noexcept {
        return false;
    }
    virtual void nofail_swap(void*, void*) const noexcept {}
    virtual void destroy(void* object) const noexcept = 0;
};

enum class SessionObjectRole : std::uint8_t {
    PreparedModel,
    RuntimeCell,
    InitialStateValue,
    CommittedState,
    CandidateState,
    CycleFrameValue,
    HeldIntervalValue,
    TerminalOutputValue,
    CommittedHistoryValue,
};

struct SessionMaterializerIdentity {
    std::uint32_t image_object_handle = 0U;
    SessionObjectRole role = SessionObjectRole::CycleFrameValue;
    std::uint32_t linked_entry_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;
};

struct SessionObjectIdentityView {
    const void* address = nullptr;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    const void* type_identity = nullptr;
    SessionObjectRole role = SessionObjectRole::CycleFrameValue;
    std::uint32_t image_object_handle = 0U;
    std::uint32_t linked_entry_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;

    [[nodiscard]] explicit operator bool() const noexcept {
        return address != nullptr && type_identity != nullptr;
    }
};

struct SessionObjectRequirement {
    std::uint32_t image_object_handle = 0U;
    SessionObjectRole role = SessionObjectRole::PreparedModel;
    std::uint32_t linked_entry_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    const void* type_identity = nullptr;
};

class SessionObjectAccess {
  public:
    virtual ~SessionObjectAccess() = default;

    [[nodiscard]] virtual SessionObjectIdentityView prepared_object(
        std::uint32_t preparation_handle) const noexcept = 0;
};

class SessionObjectMaterializer {
  public:
    virtual ~SessionObjectMaterializer() = default;

    [[nodiscard]] virtual SessionMaterializerIdentity identity()
        const noexcept = 0;
    [[nodiscard]] virtual const InProcessObjectOperations& operations()
        const noexcept = 0;
    [[nodiscard]] virtual std::size_t dependency_count() const noexcept {
        return 0U;
    }
    [[nodiscard]] virtual SessionObjectRequirement dependency(
        std::size_t) const noexcept {
        return {};
    }
    [[nodiscard]] virtual bool construct(
        const SessionObjectAccess& objects,
        void* destination) const noexcept = 0;
};

struct InProcessValueView {
    const void* object = nullptr;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    const void* type_identity = nullptr;
};

class SessionCommittedStateAccess {
  public:
    virtual ~SessionCommittedStateAccess() = default;
    [[nodiscard]] virtual SessionResult read_committed(
        std::uint8_t authority_kind, std::uint32_t authority_handle,
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept = 0;
};

class SessionFrameAccess {
  public:
    virtual ~SessionFrameAccess() = default;
    [[nodiscard]] virtual SessionResult read_input(
        std::uint8_t authority_kind, std::uint32_t authority_handle,
        std::uint64_t generation,
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept = 0;
    [[nodiscard]] virtual SessionResult write_output(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t slot_handle, std::uint32_t writer_token_handle,
        InProcessValueView value) noexcept = 0;
    [[nodiscard]] virtual bool frame_active(
        std::uint64_t generation) const noexcept = 0;
};

struct SessionCommittedHistoryInfo {
    std::uint32_t history_handle = 0U;
    std::uint32_t history_depth = 0U;
    std::size_t sample_count = 0U;
    std::size_t member_count = 0U;
    std::int64_t first_tick = 0;
    std::int64_t last_tick = 0;
};

class SessionCommittedHistoryAccess {
  public:
    virtual ~SessionCommittedHistoryAccess() = default;
    [[nodiscard]] virtual SessionResult history_info(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t history_handle,
        SessionCommittedHistoryInfo& result) const noexcept = 0;
    [[nodiscard]] virtual SessionResult read_history_member(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t history_handle, std::size_t sample_index,
        std::size_t member_index, std::int64_t& sample_tick,
        SessionObjectIdentityView& result) const noexcept = 0;
    [[nodiscard]] virtual bool history_active(
        std::uint32_t callsite_handle,
        std::uint64_t generation) const noexcept = 0;
};

enum class SessionStateAuthorityKind : std::uint8_t {
    RuntimeComponent = 1U,
    IntegrationScope = 2U,
};

enum class SessionFrameAuthorityKind : std::uint8_t {
    RuntimeCallsite = 1U,
    IntegrationScope = 2U,
};

enum class SessionCandidateProducerKind : std::uint8_t {
    RuntimeCallsite = 1U,
    IntegrationScope = 2U,
};

class SessionCandidateAccess {
  public:
    virtual ~SessionCandidateAccess() = default;
    [[nodiscard]] virtual SessionResult write_candidate(
        SessionCandidateProducerKind producer_kind,
        std::uint32_t producer_handle, std::uint64_t generation,
        std::uint32_t transaction_handle,
        std::uint32_t candidate_slot_handle,
        std::uint32_t writer_token_handle,
        InProcessValueView value) noexcept = 0;
    [[nodiscard]] virtual bool transaction_active(
        std::uint64_t generation) const noexcept = 0;
};

class SessionCommittedStateView final {
  public:
    [[nodiscard]] SessionResult read(
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept;

  private:
    explicit SessionCommittedStateView(
        const SessionCommittedStateAccess* access,
        SessionStateAuthorityKind authority_kind,
        std::uint32_t authority_handle) noexcept;
    const SessionCommittedStateAccess* access_ = nullptr;
    SessionStateAuthorityKind authority_kind_ =
        SessionStateAuthorityKind::RuntimeComponent;
    std::uint32_t authority_handle_ = 0U;

    friend class Session;
};

class SessionInputView final {
  public:
    [[nodiscard]] SessionResult read(
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

  private:
    SessionInputView(const SessionFrameAccess* access,
                     SessionFrameAuthorityKind authority_kind,
                     std::uint32_t authority_handle,
                     std::uint64_t generation) noexcept;
    const SessionFrameAccess* access_ = nullptr;
    SessionFrameAuthorityKind authority_kind_ =
        SessionFrameAuthorityKind::RuntimeCallsite;
    std::uint32_t authority_handle_ = 0U;
    std::uint64_t generation_ = 0U;

    friend class Session;
};

class SessionCandidateWriterSet final {
  public:
    [[nodiscard]] SessionResult write(
        std::uint32_t candidate_slot_handle,
        std::uint32_t writer_token_handle,
        InProcessValueView value) const noexcept;
    [[nodiscard]] bool active() const noexcept;

  private:
    SessionCandidateWriterSet(
        SessionCandidateAccess* access,
        SessionCandidateProducerKind producer_kind,
        std::uint32_t producer_handle, std::uint64_t generation,
        std::uint32_t transaction_handle) noexcept;
    SessionCandidateAccess* access_ = nullptr;
    SessionCandidateProducerKind producer_kind_ =
        SessionCandidateProducerKind::RuntimeCallsite;
    std::uint32_t producer_handle_ = 0U;
    std::uint64_t generation_ = 0U;
    std::uint32_t transaction_handle_ = 0U;

    friend class Session;
};

class SessionOutputWriterSet final {
  public:
    [[nodiscard]] SessionResult write(
        std::uint32_t slot_handle, std::uint32_t writer_token_handle,
        InProcessValueView value) const noexcept;
    [[nodiscard]] bool active() const noexcept;

  private:
    SessionOutputWriterSet(SessionFrameAccess* access,
                           std::uint32_t callsite_handle,
                           std::uint64_t generation) noexcept;
    SessionFrameAccess* access_ = nullptr;
    std::uint32_t callsite_handle_ = 0U;
    std::uint64_t generation_ = 0U;

    friend class Session;
};

class SessionCommittedHistoryView final {
  public:
    [[nodiscard]] SessionResult info(
        std::uint32_t history_handle,
        SessionCommittedHistoryInfo& result) const noexcept;
    [[nodiscard]] SessionResult read(
        std::uint32_t history_handle, std::size_t sample_index,
        std::size_t member_index, std::int64_t& sample_tick,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] bool active() const noexcept;

  private:
    SessionCommittedHistoryView(
        const SessionCommittedHistoryAccess* access,
        std::uint32_t callsite_handle,
        std::uint64_t generation) noexcept;
    const SessionCommittedHistoryAccess* access_ = nullptr;
    std::uint32_t callsite_handle_ = 0U;
    std::uint64_t generation_ = 0U;

    friend class Session;
};

class SessionInvocationContext final {
  public:
    [[nodiscard]] std::uint32_t callsite_handle() const noexcept {
        return callsite_handle_;
    }
    [[nodiscard]] std::uint32_t component_handle() const noexcept {
        return component_handle_;
    }
    [[nodiscard]] std::int64_t tick() const noexcept { return tick_; }
    [[nodiscard]] double boundary_time_seconds() const noexcept {
        return boundary_time_seconds_;
    }
    [[nodiscard]] double interval_start_seconds() const noexcept {
        return interval_start_seconds_;
    }
    [[nodiscard]] double interval_end_seconds() const noexcept {
        return interval_end_seconds_;
    }
    [[nodiscard]] contracts::DataQuality quality() const noexcept {
        return quality_;
    }
    [[nodiscard]] const SessionObjectIdentityView& runtime_cell() const noexcept {
        return runtime_cell_;
    }
    [[nodiscard]] const SessionCommittedStateView& committed() const noexcept {
        return committed_;
    }
    [[nodiscard]] const SessionInputView& inputs() const noexcept {
        return inputs_;
    }
    [[nodiscard]] const SessionOutputWriterSet& outputs() const noexcept {
        return outputs_;
    }
    [[nodiscard]] const SessionCommittedHistoryView& history()
        const noexcept {
        return history_;
    }
    [[nodiscard]] const SessionCandidateWriterSet& candidates()
        const noexcept {
        return candidates_;
    }

  private:
    SessionInvocationContext(
        std::uint32_t callsite_handle, std::uint32_t component_handle,
        std::int64_t tick, double boundary_time_seconds,
        double interval_start_seconds, double interval_end_seconds,
        contracts::DataQuality quality,
        SessionObjectIdentityView runtime_cell,
        SessionCommittedStateView committed, SessionInputView inputs,
        SessionOutputWriterSet outputs, SessionCommittedHistoryView history,
        SessionCandidateWriterSet candidates) noexcept;

    std::uint32_t callsite_handle_ = 0U;
    std::uint32_t component_handle_ = 0U;
    std::int64_t tick_ = 0;
    double boundary_time_seconds_ = 0.0;
    double interval_start_seconds_ = 0.0;
    double interval_end_seconds_ = 0.0;
    contracts::DataQuality quality_ = contracts::DataQuality::Invalid;
    SessionObjectIdentityView runtime_cell_;
    SessionCommittedStateView committed_;
    SessionInputView inputs_;
    SessionOutputWriterSet outputs_;
    SessionCommittedHistoryView history_;
    SessionCandidateWriterSet candidates_;

    friend class Session;
};

struct SessionInvocationIdentity {
    std::uint32_t callsite_handle = 0U;
    std::uint32_t runtime_component_handle = 0U;
    std::uint32_t linked_entry_handle = 0U;
};

class SessionInvocationEntry {
  public:
    virtual ~SessionInvocationEntry() = default;
    [[nodiscard]] virtual SessionInvocationIdentity identity()
        const noexcept = 0;
    [[nodiscard]] virtual SessionResult invoke(
        const SessionInvocationContext& context) const noexcept = 0;
};

struct SessionIntegrationIdentity {
    std::uint32_t integration_scope_handle = 0U;
    std::uint32_t runtime_component_handle = 0U;
};

class SessionIntegrationContext final {
  public:
    [[nodiscard]] std::uint32_t integration_scope_handle() const noexcept {
        return integration_scope_handle_;
    }
    [[nodiscard]] std::uint32_t component_handle() const noexcept {
        return component_handle_;
    }
    [[nodiscard]] std::uint32_t transaction_handle() const noexcept {
        return transaction_handle_;
    }
    [[nodiscard]] std::int64_t tick() const noexcept { return tick_; }
    [[nodiscard]] double interval_start_seconds() const noexcept {
        return interval_start_seconds_;
    }
    [[nodiscard]] double interval_end_seconds() const noexcept {
        return interval_end_seconds_;
    }
    [[nodiscard]] const SessionObjectIdentityView& runtime_cell()
        const noexcept {
        return runtime_cell_;
    }
    [[nodiscard]] const SessionCommittedStateView& committed()
        const noexcept {
        return committed_;
    }
    [[nodiscard]] const SessionInputView& inputs() const noexcept {
        return inputs_;
    }
    [[nodiscard]] const SessionCandidateWriterSet& candidates()
        const noexcept {
        return candidates_;
    }

  private:
    SessionIntegrationContext(
        std::uint32_t integration_scope_handle,
        std::uint32_t component_handle,
        std::uint32_t transaction_handle, std::int64_t tick,
        double interval_start_seconds, double interval_end_seconds,
        SessionObjectIdentityView runtime_cell,
        SessionCommittedStateView committed, SessionInputView inputs,
        SessionCandidateWriterSet candidates) noexcept;

    std::uint32_t integration_scope_handle_ = 0U;
    std::uint32_t component_handle_ = 0U;
    std::uint32_t transaction_handle_ = 0U;
    std::int64_t tick_ = 0;
    double interval_start_seconds_ = 0.0;
    double interval_end_seconds_ = 0.0;
    SessionObjectIdentityView runtime_cell_;
    SessionCommittedStateView committed_;
    SessionInputView inputs_;
    SessionCandidateWriterSet candidates_;

    friend class Session;
};

class SessionIntegrationEntry {
  public:
    virtual ~SessionIntegrationEntry() = default;
    [[nodiscard]] virtual SessionIntegrationIdentity identity()
        const noexcept = 0;
    [[nodiscard]] virtual SessionResult integrate(
        const SessionIntegrationContext& context) const noexcept = 0;
};

class SessionMaterializationProvider {
  public:
    virtual ~SessionMaterializationProvider() = default;

    [[nodiscard]] virtual const SessionObjectMaterializer* preparation(
        std::uint32_t preparation_handle) const noexcept = 0;
    [[nodiscard]] virtual const SessionObjectMaterializer* runtime_component(
        std::uint32_t component_handle) const noexcept = 0;
    [[nodiscard]] virtual const SessionObjectMaterializer* slot(
        std::uint32_t slot_handle) const noexcept = 0;
    [[nodiscard]] virtual const SessionObjectMaterializer* initial_state(
        std::uint32_t initial_binding_handle) const noexcept = 0;
    [[nodiscard]] virtual const SessionInvocationEntry* invocation(
        std::uint32_t callsite_handle) const noexcept {
        (void)callsite_handle;
        return nullptr;
    }
    [[nodiscard]] virtual const SessionIntegrationEntry* integration(
        std::uint32_t integration_scope_handle) const noexcept {
        (void)integration_scope_handle;
        return nullptr;
    }
};

struct SessionStorageExtent {
    std::uint32_t layout_handle = 0U;
    contracts::SlotStorageClass storage_class =
        contracts::SlotStorageClass::Unspecified;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
};

struct SessionStateBlockInfo {
    std::uint32_t state_block_handle = 0U;
    std::uint32_t owner_runtime_component_handle = 0U;
    std::uint32_t committed_slot_handle = 0U;
    std::uint32_t candidate_slot_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;
    std::uint64_t committed_epoch = 0U;
};

struct SessionFrameSlotInfo {
    std::uint32_t slot_handle = 0U;
    bool present = false;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
    std::int64_t sample_tick = 0;
    double sample_time_seconds = 0.0;
    double interval_start_seconds = 0.0;
    double interval_end_seconds = 0.0;
    contracts::DataQuality quality = contracts::DataQuality::Invalid;
};

struct SessionCommittedOutputInfo {
    std::uint32_t slot_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;
    bool present = false;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
    std::int64_t sample_tick = 0;
    double sample_time_seconds = 0.0;
    double interval_start_seconds = 0.0;
    double interval_end_seconds = 0.0;
    contracts::DataQuality quality = contracts::DataQuality::Invalid;
    bool terminal_result = false;
};

struct SessionBoundarySummary {
    std::uint64_t generation = 0U;
    std::size_t output_write_count = 0U;
    std::vector<std::uint32_t> executed_callsite_handles;
    std::vector<std::uint32_t> skipped_callsite_handles;
};

struct SessionCandidateJournalEntry {
    std::uint32_t slot_handle = 0U;
    SessionCandidateProducerKind producer_kind =
        SessionCandidateProducerKind::RuntimeCallsite;
    std::uint32_t producer_handle = 0U;
    std::uint32_t writer_token_handle = 0U;
    std::uint64_t base_epoch = 0U;
    std::uint64_t generation = 0U;
    bool present = false;
    bool valid = false;
};

struct SessionHistoryJournalEntry {
    std::uint32_t history_handle = 0U;
    std::int64_t staged_sample_tick = 0;
    std::size_t prospective_sample_count = 0U;
};

struct SessionSealJournalEntry {
    std::uint32_t slot_handle = 0U;
    std::uint32_t codec_entry_handle = 0U;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
    bool terminal_result = false;
};

struct SessionStepJournal {
    std::uint32_t transaction_handle = 0U;
    contracts::TransactionBranch branch =
        contracts::TransactionBranch::Continue;
    bool branch_selected = false;
    std::uint64_t generation = 0U;
    std::uint64_t base_epoch = 0U;
    std::uint64_t committed_epoch = 0U;
    std::int64_t base_tick = 0;
    std::int64_t committed_tick = 0;
    std::size_t output_write_count = 0U;
    bool committed = false;
    bool history_staged = false;
    bool observation_seal_staged = false;
    bool result_seal_staged = false;
    bool terminal_result_present = false;
    bool prevalidated = false;
    SessionResult primary_failure;
    std::vector<std::uint32_t> executed_callsite_handles;
    std::vector<std::uint32_t> skipped_callsite_handles;
    std::vector<std::uint32_t> integration_scope_handles;
    std::vector<std::uint32_t> candidate_slot_handles;
    std::vector<SessionCandidateJournalEntry> candidates;
    std::vector<SessionHistoryJournalEntry> histories;
    std::vector<SessionSealJournalEntry> seals;
};

using SessionStepSummary = SessionStepJournal;

class Session;
struct SessionCreation;
[[nodiscard]] SessionCreation create_session(
    std::shared_ptr<const contracts::ExecutionPlanImage> image,
    std::shared_ptr<const SessionMaterializationProvider> provider) noexcept;

class Session final {
  public:
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] const contracts::ExecutionPlanImage& image() const noexcept;
    [[nodiscard]] InitializationOutcome initialize(
        InitializationRequest request) noexcept;
    [[nodiscard]] ResetOutcome reset(ResetRequest request) noexcept;
    [[nodiscard]] SessionResult dispose() noexcept;
    [[nodiscard]] StepOutcome execute_step() noexcept;
    [[nodiscard]] SessionResult run_to_terminal() noexcept;
    [[nodiscard]] const SessionResult& last_result() const noexcept;
    [[nodiscard]] const InitializationOutcome&
    last_initialization_outcome() const noexcept;
    [[nodiscard]] const ResetOutcome& last_reset_outcome() const noexcept;
    [[nodiscard]] const StepOutcome& last_step_outcome() const noexcept;
    [[nodiscard]] const RunId* active_run_id() const noexcept;
    [[nodiscard]] const RunBinding* active_run_binding() const noexcept;
    [[nodiscard]] const RunId* last_committed_run_id() const noexcept;
    [[nodiscard]] const RunBinding* last_committed_run_binding() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> run_sequence() const noexcept;
    [[nodiscard]] const RunOutcome* run_outcome() const noexcept;
    [[nodiscard]] const RunOutcome* run_outcome_for_sequence(
        std::uint64_t run_sequence) const noexcept;
    [[nodiscard]] const SessionBoundarySummary& last_boundary_summary()
        const noexcept;
    [[nodiscard]] const SessionStepSummary& last_step_summary()
        const noexcept;
    [[nodiscard]] const SessionStepJournal& last_step_journal()
        const noexcept;

    [[nodiscard]] std::size_t preparation_count() const noexcept;
    [[nodiscard]] std::size_t runtime_cell_count() const noexcept;
    [[nodiscard]] std::size_t committed_state_count() const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> preparation_handles() const;
    [[nodiscard]] std::vector<std::uint32_t> runtime_component_handles() const;
    [[nodiscard]] std::vector<std::uint32_t> committed_state_block_handles()
        const;
    [[nodiscard]] std::vector<SessionStorageExtent> storage_extents() const;
    [[nodiscard]] std::vector<SessionStateBlockInfo> state_blocks() const;
    [[nodiscard]] std::vector<SessionFrameSlotInfo> frame_slots() const;
    [[nodiscard]] std::vector<SessionCommittedOutputInfo> committed_outputs()
        const;
    [[nodiscard]] std::vector<SessionCommittedHistoryInfo>
    committed_histories() const;
    [[nodiscard]] std::uint64_t committed_epoch() const noexcept;
    [[nodiscard]] std::int64_t committed_tick() const noexcept;
    [[nodiscard]] std::uint64_t committed_step_count() const noexcept;
    [[nodiscard]] bool frame_open() const noexcept;

  private:
    struct Impl;
    explicit Session(std::unique_ptr<Impl> implementation) noexcept;
    [[nodiscard]] SessionResult qualification_read_candidate(
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] SessionResult qualification_read_committed(
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] SessionResult qualification_replace_candidate(
        std::uint32_t state_block_handle,
        InProcessValueView value) noexcept;
    [[nodiscard]] SessionResult qualification_read_committed_output(
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] SessionResult qualification_execute_opening_boundary()
        noexcept;
    std::unique_ptr<Impl> implementation_;

    friend class qualification::SessionAccess;
    friend SessionCreation create_session(
        std::shared_ptr<const contracts::ExecutionPlanImage> image,
        std::shared_ptr<const SessionMaterializationProvider> provider)
        noexcept;
};

struct SessionCreation {
    std::unique_ptr<Session> session;
    SessionResult result;

    [[nodiscard]] explicit operator bool() const noexcept {
        return session != nullptr && result.error == SessionError::None;
    }
};

[[nodiscard]] SessionCreation create_session(
    std::shared_ptr<const contracts::ExecutionPlanImage> image,
    std::shared_ptr<const SessionMaterializationProvider> provider) noexcept;

} // namespace gnc::kernel
