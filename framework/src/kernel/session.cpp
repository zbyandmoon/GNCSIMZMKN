#include "gnc/kernel/session.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace gnc::kernel {
namespace {

constexpr std::uint32_t kSupportedImageRevision = 3U;

[[nodiscard]] bool valid_alignment(std::uint64_t alignment) noexcept {
    return alignment != 0U && (alignment & (alignment - 1U)) == 0U &&
           alignment <= static_cast<std::uint64_t>(
                            (std::numeric_limits<std::size_t>::max)());
}

[[nodiscard]] void* allocate_raw(std::uint64_t size,
                                 std::uint64_t alignment) noexcept {
    if (size == 0U ||
        size > static_cast<std::uint64_t>(
                   (std::numeric_limits<std::size_t>::max)()) ||
        !valid_alignment(alignment)) {
        return nullptr;
    }
    try {
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return ::operator new(static_cast<std::size_t>(size),
                                  std::align_val_t(alignment));
        }
        return ::operator new(static_cast<std::size_t>(size));
    } catch (...) {
        return nullptr;
    }
}

void release_raw(void* address, std::uint64_t alignment) noexcept {
    if (address == nullptr) {
        return;
    }
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(address, std::align_val_t(alignment));
        return;
    }
    ::operator delete(address);
}

class RawBlock final {
  public:
    RawBlock(std::uint64_t size, std::uint64_t alignment) noexcept
        : address_(allocate_raw(size, alignment)), alignment_(alignment) {}
    ~RawBlock() { release_raw(address_, alignment_); }
    RawBlock(const RawBlock&) = delete;
    RawBlock& operator=(const RawBlock&) = delete;

    [[nodiscard]] void* get() const noexcept { return address_; }
    [[nodiscard]] void* release() noexcept {
        auto* result = address_;
        address_ = nullptr;
        return result;
    }

  private:
    void* address_ = nullptr;
    std::uint64_t alignment_ = 0U;
};

class ConstructedObject final {
  public:
    ConstructedObject(void* address,
                      const InProcessObjectOperations& operations) noexcept
        : address_(address), operations_(&operations) {}
    ~ConstructedObject() {
        if (address_ != nullptr) {
            operations_->destroy(address_);
        }
    }
    ConstructedObject(const ConstructedObject&) = delete;
    ConstructedObject& operator=(const ConstructedObject&) = delete;
    void release() noexcept { address_ = nullptr; }

  private:
    void* address_ = nullptr;
    const InProcessObjectOperations* operations_ = nullptr;
};

template <typename Value>
[[nodiscard]] const Value* find_handle(const std::vector<Value>& values,
                                       std::uint32_t handle) noexcept {
    const auto found = std::find_if(
        values.begin(), values.end(), [handle](const auto& value) {
            return value.handle == handle;
        });
    return found == values.end() ? nullptr : &*found;
}

template <typename Value>
[[nodiscard]] bool unique_nonzero_handles(
    const std::vector<Value>& values) noexcept {
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (values[index].handle == 0U) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (values[prior].handle == values[index].handle) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool unique_nonzero_handles(
    const std::vector<std::uint32_t>& values) noexcept {
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (values[index] == 0U) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (values[prior] == values[index]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool same_handle_set(
    const std::vector<std::uint32_t>& lhs,
    const std::vector<std::uint32_t>& rhs) noexcept {
    if (lhs.size() != rhs.size() || !unique_nonzero_handles(lhs) ||
        !unique_nonzero_handles(rhs)) {
        return false;
    }
    return std::all_of(lhs.begin(), lhs.end(), [&rhs](std::uint32_t handle) {
        return std::find(rhs.begin(), rhs.end(), handle) != rhs.end();
    });
}

template <typename Value>
[[nodiscard]] bool exact_handle_membership(
    const std::vector<std::uint32_t>& handles,
    const std::vector<Value>& values) noexcept {
    if (handles.size() != values.size() || !unique_nonzero_handles(handles)) {
        return false;
    }
    return std::all_of(handles.begin(), handles.end(), [&values](auto handle) {
        return find_handle(values, handle) != nullptr;
    });
}

[[nodiscard]] bool same_layout(
    const InProcessObjectLayout& actual, std::uint64_t size,
    std::uint64_t alignment, std::string_view layout,
    std::uint32_t codec, SessionError& error) noexcept {
    if (actual.size_bytes != size) {
        error = SessionError::ObjectSizeMismatch;
        return false;
    }
    if (actual.alignment_bytes != alignment) {
        error = SessionError::ObjectAlignmentMismatch;
        return false;
    }
    if (actual.layout_identity != layout) {
        error = SessionError::ObjectLayoutMismatch;
        return false;
    }
    if (actual.codec_entry_handle != codec) {
        error = SessionError::ObjectCodecMismatch;
        return false;
    }
    if (actual.type_identity == nullptr || !valid_alignment(alignment)) {
        error = SessionError::InvalidStorageLayout;
        return false;
    }
    return true;
}

[[nodiscard]] SessionObjectRole role_for_slot(
    const contracts::PlanImageSlot& slot) noexcept {
    if (slot.kind == contracts::PlanImageSlotKind::CommittedState) {
        return SessionObjectRole::CommittedState;
    }
    if (slot.kind == contracts::PlanImageSlotKind::CandidateState) {
        return SessionObjectRole::CandidateState;
    }
    if (slot.kind == contracts::PlanImageSlotKind::HeldIntervalValue) {
        return SessionObjectRole::HeldIntervalValue;
    }
    if (slot.storage_class == contracts::SlotStorageClass::TerminalResult) {
        return SessionObjectRole::TerminalOutputValue;
    }
    return SessionObjectRole::CycleFrameValue;
}

[[nodiscard]] bool valid_slot_storage_pair(
    const contracts::PlanImageSlot& slot) noexcept {
    using Kind = contracts::PlanImageSlotKind;
    using Storage = contracts::SlotStorageClass;
    using Hold = contracts::SlotHoldPolicy;
    if (slot.kind == Kind::CommittedState) {
        return slot.storage_class == Storage::StateStore &&
               slot.hold_policy == Hold::Committed;
    }
    if (slot.kind == Kind::CandidateState) {
        return slot.storage_class == Storage::TransactionCandidate &&
               slot.hold_policy == Hold::CurrentBoundary;
    }
    if (slot.kind == Kind::HeldIntervalValue) {
        return slot.storage_class == Storage::IntegrationHeld &&
               slot.hold_policy == Hold::HoldInterval;
    }
    return (slot.storage_class == Storage::CycleFrame &&
            slot.hold_policy == Hold::CurrentBoundary) ||
           (slot.storage_class == Storage::TerminalResult &&
            slot.hold_policy == Hold::Terminal);
}

[[nodiscard]] bool same_requirement(
    const SessionObjectRequirement& expected,
    const SessionObjectIdentityView& actual) noexcept {
    return actual &&
           expected.image_object_handle == actual.image_object_handle &&
           expected.role == actual.role &&
           expected.linked_entry_handle == actual.linked_entry_handle &&
           expected.codec_entry_handle == actual.codec_entry_handle &&
           expected.size_bytes == actual.size_bytes &&
           expected.alignment_bytes == actual.alignment_bytes &&
           expected.type_identity != nullptr &&
           expected.type_identity == actual.type_identity;
}

[[nodiscard]] bool has_materialized_storage(SessionState state) noexcept {
    return state == SessionState::Initialized ||
           state == SessionState::Completed || state == SessionState::Failed;
}

[[nodiscard]] bool binding_matches_image(
    const RunBinding& binding,
    const contracts::ExecutionPlanImage& image) noexcept {
    return binding.image_fingerprint == image.fingerprint() &&
           binding.plan_id == image.plan_id() &&
           binding.mission_id == image.mission_id() &&
           binding.source_semantic_hash == image.source_semantic_hash() &&
           binding.descriptor_semantic_hash ==
               image.descriptor_semantic_hash();
}

[[nodiscard]] RuntimeDiagnosticCode diagnostic_code_for(
    SessionError error) noexcept {
    switch (error) {
    case SessionError::None:
        return RuntimeDiagnosticCode::None;
    case SessionError::EmptyRunId:
        return RuntimeDiagnosticCode::InitializationRequestInvalid;
    case SessionError::DuplicateRunId:
        return RuntimeDiagnosticCode::ResetRequestInvalid;
    case SessionError::RunBindingMismatch:
        return RuntimeDiagnosticCode::ImageBindingMismatch;
    case SessionError::NullImage:
    case SessionError::NullMaterializationProvider:
    case SessionError::UnsupportedImageRevision:
    case SessionError::InvalidImageHandle:
    case SessionError::InvalidImageStructure:
    case SessionError::InvalidStorageLayout:
    case SessionError::StorageBoundsViolation:
    case SessionError::StorageOverlap:
        return RuntimeDiagnosticCode::ImageValidationFailed;
    case SessionError::MissingMaterializer:
    case SessionError::InvalidMaterializerIdentity:
    case SessionError::PreparationFailed:
    case SessionError::RuntimeCellFailed:
    case SessionError::SlotConstructionFailed:
    case SessionError::InitialStateFailed:
        return RuntimeDiagnosticCode::MaterializationFailed;
    case SessionError::ResetStateFailed:
        return RuntimeDiagnosticCode::ResetStateRebuildFailed;
    case SessionError::ResetCapabilityMissing:
        return RuntimeDiagnosticCode::ResetCapabilityMissing;
    case SessionError::ResetPrecommitFailed:
        return RuntimeDiagnosticCode::ResetPrecommitFailed;
    case SessionError::InvalidSchedule:
        return RuntimeDiagnosticCode::ScheduleFailed;
    case SessionError::FrameAlreadyOpen:
    case SessionError::FrameNotOpen:
    case SessionError::FrameSlotAbsent:
    case SessionError::StaleFrameView:
        return RuntimeDiagnosticCode::FrameFailed;
    case SessionError::StateAuthorizationFailure:
    case SessionError::ReaderAuthorizationFailure:
    case SessionError::WriterAuthorizationFailure:
    case SessionError::CandidateAuthorizationFailure:
    case SessionError::HistoryAuthorizationFailure:
        return RuntimeDiagnosticCode::AuthorizationFailed;
    case SessionError::HistoryValidationFailed:
        return RuntimeDiagnosticCode::HistoryFailed;
    case SessionError::CandidateRearmFailed:
    case SessionError::CandidateValidationFailed:
        return RuntimeDiagnosticCode::CandidateFailed;
    case SessionError::ObservationSealFailed:
        return RuntimeDiagnosticCode::ObservationSealFailed;
    case SessionError::TransactionPrecommitFailed:
        return RuntimeDiagnosticCode::TransactionPrecommitFailed;
    case SessionError::InvocationFailed:
        return RuntimeDiagnosticCode::InvocationFailed;
    case SessionError::ObjectSizeMismatch:
    case SessionError::ObjectAlignmentMismatch:
    case SessionError::ObjectLayoutMismatch:
    case SessionError::ObjectCodecMismatch:
    case SessionError::ObjectTypeMismatch:
    case SessionError::ObjectValidationFailed:
        return RuntimeDiagnosticCode::ObjectValidationFailed;
    case SessionError::AllocationFailure:
        return RuntimeDiagnosticCode::AllocationFailed;
    case SessionError::InternalFailure:
        return RuntimeDiagnosticCode::InternalFailure;
    case SessionError::InvalidLifecycleTransition:
        return RuntimeDiagnosticCode::LifecycleTransitionRejected;
    }
    return RuntimeDiagnosticCode::InternalFailure;
}

[[nodiscard]] std::string_view message_key_for(
    RuntimeDiagnosticCode code) noexcept {
    switch (code) {
    case RuntimeDiagnosticCode::None: return {};
    case RuntimeDiagnosticCode::InitializationRequestInvalid:
        return "run.initialization_request.invalid";
    case RuntimeDiagnosticCode::ImageBindingMismatch:
        return "run.binding.image_mismatch";
    case RuntimeDiagnosticCode::ImageValidationFailed:
        return "run.image.validation_failed";
    case RuntimeDiagnosticCode::MaterializationFailed:
        return "run.materialization.failed";
    case RuntimeDiagnosticCode::ScheduleFailed:
        return "run.schedule.failed";
    case RuntimeDiagnosticCode::FrameFailed:
        return "run.frame.failed";
    case RuntimeDiagnosticCode::AuthorizationFailed:
        return "run.authorization.failed";
    case RuntimeDiagnosticCode::HistoryFailed:
        return "run.history.failed";
    case RuntimeDiagnosticCode::CandidateFailed:
        return "run.candidate.failed";
    case RuntimeDiagnosticCode::ObservationSealFailed:
        return "run.observation_seal.failed";
    case RuntimeDiagnosticCode::TransactionPrecommitFailed:
        return "run.transaction.precommit_failed";
    case RuntimeDiagnosticCode::InvocationFailed:
        return "run.invocation.failed";
    case RuntimeDiagnosticCode::ObjectValidationFailed:
        return "run.object.validation_failed";
    case RuntimeDiagnosticCode::AllocationFailed:
        return "run.allocation.failed";
    case RuntimeDiagnosticCode::InternalFailure:
        return "run.internal.failure";
    case RuntimeDiagnosticCode::LifecycleTransitionRejected:
        return "run.lifecycle.transition_rejected";
    case RuntimeDiagnosticCode::ResetRequestInvalid:
        return "run.reset_request.invalid";
    case RuntimeDiagnosticCode::ResetStateRebuildFailed:
        return "run.reset_state.rebuild_failed";
    case RuntimeDiagnosticCode::ResetCapabilityMissing:
        return "run.reset.capability_missing";
    case RuntimeDiagnosticCode::ResetPrecommitFailed:
        return "run.reset.precommit_failed";
    }
    return "run.internal.failure";
}

} // namespace

RunBinding exact_run_binding(const contracts::ExecutionPlanImage& image) {
    return {image.fingerprint(), image.plan_id(), image.mission_id(),
            image.source_semantic_hash(),
            image.descriptor_semantic_hash()};
}

std::string_view to_string(SessionError error) noexcept {
    switch (error) {
    case SessionError::None: return "None";
    case SessionError::NullImage: return "NullImage";
    case SessionError::NullMaterializationProvider:
        return "NullMaterializationProvider";
    case SessionError::EmptyRunId: return "EmptyRunId";
    case SessionError::DuplicateRunId: return "DuplicateRunId";
    case SessionError::RunBindingMismatch: return "RunBindingMismatch";
    case SessionError::UnsupportedImageRevision:
        return "UnsupportedImageRevision";
    case SessionError::InvalidImageHandle: return "InvalidImageHandle";
    case SessionError::InvalidImageStructure: return "InvalidImageStructure";
    case SessionError::InvalidStorageLayout: return "InvalidStorageLayout";
    case SessionError::StorageBoundsViolation: return "StorageBoundsViolation";
    case SessionError::StorageOverlap: return "StorageOverlap";
    case SessionError::MissingMaterializer: return "MissingMaterializer";
    case SessionError::InvalidMaterializerIdentity:
        return "InvalidMaterializerIdentity";
    case SessionError::ObjectSizeMismatch: return "ObjectSizeMismatch";
    case SessionError::ObjectAlignmentMismatch:
        return "ObjectAlignmentMismatch";
    case SessionError::ObjectLayoutMismatch: return "ObjectLayoutMismatch";
    case SessionError::ObjectCodecMismatch: return "ObjectCodecMismatch";
    case SessionError::ObjectTypeMismatch: return "ObjectTypeMismatch";
    case SessionError::AllocationFailure: return "AllocationFailure";
    case SessionError::PreparationFailed: return "PreparationFailed";
    case SessionError::RuntimeCellFailed: return "RuntimeCellFailed";
    case SessionError::SlotConstructionFailed: return "SlotConstructionFailed";
    case SessionError::InitialStateFailed: return "InitialStateFailed";
    case SessionError::ResetStateFailed: return "ResetStateFailed";
    case SessionError::ResetCapabilityMissing:
        return "ResetCapabilityMissing";
    case SessionError::ResetPrecommitFailed: return "ResetPrecommitFailed";
    case SessionError::ObjectValidationFailed: return "ObjectValidationFailed";
    case SessionError::InvalidLifecycleTransition:
        return "InvalidLifecycleTransition";
    case SessionError::InvalidSchedule: return "InvalidSchedule";
    case SessionError::FrameAlreadyOpen: return "FrameAlreadyOpen";
    case SessionError::FrameNotOpen: return "FrameNotOpen";
    case SessionError::FrameSlotAbsent: return "FrameSlotAbsent";
    case SessionError::StaleFrameView: return "StaleFrameView";
    case SessionError::StateAuthorizationFailure:
        return "StateAuthorizationFailure";
    case SessionError::ReaderAuthorizationFailure:
        return "ReaderAuthorizationFailure";
    case SessionError::WriterAuthorizationFailure:
        return "WriterAuthorizationFailure";
    case SessionError::CandidateAuthorizationFailure:
        return "CandidateAuthorizationFailure";
    case SessionError::CandidateRearmFailed: return "CandidateRearmFailed";
    case SessionError::CandidateValidationFailed:
        return "CandidateValidationFailed";
    case SessionError::HistoryAuthorizationFailure:
        return "HistoryAuthorizationFailure";
    case SessionError::HistoryValidationFailed:
        return "HistoryValidationFailed";
    case SessionError::ObservationSealFailed:
        return "ObservationSealFailed";
    case SessionError::TransactionPrecommitFailed:
        return "TransactionPrecommitFailed";
    case SessionError::InvocationFailed: return "InvocationFailed";
    case SessionError::InternalFailure: return "InternalFailure";
    }
    return "InternalFailure";
}

std::string_view to_string(RuntimeDiagnosticCode code) noexcept {
    switch (code) {
    case RuntimeDiagnosticCode::None: return "None";
    case RuntimeDiagnosticCode::InitializationRequestInvalid:
        return "GNC-RUN-INIT-0001";
    case RuntimeDiagnosticCode::ImageBindingMismatch:
        return "GNC-RUN-INIT-0002";
    case RuntimeDiagnosticCode::ImageValidationFailed:
        return "GNC-RUN-INIT-0003";
    case RuntimeDiagnosticCode::MaterializationFailed:
        return "GNC-RUN-INIT-0004";
    case RuntimeDiagnosticCode::ScheduleFailed:
        return "GNC-RUN-SCH-0001";
    case RuntimeDiagnosticCode::FrameFailed:
        return "GNC-RUN-FRM-0001";
    case RuntimeDiagnosticCode::AuthorizationFailed:
        return "GNC-RUN-AUT-0001";
    case RuntimeDiagnosticCode::HistoryFailed:
        return "GNC-RUN-HIS-0001";
    case RuntimeDiagnosticCode::CandidateFailed:
        return "GNC-RUN-CAN-0001";
    case RuntimeDiagnosticCode::ObservationSealFailed:
        return "GNC-RUN-SEA-0001";
    case RuntimeDiagnosticCode::TransactionPrecommitFailed:
        return "GNC-RUN-TXN-0001";
    case RuntimeDiagnosticCode::InvocationFailed:
        return "GNC-RUN-INV-0001";
    case RuntimeDiagnosticCode::ObjectValidationFailed:
        return "GNC-RUN-OBJ-0001";
    case RuntimeDiagnosticCode::AllocationFailed:
        return "GNC-RUN-ALL-0001";
    case RuntimeDiagnosticCode::InternalFailure:
        return "GNC-RUN-INT-0001";
    case RuntimeDiagnosticCode::LifecycleTransitionRejected:
        return "GNC-RUN-LIF-0001";
    case RuntimeDiagnosticCode::ResetRequestInvalid:
        return "GNC-RUN-RST-0001";
    case RuntimeDiagnosticCode::ResetStateRebuildFailed:
        return "GNC-RUN-RST-0002";
    case RuntimeDiagnosticCode::ResetCapabilityMissing:
        return "GNC-RUN-RST-0003";
    case RuntimeDiagnosticCode::ResetPrecommitFailed:
        return "GNC-RUN-RST-0004";
    }
    return "GNC-RUN-INT-0001";
}

std::string_view to_string(RuntimeDiagnosticStage stage) noexcept {
    switch (stage) {
    case RuntimeDiagnosticStage::InitializationRequest:
        return "InitializationRequest";
    case RuntimeDiagnosticStage::InitializationValidation:
        return "InitializationValidation";
    case RuntimeDiagnosticStage::Materialization:
        return "Materialization";
    case RuntimeDiagnosticStage::InitialState: return "InitialState";
    case RuntimeDiagnosticStage::Schedule: return "Schedule";
    case RuntimeDiagnosticStage::History: return "History";
    case RuntimeDiagnosticStage::BoundaryInvocation:
        return "BoundaryInvocation";
    case RuntimeDiagnosticStage::CandidateProduction:
        return "CandidateProduction";
    case RuntimeDiagnosticStage::ObservationSeal:
        return "ObservationSeal";
    case RuntimeDiagnosticStage::Precommit: return "Precommit";
    case RuntimeDiagnosticStage::Finalization: return "Finalization";
    case RuntimeDiagnosticStage::Lifecycle: return "Lifecycle";
    case RuntimeDiagnosticStage::ResetRequest: return "ResetRequest";
    case RuntimeDiagnosticStage::ResetState: return "ResetState";
    case RuntimeDiagnosticStage::ResetPrecommit: return "ResetPrecommit";
    }
    return "Lifecycle";
}

SessionCommittedStateView::SessionCommittedStateView(
    const SessionCommittedStateAccess* access,
    SessionStateAuthorityKind authority_kind,
    std::uint32_t authority_handle) noexcept
    : access_(access), authority_kind_(authority_kind),
      authority_handle_(authority_handle) {}

SessionResult SessionCommittedStateView::read(
    std::uint32_t state_block_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (access_ == nullptr) {
        return {SessionError::InvalidLifecycleTransition, state_block_handle,
                "committed state view is unavailable"};
    }
    return access_->read_committed(
        static_cast<std::uint8_t>(authority_kind_), authority_handle_,
        state_block_handle, result);
}

SessionInputView::SessionInputView(const SessionFrameAccess* access,
                                   SessionFrameAuthorityKind authority_kind,
                                   std::uint32_t authority_handle,
                                   std::uint64_t generation) noexcept
    : access_(access), authority_kind_(authority_kind),
      authority_handle_(authority_handle),
      generation_(generation) {}

SessionResult SessionInputView::read(
    std::uint32_t slot_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (access_ == nullptr) {
        return {SessionError::StaleFrameView, slot_handle,
                "input view is unavailable"};
    }
    return access_->read_input(
        static_cast<std::uint8_t>(authority_kind_), authority_handle_,
        generation_, slot_handle, result);
}

bool SessionInputView::active() const noexcept {
    return access_ != nullptr && access_->frame_active(generation_);
}

SessionCandidateWriterSet::SessionCandidateWriterSet(
    SessionCandidateAccess* access,
    SessionCandidateProducerKind producer_kind,
    std::uint32_t producer_handle, std::uint64_t generation,
    std::uint32_t transaction_handle) noexcept
    : access_(access), producer_kind_(producer_kind),
      producer_handle_(producer_handle), generation_(generation),
      transaction_handle_(transaction_handle) {}

SessionResult SessionCandidateWriterSet::write(
    std::uint32_t candidate_slot_handle,
    std::uint32_t writer_token_handle,
    InProcessValueView value) const noexcept {
    if (access_ == nullptr) {
        return {SessionError::StaleFrameView, candidate_slot_handle,
                "candidate writer set is unavailable"};
    }
    return access_->write_candidate(
        producer_kind_, producer_handle_, generation_, transaction_handle_,
        candidate_slot_handle, writer_token_handle, value);
}

bool SessionCandidateWriterSet::active() const noexcept {
    return access_ != nullptr && access_->transaction_active(generation_);
}

SessionOutputWriterSet::SessionOutputWriterSet(
    SessionFrameAccess* access, std::uint32_t callsite_handle,
    std::uint64_t generation) noexcept
    : access_(access), callsite_handle_(callsite_handle),
      generation_(generation) {}

SessionResult SessionOutputWriterSet::write(
    std::uint32_t slot_handle, std::uint32_t writer_token_handle,
    InProcessValueView value) const noexcept {
    if (access_ == nullptr) {
        return {SessionError::StaleFrameView, slot_handle,
                "output writer set is unavailable"};
    }
    return access_->write_output(callsite_handle_, generation_, slot_handle,
                                 writer_token_handle, value);
}

bool SessionOutputWriterSet::active() const noexcept {
    return access_ != nullptr && access_->frame_active(generation_);
}

SessionCommittedHistoryView::SessionCommittedHistoryView(
    const SessionCommittedHistoryAccess* access,
    std::uint32_t callsite_handle, std::uint64_t generation) noexcept
    : access_(access), callsite_handle_(callsite_handle),
      generation_(generation) {}

SessionResult SessionCommittedHistoryView::info(
    std::uint32_t history_handle,
    SessionCommittedHistoryInfo& result) const noexcept {
    result = {};
    if (access_ == nullptr) {
        return {SessionError::HistoryAuthorizationFailure, history_handle,
                "committed history view is unavailable"};
    }
    return access_->history_info(callsite_handle_, generation_,
                                 history_handle, result);
}

SessionResult SessionCommittedHistoryView::read(
    std::uint32_t history_handle, std::size_t sample_index,
    std::size_t member_index, std::int64_t& sample_tick,
    SessionObjectIdentityView& result) const noexcept {
    sample_tick = 0;
    result = {};
    if (access_ == nullptr) {
        return {SessionError::HistoryAuthorizationFailure, history_handle,
                "committed history view is unavailable"};
    }
    return access_->read_history_member(
        callsite_handle_, generation_, history_handle, sample_index,
        member_index, sample_tick, result);
}

bool SessionCommittedHistoryView::active() const noexcept {
    return access_ != nullptr &&
           access_->history_active(callsite_handle_, generation_);
}

SessionInvocationContext::SessionInvocationContext(
    std::uint32_t callsite_handle, std::uint32_t component_handle,
    std::int64_t tick, double boundary_time_seconds,
    double interval_start_seconds, double interval_end_seconds,
    contracts::DataQuality quality, SessionObjectIdentityView runtime_cell,
    SessionCommittedStateView committed, SessionInputView inputs,
    SessionOutputWriterSet outputs, SessionCommittedHistoryView history,
    SessionCandidateWriterSet candidates) noexcept
    : callsite_handle_(callsite_handle), component_handle_(component_handle),
      tick_(tick), boundary_time_seconds_(boundary_time_seconds),
      interval_start_seconds_(interval_start_seconds),
      interval_end_seconds_(interval_end_seconds), quality_(quality),
      runtime_cell_(runtime_cell), committed_(committed), inputs_(inputs),
      outputs_(outputs), history_(history), candidates_(candidates) {}

SessionIntegrationContext::SessionIntegrationContext(
    std::uint32_t integration_scope_handle,
    std::uint32_t component_handle,
    std::uint32_t transaction_handle, std::int64_t tick,
    double interval_start_seconds, double interval_end_seconds,
    SessionObjectIdentityView runtime_cell,
    SessionCommittedStateView committed, SessionInputView inputs,
    SessionCandidateWriterSet candidates) noexcept
    : integration_scope_handle_(integration_scope_handle),
      component_handle_(component_handle),
      transaction_handle_(transaction_handle), tick_(tick),
      interval_start_seconds_(interval_start_seconds),
      interval_end_seconds_(interval_end_seconds),
      runtime_cell_(runtime_cell), committed_(committed), inputs_(inputs),
      candidates_(candidates) {}

struct Session::Impl final : SessionObjectAccess,
                             SessionCommittedStateAccess,
                             SessionFrameAccess,
                             SessionCommittedHistoryAccess,
                             SessionCandidateAccess {
    struct OwnedObject {
        std::uint32_t handle = 0U;
        void* address = nullptr;
        std::uint64_t alignment = 0U;
        const SessionObjectMaterializer* materializer = nullptr;
        SessionObjectRole role = SessionObjectRole::PreparedModel;
        std::uint32_t linked_entry_handle = 0U;
    };

    struct Arena {
        const contracts::PlanImageStorageLayout* layout = nullptr;
        void* address = nullptr;
    };

    struct StateObject {
        const contracts::PlanImageStateBlock* block = nullptr;
        const contracts::PlanImageInitialBinding* initial = nullptr;
        void* address = nullptr;
        const SessionObjectMaterializer* materializer = nullptr;
        std::uint32_t owner_runtime_component_handle = 0U;
        std::uint64_t committed_epoch = 0U;
        bool candidate_present = false;
        std::uint64_t candidate_generation = 0U;
        std::uint64_t candidate_base_epoch = 0U;
        std::uint32_t candidate_producer_handle = 0U;
        std::uint32_t candidate_writer_token_handle = 0U;
    };

    struct FrameSlot {
        const contracts::PlanImageSlot* slot = nullptr;
        void* address = nullptr;
        const SessionObjectMaterializer* materializer = nullptr;
        bool owns_address = false;
        bool present = false;
        std::uint64_t generation = 0U;
        std::uint64_t sequence = 0U;
        std::int64_t sample_tick = 0;
        double sample_time_seconds = 0.0;
        double interval_start_seconds = 0.0;
        double interval_end_seconds = 0.0;
        contracts::DataQuality quality = contracts::DataQuality::Invalid;
    };

    struct StoredValue {
        const contracts::PlanImageSlot* slot = nullptr;
        const SessionObjectMaterializer* materializer = nullptr;
        void* address = nullptr;
        std::uint64_t alignment = 0U;
        std::uint64_t generation = 0U;
        std::uint64_t sequence = 0U;
        std::int64_t sample_tick = 0;
        double sample_time_seconds = 0.0;
        double interval_start_seconds = 0.0;
        double interval_end_seconds = 0.0;
        contracts::DataQuality quality = contracts::DataQuality::Invalid;

        StoredValue() = default;
        StoredValue(const StoredValue&) = delete;
        StoredValue& operator=(const StoredValue&) = delete;
        StoredValue(StoredValue&& other) noexcept { take(other); }
        StoredValue& operator=(StoredValue&& other) noexcept {
            if (this != &other) {
                reset();
                take(other);
            }
            return *this;
        }
        ~StoredValue() { reset(); }

        void reset() noexcept {
            if (address != nullptr && materializer != nullptr) {
                materializer->operations().destroy(address);
                release_raw(address, alignment);
            }
            slot = nullptr;
            materializer = nullptr;
            address = nullptr;
            alignment = 0U;
        }

      private:
        void take(StoredValue& other) noexcept {
            slot = other.slot;
            materializer = other.materializer;
            address = other.address;
            alignment = other.alignment;
            generation = other.generation;
            sequence = other.sequence;
            sample_tick = other.sample_tick;
            sample_time_seconds = other.sample_time_seconds;
            interval_start_seconds = other.interval_start_seconds;
            interval_end_seconds = other.interval_end_seconds;
            quality = other.quality;
            other.slot = nullptr;
            other.materializer = nullptr;
            other.address = nullptr;
            other.alignment = 0U;
        }
    };

    struct ResetStateReplacement {
        const SessionObjectMaterializer* materializer = nullptr;
        void* committed_address = nullptr;
        void* candidate_address = nullptr;
        std::uint64_t alignment = 0U;

        ResetStateReplacement() = default;
        ResetStateReplacement(const ResetStateReplacement&) = delete;
        ResetStateReplacement& operator=(const ResetStateReplacement&) =
            delete;
        ResetStateReplacement(ResetStateReplacement&& other) noexcept {
            take(other);
        }
        ResetStateReplacement& operator=(
            ResetStateReplacement&& other) noexcept {
            if (this != &other) {
                reset();
                take(other);
            }
            return *this;
        }
        ~ResetStateReplacement() { reset(); }

        void reset() noexcept {
            if (candidate_address != nullptr && materializer != nullptr) {
                materializer->operations().destroy(candidate_address);
                release_raw(candidate_address, alignment);
            }
            if (committed_address != nullptr && materializer != nullptr) {
                materializer->operations().destroy(committed_address);
                release_raw(committed_address, alignment);
            }
            materializer = nullptr;
            committed_address = nullptr;
            candidate_address = nullptr;
            alignment = 0U;
        }

      private:
        void take(ResetStateReplacement& other) noexcept {
            materializer = other.materializer;
            committed_address = other.committed_address;
            candidate_address = other.candidate_address;
            alignment = other.alignment;
            other.materializer = nullptr;
            other.committed_address = nullptr;
            other.candidate_address = nullptr;
            other.alignment = 0U;
        }
    };

    struct HistorySample {
        std::int64_t tick = 0;
        std::uint64_t committed_epoch = 0U;
        std::vector<StoredValue> members;
    };

    struct EvaluatorHistoryStore {
        const contracts::PlanImageEvaluatorHistory* plan = nullptr;
        std::vector<HistorySample> samples;
    };

    struct SealedBoundaryStore {
        std::uint64_t committed_epoch = 0U;
        std::int64_t committed_tick = 0;
        std::vector<StoredValue> outputs;
    };

    struct ScheduledCall {
        std::uint32_t callsite_handle = 0U;
        std::uint32_t component_handle = 0U;
        contracts::PlanImageEntryKind entry_kind =
            contracts::PlanImageEntryKind::BoundaryEvaluation;
    };

    struct SessionRuntimeBindings {
        std::vector<OwnedObject> cells;
    };

    struct CommittedStateStore {
        std::vector<StateObject> blocks;
    };

    struct TransactionCandidateStore {
        std::vector<StateObject> blocks;
    };

    struct CycleFrame {
        std::vector<FrameSlot> slots;
        std::vector<std::size_t> construction_order;
        std::uint64_t generation = 0U;
        std::uint64_t sequence = 0U;
        std::size_t write_count = 0U;
        bool open = false;
    };

    std::shared_ptr<const contracts::ExecutionPlanImage> image;
    std::shared_ptr<const SessionMaterializationProvider> provider;
    SessionState state = SessionState::Created;
    SessionResult last_result;
    InitializationOutcome initialization_outcome;
    ResetOutcome reset_outcome;
    StepOutcome step_outcome;
    std::optional<InitializationRequest> pending_run_attempt;
    std::optional<ResetRequest> pending_reset_attempt;
    std::uint64_t pending_reset_run_sequence = 0U;
    std::optional<RunId> committed_run_id;
    std::optional<RunBinding> committed_run_binding;
    std::uint64_t committed_run_sequence = 0U;
    bool has_committed_run = false;
    std::vector<std::unique_ptr<RunOutcome>> run_outcome_storages;
    RunOutcome* current_run_outcome = nullptr;
    std::unique_ptr<RunOutcome> reset_failure_outcome_storage;
    bool run_outcome_frozen = false;
    bool resources_disposed = false;
    RuntimeDiagnosticStage current_diagnostic_stage =
        RuntimeDiagnosticStage::Lifecycle;
    SessionBoundarySummary boundary_summary;
    SessionStepSummary step_summary;
    std::vector<Arena> arenas;
    std::vector<OwnedObject> preparations;
    SessionRuntimeBindings runtime_bindings;
    CommittedStateStore committed_state_store;
    TransactionCandidateStore candidate_state_store;
    CycleFrame cycle_frame;
    std::vector<EvaluatorHistoryStore> evaluator_histories;
    std::vector<EvaluatorHistoryStore> staged_evaluator_histories;
    SealedBoundaryStore sealed_boundary;
    SealedBoundaryStore staged_sealed_boundary;
    std::vector<ScheduledCall> opening_schedule;
    std::uint64_t committed_epoch = 0U;
    std::int64_t committed_tick = 0;
    std::uint64_t committed_step_count = 0U;
    std::uint32_t active_transaction_handle = 0U;

    [[nodiscard]] SessionResult failure(SessionError error,
                                        std::uint32_t handle,
                                        std::string_view detail) noexcept {
        last_result = {error, handle, detail};
        return last_result;
    }

    void prepare_outcome_identity(RunOutcome& outcome) const {
        outcome.image_fingerprint = image->fingerprint();
        outcome.plan_id = image->plan_id();
        outcome.mission_id = image->mission_id();
        outcome.source_semantic_hash = image->source_semantic_hash();
        outcome.descriptor_semantic_hash = image->descriptor_semantic_hash();
        outcome.initial_tick = image->clock().initial_tick;
        outcome.final_tick = image->clock().initial_tick;
    }

    void prepare_run_start(RunOutcome& outcome, const RunId& run_id,
                           std::uint64_t run_sequence,
                           RunStartKind run_start_kind,
                           bool run_start_committed,
                           std::uint64_t initial_epoch) const noexcept {
        outcome.run_id = run_id;
        outcome.run_sequence = run_sequence;
        outcome.run_start_kind = run_start_kind;
        outcome.run_start_committed = run_start_committed;
        outcome.final_status = RunFinalStatus::Failed;
        outcome.validity = contracts::EvidenceValidity::Unknown;
        outcome.initial_tick = image->clock().initial_tick;
        outcome.final_tick = image->clock().initial_tick;
        outcome.initial_committed_epoch = initial_epoch;
        outcome.final_committed_epoch = initial_epoch;
        outcome.committed_step_count = 0U;
        outcome.terminal_branch_committed = false;
        outcome.mission_result_available = false;
        outcome.primary_diagnostic.reset();
        outcome.related_diagnostics.clear();
        outcome.finalization_status = RunFinalizationStatus::NotStarted;
    }

    [[nodiscard]] bool run_id_seen(const RunId& run_id) const noexcept {
        return std::any_of(
            run_outcome_storages.begin(), run_outcome_storages.end(),
            [&run_id](const auto& outcome) {
                return outcome != nullptr && outcome->run_id == run_id;
            });
    }

    [[nodiscard]] RuntimeDiagnostic make_diagnostic(
        SessionResult cause, RuntimeDiagnosticStage stage,
        contracts::EvidenceValidity validity) const noexcept {
        RuntimeDiagnostic result;
        result.code = diagnostic_code_for(cause.error);
        if (stage == RuntimeDiagnosticStage::ResetRequest &&
            (cause.error == SessionError::EmptyRunId ||
             cause.error == SessionError::DuplicateRunId)) {
            result.code = RuntimeDiagnosticCode::ResetRequestInvalid;
        }
        result.stage = stage;
        result.subject_handle = cause.image_handle;
        if (pending_reset_attempt.has_value()) {
            result.run_id = pending_reset_attempt->run_id;
        } else if (pending_run_attempt.has_value()) {
            result.run_id = pending_run_attempt->run_id;
        } else if (committed_run_id.has_value()) {
            result.run_id = *committed_run_id;
        } else if (current_run_outcome != nullptr) {
            result.run_id = current_run_outcome->run_id;
        }
        result.tick = committed_tick;
        result.base_epoch = committed_epoch;
        result.cause_code = cause.error;
        result.cause_ref = cause.image_handle;
        result.validity_effect = validity;
        result.disposition = RuntimeFailureDisposition::FailOperation;
        result.message_key = message_key_for(result.code);
        result.detail = cause.detail;
        return result;
    }

    void freeze_failed_run(const RuntimeDiagnostic& diagnostic) noexcept {
        if (run_outcome_frozen || current_run_outcome == nullptr) return;
        auto& outcome = *current_run_outcome;
        if (committed_run_id.has_value()) {
            outcome.run_id = *committed_run_id;
        } else if (pending_run_attempt.has_value()) {
            outcome.run_id = pending_run_attempt->run_id;
            outcome.run_start_kind = RunStartKind::Initialize;
            outcome.run_start_committed = false;
        }
        outcome.run_sequence = committed_run_sequence;
        outcome.final_status = RunFinalStatus::Failed;
        outcome.validity = diagnostic.validity_effect;
        outcome.final_tick = committed_tick;
        outcome.final_committed_epoch = committed_epoch;
        outcome.committed_step_count = committed_step_count;
        outcome.terminal_branch_committed = false;
        outcome.mission_result_available = false;
        outcome.primary_diagnostic = diagnostic;
        outcome.related_diagnostics.clear();
        outcome.finalization_status =
            outcome.run_start_committed
                ? RunFinalizationStatus::Succeeded
                : RunFinalizationStatus::NotStarted;
        run_outcome_frozen = true;
    }

    void freeze_completed_run() noexcept {
        if (run_outcome_frozen || current_run_outcome == nullptr) return;
        auto& outcome = *current_run_outcome;
        if (committed_run_id.has_value()) {
            outcome.run_id = *committed_run_id;
        }
        outcome.run_sequence = committed_run_sequence;
        outcome.run_start_committed = true;
        outcome.final_status = RunFinalStatus::Completed;
        outcome.validity =
            contracts::EvidenceValidity::Valid;
        outcome.final_tick = committed_tick;
        outcome.final_committed_epoch = committed_epoch;
        outcome.committed_step_count = committed_step_count;
        outcome.terminal_branch_committed = true;
        outcome.mission_result_available =
            step_summary.result_seal_staged &&
            step_summary.terminal_result_present;
        outcome.primary_diagnostic.reset();
        outcome.related_diagnostics.clear();
        // The current Image declares no run-scoped finalization hooks. Its
        // empty finalization set is therefore a completed finalization.
        outcome.finalization_status =
            RunFinalizationStatus::Succeeded;
        run_outcome_frozen = true;
    }

    [[nodiscard]] StepOutcome make_step_outcome(
        StepStatus status, SessionResult result,
        std::optional<RuntimeDiagnostic> diagnostic = {}) const noexcept {
        StepOutcome outcome;
        outcome.status = status;
        outcome.result = result;
        if (committed_run_id.has_value()) {
            outcome.run_id = *committed_run_id;
        }
        outcome.run_sequence = committed_run_sequence;
        outcome.branch_selected = step_summary.branch_selected;
        outcome.transaction_handle = step_summary.transaction_handle;
        outcome.branch = step_summary.branch;
        outcome.base_epoch = step_summary.base_epoch;
        outcome.committed_epoch = step_summary.committed_epoch;
        outcome.tick_before = step_summary.base_tick;
        outcome.tick_after = step_summary.committed_tick;
        outcome.candidates.planned_count =
            image->transactions().empty()
                ? 0U
                : image->transactions().front().candidates.size();
        for (const auto& candidate : step_summary.candidates) {
            outcome.candidates.present_count +=
                static_cast<std::size_t>(candidate.present);
            outcome.candidates.valid_count +=
                static_cast<std::size_t>(candidate.present &&
                                         candidate.valid);
        }
        outcome.histories.staged = step_summary.history_staged;
        outcome.histories.history_count = step_summary.histories.size();
        for (const auto& history : step_summary.histories) {
            outcome.histories.prospective_sample_count =
                (std::max)(outcome.histories.prospective_sample_count,
                           history.prospective_sample_count);
        }
        outcome.observation_seal.staged =
            step_summary.observation_seal_staged;
        outcome.observation_seal.output_count =
            static_cast<std::size_t>(std::count_if(
                step_summary.seals.begin(), step_summary.seals.end(),
                [](const auto& seal) { return !seal.terminal_result; }));
        outcome.result_seal.staged = step_summary.result_seal_staged;
        outcome.result_seal.result_present =
            step_summary.terminal_result_present;
        if (!step_summary.executed_callsite_handles.empty()) {
            outcome.last_callsite_handle =
                step_summary.executed_callsite_handles.back();
            for (const auto& region : image->regions()) {
                if (std::find(region.callsite_handles.begin(),
                              region.callsite_handles.end(),
                              outcome.last_callsite_handle) !=
                    region.callsite_handles.end()) {
                    outcome.last_region_handle = region.handle;
                }
            }
        }
        outcome.last_image_handle =
            result.image_handle != 0U
                ? result.image_handle
                : (outcome.last_callsite_handle != 0U
                       ? outcome.last_callsite_handle
                       : outcome.transaction_handle);
        outcome.primary_diagnostic = std::move(diagnostic);
        return outcome;
    }

    [[nodiscard]] StepOutcome make_lifecycle_rejection_outcome(
        SessionResult result, RuntimeDiagnostic diagnostic) const noexcept {
        StepOutcome outcome;
        outcome.status = StepStatus::Failed;
        outcome.result = result;
        if (committed_run_id.has_value()) {
            outcome.run_id = *committed_run_id;
        } else if (current_run_outcome != nullptr) {
            outcome.run_id = current_run_outcome->run_id;
        }
        outcome.run_sequence = committed_run_sequence;
        outcome.base_epoch = committed_epoch;
        outcome.committed_epoch = committed_epoch;
        outcome.tick_before = committed_tick;
        outcome.tick_after = committed_tick;
        outcome.primary_diagnostic = std::move(diagnostic);
        return outcome;
    }

    [[nodiscard]] InitializationOutcome fail_initialization(
        SessionResult cause, RuntimeDiagnosticStage stage) noexcept {
        last_result = cause;
        unwind();
        state = SessionState::Failed;
        has_committed_run = false;
        committed_run_id.reset();
        committed_run_binding.reset();
        const auto diagnostic = make_diagnostic(
            cause, stage, contracts::EvidenceValidity::Unknown);
        initialization_outcome = {};
        initialization_outcome.status = InitializationStatus::Failed;
        initialization_outcome.result = cause;
        if (pending_run_attempt.has_value()) {
            initialization_outcome.run_id =
                pending_run_attempt->run_id;
        }
        initialization_outcome.proposed_run_sequence = 0U;
        initialization_outcome.binding_matched =
            pending_run_attempt.has_value() &&
            binding_matches_image(pending_run_attempt->binding, *image);
        initialization_outcome.initialization_commit = false;
        initialization_outcome.committed_epoch = committed_epoch;
        initialization_outcome.committed_tick = committed_tick;
        initialization_outcome.primary_diagnostic = diagnostic;
        freeze_failed_run(diagnostic);
        pending_run_attempt.reset();
        return initialization_outcome;
    }

    [[nodiscard]] ResetOutcome fail_reset(
        SessionResult cause, RuntimeDiagnosticStage stage) noexcept {
        last_result = cause;
        const auto diagnostic = make_diagnostic(
            cause, stage, contracts::EvidenceValidity::Unknown);
        reset_outcome = {};
        reset_outcome.status = ResetStatus::Failed;
        reset_outcome.result = cause;
        if (pending_reset_attempt.has_value()) {
            reset_outcome.run_id = pending_reset_attempt->run_id;
            reset_outcome.binding_matched = binding_matches_image(
                pending_reset_attempt->binding, *image);
        }
        reset_outcome.proposed_run_sequence =
            pending_reset_run_sequence;
        reset_outcome.reset_commit = false;
        reset_outcome.committed_epoch = committed_epoch;
        reset_outcome.committed_tick = committed_tick;
        reset_outcome.primary_diagnostic = diagnostic;

        if (reset_failure_outcome_storage != nullptr &&
            run_outcome_storages.size() <
                run_outcome_storages.capacity()) {
            auto& outcome = *reset_failure_outcome_storage;
            prepare_run_start(
                outcome,
                pending_reset_attempt.has_value()
                    ? pending_reset_attempt->run_id
                    : RunId{},
                pending_reset_run_sequence, RunStartKind::Reset, false,
                committed_epoch);
            outcome.final_status = RunFinalStatus::Failed;
            outcome.validity = contracts::EvidenceValidity::Unknown;
            outcome.final_tick = committed_tick;
            outcome.final_committed_epoch = committed_epoch;
            outcome.primary_diagnostic = diagnostic;
            outcome.finalization_status =
                RunFinalizationStatus::NotStarted;
            current_run_outcome = reset_failure_outcome_storage.get();
            run_outcome_storages.push_back(
                std::move(reset_failure_outcome_storage));
            run_outcome_frozen = true;
        }
        state = SessionState::Failed;
        pending_reset_attempt.reset();
        return reset_outcome;
    }

    [[nodiscard]] const contracts::PlanImageInitialBinding*
    initial_for_state(const contracts::PlanImageStateBlock& block)
        const noexcept {
        const auto found = std::find_if(
            image->initial_bindings().begin(),
            image->initial_bindings().end(), [&block](const auto& binding) {
                return binding.committed_state_slot_handle ==
                       block.committed_slot_handle;
            });
        return found == image->initial_bindings().end() ? nullptr : &*found;
    }

    [[nodiscard]] const contracts::PlanImageStateBlock* state_for_slot(
        std::uint32_t slot_handle) const noexcept {
        const auto found = std::find_if(
            image->state_blocks().begin(), image->state_blocks().end(),
            [slot_handle](const auto& block) {
                return block.committed_slot_handle == slot_handle ||
                       block.candidate_slot_handle == slot_handle;
            });
        return found == image->state_blocks().end() ? nullptr : &*found;
    }

    [[nodiscard]] std::uint32_t owner_component_for_occurrence(
        std::uint32_t occurrence_handle) const noexcept {
        std::uint32_t result = 0U;
        for (const auto& component : image->runtime_components()) {
            if (component.occurrence_handle == occurrence_handle) {
                if (result != 0U) {
                    return 0U;
                }
                result = component.handle;
            }
        }
        return result;
    }

    [[nodiscard]] const contracts::PlanImageRuntimeComponent*
    component_for_callsite(std::uint32_t callsite_handle) const noexcept {
        const contracts::PlanImageRuntimeComponent* result = nullptr;
        for (const auto& component : image->runtime_components()) {
            if (std::find(component.callsite_handles.begin(),
                          component.callsite_handles.end(),
                          callsite_handle) !=
                component.callsite_handles.end()) {
                if (result != nullptr) {
                    return nullptr;
                }
                result = &component;
            }
        }
        return result;
    }

    [[nodiscard]] const contracts::PlanImageEntry* entry_for_callsite(
        const contracts::PlanImageCallsite& callsite) const noexcept {
        return find_handle(image->entries(), callsite.entry_handle);
    }

    [[nodiscard]] Arena* arena(std::uint32_t handle) noexcept {
        const auto found = std::find_if(
            arenas.begin(), arenas.end(), [handle](const auto& candidate) {
                return candidate.layout != nullptr &&
                       candidate.layout->handle == handle;
            });
        return found == arenas.end() ? nullptr : &*found;
    }

    [[nodiscard]] void* slot_address(
        const contracts::PlanImageSlot& slot) noexcept {
        auto* owner = arena(slot.storage_layout_handle);
        if (owner == nullptr || owner->address == nullptr) {
            return nullptr;
        }
        return static_cast<void*>(
            static_cast<std::byte*>(owner->address) + slot.offset_bytes);
    }

    [[nodiscard]] const OwnedObject* owned_object(
        const std::vector<OwnedObject>& values,
        std::uint32_t handle) const noexcept {
        const auto found = std::find_if(
            values.begin(), values.end(), [handle](const auto& value) {
                return value.handle == handle;
            });
        return found == values.end() ? nullptr : &*found;
    }

    [[nodiscard]] SessionObjectIdentityView object_view(
        const OwnedObject& object) const noexcept {
        const auto layout = object.materializer->operations().layout();
        return {object.address,
                layout.size_bytes,
                layout.alignment_bytes,
                layout.type_identity,
                object.role,
                object.handle,
                object.linked_entry_handle,
                layout.codec_entry_handle};
    }

    [[nodiscard]] SessionObjectIdentityView prepared_object(
        std::uint32_t handle) const noexcept override {
        const auto* object = owned_object(preparations, handle);
        return object == nullptr ? SessionObjectIdentityView{}
                                 : object_view(*object);
    }

    [[nodiscard]] const StateObject* committed_for_slot(
        std::uint32_t slot_handle) const noexcept {
        const auto found = std::find_if(
            committed_state_store.blocks.begin(),
            committed_state_store.blocks.end(),
            [slot_handle](const auto& committed) {
                return committed.block != nullptr &&
                       committed.block->committed_slot_handle == slot_handle;
            });
        return found == committed_state_store.blocks.end() ? nullptr
                                                            : &*found;
    }

    [[nodiscard]] const EvaluatorHistoryStore* history_store(
        const std::vector<EvaluatorHistoryStore>& stores,
        std::uint32_t history_handle) const noexcept {
        const auto found = std::find_if(
            stores.begin(), stores.end(),
            [history_handle](const auto& store) {
                return store.plan != nullptr &&
                       store.plan->handle == history_handle;
            });
        return found == stores.end() ? nullptr : &*found;
    }

    [[nodiscard]] SessionResult clone_stored_value(
        const contracts::PlanImageSlot& slot,
        const SessionObjectMaterializer& materializer, const void* source,
        std::uint64_t generation, std::uint64_t sequence,
        std::int64_t sample_tick, double sample_time_seconds,
        double interval_start_seconds, double interval_end_seconds,
        contracts::DataQuality quality, StoredValue& destination,
        SessionError error, std::string_view detail) noexcept {
        const auto layout = materializer.operations().layout();
        if (source == nullptr || layout.size_bytes != slot.size_bytes ||
            layout.alignment_bytes != slot.alignment_bytes ||
            layout.layout_identity != slot.layout_id ||
            layout.codec_entry_handle != slot.codec_entry_handle ||
            layout.type_identity == nullptr ||
            !materializer.operations().validate(source)) {
            return failure(error, slot.handle, detail);
        }
        RawBlock allocation(layout.size_bytes, layout.alignment_bytes);
        if (allocation.get() == nullptr) {
            return failure(SessionError::AllocationFailure, slot.handle,
                           "transactional clone allocation failed");
        }
        if (!materializer.operations().copy_construct(source,
                                                      allocation.get())) {
            return failure(error, slot.handle, detail);
        }
        ConstructedObject guard(allocation.get(), materializer.operations());
        if (!materializer.operations().validate(allocation.get())) {
            return failure(error, slot.handle, detail);
        }
        StoredValue staged;
        staged.slot = &slot;
        staged.materializer = &materializer;
        staged.address = allocation.get();
        staged.alignment = layout.alignment_bytes;
        staged.generation = generation;
        staged.sequence = sequence;
        staged.sample_tick = sample_tick;
        staged.sample_time_seconds = sample_time_seconds;
        staged.interval_start_seconds = interval_start_seconds;
        staged.interval_end_seconds = interval_end_seconds;
        staged.quality = quality;
        guard.release();
        static_cast<void>(allocation.release());
        destination = std::move(staged);
        return {};
    }

    class ScopedObjectAccess final : public SessionObjectAccess {
      public:
        ScopedObjectAccess(
            const Impl& owner,
            const std::vector<std::uint32_t>* allowed_handles) noexcept
            : owner_(&owner), allowed_handles_(allowed_handles) {}

        [[nodiscard]] SessionObjectIdentityView prepared_object(
            std::uint32_t handle) const noexcept override {
            if (owner_ == nullptr || allowed_handles_ == nullptr ||
                std::find(allowed_handles_->begin(), allowed_handles_->end(),
                          handle) == allowed_handles_->end()) {
                return {};
            }
            return owner_->prepared_object(handle);
        }

      private:
        const Impl* owner_ = nullptr;
        const std::vector<std::uint32_t>* allowed_handles_ = nullptr;
    };

    [[nodiscard]] bool valid_materializer_identity(
        const SessionObjectMaterializer& materializer,
        std::uint32_t handle, SessionObjectRole role,
        std::uint32_t entry_handle,
        std::uint32_t codec_handle) const noexcept {
        const auto identity = materializer.identity();
        return identity.image_object_handle == handle &&
               identity.role == role &&
               identity.linked_entry_handle == entry_handle &&
               identity.codec_entry_handle == codec_handle;
    }

    [[nodiscard]] SessionResult validate_materializers() {
        for (const auto& preparation : image->preparations()) {
            const auto* materializer = provider->preparation(preparation.handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               preparation.handle,
                               "preparation materializer is missing");
            }
            if (!valid_materializer_identity(
                    *materializer, preparation.handle,
                    SessionObjectRole::PreparedModel,
                    preparation.prepare_entry_handle, 0U)) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               preparation.handle,
                               "preparation materializer identity mismatch");
            }
            const auto layout = materializer->operations().layout();
            if (layout.size_bytes == 0U ||
                !valid_alignment(layout.alignment_bytes) ||
                layout.type_identity == nullptr) {
                return failure(SessionError::InvalidStorageLayout,
                               preparation.handle,
                               "preparation object layout is invalid");
            }
            if (materializer->dependency_count() != 0U) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               preparation.handle,
                               "preparation materializer declared a dependency");
            }
        }

        for (const auto& component : image->runtime_components()) {
            const auto* materializer = provider->runtime_component(
                component.handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               component.handle,
                               "Runtime Cell materializer is missing");
            }
            if (!valid_materializer_identity(
                    *materializer, component.handle,
                    SessionObjectRole::RuntimeCell,
                    component.runtime_cell_factory_entry_handle, 0U)) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               component.handle,
                               "Runtime Cell materializer identity mismatch");
            }
            const auto layout = materializer->operations().layout();
            if (layout.size_bytes == 0U ||
                !valid_alignment(layout.alignment_bytes) ||
                layout.type_identity == nullptr) {
                return failure(SessionError::InvalidStorageLayout,
                               component.handle,
                               "Runtime Cell object layout is invalid");
            }
            if (materializer->dependency_count() !=
                    component.preparation_handles.size() ||
                !unique_nonzero_handles(component.preparation_handles)) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               component.handle,
                               "Runtime Cell dependency closure is incomplete");
            }
            std::vector<std::uint32_t> dependency_handles;
            dependency_handles.reserve(materializer->dependency_count());
            for (std::size_t index = 0U;
                 index < materializer->dependency_count(); ++index) {
                const auto expected = materializer->dependency(index);
                dependency_handles.push_back(expected.image_object_handle);
                const auto* preparation = find_handle(
                    image->preparations(), expected.image_object_handle);
                const auto* dependency_materializer = provider->preparation(
                    expected.image_object_handle);
                const auto membership = static_cast<std::size_t>(std::count(
                    component.preparation_handles.begin(),
                    component.preparation_handles.end(),
                    expected.image_object_handle));
                if (preparation == nullptr ||
                    dependency_materializer == nullptr || membership != 1U ||
                    expected.role != SessionObjectRole::PreparedModel ||
                    expected.linked_entry_handle !=
                        preparation->prepare_entry_handle ||
                    expected.codec_entry_handle != 0U ||
                    expected.size_bytes == 0U ||
                    !valid_alignment(expected.alignment_bytes) ||
                    expected.type_identity == nullptr) {
                    return failure(
                        SessionError::InvalidMaterializerIdentity,
                        component.handle,
                        "Runtime Cell dependency identity is invalid");
                }
                const auto dependency_layout =
                    dependency_materializer->operations().layout();
                if (dependency_layout.size_bytes != expected.size_bytes ||
                    dependency_layout.alignment_bytes !=
                        expected.alignment_bytes ||
                    dependency_layout.type_identity !=
                        expected.type_identity) {
                    return failure(SessionError::ObjectTypeMismatch,
                                   component.handle,
                                   "Runtime Cell dependency type is invalid");
                }
            }
            if (!unique_nonzero_handles(dependency_handles) ||
                dependency_handles.size() !=
                    component.preparation_handles.size() ||
                !std::all_of(
                    component.preparation_handles.begin(),
                    component.preparation_handles.end(),
                    [&dependency_handles](std::uint32_t handle) {
                        return std::find(dependency_handles.begin(),
                                         dependency_handles.end(), handle) !=
                               dependency_handles.end();
                    })) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               component.handle,
                               "Runtime Cell dependency set is not exact");
            }
        }

        for (const auto& binding : image->initial_bindings()) {
            const auto block = std::find_if(
                image->state_blocks().begin(), image->state_blocks().end(),
                [&binding](const auto& candidate) {
                    return candidate.committed_slot_handle ==
                           binding.committed_state_slot_handle;
                });
            if (block == image->state_blocks().end()) {
                return failure(SessionError::InvalidImageStructure,
                               binding.handle,
                               "initial binding has no state block");
            }
            const auto* materializer = provider->initial_state(binding.handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               binding.handle,
                               "initial-state materializer is missing");
            }
            if (!valid_materializer_identity(
                    *materializer, binding.handle,
                    SessionObjectRole::InitialStateValue,
                    binding.builder_entry_handle,
                    block->codec_entry_handle)) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               binding.handle,
                               "initial-state materializer identity mismatch");
            }
            SessionError mismatch = SessionError::None;
            if (!same_layout(materializer->operations().layout(),
                             block->size_bytes, block->alignment_bytes,
                             block->layout_id, block->codec_entry_handle,
                             mismatch)) {
                return failure(mismatch, block->handle,
                               "state metadata and typed adapter disagree");
            }
            if (!materializer->operations().supports_nofail_swap()) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               block->handle,
                               "state codec lacks a no-fail commit swap");
            }
        }

        for (const auto& slot : image->slots()) {
            if (state_for_slot(slot.handle) != nullptr) {
                continue;
            }
            const auto* materializer = provider->slot(slot.handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, slot.handle,
                               "slot materializer is missing");
            }
            if (!valid_materializer_identity(
                    *materializer, slot.handle, role_for_slot(slot), 0U,
                    slot.codec_entry_handle)) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               slot.handle,
                               "slot materializer identity mismatch");
            }
            SessionError mismatch = SessionError::None;
            if (!same_layout(materializer->operations().layout(),
                             slot.size_bytes, slot.alignment_bytes,
                             slot.layout_id, slot.codec_entry_handle,
                             mismatch)) {
                return failure(mismatch, slot.handle,
                               "slot metadata and typed adapter disagree");
            }
        }
        for (const auto& callsite : image->callsites()) {
            const auto* linked_entry = entry_for_callsite(callsite);
            if (linked_entry == nullptr ||
                (linked_entry->kind !=
                     contracts::PlanImageEntryKind::PublishProjection &&
                 linked_entry->kind !=
                     contracts::PlanImageEntryKind::BoundaryEvaluation &&
                 linked_entry->kind !=
                     contracts::PlanImageEntryKind::IntervalEvolution)) {
                continue;
            }
            const auto* component = component_for_callsite(callsite.handle);
            const auto* invocation = provider->invocation(callsite.handle);
            if (component == nullptr) {
                return failure(SessionError::InvalidImageStructure,
                               callsite.handle,
                               "callsite Runtime Cell owner is invalid");
            }
            if (invocation == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               callsite.handle,
                               "callsite invocation entry is missing");
            }
            const auto identity = invocation->identity();
            if (identity.callsite_handle != callsite.handle ||
                identity.runtime_component_handle != component->handle ||
                identity.linked_entry_handle != callsite.entry_handle) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               callsite.handle,
                               "callsite invocation identity mismatch");
            }
        }
        for (const auto& scope : image->integration_scopes()) {
            const auto component_handle = owner_component_for_occurrence(
                scope.owner_occurrence_handle);
            const auto* integration = provider->integration(scope.handle);
            if (component_handle == 0U) {
                return failure(SessionError::InvalidImageStructure,
                               scope.handle,
                               "IntegrationScope owner is invalid");
            }
            if (integration == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               scope.handle,
                               "IntegrationScope entry is missing");
            }
            const auto identity = integration->identity();
            if (identity.integration_scope_handle != scope.handle ||
                identity.runtime_component_handle != component_handle) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               scope.handle,
                               "IntegrationScope entry identity mismatch");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_lifecycle() {
        const auto& lifecycle = image->lifecycle();
        if (!exact_handle_membership(lifecycle.preparation_handles,
                                     image->preparations()) ||
            !exact_handle_membership(lifecycle.runtime_component_handles,
                                     image->runtime_components()) ||
            !exact_handle_membership(lifecycle.initial_binding_handles,
                                     image->initial_bindings())) {
            return failure(SessionError::InvalidImageStructure, 0U,
                           "lifecycle construction membership is incomplete");
        }
        if (lifecycle.runtime_component_dispose_handles !=
                std::vector<std::uint32_t>(
                    lifecycle.runtime_component_handles.rbegin(),
                    lifecycle.runtime_component_handles.rend()) ||
            lifecycle.preparation_dispose_handles !=
                std::vector<std::uint32_t>(
                    lifecycle.preparation_handles.rbegin(),
                    lifecycle.preparation_handles.rend())) {
            return failure(SessionError::InvalidImageStructure, 0U,
                           "lifecycle disposal order is invalid");
        }
        const auto& disposed = lifecycle.bound_provider_dispose_handles;
        if (disposed.size() != image->queries().size() +
                                   image->closures().size() ||
            !unique_nonzero_handles(disposed)) {
            return failure(SessionError::InvalidImageStructure, 0U,
                           "provider disposal membership is incomplete");
        }
        for (const auto handle : disposed) {
            if (find_handle(image->queries(), handle) == nullptr &&
                find_handle(image->closures(), handle) == nullptr) {
                return failure(SessionError::InvalidImageHandle, handle,
                               "provider disposal handle is unknown");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_storage() noexcept {
        for (const auto& layout : image->storage_layouts()) {
            if (layout.size_bytes == 0U ||
                !valid_alignment(layout.alignment_bytes) ||
                !unique_nonzero_handles(layout.ordered_slot_handles)) {
                return failure(SessionError::InvalidStorageLayout,
                               layout.handle,
                               "storage layout metadata is invalid");
            }
            std::size_t matching_slots = 0U;
            for (const auto& slot : image->slots()) {
                if (slot.storage_layout_handle == layout.handle) {
                    ++matching_slots;
                }
            }
            if (matching_slots != layout.ordered_slot_handles.size()) {
                return failure(SessionError::InvalidImageStructure,
                               layout.handle,
                               "storage membership is incomplete");
            }
            for (const auto handle : layout.ordered_slot_handles) {
                const auto* member = find_handle(image->slots(), handle);
                if (member == nullptr ||
                    member->storage_layout_handle != layout.handle) {
                    return failure(SessionError::InvalidImageStructure, handle,
                                   "storage membership cross-reference failed");
                }
            }
        }

        for (const auto& slot : image->slots()) {
            const auto* storage = find_handle(image->storage_layouts(),
                                              slot.storage_layout_handle);
            if (storage == nullptr ||
                storage->storage_class != slot.storage_class) {
                return failure(SessionError::InvalidImageHandle, slot.handle,
                               "slot storage owner is invalid");
            }
            const auto membership = static_cast<std::size_t>(std::count(
                storage->ordered_slot_handles.begin(),
                storage->ordered_slot_handles.end(), slot.handle));
            if (membership != 1U) {
                return failure(SessionError::InvalidImageStructure,
                               slot.handle,
                               "slot storage membership is not singular");
            }
            if (slot.size_bytes == 0U ||
                slot.offset_bytes > storage->size_bytes ||
                slot.size_bytes > storage->size_bytes - slot.offset_bytes) {
                return failure(SessionError::StorageBoundsViolation,
                               slot.handle,
                               "slot exceeds its storage extent");
            }
            if (!valid_alignment(slot.alignment_bytes) ||
                slot.alignment_bytes > storage->alignment_bytes ||
                slot.offset_bytes % slot.alignment_bytes != 0U ||
                !valid_slot_storage_pair(slot)) {
                return failure(SessionError::InvalidStorageLayout,
                               slot.handle,
                               "slot alignment, kind, or lifetime is invalid");
            }
            for (const auto& other : image->slots()) {
                if (other.handle <= slot.handle ||
                    other.storage_layout_handle != slot.storage_layout_handle) {
                    continue;
                }
                const auto slot_end = slot.offset_bytes + slot.size_bytes;
                const auto other_end = other.offset_bytes + other.size_bytes;
                if (slot.offset_bytes < other_end &&
                    other.offset_bytes < slot_end) {
                    return failure(SessionError::StorageOverlap, other.handle,
                                   "slot storage ranges overlap");
                }
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_states() noexcept {
        for (const auto& block : image->state_blocks()) {
            const auto* committed = find_handle(image->slots(),
                                                block.committed_slot_handle);
            const auto* candidate = find_handle(image->slots(),
                                                block.candidate_slot_handle);
            const auto* initial = initial_for_state(block);
            const auto owner = owner_component_for_occurrence(
                block.owner_occurrence_handle);
            const auto initial_count = static_cast<std::size_t>(std::count_if(
                image->initial_bindings().begin(),
                image->initial_bindings().end(), [&block](const auto& value) {
                    return value.committed_state_slot_handle ==
                           block.committed_slot_handle;
                }));
            const auto committed_owner_count =
                static_cast<std::size_t>(std::count_if(
                    image->state_blocks().begin(),
                    image->state_blocks().end(), [&block](const auto& value) {
                        return value.committed_slot_handle ==
                               block.committed_slot_handle;
                    }));
            const auto candidate_owner_count =
                static_cast<std::size_t>(std::count_if(
                    image->state_blocks().begin(),
                    image->state_blocks().end(), [&block](const auto& value) {
                        return value.candidate_slot_handle ==
                               block.candidate_slot_handle;
                    }));
            if (committed == nullptr || candidate == nullptr ||
                initial == nullptr || owner == 0U ||
                block.committed_slot_handle == block.candidate_slot_handle ||
                initial_count != 1U || committed_owner_count != 1U ||
                candidate_owner_count != 1U ||
                committed->kind !=
                    contracts::PlanImageSlotKind::CommittedState ||
                candidate->kind !=
                    contracts::PlanImageSlotKind::CandidateState ||
                committed->owner_occurrence_handle !=
                    block.owner_occurrence_handle ||
                candidate->owner_occurrence_handle !=
                    block.owner_occurrence_handle ||
                initial->owner_occurrence_handle !=
                    block.owner_occurrence_handle) {
                return failure(SessionError::InvalidImageStructure,
                               block.handle,
                               "state ownership or binding is incomplete");
            }
            if (committed->size_bytes != block.size_bytes ||
                candidate->size_bytes != block.size_bytes) {
                return failure(SessionError::ObjectSizeMismatch, block.handle,
                               "state block size disagrees with its slots");
            }
            if (committed->alignment_bytes != block.alignment_bytes ||
                candidate->alignment_bytes != block.alignment_bytes) {
                return failure(SessionError::ObjectAlignmentMismatch,
                               block.handle,
                               "state block alignment disagrees with its slots");
            }
            if (committed->layout_id != block.layout_id ||
                candidate->layout_id != block.layout_id) {
                return failure(SessionError::ObjectLayoutMismatch,
                               block.handle,
                               "state block layout disagrees with its slots");
            }
            if (committed->codec_entry_handle != block.codec_entry_handle ||
                candidate->codec_entry_handle != block.codec_entry_handle) {
                return failure(SessionError::ObjectCodecMismatch,
                               block.handle,
                               "state block codec disagrees with its slots");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_histories() {
        for (const auto& history : image->evaluator_histories()) {
            const auto* callsite = find_handle(
                image->callsites(), history.evaluator_callsite_handle);
            const auto* entry = callsite == nullptr
                                    ? nullptr
                                    : entry_for_callsite(*callsite);
            const auto* component = callsite == nullptr
                                        ? nullptr
                                        : component_for_callsite(
                                              callsite->handle);
            if (history.history_depth == 0U ||
                history.ordered_members.empty() || callsite == nullptr ||
                entry == nullptr || component == nullptr ||
                entry->kind !=
                    contracts::PlanImageEntryKind::BoundaryEvaluation ||
                std::count(component->evaluator_history_handles.begin(),
                           component->evaluator_history_handles.end(),
                           history.handle) != 1) {
                return failure(SessionError::InvalidImageStructure,
                               history.handle,
                               "evaluator history authority is invalid");
            }
            std::vector<std::uint32_t> ordered_slots;
            ordered_slots.reserve(history.ordered_members.size());
            for (std::size_t index = 0U;
                 index < history.ordered_members.size(); ++index) {
                const auto& member = history.ordered_members[index];
                const auto* block = state_for_slot(
                    member.committed_state_slot_handle);
                const auto prior_member = std::find_if(
                    history.ordered_members.begin(),
                    history.ordered_members.begin() +
                        static_cast<std::ptrdiff_t>(index),
                    [&member](const auto& prior) {
                        return prior.member_id == member.member_id ||
                               prior.committed_state_slot_handle ==
                                   member.committed_state_slot_handle;
                    });
                if (member.member_id.empty() || block == nullptr ||
                    block->committed_slot_handle !=
                        member.committed_state_slot_handle ||
                    block->schema_id != member.state_schema_id ||
                    block->layout_id != member.state_layout_id ||
                    prior_member != history.ordered_members.begin() +
                                        static_cast<std::ptrdiff_t>(index)) {
                    return failure(
                        SessionError::InvalidImageStructure, history.handle,
                        "evaluator history member order or type is invalid");
                }
                ordered_slots.push_back(member.committed_state_slot_handle);
            }
            if (callsite->input_slot_handles != ordered_slots) {
                return failure(
                    SessionError::InvalidImageStructure, history.handle,
                    "evaluator history input order is not exact");
            }
        }
        for (const auto& component : image->runtime_components()) {
            for (const auto handle : component.evaluator_history_handles) {
                const auto* history = find_handle(
                    image->evaluator_histories(), handle);
                if (history == nullptr ||
                    component_for_callsite(
                        history->evaluator_callsite_handle) != &component) {
                    return failure(SessionError::InvalidImageStructure,
                                   handle,
                                   "Runtime Cell evaluator history is invalid");
                }
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_transactions() {
        if (image->transactions().size() != 1U) {
            return failure(SessionError::InvalidImageStructure, 0U,
                           "Session slice requires one transaction");
        }
        const auto& transaction = image->transactions().front();
        if (transaction.candidates.empty() ||
            !unique_nonzero_handles(transaction.held_slot_handles)) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "transaction candidates or held slots are invalid");
        }
        std::vector<std::uint32_t> candidate_slots;
        std::vector<std::uint32_t> candidate_tokens;
        candidate_slots.reserve(transaction.candidates.size());
        candidate_tokens.reserve(transaction.candidates.size());
        for (const auto& candidate : transaction.candidates) {
            candidate_slots.push_back(candidate.candidate_state_slot_handle);
            candidate_tokens.push_back(candidate.writer_token_handle);
            const auto* block = state_for_slot(
                candidate.candidate_state_slot_handle);
            const auto* token = find_handle(image->writer_tokens(),
                                            candidate.writer_token_handle);
            bool producer_valid = false;
            if (candidate.producer_kind == "IntegrationScope") {
                const auto* scope = find_handle(image->integration_scopes(),
                                                candidate.producer_handle);
                producer_valid =
                    scope != nullptr && block != nullptr &&
                    scope->candidate_state_slot_handle ==
                        candidate.candidate_state_slot_handle &&
                    scope->owner_occurrence_handle ==
                        candidate.owner_occurrence_handle &&
                    token != nullptr &&
                    token->owner_kind ==
                        contracts::PlanImageWriterOwnerKind::
                            IntegrationCoordinator;
            } else if (candidate.producer_kind == "RuntimeCallsite") {
                const auto* callsite = find_handle(image->callsites(),
                                                   candidate.producer_handle);
                const auto* component = component_for_callsite(
                    candidate.producer_handle);
                const auto* entry = callsite == nullptr
                                        ? nullptr
                                        : entry_for_callsite(*callsite);
                producer_valid =
                    callsite != nullptr && component != nullptr &&
                    entry != nullptr &&
                    entry->kind ==
                        contracts::PlanImageEntryKind::IntervalEvolution &&
                    component->occurrence_handle ==
                        candidate.owner_occurrence_handle &&
                    token != nullptr &&
                    token->owner_kind ==
                        contracts::PlanImageWriterOwnerKind::RuntimeCallsite;
            }
            if (block == nullptr ||
                block->candidate_slot_handle !=
                    candidate.candidate_state_slot_handle ||
                block->owner_occurrence_handle !=
                    candidate.owner_occurrence_handle ||
                token == nullptr ||
                token->slot_handle !=
                    candidate.candidate_state_slot_handle ||
                token->owner_handle != candidate.producer_handle ||
                !producer_valid) {
                return failure(SessionError::InvalidImageStructure,
                               candidate.candidate_state_slot_handle,
                               "transaction candidate authority is invalid");
            }
        }
        if (!unique_nonzero_handles(candidate_slots) ||
            !unique_nonzero_handles(candidate_tokens)) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "transaction candidate membership is duplicated");
        }
        for (const auto held_handle : transaction.held_slot_handles) {
            const auto* held = find_handle(image->slots(), held_handle);
            if (held == nullptr ||
                held->storage_class !=
                    contracts::SlotStorageClass::IntegrationHeld ||
                held->hold_policy !=
                    contracts::SlotHoldPolicy::HoldInterval) {
                return failure(SessionError::InvalidImageStructure,
                               held_handle,
                               "transaction held slot is not interval-local");
            }
        }
        const auto* branch = contracts::find_transaction_branch(
            transaction, contracts::TransactionBranch::Continue);
        if (branch == nullptr || !branch->model_commit ||
            branch->epoch_delta != 1 || branch->tick_delta != 1 ||
            branch->output_visibility !=
                contracts::TransactionOutputVisibility::
                    AfterObservationSeal ||
            branch->held_interval_end_policy !=
                contracts::HeldIntervalEndPolicy::ReleaseAfterModelCommit ||
            !branch->observation_seal ||
            !same_handle_set(branch->committed_candidate_slot_handles,
                             candidate_slots) ||
            !same_handle_set(branch->retained_held_slot_handles,
                             transaction.held_slot_handles) ||
            !branch->discarded_candidate_slot_handles.empty() ||
            !branch->discarded_held_slot_handles.empty()) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "Continue transaction branch is not atomic");
        }
        std::vector<std::uint32_t> current_outputs;
        std::vector<std::uint32_t> terminal_outputs;
        for (const auto& slot : image->slots()) {
            if (slot.storage_class ==
                contracts::SlotStorageClass::CycleFrame) {
                current_outputs.push_back(slot.handle);
            } else if (slot.storage_class ==
                       contracts::SlotStorageClass::TerminalResult) {
                terminal_outputs.push_back(slot.handle);
            }
        }
        auto terminal_published = current_outputs;
        terminal_published.insert(terminal_published.end(),
                                  terminal_outputs.begin(),
                                  terminal_outputs.end());
        if (!same_handle_set(branch->published_output_slot_handles,
                             current_outputs) ||
            !same_handle_set(branch->sealed_output_slot_handles,
                             current_outputs) ||
            !same_handle_set(branch->discarded_output_slot_handles,
                             terminal_outputs)) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "Continue observation seal set is not exact");
        }
        const auto* terminal = contracts::find_transaction_branch(
            transaction, contracts::TransactionBranch::Terminal);
        if (terminal == nullptr || !terminal->committed_state_preserved ||
            !terminal->model_commit || !terminal->observation_seal ||
            !terminal->result_seal_after_observation ||
            terminal->epoch_delta != 1 || terminal->tick_delta != 0 ||
            !terminal->committed_candidate_slot_handles.empty() ||
            !same_handle_set(terminal->discarded_candidate_slot_handles,
                             candidate_slots) ||
            !terminal->retained_held_slot_handles.empty() ||
            !same_handle_set(terminal->discarded_held_slot_handles,
                             transaction.held_slot_handles) ||
            !same_handle_set(terminal->published_output_slot_handles,
                             terminal_published) ||
            !same_handle_set(terminal->sealed_output_slot_handles,
                             terminal_published) ||
            !terminal->discarded_output_slot_handles.empty() ||
            terminal->output_visibility !=
                contracts::TransactionOutputVisibility::
                    AfterObservationSeal ||
            terminal->held_interval_end_policy !=
                contracts::HeldIntervalEndPolicy::ReleaseAtTerminalSeal) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "Terminal transaction branch is not atomic");
        }
        const auto* failed = contracts::find_transaction_branch(
            transaction, contracts::TransactionBranch::Failure);
        if (failed == nullptr || !failed->committed_state_preserved ||
            failed->model_commit || failed->observation_seal ||
            failed->result_seal_after_observation ||
            failed->epoch_delta != 0 || failed->tick_delta != 0 ||
            !failed->committed_candidate_slot_handles.empty() ||
            !same_handle_set(failed->discarded_candidate_slot_handles,
                             candidate_slots) ||
            !failed->retained_held_slot_handles.empty() ||
            !same_handle_set(failed->discarded_held_slot_handles,
                             transaction.held_slot_handles) ||
            !failed->published_output_slot_handles.empty() ||
            !failed->sealed_output_slot_handles.empty() ||
            !same_handle_set(failed->discarded_output_slot_handles,
                             terminal_published) ||
            failed->output_visibility !=
                contracts::TransactionOutputVisibility::None ||
            failed->held_interval_end_policy !=
                contracts::HeldIntervalEndPolicy::DiscardOnFailure) {
            return failure(SessionError::InvalidImageStructure,
                           transaction.handle,
                           "Failure transaction branch is not atomic");
        }
        return {};
    }

    [[nodiscard]] std::uint32_t region_ordinal(
        std::uint32_t callsite_handle) const noexcept {
        std::uint32_t result =
            (std::numeric_limits<std::uint32_t>::max)();
        for (const auto& region : image->regions()) {
            if (std::find(region.callsite_handles.begin(),
                          region.callsite_handles.end(), callsite_handle) !=
                region.callsite_handles.end()) {
                result = (std::min)(result, region.ordinal);
            }
        }
        return result;
    }

    [[nodiscard]] SessionResult build_opening_schedule() {
        std::vector<std::uint32_t> ordered_nodes;
        std::vector<std::uint32_t> indegrees(image->dag_nodes().size(), 0U);
        std::vector<bool> emitted(image->dag_nodes().size(), false);
        ordered_nodes.reserve(image->dag_nodes().size());
        for (const auto& edge : image->dag_edges()) {
            const auto predecessor = find_handle(
                image->dag_nodes(), edge.predecessor_node_handle);
            const auto successor = find_handle(
                image->dag_nodes(), edge.successor_node_handle);
            if (predecessor == nullptr || successor == nullptr) {
                return failure(SessionError::InvalidSchedule, 0U,
                               "DAG edge references an unknown node");
            }
            const auto found = std::find_if(
                image->dag_nodes().begin(), image->dag_nodes().end(),
                [&edge](const auto& node) {
                    return node.handle == edge.successor_node_handle;
                });
            ++indegrees[static_cast<std::size_t>(
                std::distance(image->dag_nodes().begin(), found))];
        }
        while (ordered_nodes.size() != image->dag_nodes().size()) {
            std::size_t selected = image->dag_nodes().size();
            for (std::size_t index = 0U;
                 index < image->dag_nodes().size(); ++index) {
                if (emitted[index] || indegrees[index] != 0U) {
                    continue;
                }
                if (selected == image->dag_nodes().size()) {
                    selected = index;
                    continue;
                }
                const auto& candidate = image->dag_nodes()[index];
                const auto& incumbent = image->dag_nodes()[selected];
                const auto candidate_region =
                    candidate.kind ==
                            contracts::PlanImageDagNodeKind::RuntimeCallsite
                        ? region_ordinal(candidate.target_handle)
                        : (std::numeric_limits<std::uint32_t>::max)();
                const auto incumbent_region =
                    incumbent.kind ==
                            contracts::PlanImageDagNodeKind::RuntimeCallsite
                        ? region_ordinal(incumbent.target_handle)
                        : (std::numeric_limits<std::uint32_t>::max)();
                if (std::pair<std::uint32_t, std::uint32_t>(
                        candidate_region, candidate.handle) <
                    std::pair<std::uint32_t, std::uint32_t>(
                        incumbent_region, incumbent.handle)) {
                    selected = index;
                }
            }
            if (selected == image->dag_nodes().size()) {
                return failure(SessionError::InvalidSchedule, 0U,
                               "DAG contains a cycle");
            }
            emitted[selected] = true;
            const auto handle = image->dag_nodes()[selected].handle;
            ordered_nodes.push_back(handle);
            for (const auto& edge : image->dag_edges()) {
                if (edge.predecessor_node_handle != handle) {
                    continue;
                }
                const auto found = std::find_if(
                    image->dag_nodes().begin(), image->dag_nodes().end(),
                    [&edge](const auto& node) {
                        return node.handle == edge.successor_node_handle;
                    });
                const auto index = static_cast<std::size_t>(
                    std::distance(image->dag_nodes().begin(), found));
                if (indegrees[index] == 0U) {
                    return failure(SessionError::InvalidSchedule,
                                   edge.successor_node_handle,
                                   "DAG indegree underflow");
                }
                --indegrees[index];
            }
        }

        opening_schedule.clear();
        const auto append_kind = [this, &ordered_nodes](
                                     contracts::PlanImageEntryKind kind)
            -> SessionResult {
            for (const auto node_handle : ordered_nodes) {
                const auto* node = find_handle(image->dag_nodes(), node_handle);
                if (node == nullptr ||
                    node->kind !=
                        contracts::PlanImageDagNodeKind::RuntimeCallsite) {
                    continue;
                }
                const auto* callsite = find_handle(image->callsites(),
                                                   node->target_handle);
                if (callsite == nullptr) {
                    return failure(SessionError::InvalidSchedule,
                                   node->target_handle,
                                   "DAG callsite node is invalid");
                }
                const auto* entry = entry_for_callsite(*callsite);
                if (entry == nullptr || entry->kind != kind) {
                    continue;
                }
                const auto* component = component_for_callsite(callsite->handle);
                if (component == nullptr ||
                    region_ordinal(callsite->handle) ==
                        (std::numeric_limits<std::uint32_t>::max)()) {
                    return failure(SessionError::InvalidSchedule,
                                   callsite->handle,
                                   "opening callsite ownership is invalid");
                }
                opening_schedule.push_back(
                    {callsite->handle, component->handle, kind});
            }
            return {};
        };
        auto result = append_kind(
            contracts::PlanImageEntryKind::PublishProjection);
        if (result) {
            result = append_kind(
                contracts::PlanImageEntryKind::BoundaryEvaluation);
        }
        if (!result) {
            return result;
        }
        for (const auto& callsite : image->callsites()) {
            const auto* entry = entry_for_callsite(callsite);
            if (entry == nullptr) {
                return failure(SessionError::InvalidImageHandle,
                               callsite.handle,
                               "callsite entry is missing");
            }
            if (entry->kind !=
                    contracts::PlanImageEntryKind::PublishProjection &&
                entry->kind !=
                    contracts::PlanImageEntryKind::BoundaryEvaluation) {
                continue;
            }
            const auto count = static_cast<std::size_t>(std::count_if(
                opening_schedule.begin(), opening_schedule.end(),
                [&callsite](const auto& scheduled) {
                    return scheduled.callsite_handle == callsite.handle;
                }));
            if (count != 1U) {
                return failure(SessionError::InvalidSchedule,
                               callsite.handle,
                               "opening callsite DAG membership is not singular");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_image() {
        if (image->revision() != kSupportedImageRevision) {
            return failure(SessionError::UnsupportedImageRevision,
                           image->revision(),
                           "Image revision is unsupported");
        }
        if (!unique_nonzero_handles(image->storage_layouts()) ||
            !unique_nonzero_handles(image->entries()) ||
            !unique_nonzero_handles(image->slots()) ||
            !unique_nonzero_handles(image->state_blocks()) ||
            !unique_nonzero_handles(image->preparations()) ||
            !unique_nonzero_handles(image->queries()) ||
            !unique_nonzero_handles(image->closures()) ||
            !unique_nonzero_handles(image->runtime_components()) ||
            !unique_nonzero_handles(image->initial_bindings()) ||
            !unique_nonzero_handles(image->callsites()) ||
            !unique_nonzero_handles(image->writer_tokens()) ||
            !unique_nonzero_handles(image->invocations()) ||
            !unique_nonzero_handles(image->regions()) ||
            !unique_nonzero_handles(image->dag_nodes()) ||
            !unique_nonzero_handles(image->integration_scopes()) ||
            !unique_nonzero_handles(image->transactions()) ||
            !unique_nonzero_handles(image->evaluator_histories())) {
            return failure(SessionError::InvalidImageHandle, 0U,
                           "Image contains a duplicate or zero handle");
        }
        auto result = validate_lifecycle();
        if (result) result = validate_storage();
        if (result) result = validate_states();
        if (result) result = validate_histories();
        if (result) result = validate_transactions();
        if (result) result = validate_materializers();
        if (result) result = build_opening_schedule();
        return result;
    }

    void reserve_tracking() {
        arenas.reserve(image->storage_layouts().size());
        preparations.reserve(image->preparations().size());
        runtime_bindings.cells.reserve(image->runtime_components().size());
        committed_state_store.blocks.reserve(image->state_blocks().size());
        candidate_state_store.blocks.reserve(image->state_blocks().size());
        cycle_frame.slots.reserve(image->slots().size());
        cycle_frame.construction_order.reserve(image->slots().size());
        evaluator_histories.reserve(image->evaluator_histories().size());
        staged_evaluator_histories.reserve(
            image->evaluator_histories().size());
        sealed_boundary.outputs.reserve(image->slots().size());
        staged_sealed_boundary.outputs.reserve(image->slots().size());
        opening_schedule.reserve(image->callsites().size());
        boundary_summary.executed_callsite_handles.reserve(
            image->callsites().size());
        boundary_summary.skipped_callsite_handles.reserve(
            image->callsites().size());
        step_summary.executed_callsite_handles.reserve(
            image->callsites().size());
        step_summary.skipped_callsite_handles.reserve(
            image->callsites().size());
        step_summary.integration_scope_handles.reserve(
            image->integration_scopes().size());
        step_summary.candidate_slot_handles.reserve(
            image->state_blocks().size());
        step_summary.candidates.reserve(image->state_blocks().size());
        step_summary.histories.reserve(image->evaluator_histories().size());
        step_summary.seals.reserve(image->slots().size());
    }

    [[nodiscard]] SessionResult allocate_arenas() noexcept {
        for (const auto& layout : image->storage_layouts()) {
            RawBlock allocation(layout.size_bytes, layout.alignment_bytes);
            if (allocation.get() == nullptr) {
                return failure(SessionError::AllocationFailure, layout.handle,
                               "storage allocation failed");
            }
            arenas.push_back({&layout, allocation.release()});
        }
        return {};
    }

    [[nodiscard]] SessionResult construct_owned(
        std::uint32_t handle,
        const SessionObjectMaterializer& materializer,
        const SessionObjectAccess& object_access,
        std::vector<OwnedObject>& destination, SessionObjectRole role,
        std::uint32_t linked_entry_handle,
        SessionError construction_error) noexcept {
        const auto layout = materializer.operations().layout();
        RawBlock allocation(layout.size_bytes, layout.alignment_bytes);
        if (allocation.get() == nullptr) {
            return failure(SessionError::AllocationFailure, handle,
                           "object allocation failed");
        }
        if (!materializer.construct(object_access, allocation.get())) {
            return failure(construction_error, handle,
                           "typed object construction failed");
        }
        ConstructedObject constructed(allocation.get(),
                                      materializer.operations());
        if (!materializer.operations().validate(allocation.get())) {
            return failure(SessionError::ObjectValidationFailed, handle,
                           "constructed object validation failed");
        }
        destination.push_back({handle, allocation.get(),
                               layout.alignment_bytes, &materializer, role,
                               linked_entry_handle});
        constructed.release();
        static_cast<void>(allocation.release());
        return {};
    }

    [[nodiscard]] SessionResult validate_runtime_dependencies(
        const SessionObjectMaterializer& materializer,
        std::uint32_t component_handle) noexcept {
        for (std::size_t index = 0U;
             index < materializer.dependency_count(); ++index) {
            const auto expected = materializer.dependency(index);
            const auto actual = prepared_object(expected.image_object_handle);
            if (!same_requirement(expected, actual)) {
                return failure(SessionError::ObjectTypeMismatch,
                               component_handle,
                               "Runtime Cell dependency identity mismatch");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult prepare_frame_slots() noexcept {
        for (const auto& slot : image->slots()) {
            if (state_for_slot(slot.handle) != nullptr) {
                continue;
            }
            auto* persistent_address = slot_address(slot);
            const auto* materializer = provider->slot(slot.handle);
            if (persistent_address == nullptr || materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, slot.handle,
                               "frame slot storage is unavailable");
            }
            cycle_frame.slots.push_back(
                {&slot, persistent_address, materializer, false});
        }
        return {};
    }

    [[nodiscard]] SessionResult construct_states() noexcept {
        for (const auto handle : image->lifecycle().initial_binding_handles) {
            const auto* binding = find_handle(image->initial_bindings(), handle);
            if (binding == nullptr) {
                return failure(SessionError::InvalidImageHandle, handle,
                               "initial binding disappeared");
            }
            const auto block_found = std::find_if(
                image->state_blocks().begin(), image->state_blocks().end(),
                [binding](const auto& block) {
                    return block.committed_slot_handle ==
                           binding->committed_state_slot_handle;
                });
            if (block_found == image->state_blocks().end()) {
                return failure(SessionError::InvalidImageStructure, handle,
                               "initial binding lost its state block");
            }
            const auto& block = *block_found;
            const auto* committed = find_handle(image->slots(),
                                                block.committed_slot_handle);
            const auto* candidate = find_handle(image->slots(),
                                                block.candidate_slot_handle);
            const auto* materializer = provider->initial_state(handle);
            if (committed == nullptr || candidate == nullptr ||
                materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, handle,
                               "state construction dependency is missing");
            }
            const auto layout = materializer->operations().layout();
            RawBlock temporary(layout.size_bytes, layout.alignment_bytes);
            if (temporary.get() == nullptr) {
                return failure(SessionError::AllocationFailure, handle,
                               "initial-state temporary allocation failed");
            }
            const ScopedObjectAccess no_dependencies(*this, nullptr);
            if (!materializer->construct(no_dependencies, temporary.get())) {
                return failure(SessionError::InitialStateFailed, handle,
                               "typed initial-state builder failed");
            }
            ConstructedObject initial_object(temporary.get(),
                                             materializer->operations());
            if (!materializer->operations().validate(temporary.get())) {
                return failure(SessionError::ObjectValidationFailed, handle,
                               "initial state validation failed");
            }
            auto* committed_address = slot_address(*committed);
            auto* candidate_address = slot_address(*candidate);
            if (committed_address == nullptr || candidate_address == nullptr ||
                !materializer->operations().copy_construct(
                    temporary.get(), committed_address)) {
                return failure(SessionError::InitialStateFailed, handle,
                               "committed state clone failed");
            }
            ConstructedObject committed_guard(committed_address,
                                              materializer->operations());
            if (!materializer->operations().validate(committed_address)) {
                return failure(SessionError::ObjectValidationFailed, handle,
                               "committed state validation failed");
            }
            const auto owner = owner_component_for_occurrence(
                block.owner_occurrence_handle);
            committed_state_store.blocks.push_back(
                {&block, binding, committed_address, materializer, owner,
                 committed_epoch});
            committed_guard.release();

            if (!materializer->operations().copy_construct(
                    temporary.get(), candidate_address)) {
                return failure(SessionError::InitialStateFailed, handle,
                               "candidate state clone failed");
            }
            ConstructedObject candidate_guard(candidate_address,
                                              materializer->operations());
            if (!materializer->operations().validate(candidate_address)) {
                return failure(SessionError::ObjectValidationFailed, handle,
                               "candidate state validation failed");
            }
            candidate_state_store.blocks.push_back(
                {&block, binding, candidate_address, materializer, owner, 0U});
            candidate_guard.release();
        }
        return {};
    }

    [[nodiscard]] SessionResult stage_reset_states(
        std::vector<ResetStateReplacement>& replacements) noexcept {
        for (const auto& committed : committed_state_store.blocks) {
            if (committed.block == nullptr || committed.initial == nullptr ||
                committed.materializer == nullptr ||
                committed.address == nullptr) {
                return failure(SessionError::ResetStateFailed, 0U,
                               "reset state metadata is incomplete");
            }
            auto* candidate = candidate_for_slot(
                committed.block->candidate_slot_handle);
            if (candidate == nullptr || candidate->materializer == nullptr ||
                candidate->address == nullptr ||
                candidate->block != committed.block ||
                candidate->materializer != committed.materializer) {
                return failure(SessionError::ResetStateFailed,
                               committed.block->handle,
                               "reset candidate state identity mismatch");
            }
            const auto& materializer = *committed.materializer;
            const auto& operations = materializer.operations();
            const auto layout = operations.layout();
            RawBlock initial(layout.size_bytes, layout.alignment_bytes);
            if (initial.get() == nullptr) {
                return failure(SessionError::AllocationFailure,
                               committed.initial->handle,
                               "reset initial-state allocation failed");
            }
            const ScopedObjectAccess no_dependencies(*this, nullptr);
            if (!materializer.construct(no_dependencies, initial.get())) {
                return failure(SessionError::InitialStateFailed,
                               committed.initial->handle,
                               "reset initial-state builder failed");
            }
            ConstructedObject initial_guard(initial.get(), operations);
            if (!operations.validate(initial.get())) {
                return failure(SessionError::ObjectValidationFailed,
                               committed.initial->handle,
                               "reset initial state validation failed");
            }

            RawBlock staged_committed(layout.size_bytes,
                                      layout.alignment_bytes);
            if (staged_committed.get() == nullptr) {
                return failure(SessionError::AllocationFailure,
                               committed.block->handle,
                               "reset committed-state allocation failed");
            }
            if (!operations.copy_construct(initial.get(),
                                           staged_committed.get())) {
                return failure(SessionError::ResetStateFailed,
                               committed.block->handle,
                               "reset committed-state clone failed");
            }
            ConstructedObject committed_guard(staged_committed.get(),
                                              operations);
            if (!operations.validate(staged_committed.get())) {
                return failure(SessionError::ObjectValidationFailed,
                               committed.block->handle,
                               "reset committed state validation failed");
            }

            RawBlock staged_candidate(layout.size_bytes,
                                      layout.alignment_bytes);
            if (staged_candidate.get() == nullptr) {
                return failure(SessionError::AllocationFailure,
                               committed.block->handle,
                               "reset candidate-state allocation failed");
            }
            if (!operations.copy_construct(initial.get(),
                                           staged_candidate.get())) {
                return failure(SessionError::ResetStateFailed,
                               committed.block->handle,
                               "reset candidate-state clone failed");
            }
            ConstructedObject candidate_guard(staged_candidate.get(),
                                              operations);
            if (!operations.validate(staged_candidate.get())) {
                return failure(SessionError::ObjectValidationFailed,
                               committed.block->handle,
                               "reset candidate state validation failed");
            }
            if (!operations.replace(staged_candidate.get(), initial.get())) {
                return failure(SessionError::ResetStateFailed,
                               committed.block->handle,
                               "reset candidate-state replace failed");
            }
            if (!operations.validate(staged_candidate.get())) {
                return failure(SessionError::ObjectValidationFailed,
                               committed.block->handle,
                               "reset replaced candidate state validation failed");
            }

            ResetStateReplacement replacement;
            replacement.materializer = &materializer;
            replacement.alignment = layout.alignment_bytes;
            committed_guard.release();
            candidate_guard.release();
            replacement.committed_address = staged_committed.release();
            replacement.candidate_address = staged_candidate.release();
            replacements.push_back(std::move(replacement));
        }
        return {};
    }

    void prepare_reset_histories(
        std::vector<EvaluatorHistoryStore>& histories) const {
        histories.reserve(image->evaluator_histories().size());
        for (const auto& plan : image->evaluator_histories()) {
            EvaluatorHistoryStore history;
            history.plan = &plan;
            history.samples.reserve(plan.history_depth);
            histories.push_back(std::move(history));
        }
    }

    [[nodiscard]] SessionResult validate_reset_precommit(
        const std::vector<ResetStateReplacement>& replacements,
        const std::vector<EvaluatorHistoryStore>& histories) noexcept {
        if (state != SessionState::Completed || cycle_frame.open ||
            active_transaction_handle != 0U ||
            !cycle_frame.construction_order.empty() ||
            committed_run_sequence ==
                (std::numeric_limits<std::uint64_t>::max)() ||
            committed_epoch ==
                (std::numeric_limits<std::uint64_t>::max)() ||
            committed_state_store.blocks.size() !=
                image->state_blocks().size() ||
            candidate_state_store.blocks.size() !=
                committed_state_store.blocks.size() ||
            replacements.size() != committed_state_store.blocks.size() ||
            histories.size() != image->evaluator_histories().size()) {
            return failure(SessionError::ResetPrecommitFailed, 0U,
                           "reset final precommit shape is invalid");
        }
        for (std::size_t index = 0U; index < replacements.size(); ++index) {
            const auto& replacement = replacements[index];
            const auto& committed = committed_state_store.blocks[index];
            if (replacement.materializer == nullptr ||
                replacement.materializer != committed.materializer ||
                replacement.committed_address == nullptr ||
                replacement.candidate_address == nullptr ||
                !replacement.materializer->operations()
                     .supports_nofail_swap()) {
                return failure(SessionError::ResetPrecommitFailed,
                               committed.block == nullptr
                                   ? 0U
                                   : committed.block->handle,
                               "reset state lacks a no-fail commit operation");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult validate_reset_capabilities() noexcept {
        for (const auto& component : image->runtime_components()) {
            const auto count = static_cast<std::size_t>(std::count(
                component.lifecycle_capabilities.begin(),
                component.lifecycle_capabilities.end(), "Resettable"));
            if (count != 1U) {
                return failure(
                    SessionError::ResetCapabilityMissing,
                    component.handle,
                    "Runtime Cell does not declare exact reset reuse eligibility");
            }
        }
        return {};
    }

    void clear_run_journals_noexcept() noexcept {
        boundary_summary.generation = 0U;
        boundary_summary.output_write_count = 0U;
        boundary_summary.executed_callsite_handles.clear();
        boundary_summary.skipped_callsite_handles.clear();

        step_summary.transaction_handle = 0U;
        step_summary.branch = contracts::TransactionBranch::Continue;
        step_summary.branch_selected = false;
        step_summary.generation = 0U;
        step_summary.base_epoch = 0U;
        step_summary.committed_epoch = 0U;
        step_summary.base_tick = image->clock().initial_tick;
        step_summary.committed_tick = image->clock().initial_tick;
        step_summary.output_write_count = 0U;
        step_summary.committed = false;
        step_summary.history_staged = false;
        step_summary.observation_seal_staged = false;
        step_summary.result_seal_staged = false;
        step_summary.terminal_result_present = false;
        step_summary.prevalidated = false;
        step_summary.primary_failure = {};
        step_summary.executed_callsite_handles.clear();
        step_summary.skipped_callsite_handles.clear();
        step_summary.integration_scope_handles.clear();
        step_summary.candidate_slot_handles.clear();
        step_summary.candidates.clear();
        step_summary.histories.clear();
        step_summary.seals.clear();

        step_outcome = {};
        cycle_frame.sequence = 0U;
        cycle_frame.write_count = 0U;
        cycle_frame.open = false;
        cycle_frame.construction_order.clear();
        for (auto& slot : cycle_frame.slots) {
            slot.present = false;
            slot.generation = 0U;
            slot.sequence = 0U;
            slot.sample_tick = image->clock().initial_tick;
            slot.sample_time_seconds = 0.0;
            slot.interval_start_seconds = 0.0;
            slot.interval_end_seconds = 0.0;
            slot.quality = contracts::DataQuality::Invalid;
        }
        for (auto& candidate : candidate_state_store.blocks) {
            candidate.candidate_present = false;
            candidate.candidate_generation = 0U;
            candidate.candidate_base_epoch = 0U;
            candidate.candidate_producer_handle = 0U;
            candidate.candidate_writer_token_handle = 0U;
        }
        staged_evaluator_histories.clear();
        staged_sealed_boundary.outputs.clear();
        staged_sealed_boundary.committed_epoch = 0U;
        staged_sealed_boundary.committed_tick = image->clock().initial_tick;
        active_transaction_handle = 0U;
    }

    [[nodiscard]] SessionResult prepare_evaluator_histories() {
        evaluator_histories.clear();
        for (const auto& history : image->evaluator_histories()) {
            EvaluatorHistoryStore store;
            store.plan = &history;
            store.samples.reserve(history.history_depth);
            evaluator_histories.push_back(std::move(store));
        }
        return {};
    }

    [[nodiscard]] StateObject* candidate_for_slot(
        std::uint32_t slot_handle) noexcept {
        const auto found = std::find_if(
            candidate_state_store.blocks.begin(),
            candidate_state_store.blocks.end(),
            [slot_handle](const auto& candidate) {
                return candidate.block != nullptr &&
                       candidate.block->candidate_slot_handle == slot_handle;
            });
        return found == candidate_state_store.blocks.end() ? nullptr
                                                            : &*found;
    }

    [[nodiscard]] StateObject* committed_for_block(
        std::uint32_t block_handle) noexcept {
        const auto found = std::find_if(
            committed_state_store.blocks.begin(),
            committed_state_store.blocks.end(),
            [block_handle](const auto& committed) {
                return committed.block != nullptr &&
                       committed.block->handle == block_handle;
            });
        return found == committed_state_store.blocks.end() ? nullptr
                                                            : &*found;
    }

    [[nodiscard]] SessionResult rearm_candidates(
        const contracts::PlanImageTransaction& transaction) noexcept {
        for (auto& candidate : candidate_state_store.blocks) {
            candidate.candidate_present = false;
            candidate.candidate_generation = 0U;
            candidate.candidate_base_epoch = 0U;
            candidate.candidate_producer_handle = 0U;
            candidate.candidate_writer_token_handle = 0U;
        }
        for (const auto& member : transaction.candidates) {
            auto* candidate = candidate_for_slot(
                member.candidate_state_slot_handle);
            auto* committed =
                candidate == nullptr || candidate->block == nullptr
                    ? nullptr
                    : committed_for_block(candidate->block->handle);
            if (candidate == nullptr || committed == nullptr ||
                candidate->materializer != committed->materializer ||
                !candidate->materializer->operations().validate(
                    committed->address) ||
                !candidate->materializer->operations().replace(
                    candidate->address, committed->address) ||
                !candidate->materializer->operations().validate(
                    candidate->address)) {
                return failure(SessionError::CandidateRearmFailed,
                               member.candidate_state_slot_handle,
                               "candidate rearm from committed state failed");
            }
        }
        return {};
    }

    [[nodiscard]] SessionResult stage_histories() {
        staged_evaluator_histories.clear();
        step_summary.histories.clear();
        step_summary.history_staged = false;
        const double sample_time =
            static_cast<double>(committed_tick) *
            image->clock().base_step_seconds;
        for (const auto& persistent : evaluator_histories) {
            if (persistent.plan == nullptr ||
                persistent.samples.size() >=
                    persistent.plan->history_depth) {
                return failure(SessionError::HistoryValidationFailed,
                               persistent.plan == nullptr
                                   ? 0U
                                   : persistent.plan->handle,
                               "committed history capacity is invalid");
            }
            EvaluatorHistoryStore staged;
            staged.plan = persistent.plan;
            staged.samples.reserve(persistent.plan->history_depth);
            for (std::size_t sample_index = 0U;
                 sample_index < persistent.samples.size(); ++sample_index) {
                const auto& source_sample =
                    persistent.samples[sample_index];
                if (source_sample.members.size() !=
                        persistent.plan->ordered_members.size() ||
                    (sample_index > 0U &&
                     source_sample.tick !=
                         persistent.samples[sample_index - 1U].tick + 1)) {
                    return failure(SessionError::HistoryValidationFailed,
                                   persistent.plan->handle,
                                   "committed history sequence is invalid");
                }
                HistorySample clone;
                clone.tick = source_sample.tick;
                clone.committed_epoch = source_sample.committed_epoch;
                clone.members.reserve(source_sample.members.size());
                for (std::size_t member_index = 0U;
                     member_index < source_sample.members.size();
                     ++member_index) {
                    const auto& source =
                        source_sample.members[member_index];
                    const auto& planned =
                        persistent.plan->ordered_members[member_index];
                    if (source.slot == nullptr ||
                        source.materializer == nullptr ||
                        source.slot->handle !=
                            planned.committed_state_slot_handle) {
                        return failure(
                            SessionError::HistoryValidationFailed,
                            persistent.plan->handle,
                            "committed history member order is invalid");
                    }
                    StoredValue member;
                    auto result = clone_stored_value(
                        *source.slot, *source.materializer, source.address,
                        source.generation, source.sequence,
                        source.sample_tick, source.sample_time_seconds,
                        source.interval_start_seconds,
                        source.interval_end_seconds, source.quality, member,
                        SessionError::HistoryValidationFailed,
                        "committed history deep clone failed");
                    if (!result) return result;
                    clone.members.push_back(std::move(member));
                }
                staged.samples.push_back(std::move(clone));
            }
            if (!staged.samples.empty() &&
                staged.samples.back().tick + 1 != committed_tick) {
                return failure(SessionError::HistoryValidationFailed,
                               persistent.plan->handle,
                               "committed history tick is not contiguous");
            }

            HistorySample current;
            current.tick = committed_tick;
            current.committed_epoch = committed_epoch;
            current.members.reserve(
                persistent.plan->ordered_members.size());
            for (std::size_t member_index = 0U;
                 member_index < persistent.plan->ordered_members.size();
                 ++member_index) {
                const auto& planned =
                    persistent.plan->ordered_members[member_index];
                const auto* state = committed_for_slot(
                    planned.committed_state_slot_handle);
                const auto* slot = find_handle(
                    image->slots(), planned.committed_state_slot_handle);
                if (state == nullptr || state->block == nullptr ||
                    state->materializer == nullptr || slot == nullptr ||
                    state->block->schema_id != planned.state_schema_id ||
                    state->block->layout_id != planned.state_layout_id ||
                    state->committed_epoch != committed_epoch) {
                    return failure(
                        SessionError::HistoryValidationFailed,
                        persistent.plan->handle,
                        "current committed history member is invalid");
                }
                StoredValue member;
                auto result = clone_stored_value(
                    *slot, *state->materializer, state->address,
                    cycle_frame.generation,
                    static_cast<std::uint64_t>(member_index + 1U),
                    committed_tick, sample_time, sample_time, sample_time,
                    contracts::DataQuality::Valid, member,
                    SessionError::HistoryValidationFailed,
                    "current committed history staging failed");
                if (!result) return result;
                current.members.push_back(std::move(member));
            }
            staged.samples.push_back(std::move(current));
            step_summary.histories.push_back(
                {persistent.plan->handle, committed_tick,
                 staged.samples.size()});
            staged_evaluator_histories.push_back(std::move(staged));
        }
        step_summary.history_staged =
            staged_evaluator_histories.size() ==
            evaluator_histories.size();
        return {};
    }

    [[nodiscard]] SessionResult stage_seals(
        const contracts::PlanImageTransactionBranch& branch) {
        staged_sealed_boundary.outputs.clear();
        step_summary.seals.clear();
        step_summary.observation_seal_staged = false;
        step_summary.result_seal_staged = false;
        step_summary.terminal_result_present = false;
        staged_sealed_boundary.committed_epoch =
            committed_epoch + static_cast<std::uint64_t>(branch.epoch_delta);
        staged_sealed_boundary.committed_tick =
            committed_tick + branch.tick_delta;
        staged_sealed_boundary.outputs.reserve(
            branch.sealed_output_slot_handles.size());
        const auto stage_class = [this, &branch](
                                     contracts::SlotStorageClass storage_class)
            -> SessionResult {
            for (const auto slot_handle :
                 branch.sealed_output_slot_handles) {
                const auto* planned = find_handle(image->slots(), slot_handle);
                if (planned == nullptr ||
                    planned->storage_class != storage_class) {
                    continue;
                }
                const auto* frame = frame_slot(slot_handle);
                if (frame == nullptr || frame->materializer == nullptr) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output storage is unavailable");
                }
                if (!frame->present) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output is absent");
                }
                if (frame->generation != cycle_frame.generation) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output generation is stale");
                }
                if (frame->sample_tick != committed_tick) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output tick is stale");
                }
                if (frame->quality != contracts::DataQuality::Valid) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output quality is invalid");
                }
                if (!frame->materializer->operations().validate(
                        frame->address)) {
                    return failure(SessionError::ObservationSealFailed,
                                   slot_handle,
                                   "sealed output codec validation failed");
                }
                StoredValue sealed;
                auto result = clone_stored_value(
                    *planned, *frame->materializer, frame->address,
                    frame->generation, frame->sequence,
                    frame->sample_tick, frame->sample_time_seconds,
                    frame->interval_start_seconds,
                    frame->interval_end_seconds, frame->quality, sealed,
                    SessionError::ObservationSealFailed,
                    "sealed output deep clone failed");
                if (!result) return result;
                const bool terminal_result =
                    storage_class ==
                    contracts::SlotStorageClass::TerminalResult;
                step_summary.seals.push_back(
                    {slot_handle, planned->codec_entry_handle,
                     frame->generation, frame->sequence,
                     terminal_result});
                staged_sealed_boundary.outputs.push_back(
                    std::move(sealed));
                if (terminal_result) {
                    step_summary.terminal_result_present = true;
                }
            }
            return {};
        };
        auto result = stage_class(
            contracts::SlotStorageClass::CycleFrame);
        if (result) {
            result = stage_class(
                contracts::SlotStorageClass::TerminalResult);
        }
        if (!result) return result;
        step_summary.observation_seal_staged =
            branch.observation_seal &&
            staged_sealed_boundary.outputs.size() ==
                branch.sealed_output_slot_handles.size();
        step_summary.result_seal_staged =
            branch.result_seal_after_observation &&
            step_summary.terminal_result_present;
        return {};
    }

    [[nodiscard]] SessionResult validate_precommit(
        const contracts::PlanImageTransaction& transaction,
        const contracts::PlanImageTransactionBranch& branch) {
        if (!step_summary.history_staged ||
            staged_evaluator_histories.size() !=
                evaluator_histories.size()) {
            return failure(SessionError::TransactionPrecommitFailed,
                           transaction.handle,
                           "history staging is incomplete at precommit");
        }
        for (const auto& history : staged_evaluator_histories) {
            if (history.plan == nullptr || history.samples.empty() ||
                history.samples.size() > history.plan->history_depth ||
                history.samples.back().tick != committed_tick ||
                history.samples.back().members.size() !=
                    history.plan->ordered_members.size()) {
                return failure(SessionError::HistoryValidationFailed,
                               history.plan == nullptr
                                   ? transaction.handle
                                   : history.plan->handle,
                               "staged history is invalid at precommit");
            }
        }
        std::vector<std::uint32_t> staged_seals;
        staged_seals.reserve(staged_sealed_boundary.outputs.size());
        for (const auto& output : staged_sealed_boundary.outputs) {
            if (output.slot == nullptr || output.materializer == nullptr ||
                output.address == nullptr ||
                output.generation != cycle_frame.generation ||
                output.sample_tick != committed_tick ||
                output.quality != contracts::DataQuality::Valid ||
                !output.materializer->operations().validate(
                    output.address)) {
                return failure(SessionError::ObservationSealFailed,
                               output.slot == nullptr
                                   ? transaction.handle
                                   : output.slot->handle,
                               "staged observation seal is invalid");
            }
            staged_seals.push_back(output.slot->handle);
        }
        if (!step_summary.observation_seal_staged ||
            !same_handle_set(staged_seals,
                             branch.sealed_output_slot_handles) ||
            (branch.result_seal_after_observation &&
             (!step_summary.result_seal_staged ||
              !step_summary.terminal_result_present))) {
            return failure(SessionError::ObservationSealFailed,
                           transaction.handle,
                           "observation or result seal is incomplete");
        }
        if (!same_handle_set(branch.committed_candidate_slot_handles,
                             step_summary.candidate_slot_handles)) {
            return failure(SessionError::TransactionPrecommitFailed,
                           transaction.handle,
                           "candidate set is incomplete at precommit");
        }
        step_summary.candidates.clear();
        for (const auto& member : transaction.candidates) {
            auto* candidate = candidate_for_slot(
                member.candidate_state_slot_handle);
            const bool should_commit = std::find(
                branch.committed_candidate_slot_handles.begin(),
                branch.committed_candidate_slot_handles.end(),
                member.candidate_state_slot_handle) !=
                branch.committed_candidate_slot_handles.end();
            const bool valid =
                candidate != nullptr && candidate->materializer != nullptr &&
                candidate->materializer->operations().validate(
                    candidate->address);
            step_summary.candidates.push_back(
                {member.candidate_state_slot_handle,
                 member.producer_kind == "IntegrationScope"
                     ? SessionCandidateProducerKind::IntegrationScope
                     : SessionCandidateProducerKind::RuntimeCallsite,
                 member.producer_handle, member.writer_token_handle,
                 candidate == nullptr ? 0U : candidate->candidate_base_epoch,
                 candidate == nullptr ? 0U : candidate->candidate_generation,
                 candidate != nullptr && candidate->candidate_present,
                 valid});
            if (!should_commit) {
                if (candidate != nullptr && candidate->candidate_present) {
                    return failure(
                        SessionError::TransactionPrecommitFailed,
                        member.candidate_state_slot_handle,
                        "discarded candidate was produced unexpectedly");
                }
                continue;
            }
            if (candidate == nullptr || !candidate->candidate_present ||
                candidate->candidate_generation != cycle_frame.generation ||
                candidate->candidate_base_epoch != committed_epoch ||
                candidate->candidate_producer_handle !=
                    member.producer_handle ||
                candidate->candidate_writer_token_handle !=
                    member.writer_token_handle ||
                !candidate->materializer->operations()
                     .supports_nofail_swap()) {
                return failure(SessionError::TransactionPrecommitFailed,
                               member.candidate_state_slot_handle,
                               "candidate metadata is incomplete at precommit");
            }
            if (!valid) {
                return failure(SessionError::CandidateValidationFailed,
                               member.candidate_state_slot_handle,
                               "candidate codec validation failed");
            }
        }
        for (const auto held_handle : transaction.held_slot_handles) {
            const auto* held = frame_slot(held_handle);
            if (held == nullptr || !held->present ||
                held->generation != cycle_frame.generation) {
                return failure(SessionError::TransactionPrecommitFailed,
                               held_handle,
                               "held interval value is absent at precommit");
            }
        }
        step_summary.prevalidated = true;
        return {};
    }

    void commit_transaction_noexcept(
        const contracts::PlanImageTransactionBranch& branch) noexcept {
        for (const auto slot_handle :
             branch.committed_candidate_slot_handles) {
            auto* candidate = candidate_for_slot(slot_handle);
            auto* committed = committed_for_block(candidate->block->handle);
            candidate->materializer->operations().nofail_swap(
                committed->address, candidate->address);
        }
        committed_epoch += static_cast<std::uint64_t>(branch.epoch_delta);
        committed_tick += branch.tick_delta;
        for (auto& committed : committed_state_store.blocks) {
            committed.committed_epoch = committed_epoch;
        }
        evaluator_histories.swap(staged_evaluator_histories);
        sealed_boundary.outputs.swap(staged_sealed_boundary.outputs);
        sealed_boundary.committed_epoch =
            staged_sealed_boundary.committed_epoch;
        sealed_boundary.committed_tick =
            staged_sealed_boundary.committed_tick;
    }

    void close_frame() noexcept {
        for (auto found = cycle_frame.construction_order.rbegin();
             found != cycle_frame.construction_order.rend(); ++found) {
            auto& slot = cycle_frame.slots[*found];
            if (slot.present) {
                slot.materializer->operations().destroy(slot.address);
                slot.present = false;
            }
        }
        cycle_frame.construction_order.clear();
        cycle_frame.open = false;
        active_transaction_handle = 0U;
        for (auto& candidate : candidate_state_store.blocks) {
            candidate.candidate_present = false;
            candidate.candidate_generation = 0U;
            candidate.candidate_base_epoch = 0U;
            candidate.candidate_producer_handle = 0U;
            candidate.candidate_writer_token_handle = 0U;
        }
        staged_evaluator_histories.clear();
        staged_sealed_boundary.outputs.clear();
        staged_sealed_boundary.committed_epoch = 0U;
        staged_sealed_boundary.committed_tick = 0;
    }

    [[nodiscard]] StepOutcome fail_execution(
        SessionError error, std::uint32_t handle,
        std::string_view detail) noexcept {
        const auto result = failure(error, handle, detail);
        step_summary.primary_failure = result;
        const auto diagnostic = make_diagnostic(
            result, current_diagnostic_stage,
            contracts::EvidenceValidity::Invalid);
        close_frame();
        state = SessionState::Failed;
        step_outcome = make_step_outcome(
            StepStatus::Failed, result, diagnostic);
        freeze_failed_run(diagnostic);
        return step_outcome;
    }

    [[nodiscard]] StepOutcome fail_execution(
        SessionResult result, std::uint32_t fallback_handle,
        std::string_view fallback_detail) noexcept {
        return fail_execution(
            result.error == SessionError::None ? SessionError::InternalFailure
                                               : result.error,
            result.image_handle == 0U ? fallback_handle
                                      : result.image_handle,
            result.detail.empty() ? fallback_detail : result.detail);
    }

    void unwind() noexcept {
        if (resources_disposed) return;
        resources_disposed = true;
        close_frame();
        staged_sealed_boundary.outputs.clear();
        sealed_boundary.outputs.clear();
        staged_evaluator_histories.clear();
        evaluator_histories.clear();
        for (auto found = candidate_state_store.blocks.rbegin();
             found != candidate_state_store.blocks.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
        }
        candidate_state_store.blocks.clear();
        for (auto found = committed_state_store.blocks.rbegin();
             found != committed_state_store.blocks.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
        }
        committed_state_store.blocks.clear();
        for (auto found = cycle_frame.slots.rbegin();
             found != cycle_frame.slots.rend(); ++found) {
            if (found->owns_address) {
                release_raw(
                    found->address,
                    found->materializer->operations().layout().alignment_bytes);
            }
        }
        cycle_frame.slots.clear();
        cycle_frame.construction_order.clear();
        for (auto found = runtime_bindings.cells.rbegin();
             found != runtime_bindings.cells.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
            release_raw(found->address, found->alignment);
        }
        runtime_bindings.cells.clear();
        for (auto found = preparations.rbegin();
             found != preparations.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
            release_raw(found->address, found->alignment);
        }
        preparations.clear();
        for (auto found = arenas.rbegin(); found != arenas.rend(); ++found) {
            if (found->layout != nullptr) {
                release_raw(found->address,
                            found->layout->alignment_bytes);
            }
        }
        arenas.clear();
    }

    [[nodiscard]] SessionResult read_committed(
        std::uint8_t authority_kind, std::uint32_t authority_handle,
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept override {
        result = {};
        if (state != SessionState::Initialized) {
            return {SessionError::InvalidLifecycleTransition,
                    state_block_handle,
                    "committed state requires Initialized Session"};
        }
        const auto found = std::find_if(
            committed_state_store.blocks.begin(),
            committed_state_store.blocks.end(),
            [state_block_handle](const auto& candidate) {
                return candidate.block != nullptr &&
                       candidate.block->handle == state_block_handle;
            });
        if (found == committed_state_store.blocks.end()) {
            return {SessionError::InvalidImageHandle, state_block_handle,
                    "committed state handle is unknown"};
        }
        bool authorized = false;
        if (authority_kind == static_cast<std::uint8_t>(
                                  SessionStateAuthorityKind::
                                      RuntimeComponent)) {
            const auto* component = find_handle(image->runtime_components(),
                                                authority_handle);
            authorized = component != nullptr &&
                         std::find(component->state_block_handles.begin(),
                                   component->state_block_handles.end(),
                                   state_block_handle) !=
                             component->state_block_handles.end();
        } else if (authority_kind == static_cast<std::uint8_t>(
                                         SessionStateAuthorityKind::
                                             IntegrationScope)) {
            const auto* scope = find_handle(image->integration_scopes(),
                                            authority_handle);
            authorized = scope != nullptr && found->block != nullptr &&
                         found->block->committed_slot_handle ==
                             scope->committed_state_slot_handle;
        }
        if (!authorized) {
            return {SessionError::StateAuthorizationFailure,
                    state_block_handle,
                    "committed state is outside the current authority"};
        }
        const auto layout = found->materializer->operations().layout();
        result = {found->address,
                  layout.size_bytes,
                  layout.alignment_bytes,
                  layout.type_identity,
                  SessionObjectRole::CommittedState,
                  found->block->handle,
                  found->initial->builder_entry_handle,
                  found->block->codec_entry_handle};
        return {};
    }

    [[nodiscard]] FrameSlot* frame_slot(
        std::uint32_t slot_handle) noexcept {
        const auto found = std::find_if(
            cycle_frame.slots.begin(), cycle_frame.slots.end(),
            [slot_handle](const auto& candidate) {
                return candidate.slot != nullptr &&
                       candidate.slot->handle == slot_handle;
            });
        return found == cycle_frame.slots.end() ? nullptr : &*found;
    }

    [[nodiscard]] const FrameSlot* frame_slot(
        std::uint32_t slot_handle) const noexcept {
        const auto found = std::find_if(
            cycle_frame.slots.begin(), cycle_frame.slots.end(),
            [slot_handle](const auto& candidate) {
                return candidate.slot != nullptr &&
                       candidate.slot->handle == slot_handle;
            });
        return found == cycle_frame.slots.end() ? nullptr : &*found;
    }

    [[nodiscard]] SessionResult validate_callsite_outputs(
        const contracts::PlanImageCallsite& callsite) noexcept {
        const auto require_present = [this, &callsite](
                                         std::uint32_t slot_handle)
            -> SessionResult {
            const auto* value = frame_slot(slot_handle);
            if (value == nullptr || !value->present ||
                value->generation != cycle_frame.generation) {
                return failure(SessionError::FrameSlotAbsent, slot_handle,
                               "callsite did not produce a required output");
            }
            return {};
        };
        for (const auto slot_handle : callsite.output_slot_handles) {
            auto result = require_present(slot_handle);
            if (!result) return result;
        }
        for (const auto invocation_handle :
             callsite.authorized_invocation_handles) {
            const auto* invocation = find_handle(image->invocations(),
                                                 invocation_handle);
            if (invocation == nullptr) {
                return failure(SessionError::InvalidImageStructure,
                               invocation_handle,
                               "callsite invocation authorization is invalid");
            }
            if (invocation->result_route !=
                contracts::InvocationResultRoute::HeldInterval) {
                continue;
            }
            auto result = require_present(
                invocation->result_storage_slot_handle);
            if (!result) return result;
        }
        return {};
    }

    [[nodiscard]] bool frame_active(
        std::uint64_t generation) const noexcept override {
        return cycle_frame.open && generation != 0U &&
               generation == cycle_frame.generation;
    }

    [[nodiscard]] SessionResult read_input(
        std::uint8_t authority_kind, std::uint32_t authority_handle,
        std::uint64_t generation,
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept override {
        result = {};
        if (!frame_active(generation)) {
            return {SessionError::StaleFrameView, slot_handle,
                    "input view generation is stale"};
        }
        const auto* slot = find_handle(image->slots(), slot_handle);
        bool authorized = false;
        if (authority_kind == static_cast<std::uint8_t>(
                                  SessionFrameAuthorityKind::
                                      RuntimeCallsite)) {
            const auto* callsite = find_handle(image->callsites(),
                                               authority_handle);
            authorized = callsite != nullptr && slot != nullptr &&
                         std::find(callsite->input_slot_handles.begin(),
                                   callsite->input_slot_handles.end(),
                                   slot_handle) !=
                             callsite->input_slot_handles.end();
        } else if (authority_kind == static_cast<std::uint8_t>(
                                         SessionFrameAuthorityKind::
                                             IntegrationScope)) {
            const auto* scope = find_handle(image->integration_scopes(),
                                            authority_handle);
            authorized = scope != nullptr && slot != nullptr &&
                         (slot_handle == scope->held_form_slot_handle ||
                          slot_handle ==
                              scope->form_preparation_slot_handle ||
                          std::find(slot->reader_handles.begin(),
                                    slot->reader_handles.end(),
                                    scope->handle) !=
                              slot->reader_handles.end() ||
                          std::find(slot->reader_handles.begin(),
                                    slot->reader_handles.end(),
                                    scope->derivative_callsite_handle) !=
                              slot->reader_handles.end());
        }
        if (!authorized) {
            return {SessionError::ReaderAuthorizationFailure, slot_handle,
                    "current execution authority cannot read slot"};
        }
        const auto* stored = frame_slot(slot_handle);
        if (stored == nullptr || !stored->present ||
            stored->generation != generation) {
            return {SessionError::FrameSlotAbsent, slot_handle,
                    "required frame input is absent"};
        }
        const auto layout = stored->materializer->operations().layout();
        result = {stored->address,
                  layout.size_bytes,
                  layout.alignment_bytes,
                  layout.type_identity,
                  role_for_slot(*stored->slot),
                  stored->slot->handle,
                  0U,
                  stored->slot->codec_entry_handle};
        return {};
    }

    [[nodiscard]] bool integration_writer_authorized(
        std::uint32_t callsite_handle,
        const contracts::PlanImageWriterToken& token) const noexcept {
        for (const auto& invocation : image->invocations()) {
            if (invocation.caller_callsite_handle != callsite_handle ||
                invocation.result_storage_slot_handle != token.slot_handle ||
                invocation.result_writer_token_handle != token.handle) {
                continue;
            }
            const auto* callsite = find_handle(image->callsites(),
                                               callsite_handle);
            return callsite != nullptr &&
                   std::find(callsite->authorized_invocation_handles.begin(),
                             callsite->authorized_invocation_handles.end(),
                             invocation.handle) !=
                       callsite->authorized_invocation_handles.end();
        }
        return false;
    }

    [[nodiscard]] SessionResult write_output(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t slot_handle, std::uint32_t writer_token_handle,
        InProcessValueView value) noexcept override {
        if (!frame_active(generation)) {
            return {SessionError::StaleFrameView, slot_handle,
                    "output writer generation is stale"};
        }
        const auto* callsite = find_handle(image->callsites(), callsite_handle);
        const auto* token = find_handle(image->writer_tokens(),
                                        writer_token_handle);
        auto* stored = frame_slot(slot_handle);
        if (callsite == nullptr || token == nullptr || stored == nullptr ||
            token->slot_handle != slot_handle) {
            return {SessionError::WriterAuthorizationFailure, slot_handle,
                    "writer token does not authorize this callsite and slot"};
        }
        const bool runtime_owned =
            token->owner_kind ==
                contracts::PlanImageWriterOwnerKind::RuntimeCallsite &&
            token->owner_handle == callsite_handle;
        const bool integration_owned =
            token->owner_kind ==
                contracts::PlanImageWriterOwnerKind::IntegrationCoordinator &&
            integration_writer_authorized(callsite_handle, *token);
        const bool declared_runtime_output =
            std::find(callsite->output_slot_handles.begin(),
                      callsite->output_slot_handles.end(), slot_handle) !=
                callsite->output_slot_handles.end() &&
            std::find(callsite->output_writer_token_handles.begin(),
                      callsite->output_writer_token_handles.end(),
                      writer_token_handle) !=
                callsite->output_writer_token_handles.end();
        if ((!runtime_owned || !declared_runtime_output) &&
            !integration_owned) {
            return {SessionError::WriterAuthorizationFailure, slot_handle,
                    "writer token owner mismatch"};
        }
        const auto layout = stored->materializer->operations().layout();
        if (value.object == nullptr) {
            return {SessionError::ObjectTypeMismatch, slot_handle,
                    "output value address is null"};
        }
        if (value.type_identity != layout.type_identity) {
            return {SessionError::ObjectTypeMismatch, slot_handle,
                    "output value type identity mismatch"};
        }
        if (value.size_bytes != layout.size_bytes) {
            return {SessionError::ObjectSizeMismatch, slot_handle,
                    "output value size mismatch"};
        }
        if (value.alignment_bytes != layout.alignment_bytes) {
            return {SessionError::ObjectAlignmentMismatch, slot_handle,
                    "output value alignment mismatch"};
        }
        if (!stored->materializer->operations().validate(value.object)) {
            return {SessionError::ObjectValidationFailed, slot_handle,
                    "output value validation failed"};
        }
        if (!stored->present) {
            if (!stored->materializer->operations().copy_construct(
                    value.object, stored->address)) {
                return {SessionError::SlotConstructionFailed, slot_handle,
                        "frame output construction failed"};
            }
            ConstructedObject guard(stored->address,
                                    stored->materializer->operations());
            if (!stored->materializer->operations().validate(stored->address)) {
                return {SessionError::ObjectValidationFailed, slot_handle,
                        "stored frame output validation failed"};
            }
            const auto index = static_cast<std::size_t>(
                stored - cycle_frame.slots.data());
            cycle_frame.construction_order.push_back(index);
            stored->present = true;
            guard.release();
        } else if (!stored->materializer->operations().replace(
                       stored->address, value.object) ||
                   !stored->materializer->operations().validate(
                       stored->address)) {
            return {SessionError::ObjectValidationFailed, slot_handle,
                    "frame output replacement failed"};
        }
        stored->generation = generation;
        stored->sequence = ++cycle_frame.sequence;
        stored->sample_tick = committed_tick;
        stored->sample_time_seconds =
            static_cast<double>(committed_tick) *
            image->clock().base_step_seconds;
        stored->interval_start_seconds = stored->sample_time_seconds;
        stored->interval_end_seconds = stored->sample_time_seconds +
                                       image->clock().base_step_seconds;
        stored->quality = contracts::DataQuality::Valid;
        ++cycle_frame.write_count;
        return {};
    }

    [[nodiscard]] bool transaction_active(
        std::uint64_t generation) const noexcept override {
        return frame_active(generation) && active_transaction_handle != 0U;
    }

    [[nodiscard]] SessionResult write_candidate(
        SessionCandidateProducerKind producer_kind,
        std::uint32_t producer_handle, std::uint64_t generation,
        std::uint32_t transaction_handle,
        std::uint32_t candidate_slot_handle,
        std::uint32_t writer_token_handle,
        InProcessValueView value) noexcept override {
        if (!transaction_active(generation) ||
            transaction_handle != active_transaction_handle) {
            return {SessionError::StaleFrameView, candidate_slot_handle,
                    "candidate writer generation is stale"};
        }
        const auto* transaction = find_handle(image->transactions(),
                                              transaction_handle);
        const auto* token = find_handle(image->writer_tokens(),
                                        writer_token_handle);
        const contracts::PlanImageTransactionCandidateMember* member = nullptr;
        if (transaction != nullptr) {
            const auto found_member = std::find_if(
                transaction->candidates.begin(),
                transaction->candidates.end(),
                [&](const auto& candidate) {
                    const bool kind_matches =
                        (producer_kind ==
                             SessionCandidateProducerKind::IntegrationScope &&
                         candidate.producer_kind == "IntegrationScope") ||
                        (producer_kind ==
                             SessionCandidateProducerKind::RuntimeCallsite &&
                         candidate.producer_kind == "RuntimeCallsite");
                    return kind_matches &&
                           candidate.producer_handle == producer_handle &&
                           candidate.candidate_state_slot_handle ==
                               candidate_slot_handle &&
                           candidate.writer_token_handle ==
                               writer_token_handle;
                });
            if (found_member != transaction->candidates.end()) {
                member = &*found_member;
            }
        }
        if (transaction == nullptr || token == nullptr ||
            member == nullptr ||
            token->slot_handle != candidate_slot_handle ||
            token->owner_handle != producer_handle ||
            (producer_kind ==
                     SessionCandidateProducerKind::IntegrationScope
                 ? token->owner_kind !=
                       contracts::PlanImageWriterOwnerKind::
                           IntegrationCoordinator
                 : token->owner_kind !=
                       contracts::PlanImageWriterOwnerKind::
                           RuntimeCallsite)) {
            return {SessionError::CandidateAuthorizationFailure,
                    candidate_slot_handle,
                    "candidate writer token or producer is unauthorized"};
        }
        const auto found = std::find_if(
            candidate_state_store.blocks.begin(),
            candidate_state_store.blocks.end(),
            [candidate_slot_handle](const auto& candidate) {
                return candidate.block != nullptr &&
                       candidate.block->candidate_slot_handle ==
                           candidate_slot_handle;
            });
        if (found == candidate_state_store.blocks.end()) {
            return {SessionError::InvalidImageHandle,
                    candidate_slot_handle,
                    "candidate slot has no state block"};
        }
        const auto layout = found->materializer->operations().layout();
        if (value.object == nullptr ||
            value.type_identity != layout.type_identity) {
            return {SessionError::ObjectTypeMismatch,
                    candidate_slot_handle,
                    "candidate value type mismatch"};
        }
        if (value.size_bytes != layout.size_bytes) {
            return {SessionError::ObjectSizeMismatch,
                    candidate_slot_handle,
                    "candidate value size mismatch"};
        }
        if (value.alignment_bytes != layout.alignment_bytes) {
            return {SessionError::ObjectAlignmentMismatch,
                    candidate_slot_handle,
                    "candidate value alignment mismatch"};
        }
        if (found->candidate_present ||
            !found->materializer->operations().replace(found->address,
                                                       value.object)) {
            return {SessionError::CandidateValidationFailed,
                    candidate_slot_handle,
                    "candidate value staging failed"};
        }
        found->candidate_present = true;
        found->candidate_generation = generation;
        found->candidate_base_epoch = committed_epoch;
        found->candidate_producer_handle = producer_handle;
        found->candidate_writer_token_handle = writer_token_handle;
        return {};
    }

    [[nodiscard]] bool history_ready(
        std::uint32_t callsite_handle) const noexcept {
        const auto found = std::find_if(
            image->evaluator_histories().begin(),
            image->evaluator_histories().end(),
            [callsite_handle](const auto& history) {
                return history.evaluator_callsite_handle == callsite_handle;
            });
        if (found == image->evaluator_histories().end()) {
            return true;
        }
        const auto* store = history_store(staged_evaluator_histories,
                                          found->handle);
        return store != nullptr &&
               store->samples.size() == found->history_depth;
    }

    [[nodiscard]] bool history_active(
        std::uint32_t callsite_handle,
        std::uint64_t generation) const noexcept override {
        if (!frame_active(generation)) return false;
        return std::any_of(
            image->evaluator_histories().begin(),
            image->evaluator_histories().end(),
            [callsite_handle](const auto& history) {
                return history.evaluator_callsite_handle ==
                       callsite_handle;
            });
    }

    [[nodiscard]] SessionResult history_info(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t history_handle,
        SessionCommittedHistoryInfo& result) const noexcept override {
        result = {};
        if (!frame_active(generation)) {
            return {SessionError::StaleFrameView, history_handle,
                    "committed history view generation is stale"};
        }
        const auto* plan = find_handle(image->evaluator_histories(),
                                       history_handle);
        const auto* store = history_store(staged_evaluator_histories,
                                          history_handle);
        if (plan == nullptr || store == nullptr ||
            plan->evaluator_callsite_handle != callsite_handle ||
            store->plan != plan) {
            return {SessionError::HistoryAuthorizationFailure,
                    history_handle,
                    "callsite cannot access this committed history"};
        }
        if (store->samples.empty() ||
            store->samples.size() > plan->history_depth) {
            return {SessionError::HistoryValidationFailed, history_handle,
                    "committed history sample count is invalid"};
        }
        result = {history_handle,
                  plan->history_depth,
                  store->samples.size(),
                  plan->ordered_members.size(),
                  store->samples.front().tick,
                  store->samples.back().tick};
        return {};
    }

    [[nodiscard]] SessionResult read_history_member(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t history_handle, std::size_t sample_index,
        std::size_t member_index, std::int64_t& sample_tick,
        SessionObjectIdentityView& result) const noexcept override {
        sample_tick = 0;
        result = {};
        SessionCommittedHistoryInfo info;
        const auto status = history_info(callsite_handle, generation,
                                         history_handle, info);
        if (!status) return status;
        const auto* plan = find_handle(image->evaluator_histories(),
                                       history_handle);
        const auto* store = history_store(staged_evaluator_histories,
                                          history_handle);
        if (sample_index >= store->samples.size() ||
            member_index >= plan->ordered_members.size()) {
            return {SessionError::HistoryAuthorizationFailure,
                    history_handle,
                    "committed history sample or member is outside authority"};
        }
        const auto& sample = store->samples[sample_index];
        if (sample.members.size() != plan->ordered_members.size()) {
            return {SessionError::HistoryValidationFailed, history_handle,
                    "committed history member count is invalid"};
        }
        const auto& member = sample.members[member_index];
        const auto& planned = plan->ordered_members[member_index];
        if (member.slot == nullptr || member.materializer == nullptr ||
            member.address == nullptr ||
            member.slot->handle != planned.committed_state_slot_handle ||
            member.slot->layout_id != planned.state_layout_id ||
            member.sample_tick != sample.tick ||
            member.quality != contracts::DataQuality::Valid ||
            !member.materializer->operations().validate(member.address)) {
            return {SessionError::HistoryValidationFailed, history_handle,
                    "committed history member identity is invalid"};
        }
        const auto layout = member.materializer->operations().layout();
        sample_tick = sample.tick;
        result = {member.address,
                  layout.size_bytes,
                  layout.alignment_bytes,
                  layout.type_identity,
                  SessionObjectRole::CommittedHistoryValue,
                  member.slot->handle,
                  0U,
                  member.slot->codec_entry_handle};
        return {};
    }

    [[nodiscard]] bool scheduled_now(
        const contracts::PlanImageRuntimeComponent& component) const noexcept {
        if (component.schedule_trigger == "TerminalSequenceReady") {
            return component.step_interval == 0U &&
                   committed_tick == image->clock().terminal_tick;
        }
        if (component.step_interval == 0U ||
            committed_tick < static_cast<std::int64_t>(component.offset)) {
            return false;
        }
        const auto relative = committed_tick -
                              static_cast<std::int64_t>(component.offset);
        return relative % static_cast<std::int64_t>(component.step_interval) ==
               0;
    }

    [[nodiscard]] SessionObjectIdentityView runtime_view(
        std::uint32_t component_handle) const noexcept {
        const auto* object = owned_object(runtime_bindings.cells,
                                          component_handle);
        return object == nullptr ? SessionObjectIdentityView{}
                                 : object_view(*object);
    }

    [[nodiscard]] SessionResult begin_frame(
        std::uint32_t transaction_handle) noexcept {
        if (cycle_frame.open) {
            return failure(SessionError::FrameAlreadyOpen, 0U,
                           "CycleFrame is already open");
        }
        ++cycle_frame.generation;
        if (cycle_frame.generation == 0U) {
            ++cycle_frame.generation;
        }
        cycle_frame.sequence = 0U;
        cycle_frame.write_count = 0U;
        cycle_frame.open = true;
        active_transaction_handle = transaction_handle;
        boundary_summary.generation = cycle_frame.generation;
        boundary_summary.output_write_count = 0U;
        boundary_summary.executed_callsite_handles.clear();
        boundary_summary.skipped_callsite_handles.clear();
        return {};
    }

    [[nodiscard]] SessionResult execute_boundary_calls() noexcept {
        const auto boundary_time =
            static_cast<double>(committed_tick) *
            image->clock().base_step_seconds;
        const auto interval_end = boundary_time +
                                  image->clock().base_step_seconds;
        for (const auto& scheduled : opening_schedule) {
            const auto* component = find_handle(
                image->runtime_components(), scheduled.component_handle);
            const auto* callsite = find_handle(
                image->callsites(), scheduled.callsite_handle);
            if (component == nullptr || callsite == nullptr) {
                return failure(SessionError::InvalidSchedule,
                               scheduled.callsite_handle,
                               "scheduled callsite disappeared");
            }
            if (!scheduled_now(*component) ||
                !history_ready(callsite->handle)) {
                boundary_summary.skipped_callsite_handles.push_back(
                    callsite->handle);
                continue;
            }
            const auto* entry = provider->invocation(callsite->handle);
            if (entry == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               callsite->handle,
                               "callsite invocation entry is missing");
            }
            const auto identity = entry->identity();
            if (identity.callsite_handle != callsite->handle ||
                identity.runtime_component_handle != component->handle ||
                identity.linked_entry_handle != callsite->entry_handle) {
                return failure(SessionError::InvalidMaterializerIdentity,
                               callsite->handle,
                               "callsite invocation identity mismatch");
            }
            const auto runtime = runtime_view(component->handle);
            if (!runtime) {
                return failure(SessionError::RuntimeCellFailed,
                               component->handle,
                               "scheduled Runtime Cell is unavailable");
            }
            SessionInvocationContext context(
                callsite->handle, component->handle, committed_tick,
                boundary_time, boundary_time, interval_end,
                contracts::DataQuality::Valid, runtime,
                SessionCommittedStateView(
                    this, SessionStateAuthorityKind::RuntimeComponent,
                    component->handle),
                SessionInputView(
                    this, SessionFrameAuthorityKind::RuntimeCallsite,
                    callsite->handle, cycle_frame.generation),
                SessionOutputWriterSet(this, callsite->handle,
                                       cycle_frame.generation),
                SessionCommittedHistoryView(
                    this, callsite->handle, cycle_frame.generation),
                SessionCandidateWriterSet(
                    nullptr,
                    SessionCandidateProducerKind::RuntimeCallsite,
                    callsite->handle, cycle_frame.generation, 0U));
            const auto result = entry->invoke(context);
            if (!result) {
                return failure(
                    result.error == SessionError::None
                        ? SessionError::InvocationFailed
                        : result.error,
                    result.image_handle == 0U ? callsite->handle
                                              : result.image_handle,
                    result.detail.empty() ? "callsite invocation failed"
                                          : result.detail);
            }
            const auto outputs = validate_callsite_outputs(*callsite);
            if (!outputs) {
                return last_result;
            }
            boundary_summary.executed_callsite_handles.push_back(
                callsite->handle);
        }
        boundary_summary.output_write_count = cycle_frame.write_count;
        return {};
    }
};

Session::Session(std::unique_ptr<Impl> implementation) noexcept
    : implementation_(std::move(implementation)) {}

Session::~Session() {
    if (implementation_ != nullptr) {
        implementation_->unwind();
    }
}

SessionState Session::state() const noexcept { return implementation_->state; }

const contracts::ExecutionPlanImage& Session::image() const noexcept {
    return *implementation_->image;
}

const SessionResult& Session::last_result() const noexcept {
    return implementation_->last_result;
}

const InitializationOutcome& Session::last_initialization_outcome()
    const noexcept {
    return implementation_->initialization_outcome;
}

const ResetOutcome& Session::last_reset_outcome() const noexcept {
    return implementation_->reset_outcome;
}

const StepOutcome& Session::last_step_outcome() const noexcept {
    return implementation_->step_outcome;
}

const RunId* Session::active_run_id() const noexcept {
    const auto& impl = *implementation_;
    return impl.state == SessionState::Initialized &&
                   impl.committed_run_id.has_value()
               ? &*impl.committed_run_id
               : nullptr;
}

const RunBinding* Session::active_run_binding() const noexcept {
    const auto& impl = *implementation_;
    return impl.state == SessionState::Initialized &&
                   impl.committed_run_binding.has_value()
               ? &*impl.committed_run_binding
               : nullptr;
}

const RunId* Session::last_committed_run_id() const noexcept {
    return implementation_->committed_run_id.has_value()
               ? &*implementation_->committed_run_id
               : nullptr;
}

const RunBinding* Session::last_committed_run_binding() const noexcept {
    return implementation_->committed_run_binding.has_value()
               ? &*implementation_->committed_run_binding
               : nullptr;
}

std::optional<std::uint64_t> Session::run_sequence() const noexcept {
    const auto& impl = *implementation_;
    return impl.has_committed_run
               ? std::optional<std::uint64_t>{impl.committed_run_sequence}
               : std::nullopt;
}

const RunOutcome* Session::run_outcome() const noexcept {
    return implementation_->run_outcome_frozen
               ? implementation_->current_run_outcome
               : nullptr;
}

const RunOutcome* Session::run_outcome_for_sequence(
    std::uint64_t run_sequence) const noexcept {
    const auto& impl = *implementation_;
    const auto found = std::find_if(
        impl.run_outcome_storages.begin(),
        impl.run_outcome_storages.end(),
        [run_sequence](const auto& outcome) {
            return outcome != nullptr &&
                   outcome->run_sequence == run_sequence;
        });
    if (found == impl.run_outcome_storages.end() ||
        (found->get() == impl.current_run_outcome &&
         !impl.run_outcome_frozen)) {
        return nullptr;
    }
    return found->get();
}

const SessionBoundarySummary& Session::last_boundary_summary() const noexcept {
    return implementation_->boundary_summary;
}

const SessionStepSummary& Session::last_step_summary() const noexcept {
    return implementation_->step_summary;
}

const SessionStepJournal& Session::last_step_journal() const noexcept {
    return implementation_->step_summary;
}

InitializationOutcome Session::initialize(
    InitializationRequest request) noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Created) {
        const auto result = impl.failure(
            SessionError::InvalidLifecycleTransition, 0U,
            "initialize requires Created Session");
        auto diagnostic = impl.make_diagnostic(
            result, RuntimeDiagnosticStage::Lifecycle,
            impl.run_outcome_frozen && impl.current_run_outcome != nullptr
                ? impl.current_run_outcome->validity
                : contracts::EvidenceValidity::Unknown);
        diagnostic.run_id = request.run_id;
        InitializationOutcome rejected;
        rejected.status = InitializationStatus::Failed;
        rejected.result = result;
        rejected.run_id = request.run_id;
        rejected.proposed_run_sequence = 0U;
        rejected.binding_matched =
            binding_matches_image(request.binding, *impl.image);
        rejected.committed_epoch = impl.committed_epoch;
        rejected.committed_tick = impl.committed_tick;
        rejected.primary_diagnostic = diagnostic;
        return rejected;
    }
    impl.pending_run_attempt.emplace(std::move(request));
    if (impl.pending_run_attempt->run_id.empty()) {
        const auto result = impl.failure(
            SessionError::EmptyRunId, 0U,
            "initialization requires a non-empty caller RunId");
        return impl.fail_initialization(
            result, RuntimeDiagnosticStage::InitializationRequest);
    }
    if (!binding_matches_image(impl.pending_run_attempt->binding,
                               *impl.image)) {
        const auto result = impl.failure(
            SessionError::RunBindingMismatch, 0U,
            "RunBinding does not match the Session Image");
        return impl.fail_initialization(
            result, RuntimeDiagnosticStage::InitializationRequest);
    }
    try {
        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::InitializationValidation;
        auto result = impl.validate_image();
        if (result) {
            impl.current_diagnostic_stage =
                RuntimeDiagnosticStage::Materialization;
            impl.reserve_tracking();
            result = impl.allocate_arenas();
        }
        if (result) {
            for (const auto handle :
                 impl.image->lifecycle().preparation_handles) {
                const auto* preparation = find_handle(
                    impl.image->preparations(), handle);
                const auto* materializer = impl.provider->preparation(handle);
                const Impl::ScopedObjectAccess no_dependencies(impl,
                                                               nullptr);
                result = impl.construct_owned(
                    handle, *materializer, no_dependencies,
                    impl.preparations,
                    SessionObjectRole::PreparedModel,
                    preparation->prepare_entry_handle,
                    SessionError::PreparationFailed);
                if (!result) break;
            }
        }
        if (result) {
            for (const auto handle :
                 impl.image->lifecycle().runtime_component_handles) {
                const auto* component = find_handle(
                    impl.image->runtime_components(), handle);
                const auto* materializer =
                    impl.provider->runtime_component(handle);
                result = impl.validate_runtime_dependencies(*materializer,
                                                            handle);
                if (result) {
                    const Impl::ScopedObjectAccess dependencies(
                        impl, &component->preparation_handles);
                    result = impl.construct_owned(
                        handle, *materializer, dependencies,
                        impl.runtime_bindings.cells,
                        SessionObjectRole::RuntimeCell,
                        component->runtime_cell_factory_entry_handle,
                        SessionError::RuntimeCellFailed);
                }
                if (!result) break;
            }
        }
        if (result) result = impl.prepare_frame_slots();
        if (result) {
            impl.current_diagnostic_stage =
                RuntimeDiagnosticStage::InitialState;
            result = impl.construct_states();
        }
        if (result) {
            impl.current_diagnostic_stage = RuntimeDiagnosticStage::History;
            result = impl.prepare_evaluator_histories();
        }
        if (!result) {
            return impl.fail_initialization(
                impl.last_result, impl.current_diagnostic_stage);
        }
        impl.committed_tick = impl.image->clock().initial_tick;
        impl.committed_run_id = impl.pending_run_attempt->run_id;
        impl.committed_run_binding.emplace(
            std::move(impl.pending_run_attempt->binding));
        impl.committed_run_sequence = 0U;
        impl.has_committed_run = true;
        impl.committed_step_count = 0U;
        impl.state = SessionState::Initialized;
        impl.last_result = {};
        impl.prepare_run_start(*impl.current_run_outcome,
                               *impl.committed_run_id, 0U,
                               RunStartKind::Initialize, true,
                               impl.committed_epoch);
        impl.run_outcome_frozen = false;
        impl.initialization_outcome = {};
        impl.initialization_outcome.status =
            InitializationStatus::Committed;
        impl.initialization_outcome.result = {};
        impl.initialization_outcome.run_id = *impl.committed_run_id;
        impl.initialization_outcome.proposed_run_sequence = 0U;
        impl.initialization_outcome.binding_matched = true;
        impl.initialization_outcome.initialization_commit = true;
        impl.initialization_outcome.committed_epoch = impl.committed_epoch;
        impl.initialization_outcome.committed_tick = impl.committed_tick;
        impl.pending_run_attempt.reset();
        return impl.initialization_outcome;
    } catch (const std::bad_alloc&) {
        const auto result = impl.failure(
            SessionError::AllocationFailure, 0U,
            "Session initialization allocation failed");
        return impl.fail_initialization(
            result, impl.current_diagnostic_stage);
    } catch (...) {
        const auto result = impl.failure(
            SessionError::InternalFailure, 0U,
            "Session initialization failed unexpectedly");
        return impl.fail_initialization(
            result, impl.current_diagnostic_stage);
    }
}

ResetOutcome Session::reset(ResetRequest request) noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Completed) {
        const auto result = impl.failure(
            SessionError::InvalidLifecycleTransition, 0U,
            "reset requires Completed Session");
        auto diagnostic = impl.make_diagnostic(
            result, RuntimeDiagnosticStage::Lifecycle,
            impl.run_outcome_frozen && impl.current_run_outcome != nullptr
                ? impl.current_run_outcome->validity
                : contracts::EvidenceValidity::Unknown);
        diagnostic.run_id = request.run_id;
        impl.reset_outcome = {};
        impl.reset_outcome.status = ResetStatus::Failed;
        impl.reset_outcome.result = result;
        impl.reset_outcome.run_id = request.run_id;
        impl.reset_outcome.proposed_run_sequence =
            impl.has_committed_run &&
                    impl.committed_run_sequence !=
                        (std::numeric_limits<std::uint64_t>::max)()
                ? impl.committed_run_sequence + 1U
                : 0U;
        impl.reset_outcome.binding_matched =
            binding_matches_image(request.binding, *impl.image);
        impl.reset_outcome.reset_commit = false;
        impl.reset_outcome.committed_epoch = impl.committed_epoch;
        impl.reset_outcome.committed_tick = impl.committed_tick;
        impl.reset_outcome.primary_diagnostic = diagnostic;
        return impl.reset_outcome;
    }

    impl.pending_reset_run_sequence =
        impl.committed_run_sequence ==
                (std::numeric_limits<std::uint64_t>::max)()
            ? impl.committed_run_sequence
            : impl.committed_run_sequence + 1U;
    impl.pending_reset_attempt.emplace(std::move(request));
    if (impl.pending_reset_attempt->run_id.empty()) {
        return impl.fail_reset(
            impl.failure(SessionError::EmptyRunId, 0U,
                         "reset requires a non-empty caller RunId"),
            RuntimeDiagnosticStage::ResetRequest);
    }
    if (impl.run_id_seen(impl.pending_reset_attempt->run_id)) {
        return impl.fail_reset(
            impl.failure(SessionError::DuplicateRunId, 0U,
                         "reset RunId is already known to this Session"),
            RuntimeDiagnosticStage::ResetRequest);
    }
    if (!binding_matches_image(impl.pending_reset_attempt->binding,
                               *impl.image)) {
        return impl.fail_reset(
            impl.failure(SessionError::RunBindingMismatch, 0U,
                         "reset RunBinding does not match the Session Image"),
            RuntimeDiagnosticStage::ResetRequest);
    }
    if (impl.committed_run_sequence ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        impl.committed_epoch ==
            (std::numeric_limits<std::uint64_t>::max)()) {
        return impl.fail_reset(
            impl.failure(SessionError::ResetPrecommitFailed, 0U,
                         "reset sequence or epoch cannot advance"),
            RuntimeDiagnosticStage::ResetPrecommit);
    }
    const auto reset_eligibility = impl.validate_reset_capabilities();
    if (!reset_eligibility) {
        return impl.fail_reset(
            reset_eligibility, RuntimeDiagnosticStage::ResetPrecommit);
    }

    std::string_view allocation_detail =
        "reset outcome staging allocation failed";
    try {
        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::ResetPrecommit;
        impl.run_outcome_storages.reserve(
            impl.run_outcome_storages.size() + 2U);
        auto next_outcome = std::make_unique<RunOutcome>();
        impl.prepare_outcome_identity(*next_outcome);
        impl.prepare_run_start(
            *next_outcome, impl.pending_reset_attempt->run_id,
            impl.pending_reset_run_sequence, RunStartKind::Reset, true,
            impl.committed_epoch + 1U);
        auto next_failure_outcome = std::make_unique<RunOutcome>();
        impl.prepare_outcome_identity(*next_failure_outcome);

        allocation_detail = "reset state staging allocation failed";
        std::vector<Impl::ResetStateReplacement> replacements;
        replacements.reserve(impl.committed_state_store.blocks.size());
        impl.current_diagnostic_stage = RuntimeDiagnosticStage::ResetState;
        auto result = impl.stage_reset_states(replacements);
        if (!result) {
            return impl.fail_reset(impl.last_result,
                                   impl.current_diagnostic_stage);
        }

        allocation_detail = "reset history staging allocation failed";
        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::ResetPrecommit;
        std::vector<Impl::EvaluatorHistoryStore> histories;
        impl.prepare_reset_histories(histories);
        Impl::SealedBoundaryStore empty_seal;
        empty_seal.outputs.reserve(impl.image->slots().size());

        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::ResetPrecommit;
        result = impl.validate_reset_precommit(replacements, histories);
        if (!result || impl.reset_failure_outcome_storage == nullptr ||
            impl.run_outcome_storages.size() >=
                impl.run_outcome_storages.capacity()) {
            if (result) {
                result = impl.failure(
                    SessionError::ResetPrecommitFailed, 0U,
                    "reset outcome retention precheck failed");
            }
            return impl.fail_reset(result,
                                   RuntimeDiagnosticStage::ResetPrecommit);
        }

        RunId next_run_id = impl.pending_reset_attempt->run_id;
        RunBinding next_binding =
            std::move(impl.pending_reset_attempt->binding);
        impl.current_run_outcome = next_outcome.get();
        impl.run_outcome_storages.push_back(std::move(next_outcome));
        impl.reset_failure_outcome_storage =
            std::move(next_failure_outcome);

        for (std::size_t index = 0U; index < replacements.size(); ++index) {
            auto& replacement = replacements[index];
            auto& committed = impl.committed_state_store.blocks[index];
            auto* candidate = impl.candidate_for_slot(
                committed.block->candidate_slot_handle);
            replacement.materializer->operations().nofail_swap(
                committed.address, replacement.committed_address);
            replacement.materializer->operations().nofail_swap(
                candidate->address, replacement.candidate_address);
        }

        ++impl.committed_epoch;
        impl.committed_tick = impl.image->clock().initial_tick;
        for (auto& committed : impl.committed_state_store.blocks) {
            committed.committed_epoch = impl.committed_epoch;
        }
        for (auto& candidate : impl.candidate_state_store.blocks) {
            candidate.committed_epoch = impl.committed_epoch;
        }
        impl.evaluator_histories.swap(histories);
        impl.sealed_boundary.outputs.swap(empty_seal.outputs);
        impl.sealed_boundary.committed_epoch = 0U;
        impl.sealed_boundary.committed_tick =
            impl.image->clock().initial_tick;
        impl.committed_run_id->swap(next_run_id);
        impl.committed_run_binding->swap(next_binding);
        impl.committed_run_sequence = impl.pending_reset_run_sequence;
        impl.committed_step_count = 0U;
        impl.clear_run_journals_noexcept();
        impl.run_outcome_frozen = false;
        impl.has_committed_run = true;
        impl.state = SessionState::Initialized;
        impl.last_result = {};
        impl.current_diagnostic_stage = RuntimeDiagnosticStage::Lifecycle;

        impl.reset_outcome = {};
        impl.reset_outcome.status = ResetStatus::Committed;
        impl.reset_outcome.result = {};
        impl.reset_outcome.run_id = *impl.committed_run_id;
        impl.reset_outcome.proposed_run_sequence =
            impl.committed_run_sequence;
        impl.reset_outcome.binding_matched = true;
        impl.reset_outcome.reset_commit = true;
        impl.reset_outcome.committed_epoch = impl.committed_epoch;
        impl.reset_outcome.committed_tick = impl.committed_tick;
        impl.pending_reset_attempt.reset();
        return impl.reset_outcome;
    } catch (const std::bad_alloc&) {
        return impl.fail_reset(
            impl.failure(SessionError::AllocationFailure, 0U,
                         allocation_detail),
            impl.current_diagnostic_stage);
    } catch (...) {
        return impl.fail_reset(
            impl.failure(SessionError::InternalFailure, 0U,
                         "reset staging failed unexpectedly"),
            impl.current_diagnostic_stage);
    }
}

SessionResult Session::dispose() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Created &&
        impl.state != SessionState::Completed &&
        impl.state != SessionState::Failed) {
        return impl.failure(
            SessionError::InvalidLifecycleTransition, 0U,
            "dispose requires Created, Completed, or Failed Session");
    }
    impl.unwind();
    impl.state = SessionState::Disposed;
    impl.last_result = {};
    return {};
}

SessionResult Session::qualification_execute_opening_boundary() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        return impl.failure(SessionError::InvalidLifecycleTransition, 0U,
                            "opening boundary requires Initialized Session");
    }
    try {
        auto result = impl.begin_frame(0U);
        if (result) result = impl.execute_boundary_calls();
        if (!result) {
            impl.close_frame();
            return impl.last_result;
        }
        impl.close_frame();
        impl.last_result = {};
        return {};
    } catch (const std::bad_alloc&) {
        impl.close_frame();
        return impl.failure(SessionError::AllocationFailure, 0U,
                            "opening boundary allocation failed");
    } catch (...) {
        impl.close_frame();
        return impl.failure(SessionError::InternalFailure, 0U,
                            "opening boundary failed unexpectedly");
    }
}

StepOutcome Session::execute_step() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        const auto result = impl.failure(
            SessionError::InvalidLifecycleTransition, 0U,
            "step execution requires Initialized Session");
        const auto diagnostic = impl.make_diagnostic(
            result, RuntimeDiagnosticStage::Lifecycle,
            impl.run_outcome_frozen && impl.current_run_outcome != nullptr
                ? impl.current_run_outcome->validity
                : contracts::EvidenceValidity::Unknown);
        impl.step_outcome = impl.make_lifecycle_rejection_outcome(
            result, diagnostic);
        return impl.step_outcome;
    }
    impl.current_diagnostic_stage = RuntimeDiagnosticStage::Schedule;
    if (impl.cycle_frame.open) {
        return impl.fail_execution(SessionError::FrameAlreadyOpen, 0U,
                                   "CycleFrame is already open");
    }
    if (impl.committed_tick > impl.image->clock().terminal_tick) {
        return impl.fail_execution(
            SessionError::InvalidLifecycleTransition,
            impl.image->clock().handle,
            "committed tick exceeds the Image terminal tick");
    }
    const auto& transaction = impl.image->transactions().front();
    const auto branch_kind =
        impl.committed_tick < impl.image->clock().terminal_tick
            ? contracts::TransactionBranch::Continue
            : contracts::TransactionBranch::Terminal;
    const auto* branch = contracts::find_transaction_branch(
        transaction, branch_kind);
    if (branch == nullptr) {
        return impl.fail_execution(
            SessionError::InvalidImageStructure, transaction.handle,
            "selected transaction branch is missing");
    }
    impl.step_summary.transaction_handle = transaction.handle;
    impl.step_summary.branch = branch_kind;
    impl.step_summary.branch_selected = true;
    impl.step_summary.generation = 0U;
    impl.step_summary.base_epoch = impl.committed_epoch;
    impl.step_summary.committed_epoch = impl.committed_epoch;
    impl.step_summary.base_tick = impl.committed_tick;
    impl.step_summary.committed_tick = impl.committed_tick;
    impl.step_summary.output_write_count = 0U;
    impl.step_summary.committed = false;
    impl.step_summary.history_staged = false;
    impl.step_summary.observation_seal_staged = false;
    impl.step_summary.result_seal_staged = false;
    impl.step_summary.terminal_result_present = false;
    impl.step_summary.prevalidated = false;
    impl.step_summary.primary_failure = {};
    impl.step_summary.executed_callsite_handles.clear();
    impl.step_summary.skipped_callsite_handles.clear();
    impl.step_summary.integration_scope_handles.clear();
    impl.step_summary.candidate_slot_handles.clear();
    impl.step_summary.candidates.clear();
    impl.step_summary.histories.clear();
    impl.step_summary.seals.clear();
    try {
        impl.current_diagnostic_stage = RuntimeDiagnosticStage::Schedule;
        auto result = impl.begin_frame(transaction.handle);
        if (!result) {
            return impl.fail_execution(result, transaction.handle,
                                       "CycleFrame open failed");
        }
        impl.step_summary.generation = impl.cycle_frame.generation;
        impl.current_diagnostic_stage = RuntimeDiagnosticStage::History;
        result = impl.stage_histories();
        if (!result) {
            return impl.fail_execution(result, transaction.handle,
                                       "committed history staging failed");
        }
        if (branch_kind == contracts::TransactionBranch::Continue) {
            impl.current_diagnostic_stage =
                RuntimeDiagnosticStage::CandidateProduction;
            result = impl.rearm_candidates(transaction);
            if (!result) {
                return impl.fail_execution(result, transaction.handle,
                                           "candidate rearm failed");
            }
        }
        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::BoundaryInvocation;
        result = impl.execute_boundary_calls();
        impl.step_summary.executed_callsite_handles =
            impl.boundary_summary.executed_callsite_handles;
        impl.step_summary.skipped_callsite_handles =
            impl.boundary_summary.skipped_callsite_handles;
        impl.step_summary.output_write_count = impl.cycle_frame.write_count;
        if (!result) {
            return impl.fail_execution(result, transaction.handle,
                                       "boundary execution failed");
        }

        if (branch_kind == contracts::TransactionBranch::Continue) {
            impl.current_diagnostic_stage =
                RuntimeDiagnosticStage::CandidateProduction;
            const auto interval_start =
                static_cast<double>(impl.committed_tick) *
                impl.image->clock().base_step_seconds;
            const auto interval_end = interval_start +
                                      impl.image->clock().base_step_seconds;
            for (const auto& member : transaction.candidates) {
                if (member.producer_kind == "IntegrationScope") {
                    const auto* scope = find_handle(
                        impl.image->integration_scopes(),
                        member.producer_handle);
                    const auto component_handle =
                        scope == nullptr
                            ? 0U
                            : impl.owner_component_for_occurrence(
                                  scope->owner_occurrence_handle);
                    const auto* entry = impl.provider->integration(
                        member.producer_handle);
                    const auto runtime = impl.runtime_view(component_handle);
                    if (scope == nullptr || component_handle == 0U ||
                        entry == nullptr || !runtime) {
                        return impl.fail_execution(
                            SessionError::MissingMaterializer,
                            member.producer_handle,
                            "IntegrationScope execution dependency is missing");
                    }
                    const auto identity = entry->identity();
                    if (identity.integration_scope_handle != scope->handle ||
                        identity.runtime_component_handle !=
                            component_handle) {
                        return impl.fail_execution(
                            SessionError::InvalidMaterializerIdentity,
                            scope->handle,
                            "IntegrationScope execution identity mismatch");
                    }
                    SessionIntegrationContext context(
                        scope->handle, component_handle,
                        transaction.handle, impl.committed_tick,
                        interval_start, interval_end, runtime,
                        SessionCommittedStateView(
                            &impl,
                            SessionStateAuthorityKind::IntegrationScope,
                            scope->handle),
                        SessionInputView(
                            &impl,
                            SessionFrameAuthorityKind::IntegrationScope,
                            scope->handle,
                            impl.cycle_frame.generation),
                        SessionCandidateWriterSet(
                            &impl,
                            SessionCandidateProducerKind::IntegrationScope,
                            scope->handle,
                            impl.cycle_frame.generation,
                            transaction.handle));
                    result = entry->integrate(context);
                    if (result) {
                        impl.step_summary.integration_scope_handles.push_back(
                            scope->handle);
                    }
                } else {
                    const auto* callsite = find_handle(
                        impl.image->callsites(), member.producer_handle);
                    const auto* component =
                        impl.component_for_callsite(member.producer_handle);
                    const auto* entry = impl.provider->invocation(
                        member.producer_handle);
                    const auto runtime =
                        component == nullptr
                            ? SessionObjectIdentityView{}
                            : impl.runtime_view(component->handle);
                    if (callsite == nullptr || component == nullptr ||
                        entry == nullptr || !runtime) {
                        return impl.fail_execution(
                            SessionError::MissingMaterializer,
                            member.producer_handle,
                            "interval evolution dependency is missing");
                    }
                    SessionInvocationContext context(
                        callsite->handle, component->handle,
                        impl.committed_tick, interval_start,
                        interval_start, interval_end,
                        contracts::DataQuality::Valid, runtime,
                        SessionCommittedStateView(
                            &impl,
                            SessionStateAuthorityKind::RuntimeComponent,
                            component->handle),
                        SessionInputView(
                            &impl,
                            SessionFrameAuthorityKind::RuntimeCallsite,
                            callsite->handle,
                            impl.cycle_frame.generation),
                        SessionOutputWriterSet(
                            nullptr, callsite->handle,
                            impl.cycle_frame.generation),
                        SessionCommittedHistoryView(
                            nullptr, callsite->handle,
                            impl.cycle_frame.generation),
                        SessionCandidateWriterSet(
                            &impl,
                            SessionCandidateProducerKind::RuntimeCallsite,
                            callsite->handle,
                            impl.cycle_frame.generation,
                            transaction.handle));
                    result = entry->invoke(context);
                    if (result) {
                        impl.step_summary.executed_callsite_handles.push_back(
                            callsite->handle);
                    }
                }
                if (!result) {
                    return impl.fail_execution(
                        result, member.producer_handle,
                        "candidate producer failed");
                }
                impl.step_summary.candidate_slot_handles.push_back(
                    member.candidate_state_slot_handle);
            }
        }

        impl.current_diagnostic_stage =
            RuntimeDiagnosticStage::ObservationSeal;
        result = impl.stage_seals(*branch);
        if (!result) {
            return impl.fail_execution(result, transaction.handle,
                                       "observation seal staging failed");
        }
        impl.current_diagnostic_stage = RuntimeDiagnosticStage::Precommit;
        result = impl.validate_precommit(transaction, *branch);
        if (!result) {
            return impl.fail_execution(result, transaction.handle,
                                       "transaction prevalidation failed");
        }
        impl.commit_transaction_noexcept(*branch);
        ++impl.committed_step_count;
        impl.step_summary.committed = true;
        impl.step_summary.committed_epoch = impl.committed_epoch;
        impl.step_summary.committed_tick = impl.committed_tick;
        const auto terminal =
            branch_kind == contracts::TransactionBranch::Terminal;
        impl.close_frame();
        impl.last_result = {};
        impl.step_outcome = impl.make_step_outcome(
            terminal ? StepStatus::Terminated : StepStatus::Committed, {});
        if (terminal) {
            impl.state = SessionState::Completed;
            impl.current_diagnostic_stage =
                RuntimeDiagnosticStage::Finalization;
            impl.freeze_completed_run();
        }
        return impl.step_outcome;
    } catch (const std::bad_alloc&) {
        return impl.fail_execution(SessionError::AllocationFailure,
                                   transaction.handle,
                                   "step transaction allocation failed");
    } catch (...) {
        return impl.fail_execution(SessionError::InternalFailure,
                                   transaction.handle,
                                   "step transaction failed unexpectedly");
    }
}

SessionResult Session::run_to_terminal() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        return impl.failure(
            SessionError::InvalidLifecycleTransition, 0U,
            "run_to_terminal requires an Initialized Session");
    }
    while (impl.state == SessionState::Initialized) {
        const auto step = execute_step();
        if (!step) return step.result;
        if (step.status == StepStatus::Terminated) return {};
    }
    return impl.failure(SessionError::InternalFailure, 0U,
                        "run_to_terminal left the executable lifecycle");
}

std::size_t Session::preparation_count() const noexcept {
    return has_materialized_storage(state())
               ? implementation_->preparations.size()
               : 0U;
}

std::size_t Session::runtime_cell_count() const noexcept {
    return has_materialized_storage(state())
               ? implementation_->runtime_bindings.cells.size()
               : 0U;
}

std::size_t Session::committed_state_count() const noexcept {
    return has_materialized_storage(state())
               ? implementation_->committed_state_store.blocks.size()
               : 0U;
}

std::vector<std::uint32_t> Session::preparation_handles() const {
    std::vector<std::uint32_t> handles;
    if (!has_materialized_storage(state())) return handles;
    handles.reserve(implementation_->preparations.size());
    for (const auto& object : implementation_->preparations) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::runtime_component_handles() const {
    std::vector<std::uint32_t> handles;
    if (!has_materialized_storage(state())) return handles;
    handles.reserve(implementation_->runtime_bindings.cells.size());
    for (const auto& object : implementation_->runtime_bindings.cells) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::committed_state_block_handles() const {
    std::vector<std::uint32_t> handles;
    if (!has_materialized_storage(state())) return handles;
    handles.reserve(implementation_->committed_state_store.blocks.size());
    for (const auto& object : implementation_->committed_state_store.blocks) {
        handles.push_back(object.block->handle);
    }
    return handles;
}

std::vector<SessionStorageExtent> Session::storage_extents() const {
    std::vector<SessionStorageExtent> extents;
    if (!has_materialized_storage(state())) return extents;
    extents.reserve(implementation_->arenas.size());
    for (const auto& arena : implementation_->arenas) {
        extents.push_back({arena.layout->handle, arena.layout->storage_class,
                           arena.layout->size_bytes,
                           arena.layout->alignment_bytes});
    }
    return extents;
}

std::vector<SessionStateBlockInfo> Session::state_blocks() const {
    std::vector<SessionStateBlockInfo> result;
    if (!has_materialized_storage(state())) return result;
    result.reserve(implementation_->committed_state_store.blocks.size());
    for (const auto& object : implementation_->committed_state_store.blocks) {
        result.push_back({object.block->handle,
                          object.owner_runtime_component_handle,
                          object.block->committed_slot_handle,
                          object.block->candidate_slot_handle,
                          object.block->codec_entry_handle,
                          object.committed_epoch});
    }
    return result;
}

std::vector<SessionFrameSlotInfo> Session::frame_slots() const {
    std::vector<SessionFrameSlotInfo> result;
    if (!has_materialized_storage(state())) return result;
    result.reserve(implementation_->cycle_frame.slots.size());
    for (const auto& slot : implementation_->cycle_frame.slots) {
        result.push_back({slot.slot->handle,
                          slot.present,
                          slot.generation,
                          slot.sequence,
                          slot.sample_tick,
                          slot.sample_time_seconds,
                          slot.interval_start_seconds,
                          slot.interval_end_seconds,
                          slot.quality});
    }
    return result;
}

std::vector<SessionCommittedOutputInfo> Session::committed_outputs() const {
    std::vector<SessionCommittedOutputInfo> result;
    if (!has_materialized_storage(state())) return result;
    result.reserve(implementation_->sealed_boundary.outputs.size());
    for (const auto& output :
         implementation_->sealed_boundary.outputs) {
        if (output.slot == nullptr || output.materializer == nullptr ||
            output.address == nullptr) {
            continue;
        }
        result.push_back(
            {output.slot->handle,
             output.slot->codec_entry_handle,
             true,
             output.generation,
             output.sequence,
             output.sample_tick,
             output.sample_time_seconds,
             output.interval_start_seconds,
             output.interval_end_seconds,
             output.quality,
             output.slot->storage_class ==
                 contracts::SlotStorageClass::TerminalResult});
    }
    return result;
}

std::vector<SessionCommittedHistoryInfo>
Session::committed_histories() const {
    std::vector<SessionCommittedHistoryInfo> result;
    if (!has_materialized_storage(state())) return result;
    result.reserve(implementation_->evaluator_histories.size());
    for (const auto& history : implementation_->evaluator_histories) {
        if (history.plan == nullptr) continue;
        result.push_back(
            {history.plan->handle,
             history.plan->history_depth,
             history.samples.size(),
             history.plan->ordered_members.size(),
             history.samples.empty() ? 0 : history.samples.front().tick,
             history.samples.empty() ? 0 : history.samples.back().tick});
    }
    return result;
}

SessionResult Session::qualification_read_committed(
    std::uint32_t state_block_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (!has_materialized_storage(state())) {
        return {SessionError::InvalidLifecycleTransition,
                state_block_handle,
                "committed inspection requires a materialized Session"};
    }
    const auto found = std::find_if(
        implementation_->committed_state_store.blocks.begin(),
        implementation_->committed_state_store.blocks.end(),
        [state_block_handle](const auto& committed) {
            return committed.block != nullptr &&
                   committed.block->handle == state_block_handle;
        });
    if (found == implementation_->committed_state_store.blocks.end()) {
        return {SessionError::InvalidImageHandle, state_block_handle,
                "committed state handle is unknown"};
    }
    const auto layout = found->materializer->operations().layout();
    result = {found->address,
              layout.size_bytes,
              layout.alignment_bytes,
              layout.type_identity,
              SessionObjectRole::CommittedState,
              found->block->handle,
              found->initial->builder_entry_handle,
              found->block->codec_entry_handle};
    return {};
}

SessionResult Session::qualification_read_candidate(
    std::uint32_t state_block_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (state() != SessionState::Initialized) {
        return {SessionError::InvalidLifecycleTransition,
                state_block_handle,
                "candidate inspection requires Initialized Session"};
    }
    const auto found = std::find_if(
        implementation_->candidate_state_store.blocks.begin(),
        implementation_->candidate_state_store.blocks.end(),
        [state_block_handle](const auto& candidate) {
            return candidate.block != nullptr &&
                   candidate.block->handle == state_block_handle;
        });
    if (found == implementation_->candidate_state_store.blocks.end()) {
        return {SessionError::InvalidImageHandle, state_block_handle,
                "candidate state handle is unknown"};
    }
    const auto layout = found->materializer->operations().layout();
    result = {found->address,
              layout.size_bytes,
              layout.alignment_bytes,
              layout.type_identity,
              SessionObjectRole::CandidateState,
              found->block->handle,
              found->initial->builder_entry_handle,
              found->block->codec_entry_handle};
    return {};
}

SessionResult Session::qualification_read_committed_output(
    std::uint32_t slot_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (!has_materialized_storage(state())) {
        return {SessionError::InvalidLifecycleTransition, slot_handle,
                "sealed output inspection requires a materialized Session"};
    }
    const auto found = std::find_if(
        implementation_->sealed_boundary.outputs.begin(),
        implementation_->sealed_boundary.outputs.end(),
        [slot_handle](const auto& output) {
            return output.slot != nullptr &&
                   output.slot->handle == slot_handle;
        });
    if (found == implementation_->sealed_boundary.outputs.end() ||
        found->materializer == nullptr || found->address == nullptr) {
        return {SessionError::FrameSlotAbsent, slot_handle,
                "sealed output is absent"};
    }
    const auto layout = found->materializer->operations().layout();
    if (!found->materializer->operations().validate(found->address)) {
        return {SessionError::ObservationSealFailed, slot_handle,
                "sealed output validation failed"};
    }
    result = {found->address,
              layout.size_bytes,
              layout.alignment_bytes,
              layout.type_identity,
              found->slot->storage_class ==
                      contracts::SlotStorageClass::TerminalResult
                  ? SessionObjectRole::TerminalOutputValue
                  : SessionObjectRole::CycleFrameValue,
              found->slot->handle,
              0U,
              found->slot->codec_entry_handle};
    return {};
}

SessionResult Session::qualification_replace_candidate(
    std::uint32_t state_block_handle,
    InProcessValueView value) noexcept {
    SessionObjectIdentityView destination;
    const auto read = qualification_read_candidate(state_block_handle,
                                                   destination);
    if (!read) return read;
    const auto found = std::find_if(
        implementation_->candidate_state_store.blocks.begin(),
        implementation_->candidate_state_store.blocks.end(),
        [state_block_handle](const auto& candidate) {
            return candidate.block != nullptr &&
                   candidate.block->handle == state_block_handle;
        });
    const auto& operations = found->materializer->operations();
    if (value.object == nullptr ||
        value.type_identity != destination.type_identity) {
        return {SessionError::ObjectTypeMismatch, state_block_handle,
                "candidate replacement type mismatch"};
    }
    if (value.size_bytes != destination.size_bytes) {
        return {SessionError::ObjectSizeMismatch, state_block_handle,
                "candidate replacement size mismatch"};
    }
    if (value.alignment_bytes != destination.alignment_bytes) {
        return {SessionError::ObjectAlignmentMismatch,
                state_block_handle,
                "candidate replacement alignment mismatch"};
    }
    if (!operations.validate(value.object) ||
        !operations.replace(found->address, value.object) ||
        !operations.validate(found->address)) {
        return {SessionError::ObjectValidationFailed,
                state_block_handle,
                "candidate replacement validation failed"};
    }
    return {};
}

std::uint64_t Session::committed_epoch() const noexcept {
    return implementation_->committed_epoch;
}

std::int64_t Session::committed_tick() const noexcept {
    return implementation_->committed_tick;
}

std::uint64_t Session::committed_step_count() const noexcept {
    return implementation_->committed_step_count;
}

bool Session::frame_open() const noexcept {
    return implementation_->cycle_frame.open;
}

SessionCreation create_session(
    std::shared_ptr<const contracts::ExecutionPlanImage> image,
    std::shared_ptr<const SessionMaterializationProvider> provider) noexcept {
    if (image == nullptr) {
        return {nullptr,
                {SessionError::NullImage, 0U, "Session requires an Image"}};
    }
    if (provider == nullptr) {
        return {nullptr,
                {SessionError::NullMaterializationProvider, 0U,
                 "Session requires a materialization provider"}};
    }
    try {
        auto implementation = std::make_unique<Session::Impl>();
        implementation->image = std::move(image);
        implementation->provider = std::move(provider);
        implementation->committed_tick =
            implementation->image->clock().initial_tick;
        implementation->run_outcome_storages.reserve(2U);
        auto initial_outcome = std::make_unique<RunOutcome>();
        implementation->prepare_outcome_identity(*initial_outcome);
        implementation->current_run_outcome = initial_outcome.get();
        implementation->run_outcome_storages.push_back(
            std::move(initial_outcome));
        implementation->reset_failure_outcome_storage =
            std::make_unique<RunOutcome>();
        implementation->prepare_outcome_identity(
            *implementation->reset_failure_outcome_storage);
        return {std::unique_ptr<Session>(
                    new Session(std::move(implementation))),
                {}};
    } catch (...) {
        return {nullptr,
                {SessionError::AllocationFailure, 0U,
                 "Session setup allocation failed"}};
    }
}

} // namespace gnc::kernel
