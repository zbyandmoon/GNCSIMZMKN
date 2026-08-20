#pragma once

#include "gnc/kernel/session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gnc::tests::ref_yyz {

enum class FailurePhase : std::uint8_t {
    None,
    Preparation,
    RuntimeCell,
    InitialState,
};

enum class TraceObjectKind : std::uint8_t {
    Preparation,
    RuntimeCell,
    Slot,
    State,
};

enum class TraceAction : std::uint8_t {
    Construct,
    CopyConstruct,
    Replace,
    Destroy,
    InjectedFailure,
};

struct FailurePoint {
    FailurePhase phase = FailurePhase::None;
    // Zero-based position in the corresponding Image lifecycle order.
    std::size_t ordinal = 0U;
};

struct AdapterOptions {
    FailurePoint failure;
    bool omit_first_slot_materializer = false;
};

struct TraceEvent {
    TraceAction action = TraceAction::Construct;
    TraceObjectKind kind = TraceObjectKind::Slot;
    std::uint32_t handle = 0U;
};

struct MaterializationTrace {
    std::vector<TraceEvent> events;

    [[nodiscard]] std::size_t live_object_count() const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> constructed_handles(
        TraceObjectKind kind) const;
    [[nodiscard]] std::vector<std::uint32_t> destroyed_handles(
        TraceObjectKind kind) const;
};

struct RefYyzSessionAdapter {
    std::shared_ptr<const kernel::SessionMaterializationProvider> provider;
    std::shared_ptr<MaterializationTrace> trace;
    std::uint32_t mass_state_block_handle = 0U;
    std::uint32_t mission_result_slot_handle = 0U;
    std::uint32_t first_non_state_slot_handle = 0U;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return provider != nullptr && error.empty();
    }
};

[[nodiscard]] RefYyzSessionAdapter make_session_adapter(
    const contracts::ExecutionPlanImage& image,
    AdapterOptions options = {});

struct NonTrivialObjectProbe {
    bool state_is_non_trivial = false;
    bool output_is_non_trivial = false;
    bool initial_mass_string_present = false;
    bool state_clone_restored_string = false;
    bool first_output_replace_succeeded = false;
    bool second_output_replace_succeeded = false;
    bool output_string_replaced = false;
};

// Kept behind the adapter facade so the Session probe includes no YYZ
// concrete state, output, definition, or Runtime Cell header.
[[nodiscard]] NonTrivialObjectProbe exercise_non_trivial_objects(
    kernel::Session& session, const RefYyzSessionAdapter& adapter);

} // namespace gnc::tests::ref_yyz
