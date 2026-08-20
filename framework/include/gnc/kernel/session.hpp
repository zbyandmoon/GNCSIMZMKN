#pragma once

#include "gnc/contracts/execution_plan_image.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gnc::kernel {

enum class SessionState : std::uint8_t {
    Created,
    Initialized,
    InitializationFailed,
};

enum class SessionError : std::uint8_t {
    None,
    NullImage,
    NullMaterializationProvider,
    InvalidImageHandle,
    InvalidStorageLayout,
    StorageBoundsViolation,
    MissingMaterializer,
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
};

[[nodiscard]] std::string_view to_string(SessionError error) noexcept;

struct SessionResult {
    SessionError error = SessionError::None;
    std::uint32_t image_handle = 0U;
    std::string detail;

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

// Explicit process-local lifetime operations. Implementations must construct
// and destroy real C++ objects; byte copying and byte clearing are outside
// this contract.
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

class SessionObjectAccess {
  public:
    virtual ~SessionObjectAccess() = default;

    [[nodiscard]] virtual const void* prepared_object(
        std::uint32_t preparation_handle) const noexcept = 0;
};

// One fixed, typed adapter entry. The package/generated adapter resolves its
// callable and concrete type before Session initialization. The Kernel sees
// only the Image handle and this type-erased process-local contract.
class SessionObjectMaterializer {
  public:
    virtual ~SessionObjectMaterializer() = default;

    [[nodiscard]] virtual const InProcessObjectOperations& operations()
        const noexcept = 0;
    [[nodiscard]] virtual bool construct(
        const SessionObjectAccess& objects,
        void* destination) const noexcept = 0;
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
};

struct InProcessValueView {
    const void* object = nullptr;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    const void* type_identity = nullptr;
};

struct SessionStorageExtent {
    std::uint32_t layout_handle = 0U;
    contracts::SlotStorageClass storage_class =
        contracts::SlotStorageClass::Unspecified;
    std::uint64_t size_bytes = 0U;
    std::uint64_t alignment_bytes = 0U;
    const void* address = nullptr;
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
    [[nodiscard]] const SessionResult& last_result() const noexcept;

    [[nodiscard]] std::size_t preparation_count() const noexcept;
    [[nodiscard]] std::size_t runtime_cell_count() const noexcept;
    [[nodiscard]] std::size_t committed_state_count() const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> preparation_handles() const;
    [[nodiscard]] std::vector<std::uint32_t> runtime_component_handles() const;
    [[nodiscard]] std::vector<std::uint32_t> committed_state_block_handles()
        const;
    [[nodiscard]] std::vector<SessionStorageExtent> storage_extents() const;

    [[nodiscard]] const void* prepared_object(
        std::uint32_t preparation_handle) const noexcept;
    [[nodiscard]] const void* runtime_cell_object(
        std::uint32_t component_handle) const noexcept;
    [[nodiscard]] const void* committed_state_object(
        std::uint32_t state_block_handle) const noexcept;
    [[nodiscard]] const void* candidate_state_object(
        std::uint32_t state_block_handle) const noexcept;
    [[nodiscard]] const void* slot_object(
        std::uint32_t slot_handle) const noexcept;

    [[nodiscard]] SessionResult replace_slot(
        std::uint32_t slot_handle, InProcessValueView source) noexcept;
    [[nodiscard]] SessionResult clone_committed_to_candidate(
        std::uint32_t state_block_handle) noexcept;

  private:
    struct Impl;
    explicit Session(std::unique_ptr<Impl> implementation) noexcept;
    std::unique_ptr<Impl> implementation_;

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
