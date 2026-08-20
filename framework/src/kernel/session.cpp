#include "gnc/kernel/session.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace gnc::kernel {
namespace {

[[nodiscard]] bool valid_alignment(std::uint64_t alignment) noexcept {
    return alignment != 0U &&
           (alignment & (alignment - 1U)) == 0U &&
           alignment <=
               static_cast<std::uint64_t>(
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
    const std::vector<Value>& values) {
    std::unordered_set<std::uint32_t> handles;
    for (const auto& value : values) {
        if (value.handle == 0U || !handles.insert(value.handle).second) {
            return false;
        }
    }
    return true;
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

} // namespace

std::string_view to_string(SessionError error) noexcept {
    switch (error) {
    case SessionError::None:
        return "None";
    case SessionError::NullImage:
        return "NullImage";
    case SessionError::NullMaterializationProvider:
        return "NullMaterializationProvider";
    case SessionError::InvalidImageHandle:
        return "InvalidImageHandle";
    case SessionError::InvalidStorageLayout:
        return "InvalidStorageLayout";
    case SessionError::StorageBoundsViolation:
        return "StorageBoundsViolation";
    case SessionError::MissingMaterializer:
        return "MissingMaterializer";
    case SessionError::ObjectSizeMismatch:
        return "ObjectSizeMismatch";
    case SessionError::ObjectAlignmentMismatch:
        return "ObjectAlignmentMismatch";
    case SessionError::ObjectLayoutMismatch:
        return "ObjectLayoutMismatch";
    case SessionError::ObjectCodecMismatch:
        return "ObjectCodecMismatch";
    case SessionError::ObjectTypeMismatch:
        return "ObjectTypeMismatch";
    case SessionError::AllocationFailure:
        return "AllocationFailure";
    case SessionError::PreparationFailed:
        return "PreparationFailed";
    case SessionError::RuntimeCellFailed:
        return "RuntimeCellFailed";
    case SessionError::SlotConstructionFailed:
        return "SlotConstructionFailed";
    case SessionError::InitialStateFailed:
        return "InitialStateFailed";
    case SessionError::ObjectValidationFailed:
        return "ObjectValidationFailed";
    case SessionError::InvalidLifecycleTransition:
        return "InvalidLifecycleTransition";
    }
    return "InvalidLifecycleTransition";
}

struct Session::Impl final : SessionObjectAccess {
    struct OwnedObject {
        std::uint32_t handle = 0U;
        void* address = nullptr;
        std::uint64_t alignment = 0U;
        const SessionObjectMaterializer* materializer = nullptr;
    };

    struct Arena {
        const contracts::PlanImageStorageLayout* layout = nullptr;
        void* address = nullptr;
    };

    struct ArenaObject {
        std::uint32_t slot_handle = 0U;
        std::uint32_t state_block_handle = 0U;
        void* address = nullptr;
        const SessionObjectMaterializer* materializer = nullptr;
    };

    std::shared_ptr<const contracts::ExecutionPlanImage> image;
    std::shared_ptr<const SessionMaterializationProvider> provider;
    SessionState state = SessionState::Created;
    SessionResult last_result;
    std::vector<Arena> arenas;
    std::vector<OwnedObject> preparations;
    std::vector<OwnedObject> runtime_cells;
    std::vector<ArenaObject> slots;
    std::vector<ArenaObject> states;

    [[nodiscard]] const void* prepared_object(
        std::uint32_t handle) const noexcept override {
        const auto found = std::find_if(
            preparations.begin(), preparations.end(),
            [handle](const auto& object) { return object.handle == handle; });
        return found == preparations.end() ? nullptr : found->address;
    }

    [[nodiscard]] Arena* arena(std::uint32_t handle) noexcept {
        const auto found = std::find_if(
            arenas.begin(), arenas.end(), [handle](const auto& candidate) {
                return candidate.layout != nullptr &&
                       candidate.layout->handle == handle;
            });
        return found == arenas.end() ? nullptr : &*found;
    }

    [[nodiscard]] const Arena* arena(std::uint32_t handle) const noexcept {
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

    [[nodiscard]] const contracts::PlanImageInitialBinding*
    initial_for_state(const contracts::PlanImageStateBlock& state_block)
        const noexcept {
        const auto found = std::find_if(
            image->initial_bindings().begin(),
            image->initial_bindings().end(),
            [&state_block](const auto& binding) {
                return binding.committed_state_slot_handle ==
                       state_block.committed_slot_handle;
            });
        return found == image->initial_bindings().end() ? nullptr : &*found;
    }

    [[nodiscard]] const contracts::PlanImageStateBlock*
    state_for_slot(std::uint32_t slot_handle) const noexcept {
        const auto found = std::find_if(
            image->state_blocks().begin(), image->state_blocks().end(),
            [slot_handle](const auto& block) {
                return block.committed_slot_handle == slot_handle ||
                       block.candidate_slot_handle == slot_handle;
            });
        return found == image->state_blocks().end() ? nullptr : &*found;
    }

    [[nodiscard]] const SessionObjectMaterializer* materializer_for_slot(
        const contracts::PlanImageSlot& slot) const noexcept {
        const auto* block = state_for_slot(slot.handle);
        if (block == nullptr) {
            return provider->slot(slot.handle);
        }
        const auto* initial = initial_for_state(*block);
        return initial == nullptr ? nullptr
                                  : provider->initial_state(initial->handle);
    }

    [[nodiscard]] SessionResult failure(SessionError error,
                                        std::uint32_t handle,
                                        std::string detail) noexcept {
        last_result = {error, handle, std::move(detail)};
        return last_result;
    }

    [[nodiscard]] SessionResult validate_image() noexcept {
        if (!unique_nonzero_handles(image->storage_layouts()) ||
            !unique_nonzero_handles(image->slots()) ||
            !unique_nonzero_handles(image->state_blocks()) ||
            !unique_nonzero_handles(image->preparations()) ||
            !unique_nonzero_handles(image->runtime_components()) ||
            !unique_nonzero_handles(image->initial_bindings())) {
            return failure(SessionError::InvalidImageHandle, 0U,
                           "duplicate or zero Image handle");
        }

        for (const auto& layout : image->storage_layouts()) {
            if (layout.size_bytes == 0U ||
                !valid_alignment(layout.alignment_bytes)) {
                return failure(SessionError::InvalidStorageLayout,
                               layout.handle,
                               "invalid storage extent size or alignment");
            }
        }

        std::unordered_set<std::uint32_t> lifecycle_handles;
        for (const auto handle : image->lifecycle().preparation_handles) {
            if (!lifecycle_handles.insert(handle).second ||
                find_handle(image->preparations(), handle) == nullptr) {
                return failure(SessionError::InvalidImageHandle, handle,
                               "invalid preparation lifecycle handle");
            }
            const auto* materializer = provider->preparation(handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, handle,
                               "missing preparation materializer");
            }
            const auto object = materializer->operations().layout();
            if (object.size_bytes == 0U ||
                !valid_alignment(object.alignment_bytes) ||
                object.type_identity == nullptr) {
                return failure(SessionError::InvalidStorageLayout, handle,
                               "invalid preparation object layout");
            }
        }

        lifecycle_handles.clear();
        for (const auto handle : image->lifecycle().runtime_component_handles) {
            if (!lifecycle_handles.insert(handle).second ||
                find_handle(image->runtime_components(), handle) == nullptr) {
                return failure(SessionError::InvalidImageHandle, handle,
                               "invalid runtime lifecycle handle");
            }
            const auto* materializer = provider->runtime_component(handle);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, handle,
                               "missing runtime component materializer");
            }
            const auto object = materializer->operations().layout();
            if (object.size_bytes == 0U ||
                !valid_alignment(object.alignment_bytes) ||
                object.type_identity == nullptr) {
                return failure(SessionError::InvalidStorageLayout, handle,
                               "invalid runtime component object layout");
            }
        }

        lifecycle_handles.clear();
        for (const auto handle : image->lifecycle().initial_binding_handles) {
            if (!lifecycle_handles.insert(handle).second ||
                find_handle(image->initial_bindings(), handle) == nullptr) {
                return failure(SessionError::InvalidImageHandle, handle,
                               "invalid initial-state lifecycle handle");
            }
            if (provider->initial_state(handle) == nullptr) {
                return failure(SessionError::MissingMaterializer, handle,
                               "missing initial-state materializer");
            }
        }

        for (const auto& slot : image->slots()) {
            const auto* storage =
                find_handle(image->storage_layouts(),
                            slot.storage_layout_handle);
            if (storage == nullptr) {
                return failure(SessionError::InvalidImageHandle, slot.handle,
                               "slot references missing storage extent");
            }
            if (slot.offset_bytes > storage->size_bytes ||
                slot.size_bytes > storage->size_bytes - slot.offset_bytes) {
                return failure(SessionError::StorageBoundsViolation,
                               slot.handle,
                               "slot exceeds its storage extent");
            }
            if (!valid_alignment(slot.alignment_bytes) ||
                slot.alignment_bytes > storage->alignment_bytes ||
                slot.offset_bytes % slot.alignment_bytes != 0U) {
                return failure(SessionError::InvalidStorageLayout,
                               slot.handle,
                               "slot offset or alignment is invalid");
            }
            const auto* materializer = materializer_for_slot(slot);
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, slot.handle,
                               "missing slot or state materializer");
            }
            SessionError mismatch = SessionError::None;
            if (!same_layout(materializer->operations().layout(),
                             slot.size_bytes, slot.alignment_bytes,
                             slot.layout_id, slot.codec_entry_handle,
                             mismatch)) {
                return failure(mismatch, slot.handle,
                               "Image slot metadata disagrees with typed adapter");
            }
        }

        for (const auto& block : image->state_blocks()) {
            const auto* committed = find_handle(image->slots(),
                                                block.committed_slot_handle);
            const auto* candidate = find_handle(image->slots(),
                                                block.candidate_slot_handle);
            const auto* initial = initial_for_state(block);
            if (committed == nullptr || candidate == nullptr ||
                initial == nullptr) {
                return failure(SessionError::InvalidImageHandle, block.handle,
                               "state block lacks slot or initial binding");
            }
            const auto* materializer = provider->initial_state(initial->handle);
            SessionError mismatch = SessionError::None;
            if (materializer == nullptr) {
                return failure(SessionError::MissingMaterializer,
                               initial->handle,
                               "missing state materializer");
            }
            if (!same_layout(materializer->operations().layout(),
                             block.size_bytes, block.alignment_bytes,
                             block.layout_id, block.codec_entry_handle,
                             mismatch)) {
                return failure(mismatch, block.handle,
                               "Image state metadata disagrees with typed adapter");
            }
        }
        return {};
    }

    void unwind() noexcept {
        for (auto found = states.rbegin(); found != states.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
        }
        states.clear();
        for (auto found = slots.rbegin(); found != slots.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
        }
        slots.clear();
        for (auto found = runtime_cells.rbegin();
             found != runtime_cells.rend(); ++found) {
            found->materializer->operations().destroy(found->address);
            release_raw(found->address, found->alignment);
        }
        runtime_cells.clear();
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

    [[nodiscard]] SessionResult allocate_arenas() noexcept {
        arenas.reserve(image->storage_layouts().size());
        for (const auto& layout : image->storage_layouts()) {
            void* address =
                allocate_raw(layout.size_bytes, layout.alignment_bytes);
            if (address == nullptr) {
                return failure(SessionError::AllocationFailure, layout.handle,
                               "storage extent allocation failed");
            }
            arenas.push_back({&layout, address});
        }
        return {};
    }

    [[nodiscard]] SessionResult construct_owned(
        std::uint32_t handle,
        const SessionObjectMaterializer& materializer,
        std::vector<OwnedObject>& destination,
        SessionError construction_error) noexcept {
        const auto layout = materializer.operations().layout();
        void* address = allocate_raw(layout.size_bytes,
                                     layout.alignment_bytes);
        if (address == nullptr) {
            return failure(SessionError::AllocationFailure, handle,
                           "object allocation failed");
        }
        if (!materializer.construct(*this, address)) {
            release_raw(address, layout.alignment_bytes);
            return failure(construction_error, handle,
                           "typed object construction failed");
        }
        if (!materializer.operations().validate(address)) {
            materializer.operations().destroy(address);
            release_raw(address, layout.alignment_bytes);
            return failure(SessionError::ObjectValidationFailed, handle,
                           "constructed object failed typed validation");
        }
        destination.push_back(
            {handle, address, layout.alignment_bytes, &materializer});
        return {};
    }

    [[nodiscard]] SessionResult construct_slots() noexcept {
        for (const auto& slot : image->slots()) {
            if (state_for_slot(slot.handle) != nullptr) {
                continue;
            }
            const auto* materializer = provider->slot(slot.handle);
            void* address = slot_address(slot);
            if (materializer == nullptr || address == nullptr) {
                return failure(SessionError::MissingMaterializer, slot.handle,
                               "slot materializer or storage is missing");
            }
            if (!materializer->construct(*this, address)) {
                return failure(SessionError::SlotConstructionFailed,
                               slot.handle,
                               "slot default construction failed");
            }
            if (!materializer->operations().validate(address)) {
                materializer->operations().destroy(address);
                return failure(SessionError::ObjectValidationFailed,
                               slot.handle,
                               "slot failed typed validation");
            }
            slots.push_back({slot.handle, 0U, address, materializer});
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
                return failure(SessionError::InvalidImageHandle, handle,
                               "initial binding has no state block");
            }
            const auto& block = *block_found;
            const auto* committed =
                find_handle(image->slots(), block.committed_slot_handle);
            const auto* candidate =
                find_handle(image->slots(), block.candidate_slot_handle);
            const auto* materializer = provider->initial_state(handle);
            if (committed == nullptr || candidate == nullptr ||
                materializer == nullptr) {
                return failure(SessionError::MissingMaterializer, handle,
                               "state slot or materializer is missing");
            }
            const auto layout = materializer->operations().layout();
            void* temporary =
                allocate_raw(layout.size_bytes, layout.alignment_bytes);
            if (temporary == nullptr) {
                return failure(SessionError::AllocationFailure, handle,
                               "initial-state temporary allocation failed");
            }
            if (!materializer->construct(*this, temporary)) {
                release_raw(temporary, layout.alignment_bytes);
                return failure(SessionError::InitialStateFailed, handle,
                               "typed initial-state builder failed");
            }
            if (!materializer->operations().validate(temporary)) {
                materializer->operations().destroy(temporary);
                release_raw(temporary, layout.alignment_bytes);
                return failure(SessionError::ObjectValidationFailed, handle,
                               "initial state failed typed validation");
            }

            void* committed_address = slot_address(*committed);
            void* candidate_address = slot_address(*candidate);
            if (committed_address == nullptr || candidate_address == nullptr ||
                !materializer->operations().copy_construct(
                    temporary, committed_address)) {
                materializer->operations().destroy(temporary);
                release_raw(temporary, layout.alignment_bytes);
                return failure(SessionError::InitialStateFailed, handle,
                               "committed state clone failed");
            }
            states.push_back({committed->handle, block.handle,
                              committed_address, materializer});
            if (!materializer->operations().validate(committed_address) ||
                !materializer->operations().copy_construct(
                    temporary, candidate_address)) {
                materializer->operations().destroy(temporary);
                release_raw(temporary, layout.alignment_bytes);
                return failure(SessionError::ObjectValidationFailed, handle,
                               "committed or candidate state clone failed");
            }
            states.push_back({candidate->handle, block.handle,
                              candidate_address, materializer});
            const bool candidate_valid =
                materializer->operations().validate(candidate_address);
            materializer->operations().destroy(temporary);
            release_raw(temporary, layout.alignment_bytes);
            if (!candidate_valid) {
                return failure(SessionError::ObjectValidationFailed, handle,
                               "candidate state failed typed validation");
            }
        }
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

SessionState Session::state() const noexcept {
    return implementation_->state;
}

const contracts::ExecutionPlanImage& Session::image() const noexcept {
    return *implementation_->image;
}

const SessionResult& Session::last_result() const noexcept {
    return implementation_->last_result;
}

SessionResult Session::initialize() noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Created) {
        return impl.failure(SessionError::InvalidLifecycleTransition, 0U,
                            "Session initialize requires Created state");
    }
    auto result = impl.validate_image();
    if (result) {
        result = impl.allocate_arenas();
    }
    if (result) {
        for (const auto handle : impl.image->lifecycle().preparation_handles) {
            const auto* materializer = impl.provider->preparation(handle);
            result = impl.construct_owned(
                handle, *materializer, impl.preparations,
                SessionError::PreparationFailed);
            if (!result) {
                break;
            }
        }
    }
    if (result) {
        for (const auto handle :
             impl.image->lifecycle().runtime_component_handles) {
            const auto* materializer =
                impl.provider->runtime_component(handle);
            result = impl.construct_owned(
                handle, *materializer, impl.runtime_cells,
                SessionError::RuntimeCellFailed);
            if (!result) {
                break;
            }
        }
    }
    if (result) {
        result = impl.construct_slots();
    }
    if (result) {
        result = impl.construct_states();
    }
    if (!result) {
        impl.unwind();
        impl.state = SessionState::InitializationFailed;
        return impl.last_result;
    }
    impl.state = SessionState::Initialized;
    impl.last_result = {};
    return {};
}

std::size_t Session::preparation_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->preparations.size()
               : 0U;
}

std::size_t Session::runtime_cell_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->runtime_cells.size()
               : 0U;
}

std::size_t Session::committed_state_count() const noexcept {
    return state() == SessionState::Initialized
               ? implementation_->image->state_blocks().size()
               : 0U;
}

std::vector<std::uint32_t> Session::preparation_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) {
        return handles;
    }
    handles.reserve(implementation_->preparations.size());
    for (const auto& object : implementation_->preparations) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::runtime_component_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) {
        return handles;
    }
    handles.reserve(implementation_->runtime_cells.size());
    for (const auto& object : implementation_->runtime_cells) {
        handles.push_back(object.handle);
    }
    return handles;
}

std::vector<std::uint32_t> Session::committed_state_block_handles() const {
    std::vector<std::uint32_t> handles;
    if (state() != SessionState::Initialized) {
        return handles;
    }
    handles.reserve(implementation_->image->state_blocks().size());
    for (const auto& block : implementation_->image->state_blocks()) {
        handles.push_back(block.handle);
    }
    return handles;
}

std::vector<SessionStorageExtent> Session::storage_extents() const {
    std::vector<SessionStorageExtent> extents;
    if (state() != SessionState::Initialized) {
        return extents;
    }
    extents.reserve(implementation_->arenas.size());
    for (const auto& arena : implementation_->arenas) {
        extents.push_back({arena.layout->handle, arena.layout->storage_class,
                           arena.layout->size_bytes,
                           arena.layout->alignment_bytes, arena.address});
    }
    return extents;
}

const void* Session::prepared_object(std::uint32_t handle) const noexcept {
    if (state() != SessionState::Initialized) {
        return nullptr;
    }
    return implementation_->prepared_object(handle);
}

const void* Session::runtime_cell_object(std::uint32_t handle) const noexcept {
    if (state() != SessionState::Initialized) {
        return nullptr;
    }
    const auto found = std::find_if(
        implementation_->runtime_cells.begin(),
        implementation_->runtime_cells.end(),
        [handle](const auto& object) { return object.handle == handle; });
    return found == implementation_->runtime_cells.end() ? nullptr
                                                          : found->address;
}

const void* Session::committed_state_object(
    std::uint32_t handle) const noexcept {
    if (state() != SessionState::Initialized) {
        return nullptr;
    }
    const auto* block = find_handle(implementation_->image->state_blocks(),
                                    handle);
    return block == nullptr ? nullptr : slot_object(block->committed_slot_handle);
}

const void* Session::candidate_state_object(
    std::uint32_t handle) const noexcept {
    if (state() != SessionState::Initialized) {
        return nullptr;
    }
    const auto* block = find_handle(implementation_->image->state_blocks(),
                                    handle);
    return block == nullptr ? nullptr : slot_object(block->candidate_slot_handle);
}

const void* Session::slot_object(std::uint32_t handle) const noexcept {
    if (state() != SessionState::Initialized) {
        return nullptr;
    }
    const auto* slot = find_handle(implementation_->image->slots(), handle);
    if (slot == nullptr) {
        return nullptr;
    }
    const auto* owner = implementation_->arena(slot->storage_layout_handle);
    return owner == nullptr || owner->address == nullptr
               ? nullptr
               : static_cast<const void*>(
                     static_cast<const std::byte*>(owner->address) +
                     slot->offset_bytes);
}

SessionResult Session::replace_slot(std::uint32_t handle,
                                    InProcessValueView source) noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        return {SessionError::InvalidLifecycleTransition, handle,
                "slot replacement requires Initialized state"};
    }
    const auto* slot = find_handle(impl.image->slots(), handle);
    if (slot == nullptr) {
        return {SessionError::InvalidImageHandle, handle,
                "slot replacement handle is unknown"};
    }
    const auto* materializer = impl.materializer_for_slot(*slot);
    if (materializer == nullptr) {
        return {SessionError::MissingMaterializer, handle,
                "slot replacement materializer is missing"};
    }
    const auto layout = materializer->operations().layout();
    if (source.object == nullptr || source.type_identity != layout.type_identity ||
        source.size_bytes != layout.size_bytes ||
        source.alignment_bytes != layout.alignment_bytes) {
        return {SessionError::ObjectTypeMismatch, handle,
                "replacement value has the wrong process-local type"};
    }
    if (!materializer->operations().validate(source.object) ||
        !materializer->operations().replace(
            const_cast<void*>(slot_object(handle)), source.object) ||
        !materializer->operations().validate(slot_object(handle))) {
        return {SessionError::ObjectValidationFailed, handle,
                "typed slot replacement failed"};
    }
    return {};
}

SessionResult Session::clone_committed_to_candidate(
    std::uint32_t handle) noexcept {
    auto& impl = *implementation_;
    if (impl.state != SessionState::Initialized) {
        return {SessionError::InvalidLifecycleTransition, handle,
                "state clone requires Initialized state"};
    }
    const auto* block = find_handle(impl.image->state_blocks(), handle);
    if (block == nullptr) {
        return {SessionError::InvalidImageHandle, handle,
                "state block handle is unknown"};
    }
    const auto* initial = impl.initial_for_state(*block);
    const auto* materializer =
        initial == nullptr ? nullptr
                           : impl.provider->initial_state(initial->handle);
    if (materializer == nullptr) {
        return {SessionError::MissingMaterializer, handle,
                "state clone materializer is missing"};
    }
    void* candidate = const_cast<void*>(candidate_state_object(handle));
    const void* committed = committed_state_object(handle);
    if (!materializer->operations().replace(candidate, committed) ||
        !materializer->operations().validate(candidate)) {
        return {SessionError::ObjectValidationFailed, handle,
                "typed committed-to-candidate clone failed"};
    }
    return {};
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
                 "Session fixed setup allocation failed"}};
    }
}

} // namespace gnc::kernel
