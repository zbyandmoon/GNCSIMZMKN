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
using gnc::tests::ref_yyz::MissionResultProbe;
using gnc::tests::ref_yyz::SealedObservationSnapshot;

namespace yyz = gnc::packages::yyz;

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

[[nodiscard]] bool same_committed_outputs(
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.slot_handle != right.slot_handle ||
            left.codec_entry_handle != right.codec_entry_handle ||
            left.present != right.present ||
            left.generation != right.generation ||
            left.sequence != right.sequence ||
            left.sample_tick != right.sample_tick ||
            left.sample_time_seconds != right.sample_time_seconds ||
            left.interval_start_seconds != right.interval_start_seconds ||
            left.interval_end_seconds != right.interval_end_seconds ||
            left.quality != right.quality ||
            left.terminal_result != right.terminal_result) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool near_vector(
    const gnc::foundation::Vec3& lhs,
    const gnc::foundation::Vec3& rhs) noexcept {
    for (Eigen::Index index = 0; index < 3; ++index) {
        if (!near(lhs(index), rhs(index))) return false;
    }
    return true;
}

[[nodiscard]] bool near_matrix(
    const gnc::foundation::Mat3& lhs,
    const gnc::foundation::Mat3& rhs) noexcept {
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (!near(lhs(row, column), rhs(row, column))) return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_sample_context(
    const gnc::contracts::SampleContext& lhs,
    const gnc::contracts::SampleContext& rhs) noexcept {
    return lhs.frame == rhs.frame &&
           lhs.clock_domain == rhs.clock_domain &&
           lhs.sample_time.tick == rhs.sample_time.tick &&
           near(lhs.sample_time.seconds, rhs.sample_time.seconds) &&
           lhs.configuration_revision == rhs.configuration_revision &&
           lhs.quality == rhs.quality;
}

[[nodiscard]] bool same_interval_context(
    const gnc::contracts::IntervalSampleContext& lhs,
    const gnc::contracts::IntervalSampleContext& rhs) noexcept {
    return same_sample_context(lhs.sample, rhs.sample) &&
           lhs.validity.effective_from.tick ==
               rhs.validity.effective_from.tick &&
           near(lhs.validity.effective_from.seconds,
                rhs.validity.effective_from.seconds) &&
           lhs.validity.effective_until.tick ==
               rhs.validity.effective_until.tick &&
           near(lhs.validity.effective_until.seconds,
                rhs.validity.effective_until.seconds);
}

[[nodiscard]] bool same_rigid_state(
    const yyz::RigidState& lhs, const yyz::RigidState& rhs) noexcept {
    return near_vector(lhs.position.value, rhs.position.value) &&
           near_vector(lhs.velocity.value, rhs.velocity.value) &&
           near_array(gnc::foundation::quaternion_to_wxyz(lhs.attitude.value),
                      gnc::foundation::quaternion_to_wxyz(rhs.attitude.value)) &&
           near_vector(lhs.angular_rate.value, rhs.angular_rate.value);
}

[[nodiscard]] bool same_rigid_observation(
    const yyz::CommittedRigidObservation& lhs,
    const yyz::CommittedRigidObservation& rhs) noexcept {
    return same_sample_context(lhs.context, rhs.context) &&
           same_rigid_state(lhs.state, rhs.state);
}

[[nodiscard]] bool same_mass_properties(
    const yyz::MassPropertiesInput& lhs,
    const yyz::MassPropertiesInput& rhs) noexcept {
    return same_interval_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           near(lhs.mass_kilograms, rhs.mass_kilograms) &&
           near_vector(lhs.body_origin_to_center_of_mass.value,
                       rhs.body_origin_to_center_of_mass.value) &&
           near_matrix(lhs.inertia_about_center_of_mass.value,
                       rhs.inertia_about_center_of_mass.value);
}

[[nodiscard]] bool same_applied_wrench(
    const yyz::AppliedBodyWrenchInput& lhs,
    const yyz::AppliedBodyWrenchInput& rhs) noexcept {
    return same_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           near_vector(lhs.force.value, rhs.force.value) &&
           near_vector(lhs.body_origin_to_application.value,
                       rhs.body_origin_to_application.value) &&
           near_vector(lhs.intrinsic_moment_at_application.value,
                       rhs.intrinsic_moment_at_application.value);
}

[[nodiscard]] bool same_environment(
    const yyz::EnvironmentInput& lhs,
    const yyz::EnvironmentInput& rhs) noexcept {
    return same_sample_context(lhs.context, rhs.context) &&
           near_vector(lhs.gravity.value, rhs.gravity.value) &&
           near_vector(lhs.velocity_airmass.value,
                       rhs.velocity_airmass.value) &&
           near(lhs.density_kilograms_per_cubic_meter,
                rhs.density_kilograms_per_cubic_meter) &&
           near(lhs.speed_of_sound_meters_per_second,
                rhs.speed_of_sound_meters_per_second);
}

[[nodiscard]] bool same_rigid_preparation(
    const yyz::ControlledRigidBoundaryPreparationOutput& lhs,
    const yyz::ControlledRigidBoundaryPreparationOutput& rhs) noexcept {
    if (!same_environment(lhs.environment_response,
                          rhs.environment_response) ||
        !near_array(lhs.aerodynamic_coefficients
                        .coefficients_ca_cy_cn_cl_cm_cn,
                    rhs.aerodynamic_coefficients
                        .coefficients_ca_cy_cn_cl_cm_cn) ||
        !near_vector(lhs.closure_request.body_origin_to_center_of_mass.value,
                     rhs.closure_request.body_origin_to_center_of_mass.value) ||
        lhs.closure_request.contributions.size() !=
            rhs.closure_request.contributions.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < lhs.closure_request.contributions.size(); ++index) {
        if (!same_applied_wrench(lhs.closure_request.contributions[index],
                                 rhs.closure_request.contributions[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_rigid_form(
    const yyz::RigidFormInput& lhs,
    const yyz::RigidFormInput& rhs) noexcept {
    return near_vector(lhs.force_total.value, rhs.force_total.value) &&
           near_vector(lhs.moment_total_about_center_of_mass.value,
                       rhs.moment_total_about_center_of_mass.value);
}

[[nodiscard]] bool same_guidance(
    const yyz::AltitudePitchGuidanceOutput& lhs,
    const yyz::AltitudePitchGuidanceOutput& rhs) noexcept {
    return same_rigid_observation(lhs.source_observation,
                                  rhs.source_observation) &&
           near(lhs.measured_pitch_radians, rhs.measured_pitch_radians) &&
           near(lhs.measured_pitch_rate_radians_per_second,
                rhs.measured_pitch_rate_radians_per_second) &&
           near(lhs.altitude_error_meters, rhs.altitude_error_meters) &&
           near(lhs.altitude_feedback_radians,
                rhs.altitude_feedback_radians) &&
           near(lhs.vertical_speed_feedback_radians,
                rhs.vertical_speed_feedback_radians) &&
           near(lhs.raw_pitch_command_radians,
                rhs.raw_pitch_command_radians) &&
           near(lhs.pitch_command_radians, rhs.pitch_command_radians) &&
           lhs.saturated == rhs.saturated;
}

[[nodiscard]] bool same_controller(
    const yyz::PitchMomentControllerOutput& lhs,
    const yyz::PitchMomentControllerOutput& rhs) noexcept {
    return same_sample_context(lhs.context, rhs.context) &&
           near(lhs.pitch_error_radians, rhs.pitch_error_radians) &&
           near(lhs.proportional_moment_newton_meters,
                rhs.proportional_moment_newton_meters) &&
           near(lhs.rate_damping_moment_newton_meters,
                rhs.rate_damping_moment_newton_meters) &&
           near(lhs.raw_moment_command_newton_meters,
                rhs.raw_moment_command_newton_meters) &&
           near(lhs.moment_command_newton_meters,
                rhs.moment_command_newton_meters) &&
           lhs.saturated == rhs.saturated;
}

[[nodiscard]] bool same_actuator(
    const yyz::IdealBodyMomentActuatorOutput& lhs,
    const yyz::IdealBodyMomentActuatorOutput& rhs) noexcept {
    return same_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           near_vector(lhs.moment_about_center_of_mass.value,
                       rhs.moment_about_center_of_mass.value);
}

[[nodiscard]] bool same_propulsion(
    const yyz::SuppliedPropulsionBodyWrench& lhs,
    const yyz::SuppliedPropulsionBodyWrench& rhs) noexcept {
    return same_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           near_vector(lhs.force.value, rhs.force.value) &&
           near_vector(lhs.center_of_mass_to_application.value,
                       rhs.center_of_mass_to_application.value) &&
           near_vector(lhs.intrinsic_moment_at_application.value,
                       rhs.intrinsic_moment_at_application.value);
}

[[nodiscard]] bool same_mass_flow(
    const yyz::MassFlowIntervalInput& lhs,
    const yyz::MassFlowIntervalInput& rhs) noexcept {
    return same_interval_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           near(lhs.fuel_consumption_rate_kilograms_per_second,
                rhs.fuel_consumption_rate_kilograms_per_second);
}

[[nodiscard]] bool same_mass_state(
    const yyz::MassState& lhs, const yyz::MassState& rhs) noexcept {
    return same_sample_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           near(lhs.mass_kilograms, rhs.mass_kilograms) &&
           near_vector(lhs.body_origin_to_center_of_mass.value,
                       rhs.body_origin_to_center_of_mass.value) &&
           near_matrix(lhs.inertia_about_center_of_mass.value,
                       rhs.inertia_about_center_of_mass.value);
}

[[nodiscard]] bool same_committed_boundary(
    const yyz::CommittedRigidMassBoundary& lhs,
    const yyz::CommittedRigidMassBoundary& rhs) noexcept {
    return same_sample_context(lhs.rigid_context, rhs.rigid_context) &&
           same_rigid_state(lhs.rigid_state, rhs.rigid_state) &&
           same_mass_state(lhs.mass_state, rhs.mass_state);
}

[[nodiscard]] bool same_mission_metrics(
    const yyz::MissionMetrics& lhs,
    const yyz::MissionMetrics& rhs) noexcept {
    return near(lhs.duration_seconds, rhs.duration_seconds) &&
           near(lhs.downrange_meters, rhs.downrange_meters) &&
           near(lhs.vertical_displacement_meters,
                rhs.vertical_displacement_meters) &&
           near(lhs.remaining_mass_kilograms,
                rhs.remaining_mass_kilograms) &&
           near(lhs.consumed_mass_kilograms,
                rhs.consumed_mass_kilograms) &&
           near(lhs.speed_meters_per_second,
                rhs.speed_meters_per_second);
}

[[nodiscard]] bool same_mission_result(
    const yyz::CommittedMissionResultOutput& lhs,
    const yyz::CommittedMissionResultOutput& rhs) noexcept {
    if (lhs.status != rhs.status || lhs.initial_tick != rhs.initial_tick ||
        lhs.final_tick != rhs.final_tick ||
        !near(lhs.final_time_seconds, rhs.final_time_seconds) ||
        lhs.termination.action != rhs.termination.action ||
        lhs.termination.reason_code != rhs.termination.reason_code ||
        !near(lhs.termination.trigger_time_seconds,
              rhs.termination.trigger_time_seconds) ||
        lhs.termination.priority != rhs.termination.priority ||
        lhs.metrics.evaluated_sample_count !=
            rhs.metrics.evaluated_sample_count ||
        !same_mission_metrics(lhs.metrics.terminal,
                              rhs.metrics.terminal) ||
        !near(lhs.metrics.peak_speed_meters_per_second,
              rhs.metrics.peak_speed_meters_per_second) ||
        lhs.metrics.peak_speed_tick != rhs.metrics.peak_speed_tick ||
        !near(lhs.metrics.maximum_downrange_meters,
              rhs.metrics.maximum_downrange_meters) ||
        lhs.metrics.maximum_downrange_tick !=
            rhs.metrics.maximum_downrange_tick ||
        !near(lhs.metrics.minimum_remaining_mass_kilograms,
              rhs.metrics.minimum_remaining_mass_kilograms) ||
        lhs.metrics.minimum_remaining_mass_tick !=
            rhs.metrics.minimum_remaining_mass_tick ||
        !same_committed_boundary(lhs.terminal_boundary,
                                 rhs.terminal_boundary)) {
        return false;
    }
    for (std::size_t index = 0U;
         index < lhs.terminal_predicates.size(); ++index) {
        const auto& left = lhs.terminal_predicates[index];
        const auto& right = rhs.terminal_predicates[index];
        if (left.predicate_id != right.predicate_id ||
            !near(left.observed, right.observed) || left.met != right.met ||
            left.action != right.action ||
            left.reason_code != right.reason_code ||
            left.priority != right.priority) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_seal_metadata(
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.slot_handle != right.slot_handle ||
            left.codec_entry_handle != right.codec_entry_handle ||
            left.present != right.present ||
            left.generation != right.generation ||
            left.sequence != right.sequence ||
            left.sample_tick != right.sample_tick ||
            !near(left.sample_time_seconds, right.sample_time_seconds) ||
            !near(left.interval_start_seconds,
                  right.interval_start_seconds) ||
            !near(left.interval_end_seconds, right.interval_end_seconds) ||
            left.quality != right.quality ||
            left.terminal_result != right.terminal_result) {
            return false;
        }
    }
    return true;
}

template <typename Value, typename Equal>
[[nodiscard]] bool same_optional_payload(
    const std::optional<Value>& lhs, const std::optional<Value>& rhs,
    Equal equal) noexcept {
    return lhs.has_value() == rhs.has_value() &&
           (!lhs.has_value() || equal(*lhs, *rhs));
}

[[nodiscard]] bool same_sealed_observation_snapshot(
    const SealedObservationSnapshot& lhs,
    const SealedObservationSnapshot& rhs) noexcept {
    return same_seal_metadata(lhs.seals, rhs.seals) &&
           same_optional_payload(lhs.rigid_observation,
                                 rhs.rigid_observation,
                                 same_rigid_observation) &&
           same_optional_payload(lhs.rigid_form, rhs.rigid_form,
                                 same_rigid_form) &&
           same_optional_payload(lhs.rigid_preparation,
                                 rhs.rigid_preparation,
                                 same_rigid_preparation) &&
           same_optional_payload(lhs.mass_properties,
                                 rhs.mass_properties,
                                 same_mass_properties) &&
           same_optional_payload(lhs.guidance, rhs.guidance,
                                 same_guidance) &&
           same_optional_payload(lhs.controller, rhs.controller,
                                 same_controller) &&
           same_optional_payload(lhs.actuator, rhs.actuator,
                                 same_actuator) &&
           same_optional_payload(lhs.propulsion, rhs.propulsion,
                                 same_propulsion) &&
           same_optional_payload(lhs.mass_flow, rhs.mass_flow,
                                 same_mass_flow) &&
           same_optional_payload(lhs.mission_result,
                                 rhs.mission_result,
                                 same_mission_result);
}

[[nodiscard]] bool all_frame_slots_absent(
    const gnc::kernel::Session& session) {
    const auto slots = session.frame_slots();
    return std::all_of(slots.begin(), slots.end(),
                       [](const auto& slot) { return !slot.present; });
}

void require_history(const gnc::kernel::Session& session,
                     std::size_t sample_count,
                     std::int64_t first_tick,
                     std::int64_t last_tick) {
    const auto histories = session.committed_histories();
    require(histories.size() == 1U &&
                histories.front().history_depth == 3U &&
                histories.front().sample_count == sample_count &&
                histories.front().member_count == 2U &&
                histories.front().first_tick == first_tick &&
                histories.front().last_tick == last_tick,
            "committed evaluator history metadata changed");
}

void require_sealed_boundary(const gnc::kernel::Session& session,
                             std::int64_t tick,
                             std::size_t output_count,
                             bool terminal_result) {
    const auto outputs = session.committed_outputs();
    require(outputs.size() == output_count,
            "sealed boundary output count changed");
    std::vector<std::uint32_t> handles;
    handles.reserve(outputs.size());
    std::size_t terminal_count = 0U;
    for (const auto& output : outputs) {
        require(output.present && output.codec_entry_handle != 0U &&
                    output.generation != 0U && output.sequence != 0U &&
                    output.sample_tick == tick &&
                    output.quality == gnc::contracts::DataQuality::Valid,
                "sealed boundary output metadata is invalid");
        handles.push_back(output.slot_handle);
        terminal_count += static_cast<std::size_t>(output.terminal_result);
    }
    std::sort(handles.begin(), handles.end());
    require(std::adjacent_find(handles.begin(), handles.end()) ==
                    handles.end() &&
                terminal_count ==
                    static_cast<std::size_t>(terminal_result) &&
                (!terminal_result ||
                 (!outputs.empty() && outputs.back().terminal_result &&
                  std::none_of(outputs.begin(), outputs.end() - 1,
                               [](const auto& output) {
                                   return output.terminal_result;
                               }))),
            "sealed boundary output identity is invalid");
}

[[nodiscard]] MissionResultProbe mission_result_probe(
    const gnc::kernel::Session& session,
    const gnc::tests::ref_yyz::RefYyzSessionAdapter& adapter) {
    MissionResultProbe result;
    const auto read =
        gnc::tests::ref_yyz::read_mission_result_for_qualification(
            session, adapter, result);
    require(static_cast<bool>(read),
            "sealed mission-result qualification read failed");
    return result;
}

void require_mission_oracle(const MissionResultProbe& value) {
    require(value.present && value.completed && value.initial_tick == 0 &&
                value.final_tick == 2 && near(value.final_time_seconds, 0.2) &&
                value.reason_code == "downrange-goal" &&
                value.priority == 200 &&
                value.evaluated_sample_count == 3U &&
                near(value.duration_seconds, 0.2) &&
                near(value.downrange_meters, 21.981798901675346) &&
                near(value.remaining_mass_kilograms, 99.9) &&
                near(value.consumed_mass_kilograms, 0.1) &&
                near(value.terminal_speed_meters_per_second,
                     109.84183032040381) &&
                value.terminal_tick == 2,
            "terminal mission result differs from the mission oracle");
}

[[nodiscard]] bool exactly_same(const MissionResultProbe& lhs,
                                const MissionResultProbe& rhs) noexcept {
    return lhs.present == rhs.present && lhs.completed == rhs.completed &&
           lhs.initial_tick == rhs.initial_tick &&
           lhs.final_tick == rhs.final_tick &&
           lhs.final_time_seconds == rhs.final_time_seconds &&
           lhs.reason_code == rhs.reason_code && lhs.priority == rhs.priority &&
           lhs.evaluated_sample_count == rhs.evaluated_sample_count &&
           lhs.duration_seconds == rhs.duration_seconds &&
           lhs.downrange_meters == rhs.downrange_meters &&
           lhs.remaining_mass_kilograms == rhs.remaining_mass_kilograms &&
           lhs.consumed_mass_kilograms == rhs.consumed_mass_kilograms &&
           lhs.terminal_speed_meters_per_second ==
               rhs.terminal_speed_meters_per_second &&
           lhs.terminal_tick == rhs.terminal_tick;
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

void verify_complete_step_transactions(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto bundle = initialize_session(image);
    const auto& transaction = image->transactions().front();
    const auto* continue_branch = gnc::contracts::find_transaction_branch(
        transaction, gnc::contracts::TransactionBranch::Continue);
    const auto* terminal_branch = gnc::contracts::find_transaction_branch(
        transaction, gnc::contracts::TransactionBranch::Terminal);
    require(continue_branch != nullptr && terminal_branch != nullptr,
            "Image transaction branches are missing");
    const auto opening = committed_probe(*bundle.session, bundle.adapter);
    require(bundle.session->state() == gnc::kernel::SessionState::Initialized &&
                bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0 &&
                near(opening.mass_kilograms, 100.0),
            "opening committed boundary is invalid");
    require_history(*bundle.session, 0U, 0, 0);
    require(bundle.session->committed_outputs().empty(),
            "opening Session already has a sealed boundary");

    const auto first = bundle.session->execute_step();
    require(static_cast<bool>(first), "first Continue step failed");
    require_tick_one_oracle(committed_probe(*bundle.session,
                                            bundle.adapter));
    const auto& first_journal = bundle.session->last_step_journal();
    require(bundle.session->state() == gnc::kernel::SessionState::Initialized &&
                first_journal.transaction_handle == transaction.handle &&
                first_journal.branch_selected &&
                first_journal.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                first_journal.committed && first_journal.prevalidated &&
                first_journal.history_staged &&
                first_journal.observation_seal_staged &&
                !first_journal.result_seal_staged &&
                !first_journal.terminal_result_present &&
                first_journal.base_epoch == 0U &&
                first_journal.committed_epoch == 1U &&
                first_journal.base_tick == 0 &&
                first_journal.committed_tick == 1 &&
                first_journal.executed_callsite_handles.size() == 8U &&
                first_journal.skipped_callsite_handles.size() == 1U &&
                first_journal.integration_scope_handles.size() == 1U &&
                first_journal.candidate_slot_handles.size() == 2U &&
                first_journal.candidates.size() == 2U &&
                std::all_of(first_journal.candidates.begin(),
                            first_journal.candidates.end(),
                            [](const auto& candidate) {
                                return candidate.present && candidate.valid &&
                                       candidate.base_epoch == 0U &&
                                       candidate.generation != 0U;
                            }) &&
                first_journal.output_write_count == 9U &&
                first_journal.histories.size() == 1U &&
                first_journal.histories.front().staged_sample_tick == 0 &&
                first_journal.histories.front().prospective_sample_count ==
                    1U &&
                first_journal.seals.size() ==
                    continue_branch->sealed_output_slot_handles.size() &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session),
            "first Continue StepJournal is incomplete");
    require_history(*bundle.session, 1U, 0, 0);
    require_sealed_boundary(
        *bundle.session, 0,
        continue_branch->sealed_output_slot_handles.size(), false);
    require(!gnc::tests::ref_yyz::read_captured_stale_input(bundle.adapter) &&
                gnc::tests::ref_yyz::read_captured_stale_input(bundle.adapter)
                        .error == SessionError::StaleFrameView,
            "sealed first boundary left a frame view active");

    const auto second = bundle.session->execute_step();
    require(static_cast<bool>(second), "second Continue step failed");
    require_tick_two_oracle(committed_probe(*bundle.session,
                                            bundle.adapter));
    const auto& second_journal = bundle.session->last_step_journal();
    require(second_journal.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                second_journal.committed && second_journal.prevalidated &&
                second_journal.base_epoch == 1U &&
                second_journal.committed_epoch == 2U &&
                second_journal.base_tick == 1 &&
                second_journal.committed_tick == 2 &&
                second_journal.executed_callsite_handles.size() == 8U &&
                second_journal.skipped_callsite_handles.size() == 1U &&
                second_journal.integration_scope_handles.size() == 1U &&
                second_journal.candidate_slot_handles.size() == 2U &&
                second_journal.output_write_count == 9U &&
                second_journal.histories.front().staged_sample_tick == 1 &&
                second_journal.histories.front().prospective_sample_count ==
                    2U &&
                !bundle.adapter.opening_boundary->terminal_evaluator_called,
            "second Continue StepJournal or evaluator cutoff is invalid");
    require_history(*bundle.session, 2U, 0, 1);
    require_sealed_boundary(
        *bundle.session, 1,
        continue_branch->sealed_output_slot_handles.size(), false);

    const auto before_terminal = committed_probe(*bundle.session,
                                                 bundle.adapter);
    const auto terminal = bundle.session->execute_step();
    if (!terminal) {
        const auto failed_slot = std::find_if(
            image->slots().begin(), image->slots().end(),
            [&terminal](const auto& slot) {
                return slot.handle == terminal.image_handle;
            });
        throw std::runtime_error(
            std::string("Terminal step failed: ") +
            std::string(gnc::kernel::to_string(terminal.error)) + " / " +
            std::string(terminal.detail) + " / handle=" +
            std::to_string(terminal.image_handle) + " / slot=" +
            (failed_slot == image->slots().end()
                 ? std::string("unknown")
                 : failed_slot->slot_id) + " / evaluator_calls=" +
            std::to_string(
                bundle.adapter.opening_boundary->terminal_evaluator_calls) +
            " / executed=" +
            std::to_string(bundle.session->last_step_journal()
                               .executed_callsite_handles.size()) +
            " / skipped=" +
            std::to_string(bundle.session->last_step_journal()
                               .skipped_callsite_handles.size()));
    }
    const auto after_terminal = committed_probe(*bundle.session,
                                                bundle.adapter);
    const auto& terminal_journal = bundle.session->last_step_journal();
    require(bundle.session->state() == gnc::kernel::SessionState::Completed &&
                bundle.session->committed_epoch() == 3U &&
                bundle.session->committed_tick() == 2 &&
                exactly_same(after_terminal, before_terminal) &&
                terminal_journal.branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                terminal_journal.committed &&
                terminal_journal.prevalidated &&
                terminal_journal.history_staged &&
                terminal_journal.observation_seal_staged &&
                terminal_journal.result_seal_staged &&
                terminal_journal.terminal_result_present &&
                terminal_journal.base_epoch == 2U &&
                terminal_journal.committed_epoch == 3U &&
                terminal_journal.base_tick == 2 &&
                terminal_journal.committed_tick == 2 &&
                terminal_journal.executed_callsite_handles.size() == 8U &&
                terminal_journal.skipped_callsite_handles.empty() &&
                terminal_journal.integration_scope_handles.empty() &&
                terminal_journal.candidate_slot_handles.empty() &&
                terminal_journal.output_write_count == 10U &&
                terminal_journal.histories.front().staged_sample_tick == 2 &&
                terminal_journal.histories.front().prospective_sample_count ==
                    3U &&
                terminal_journal.seals.size() ==
                    terminal_branch->sealed_output_slot_handles.size() &&
                bundle.adapter.opening_boundary->terminal_evaluator_calls ==
                    1U &&
                bundle.adapter.step_execution->integration_attempts == 2U &&
                bundle.adapter.step_execution->mass_evolution_attempts == 2U &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session),
            "Terminal StepJournal, epoch, or preserved state is invalid");
    require_history(*bundle.session, 3U, 0, 2);
    require_sealed_boundary(
        *bundle.session, 2,
        terminal_branch->sealed_output_slot_handles.size(), true);
    require_mission_oracle(mission_result_probe(*bundle.session,
                                                bundle.adapter));
    const auto terminal_blocks = bundle.session->state_blocks();
    require(std::all_of(terminal_blocks.begin(), terminal_blocks.end(),
                        [](const auto& block) {
                            return block.committed_epoch == 3U;
                        }),
            "Terminal ModelCommit did not advance every state-block epoch");

    const auto slot_constructs =
        bundle.adapter.trace->constructed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    const auto slot_destroys =
        bundle.adapter.trace->destroyed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    for (const auto held : transaction.held_slot_handles) {
        require(std::count(slot_constructs.begin(), slot_constructs.end(),
                           held) == 3 &&
                    std::count(slot_destroys.begin(), slot_destroys.end(),
                               held) == 3,
                "IntegrationHeld value escaped its transaction lifetime");
    }
    const auto rejected = bundle.session->execute_step();
    require(!rejected &&
                rejected.error == SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Completed &&
                bundle.session->committed_epoch() == 3U &&
                bundle.session->committed_tick() == 2 &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_terminal),
            "Completed Session accepted another execute_step call");
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
    const auto failed = bundle.session->execute_step();
    const auto after_value = committed_probe(*bundle.session,
                                             bundle.adapter);
    require(!failed && failed.error == expected &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                exactly_same(after_value, before_value) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0 &&
                !bundle.session->last_step_summary().committed &&
                bundle.session->last_step_journal().primary_failure.error ==
                    expected &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.session->committed_outputs().empty(),
            message);
    require_history(*bundle.session, 0U, 0, 0);
    if (bundle.adapter.captured_input->view != nullptr) {
        const auto stale =
            gnc::tests::ref_yyz::read_captured_stale_input(bundle.adapter);
        require(!stale && stale.error == SessionError::StaleFrameView,
                "rollback left a captured CycleFrame view active");
    }
    const auto retry = bundle.session->execute_step();
    require(!retry &&
                retry.error == SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0,
            "Failed Session accepted a same-Session retry");
    auto fresh = initialize_session(image);
    require(static_cast<bool>(fresh.session->execute_step()),
            "fresh Session did not recover after an execution failure");
    require_tick_one_oracle(committed_probe(*fresh.session,
                                            fresh.adapter));
}

void verify_failure_matrix(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    verify_precommit_rollback(
        image,
        [](auto& options) { options.fail_state_copy_ordinal = 2U; },
        SessionError::HistoryValidationFailed,
        "history staging failure changed committed state");
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
    verify_precommit_rollback(
        image,
        [](auto& options) {
            options.fail_cycle_output_seal_clone = true;
        },
        SessionError::ObservationSealFailed,
        "observation seal staging failure changed committed state");
}

template <typename Configure>
void verify_nonzero_rollback(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    Configure configure, SessionError expected,
    std::string_view message) {
    AdapterOptions options;
    configure(options);
    auto bundle = initialize_session(image, options);
    require(static_cast<bool>(bundle.session->execute_step()),
            "nonzero rollback fixture could not commit tick zero");
    const auto before_value = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto before_outputs = bundle.session->committed_outputs();
    require_history(*bundle.session, 1U, 0, 0);
    const auto failed = bundle.session->execute_step();
    const auto after_value = committed_probe(*bundle.session,
                                             bundle.adapter);
    require(!failed && failed.error == expected &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                bundle.session->committed_epoch() == 1U &&
                bundle.session->committed_tick() == 1 &&
                exactly_same(after_value, before_value) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                same_committed_outputs(bundle.session->committed_outputs(),
                                       before_outputs) &&
                bundle.session->last_step_journal().base_epoch == 1U &&
                bundle.session->last_step_journal().base_tick == 1 &&
                !bundle.session->last_step_journal().committed &&
                bundle.session->last_step_journal().primary_failure.error ==
                    expected &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session),
            message);
    require_history(*bundle.session, 1U, 0, 0);
    require_sealed_boundary(*bundle.session, 0, before_outputs.size(), false);
    auto fresh = initialize_session(image);
    require(static_cast<bool>(fresh.session->execute_step()) &&
                static_cast<bool>(fresh.session->execute_step()) &&
                fresh.session->committed_epoch() == 2U &&
                fresh.session->committed_tick() == 2,
            "fresh Session failed after a nonzero rollback");
}

void verify_nonzero_failure_matrix(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    verify_nonzero_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Boundary, 8U};
        },
        SessionError::InvocationFailed,
        "second-boundary failure changed the tick-one commit");
    verify_nonzero_rollback(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Integration, 1U};
        },
        SessionError::InvocationFailed,
        "second integration failure changed the tick-one commit");
    verify_nonzero_rollback(
        image,
        [](auto& options) { options.fail_state_copy_ordinal = 3U; },
        SessionError::HistoryValidationFailed,
        "second history staging failure changed the tick-one commit");
    verify_nonzero_rollback(
        image,
        [](auto& options) {
            options.fail_cycle_output_copy_ordinal = 3U;
        },
        SessionError::ObservationSealFailed,
        "second observation seal failure changed the tick-one commit");
    verify_nonzero_rollback(
        image,
        [](auto& options) {
            options.omit_candidate_mass_ordinal = 1U;
        },
        SessionError::TransactionPrecommitFailed,
        "second final-precommit failure changed the tick-one commit");
}

template <typename Configure>
void verify_terminal_failure(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    Configure configure, SessionError expected,
    std::string_view message) {
    AdapterOptions options;
    configure(options);
    auto bundle = initialize_session(image, options);
    require(static_cast<bool>(bundle.session->execute_step()) &&
                static_cast<bool>(bundle.session->execute_step()),
            "terminal failure fixture could not reach tick two");
    const auto before_value = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto before_outputs = bundle.session->committed_outputs();
    require_history(*bundle.session, 2U, 0, 1);
    const auto failed = bundle.session->execute_step();
    const auto after_value = committed_probe(*bundle.session,
                                             bundle.adapter);
    MissionResultProbe absent;
    const auto absent_read =
        gnc::tests::ref_yyz::read_mission_result_for_qualification(
            *bundle.session, bundle.adapter, absent);
    require(!failed && failed.error == expected &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                bundle.session->committed_epoch() == 2U &&
                bundle.session->committed_tick() == 2 &&
                exactly_same(after_value, before_value) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                same_committed_outputs(bundle.session->committed_outputs(),
                                       before_outputs) &&
                !absent_read &&
                absent_read.error == SessionError::FrameSlotAbsent &&
                !absent.present &&
                bundle.session->last_step_journal().branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                bundle.session->last_step_journal().base_epoch == 2U &&
                bundle.session->last_step_journal().base_tick == 2 &&
                !bundle.session->last_step_journal().committed &&
                bundle.session->last_step_journal().primary_failure.error ==
                    expected &&
                bundle.adapter.step_execution->integration_attempts == 2U &&
                bundle.adapter.step_execution->mass_evolution_attempts == 2U &&
                !bundle.session->frame_open() &&
                all_frame_slots_absent(*bundle.session),
            message);
    require_history(*bundle.session, 2U, 0, 1);
    require_sealed_boundary(*bundle.session, 1, before_outputs.size(), false);
    const auto rejected = bundle.session->execute_step();
    require(!rejected &&
                rejected.error == SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Failed,
            "terminally Failed Session accepted another step");
    auto fresh = initialize_session(image);
    require(static_cast<bool>(fresh.session->execute_step()) &&
                static_cast<bool>(fresh.session->execute_step()) &&
                static_cast<bool>(fresh.session->execute_step()) &&
                fresh.session->state() ==
                    gnc::kernel::SessionState::Completed,
            "fresh Session failed after a terminal execution failure");
    require_mission_oracle(mission_result_probe(*fresh.session,
                                                fresh.adapter));
}

void verify_terminal_failure_matrix(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    verify_terminal_failure(
        image,
        [](auto& options) { options.fail_state_copy_ordinal = 5U; },
        SessionError::HistoryValidationFailed,
        "terminal history staging failure changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.out_of_range_terminal_history_sample = true;
        },
        SessionError::HistoryAuthorizationFailure,
        "incomplete terminal history changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.wrong_terminal_history_handle = true;
        },
        SessionError::HistoryAuthorizationFailure,
        "wrong terminal history handle changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.reverse_terminal_history_members = true;
        },
        SessionError::ObjectTypeMismatch,
        "reordered terminal history changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.wrong_terminal_history_member_type = true;
        },
        SessionError::ObjectTypeMismatch,
        "wrong terminal history member type changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) { options.fail_terminal_evaluator = true; },
        SessionError::InvocationFailed,
        "terminal evaluator failure changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) { options.omit_terminal_output = true; },
        SessionError::FrameSlotAbsent,
        "omitted terminal output changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.wrong_terminal_writer_token = true;
        },
        SessionError::WriterAuthorizationFailure,
        "wrong terminal writer token changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) { options.invalid_terminal_output = true; },
        SessionError::ObjectValidationFailed,
        "invalid terminal output changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.fail_terminal_result_seal_clone = true;
        },
        SessionError::ObservationSealFailed,
        "terminal result seal staging failure changed the tick-two commit");
    verify_terminal_failure(
        image,
        [](auto& options) {
            options.fail_terminal_final_precommit = true;
        },
        SessionError::ObservationSealFailed,
        "terminal final-precommit failure changed the tick-two commit");
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
    require(static_cast<bool>(first.session->execute_step()) &&
                static_cast<bool>(first.session->execute_step()) &&
                static_cast<bool>(first.session->execute_step()),
            "first isolated Session run failed");
    require(exactly_same(committed_probe(*second.session, adapter),
                          second_before) &&
                second.session->committed_epoch() == 0U &&
                second.session->committed_tick() == 0 &&
                second.session->state() ==
                    gnc::kernel::SessionState::Initialized &&
                !second.session->frame_open() &&
                second.session->committed_outputs().empty(),
            "first Session history or seal crossed the second Session boundary");
    require_history(*second.session, 0U, 0, 0);
    require(static_cast<bool>(second.session->execute_step()) &&
                static_cast<bool>(second.session->execute_step()) &&
                static_cast<bool>(second.session->execute_step()),
            "second isolated Session run failed");
    const auto first_value = committed_probe(*first.session, adapter);
    const auto second_value = committed_probe(*second.session, adapter);
    require(exactly_same(first_value, second_value) &&
                first.session->committed_epoch() == 3U &&
                second.session->committed_epoch() == 3U &&
                first.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                second.session->state() ==
                    gnc::kernel::SessionState::Completed,
            "isolated Sessions produced different complete runs");
    require_history(*first.session, 3U, 0, 2);
    require_history(*second.session, 3U, 0, 2);
    require(exactly_same(mission_result_probe(*first.session, adapter),
                         mission_result_probe(*second.session, adapter)),
            "isolated Sessions produced different terminal results");
}

void verify_query_count_invariance(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto baseline = initialize_session(image);
    AdapterOptions options;
    options.extra_discarded_boundary_evaluations = 2U;
    auto repeated = initialize_session(image, options);
    for (std::size_t step = 0U; step < 3U; ++step) {
        require(static_cast<bool>(baseline.session->execute_step()) &&
                    static_cast<bool>(repeated.session->execute_step()),
                "query-count invariance run failed");
    }
    SealedObservationSnapshot baseline_snapshot;
    SealedObservationSnapshot repeated_snapshot;
    const auto baseline_snapshot_read =
        gnc::tests::ref_yyz::
            read_sealed_observation_snapshot_for_qualification(
                *baseline.session, baseline_snapshot);
    const auto repeated_snapshot_read =
        gnc::tests::ref_yyz::
            read_sealed_observation_snapshot_for_qualification(
                *repeated.session, repeated_snapshot);
    require(baseline_snapshot_read && repeated_snapshot_read,
            "typed sealed-output qualification read failed");
    const auto& transaction = image->transactions().front();
    const auto* terminal_branch = gnc::contracts::find_transaction_branch(
        transaction, gnc::contracts::TransactionBranch::Terminal);
    require(terminal_branch != nullptr &&
                baseline_snapshot.seals.size() ==
                    terminal_branch->sealed_output_slot_handles.size() &&
                repeated_snapshot.seals.size() ==
                    terminal_branch->sealed_output_slot_handles.size(),
            "terminal seal does not contain every planned REF-YYZ output");
    require(std::all_of(
                baseline_snapshot.seals.begin(),
                baseline_snapshot.seals.end(), [](const auto& seal) {
                    return seal.present &&
                           seal.quality ==
                               gnc::contracts::DataQuality::Valid;
                }) &&
                std::count_if(
                    baseline_snapshot.seals.begin(),
                    baseline_snapshot.seals.end(), [](const auto& seal) {
                        return seal.terminal_result;
                    }) == 1,
            "terminal seal lost presence, quality, or result identity");
    require(baseline_snapshot.mission_result.has_value(),
            "terminal snapshot omitted mission result");
    require(same_sealed_observation_snapshot(baseline_snapshot,
                                             repeated_snapshot),
            "discarded query evaluations changed a typed sealed payload");
    require(exactly_same(committed_probe(*baseline.session,
                                         baseline.adapter),
                         committed_probe(*repeated.session,
                                         repeated.adapter)) &&
                exactly_same(mission_result_probe(*baseline.session,
                                                  baseline.adapter),
                             mission_result_probe(*repeated.session,
                                                  repeated.adapter)) &&
                baseline.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                repeated.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                baseline.session->last_step_journal().branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                repeated.session->last_step_journal().branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                baseline.session->committed_epoch() == 3U &&
                repeated.session->committed_epoch() == 3U &&
                baseline.session->committed_tick() == 2 &&
                repeated.session->committed_tick() == 2 &&
                baseline.adapter.opening_boundary->environment_query_calls ==
                    3U &&
                baseline.adapter.opening_boundary->aerodynamic_query_calls ==
                    3U &&
                repeated.adapter.opening_boundary->environment_query_calls ==
                    9U &&
                repeated.adapter.opening_boundary->aerodynamic_query_calls ==
                    9U &&
                repeated.adapter.opening_boundary
                        ->discarded_boundary_evaluations == 6U &&
                same_committed_outputs(
                    baseline.session->committed_outputs(),
                    repeated.session->committed_outputs()),
            "discarded environment/aero queries changed committed state, termination, or a typed sealed payload");
}

void run() {
    const auto image = build_image();
    verify_complete_step_transactions(image);
    verify_failure_matrix(image);
    verify_nonzero_failure_matrix(image);
    verify_terminal_failure_matrix(image);
    verify_two_session_isolation(image);
    verify_query_count_invariance(image);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_step_transaction_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 REF-YYZ complete StepTransactions: PASS"
                  << " (max_abs_difference=" << std::setprecision(17)
                  << maximum_observed_absolute_difference << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 REF-YYZ complete StepTransactions: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
