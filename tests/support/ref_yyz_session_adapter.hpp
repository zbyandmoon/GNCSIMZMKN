#pragma once

#include "gnc/kernel/session.hpp"

#include <array>
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
    Boundary,
    Integration,
    MassEvolution,
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
    bool swap_first_two_preparation_materializers = false;
    bool disguise_second_preparation_as_first = false;
    bool wrong_first_runtime_factory_identity = false;
    std::size_t wrong_writer_token_boundary_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t omit_output_boundary_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t fail_after_output_boundary_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t fail_held_closure_boundary_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t cross_owner_state_read_boundary_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t cross_owner_state_read_integration_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t wrong_candidate_token_integration_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t omit_candidate_integration_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t invalid_rigid_candidate_integration_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t wrong_candidate_token_mass_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t omit_candidate_mass_ordinal =
        static_cast<std::size_t>(-1);
    std::size_t invalid_mass_candidate_ordinal =
        static_cast<std::size_t>(-1);
    bool fail_first_candidate_rearm = false;
    bool request_undeclared_preparation = false;
    bool reverse_invocation_registration = false;
};

struct BoundaryContextProbe {
    std::int64_t tick = -1;
    double sample_seconds = 0.0;
    double interval_start_seconds = 0.0;
    double interval_end_seconds = 0.0;
    std::int64_t configuration_revision = -1;
    bool quality_valid = false;
};

struct OpeningBoundaryProbe {
    std::vector<std::uint32_t> call_order;
    std::vector<BoundaryContextProbe> contexts;
    std::array<double, 3U> observation_position{};
    std::array<double, 3U> observation_velocity{};
    std::array<double, 4U> observation_attitude_wxyz{};
    std::array<double, 3U> observation_angular_rate{};
    double mass_kilograms = 0.0;
    std::array<double, 3U> center_of_mass{};
    std::array<double, 9U> inertia{};
    double guidance_altitude_error = 0.0;
    double guidance_raw_command = 0.0;
    double guidance_command = 0.0;
    double guidance_limit = 0.0;
    bool guidance_saturated = false;
    double controller_pitch_error = 0.0;
    double controller_raw_moment = 0.0;
    double controller_moment = 0.0;
    double controller_limit = 0.0;
    bool controller_saturated = false;
    std::array<double, 3U> actuator_moment{};
    std::array<double, 3U> propulsion_force{};
    std::array<double, 3U> propulsion_application_from_com{};
    std::array<double, 3U> propulsion_intrinsic_moment{};
    double mass_flow_rate = 0.0;
    std::array<double, 3U> gravity{};
    std::array<double, 3U> wind{};
    double density = 0.0;
    double speed_of_sound = 0.0;
    double airspeed = 0.0;
    double alpha = 0.0;
    double beta = 0.0;
    double dynamic_pressure = 0.0;
    double mach = 0.0;
    std::array<double, 6U> aerodynamic_coefficients{};
    std::size_t closure_contribution_count = 0U;
    std::array<double, 3U> held_force{};
    std::array<double, 3U> held_moment{};
    bool controlled_preparation_written = false;
    bool held_form_written = false;
    bool terminal_evaluator_called = false;
};

struct StepIntervalProbe {
    std::int64_t opening_tick = -1;
    std::size_t rk4_derivative_evaluations = 0U;
    double integration_mass_kilograms = 0.0;
    double mass_candidate_kilograms = 0.0;
    std::array<double, 3U> rigid_candidate_position{};
    std::array<double, 3U> rigid_candidate_velocity{};
    std::array<double, 4U> rigid_candidate_attitude_wxyz{};
    std::array<double, 3U> rigid_candidate_angular_rate{};
};

struct StepExecutionProbe {
    std::size_t integration_attempts = 0U;
    std::size_t mass_evolution_attempts = 0U;
    std::vector<StepIntervalProbe> completed_intervals;
};

struct CommittedRigidMassProbe {
    std::array<double, 3U> position{};
    std::array<double, 3U> velocity{};
    std::array<double, 4U> attitude_wxyz{};
    std::array<double, 3U> angular_rate{};
    double mass_kilograms = 0.0;
    std::array<double, 3U> center_of_mass{};
    std::array<double, 9U> inertia{};
    std::int64_t mass_sample_tick = -1;
};

struct CapturedFrameView {
    std::shared_ptr<kernel::SessionInputView> view;
    std::uint32_t slot_handle = 0U;
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
    std::shared_ptr<OpeningBoundaryProbe> opening_boundary;
    std::shared_ptr<StepExecutionProbe> step_execution;
    std::shared_ptr<CapturedFrameView> captured_input;
    std::shared_ptr<bool> undeclared_preparation_visible;
    std::uint32_t mass_state_block_handle = 0U;
    std::uint32_t rigid_state_block_handle = 0U;
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
    bool state_store_cloned_twice = false;
    bool frame_values_deferred = false;
};

// Kept behind the adapter facade so the Session probe includes no YYZ
// concrete state, output, definition, or Runtime Cell header.
[[nodiscard]] NonTrivialObjectProbe exercise_non_trivial_objects(
    const kernel::Session& session, const RefYyzSessionAdapter& adapter);

[[nodiscard]] kernel::SessionResult read_captured_stale_input(
    const RefYyzSessionAdapter& adapter) noexcept;

[[nodiscard]] kernel::SessionResult read_mass_candidate_for_qualification(
    const kernel::Session& session, const RefYyzSessionAdapter& adapter,
    double& mass_kilograms) noexcept;

[[nodiscard]] kernel::SessionResult replace_mass_candidate_for_qualification(
    kernel::Session& session, const RefYyzSessionAdapter& adapter,
    double mass_kilograms) noexcept;

[[nodiscard]] kernel::SessionResult read_committed_rigid_mass_for_qualification(
    const kernel::Session& session, const RefYyzSessionAdapter& adapter,
    CommittedRigidMassProbe& result) noexcept;

} // namespace gnc::tests::ref_yyz
