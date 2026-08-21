#pragma once

#include "gnc/contracts/execution_plan_image.hpp"
#include "gnc/contracts/sample_context.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace gnc::kernel {

namespace qualification {
class SessionAccess;
}

enum class SessionState : std::uint8_t {
    Created,
    Initialized,
    InitializationFailed,
};

enum class SessionError : std::uint8_t {
    None,
    NullImage,
    NullMaterializationProvider,
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
    ObjectValidationFailed,
    InvalidLifecycleTransition,
    InvalidSchedule,
    FrameAlreadyOpen,
    FrameNotOpen,
    FrameSlotAbsent,
    StaleFrameView,
    ReaderAuthorizationFailure,
    WriterAuthorizationFailure,
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
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept = 0;
};

class SessionFrameAccess {
  public:
    virtual ~SessionFrameAccess() = default;
    [[nodiscard]] virtual SessionResult read_input(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t slot_handle,
        SessionObjectIdentityView& result) const noexcept = 0;
    [[nodiscard]] virtual SessionResult write_output(
        std::uint32_t callsite_handle, std::uint64_t generation,
        std::uint32_t slot_handle, std::uint32_t writer_token_handle,
        InProcessValueView value) noexcept = 0;
    [[nodiscard]] virtual bool frame_active(
        std::uint64_t generation) const noexcept = 0;
};

class SessionCommittedStateView final {
  public:
    [[nodiscard]] SessionResult read(
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept;

  private:
    explicit SessionCommittedStateView(
        const SessionCommittedStateAccess* access) noexcept;
    const SessionCommittedStateAccess* access_ = nullptr;

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
                     std::uint32_t callsite_handle,
                     std::uint64_t generation) noexcept;
    const SessionFrameAccess* access_ = nullptr;
    std::uint32_t callsite_handle_ = 0U;
    std::uint64_t generation_ = 0U;

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

  private:
    SessionInvocationContext(
        std::uint32_t callsite_handle, std::uint32_t component_handle,
        std::int64_t tick, double boundary_time_seconds,
        double interval_start_seconds, double interval_end_seconds,
        contracts::DataQuality quality,
        SessionObjectIdentityView runtime_cell,
        SessionCommittedStateView committed, SessionInputView inputs,
        SessionOutputWriterSet outputs) noexcept;

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
};

struct SessionBoundarySummary {
    std::uint64_t generation = 0U;
    std::size_t output_write_count = 0U;
    std::vector<std::uint32_t> executed_callsite_handles;
    std::vector<std::uint32_t> skipped_callsite_handles;
};

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
    [[nodiscard]] SessionResult initialize() noexcept;
    [[nodiscard]] SessionResult execute_opening_boundary() noexcept;
    [[nodiscard]] const SessionResult& last_result() const noexcept;
    [[nodiscard]] const SessionBoundarySummary& last_boundary_summary()
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
    [[nodiscard]] std::uint64_t committed_epoch() const noexcept;
    [[nodiscard]] std::int64_t committed_tick() const noexcept;
    [[nodiscard]] bool frame_open() const noexcept;

  private:
    struct Impl;
    explicit Session(std::unique_ptr<Impl> implementation) noexcept;
    [[nodiscard]] SessionResult qualification_read_candidate(
        std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) const noexcept;
    [[nodiscard]] SessionResult qualification_replace_candidate(
        std::uint32_t state_block_handle,
        InProcessValueView value) noexcept;
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
