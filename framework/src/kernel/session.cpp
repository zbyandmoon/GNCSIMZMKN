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

} // namespace

std::string_view to_string(SessionError error) noexcept {
    switch (error) {
    case SessionError::None: return "None";
    case SessionError::NullImage: return "NullImage";
    case SessionError::NullMaterializationProvider:
        return "NullMaterializationProvider";
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
    case SessionError::ObjectValidationFailed: return "ObjectValidationFailed";
    case SessionError::InvalidLifecycleTransition:
        return "InvalidLifecycleTransition";
    case SessionError::InvalidSchedule: return "InvalidSchedule";
    case SessionError::FrameAlreadyOpen: return "FrameAlreadyOpen";
    case SessionError::FrameNotOpen: return "FrameNotOpen";
    case SessionError::FrameSlotAbsent: return "FrameSlotAbsent";
    case SessionError::StaleFrameView: return "StaleFrameView";
    case SessionError::ReaderAuthorizationFailure:
        return "ReaderAuthorizationFailure";
    case SessionError::WriterAuthorizationFailure:
        return "WriterAuthorizationFailure";
    case SessionError::InvocationFailed: return "InvocationFailed";
    case SessionError::InternalFailure: return "InternalFailure";
    }
    return "InternalFailure";
}

SessionCommittedStateView::SessionCommittedStateView(
    const SessionCommittedStateAccess* access) noexcept
    : access_(access) {}

SessionResult SessionCommittedStateView::read(
    std::uint32_t state_block_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (access_ == nullptr) {
        return {SessionError::InvalidLifecycleTransition, state_block_handle,
                "committed state view is unavailable"};
    }
    return access_->read_committed(state_block_handle, result);
}

SessionInputView::SessionInputView(const SessionFrameAccess* access,
                                   std::uint32_t callsite_handle,
                                   std::uint64_t generation) noexcept
    : access_(access), callsite_handle_(callsite_handle),
      generation_(generation) {}

SessionResult SessionInputView::read(
    std::uint32_t slot_handle,
    SessionObjectIdentityView& result) const noexcept {
    result = {};
    if (access_ == nullptr) {
        return {SessionError::StaleFrameView, slot_handle,
                "input view is unavailable"};
    }
    return access_->read_input(callsite_handle_, generation_, slot_handle,
                               result);
}

bool SessionInputView::active() const noexcept {
    return access_ != nullptr && access_->frame_active(generation_);
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

SessionInvocationContext::SessionInvocationContext(
    std::uint32_t callsite_handle, std::uint32_t component_handle,
    std::int64_t tick, double boundary_time_seconds,
    double interval_start_seconds, double interval_end_seconds,
    contracts::DataQuality quality, SessionObjectIdentityView runtime_cell,
    SessionCommittedStateView committed, SessionInputView inputs,
    SessionOutputWriterSet outputs) noexcept
    : callsite_handle_(callsite_handle), component_handle_(component_handle),
      tick_(tick), boundary_time_seconds_(boundary_time_seconds),
      interval_start_seconds_(interval_start_seconds),
      interval_end_seconds_(interval_end_seconds), quality_(quality),
      runtime_cell_(runtime_cell), committed_(committed), inputs_(inputs),
      outputs_(outputs) {}

struct Session::Impl final : SessionObjectAccess,
                             SessionCommittedStateAccess,
                             SessionFrameAccess {
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

    struct CommittedOutput {
        const contracts::PlanImageSlot* slot = nullptr;
        void* address = nullptr;
        const SessionObjectMaterializer* materializer = nullptr;
        bool present = false;
        std::uint64_t generation = 0U;
        std::uint64_t sequence = 0U;
        std::int64_t sample_tick = 0;
        double sample_time_seconds = 0.0;
        double interval_start_seconds = 0.0;
        double interval_end_seconds = 0.0;
        contracts::DataQuality quality = contracts::DataQuality::Invalid;
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

    struct CommittedOutputStore {
        std::vector<CommittedOutput> values;
        std::vector<std::size_t> construction_order;
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
    SessionBoundarySummary boundary_summary;
    std::vector<Arena> arenas;
    std::vector<OwnedObject> preparations;
    SessionRuntimeBindings runtime_bindings;
    CommittedStateStore committed_state_store;
    TransactionCandidateStore candidate_state_store;
    CommittedOutputStore committed_output_store;
    CycleFrame cycle_frame;
    std::vector<ScheduledCall> opening_schedule;
    std::uint64_t committed_epoch = 0U;
    std::int64_t committed_tick = 0;
    bool opening_boundary_complete = false;

    [[nodiscard]] SessionResult failure(SessionError error,
                                        std::uint32_t handle,
                                        std::string_view detail) noexcept {
        last_result = {error, handle, detail};
        return last_result;
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

    [[nodiscard]] SessionResult validate_materializers() noexcept {
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
            for (std::size_t index = 0U;
                 index < materializer->dependency_count(); ++index) {
                const auto expected = materializer->dependency(index);
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
                     contracts::PlanImageEntryKind::BoundaryEvaluation)) {
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
            !unique_nonzero_handles(image->evaluator_histories())) {
            return failure(SessionError::InvalidImageHandle, 0U,
                           "Image contains a duplicate or zero handle");
        }
        auto result = validate_lifecycle();
        if (result) result = validate_storage();
        if (result) result = validate_states();
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
        committed_output_store.values.reserve(image->slots().size());
        committed_output_store.construction_order.reserve(
            image->slots().size());
        cycle_frame.slots.reserve(image->slots().size());
        cycle_frame.construction_order.reserve(image->slots().size());
        opening_schedule.reserve(image->callsites().size());
        boundary_summary.executed_callsite_handles.reserve(
            image->callsites().size());
        boundary_summary.skipped_callsite_handles.reserve(
            image->callsites().size());
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
        std::vector<OwnedObject>& destination, SessionObjectRole role,
        std::uint32_t linked_entry_handle,
        SessionError construction_error) noexcept {
        const auto layout = materializer.operations().layout();
        RawBlock allocation(layout.size_bytes, layout.alignment_bytes);
        if (allocation.get() == nullptr) {
            return failure(SessionError::AllocationFailure, handle,
                           "object allocation failed");
        }
        if (!materializer.construct(*this, allocation.get())) {
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
            if (slot.storage_class ==
                contracts::SlotStorageClass::IntegrationHeld) {
                const auto layout = materializer->operations().layout();
                RawBlock staging(layout.size_bytes, layout.alignment_bytes);
                if (staging.get() == nullptr) {
                    return failure(SessionError::AllocationFailure,
                                   slot.handle,
                                   "held-output staging allocation failed");
                }
                committed_output_store.values.push_back(
                    {&slot, persistent_address, materializer});
                cycle_frame.slots.push_back(
                    {&slot, staging.get(), materializer, true});
                static_cast<void>(staging.release());
                continue;
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
            if (!materializer->construct(*this, temporary.get())) {
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
    }

    void unwind() noexcept {
        close_frame();
        discard_new_committed_outputs();
        committed_output_store.values.clear();
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

    [[nodiscard]] CommittedOutput* committed_output(
        std::uint32_t slot_handle) noexcept {
        const auto found = std::find_if(
            committed_output_store.values.begin(),
            committed_output_store.values.end(),
            [slot_handle](const auto& candidate) {
                return candidate.slot != nullptr &&
                       candidate.slot->handle == slot_handle;
            });
        return found == committed_output_store.values.end() ? nullptr
                                                             : &*found;
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

    void discard_new_committed_outputs() noexcept {
        for (auto found =
                 committed_output_store.construction_order.rbegin();
             found != committed_output_store.construction_order.rend();
             ++found) {
            auto& output = committed_output_store.values[*found];
            if (output.present) {
                output.materializer->operations().destroy(output.address);
                output.present = false;
            }
        }
        committed_output_store.construction_order.clear();
    }

    [[nodiscard]] SessionResult publish_retained_outputs() noexcept {
        if (!committed_output_store.construction_order.empty()) {
            return failure(SessionError::InvalidLifecycleTransition, 0U,
                           "opening-boundary outputs are already committed");
        }
        for (const auto& staged : cycle_frame.slots) {
            if (staged.slot == nullptr ||
                staged.slot->storage_class !=
                    contracts::SlotStorageClass::IntegrationHeld) {
                continue;
            }
            if (!staged.present ||
                staged.generation != cycle_frame.generation) {
                discard_new_committed_outputs();
                return failure(SessionError::FrameSlotAbsent,
                               staged.slot->handle,
                               "required held interval output is absent");
            }
            auto* output = committed_output(staged.slot->handle);
            if (output == nullptr || output->present) {
                discard_new_committed_outputs();
                return failure(SessionError::InvalidLifecycleTransition,
                               staged.slot->handle,
                               "held interval output ownership is invalid");
            }
            if (!output->materializer->operations().copy_construct(
                    staged.address, output->address)) {
                discard_new_committed_outputs();
                return failure(SessionError::SlotConstructionFailed,
                               staged.slot->handle,
                               "held interval output copy failed");
            }
            ConstructedObject guard(output->address,
                                    output->materializer->operations());
            if (!output->materializer->operations().validate(
                    output->address)) {
                discard_new_committed_outputs();
                return failure(SessionError::ObjectValidationFailed,
                               staged.slot->handle,
                               "committed held output validation failed");
            }
            const auto index = static_cast<std::size_t>(
                output - committed_output_store.values.data());
            committed_output_store.construction_order.push_back(index);
            output->present = true;
            output->generation = staged.generation;
            output->sequence = staged.sequence;
            output->sample_tick = staged.sample_tick;
            output->sample_time_seconds = staged.sample_time_seconds;
            output->interval_start_seconds =
                staged.interval_start_seconds;
            output->interval_end_seconds = staged.interval_end_seconds;
            output->quality = staged.quality;
            guard.release();
        }
        return {};
    }

    [[nodiscard]] bool frame_active(
        std::uint64_t generation) const noexcept override {
        return cycle_frame.open && generation != 0U &&
               generation == cycle_frame.generation;
    }

    [[nodiscard]] SessionResult read_input(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept override {
        result = {};
        if (!frame_active(generation)) {
            return {SessionError::StaleFrameView, slot_handle,
                    "input view generation is stale"};
        }
        const auto* callsite = find_handle(image->callsites(), callsite_handle);
        if (callsite == nullptr ||
            std::find(callsite->input_slot_handles.begin(),
                      callsite->input_slot_handles.end(), slot_handle) ==
                callsite->input_slot_handles.end()) {
            return {SessionError::ReaderAuthorizationFailure, slot_handle,
                    "callsite is not authorized to read slot"};
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
        const auto completed_boundaries =
            committed_tick - image->clock().initial_tick + 1;
        return completed_boundaries >=
               static_cast<std::int64_t>(found->history_depth);
    }

    [[nodiscard]] bool scheduled_now(
        const contracts::PlanImageRuntimeComponent& component) const noexcept {
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

const SessionBoundarySummary& Session::last_boundary_summary() const noexcept {
    return implementation_->boundary_summary;
}

SessionResult Session::initialize() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Created) {
        return impl.failure(SessionError::InvalidLifecycleTransition, 0U,
                            "initialize requires Created Session");
    }
    try {
        auto result = impl.validate_image();
        if (result) {
            impl.reserve_tracking();
            result = impl.allocate_arenas();
        }
        if (result) {
            for (const auto handle :
                 impl.image->lifecycle().preparation_handles) {
                const auto* preparation = find_handle(
                    impl.image->preparations(), handle);
                const auto* materializer = impl.provider->preparation(handle);
                result = impl.construct_owned(
                    handle, *materializer, impl.preparations,
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
                    result = impl.construct_owned(
                        handle, *materializer, impl.runtime_bindings.cells,
                        SessionObjectRole::RuntimeCell,
                        component->runtime_cell_factory_entry_handle,
                        SessionError::RuntimeCellFailed);
                }
                if (!result) break;
            }
        }
        if (result) result = impl.prepare_frame_slots();
        if (result) result = impl.construct_states();
        if (!result) {
            impl.unwind();
            impl.state = SessionState::InitializationFailed;
            return impl.last_result;
        }
        impl.committed_tick = impl.image->clock().initial_tick;
        impl.state = SessionState::Initialized;
        impl.last_result = {};
        return {};
    } catch (const std::bad_alloc&) {
        impl.unwind();
        impl.state = SessionState::InitializationFailed;
        return impl.failure(SessionError::AllocationFailure, 0U,
                            "Session initialization allocation failed");
    } catch (...) {
        impl.unwind();
        impl.state = SessionState::InitializationFailed;
        return impl.failure(SessionError::InternalFailure, 0U,
                            "Session initialization failed unexpectedly");
    }
}

SessionResult Session::execute_opening_boundary() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        return impl.failure(SessionError::InvalidLifecycleTransition, 0U,
                            "opening boundary requires Initialized Session");
    }
    if (impl.cycle_frame.open) {
        return impl.failure(SessionError::FrameAlreadyOpen, 0U,
                            "CycleFrame is already open");
    }
    if (impl.opening_boundary_complete) {
        return impl.failure(SessionError::InvalidLifecycleTransition, 0U,
                            "opening boundary has already completed");
    }
    try {
        ++impl.cycle_frame.generation;
        if (impl.cycle_frame.generation == 0U) {
            ++impl.cycle_frame.generation;
        }
        impl.cycle_frame.sequence = 0U;
        impl.cycle_frame.write_count = 0U;
        impl.cycle_frame.open = true;
        impl.boundary_summary.generation = impl.cycle_frame.generation;
        impl.boundary_summary.output_write_count = 0U;
        impl.boundary_summary.executed_callsite_handles.clear();
        impl.boundary_summary.skipped_callsite_handles.clear();

        const auto boundary_time =
            static_cast<double>(impl.committed_tick) *
            impl.image->clock().base_step_seconds;
        const auto interval_end = boundary_time +
                                  impl.image->clock().base_step_seconds;
        for (const auto& scheduled : impl.opening_schedule) {
            const auto* component = find_handle(
                impl.image->runtime_components(), scheduled.component_handle);
            const auto* callsite = find_handle(
                impl.image->callsites(), scheduled.callsite_handle);
            if (component == nullptr || callsite == nullptr) {
                impl.close_frame();
                return impl.failure(SessionError::InvalidSchedule,
                                    scheduled.callsite_handle,
                                    "scheduled callsite disappeared");
            }
            if (!impl.scheduled_now(*component) ||
                !impl.history_ready(callsite->handle)) {
                impl.boundary_summary.skipped_callsite_handles.push_back(
                    callsite->handle);
                continue;
            }
            const auto* entry = impl.provider->invocation(callsite->handle);
            if (entry == nullptr) {
                impl.close_frame();
                return impl.failure(SessionError::MissingMaterializer,
                                    callsite->handle,
                                    "callsite invocation entry is missing");
            }
            const auto identity = entry->identity();
            if (identity.callsite_handle != callsite->handle ||
                identity.runtime_component_handle != component->handle ||
                identity.linked_entry_handle != callsite->entry_handle) {
                impl.close_frame();
                return impl.failure(SessionError::InvalidMaterializerIdentity,
                                    callsite->handle,
                                    "callsite invocation identity mismatch");
            }
            const auto runtime = impl.runtime_view(component->handle);
            if (!runtime) {
                impl.close_frame();
                return impl.failure(SessionError::RuntimeCellFailed,
                                    component->handle,
                                    "scheduled Runtime Cell is unavailable");
            }
            SessionInvocationContext context(
                callsite->handle, component->handle, impl.committed_tick,
                boundary_time, boundary_time, interval_end,
                contracts::DataQuality::Valid, runtime,
                SessionCommittedStateView(&impl),
                SessionInputView(&impl, callsite->handle,
                                 impl.cycle_frame.generation),
                SessionOutputWriterSet(&impl, callsite->handle,
                                       impl.cycle_frame.generation));
            const auto result = entry->invoke(context);
            if (!result) {
                impl.close_frame();
                return impl.failure(
                    result.error == SessionError::None
                        ? SessionError::InvocationFailed
                        : result.error,
                    result.image_handle == 0U ? callsite->handle
                                              : result.image_handle,
                    result.detail.empty() ? "callsite invocation failed"
                                          : result.detail);
            }
            const auto outputs = impl.validate_callsite_outputs(*callsite);
            if (!outputs) {
                impl.close_frame();
                return impl.last_result;
            }
            impl.boundary_summary.executed_callsite_handles.push_back(
                callsite->handle);
        }
        const auto retained = impl.publish_retained_outputs();
        if (!retained) {
            impl.close_frame();
            return impl.last_result;
        }
        impl.boundary_summary.output_write_count =
            impl.cycle_frame.write_count;
        impl.close_frame();
        impl.opening_boundary_complete = true;
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

std::size_t Session::preparation_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->preparations.size()
               : 0U;
}

std::size_t Session::runtime_cell_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->runtime_bindings.cells.size()
               : 0U;
}

std::size_t Session::committed_state_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->committed_state_store.blocks.size()
               : 0U;
}

std::vector<std::uint32_t> Session::preparation_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) return handles;
    handles.reserve(implementation_->preparations.size());
    for (const auto& object : implementation_->preparations) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::runtime_component_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) return handles;
    handles.reserve(implementation_->runtime_bindings.cells.size());
    for (const auto& object : implementation_->runtime_bindings.cells) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::committed_state_block_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) return handles;
    handles.reserve(implementation_->committed_state_store.blocks.size());
    for (const auto& object : implementation_->committed_state_store.blocks) {
        handles.push_back(object.block->handle);
    }
    return handles;
}

std::vector<SessionStorageExtent> Session::storage_extents() const {
    std::vector<SessionStorageExtent> extents;
    if (state() != SessionState::Initialized) return extents;
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
    if (state() != SessionState::Initialized) return result;
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
    if (state() != SessionState::Initialized) return result;
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
    if (state() != SessionState::Initialized) return result;
    result.reserve(implementation_->committed_output_store.values.size());
    for (const auto& output :
         implementation_->committed_output_store.values) {
        result.push_back({output.slot->handle,
                          output.slot->codec_entry_handle,
                          output.present,
                          output.generation,
                          output.sequence,
                          output.sample_tick,
                          output.sample_time_seconds,
                          output.interval_start_seconds,
                          output.interval_end_seconds,
                          output.quality});
    }
    return result;
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
