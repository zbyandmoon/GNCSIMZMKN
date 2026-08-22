#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using gnc::contracts::ExecutionPlanImage;
using gnc::kernel::SessionError;
using gnc::kernel::RuntimeDiagnosticCode;
using gnc::kernel::RuntimeDiagnosticStage;
using gnc::tests::ref_yyz::AdapterOptions;
using gnc::tests::ref_yyz::AdapterCoordinationPoint;
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

[[nodiscard]] bool exact_double(double lhs, double rhs) noexcept {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t lhs_bits = 0U;
    std::uint64_t rhs_bits = 0U;
    std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
    return lhs_bits == rhs_bits;
}

template <std::size_t Size>
[[nodiscard]] bool exact_array(const std::array<double, Size>& lhs,
                               const std::array<double, Size>& rhs) noexcept {
    for (std::size_t index = 0U; index < Size; ++index) {
        if (!exact_double(lhs[index], rhs[index])) return false;
    }
    return true;
}

[[nodiscard]] bool exactly_same(const CommittedRigidMassProbe& lhs,
                                const CommittedRigidMassProbe& rhs) noexcept {
    return exact_array(lhs.position, rhs.position) &&
           exact_array(lhs.velocity, rhs.velocity) &&
           exact_array(lhs.attitude_wxyz, rhs.attitude_wxyz) &&
           exact_array(lhs.angular_rate, rhs.angular_rate) &&
           exact_double(lhs.mass_kilograms, rhs.mass_kilograms) &&
           exact_array(lhs.center_of_mass, rhs.center_of_mass) &&
           exact_array(lhs.inertia, rhs.inertia) &&
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

[[nodiscard]] bool exact_vector(
    const gnc::foundation::Vec3& lhs,
    const gnc::foundation::Vec3& rhs) noexcept {
    for (Eigen::Index index = 0; index < 3; ++index) {
        if (!exact_double(lhs(index), rhs(index))) return false;
    }
    return true;
}

[[nodiscard]] bool exact_matrix(
    const gnc::foundation::Mat3& lhs,
    const gnc::foundation::Mat3& rhs) noexcept {
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (!exact_double(lhs(row, column), rhs(row, column))) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool exact_sample_context(
    const gnc::contracts::SampleContext& lhs,
    const gnc::contracts::SampleContext& rhs) noexcept {
    return lhs.frame == rhs.frame &&
           lhs.clock_domain == rhs.clock_domain &&
           lhs.sample_time.tick == rhs.sample_time.tick &&
           exact_double(lhs.sample_time.seconds, rhs.sample_time.seconds) &&
           lhs.configuration_revision == rhs.configuration_revision &&
           lhs.quality == rhs.quality;
}

[[nodiscard]] bool exact_interval_context(
    const gnc::contracts::IntervalSampleContext& lhs,
    const gnc::contracts::IntervalSampleContext& rhs) noexcept {
    return exact_sample_context(lhs.sample, rhs.sample) &&
           lhs.validity.effective_from.tick ==
               rhs.validity.effective_from.tick &&
           exact_double(lhs.validity.effective_from.seconds,
                        rhs.validity.effective_from.seconds) &&
           lhs.validity.effective_until.tick ==
               rhs.validity.effective_until.tick &&
           exact_double(lhs.validity.effective_until.seconds,
                        rhs.validity.effective_until.seconds);
}

[[nodiscard]] bool exact_rigid_state(
    const yyz::RigidState& lhs, const yyz::RigidState& rhs) noexcept {
    return exact_vector(lhs.position.value, rhs.position.value) &&
           exact_vector(lhs.velocity.value, rhs.velocity.value) &&
           exact_array(
               gnc::foundation::quaternion_to_wxyz(lhs.attitude.value),
               gnc::foundation::quaternion_to_wxyz(rhs.attitude.value)) &&
           exact_vector(lhs.angular_rate.value, rhs.angular_rate.value);
}

[[nodiscard]] bool exact_rigid_observation(
    const yyz::CommittedRigidObservation& lhs,
    const yyz::CommittedRigidObservation& rhs) noexcept {
    return exact_sample_context(lhs.context, rhs.context) &&
           exact_rigid_state(lhs.state, rhs.state);
}

[[nodiscard]] bool exact_mass_properties(
    const yyz::MassPropertiesInput& lhs,
    const yyz::MassPropertiesInput& rhs) noexcept {
    return exact_interval_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           exact_double(lhs.mass_kilograms, rhs.mass_kilograms) &&
           exact_vector(lhs.body_origin_to_center_of_mass.value,
                        rhs.body_origin_to_center_of_mass.value) &&
           exact_matrix(lhs.inertia_about_center_of_mass.value,
                        rhs.inertia_about_center_of_mass.value);
}

[[nodiscard]] bool exact_applied_wrench(
    const yyz::AppliedBodyWrenchInput& lhs,
    const yyz::AppliedBodyWrenchInput& rhs) noexcept {
    return exact_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           exact_vector(lhs.force.value, rhs.force.value) &&
           exact_vector(lhs.body_origin_to_application.value,
                        rhs.body_origin_to_application.value) &&
           exact_vector(lhs.intrinsic_moment_at_application.value,
                        rhs.intrinsic_moment_at_application.value);
}

[[nodiscard]] bool exact_environment(
    const yyz::EnvironmentInput& lhs,
    const yyz::EnvironmentInput& rhs) noexcept {
    return exact_sample_context(lhs.context, rhs.context) &&
           exact_vector(lhs.gravity.value, rhs.gravity.value) &&
           exact_vector(lhs.velocity_airmass.value,
                        rhs.velocity_airmass.value) &&
           exact_double(lhs.density_kilograms_per_cubic_meter,
                        rhs.density_kilograms_per_cubic_meter) &&
           exact_double(lhs.speed_of_sound_meters_per_second,
                        rhs.speed_of_sound_meters_per_second);
}

[[nodiscard]] bool exact_rigid_preparation(
    const yyz::ControlledRigidBoundaryPreparationOutput& lhs,
    const yyz::ControlledRigidBoundaryPreparationOutput& rhs) noexcept {
    if (!exact_environment(lhs.environment_response,
                           rhs.environment_response) ||
        !exact_array(lhs.aerodynamic_coefficients
                         .coefficients_ca_cy_cn_cl_cm_cn,
                     rhs.aerodynamic_coefficients
                         .coefficients_ca_cy_cn_cl_cm_cn) ||
        !exact_vector(
            lhs.closure_request.body_origin_to_center_of_mass.value,
            rhs.closure_request.body_origin_to_center_of_mass.value) ||
        lhs.closure_request.contributions.size() !=
            rhs.closure_request.contributions.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < lhs.closure_request.contributions.size(); ++index) {
        if (!exact_applied_wrench(
                lhs.closure_request.contributions[index],
                rhs.closure_request.contributions[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exact_rigid_form(
    const yyz::RigidFormInput& lhs,
    const yyz::RigidFormInput& rhs) noexcept {
    return exact_vector(lhs.force_total.value, rhs.force_total.value) &&
           exact_vector(lhs.moment_total_about_center_of_mass.value,
                        rhs.moment_total_about_center_of_mass.value);
}

[[nodiscard]] bool exact_guidance(
    const yyz::AltitudePitchGuidanceOutput& lhs,
    const yyz::AltitudePitchGuidanceOutput& rhs) noexcept {
    return exact_rigid_observation(lhs.source_observation,
                                   rhs.source_observation) &&
           exact_double(lhs.measured_pitch_radians,
                        rhs.measured_pitch_radians) &&
           exact_double(lhs.measured_pitch_rate_radians_per_second,
                        rhs.measured_pitch_rate_radians_per_second) &&
           exact_double(lhs.altitude_error_meters,
                        rhs.altitude_error_meters) &&
           exact_double(lhs.altitude_feedback_radians,
                        rhs.altitude_feedback_radians) &&
           exact_double(lhs.vertical_speed_feedback_radians,
                        rhs.vertical_speed_feedback_radians) &&
           exact_double(lhs.raw_pitch_command_radians,
                        rhs.raw_pitch_command_radians) &&
           exact_double(lhs.pitch_command_radians,
                        rhs.pitch_command_radians) &&
           lhs.saturated == rhs.saturated;
}

[[nodiscard]] bool exact_controller(
    const yyz::PitchMomentControllerOutput& lhs,
    const yyz::PitchMomentControllerOutput& rhs) noexcept {
    return exact_sample_context(lhs.context, rhs.context) &&
           exact_double(lhs.pitch_error_radians,
                        rhs.pitch_error_radians) &&
           exact_double(lhs.proportional_moment_newton_meters,
                        rhs.proportional_moment_newton_meters) &&
           exact_double(lhs.rate_damping_moment_newton_meters,
                        rhs.rate_damping_moment_newton_meters) &&
           exact_double(lhs.raw_moment_command_newton_meters,
                        rhs.raw_moment_command_newton_meters) &&
           exact_double(lhs.moment_command_newton_meters,
                        rhs.moment_command_newton_meters) &&
           lhs.saturated == rhs.saturated;
}

[[nodiscard]] bool exact_actuator(
    const yyz::IdealBodyMomentActuatorOutput& lhs,
    const yyz::IdealBodyMomentActuatorOutput& rhs) noexcept {
    return exact_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           exact_vector(lhs.moment_about_center_of_mass.value,
                        rhs.moment_about_center_of_mass.value);
}

[[nodiscard]] bool exact_propulsion(
    const yyz::SuppliedPropulsionBodyWrench& lhs,
    const yyz::SuppliedPropulsionBodyWrench& rhs) noexcept {
    return exact_interval_context(lhs.context, rhs.context) &&
           lhs.source_id == rhs.source_id &&
           exact_vector(lhs.force.value, rhs.force.value) &&
           exact_vector(lhs.center_of_mass_to_application.value,
                        rhs.center_of_mass_to_application.value) &&
           exact_vector(lhs.intrinsic_moment_at_application.value,
                        rhs.intrinsic_moment_at_application.value);
}

[[nodiscard]] bool exact_mass_flow(
    const yyz::MassFlowIntervalInput& lhs,
    const yyz::MassFlowIntervalInput& rhs) noexcept {
    return exact_interval_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           exact_double(lhs.fuel_consumption_rate_kilograms_per_second,
                        rhs.fuel_consumption_rate_kilograms_per_second);
}

[[nodiscard]] bool exact_mass_state(
    const yyz::MassState& lhs, const yyz::MassState& rhs) noexcept {
    return exact_sample_context(lhs.context, rhs.context) &&
           lhs.mass_state_id == rhs.mass_state_id &&
           exact_double(lhs.mass_kilograms, rhs.mass_kilograms) &&
           exact_vector(lhs.body_origin_to_center_of_mass.value,
                        rhs.body_origin_to_center_of_mass.value) &&
           exact_matrix(lhs.inertia_about_center_of_mass.value,
                        rhs.inertia_about_center_of_mass.value);
}

[[nodiscard]] bool exact_committed_boundary(
    const yyz::CommittedRigidMassBoundary& lhs,
    const yyz::CommittedRigidMassBoundary& rhs) noexcept {
    return exact_sample_context(lhs.rigid_context, rhs.rigid_context) &&
           exact_rigid_state(lhs.rigid_state, rhs.rigid_state) &&
           exact_mass_state(lhs.mass_state, rhs.mass_state);
}

[[nodiscard]] bool exact_mission_metrics(
    const yyz::MissionMetrics& lhs,
    const yyz::MissionMetrics& rhs) noexcept {
    return exact_double(lhs.duration_seconds, rhs.duration_seconds) &&
           exact_double(lhs.downrange_meters, rhs.downrange_meters) &&
           exact_double(lhs.vertical_displacement_meters,
                        rhs.vertical_displacement_meters) &&
           exact_double(lhs.remaining_mass_kilograms,
                        rhs.remaining_mass_kilograms) &&
           exact_double(lhs.consumed_mass_kilograms,
                        rhs.consumed_mass_kilograms) &&
           exact_double(lhs.speed_meters_per_second,
                        rhs.speed_meters_per_second);
}

[[nodiscard]] bool exact_mission_result(
    const yyz::CommittedMissionResultOutput& lhs,
    const yyz::CommittedMissionResultOutput& rhs) noexcept {
    if (lhs.status != rhs.status || lhs.initial_tick != rhs.initial_tick ||
        lhs.final_tick != rhs.final_tick ||
        !exact_double(lhs.final_time_seconds, rhs.final_time_seconds) ||
        lhs.termination.action != rhs.termination.action ||
        lhs.termination.reason_code != rhs.termination.reason_code ||
        !exact_double(lhs.termination.trigger_time_seconds,
                      rhs.termination.trigger_time_seconds) ||
        lhs.termination.priority != rhs.termination.priority ||
        lhs.metrics.evaluated_sample_count !=
            rhs.metrics.evaluated_sample_count ||
        !exact_mission_metrics(lhs.metrics.terminal,
                               rhs.metrics.terminal) ||
        !exact_double(lhs.metrics.peak_speed_meters_per_second,
                      rhs.metrics.peak_speed_meters_per_second) ||
        lhs.metrics.peak_speed_tick != rhs.metrics.peak_speed_tick ||
        !exact_double(lhs.metrics.maximum_downrange_meters,
                      rhs.metrics.maximum_downrange_meters) ||
        lhs.metrics.maximum_downrange_tick !=
            rhs.metrics.maximum_downrange_tick ||
        !exact_double(lhs.metrics.minimum_remaining_mass_kilograms,
                      rhs.metrics.minimum_remaining_mass_kilograms) ||
        lhs.metrics.minimum_remaining_mass_tick !=
            rhs.metrics.minimum_remaining_mass_tick ||
        !exact_committed_boundary(lhs.terminal_boundary,
                                  rhs.terminal_boundary)) {
        return false;
    }
    for (std::size_t index = 0U;
         index < lhs.terminal_predicates.size(); ++index) {
        const auto& left = lhs.terminal_predicates[index];
        const auto& right = rhs.terminal_predicates[index];
        if (left.predicate_id != right.predicate_id ||
            !exact_double(left.observed, right.observed) ||
            left.met != right.met ||
            left.action != right.action ||
            left.reason_code != right.reason_code ||
            left.priority != right.priority) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exact_seal_metadata(
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& rhs,
    bool include_frame_coordinates = true) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.slot_handle != right.slot_handle ||
            left.codec_entry_handle != right.codec_entry_handle ||
            left.present != right.present ||
            (include_frame_coordinates &&
             (left.generation != right.generation ||
              left.sequence != right.sequence)) ||
            left.sample_tick != right.sample_tick ||
            !exact_double(left.sample_time_seconds,
                          right.sample_time_seconds) ||
            !exact_double(left.interval_start_seconds,
                          right.interval_start_seconds) ||
            !exact_double(left.interval_end_seconds,
                          right.interval_end_seconds) ||
            left.quality != right.quality ||
            left.terminal_result != right.terminal_result) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exactly_same(
    const std::vector<gnc::kernel::SessionCommittedHistoryInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedHistoryInfo>& rhs)
    noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.history_handle != right.history_handle ||
            left.history_depth != right.history_depth ||
            left.sample_count != right.sample_count ||
            left.member_count != right.member_count ||
            left.first_tick != right.first_tick ||
            left.last_tick != right.last_tick) {
            return false;
        }
    }
    return true;
}

template <typename Value, typename Equal>
[[nodiscard]] bool exact_optional_payload(
    const std::optional<Value>& lhs, const std::optional<Value>& rhs,
    Equal equal) noexcept {
    return lhs.has_value() == rhs.has_value() &&
           (!lhs.has_value() || equal(*lhs, *rhs));
}

[[nodiscard]] bool exact_sealed_observation_snapshot(
    const SealedObservationSnapshot& lhs,
    const SealedObservationSnapshot& rhs,
    bool include_frame_coordinates = true) noexcept {
    return exact_seal_metadata(lhs.seals, rhs.seals,
                               include_frame_coordinates) &&
           exact_optional_payload(lhs.rigid_observation,
                                  rhs.rigid_observation,
                                  exact_rigid_observation) &&
           exact_optional_payload(lhs.rigid_form, rhs.rigid_form,
                                  exact_rigid_form) &&
           exact_optional_payload(lhs.rigid_preparation,
                                  rhs.rigid_preparation,
                                  exact_rigid_preparation) &&
           exact_optional_payload(lhs.mass_properties,
                                  rhs.mass_properties,
                                  exact_mass_properties) &&
           exact_optional_payload(lhs.guidance, rhs.guidance,
                                  exact_guidance) &&
           exact_optional_payload(lhs.controller, rhs.controller,
                                  exact_controller) &&
           exact_optional_payload(lhs.actuator, rhs.actuator,
                                  exact_actuator) &&
           exact_optional_payload(lhs.propulsion, rhs.propulsion,
                                  exact_propulsion) &&
           exact_optional_payload(lhs.mass_flow, rhs.mass_flow,
                                  exact_mass_flow) &&
           exact_optional_payload(lhs.mission_result,
                                  rhs.mission_result,
                                  exact_mission_result);
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

[[nodiscard]] SealedObservationSnapshot sealed_snapshot(
    const gnc::kernel::Session& session) {
    SealedObservationSnapshot result;
    const auto read = gnc::tests::ref_yyz::
        read_sealed_observation_snapshot_for_qualification(session, result);
    require(static_cast<bool>(read),
            "typed sealed-output qualification read failed");
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
           exact_double(lhs.final_time_seconds, rhs.final_time_seconds) &&
           lhs.reason_code == rhs.reason_code && lhs.priority == rhs.priority &&
           lhs.evaluated_sample_count == rhs.evaluated_sample_count &&
           exact_double(lhs.duration_seconds, rhs.duration_seconds) &&
           exact_double(lhs.downrange_meters, rhs.downrange_meters) &&
           exact_double(lhs.remaining_mass_kilograms,
                        rhs.remaining_mass_kilograms) &&
           exact_double(lhs.consumed_mass_kilograms,
                        rhs.consumed_mass_kilograms) &&
           exact_double(lhs.terminal_speed_meters_per_second,
                        rhs.terminal_speed_meters_per_second) &&
           lhs.terminal_tick == rhs.terminal_tick;
}

[[nodiscard]] bool exactly_same(
    const gnc::kernel::RuntimeDiagnostic& lhs,
    const gnc::kernel::RuntimeDiagnostic& rhs) noexcept {
    return lhs.code == rhs.code && lhs.stage == rhs.stage &&
           lhs.subject_handle == rhs.subject_handle &&
           lhs.run_id == rhs.run_id && lhs.tick == rhs.tick &&
           lhs.base_epoch == rhs.base_epoch &&
           lhs.cause_code == rhs.cause_code &&
           lhs.cause_ref == rhs.cause_ref &&
           lhs.validity_effect == rhs.validity_effect &&
           lhs.disposition == rhs.disposition &&
           lhs.message_key == rhs.message_key && lhs.detail == rhs.detail;
}

[[nodiscard]] bool exactly_same(
    const gnc::kernel::RunOutcome& lhs,
    const gnc::kernel::RunOutcome& rhs) noexcept {
    if (lhs.run_id != rhs.run_id ||
        lhs.run_sequence != rhs.run_sequence ||
        lhs.image_fingerprint != rhs.image_fingerprint ||
        lhs.plan_id != rhs.plan_id || lhs.mission_id != rhs.mission_id ||
        lhs.source_semantic_hash != rhs.source_semantic_hash ||
        lhs.descriptor_semantic_hash != rhs.descriptor_semantic_hash ||
        lhs.run_start_kind != rhs.run_start_kind ||
        lhs.run_start_committed != rhs.run_start_committed ||
        lhs.final_status != rhs.final_status ||
        lhs.validity != rhs.validity ||
        lhs.initial_tick != rhs.initial_tick ||
        lhs.final_tick != rhs.final_tick ||
        lhs.initial_committed_epoch != rhs.initial_committed_epoch ||
        lhs.final_committed_epoch != rhs.final_committed_epoch ||
        lhs.committed_step_count != rhs.committed_step_count ||
        lhs.terminal_branch_committed != rhs.terminal_branch_committed ||
        lhs.mission_result_available != rhs.mission_result_available ||
        lhs.primary_diagnostic.has_value() !=
            rhs.primary_diagnostic.has_value() ||
        lhs.related_diagnostics.size() != rhs.related_diagnostics.size() ||
        lhs.finalization_status != rhs.finalization_status) {
        return false;
    }
    if (lhs.primary_diagnostic.has_value() &&
        !exactly_same(*lhs.primary_diagnostic,
                      *rhs.primary_diagnostic)) {
        return false;
    }
    for (std::size_t index = 0U;
         index < lhs.related_diagnostics.size(); ++index) {
        if (!exactly_same(lhs.related_diagnostics[index],
                          rhs.related_diagnostics[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_exact_image_binding(
    const gnc::kernel::RunOutcome& outcome,
    const ExecutionPlanImage& image) noexcept {
    return outcome.image_fingerprint == image.fingerprint() &&
           outcome.plan_id == image.plan_id() &&
           outcome.mission_id == image.mission_id() &&
           outcome.source_semantic_hash == image.source_semantic_hash() &&
           outcome.descriptor_semantic_hash ==
               image.descriptor_semantic_hash();
}

[[nodiscard]] bool is_empty_lifecycle_rejection(
    const gnc::kernel::StepOutcome& outcome,
    const gnc::kernel::RunId& run_id,
    std::uint64_t run_sequence,
    std::uint64_t committed_epoch,
    std::int64_t committed_tick) noexcept {
    return outcome.status == gnc::kernel::StepStatus::Failed &&
           outcome.result.error ==
               SessionError::InvalidLifecycleTransition &&
           outcome.run_id == run_id &&
           outcome.run_sequence == run_sequence &&
           !outcome.branch_selected && outcome.transaction_handle == 0U &&
           outcome.base_epoch == committed_epoch &&
           outcome.committed_epoch == committed_epoch &&
           outcome.tick_before == committed_tick &&
           outcome.tick_after == committed_tick &&
           outcome.last_region_handle == 0U &&
           outcome.last_callsite_handle == 0U &&
           outcome.last_image_handle == 0U &&
           outcome.candidates.planned_count == 0U &&
           outcome.candidates.present_count == 0U &&
           outcome.candidates.valid_count == 0U &&
           !outcome.histories.staged &&
           outcome.histories.history_count == 0U &&
           outcome.histories.prospective_sample_count == 0U &&
           !outcome.observation_seal.staged &&
           outcome.observation_seal.output_count == 0U &&
           !outcome.result_seal.staged &&
           !outcome.result_seal.result_present &&
           outcome.primary_diagnostic.has_value() &&
           outcome.primary_diagnostic->code ==
               RuntimeDiagnosticCode::LifecycleTransitionRejected &&
           outcome.primary_diagnostic->stage ==
               RuntimeDiagnosticStage::Lifecycle &&
           outcome.primary_diagnostic->run_id == run_id &&
           outcome.primary_diagnostic->tick == committed_tick &&
           outcome.primary_diagnostic->base_epoch == committed_epoch;
}

[[nodiscard]] RuntimeDiagnosticCode expected_runtime_code(
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

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage> build_image() {
    const auto compiled = gnc::tests::ref_yyz::compile_complete_image();
    require(compiled.succeeded(), "REF-YYZ Image compilation failed");
    return std::make_shared<const ExecutionPlanImage>(*compiled.value);
}

void verify_reset_diagnostic_code_stability() {
    require(gnc::kernel::to_string(
                RuntimeDiagnosticCode::ResetRequestInvalid) ==
                "GNC-RUN-RST-0001" &&
                gnc::kernel::to_string(
                    RuntimeDiagnosticCode::ResetStateRebuildFailed) ==
                    "GNC-RUN-RST-0002" &&
                gnc::kernel::to_string(
                    RuntimeDiagnosticCode::ResetPrecommitFailed) ==
                    "GNC-RUN-RST-0003" &&
                gnc::kernel::to_string(
                    RuntimeDiagnosticCode::ResetCapabilityMissing) ==
                    "GNC-RUN-RST-0004",
            "reset diagnostic code compatibility changed");
}

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage>
build_image_without_reset_capability() {
    const auto compiled = gnc::tests::ref_yyz::
        compile_complete_image_without_reset_capability();
    require(compiled.succeeded(),
            "Compiler rejected a legal non-resettable Runtime Cell");
    return std::make_shared<const ExecutionPlanImage>(*compiled.value);
}

[[nodiscard]] gnc::kernel::InitializationRequest initialization_request(
    const ExecutionPlanImage& image,
    std::string run_id = "run:step-transaction") {
    return {gnc::kernel::RunId{std::move(run_id)},
            gnc::kernel::exact_run_binding(image)};
}

[[nodiscard]] gnc::kernel::ResetRequest reset_request(
    const ExecutionPlanImage& image, std::string run_id) {
    return {gnc::kernel::RunId{std::move(run_id)},
            gnc::kernel::exact_run_binding(image)};
}

[[nodiscard]] gnc::kernel::CancellationRequest cancellation_request(
    std::string request_id, std::string run_id) {
    return {gnc::kernel::CancellationRequestId{std::move(request_id)},
            gnc::kernel::RunId{std::move(run_id)}};
}

[[nodiscard]] std::uint32_t initial_binding_handle_for_candidate(
    const ExecutionPlanImage& image,
    const gnc::contracts::PlanImageTransactionCandidateMember& member) {
    const auto block = std::find_if(
        image.state_blocks().begin(), image.state_blocks().end(),
        [&member](const auto& value) {
            return value.candidate_slot_handle ==
                   member.candidate_state_slot_handle;
        });
    require(block != image.state_blocks().end(),
            "candidate has no state block");
    const auto binding = std::find_if(
        image.initial_bindings().begin(), image.initial_bindings().end(),
        [&block](const auto& value) {
            return value.committed_state_slot_handle ==
                   block->committed_slot_handle;
        });
    require(binding != image.initial_bindings().end(),
            "candidate has no initial binding");
    return binding->handle;
}

struct SessionBundle {
    gnc::tests::ref_yyz::RefYyzSessionAdapter adapter;
    std::unique_ptr<gnc::kernel::Session> session;
};

[[nodiscard]] SessionBundle initialize_session(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    AdapterOptions options = {},
    std::string run_id = "run:step-transaction") {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "Session creation failed");
    const auto initialized = creation.session->initialize(
        initialization_request(*image, std::move(run_id)));
    if (!initialized) {
        throw std::runtime_error(
            std::string("Session initialization failed: ") +
            std::string(gnc::kernel::to_string(initialized.result.error)) +
            " / " + std::string(initialized.result.detail) + " / handle=" +
            std::to_string(initialized.result.image_handle));
    }
    require(initialized.status ==
                    gnc::kernel::InitializationStatus::Committed &&
                initialized.binding_matched &&
                initialized.initialization_commit &&
                initialized.proposed_run_sequence == 0U &&
                initialized.committed_epoch == 0U &&
                initialized.committed_tick == 0 &&
                !initialized.primary_diagnostic.has_value() &&
                creation.session->active_run_id() != nullptr &&
                creation.session->active_run_binding() != nullptr &&
                creation.session->run_sequence().has_value() &&
                *creation.session->run_sequence() == 0U &&
                creation.session->run_outcome() == nullptr &&
                creation.session->committed_step_count() == 0U,
            "InitializationCommit did not publish the exact run context");
    return {std::move(adapter), std::move(creation.session)};
}

void require_cancelled_run(const gnc::kernel::Session& session,
                           std::string_view run_id,
                           std::uint64_t epoch,
                           std::int64_t tick,
                           std::uint64_t committed_steps) {
    const auto* outcome = session.run_outcome();
    require(session.state() == gnc::kernel::SessionState::Cancelled &&
                session.last_result().error == SessionError::None &&
                session.committed_epoch() == epoch &&
                session.committed_tick() == tick &&
                session.committed_step_count() == committed_steps &&
                session.last_committed_run_id() != nullptr &&
                session.last_committed_run_id()->value() == run_id &&
                outcome != nullptr && outcome->run_id.value() == run_id &&
                outcome->run_start_committed &&
                outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Cancelled &&
                outcome->validity ==
                    gnc::contracts::EvidenceValidity::Valid &&
                outcome->final_committed_epoch == epoch &&
                outcome->final_tick == tick &&
                outcome->committed_step_count == committed_steps &&
                !outcome->terminal_branch_committed &&
                !outcome->mission_result_available &&
                !outcome->primary_diagnostic.has_value() &&
                outcome->related_diagnostics.empty() &&
                outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::Succeeded,
            "cancelled run lost committed evidence or gained an error diagnostic");
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

void verify_initialization_identity_and_commit(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);

    auto created = gnc::kernel::create_session(image, adapter.provider);
    require(created &&
                created.session->state() ==
                    gnc::kernel::SessionState::Created &&
                created.session->active_run_id() == nullptr &&
                created.session->active_run_binding() == nullptr &&
                !created.session->run_sequence().has_value() &&
                created.session->run_outcome() == nullptr &&
                created.session->committed_epoch() == 0U &&
                created.session->committed_tick() == 0,
            "Created Session exposed a committed run identity");

    const auto empty = created.session->initialize(
        {gnc::kernel::RunId{}, gnc::kernel::exact_run_binding(*image)});
    require(!empty &&
                empty.result.error == SessionError::EmptyRunId &&
                empty.binding_matched && !empty.initialization_commit &&
                empty.committed_epoch == 0U && empty.committed_tick == 0 &&
                empty.primary_diagnostic.has_value() &&
                empty.primary_diagnostic->code ==
                    RuntimeDiagnosticCode::InitializationRequestInvalid &&
                empty.primary_diagnostic->stage ==
                    RuntimeDiagnosticStage::InitializationRequest &&
                empty.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                created.session->state() ==
                    gnc::kernel::SessionState::Failed &&
                created.session->active_run_id() == nullptr &&
                !created.session->run_sequence().has_value() &&
                created.session->committed_state_count() == 0U &&
                created.session->committed_epoch() == 0U &&
                created.session->committed_tick() == 0,
            "empty RunId crossed InitializationCommit");
    const auto* empty_outcome = created.session->run_outcome();
    require(empty_outcome != nullptr && empty_outcome->run_id.empty() &&
                has_exact_image_binding(*empty_outcome, *image) &&
                empty_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Initialize &&
                !empty_outcome->run_start_committed &&
                empty_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                empty_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                empty_outcome->final_committed_epoch == 0U &&
                empty_outcome->final_tick == 0 &&
                empty_outcome->committed_step_count == 0U &&
                empty_outcome->primary_diagnostic.has_value() &&
                empty_outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::NotStarted,
            "empty RunId did not freeze a precommit failure outcome");

    auto mismatch = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(mismatch),
            "binding-mismatch Session creation failed");
    auto wrong_binding = gnc::kernel::exact_run_binding(*image);
    wrong_binding.image_fingerprint += ".wrong";
    const gnc::kernel::RunId mismatch_id{"run:binding-mismatch"};
    const auto mismatched = mismatch.session->initialize(
        {mismatch_id, std::move(wrong_binding)});
    require(!mismatched &&
                mismatched.result.error ==
                    SessionError::RunBindingMismatch &&
                mismatched.run_id == mismatch_id &&
                !mismatched.binding_matched &&
                !mismatched.initialization_commit &&
                mismatched.primary_diagnostic.has_value() &&
                mismatched.primary_diagnostic->code ==
                    RuntimeDiagnosticCode::ImageBindingMismatch &&
                mismatched.primary_diagnostic->run_id == mismatch_id &&
                mismatch.session->state() ==
                    gnc::kernel::SessionState::Failed &&
                mismatch.session->active_run_id() == nullptr &&
                mismatch.session->run_outcome() != nullptr &&
                mismatch.session->committed_epoch() == 0U &&
                mismatch.session->committed_tick() == 0,
            "mismatched RunBinding crossed InitializationCommit");
    const auto frozen_mismatch = *mismatch.session->run_outcome();
    const auto rejected_step = mismatch.session->execute_step();
    const auto rejected_run = mismatch.session->run_to_terminal();
    require(!rejected_step &&
                rejected_step.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                is_empty_lifecycle_rejection(
                    rejected_step, mismatch_id, 0U, 0U, 0) &&
                is_empty_lifecycle_rejection(
                    mismatch.session->last_step_outcome(),
                    mismatch_id, 0U, 0U, 0) &&
                !rejected_run &&
                rejected_run.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                frozen_mismatch.finalization_status ==
                    gnc::kernel::RunFinalizationStatus::NotStarted &&
                exactly_same(*mismatch.session->run_outcome(),
                             frozen_mismatch) &&
                has_exact_image_binding(
                    *mismatch.session->run_outcome(), *image),
            "post-initialization-failure rejection changed frozen evidence");

    auto successful = initialize_session(
        image, {}, "run:initialization-commit");
    require(successful.session->active_run_id()->value() ==
                    "run:initialization-commit" &&
                *successful.session->active_run_binding() ==
                    gnc::kernel::exact_run_binding(*image) &&
                successful.session->state() ==
                    gnc::kernel::SessionState::Initialized,
            "successful initialization published the wrong active run");
    const auto completed = successful.session->run_to_terminal();
    require(completed && successful.session->run_outcome() != nullptr &&
                successful.session->run_outcome()->final_status ==
                    gnc::kernel::RunFinalStatus::Completed,
            "fresh Session did not complete after initialization failures");
    require_mission_oracle(mission_result_probe(*successful.session,
                                                successful.adapter));
}

void verify_reset_capability_fail_closed(
    const std::shared_ptr<const ExecutionPlanImage>& baseline) {
    const auto image = build_image_without_reset_capability();
    require(image->fingerprint() != baseline->fingerprint(),
            "Resettable capability did not enter the Image fingerprint");
    auto bundle = initialize_session(
        image, {}, "run:reset-capability-base");
    require(static_cast<bool>(bundle.session->run_to_terminal()),
            "reset capability negative could not complete its first run");

    const auto before_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto before_histories = bundle.session->committed_histories();
    const auto before_outputs = bundle.session->committed_outputs();
    const auto before_result = mission_result_probe(*bundle.session,
                                                    bundle.adapter);
    const auto before_epoch = bundle.session->committed_epoch();
    const auto before_tick = bundle.session->committed_tick();
    const auto before_steps = bundle.session->committed_step_count();
    const auto trace_size = bundle.adapter.trace->events.size();
    const auto* completed = bundle.session->run_outcome();
    require(completed != nullptr,
            "reset capability negative lacks its completed outcome");
    const auto completed_copy = *completed;

    const auto reset = bundle.session->reset(
        reset_request(*image, "run:reset-capability-rejected"));
    const auto* failed = bundle.session->run_outcome();
    require(!reset &&
                reset.result.error ==
                    SessionError::ResetCapabilityMissing &&
                reset.result.image_handle ==
                    image->runtime_components().front().handle &&
                reset.primary_diagnostic.has_value() &&
                reset.primary_diagnostic->code ==
                    RuntimeDiagnosticCode::ResetCapabilityMissing &&
                reset.primary_diagnostic->stage ==
                    RuntimeDiagnosticStage::ResetPrecommit &&
                !reset.reset_commit &&
                bundle.session->state() ==
                    gnc::kernel::SessionState::Failed &&
                bundle.session->committed_epoch() == before_epoch &&
                bundle.session->committed_tick() == before_tick &&
                bundle.session->committed_step_count() == before_steps &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                exactly_same(bundle.session->committed_histories(),
                             before_histories) &&
                same_committed_outputs(bundle.session->committed_outputs(),
                                       before_outputs) &&
                exactly_same(mission_result_probe(*bundle.session,
                                                  bundle.adapter),
                             before_result) &&
                bundle.adapter.trace->events.size() == trace_size &&
                bundle.session->run_outcome_for_sequence(0U) == completed &&
                exactly_same(*completed, completed_copy) &&
                failed != nullptr && failed != completed &&
                failed->run_id.value() ==
                    "run:reset-capability-rejected" &&
                failed->run_sequence == 1U &&
                failed->run_start_kind ==
                    gnc::kernel::RunStartKind::Reset &&
                !failed->run_start_committed &&
                failed->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed->validity ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                failed->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::NotStarted &&
                failed->primary_diagnostic.has_value() &&
                exactly_same(*failed->primary_diagnostic,
                             *reset.primary_diagnostic),
            "missing reset capability crossed staging or changed committed evidence");
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
    require(first.status == gnc::kernel::StepStatus::Committed &&
                first.run_id.value() == "run:step-transaction" &&
                first.run_sequence == 0U &&
                first.branch_selected &&
                first.transaction_handle == transaction.handle &&
                first.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                first.base_epoch == 0U && first.committed_epoch == 1U &&
                first.tick_before == 0 && first.tick_after == 1 &&
                first.last_region_handle != 0U &&
                first.last_callsite_handle != 0U &&
                first.last_image_handle == first.last_callsite_handle &&
                first.candidates.planned_count == 2U &&
                first.candidates.present_count == 2U &&
                first.candidates.valid_count == 2U &&
                first.histories.staged &&
                first.histories.history_count == 1U &&
                first.histories.prospective_sample_count == 1U &&
                first.observation_seal.staged &&
                first.observation_seal.output_count ==
                    continue_branch->sealed_output_slot_handles.size() &&
                !first.result_seal.staged &&
                !first.result_seal.result_present &&
                !first.primary_diagnostic.has_value(),
            "first Continue StepOutcome is incomplete");
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
    require(second.status == gnc::kernel::StepStatus::Committed &&
                second.run_id == first.run_id &&
                second.run_sequence == 0U &&
                second.branch_selected &&
                second.transaction_handle == transaction.handle &&
                second.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                second.base_epoch == 1U &&
                second.committed_epoch == 2U &&
                second.tick_before == 1 && second.tick_after == 2 &&
                second.candidates.planned_count == 2U &&
                second.candidates.present_count == 2U &&
                second.candidates.valid_count == 2U &&
                second.histories.staged &&
                second.histories.prospective_sample_count == 2U &&
                second.observation_seal.staged &&
                !second.result_seal.staged &&
                !second.primary_diagnostic.has_value(),
            "second Continue StepOutcome is incomplete");
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
                return slot.handle == terminal.result.image_handle;
            });
        throw std::runtime_error(
            std::string("Terminal step failed: ") +
            std::string(gnc::kernel::to_string(terminal.result.error)) +
            " / " + std::string(terminal.result.detail) + " / handle=" +
            std::to_string(terminal.result.image_handle) + " / slot=" +
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
    require(terminal.status == gnc::kernel::StepStatus::Terminated &&
                terminal.run_id == first.run_id &&
                terminal.run_sequence == 0U &&
                terminal.branch_selected &&
                terminal.transaction_handle == transaction.handle &&
                terminal.branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                terminal.base_epoch == 2U &&
                terminal.committed_epoch == 3U &&
                terminal.tick_before == 2 && terminal.tick_after == 2 &&
                terminal.candidates.planned_count == 2U &&
                terminal.candidates.present_count == 0U &&
                terminal.candidates.valid_count == 0U &&
                terminal.histories.staged &&
                terminal.histories.prospective_sample_count == 3U &&
                terminal.observation_seal.staged &&
                terminal.observation_seal.output_count + 1U ==
                    terminal_branch->sealed_output_slot_handles.size() &&
                terminal.result_seal.staged &&
                terminal.result_seal.result_present &&
                !terminal.primary_diagnostic.has_value(),
            "Terminal StepOutcome is incomplete");
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
    const auto* completed_outcome = bundle.session->run_outcome();
    require(completed_outcome != nullptr &&
                completed_outcome->run_id == terminal.run_id &&
                completed_outcome->run_sequence == 0U &&
                completed_outcome->image_fingerprint ==
                    image->fingerprint() &&
                completed_outcome->plan_id == image->plan_id() &&
                completed_outcome->mission_id == image->mission_id() &&
                completed_outcome->source_semantic_hash ==
                    image->source_semantic_hash() &&
                completed_outcome->descriptor_semantic_hash ==
                    image->descriptor_semantic_hash() &&
                completed_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Initialize &&
                completed_outcome->run_start_committed &&
                completed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Completed &&
                completed_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Valid &&
                completed_outcome->initial_tick == 0 &&
                completed_outcome->final_tick == 2 &&
                completed_outcome->initial_committed_epoch == 0U &&
                completed_outcome->final_committed_epoch == 3U &&
                completed_outcome->committed_step_count == 3U &&
                completed_outcome->terminal_branch_committed &&
                completed_outcome->mission_result_available &&
                !completed_outcome->primary_diagnostic.has_value() &&
                completed_outcome->related_diagnostics.empty() &&
                completed_outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::Succeeded &&
                bundle.session->active_run_id() == nullptr &&
                bundle.session->run_sequence().has_value() &&
                *bundle.session->run_sequence() == 0U &&
                bundle.session->committed_step_count() == 3U,
            "Completed RunOutcome lost run, commit, result, or finalization facts");
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
    const auto frozen_outcome = *completed_outcome;
    const auto rejected = bundle.session->execute_step();
    const auto rejected_run = bundle.session->run_to_terminal();
    require(!rejected &&
                rejected.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                is_empty_lifecycle_rejection(
                    rejected, terminal.run_id, 0U, 3U, 2) &&
                is_empty_lifecycle_rejection(
                    bundle.session->last_step_outcome(),
                    terminal.run_id, 0U, 3U, 2) &&
                !rejected_run &&
                rejected_run.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Completed &&
                bundle.session->committed_epoch() == 3U &&
                bundle.session->committed_tick() == 2 &&
                exactly_same(*bundle.session->run_outcome(),
                             frozen_outcome) &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_terminal),
            "Completed Session accepted another execute_step call");
}

void verify_run_to_terminal(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto bundle = initialize_session(
        image, {}, "run:formal-run-to-terminal");
    const auto completed = bundle.session->run_to_terminal();
    const auto* outcome = bundle.session->run_outcome();
    require(completed &&
                completed.status ==
                    gnc::kernel::RunDriveStatus::Completed &&
                bundle.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                bundle.session->last_step_outcome().status ==
                    gnc::kernel::StepStatus::Terminated &&
                bundle.session->last_step_journal().branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                bundle.session->committed_epoch() == 3U &&
                bundle.session->committed_tick() == 2 &&
                bundle.session->committed_step_count() == 3U &&
                bundle.adapter.step_execution->integration_attempts == 2U &&
                bundle.adapter.step_execution->mass_evolution_attempts == 2U &&
                bundle.adapter.opening_boundary->terminal_evaluator_calls ==
                    1U &&
                outcome != nullptr &&
                outcome->run_id.value() ==
                    "run:formal-run-to-terminal" &&
                outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Completed &&
                outcome->validity ==
                    gnc::contracts::EvidenceValidity::Valid &&
                outcome->final_committed_epoch == 3U &&
                outcome->final_tick == 2 &&
                outcome->committed_step_count == 3U &&
                outcome->terminal_branch_committed &&
                outcome->mission_result_available &&
                outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::Succeeded,
            "run_to_terminal did not drive the authoritative three-step path");
    require_mission_oracle(mission_result_probe(*bundle.session,
                                                bundle.adapter));
    const auto frozen = *outcome;
    const auto repeated = bundle.session->run_to_terminal();
    require(!repeated &&
                repeated.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                exactly_same(*bundle.session->run_outcome(), frozen),
            "repeated run_to_terminal changed the completed outcome");
}

template <typename Configure>
void verify_precommit_rollback(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    Configure configure, SessionError expected,
    std::string_view message,
    RuntimeDiagnosticCode expected_diagnostic =
        RuntimeDiagnosticCode::None,
    RuntimeDiagnosticStage expected_stage =
        RuntimeDiagnosticStage::Lifecycle) {
    AdapterOptions options;
    configure(options);
    auto bundle = initialize_session(image, options);
    const auto before_value = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto failed = bundle.session->execute_step();
    const auto after_value = committed_probe(*bundle.session,
                                             bundle.adapter);
    require(!failed && failed.result.error == expected &&
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
    const auto* failed_outcome = bundle.session->run_outcome();
    require(failed.status == gnc::kernel::StepStatus::Failed &&
                failed.branch_selected &&
                failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->code ==
                    expected_runtime_code(expected) &&
                failed.primary_diagnostic->cause_code == expected &&
                failed.primary_diagnostic->cause_ref ==
                    failed.result.image_handle &&
                failed.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Invalid &&
                failed.primary_diagnostic->disposition ==
                    gnc::kernel::RuntimeFailureDisposition::FailOperation &&
                failed_outcome != nullptr &&
                failed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Invalid &&
                has_exact_image_binding(*failed_outcome, *image) &&
                failed_outcome->final_committed_epoch == 0U &&
                failed_outcome->final_tick == 0 &&
                failed_outcome->committed_step_count == 0U &&
                failed_outcome->primary_diagnostic.has_value() &&
                exactly_same(*failed_outcome->primary_diagnostic,
                             *failed.primary_diagnostic) &&
                failed_outcome->related_diagnostics.empty() &&
                failed_outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::Succeeded,
            "first-step failure did not freeze its primary failure fact");
    require(expected_diagnostic == RuntimeDiagnosticCode::None ||
                (failed.primary_diagnostic->code == expected_diagnostic &&
                 failed.primary_diagnostic->stage == expected_stage &&
                 failed.primary_diagnostic->subject_handle ==
                     failed.result.image_handle &&
                 failed.primary_diagnostic->subject_handle != 0U),
            "representative first-step diagnostic mapping changed");
    require_history(*bundle.session, 0U, 0, 0);
    if (bundle.adapter.captured_input->view != nullptr) {
        const auto stale =
            gnc::tests::ref_yyz::read_captured_stale_input(bundle.adapter);
        require(!stale && stale.error == SessionError::StaleFrameView,
                "rollback left a captured CycleFrame view active");
    }
    const auto frozen_outcome = *failed_outcome;
    const auto retry = bundle.session->execute_step();
    const auto rerun = bundle.session->run_to_terminal();
    require(!retry &&
                retry.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                is_empty_lifecycle_rejection(
                    retry, failed.run_id, 0U, 0U, 0) &&
                is_empty_lifecycle_rejection(
                    bundle.session->last_step_outcome(),
                    failed.run_id, 0U, 0U, 0) &&
                !rerun &&
                rerun.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                bundle.session->committed_epoch() == 0U &&
                bundle.session->committed_tick() == 0 &&
                exactly_same(*bundle.session->run_outcome(),
                             frozen_outcome),
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
        "projection failure changed committed state",
        RuntimeDiagnosticCode::InvocationFailed,
        RuntimeDiagnosticStage::BoundaryInvocation);
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
    std::string_view message,
    RuntimeDiagnosticCode expected_diagnostic =
        RuntimeDiagnosticCode::None,
    RuntimeDiagnosticStage expected_stage =
        RuntimeDiagnosticStage::Lifecycle) {
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
    require(!failed && failed.result.error == expected &&
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
    const auto* failed_outcome = bundle.session->run_outcome();
    require(failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->code ==
                    expected_runtime_code(expected) &&
                failed_outcome != nullptr &&
                failed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Invalid &&
                failed_outcome->final_committed_epoch == 1U &&
                failed_outcome->final_tick == 1 &&
                failed_outcome->committed_step_count == 1U &&
                failed_outcome->primary_diagnostic.has_value() &&
                exactly_same(*failed_outcome->primary_diagnostic,
                             *failed.primary_diagnostic),
            "second-step failure lost the tick-one run outcome");
    require(expected_diagnostic == RuntimeDiagnosticCode::None ||
                (failed.primary_diagnostic->code == expected_diagnostic &&
                 failed.primary_diagnostic->stage == expected_stage &&
                 failed.primary_diagnostic->subject_handle ==
                     failed.result.image_handle &&
                 failed.primary_diagnostic->subject_handle != 0U),
            "representative second-step diagnostic mapping changed");
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
        "second-boundary failure changed the tick-one commit",
        RuntimeDiagnosticCode::InvocationFailed,
        RuntimeDiagnosticStage::BoundaryInvocation);
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
    std::string_view message,
    RuntimeDiagnosticCode expected_diagnostic =
        RuntimeDiagnosticCode::None,
    RuntimeDiagnosticStage expected_stage =
        RuntimeDiagnosticStage::Lifecycle) {
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
    require(!failed && failed.result.error == expected &&
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
    const auto* failed_outcome = bundle.session->run_outcome();
    require(failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->code ==
                    expected_runtime_code(expected) &&
                failed_outcome != nullptr &&
                failed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Invalid &&
                failed_outcome->final_committed_epoch == 2U &&
                failed_outcome->final_tick == 2 &&
                failed_outcome->committed_step_count == 2U &&
                !failed_outcome->terminal_branch_committed &&
                !failed_outcome->mission_result_available &&
                failed_outcome->primary_diagnostic.has_value() &&
                exactly_same(*failed_outcome->primary_diagnostic,
                             *failed.primary_diagnostic),
            "terminal failure lost the tick-two run outcome");
    require(expected_diagnostic == RuntimeDiagnosticCode::None ||
                (failed.primary_diagnostic->code == expected_diagnostic &&
                 failed.primary_diagnostic->stage == expected_stage &&
                 failed.primary_diagnostic->subject_handle ==
                     failed.result.image_handle &&
                 failed.primary_diagnostic->subject_handle != 0U),
            "representative terminal diagnostic mapping changed");
    require_history(*bundle.session, 2U, 0, 1);
    require_sealed_boundary(*bundle.session, 1, before_outputs.size(), false);
    const auto frozen_outcome = *failed_outcome;
    const auto rejected = bundle.session->execute_step();
    const auto rerun = bundle.session->run_to_terminal();
    require(!rejected &&
                rejected.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                !rerun &&
                rerun.result.error ==
                    SessionError::InvalidLifecycleTransition &&
                bundle.session->state() == gnc::kernel::SessionState::Failed &&
                exactly_same(*bundle.session->run_outcome(),
                             frozen_outcome),
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
        "terminal evaluator failure changed the tick-two commit",
        RuntimeDiagnosticCode::InvocationFailed,
        RuntimeDiagnosticStage::BoundaryInvocation);
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
                static_cast<bool>(first.session->initialize(
                    initialization_request(*image,
                                           "run:step-isolation-first"))) &&
                static_cast<bool>(second.session->initialize(
                    initialization_request(*image,
                                           "run:step-isolation-second"))),
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
                first.session->run_outcome() != nullptr &&
                first.session->run_outcome()->run_id.value() ==
                    "run:step-isolation-first" &&
                second.session->run_outcome() == nullptr &&
                second.session->active_run_id() != nullptr &&
                second.session->active_run_id()->value() ==
                    "run:step-isolation-second" &&
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
                    gnc::kernel::SessionState::Completed &&
                first.session->run_outcome() != nullptr &&
                second.session->run_outcome() != nullptr &&
                first.session->run_outcome()->run_id !=
                    second.session->run_outcome()->run_id &&
                first.session->run_outcome()->final_status ==
                    gnc::kernel::RunFinalStatus::Completed &&
                second.session->run_outcome()->final_status ==
                    gnc::kernel::RunFinalStatus::Completed &&
                !first.session->run_outcome()
                     ->primary_diagnostic.has_value() &&
                !second.session->run_outcome()
                     ->primary_diagnostic.has_value() &&
                first.session->run_outcome()
                    ->related_diagnostics.empty() &&
                second.session->run_outcome()
                    ->related_diagnostics.empty(),
            "isolated Sessions produced different complete runs");
    require_history(*first.session, 3U, 0, 2);
    require_history(*second.session, 3U, 0, 2);
    require(exactly_same(mission_result_probe(*first.session, adapter),
                         mission_result_probe(*second.session, adapter)),
            "isolated Sessions produced different terminal results");
}

void verify_query_count_invariance(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto baseline = initialize_session(
        image, {}, "run:query-baseline");
    AdapterOptions options;
    options.extra_discarded_boundary_evaluations = 2U;
    auto repeated = initialize_session(
        image, options, "run:query-repeated");
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
    require(exact_sealed_observation_snapshot(baseline_snapshot,
                                              repeated_snapshot),
            "discarded query evaluations changed a typed sealed payload");
    auto mutated_snapshot = baseline_snapshot;
    require(mutated_snapshot.rigid_observation.has_value(),
            "exact payload comparator mutation lacks a rigid observation");
    auto& mutated_position =
        mutated_snapshot.rigid_observation->state.position.value(0);
    mutated_position = std::nextafter(
        mutated_position, std::numeric_limits<double>::infinity());
    require(!exact_sealed_observation_snapshot(baseline_snapshot,
                                               mutated_snapshot),
            "exact payload comparator accepted a one-ULP mutation");
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
                baseline.session->run_outcome() != nullptr &&
                repeated.session->run_outcome() != nullptr &&
                baseline.session->run_outcome()->final_status ==
                    repeated.session->run_outcome()->final_status &&
                baseline.session->run_outcome()->validity ==
                    repeated.session->run_outcome()->validity &&
                baseline.session->run_outcome()
                        ->terminal_branch_committed ==
                    repeated.session->run_outcome()
                        ->terminal_branch_committed &&
                baseline.session->run_outcome()
                        ->mission_result_available ==
                    repeated.session->run_outcome()
                        ->mission_result_available &&
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

void verify_completed_run_reset(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto bundle = initialize_session(
        image, {}, "run:reset-sequence-0");
    const auto opening_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto preparation_constructs =
        bundle.adapter.trace->constructed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Preparation);
    const auto runtime_constructs =
        bundle.adapter.trace->constructed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::RuntimeCell);
    const auto preparation_count = bundle.session->preparation_count();
    const auto runtime_count = bundle.session->runtime_cell_count();
    const auto state_count = bundle.session->committed_state_count();

    require(static_cast<bool>(bundle.session->run_to_terminal()),
            "first reset qualification run failed");
    const auto first_state = committed_probe(*bundle.session,
                                             bundle.adapter);
    const auto first_history = bundle.session->committed_histories();
    const auto first_seal = sealed_snapshot(*bundle.session);
    const auto first_result = mission_result_probe(*bundle.session,
                                                   bundle.adapter);
    const auto* first_outcome = bundle.session->run_outcome();
    require(first_outcome != nullptr &&
                first_outcome->run_sequence == 0U &&
                first_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Initialize &&
                first_outcome->run_start_committed &&
                first_outcome->initial_committed_epoch == 0U &&
                first_outcome->final_committed_epoch == 3U &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome,
            "first completed outcome is not queryable by sequence");
    const auto first_outcome_copy = *first_outcome;

    const auto reset_one = bundle.session->reset(
        reset_request(*image, "run:reset-sequence-1"));
    const auto reset_opening_state = committed_probe(*bundle.session,
                                                     bundle.adapter);
    const auto reset_one_blocks = bundle.session->state_blocks();
    const auto reset_one_seal = sealed_snapshot(*bundle.session);
    const auto& empty_journal = bundle.session->last_step_journal();
    require(reset_one &&
                reset_one.status == gnc::kernel::ResetStatus::Committed &&
                reset_one.run_id.value() == "run:reset-sequence-1" &&
                reset_one.proposed_run_sequence == 1U &&
                reset_one.binding_matched && reset_one.reset_commit &&
                reset_one.committed_epoch == 4U &&
                reset_one.committed_tick == 0 &&
                !reset_one.primary_diagnostic.has_value() &&
                bundle.session->state() ==
                    gnc::kernel::SessionState::Initialized &&
                bundle.session->run_sequence().has_value() &&
                *bundle.session->run_sequence() == 1U &&
                bundle.session->committed_epoch() == 4U &&
                bundle.session->committed_tick() == 0 &&
                bundle.session->committed_step_count() == 0U &&
                bundle.session->active_run_id() != nullptr &&
                bundle.session->active_run_id()->value() ==
                    "run:reset-sequence-1" &&
                bundle.session->active_run_binding() != nullptr &&
                *bundle.session->active_run_binding() ==
                    gnc::kernel::exact_run_binding(*image) &&
                bundle.session->last_committed_run_id() != nullptr &&
                bundle.session->last_committed_run_id()->value() ==
                    "run:reset-sequence-1" &&
                bundle.session->last_committed_run_binding() != nullptr &&
                *bundle.session->last_committed_run_binding() ==
                    gnc::kernel::exact_run_binding(*image) &&
                bundle.session->run_outcome() == nullptr &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome &&
                bundle.session->run_outcome_for_sequence(1U) == nullptr &&
                exactly_same(*first_outcome, first_outcome_copy) &&
                exactly_same(reset_opening_state, opening_state) &&
                bundle.session->committed_histories().size() == 1U &&
                bundle.session->committed_histories().front().sample_count ==
                    0U &&
                bundle.session->committed_outputs().empty() &&
                reset_one_seal.seals.empty() &&
                !reset_one_seal.mission_result.has_value() &&
                !empty_journal.branch_selected &&
                empty_journal.transaction_handle == 0U &&
                empty_journal.executed_callsite_handles.empty() &&
                empty_journal.integration_scope_handles.empty() &&
                empty_journal.candidates.empty() &&
                empty_journal.histories.empty() &&
                empty_journal.seals.empty() &&
                !empty_journal.committed &&
                bundle.session->preparation_count() == preparation_count &&
                bundle.session->runtime_cell_count() == runtime_count &&
                bundle.session->committed_state_count() == state_count &&
                &bundle.session->image() == image.get() &&
                bundle.adapter.trace->constructed_handles(
                    gnc::tests::ref_yyz::TraceObjectKind::Preparation) ==
                    preparation_constructs &&
                bundle.adapter.trace->constructed_handles(
                    gnc::tests::ref_yyz::TraceObjectKind::RuntimeCell) ==
                    runtime_constructs &&
                std::all_of(
                    reset_one_blocks.begin(), reset_one_blocks.end(),
                    [](const auto& block) {
                        return block.committed_epoch == 4U;
                    }),
            "ResetCommit did not publish a clean sequence-one initial boundary");

    const auto integration_before_second =
        bundle.adapter.step_execution->integration_attempts;
    const auto mass_before_second =
        bundle.adapter.step_execution->mass_evolution_attempts;
    const auto terminal_before_second =
        bundle.adapter.opening_boundary->terminal_evaluator_calls;
    require(static_cast<bool>(bundle.session->run_to_terminal()),
            "second reset qualification run failed");
    const auto second_state = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto second_history = bundle.session->committed_histories();
    const auto second_seal = sealed_snapshot(*bundle.session);
    const auto second_result = mission_result_probe(*bundle.session,
                                                    bundle.adapter);
    const auto* second_outcome = bundle.session->run_outcome();
    require(second_outcome != nullptr &&
                second_outcome->run_id.value() ==
                    "run:reset-sequence-1" &&
                second_outcome->run_sequence == 1U &&
                second_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Reset &&
                second_outcome->run_start_committed &&
                second_outcome->initial_committed_epoch == 4U &&
                second_outcome->final_committed_epoch == 7U &&
                second_outcome->final_tick == 2 &&
                second_outcome->committed_step_count == 3U &&
                second_outcome->terminal_branch_committed &&
                second_outcome->mission_result_available &&
                bundle.session->committed_epoch() == 7U &&
                bundle.session->committed_tick() == 2 &&
                bundle.adapter.step_execution->integration_attempts ==
                    integration_before_second + 2U &&
                bundle.adapter.step_execution->mass_evolution_attempts ==
                    mass_before_second + 2U &&
                bundle.adapter.opening_boundary->terminal_evaluator_calls ==
                    terminal_before_second + 1U &&
                bundle.session->last_step_journal().branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                !first_seal.seals.empty() &&
                !second_seal.seals.empty() &&
                second_seal.seals.front().generation ==
                    first_seal.seals.front().generation + 3U &&
                exactly_same(second_state, first_state) &&
                exactly_same(second_history, first_history) &&
                exact_sealed_observation_snapshot(second_seal, first_seal,
                                                  false) &&
                exactly_same(second_result, first_result) &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome &&
                bundle.session->run_outcome_for_sequence(1U) ==
                    second_outcome &&
                exactly_same(*first_outcome, first_outcome_copy),
            "second full run changed science, payloads, history, or the first outcome");
    const auto second_outcome_copy = *second_outcome;

    const auto reset_two = bundle.session->reset(
        reset_request(*image, "run:reset-sequence-2"));
    require(reset_two && reset_two.proposed_run_sequence == 2U &&
                reset_two.committed_epoch == 8U &&
                reset_two.committed_tick == 0 &&
                bundle.session->run_outcome() == nullptr &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome &&
                bundle.session->run_outcome_for_sequence(1U) ==
                    second_outcome &&
                exactly_same(*first_outcome, first_outcome_copy) &&
                exactly_same(*second_outcome, second_outcome_copy) &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             opening_state) &&
                bundle.session->committed_outputs().empty() &&
                bundle.session->committed_histories().front().sample_count ==
                    0U,
            "second ResetCommit did not create a clean sequence-two run");
    const auto third_first = bundle.session->execute_step();
    const auto third_second = bundle.session->execute_step();
    const auto third_terminal = bundle.session->execute_step();
    const auto* third_outcome = bundle.session->run_outcome();
    const auto third_seal = sealed_snapshot(*bundle.session);
    require(third_first && third_second && third_terminal &&
                third_first.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                third_second.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                third_terminal.branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                third_terminal.status ==
                    gnc::kernel::StepStatus::Terminated &&
                third_first.run_sequence == 2U &&
                third_second.run_sequence == 2U &&
                third_terminal.run_sequence == 2U &&
                third_outcome != nullptr &&
                third_outcome->run_sequence == 2U &&
                third_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Reset &&
                third_outcome->initial_committed_epoch == 8U &&
                third_outcome->final_committed_epoch == 11U &&
                bundle.session->committed_epoch() == 11U &&
                bundle.session->committed_tick() == 2 &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             first_state) &&
                exactly_same(bundle.session->committed_histories(),
                             first_history) &&
                !third_seal.seals.empty() &&
                third_seal.seals.front().generation ==
                    second_seal.seals.front().generation + 3U &&
                exact_sealed_observation_snapshot(
                    third_seal, first_seal, false) &&
                exactly_same(mission_result_probe(*bundle.session,
                                                  bundle.adapter),
                             first_result) &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome &&
                bundle.session->run_outcome_for_sequence(1U) ==
                    second_outcome &&
                bundle.session->run_outcome_for_sequence(2U) ==
                    third_outcome &&
                exactly_same(*first_outcome, first_outcome_copy) &&
                exactly_same(*second_outcome, second_outcome_copy),
            "third full run was hard-coded to a two-run lifecycle");
}

enum class ResetFault : std::uint8_t {
    EmptyRunId,
    DuplicateRunId,
    BindingMismatch,
    InitialStateConstruct,
    StateCopy,
    StateReplace,
    StateValidation,
    FinalPrecheck,
};

void verify_reset_precommit_failure(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    ResetFault fault, SessionError expected_error,
    RuntimeDiagnosticCode expected_code,
    RuntimeDiagnosticStage expected_stage,
    std::string_view label) {
    const std::string initial_id =
        std::string("run:reset-failure-base:") + std::string(label);
    const std::string attempted_id =
        std::string("run:reset-failure-attempt:") + std::string(label);
    auto bundle = initialize_session(image, {}, initial_id);
    require(static_cast<bool>(bundle.session->run_to_terminal()),
            "reset-failure fixture could not complete its first run");
    const auto before_state = committed_probe(*bundle.session,
                                              bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto before_history = bundle.session->committed_histories();
    const auto before_seal = sealed_snapshot(*bundle.session);
    const auto before_result = mission_result_probe(*bundle.session,
                                                    bundle.adapter);
    const auto before_journal_epoch =
        bundle.session->last_step_journal().committed_epoch;
    const auto* first_outcome = bundle.session->run_outcome();
    require(first_outcome != nullptr,
            "reset-failure fixture lacks its completed outcome");
    const auto first_outcome_copy = *first_outcome;

    auto request = reset_request(*image, attempted_id);
    bool binding_matched = true;
    switch (fault) {
    case ResetFault::EmptyRunId:
        request.run_id = gnc::kernel::RunId{};
        break;
    case ResetFault::DuplicateRunId:
        request.run_id = gnc::kernel::RunId{initial_id};
        break;
    case ResetFault::BindingMismatch:
        request.binding.source_semantic_hash += ".mismatch";
        binding_matched = false;
        break;
    case ResetFault::InitialStateConstruct:
        require(bundle.adapter.fail_next_initial_state_construct != nullptr,
                "initial-state reset failure seam is absent");
        *bundle.adapter.fail_next_initial_state_construct = true;
        break;
    case ResetFault::StateCopy:
        require(bundle.adapter.fail_next_state_copy != nullptr,
                "state-copy reset failure seam is absent");
        *bundle.adapter.fail_next_state_copy = true;
        break;
    case ResetFault::StateReplace:
        require(bundle.adapter.fail_next_state_replace != nullptr,
                "state-replace reset failure seam is absent");
        *bundle.adapter.fail_next_state_replace = true;
        break;
    case ResetFault::StateValidation:
        require(bundle.adapter.fail_next_state_validation != nullptr,
                "state-validation reset failure seam is absent");
        *bundle.adapter.fail_next_state_validation = true;
        break;
    case ResetFault::FinalPrecheck:
        require(bundle.adapter.disable_state_nofail_swap != nullptr,
                "reset precheck failure seam is absent");
        *bundle.adapter.disable_state_nofail_swap = true;
        break;
    }
    const auto attempted_run_id = request.run_id;
    const auto failed = bundle.session->reset(std::move(request));
    const auto* failed_outcome = bundle.session->run_outcome();
    require(!failed && failed.status == gnc::kernel::ResetStatus::Failed &&
                failed.result.error == expected_error &&
                failed.run_id == attempted_run_id &&
                failed.proposed_run_sequence == 1U &&
                failed.binding_matched == binding_matched &&
                !failed.reset_commit && failed.committed_epoch == 3U &&
                failed.committed_tick == 2 &&
                failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->code == expected_code &&
                failed.primary_diagnostic->stage == expected_stage &&
                failed.primary_diagnostic->run_id == attempted_run_id &&
                failed.primary_diagnostic->base_epoch == 3U &&
                failed.primary_diagnostic->tick == 2 &&
                failed.primary_diagnostic->cause_code == expected_error &&
                failed.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                bundle.session->state() ==
                    gnc::kernel::SessionState::Failed &&
                bundle.session->active_run_id() == nullptr &&
                bundle.session->active_run_binding() == nullptr &&
                bundle.session->last_committed_run_id() != nullptr &&
                bundle.session->last_committed_run_id()->value() ==
                    initial_id &&
                bundle.session->last_committed_run_binding() != nullptr &&
                *bundle.session->last_committed_run_binding() ==
                    gnc::kernel::exact_run_binding(*image) &&
                bundle.session->run_sequence().has_value() &&
                *bundle.session->run_sequence() == 0U &&
                bundle.session->committed_epoch() == 3U &&
                bundle.session->committed_tick() == 2 &&
                bundle.session->committed_step_count() == 3U &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                exactly_same(bundle.session->committed_histories(),
                             before_history) &&
                exact_sealed_observation_snapshot(
                    sealed_snapshot(*bundle.session), before_seal) &&
                exactly_same(mission_result_probe(*bundle.session,
                                                  bundle.adapter),
                             before_result) &&
                bundle.session->last_step_journal().committed_epoch ==
                    before_journal_epoch &&
                bundle.session->run_outcome_for_sequence(0U) ==
                    first_outcome &&
                exactly_same(*first_outcome, first_outcome_copy) &&
                failed_outcome != nullptr &&
                failed_outcome != first_outcome &&
                failed_outcome->run_id == attempted_run_id &&
                failed_outcome->run_sequence == 1U &&
                has_exact_image_binding(*failed_outcome, *image) &&
                failed_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::Reset &&
                !failed_outcome->run_start_committed &&
                failed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed_outcome->validity ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                failed_outcome->initial_committed_epoch == 3U &&
                failed_outcome->final_committed_epoch == 3U &&
                failed_outcome->final_tick == 2 &&
                failed_outcome->committed_step_count == 0U &&
                failed_outcome->primary_diagnostic.has_value() &&
                exactly_same(*failed_outcome->primary_diagnostic,
                             *failed.primary_diagnostic) &&
                failed_outcome->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::NotStarted &&
                bundle.session->run_outcome_for_sequence(1U) ==
                    failed_outcome,
            "reset precommit failure changed committed evidence or lost its attempt outcome");
}

void verify_reset_failure_matrix(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    verify_reset_precommit_failure(
        image, ResetFault::EmptyRunId, SessionError::EmptyRunId,
        RuntimeDiagnosticCode::ResetRequestInvalid,
        RuntimeDiagnosticStage::ResetRequest, "empty");
    verify_reset_precommit_failure(
        image, ResetFault::DuplicateRunId, SessionError::DuplicateRunId,
        RuntimeDiagnosticCode::ResetRequestInvalid,
        RuntimeDiagnosticStage::ResetRequest, "duplicate");
    verify_reset_precommit_failure(
        image, ResetFault::BindingMismatch,
        SessionError::RunBindingMismatch,
        RuntimeDiagnosticCode::ImageBindingMismatch,
        RuntimeDiagnosticStage::ResetRequest, "binding");
    verify_reset_precommit_failure(
        image, ResetFault::InitialStateConstruct,
        SessionError::InitialStateFailed,
        RuntimeDiagnosticCode::MaterializationFailed,
        RuntimeDiagnosticStage::ResetState, "construct");
    verify_reset_precommit_failure(
        image, ResetFault::StateCopy, SessionError::ResetStateFailed,
        RuntimeDiagnosticCode::ResetStateRebuildFailed,
        RuntimeDiagnosticStage::ResetState, "copy");
    verify_reset_precommit_failure(
        image, ResetFault::StateReplace, SessionError::ResetStateFailed,
        RuntimeDiagnosticCode::ResetStateRebuildFailed,
        RuntimeDiagnosticStage::ResetState, "replace");
    verify_reset_precommit_failure(
        image, ResetFault::StateValidation,
        SessionError::ObjectValidationFailed,
        RuntimeDiagnosticCode::ObjectValidationFailed,
        RuntimeDiagnosticStage::ResetState, "validation");
    verify_reset_precommit_failure(
        image, ResetFault::FinalPrecheck,
        SessionError::ResetPrecommitFailed,
        RuntimeDiagnosticCode::ResetPrecommitFailed,
        RuntimeDiagnosticStage::ResetPrecommit, "precheck");

    auto fresh = initialize_session(
        image, {}, "run:reset-failure-recovery");
    require(fresh.session->run_to_terminal() &&
                fresh.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                fresh.session->run_outcome() != nullptr &&
                fresh.session->run_outcome()->validity ==
                    gnc::contracts::EvidenceValidity::Valid,
            "fresh Session did not recover after reset failure injection");
}

void verify_cancel_before_first_step_idempotence_and_dispose(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    constexpr std::string_view run_id = "run:cancel-before-first";
    constexpr std::string_view request_id = "cancel:before-first";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    const auto before_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto before_blocks = bundle.session->state_blocks();
    const auto before_histories = bundle.session->committed_histories();
    const auto before_outputs = bundle.session->committed_outputs();
    const auto live_before = bundle.adapter.trace->live_object_count();

    const auto empty_id = bundle.session->request_cancel(
        cancellation_request("", std::string(run_id)));
    const auto wrong_run = bundle.session->request_cancel(
        cancellation_request("cancel:wrong-run", "run:other"));
    const auto accepted = bundle.session->request_cancel(
        cancellation_request(std::string(request_id),
                             std::string(run_id)));
    const auto repeated = bundle.session->request_cancel(
        cancellation_request(std::string(request_id),
                             std::string(run_id)));
    const auto superseded = bundle.session->request_cancel(
        cancellation_request("cancel:before-first:other",
                             std::string(run_id)));
    const auto driven = bundle.session->run_to_terminal();
    const auto& step = bundle.session->last_step_outcome();
    require(!empty_id &&
                empty_id.disposition ==
                    gnc::kernel::CancellationDisposition::Rejected &&
                !wrong_run &&
                wrong_run.disposition ==
                    gnc::kernel::CancellationDisposition::Rejected &&
                accepted &&
                accepted.disposition ==
                    gnc::kernel::CancellationDisposition::Accepted &&
                accepted.observed_committed_epoch == 0U &&
                accepted.observed_committed_tick == 0 && repeated &&
                repeated.disposition ==
                    gnc::kernel::CancellationDisposition::AlreadyRequested &&
                !superseded &&
                superseded.disposition ==
                    gnc::kernel::CancellationDisposition::Superseded &&
                !driven && driven.result &&
                driven.status == gnc::kernel::RunDriveStatus::Cancelled &&
                step.status == gnc::kernel::StepStatus::Cancelled &&
                step.result && !step.primary_diagnostic.has_value() &&
                step.base_epoch == 0U && step.committed_epoch == 0U &&
                step.tick_before == 0 && step.tick_after == 0 &&
                !step.histories.staged &&
                !step.observation_seal.staged &&
                !step.result_seal.staged &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                same_state_blocks(bundle.session->state_blocks(),
                                  before_blocks) &&
                exactly_same(bundle.session->committed_histories(),
                             before_histories) &&
                same_committed_outputs(bundle.session->committed_outputs(),
                                       before_outputs) &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.adapter.trace->live_object_count() == live_before,
            "pre-step cancellation changed committed evidence or request idempotence");
    require_cancelled_run(*bundle.session, run_id, 0U, 0, 0U);

    const auto* frozen = bundle.session->run_outcome();
    const auto frozen_copy = *frozen;
    const auto after_cancel_retry = bundle.session->request_cancel(
        cancellation_request(std::string(request_id),
                             std::string(run_id)));
    require(after_cancel_retry &&
                after_cancel_retry.disposition ==
                    gnc::kernel::CancellationDisposition::AlreadyRequested &&
                bundle.session->run_outcome() == frozen &&
                exactly_same(*frozen, frozen_copy),
            "idempotent cancellation retry changed the frozen outcome");

    const auto last_run_id = *bundle.session->last_committed_run_id();
    require(bundle.session->dispose() &&
                bundle.session->state() ==
                    gnc::kernel::SessionState::Disposed &&
                bundle.session->preparation_count() == 0U &&
                bundle.session->runtime_cell_count() == 0U &&
                bundle.session->committed_state_count() == 0U &&
                bundle.adapter.trace->live_object_count() == 0U &&
                bundle.session->last_committed_run_id() != nullptr &&
                *bundle.session->last_committed_run_id() == last_run_id &&
                bundle.session->run_outcome() == frozen &&
                exactly_same(*frozen, frozen_copy) &&
                &bundle.session->image() == image.get(),
            "cancelled Session dispose lost identity/outcome or leaked resources");
    const auto event_count = bundle.adapter.trace->events.size();
    bundle.session.reset();
    require(bundle.adapter.trace->events.size() == event_count,
            "cancelled Session destructor repeated explicit cleanup");
}

void verify_cancel_between_boundary_callsites(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto discovery = initialize_session(
        image, {}, "run:cancel-boundary-discovery");
    require(discovery.session->execute_step() &&
                !discovery.session->last_boundary_summary()
                     .executed_callsite_handles.empty(),
            "boundary cancellation discovery step failed");
    const auto full_call_count =
        discovery.session->last_boundary_summary()
            .executed_callsite_handles.size();
    const auto first_callsite =
        discovery.session->last_boundary_summary()
            .executed_callsite_handles.front();
    require(full_call_count > 1U &&
                std::count_if(
                    image->cancellation_policy().safe_points.begin(),
                    image->cancellation_policy().safe_points.end(),
                    [first_callsite](const auto& point) {
                        return point.kind ==
                                   gnc::contracts::
                                       PlanImageCancellationSafePointKind::
                                           AfterBoundaryCallsite &&
                               point.subject_handle == first_callsite;
                    }) == 1,
            "first boundary callsite lacks a declared cancellation point");

    constexpr std::string_view run_id = "run:cancel-between-boundaries";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    const auto before_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto live_before = bundle.adapter.trace->live_object_count();
    bundle.adapter.coordination->arm(
        AdapterCoordinationPoint::InvocationReturn, first_callsite);
    gnc::kernel::StepOutcome step;
    std::thread execution([&] { step = bundle.session->execute_step(); });
    const auto reached =
        bundle.adapter.coordination->wait_until_reached();
    if (!reached) {
        bundle.adapter.coordination->release();
        execution.join();
        require(false, "boundary cancellation rendezvous was not reached");
    }
    const auto accepted = bundle.session->request_cancel(
        cancellation_request("cancel:between-boundaries",
                             std::string(run_id)));
    bundle.adapter.coordination->release();
    execution.join();

    const auto& boundary = bundle.session->last_boundary_summary();
    require(accepted &&
                accepted.disposition ==
                    gnc::kernel::CancellationDisposition::Accepted &&
                step.status == gnc::kernel::StepStatus::Cancelled &&
                step.result && !step.primary_diagnostic.has_value() &&
                boundary.executed_callsite_handles.size() == 1U &&
                boundary.executed_callsite_handles.front() ==
                    first_callsite &&
                bundle.adapter.opening_boundary->call_order.size() == 1U &&
                bundle.adapter.opening_boundary->call_order.front() ==
                    first_callsite &&
                bundle.adapter.opening_boundary->call_order.size() <
                    full_call_count &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                bundle.session->committed_outputs().empty() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.adapter.trace->live_object_count() == live_before,
            "boundary cancellation ran a later callsite or retained frame output");
    require_history(*bundle.session, 0U, 0, 0);
    require_cancelled_run(*bundle.session, run_id, 0U, 0, 0U);
}

void verify_cancel_between_candidate_producers(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    const auto& transaction = image->transactions().front();
    const auto integration = std::find_if(
        transaction.candidates.begin(), transaction.candidates.end(),
        [](const auto& member) {
            return member.producer_kind == "IntegrationScope";
        });
    require(integration != transaction.candidates.end(),
            "candidate cancellation lacks an IntegrationScope member");
    constexpr std::string_view run_id = "run:cancel-between-candidates";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    const auto before_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto live_before = bundle.adapter.trace->live_object_count();
    const auto held_handle = transaction.held_slot_handles.front();
    const auto initial_slot_constructs =
        bundle.adapter.trace->constructed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    const auto initial_slot_destroys =
        bundle.adapter.trace->destroyed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    const auto before_held_constructs = static_cast<std::size_t>(std::count(
        initial_slot_constructs.begin(), initial_slot_constructs.end(),
        held_handle));
    const auto before_held_destroys = static_cast<std::size_t>(std::count(
        initial_slot_destroys.begin(), initial_slot_destroys.end(),
        held_handle));

    bundle.adapter.coordination->arm(
        AdapterCoordinationPoint::IntegrationReturn,
        integration->producer_handle);
    gnc::kernel::StepOutcome step;
    std::thread execution([&] { step = bundle.session->execute_step(); });
    const auto reached =
        bundle.adapter.coordination->wait_until_reached();
    if (!reached) {
        bundle.adapter.coordination->release();
        execution.join();
        require(false, "candidate cancellation rendezvous was not reached");
    }
    const auto accepted = bundle.session->request_cancel(
        cancellation_request("cancel:between-candidates",
                             std::string(run_id)));
    bundle.adapter.coordination->release();
    execution.join();

    const auto held_constructs =
        bundle.adapter.trace->constructed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    const auto held_destroys =
        bundle.adapter.trace->destroyed_handles(
            gnc::tests::ref_yyz::TraceObjectKind::Slot);
    require(accepted &&
                step.status == gnc::kernel::StepStatus::Cancelled &&
                step.result && !step.primary_diagnostic.has_value() &&
                step.candidates.planned_count ==
                    transaction.candidates.size() &&
                step.candidates.present_count == 0U &&
                step.candidates.valid_count == 0U &&
                bundle.session->last_step_journal()
                        .candidate_slot_handles.size() == 1U &&
                bundle.adapter.step_execution->integration_attempts == 1U &&
                bundle.adapter.step_execution->mass_evolution_attempts ==
                    0U &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                bundle.session->committed_outputs().empty() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.adapter.trace->live_object_count() == live_before &&
                static_cast<std::size_t>(std::count(
                    held_constructs.begin(), held_constructs.end(),
                    held_handle)) == before_held_constructs + 1U &&
                static_cast<std::size_t>(std::count(
                    held_destroys.begin(), held_destroys.end(),
                    held_handle)) == before_held_destroys + 1U,
            "candidate cancellation retained candidate/held staging or changed state");
    require_history(*bundle.session, 0U, 0, 0);
    require_cancelled_run(*bundle.session, run_id, 0U, 0, 0U);
}

void verify_cancel_at_final_precommit(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    const auto& transaction = image->transactions().front();
    const auto subject = initial_binding_handle_for_candidate(
        *image, transaction.candidates.front());
    constexpr std::string_view run_id = "run:cancel-final-precommit";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    const auto before_state = committed_probe(*bundle.session,
                                               bundle.adapter);
    const auto before_history = bundle.session->committed_histories();
    const auto live_before = bundle.adapter.trace->live_object_count();
    bundle.adapter.coordination->arm(
        AdapterCoordinationPoint::FinalPrecommit, subject);
    gnc::kernel::StepOutcome step;
    std::thread execution([&] { step = bundle.session->execute_step(); });
    const auto reached =
        bundle.adapter.coordination->wait_until_reached();
    if (!reached) {
        bundle.adapter.coordination->release();
        execution.join();
        require(false, "final-precommit cancellation rendezvous was not reached");
    }
    const auto accepted = bundle.session->request_cancel(
        cancellation_request("cancel:final-precommit",
                             std::string(run_id)));
    bundle.adapter.coordination->release();
    execution.join();
    require(accepted &&
                step.status == gnc::kernel::StepStatus::Cancelled &&
                step.result &&
                bundle.session->last_step_journal().prevalidated &&
                !bundle.session->last_step_journal().committed &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             before_state) &&
                exactly_same(bundle.session->committed_histories(),
                             before_history) &&
                bundle.session->committed_outputs().empty() &&
                all_frame_slots_absent(*bundle.session) &&
                bundle.adapter.trace->live_object_count() == live_before,
            "final-precommit cancellation crossed ModelCommit");
    require_cancelled_run(*bundle.session, run_id, 0U, 0, 0U);
}

void verify_cancel_at_postcommit_boundary(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    const auto& transaction = image->transactions().front();
    const auto subject = initial_binding_handle_for_candidate(
        *image, transaction.candidates.back());
    constexpr std::string_view run_id = "run:cancel-postcommit";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    bundle.adapter.coordination->arm(
        AdapterCoordinationPoint::ModelCommit, subject);
    gnc::kernel::RunDriveOutcome drive;
    std::thread execution(
        [&] { drive = bundle.session->run_to_terminal(); });
    const auto reached =
        bundle.adapter.coordination->wait_until_reached();
    if (!reached) {
        bundle.adapter.coordination->release();
        execution.join();
        require(false, "postcommit cancellation rendezvous was not reached");
    }
    const auto accepted = bundle.session->request_cancel(
        cancellation_request("cancel:postcommit", std::string(run_id)));
    bundle.adapter.coordination->release();
    execution.join();
    const auto& step = bundle.session->last_step_outcome();
    require(accepted &&
                accepted.observed_committed_epoch == 0U &&
                accepted.observed_committed_tick == 0 &&
                !drive && drive.result &&
                drive.status == gnc::kernel::RunDriveStatus::Cancelled &&
                step.status == gnc::kernel::StepStatus::Committed &&
                step.result && !step.primary_diagnostic.has_value() &&
                step.committed_epoch == 1U && step.tick_after == 1 &&
                bundle.session->last_step_journal().committed &&
                all_frame_slots_absent(*bundle.session),
            "postcommit cancellation rewrote the committed StepOutcome");
    require_tick_one_oracle(committed_probe(*bundle.session,
                                            bundle.adapter));
    require_history(*bundle.session, 1U, 0, 0);
    require_cancelled_run(*bundle.session, run_id, 1U, 1, 1U);
}

void verify_cancel_after_first_continue_before_next_boundary(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    constexpr std::string_view run_id = "run:cancel-after-one-commit";
    auto bundle = initialize_session(image, {}, std::string(run_id));
    const auto first = bundle.session->execute_step();
    require(first.status == gnc::kernel::StepStatus::Committed && first,
            "one-commit cancellation fixture could not commit step one");
    const auto committed_state = committed_probe(*bundle.session,
                                                  bundle.adapter);
    const auto committed_history = bundle.session->committed_histories();
    const auto committed_seal = sealed_snapshot(*bundle.session);
    const auto call_count =
        bundle.adapter.opening_boundary->call_order.size();
    const auto accepted = bundle.session->request_cancel(
        cancellation_request("cancel:after-one-commit",
                             std::string(run_id)));
    const auto cancelled = bundle.session->execute_step();
    require(accepted &&
                accepted.observed_committed_epoch == 1U &&
                accepted.observed_committed_tick == 1 &&
                cancelled.status ==
                    gnc::kernel::StepStatus::Cancelled &&
                cancelled.result &&
                bundle.adapter.opening_boundary->call_order.size() ==
                    call_count &&
                exactly_same(committed_probe(*bundle.session,
                                             bundle.adapter),
                             committed_state) &&
                exactly_same(bundle.session->committed_histories(),
                             committed_history) &&
                exact_sealed_observation_snapshot(
                    sealed_snapshot(*bundle.session), committed_seal) &&
                all_frame_slots_absent(*bundle.session),
            "next-transaction cancellation changed the first committed boundary");
    require_cancelled_run(*bundle.session, run_id, 1U, 1, 1U);
}

void verify_terminal_and_failure_precedence_over_cancellation(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    {
        constexpr std::string_view run_id =
            "run:cancel-after-terminal";
        auto bundle = initialize_session(image, {}, std::string(run_id));
        const auto drive = bundle.session->run_to_terminal();
        const auto* outcome = bundle.session->run_outcome();
        require(drive && outcome != nullptr,
                "terminal cancellation fixture did not complete");
        const auto frozen = *outcome;
        const auto rejected = bundle.session->request_cancel(
            cancellation_request("cancel:after-terminal",
                                 std::string(run_id)));
        require(!rejected &&
                    rejected.disposition ==
                        gnc::kernel::CancellationDisposition::Rejected &&
                    rejected.observed_committed_epoch == 3U &&
                    rejected.observed_committed_tick == 2 &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Completed &&
                    bundle.session->run_outcome() == outcome &&
                    outcome->final_status ==
                        gnc::kernel::RunFinalStatus::Completed &&
                    outcome->validity ==
                        gnc::contracts::EvidenceValidity::Valid &&
                    exactly_same(*outcome, frozen),
                "post-Terminal cancellation overrode Completed outcome");
    }

    {
        constexpr std::string_view run_id =
            "run:cancel-after-failure";
        AdapterOptions options;
        options.failure = {FailurePhase::Boundary, 0U};
        auto bundle = initialize_session(image, options,
                                         std::string(run_id));
        const auto failed = bundle.session->execute_step();
        const auto* outcome = bundle.session->run_outcome();
        require(!failed && outcome != nullptr &&
                    outcome->final_status ==
                        gnc::kernel::RunFinalStatus::Failed &&
                    outcome->primary_diagnostic.has_value(),
                "failure precedence fixture did not freeze its failure");
        const auto frozen = *outcome;
        const auto superseded = bundle.session->request_cancel(
            cancellation_request("cancel:after-failure",
                                 std::string(run_id)));
        require(!superseded &&
                    superseded.disposition ==
                        gnc::kernel::CancellationDisposition::Superseded &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Failed &&
                    bundle.session->run_outcome() == outcome &&
                    exactly_same(*outcome, frozen),
                "later cancellation replaced the primary execution failure");
    }
}

void verify_cancellation_session_isolation(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto first = gnc::kernel::create_session(image, adapter.provider);
    auto second = gnc::kernel::create_session(image, adapter.provider);
    require(first && second &&
                first.session->initialize(initialization_request(
                    *image, "run:cancel-isolation-first")) &&
                second.session->initialize(initialization_request(
                    *image, "run:cancel-isolation-second")),
            "cancellation isolation Sessions could not initialize");
    const auto second_before = committed_probe(*second.session, adapter);
    const auto accepted = first.session->request_cancel(
        cancellation_request("cancel:isolation-first",
                             "run:cancel-isolation-first"));
    const auto cancelled = first.session->execute_step();
    const auto completed = second.session->run_to_terminal();
    require(accepted &&
                cancelled.status ==
                    gnc::kernel::StepStatus::Cancelled &&
                completed &&
                first.session->state() ==
                    gnc::kernel::SessionState::Cancelled &&
                first.session->committed_epoch() == 0U &&
                first.session->committed_tick() == 0 &&
                exactly_same(committed_probe(*first.session, adapter),
                             second_before) &&
                second.session->state() ==
                    gnc::kernel::SessionState::Completed &&
                second.session->committed_epoch() == 3U &&
                second.session->committed_tick() == 2 &&
                second.session->run_outcome() != nullptr &&
                second.session->run_outcome()->final_status ==
                    gnc::kernel::RunFinalStatus::Completed,
            "one Session cancellation crossed the shared Image/provider boundary");
    require_cancelled_run(*first.session, "run:cancel-isolation-first",
                          0U, 0, 0U);
    require_mission_oracle(mission_result_probe(*second.session, adapter));
}

void verify_dispose_lifecycle(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    {
        auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
        require(static_cast<bool>(adapter), adapter.error);
        auto created = gnc::kernel::create_session(image, adapter.provider);
        require(static_cast<bool>(created),
                "Created dispose fixture could not create a Session");
        const auto created_reset = created.session->reset(
            reset_request(*image, "run:created-reset"));
        require(!created_reset &&
                    created_reset.result.error ==
                        SessionError::InvalidLifecycleTransition &&
                    created.session->state() ==
                        gnc::kernel::SessionState::Created &&
                    created.session->run_outcome() == nullptr &&
                    created.session->dispose() &&
                    created.session->state() ==
                        gnc::kernel::SessionState::Disposed &&
                    created.session->preparation_count() == 0U &&
                    created.session->runtime_cell_count() == 0U &&
                    created.session->committed_state_count() == 0U &&
                    &created.session->image() == image.get() &&
                    created.session->last_committed_run_id() == nullptr &&
                    created.session->run_outcome() == nullptr,
                "Created Session did not dispose cleanly");
        const auto rejected_reset = created.session->reset(
            reset_request(*image, "run:disposed-reset"));
        const auto event_count = adapter.trace->events.size();
        const auto repeated = created.session->dispose();
        require(!rejected_reset &&
                    rejected_reset.result.error ==
                        SessionError::InvalidLifecycleTransition &&
                    !repeated &&
                    repeated.error ==
                        SessionError::InvalidLifecycleTransition &&
                    adapter.trace->events.size() == event_count,
                "Disposed Session accepted reset/dispose reentry");
        created.session.reset();
        require(adapter.trace->events.size() == event_count,
                "Created dispose was repeated by the destructor");
    }

    {
        auto bundle = initialize_session(
            image, {}, "run:dispose-completed");
        const auto live_initialized =
            bundle.adapter.trace->live_object_count();
        const auto rejected_reset = bundle.session->reset(
            reset_request(*image, "run:dispose-active-reset"));
        const auto rejected_dispose = bundle.session->dispose();
        require(!rejected_reset &&
                    rejected_reset.result.error ==
                        SessionError::InvalidLifecycleTransition &&
                    !rejected_dispose &&
                    rejected_dispose.error ==
                        SessionError::InvalidLifecycleTransition &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Initialized &&
                    bundle.session->run_outcome() == nullptr &&
                    bundle.adapter.trace->live_object_count() ==
                        live_initialized,
                "active run was reset or disposed implicitly");
        require(static_cast<bool>(bundle.session->run_to_terminal()),
                "Completed dispose fixture did not terminate");
        const auto* outcome = bundle.session->run_outcome();
        require(outcome != nullptr,
                "Completed dispose fixture lacks an outcome");
        const auto outcome_copy = *outcome;
        const auto run_id = *bundle.session->last_committed_run_id();
        require(bundle.adapter.trace->live_object_count() > 0U &&
                    bundle.session->dispose() &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Disposed &&
                    bundle.session->preparation_count() == 0U &&
                    bundle.session->runtime_cell_count() == 0U &&
                    bundle.session->committed_state_count() == 0U &&
                    bundle.session->storage_extents().empty() &&
                    bundle.session->committed_histories().empty() &&
                    bundle.session->committed_outputs().empty() &&
                    bundle.adapter.trace->live_object_count() == 0U &&
                    &bundle.session->image() == image.get() &&
                    bundle.session->last_committed_run_id() != nullptr &&
                    *bundle.session->last_committed_run_id() == run_id &&
                    bundle.session->run_outcome() == outcome &&
                    bundle.session->run_outcome_for_sequence(0U) == outcome &&
                    exactly_same(*outcome, outcome_copy),
                "Completed dispose lost identity/outcome or retained resources");
        const auto event_count = bundle.adapter.trace->events.size();
        const auto repeated = bundle.session->dispose();
        require(!repeated &&
                    repeated.error ==
                        SessionError::InvalidLifecycleTransition &&
                    bundle.adapter.trace->events.size() == event_count,
                "Completed dispose ran cleanup twice");
        bundle.session.reset();
        require(bundle.adapter.trace->events.size() == event_count,
                "Completed dispose was repeated by the destructor");
    }

    {
        AdapterOptions options;
        options.failure = {FailurePhase::Boundary, 0U};
        auto bundle = initialize_session(
            image, options, "run:dispose-execution-failed");
        const auto failed_step = bundle.session->execute_step();
        const auto* outcome = bundle.session->run_outcome();
        require(!failed_step && outcome != nullptr &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Failed &&
                    bundle.adapter.trace->live_object_count() > 0U,
                "execution-failed dispose fixture did not fail in-run");
        const auto outcome_copy = *outcome;
        const auto rejected_reset = bundle.session->reset(
            reset_request(*image, "run:dispose-failed-reset"));
        require(!rejected_reset &&
                    rejected_reset.result.error ==
                        SessionError::InvalidLifecycleTransition &&
                    bundle.session->dispose() &&
                    bundle.session->state() ==
                        gnc::kernel::SessionState::Disposed &&
                    bundle.session->preparation_count() == 0U &&
                    bundle.session->runtime_cell_count() == 0U &&
                    bundle.session->committed_state_count() == 0U &&
                    bundle.adapter.trace->live_object_count() == 0U &&
                    bundle.session->run_outcome() == outcome &&
                    bundle.session->run_outcome_for_sequence(0U) == outcome &&
                    exactly_same(*outcome, outcome_copy),
                "execution-failed dispose lost its frozen outcome or leaked resources");
        const auto event_count = bundle.adapter.trace->events.size();
        bundle.session.reset();
        require(bundle.adapter.trace->events.size() == event_count,
                "execution-failed dispose was repeated by the destructor");
    }
}

void run() {
    const auto image = build_image();
    verify_initialization_identity_and_commit(image);
    verify_reset_diagnostic_code_stability();
    verify_complete_step_transactions(image);
    verify_run_to_terminal(image);
    verify_failure_matrix(image);
    verify_nonzero_failure_matrix(image);
    verify_terminal_failure_matrix(image);
    verify_two_session_isolation(image);
    verify_query_count_invariance(image);
    verify_completed_run_reset(image);
    verify_reset_capability_fail_closed(image);
    verify_reset_failure_matrix(image);
    verify_cancel_before_first_step_idempotence_and_dispose(image);
    verify_cancel_between_boundary_callsites(image);
    verify_cancel_between_candidate_producers(image);
    verify_cancel_at_final_precommit(image);
    verify_cancel_at_postcommit_boundary(image);
    verify_cancel_after_first_continue_before_next_boundary(image);
    verify_terminal_and_failure_precedence_over_cancellation(image);
    verify_cancellation_session_isolation(image);
    verify_dispose_lifecycle(image);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_step_transaction_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 REF-YYZ formal run lifecycle: PASS"
                  << " (max_abs_difference=" << std::setprecision(17)
                  << maximum_observed_absolute_difference << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 REF-YYZ formal run lifecycle: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
