#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gnc::contracts::ExecutionPlanImage;
using gnc::kernel::SessionError;
using gnc::tests::ref_yyz::AdapterOptions;
using gnc::tests::ref_yyz::CommittedRigidMassProbe;
using gnc::tests::ref_yyz::FailurePhase;

double maximum_observed_absolute_difference = 0.0;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool near(double actual, double expected,
                        double tolerance = 2.0e-12) noexcept {
    const auto difference = std::abs(actual - expected);
    maximum_observed_absolute_difference =
        (std::max)(maximum_observed_absolute_difference, difference);
    return difference <= tolerance * (std::max)(1.0, std::abs(expected));
}

template <std::size_t Size>
[[nodiscard]] bool near_array(const std::array<double, Size>& actual,
                              const std::array<double, Size>& expected,
                              double tolerance = 2.0e-12) noexcept {
    for (std::size_t index = 0U; index < Size; ++index) {
        if (!near(actual[index], expected[index], tolerance)) return false;
    }
    return true;
}

[[nodiscard]] bool exactly_same(const CommittedRigidMassProbe& lhs,
                                const CommittedRigidMassProbe& rhs) noexcept {
    return lhs.position == rhs.position && lhs.velocity == rhs.velocity &&
           lhs.attitude_wxyz == rhs.attitude_wxyz &&
           lhs.angular_rate == rhs.angular_rate &&
           lhs.mass_kilograms == rhs.mass_kilograms &&
           lhs.center_of_mass == rhs.center_of_mass &&
           lhs.inertia == rhs.inertia &&
           lhs.mass_sample_tick == rhs.mass_sample_tick;
}

[[nodiscard]] bool same_state_blocks(
    const std::vector<gnc::kernel::SessionStateBlockInfo>& lhs,
    const std::vector<gnc::kernel::SessionStateBlockInfo>& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (lhs[index].state_block_handle != rhs[index].state_block_handle ||
            lhs[index].owner_runtime_component_handle !=
                rhs[index].owner_runtime_component_handle ||
            lhs[index].committed_slot_handle !=
                rhs[index].committed_slot_handle ||
            lhs[index].candidate_slot_handle !=
                rhs[index].candidate_slot_handle ||
            lhs[index].codec_entry_handle != rhs[index].codec_entry_handle ||
            lhs[index].committed_epoch != rhs[index].committed_epoch) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool all_frame_slots_absent(
    const gnc::kernel::Session& session) {
    const auto slots = session.frame_slots();
    return std::all_of(slots.begin(), slots.end(),
                       [](const auto& slot) { return !slot.present; });
}

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage> build_image() {
    const auto compiled = gnc::tests::ref_yyz::compile_complete_image();
    require(compiled.succeeded(), "REF-YYZ Image compilation failed");
    return std::make_shared<const ExecutionPlanImage>(*compiled.value);
}

struct SessionBundle {
    gnc::tests::ref_yyz::RefYyzSessionAdapter adapter;
    std::unique_ptr<gnc::kernel::Session> session;
};

[[nodiscard]] SessionBundle initialize_session(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    AdapterOptions options = {}) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "Session creation failed");
    const auto initialized = creation.session->initialize();
    if (!initialized) {
        throw std::runtime_error(
            std::string("Session initialization failed: ") +
            std::string(gnc::kernel::to_string(initialized.error)) + " / " +
            std::string(initialized.detail) + " / handle=" +
            std::to_string(initialized.image_handle));
    }
    return {std::move(adapter), std::move(creation.session)};
}

[[nodiscard]] CommittedRigidMassProbe committed_probe(
    const gnc::kernel::Session& session,
    const gnc::tests::ref_yyz::RefYyzSessionAdapter& adapter) {
    CommittedRigidMassProbe result;
    const auto read =
        gnc::tests::ref_yyz::read_committed_rigid_mass_for_qualification(
            session, adapter, result);
    require(static_cast<bool>(read),
            "committed rigid/mass qualification read failed");
    return result;
}

void require_tick_one_oracle(const CommittedRigidMassProbe& value) {
    require(near_array(value.position,
                       std::array<double, 3U>{
                           10.995272058823529, 0.0, 999.95096675}) &&
                near_array(value.velocity,
                           std::array<double, 3U>{
                               109.90544117647059, 0.0, -0.980665}) &&
                near_array(value.attitude_wxyz,
                           std::array<double, 4U>{1.0, 0.0, 0.0, 0.0}) &&
                near_array(value.angular_rate,
                           std::array<double, 3U>{0.0, 0.0, 0.0},
                           1.0e-10) &&
                near(value.mass_kilograms, 99.95) &&
                value.mass_sample_tick == 1,
            "tick-one committed pair differs from mission oracle");
}

void require_tick_two_oracle(const CommittedRigidMassProbe& value) {
    require(near_array(value.position,
                       std::array<double, 3U>{
                           21.981798901675346, 0.0,
                           999.8062748637297}) &&
                near_array(value.velocity,
                           std::array<double, 3U>{
                               109.82516983067299, 0.0,
                               -1.9130498687217244}) &&
                near_array(value.attitude_wxyz,
                           std::array<double, 4U>{
                               0.9999894394538129, 0.0,
                               -0.004595756830941491, 0.0}) &&
                near_array(value.angular_rate,
                           std::array<double, 3U>{
                               0.0, 0.18383108213675527, 0.0}) &&
                near(value.mass_kilograms, 99.9) &&
                value.mass_sample_tick == 2,
            "tick-two committed pair differs from mission oracle");
    require(near_array(value.center_of_mass,
                       std::array<double, 3U>{0.2, 0.0, 0.0}) &&
                near_array(value.inertia,
                           std::array<double, 9U>{
                               10.0, 0.0, 0.0, 0.0, 20.0, 0.0,
                               0.0, 0.0, 30.0}),
            "constant mass geometry changed across two commits");
}

void verify_two_continue_steps(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto bundle = initialize_session(image);
    const auto opening = committed_probe(*bundle.session, bundle.adapter);
    require(bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0 &&
                near(opening.mass_kilograms, 100.0),
            "opening committed pair is invalid");

    const auto first = bundle.session->execute_continue_step();
    require(static_cast<bool>(first), "first Continue step failed");
    const auto first_committed = committed_probe(*bundle.session,
                                                 bundle.adapter);
    require_tick_one_oracle(first_committed);
    const auto& first_summary = bundle.session->last_step_summary();
    require(first_summary.committed && first_summary.base_epoch == 0U &&
                first_summary.committed_epoch == 1U &&
                first_summary.base_tick == 0 &&
                first_summary.committed_tick == 1 &&
                first_summary.executed_callsite_handles.size() == 8U &&
                first_summary.skipped_callsite_handles.size() == 1U &&
                first_summary.integration_scope_handles.size() == 1U &&
                first_summary.candidate_slot_handles.size() == 2U &&
                first_summary.output_write_count == 9U &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.session->committed_outputs().empty(),
            "first Continue transaction metadata is incomplete");

    const auto second = bundle.session->execute_continue_step();
    if (!second) {
        throw std::runtime_error(
            std::string("second Continue step failed: ") +
            std::string(gnc::kernel::to_string(second.error)) + " / " +
            std::string(second.detail) + " / handle=" +
            std::to_string(second.image_handle));
    }
    const auto second_committed = committed_probe(*bundle.session,
                                                  bundle.adapter);
    require_tick_two_oracle(second_committed);
    const auto& second_summary = bundle.session->last_step_summary();
    require(second_summary.committed && second_summary.base_epoch == 1U &&
                second_summary.committed_epoch == 2U &&
                second_summary.base_tick == 1 &&
                second_summary.committed_tick == 2 &&
                second_summary.executed_callsite_handles.size() == 8U &&
                second_summary.skipped_callsite_handles.size() == 1U &&
                second_summary.integration_scope_handles.size() == 1U &&
                second_summary.candidate_slot_handles.size() == 2U &&
                !bundle.adapter.opening_boundary->terminal_evaluator_called &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.session->committed_outputs().empty(),
            "second Continue transaction or terminal cutoff is invalid");
    require(bundle.adapter.step_execution->completed_intervals.size() == 2U &&
                bundle.adapter.step_execution->completed_intervals[0U]
                        .opening_tick == 0 &&
                bundle.adapter.step_execution->completed_intervals[1U]
                        .opening_tick == 1 &&
                bundle.adapter.step_execution->completed_intervals[0U]
                        .rk4_derivative_evaluations == 4U &&
                bundle.adapter.step_execution->completed_intervals[1U]
                        .rk4_derivative_evaluations == 4U &&
                near(bundle.adapter.step_execution->completed_intervals[0U]
                         .integration_mass_kilograms,
                     100.0) &&
                near(bundle.adapter.step_execution->completed_intervals[1U]
                         .integration_mass_kilograms,
                     99.95),
            "Image RK4 or next-step committed-mass visibility changed");
    const auto committed_blocks = bundle.session->state_blocks();
    require(std::all_of(
                committed_blocks.begin(), committed_blocks.end(),
                [](const auto& block) {
                    return block.committed_epoch == 2U;
                }),
            "atomic state-block epochs did not advance together");
    const auto third = bundle.session->execute_continue_step();
    require(!third &&
                third.error == SessionError::InvalidLifecycleTransition &&
                bundle.session->committed_epoch() == 2U &&
                bundle.session->committed_tick() == 2,
            "Session advanced or evaluated the terminal tick");
}

template <typename Configure>
void verify_precommit_rollback(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    Configure configure, SessionError expected,
    std::string_view message) {
    AdapterOptions options;
    configure(options);
    auto bundle = initialize_session(image, options);
    const auto before_value = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto failed = bundle.session->execute_continue_step();
    const auto after_value = committed_probe(*bundle.session,
                                             bundle.adapter);
    require(!failed && failed.error == expected &&
                exactly_same(after_value, before_value) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0 &&
                !bundle.session->last_step_summary().committed &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.session->committed_outputs().empty(),
            message);
    if (bundle.adapter.captured_input->view != nullptr) {
        const auto stale =
            gnc::tests::ref_yyz::read_captured_stale_input(bundle.adapter);
        require(!stale && stale.error == SessionError::StaleFrameView,
                "rollback left a captured CycleFrame view active");
    }
    const auto retry = bundle.session->execute_continue_step();
    require(static_cast<bool>(retry) &&
                bundle.session->committed_epoch() == 1U &&
                bundle.session->committed_tick() == 1 &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.session->committed_outputs().empty(),
            "same-Session retry did not recover after rollback");
    require_tick_one_oracle(committed_probe(*bundle.session,
                                            bundle.adapter));
}

void verify_failure_matrix(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Boundary, 0U};
        },
        SessionError::InvocationFailed,
        "projection failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Boundary, 4U};
        },
        SessionError::InvocationFailed,
        "mid-boundary failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.fail_held_closure_boundary_ordinal = 6U;
        },
        SessionError::InvocationFailed,
        "held closure failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.fail_after_output_boundary_ordinal = 6U;
        },
        SessionError::InvocationFailed,
        "post-held-write failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Integration, 0U};
        },
        SessionError::InvocationFailed,
        "RK4 derivative-stage failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::MassEvolution, 0U};
        },
        SessionError::InvocationFailed,
        "mass evolution failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.invalid_rigid_candidate_integration_ordinal = 0U;
        },
        SessionError::CandidateValidationFailed,
        "rigid candidate validation failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.invalid_mass_candidate_ordinal = 0U;
        },
        SessionError::CandidateValidationFailed,
        "mass candidate validation failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.omit_candidate_mass_ordinal = 0U;
        },
        SessionError::TransactionPrecommitFailed,
        "last precommit completeness failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) { options.fail_first_candidate_rearm = true; },
        SessionError::CandidateRearmFailed,
        "candidate rearm failure changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.wrong_candidate_token_integration_ordinal = 0U;
        },
        SessionError::CandidateAuthorizationFailure,
        "wrong candidate writer token changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) { options.omit_output_boundary_ordinal = 0U; },
        SessionError::FrameSlotAbsent,
        "missing boundary input changed committed state");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.cross_owner_state_read_boundary_ordinal = 0U;
        },
        SessionError::StateAuthorizationFailure,
        "cross-owner Runtime Cell state read reached the product kernel");
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.cross_owner_state_read_integration_ordinal = 0U;
        },
        SessionError::StateAuthorizationFailure,
        "cross-owner IntegrationScope state read reached RK4");
}

void verify_two_session_isolation(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto first = gnc::kernel::create_session(image, adapter.provider);
    auto second = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(first) && static_cast<bool>(second) &&
                static_cast<bool>(first.session->initialize()) &&
                static_cast<bool>(second.session->initialize()),
            "two Sessions could not share the immutable Image/provider");
    const auto second_before = committed_probe(*second.session, adapter);
    require(static_cast<bool>(first.session->execute_continue_step()),
            "first isolated Session step failed");
    require(exactly_same(committed_probe(*second.session, adapter),
                         second_before) &&
                second.session->committed_epoch() == 0U &&
                second.session->committed_tick() == 0 &&
                !second.session->frame_open() &&
                second.session->committed_outputs().empty(),
            "first Session commit crossed the second Session boundary");
    require(static_cast<bool>(second.session->execute_continue_step()),
            "second isolated Session step failed");
    const auto first_value = committed_probe(*first.session, adapter);
    const auto second_value = committed_probe(*second.session, adapter);
    require(exactly_same(first_value, second_value) &&
                first.session->committed_epoch() == 1U &&
                second.session->committed_epoch() == 1U,
            "isolated Sessions produced different first commits");
}

void run() {
    const auto image = build_image();
    verify_two_continue_steps(image);
    verify_failure_matrix(image);
    verify_two_session_isolation(image);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_step_transaction_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 REF-YYZ atomic Continue transactions: PASS"
                  << " (max_abs_difference=" << std::setprecision(17)
                  << maximum_observed_absolute_difference << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 REF-YYZ atomic Continue transactions: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
