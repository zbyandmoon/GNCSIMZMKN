#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace ref_yyz = gnc::tests::ref_yyz;
namespace yyz = gnc::packages::yyz;

using Image = contracts::ExecutionPlanImage;
using CommittedHistorySampleProbe =
    ref_yyz::CommittedHistorySampleProbe;
using CommittedRigidMassProbe = ref_yyz::CommittedRigidMassProbe;
using MissionResultProbe = ref_yyz::MissionResultProbe;

constexpr std::string_view kBaselineFingerprint =
    "7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14";
constexpr std::string_view kInertialFrame =
    "frame.fixture.yyz.inertial-cartesian@1";
constexpr std::string_view kClock = "clock.fixture.yyz.simulation@1";
constexpr std::int64_t kShortTerminalTick = 31;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Outcome>
[[nodiscard]] std::string diagnostic_text(const Outcome& outcome) {
    std::string result;
    for (const auto& diagnostic : outcome.diagnostics) {
        if (!result.empty()) result += "; ";
        result += std::string(compiler::to_string(diagnostic.code));
        result += " ";
        result += diagnostic.subject;
        result += ": ";
        result += diagnostic.detail;
    }
    return result;
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

[[nodiscard]] bool exactly_same(
    const std::vector<CommittedHistorySampleProbe>& lhs,
    const std::vector<CommittedHistorySampleProbe>& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (lhs[index].tick != rhs[index].tick ||
            lhs[index].committed_epoch != rhs[index].committed_epoch ||
            !exactly_same(lhs[index].state, rhs[index].state)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exactly_same(const MissionResultProbe& lhs,
                                const MissionResultProbe& rhs) noexcept {
    return lhs.present == rhs.present && lhs.completed == rhs.completed &&
           lhs.initial_tick == rhs.initial_tick &&
           lhs.final_tick == rhs.final_tick &&
           exact_double(lhs.final_time_seconds, rhs.final_time_seconds) &&
           lhs.reason_code == rhs.reason_code &&
           lhs.priority == rhs.priority &&
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

[[nodiscard]] bool exactly_same(const kernel::RuntimeDiagnostic& lhs,
                                const kernel::RuntimeDiagnostic& rhs) noexcept {
    return lhs.code == rhs.code && lhs.stage == rhs.stage &&
           lhs.subject_handle == rhs.subject_handle &&
           lhs.tick == rhs.tick &&
           lhs.base_epoch == rhs.base_epoch &&
           lhs.cause_code == rhs.cause_code &&
           lhs.cause_ref == rhs.cause_ref &&
           lhs.validity_effect == rhs.validity_effect &&
           lhs.disposition == rhs.disposition &&
           lhs.message_key == rhs.message_key && lhs.detail == rhs.detail;
}

[[nodiscard]] bool exactly_same(const kernel::RunOutcome& lhs,
                                const kernel::RunOutcome& rhs) noexcept {
    const bool diagnostic_equal =
        lhs.primary_diagnostic.has_value() ==
            rhs.primary_diagnostic.has_value() &&
        (!lhs.primary_diagnostic.has_value() ||
         exactly_same(*lhs.primary_diagnostic, *rhs.primary_diagnostic));
    return lhs.image_fingerprint == rhs.image_fingerprint &&
           lhs.plan_id == rhs.plan_id && lhs.mission_id == rhs.mission_id &&
           lhs.source_semantic_hash == rhs.source_semantic_hash &&
           lhs.descriptor_semantic_hash == rhs.descriptor_semantic_hash &&
           lhs.run_start_kind == rhs.run_start_kind &&
           lhs.run_start_committed == rhs.run_start_committed &&
           lhs.final_status == rhs.final_status &&
           lhs.validity == rhs.validity &&
           lhs.initial_tick == rhs.initial_tick &&
           lhs.final_tick == rhs.final_tick &&
           lhs.initial_committed_epoch == rhs.initial_committed_epoch &&
           lhs.final_committed_epoch == rhs.final_committed_epoch &&
           lhs.committed_step_count == rhs.committed_step_count &&
           lhs.terminal_branch_committed ==
               rhs.terminal_branch_committed &&
           lhs.mission_result_available == rhs.mission_result_available &&
           lhs.finalization_status == rhs.finalization_status &&
           diagnostic_equal;
}

[[nodiscard]] const gnc::model_sdk::StaticModelDescriptor& package_model(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::string_view model_id) {
    const auto found = std::find_if(
        package.models.begin(), package.models.end(),
        [model_id](const auto& model) {
            return model.definition.model_id == model_id;
        });
    require(found != package.models.end(), "package model is missing");
    return *found;
}

[[nodiscard]] const contracts::PlanImageRuntimeComponent& image_component(
    const Image& image, std::string_view model_id) {
    const auto occurrence = std::find_if(
        image.occurrences().begin(), image.occurrences().end(),
        [model_id](const auto& value) {
            return value.definition_id == model_id;
        });
    require(occurrence != image.occurrences().end(),
            "Image occurrence is missing");
    const auto component = std::find_if(
        image.runtime_components().begin(),
        image.runtime_components().end(),
        [&occurrence](const auto& value) {
            return value.occurrence_handle == occurrence->handle;
        });
    require(component != image.runtime_components().end(),
            "Image RuntimeComponent is missing");
    return *component;
}

[[nodiscard]] std::shared_ptr<const Image> link_source(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    const compiler::CompleteStaticCompositionSource& source) {
    const auto implementation =
        yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release", package);
    const auto linked = compiler::compile_and_link_complete_execution_plan(
        source, {package}, {implementation});
    require(linked.succeeded(),
            std::string("target-rate Image compilation failed: ") +
                diagnostic_text(linked));
    return std::make_shared<const Image>(*linked.value);
}

[[nodiscard]] std::shared_ptr<const Image> target_image(
    std::int64_t terminal_tick) {
    const auto linked =
        ref_yyz::compile_00a_target_rate_image(terminal_tick);
    require(linked.succeeded(),
            std::string("00A target-rate Image failed: ") +
                diagnostic_text(linked));
    return std::make_shared<const Image>(*linked.value);
}

[[nodiscard]] std::shared_ptr<const Image> zero_control_image(
    std::int64_t terminal_tick) {
    const auto package = yyz::describe_yyz_rigid_step_package();
    auto source =
        ref_yyz::make_00a_target_rate_source(package, terminal_tick);
    source.mission_id =
        "mission.qualification.yyz.00a-target-rate-zero-control@1";
    source.plan_id =
        "plan.qualification.yyz.00a-target-rate-zero-control";
    const auto controller = std::find_if(
        source.occurrences.begin(), source.occurrences.end(),
        [](const auto& occurrence) {
            return occurrence.model_id ==
                   yyz::kPitchMomentControllerModelIdentity;
        });
    require(controller != source.occurrences.end(),
            "zero-control occurrence is missing");
    std::size_t changed = 0U;
    for (auto& field : controller->configuration.fields) {
        if (field.field_id ==
                "pitch_error_gain_newton_meters_per_radian" ||
            field.field_id ==
                "pitch_rate_gain_newton_meter_seconds_per_radian") {
            field.value = 0.0;
            ++changed;
        }
    }
    require(changed == 2U, "zero-control gains are incomplete");
    return link_source(package, source);
}

void verify_navigation_oracle() {
    yyz::TruthPassthroughNavigationDefinition definition;
    definition.model_id =
        std::string(yyz::kTruthPassthroughNavigationModelIdentity);
    definition.model_version =
        std::string(yyz::kTruthPassthroughNavigationModelVersion);
    definition.inertial_frame = {std::string(kInertialFrame)};
    definition.clock_domain = {std::string(kClock)};
    definition.configuration_revision = 11;
    const auto configuration =
        yyz::canonical_truth_passthrough_navigation_config(definition);
    const auto rebuilt =
        yyz::build_truth_passthrough_navigation_definition(configuration);
    require(rebuilt.succeeded() && rebuilt.has_value() &&
                yyz::canonical_truth_passthrough_navigation_config(
                    rebuilt.value()) == configuration,
            "navigation definition did not round-trip");

    yyz::CommittedRigidObservation observation;
    observation.context = {
        {std::string(kInertialFrame)}, {std::string(kClock)},
        {7, 0.07}, 11, contracts::DataQuality::Valid};
    observation.state.position.value =
        gnc::foundation::Vec3{1.0, 2.0, 3.0};
    observation.state.velocity.value =
        gnc::foundation::Vec3{4.0, 5.0, 6.0};
    observation.state.attitude.value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
    observation.state.angular_rate.value =
        gnc::foundation::Vec3{0.1, 0.2, 0.3};
    const auto output = yyz::TruthPassthroughNavigationKernel::evaluate(
        rebuilt.value(), observation);
    require(output.succeeded() && output.has_value() &&
                output.value().context.sample_time.tick == 7 &&
                exact_double(output.value().context.sample_time.seconds,
                             0.07) &&
                (output.value().state.position.value.array() ==
                 observation.state.position.value.array()).all() &&
                (output.value().state.velocity.value.array() ==
                 observation.state.velocity.value.array()).all() &&
                (output.value().state.attitude.value.coeffs().array() ==
                 observation.state.attitude.value.coeffs().array()).all() &&
                (output.value().state.angular_rate.value.array() ==
                 observation.state.angular_rate.value.array()).all(),
            "navigation changed its truth observation");
    auto invalid = observation;
    invalid.context.quality = contracts::DataQuality::Invalid;
    require(!yyz::TruthPassthroughNavigationKernel::evaluate(
                 rebuilt.value(), invalid)
                 .succeeded(),
            "navigation accepted an invalid observation context");
}

void verify_static_target_contract(
    const std::shared_ptr<const Image>& short_image) {
    const auto package = yyz::describe_yyz_rigid_step_package();
    for (const auto model_id : {
             yyz::kTruthPassthroughNavigationModelIdentity,
             yyz::kAltitudePitchGuidanceModelIdentity,
             yyz::kPitchMomentControllerModelIdentity,
             yyz::kIdealBodyMomentActuatorModelIdentity}) {
        const auto& model = package_model(package, model_id);
        require(model.runtime_component.has_value() &&
                    model.runtime_component->schedule.step_interval == 1U &&
                    model.runtime_component->schedule.offset == 0U,
                "package target component ceased to be interval one");
    }
    const auto& navigation = image_component(
        *short_image, yyz::kTruthPassthroughNavigationModelIdentity);
    const auto& guidance = image_component(
        *short_image, yyz::kAltitudePitchGuidanceModelIdentity);
    const auto& controller = image_component(
        *short_image, yyz::kPitchMomentControllerModelIdentity);
    const auto& actuator = image_component(
        *short_image, yyz::kIdealBodyMomentActuatorModelIdentity);
    require(short_image->clock().base_step_seconds == 0.01 &&
                navigation.step_interval == 1U && navigation.offset == 0U &&
                guidance.step_interval == 5U && guidance.offset == 0U &&
                controller.step_interval == 2U && controller.offset == 0U &&
                controller.max_input_age_steps == 4U &&
                actuator.step_interval == 1U && actuator.offset == 0U &&
                actuator.max_input_age_steps == 1U &&
                short_image->held_outputs().size() == 2U &&
                short_image->observation_schedules().size() == 1U &&
                short_image->observation_schedules().front().observation_id ==
                    "observation.00a.committed-rigid-mass" &&
                short_image->observation_schedules().front().step_interval ==
                    4U &&
                short_image->observation_schedules().front().offset == 0U,
            "linked 00A rate or maximum-age facts changed");

    const auto source =
        ref_yyz::make_00a_target_rate_source(package, kShortTerminalTick);
    const auto compiled =
        compiler::compile_complete_execution_plan(source, {package});
    require(compiled.succeeded(),
            std::string("target-rate plan compilation failed: ") +
                diagnostic_text(compiled));
    const auto proof = std::find_if(
        compiled.value->proofs.records.begin(),
        compiled.value->proofs.records.end(), [](const auto& record) {
            return record.proof_id ==
                   "proof/observation-schedule/observation.00a.committed-rigid-mass";
        });
    require(compiled.value->plan.observation_schedules.size() == 1U &&
                proof != compiled.value->proofs.records.end() &&
                proof->kind ==
                    compiler::PlanProofKind::TemporalCompatibility,
            "observation cadence plan/proof evidence is incomplete");

    const auto baseline = ref_yyz::compile_complete_image();
    require(baseline.succeeded() &&
                baseline.value->observation_schedules().empty() &&
                baseline.value->fingerprint() == kBaselineFingerprint &&
                baseline.value->source_semantic_hash() !=
                    short_image->source_semantic_hash() &&
                baseline.value->descriptor_semantic_hash() !=
                    short_image->descriptor_semantic_hash() &&
                baseline.value->proof_index_hash() !=
                    short_image->proof_index_hash() &&
                baseline.value->fingerprint() != short_image->fingerprint(),
            "target facts changed the empty-extension baseline or reused its identity");

    auto alternate = source;
    alternate.observation_schedules.front().step_interval = 3U;
    const auto alternate_compiled =
        compiler::compile_complete_execution_plan(alternate, {package});
    require(alternate_compiled.succeeded() &&
                alternate_compiled.value->plan.source_semantic_hash !=
                    compiled.value->plan.source_semantic_hash &&
                alternate_compiled.value->plan.descriptor_semantic_hash !=
                    compiled.value->plan.descriptor_semantic_hash &&
                alternate_compiled.value->proofs.proof_index_hash !=
                    compiled.value->proofs.proof_index_hash,
            "observation cadence did not enter source/plan/proof identity");
    const auto alternate_image = link_source(package, alternate);
    require(alternate_image->fingerprint() != short_image->fingerprint(),
            "observation cadence did not enter Image identity");
    auto invalid = source;
    invalid.observation_schedules.front().step_interval = 0U;
    const auto rejected =
        compiler::compile_complete_execution_plan(invalid, {package});
    require(!rejected.succeeded(),
            "zero observation interval passed source validation");
}

struct SessionBundle {
    ref_yyz::RefYyzSessionAdapter adapter;
    std::unique_ptr<kernel::Session> session;
};

[[nodiscard]] kernel::InitializationRequest initialization_request(
    const Image& image, std::string run_id) {
    return {kernel::RunId(std::move(run_id)),
            kernel::exact_run_binding(image)};
}

[[nodiscard]] SessionBundle initialize_session(
    const std::shared_ptr<const Image>& image, std::string run_id) {
    auto adapter = ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "Session creation failed");
    const auto initialized = creation.session->initialize(
        initialization_request(*image, std::move(run_id)));
    require(static_cast<bool>(initialized),
            "Session initialization failed");
    return {std::move(adapter), std::move(creation.session)};
}

void verify_observation_image_validation(
    const std::shared_ptr<const Image>& image) {
    auto data = image->data();
    require(data.observation_schedules.size() == 1U,
            "target observation schedule is missing");
    data.observation_schedules.front().step_interval = 0U;
    auto malformed = std::make_shared<const Image>(
        Image::freeze(std::move(data)));
    auto adapter = ref_yyz::make_session_adapter(*malformed);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = kernel::create_session(malformed, adapter.provider);
    require(static_cast<bool>(creation),
            "malformed observation Image could not reach validation");
    const auto initialized = creation.session->initialize(
        initialization_request(
            *malformed, "run:00a-invalid-observation-schedule"));
    require(!initialized &&
                initialized.result.error ==
                    kernel::SessionError::InvalidSchedule &&
                creation.session->state() == kernel::SessionState::Failed,
            "invalid observation Image reached an initialized Session");
}

[[nodiscard]] CommittedRigidMassProbe committed_state(
    const SessionBundle& bundle) {
    CommittedRigidMassProbe result;
    const auto read =
        ref_yyz::read_committed_rigid_mass_for_qualification(
            *bundle.session, bundle.adapter, result);
    require(static_cast<bool>(read), "committed state read failed");
    return result;
}

[[nodiscard]] std::vector<CommittedHistorySampleProbe> committed_history(
    const SessionBundle& bundle) {
    std::vector<CommittedHistorySampleProbe> result;
    const auto read = ref_yyz::read_committed_history_for_qualification(
        *bundle.session, result);
    require(static_cast<bool>(read), "committed history read failed");
    return result;
}

[[nodiscard]] MissionResultProbe mission_result(
    const SessionBundle& bundle) {
    MissionResultProbe result;
    const auto read =
        ref_yyz::read_mission_result_for_qualification(
            *bundle.session, bundle.adapter, result);
    require(static_cast<bool>(read),
            std::string("mission result read failed: ") +
                std::string(kernel::to_string(read.error)) + " handle=" +
                std::to_string(read.image_handle) + " / " +
                std::string(read.detail));
    return result;
}

[[nodiscard]] std::vector<std::int64_t> ticks(
    std::int64_t first, std::int64_t last, std::int64_t step) {
    std::vector<std::int64_t> result;
    for (auto tick = first; tick <= last; tick += step) {
        result.push_back(tick);
    }
    return result;
}

void verify_short_target_run(
    const std::shared_ptr<const Image>& image) {
    auto first = initialize_session(image, "run:00a-short-first");
    auto second = initialize_session(image, "run:00a-short-second");
    const auto first_drive = first.session->run_to_terminal();
    const auto second_drive = second.session->run_to_terminal();
    std::string failed_slot;
    const auto slot = std::find_if(
        image->slots().begin(), image->slots().end(),
        [&](const auto& value) {
            return value.handle == first_drive.result.image_handle;
        });
    if (slot != image->slots().end()) failed_slot = slot->slot_id;
    require(first_drive && second_drive,
                std::string("short target-rate run did not complete: ") +
                std::string(kernel::to_string(first_drive.result.error)) +
                " handle=" +
                std::to_string(first_drive.result.image_handle) + " / " +
                failed_slot + " / " +
                std::string(first_drive.result.detail));
    const auto& probe = *first.adapter.opening_boundary;
    const auto expected_controller_ticks =
        ticks(0, kShortTerminalTick, 2);
    require(probe.navigation_output_ticks ==
                    ticks(0, kShortTerminalTick, 1) &&
                probe.navigation_source_ticks ==
                    ticks(0, kShortTerminalTick, 1) &&
                probe.guidance_output_ticks ==
                    ticks(0, kShortTerminalTick, 5) &&
                probe.controller_output_ticks ==
                    expected_controller_ticks &&
                probe.controller_output_context_ticks ==
                    expected_controller_ticks &&
                probe.actuator_output_ticks ==
                    ticks(0, kShortTerminalTick, 1) &&
                probe.navigation_input_samples.size() ==
                    ticks(0, kShortTerminalTick, 1).size() &&
                probe.controller_guidance_samples.size() ==
                    expected_controller_ticks.size() &&
                probe.actuator_controller_samples.size() ==
                    ticks(0, kShortTerminalTick, 1).size() &&
                probe.actuator_pitch_moments.size() ==
                    ticks(0, kShortTerminalTick, 1).size(),
            "short target-rate invocation cadence changed");
    for (std::size_t index = 0U;
         index < probe.navigation_input_samples.size(); ++index) {
        const auto& sample = probe.navigation_input_samples[index];
        require(sample.consumer_tick == static_cast<std::int64_t>(index) &&
                    sample.sample_tick == sample.consumer_tick &&
                    sample.age_steps == 0U && sample.fresh &&
                    sample.quality_valid,
                "navigation lost same-tick truth provenance");
    }
    for (std::size_t index = 0U;
        index < probe.controller_guidance_samples.size(); ++index) {
        const auto& sample = probe.controller_guidance_samples[index];
        const auto consumer_tick =
            static_cast<std::int64_t>(index * 2U);
        const auto source_tick = (consumer_tick / 5) * 5;
        const auto age = static_cast<std::uint64_t>(
            consumer_tick - source_tick);
        require(sample.consumer_tick == expected_controller_ticks[index] &&
                    sample.sample_tick == source_tick &&
                    sample.age_steps == age &&
                    probe.controller_guidance_source_ticks[index] ==
                        source_tick &&
                    sample.fresh == (sample.age_steps == 0U) &&
                    sample.quality_valid,
                "guidance-to-controller HeldLatest age changed");
    }
    for (std::size_t index = 0U;
         index < probe.actuator_controller_samples.size(); ++index) {
        const auto& sample = probe.actuator_controller_samples[index];
        const auto expected_source =
            static_cast<std::int64_t>((index / 2U) * 2U);
        const auto expected_age =
            static_cast<std::uint64_t>(index % 2U);
        require(sample.consumer_tick == static_cast<std::int64_t>(index) &&
                    sample.sample_tick == expected_source &&
                    sample.age_steps == expected_age &&
                    sample.fresh == (expected_age == 0U) &&
                    sample.quality_valid &&
                    exact_double(probe.actuator_pitch_moments[index],
                                 probe.controller_moments[index / 2U]),
                "controller-to-actuator HeldLatest closure changed");
    }
    require(probe.held_moment[1U] != 0.0,
            "controlled pitch moment did not reach the rigid closure");

    const auto first_state = committed_state(first);
    const auto second_state = committed_state(second);
    const auto first_history = committed_history(first);
    const auto second_history = committed_history(second);
    const auto first_result = mission_result(first);
    const auto second_result = mission_result(second);
    require(exactly_same(first_state, second_state) &&
                exactly_same(first_history, second_history) &&
                exactly_same(first_result, second_result) &&
                first.session->run_outcome() != nullptr &&
                second.session->run_outcome() != nullptr &&
                exactly_same(*first.session->run_outcome(),
                             *second.session->run_outcome()),
            "two short target runs were not bit-deterministic");

    const auto zero_image = zero_control_image(kShortTerminalTick);
    auto zero = initialize_session(zero_image, "run:00a-short-zero-control");
    require(static_cast<bool>(zero.session->run_to_terminal()),
            "zero-control comparison did not complete");
    const auto zero_state = committed_state(zero);
    const bool control_effect =
        !exact_array(first_state.attitude_wxyz,
                     zero_state.attitude_wxyz) ||
        !exact_array(first_state.angular_rate,
                     zero_state.angular_rate);
    require(control_effect &&
                zero_image->source_semantic_hash() !=
                    image->source_semantic_hash() &&
                zero_image->fingerprint() != image->fingerprint(),
            "control moment did not alter committed rigid motion");
}

void verify_two_session_isolation(
    const std::shared_ptr<const Image>& image) {
    auto adapter = ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto first_creation = kernel::create_session(image, adapter.provider);
    auto second_creation = kernel::create_session(image, adapter.provider);
    require(first_creation && second_creation,
            "shared-provider Session creation failed");
    auto first = std::move(first_creation.session);
    auto second = std::move(second_creation.session);
    require(first->initialize(initialization_request(
                *image, "run:00a-isolation-first")) &&
                second->initialize(initialization_request(
                    *image, "run:00a-isolation-second")),
            "isolation Session initialization failed");
    CommittedRigidMassProbe idle_before;
    CommittedRigidMassProbe idle_after;
    require(static_cast<bool>(
                ref_yyz::read_committed_rigid_mass_for_qualification(
                    *second, adapter, idle_before)),
            "idle sibling opening state read failed");
    const auto idle_epoch = second->committed_epoch();
    const auto idle_tick = second->committed_tick();
    require(static_cast<bool>(first->execute_step()),
            "first isolation step failed");
    require(ref_yyz::read_committed_rigid_mass_for_qualification(
                *second, adapter, idle_after) &&
                second->committed_epoch() == idle_epoch &&
                second->committed_tick() == idle_tick &&
                exactly_same(idle_before, idle_after),
            "one Session mutated its idle sibling");
    require(first->run_to_terminal() && second->run_to_terminal(),
            "isolation Sessions did not complete");
    CommittedRigidMassProbe first_final;
    CommittedRigidMassProbe second_final;
    require(ref_yyz::read_committed_rigid_mass_for_qualification(
                *first, adapter, first_final) &&
                ref_yyz::read_committed_rigid_mass_for_qualification(
                    *second, adapter, second_final) &&
                exactly_same(first_final, second_final),
            "isolated Sessions produced different final states");
}

struct LongRunCapture {
    SessionBundle bundle;
    kernel::RunDriveOutcome drive;
    CommittedRigidMassProbe state;
    std::vector<CommittedHistorySampleProbe> history;
    MissionResultProbe mission;
    kernel::RunOutcome outcome;
};

[[nodiscard]] LongRunCapture run_long(
    const std::shared_ptr<const Image>& image, std::string run_id) {
    auto bundle = initialize_session(image, std::move(run_id));
    const auto drive = bundle.session->run_to_terminal();
    const auto state = committed_state(bundle);
    const auto history = committed_history(bundle);
    const auto mission =
        drive.status == kernel::RunDriveStatus::Completed
            ? mission_result(bundle)
            : MissionResultProbe{};
    require(bundle.session->run_outcome() != nullptr,
            "long run outcome is missing");
    const auto outcome = *bundle.session->run_outcome();
    return {std::move(bundle), drive, state, history, mission,
            outcome};
}

void verify_long_target_run() {
    const auto image = target_image(3000);
    auto first = run_long(image, "run:00a-long-first");
    auto second = run_long(image, "run:00a-long-second");
    require(first.drive.status == second.drive.status &&
                first.drive.result.error == second.drive.result.error &&
                first.drive.result.image_handle ==
                    second.drive.result.image_handle &&
                first.drive.result.detail == second.drive.result.detail &&
                first.bundle.session->state() ==
                    second.bundle.session->state() &&
                first.bundle.session->committed_tick() ==
                    second.bundle.session->committed_tick() &&
                first.bundle.session->committed_epoch() ==
                    second.bundle.session->committed_epoch() &&
                exactly_same(first.state, second.state) &&
                exactly_same(first.history, second.history) &&
                exactly_same(first.mission, second.mission) &&
                exactly_same(first.outcome, second.outcome),
            "3000-tick target result was not deterministic");

    if (first.drive.status == kernel::RunDriveStatus::Completed) {
        require(first.bundle.session->state() ==
                    kernel::SessionState::Completed &&
                    first.outcome.final_tick == 3000 &&
                    first.outcome.terminal_branch_committed &&
                    first.outcome.mission_result_available &&
                    first.mission.present && !first.mission.completed &&
                    first.mission.final_tick == 3000 &&
                    first.mission.reason_code == "remaining-mass-floor" &&
                    first.mission.priority == 300 &&
                    first.mission.evaluated_sample_count == 3U &&
                    std::abs(
                        first.mission.remaining_mass_kilograms - 85.0) <=
                        1.0e-9,
                "completed 3000-tick target result changed");
        std::cout <<
            "target_conformance science_verdict_pending long_run=completed"
            " terminal_tick=" << first.mission.final_tick <<
            " mission_status=aborted reason=" <<
            first.mission.reason_code << " remaining_mass_kg=" <<
            first.mission.remaining_mass_kilograms << '\n';
    } else {
        require(first.drive.status == kernel::RunDriveStatus::Failed &&
                    first.bundle.session->state() ==
                        kernel::SessionState::Failed &&
                    first.outcome.final_status ==
                        kernel::RunFinalStatus::Failed &&
                    first.bundle.session->committed_tick() < 3000,
                "long-run failure was not a frozen numerical failure");
        std::cout <<
            "target_conformance science_verdict_pending long_run="
            "numerical_failure committed_tick=" <<
            first.bundle.session->committed_tick() << " error=" <<
            kernel::to_string(first.drive.result.error) << " detail=" <<
            first.drive.result.detail << '\n';
    }
}

void run() {
    verify_navigation_oracle();
    const auto short_image = target_image(kShortTerminalTick);
    verify_static_target_contract(short_image);
    verify_observation_image_validation(short_image);
    verify_short_target_run(short_image);
    verify_two_session_isolation(short_image);
    verify_long_target_run();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr <<
            "usage: gnc_kernel_yyz_target_rate_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout <<
            "R3 YYZ 00A target-rate conformance: PASS "
            "(science_verdict_pending)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr <<
            "R3 YYZ 00A target-rate conformance: FAIL: " <<
            error.what() << '\n';
        return 1;
    }
}
