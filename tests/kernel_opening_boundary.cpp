#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
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
using gnc::tests::ref_yyz::FailurePhase;
using gnc::tests::ref_yyz::OpeningBoundaryProbe;

double maximum_observed_absolute_difference = 0.0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] bool near(double actual, double expected,
                        double tolerance = 2.0e-12) noexcept {
    const auto difference = std::abs(actual - expected);
    maximum_observed_absolute_difference =
        (std::max)(maximum_observed_absolute_difference, difference);
    return difference <=
           tolerance * (std::max)(1.0, std::abs(expected));
}

template <std::size_t Size>
[[nodiscard]] bool near_array(const std::array<double, Size>& actual,
                              const std::array<double, Size>& expected,
                              double tolerance = 2.0e-12) noexcept {
    for (std::size_t index = 0U; index < Size; ++index) {
        if (!near(actual[index], expected[index], tolerance)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage> build_image() {
    const auto compiled = gnc::tests::ref_yyz::compile_complete_image();
    require(compiled.succeeded(), "REF-YYZ Image compilation failed");
    return std::make_shared<const ExecutionPlanImage>(*compiled.value);
}

[[nodiscard]] const gnc::contracts::PlanImageEntry* entry_for(
    const ExecutionPlanImage& image,
    std::uint32_t callsite_handle) noexcept {
    const auto callsite = std::find_if(
        image.callsites().begin(), image.callsites().end(),
        [callsite_handle](const auto& value) {
            return value.handle == callsite_handle;
        });
    if (callsite == image.callsites().end()) return nullptr;
    const auto entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [callsite](const auto& value) {
            return value.handle == callsite->entry_handle;
        });
    return entry == image.entries().end() ? nullptr : &*entry;
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
        if (lhs[index].slot_handle != rhs[index].slot_handle ||
            lhs[index].codec_entry_handle != rhs[index].codec_entry_handle ||
            lhs[index].present != rhs[index].present ||
            lhs[index].generation != rhs[index].generation ||
            lhs[index].sequence != rhs[index].sequence ||
            lhs[index].sample_tick != rhs[index].sample_tick ||
            lhs[index].sample_time_seconds !=
                rhs[index].sample_time_seconds ||
            lhs[index].interval_start_seconds !=
                rhs[index].interval_start_seconds ||
            lhs[index].interval_end_seconds !=
                rhs[index].interval_end_seconds ||
            lhs[index].quality != rhs[index].quality) {
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

void require_opening_values(const OpeningBoundaryProbe& probe) {
    require(near_array(probe.observation_position,
                       std::array<double, 3U>{0.0, 0.0, 1000.0}) &&
                near_array(probe.observation_velocity,
                           std::array<double, 3U>{110.0, 0.0, 0.0}) &&
                near_array(probe.observation_attitude_wxyz,
                           std::array<double, 4U>{1.0, 0.0, 0.0, 0.0}) &&
                near_array(probe.observation_angular_rate,
                           std::array<double, 3U>{0.0, 0.0, 0.0}),
            "opening rigid observation differs from REF-YYZ oracle");
    require(near(probe.mass_kilograms, 100.0) &&
                near_array(probe.center_of_mass,
                           std::array<double, 3U>{0.2, 0.0, 0.0}) &&
                near_array(probe.inertia,
                           std::array<double, 9U>{
                               10.0, 0.0, 0.0, 0.0, 20.0, 0.0,
                               0.0, 0.0, 30.0}),
            "opening mass projection differs from REF-YYZ fixture");
    require(near(probe.guidance_altitude_error, 0.0) &&
                near(probe.guidance_raw_command, 0.0) &&
                near(probe.guidance_command, 0.0) &&
                near(probe.guidance_limit, 0.04) &&
                !probe.guidance_saturated,
            "opening guidance differs from REF-YYZ oracle");
    require(near(probe.controller_pitch_error, 0.0) &&
                near(probe.controller_raw_moment, 0.0) &&
                near(probe.controller_moment, 0.0) &&
                near(probe.controller_limit, 25.0) &&
                !probe.controller_saturated,
            "opening controller differs from REF-YYZ oracle");
    require(near_array(probe.actuator_moment,
                       std::array<double, 3U>{0.0, 0.0, 0.0}),
            "opening actuator differs from REF-YYZ oracle");
    require(near_array(probe.propulsion_force,
                       std::array<double, 3U>{100.0, 0.0, 0.0}) &&
                near_array(probe.propulsion_application_from_com,
                           std::array<double, 3U>{0.0, 0.2, 0.0}) &&
                near_array(probe.propulsion_intrinsic_moment,
                           std::array<double, 3U>{0.0, 0.0, 20.0}) &&
                near(probe.mass_flow_rate, 0.5),
            "opening propulsion differs from REF-YYZ fixture");
    require(near_array(probe.gravity,
                       std::array<double, 3U>{0.0, 0.0, -9.80665}) &&
                near_array(probe.wind,
                           std::array<double, 3U>{10.0, 0.0, 0.0}) &&
                near(probe.density, 1.225) &&
                near(probe.speed_of_sound, 340.0),
            "opening environment differs from REF-YYZ oracle");
    require(near(probe.airspeed, 100.0) && near(probe.alpha, 0.0) &&
                near(probe.beta, 0.0) &&
                near(probe.dynamic_pressure, 6125.0) &&
                near(probe.mach, 100.0 / 340.0),
            "opening air data differs from REF-YYZ oracle");
    require(near_array(
                probe.aerodynamic_coefficients,
                std::array<double, 6U>{
                    0.031764705882352941, 0.0, 0.0, 0.0,
                    -0.044117647058823529, 0.0}),
            "opening aerodynamic lookup differs from REF-YYZ oracle");
    require(probe.closure_contribution_count == 2U &&
                near_array(probe.held_force,
                           std::array<double, 3U>{
                               -94.558823529411765, 0.0, 0.0}) &&
                near_array(probe.held_moment,
                           std::array<double, 3U>{0.0, 0.0, 0.0},
                           1.0e-10) &&
                probe.controlled_preparation_written &&
                probe.held_form_written,
            "opening controlled preparation or held form differs from oracle");
    require(probe.contexts.size() == 7U &&
                std::all_of(probe.contexts.begin(), probe.contexts.end(),
                            [](const auto& context) {
                                return context.tick == 0 &&
                                       near(context.sample_seconds, 0.0) &&
                                       near(context.interval_start_seconds,
                                            0.0) &&
                                       near(context.interval_end_seconds,
                                            0.1) &&
                                       context.configuration_revision == 11 &&
                                       context.quality_valid;
                            }),
            "opening callsite contexts are inconsistent");
}

void verify_success(const std::shared_ptr<const ExecutionPlanImage>& image,
                    bool reverse_registration,
                    std::vector<std::uint32_t>& execution_order) {
    AdapterOptions options;
    options.reverse_invocation_registration = reverse_registration;
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation) &&
                static_cast<bool>(creation.session->initialize()),
            "opening-boundary Session initialization failed");
    const auto before_blocks = creation.session->state_blocks();
    const auto before_outputs = creation.session->committed_outputs();
    const auto before_epoch = creation.session->committed_epoch();
    const auto before_tick = creation.session->committed_tick();
    const auto executed = creation.session->execute_opening_boundary();
    if (!executed) {
        const auto slot = std::find_if(
            image->slots().begin(), image->slots().end(),
            [&executed](const auto& value) {
                return value.handle == executed.image_handle;
            });
        throw std::runtime_error(
            std::string("opening boundary failed: ") +
            std::string(gnc::kernel::to_string(executed.error)) + " / " +
            std::string(executed.detail) + " / handle=" +
            std::to_string(executed.image_handle) + " / calls=" +
            std::to_string(adapter.opening_boundary->call_order.size()) +
            " / contract=" +
            (slot == image->slots().end() ? std::string("unknown")
                                          : slot->contract_id));
    }
    const auto& summary = creation.session->last_boundary_summary();
    require(summary.executed_callsite_handles.size() == 7U &&
                summary.skipped_callsite_handles.size() == 1U &&
                summary.output_write_count == 9U &&
                summary.executed_callsite_handles ==
                    adapter.opening_boundary->call_order,
            "opening boundary executed the wrong callsite set");
    require(entry_for(*image, summary.executed_callsite_handles[0U])->kind ==
                    gnc::contracts::PlanImageEntryKind::PublishProjection &&
                entry_for(*image,
                          summary.executed_callsite_handles[1U])->kind ==
                    gnc::contracts::PlanImageEntryKind::PublishProjection &&
                std::all_of(
                    summary.executed_callsite_handles.begin() + 2,
                    summary.executed_callsite_handles.end(),
                    [&image](auto handle) {
                        const auto* entry = entry_for(*image, handle);
                        return entry != nullptr &&
                               entry->kind == gnc::contracts::
                                                  PlanImageEntryKind::
                                                      BoundaryEvaluation;
                    }),
            "opening projections and boundary calls are misordered");
    const auto outputs = creation.session->committed_outputs();
    const auto expected_held_count = static_cast<std::size_t>(std::count_if(
        image->slots().begin(), image->slots().end(), [](const auto& slot) {
            return slot.storage_class ==
                       gnc::contracts::SlotStorageClass::IntegrationHeld &&
                   slot.hold_policy ==
                       gnc::contracts::SlotHoldPolicy::HoldInterval;
        }));
    const bool held_committed =
        expected_held_count > 0U &&
        before_outputs.size() == expected_held_count &&
        std::all_of(before_outputs.begin(), before_outputs.end(),
                    [](const auto& output) { return !output.present; }) &&
        outputs.size() == expected_held_count &&
        std::all_of(
            outputs.begin(), outputs.end(), [&](const auto& output) {
                const auto expected = std::find_if(
                    image->slots().begin(), image->slots().end(),
                    [&output](const auto& slot) {
                        return slot.handle == output.slot_handle &&
                               slot.storage_class ==
                                   gnc::contracts::SlotStorageClass::
                                       IntegrationHeld &&
                               slot.hold_policy ==
                                   gnc::contracts::SlotHoldPolicy::
                                       HoldInterval;
                    });
                return expected != image->slots().end() &&
                       output.codec_entry_handle ==
                           expected->codec_entry_handle &&
                       output.present &&
                       output.generation == summary.generation &&
                       output.sample_tick == before_tick &&
                       near(output.sample_time_seconds, 0.0) &&
                       near(output.interval_start_seconds, 0.0) &&
                       near(output.interval_end_seconds, 0.1) &&
                       output.quality ==
                           gnc::contracts::DataQuality::Valid;
            });
    if (!held_committed) {
        throw std::runtime_error(
            "held interval output metadata mismatch: before=" +
            std::to_string(before_outputs.size()) + ", after=" +
            std::to_string(outputs.size()) + ", expected=" +
            std::to_string(expected_held_count) +
            ", actual=" +
            std::to_string(outputs.empty() ? 0U
                                           : outputs.front().slot_handle) +
            ", present=" +
            std::to_string(outputs.empty() || !outputs.front().present ? 0
                                                                       : 1) +
            ", generation=" +
            std::to_string(outputs.empty() ? 0U
                                           : outputs.front().generation) +
            ", tick=" +
            std::to_string(outputs.empty() ? -1
                                           : outputs.front().sample_tick) +
            ", interval_end=" +
            std::to_string(outputs.empty()
                               ? -1.0
                               : outputs.front().interval_end_seconds));
    }
    require(!adapter.opening_boundary->terminal_evaluator_called &&
                creation.session->committed_epoch() == before_epoch &&
                creation.session->committed_tick() == before_tick &&
                same_state_blocks(creation.session->state_blocks(),
                                  before_blocks) &&
                !creation.session->frame_open() &&
                all_frame_slots_absent(*creation.session),
            "opening boundary changed committed state or retained frame data");
    const auto stale =
        gnc::tests::ref_yyz::read_captured_stale_input(adapter);
    require(!stale && stale.error == SessionError::StaleFrameView,
            "closed CycleFrame did not invalidate captured input view");
    require_opening_values(*adapter.opening_boundary);
    execution_order = summary.executed_callsite_handles;
}

template <typename Configure>
void verify_failure(const std::shared_ptr<const ExecutionPlanImage>& image,
                    Configure configure, SessionError expected,
                    std::string_view message) {
    AdapterOptions options;
    configure(options);
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation) &&
                static_cast<bool>(creation.session->initialize()),
            "failure-case Session initialization failed");
    const auto before = creation.session->state_blocks();
    const auto before_outputs = creation.session->committed_outputs();
    const auto epoch = creation.session->committed_epoch();
    const auto tick = creation.session->committed_tick();
    const auto result = creation.session->execute_opening_boundary();
    require(!result && result.error == expected &&
                !creation.session->frame_open() &&
                same_state_blocks(creation.session->state_blocks(), before) &&
                same_committed_outputs(
                    creation.session->committed_outputs(), before_outputs) &&
                creation.session->committed_epoch() == epoch &&
                creation.session->committed_tick() == tick &&
                all_frame_slots_absent(*creation.session),
            message);
}

void verify_display_name_independence(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    const std::vector<std::uint32_t>& expected_order) {
    auto data = image->data();
    data.plan_id = "renamed.plan";
    data.mission_id = "renamed.mission";
    for (auto& callsite : data.callsites) {
        callsite.plan_element_id = "renamed.callsite";
        callsite.callsite_id = "renamed";
        callsite.region_id = "renamed.region";
    }
    for (auto& region : data.regions) {
        region.plan_element_id = "renamed.region.element";
        region.region_id = "renamed";
        region.phase = "renamed.phase";
    }
    for (auto& node : data.dag_nodes) {
        node.plan_element_id = "renamed.node";
        node.node_id = "renamed";
        node.phase = "renamed.phase";
    }
    auto renamed = std::make_shared<const ExecutionPlanImage>(
        ExecutionPlanImage::freeze(std::move(data)));
    std::vector<std::uint32_t> actual_order;
    verify_success(renamed, false, actual_order);
    require(actual_order == expected_order,
            "display-name changes affected numeric execution order");
}

void verify_shared_image_provider_isolation(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto first = gnc::kernel::create_session(image, adapter.provider);
    auto second = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(first) && static_cast<bool>(second) &&
                static_cast<bool>(first.session->initialize()) &&
                static_cast<bool>(second.session->initialize()),
            "two Sessions could not share immutable Image/provider");
    const auto live_after_initialization =
        adapter.trace->live_object_count();
    const auto second_state = second.session->state_blocks();
    const auto second_outputs = second.session->committed_outputs();
    double first_candidate_mass = 0.0;
    double second_candidate_mass = 0.0;
    require(static_cast<bool>(
                gnc::tests::ref_yyz::read_mass_candidate_for_qualification(
                    *first.session, adapter, first_candidate_mass)) &&
                static_cast<bool>(
                    gnc::tests::ref_yyz::read_mass_candidate_for_qualification(
                        *second.session, adapter,
                        second_candidate_mass)) &&
                near(first_candidate_mass, 100.0) &&
                near(second_candidate_mass, 100.0) &&
                static_cast<bool>(gnc::tests::ref_yyz::
                                      replace_mass_candidate_for_qualification(
                                          *first.session, adapter, 91.25)) &&
                static_cast<bool>(
                    gnc::tests::ref_yyz::read_mass_candidate_for_qualification(
                        *first.session, adapter, first_candidate_mass)) &&
                static_cast<bool>(
                    gnc::tests::ref_yyz::read_mass_candidate_for_qualification(
                        *second.session, adapter,
                        second_candidate_mass)) &&
                near(first_candidate_mass, 91.25) &&
                near(second_candidate_mass, 100.0),
            "qualification candidate mutation crossed Session ownership");
    const auto first_boundary = first.session->execute_opening_boundary();
    const auto second_outputs_after_first =
        second.session->committed_outputs();
    require(static_cast<bool>(first_boundary) &&
                first.session->committed_epoch() == 0U &&
                first.session->committed_tick() == 0 &&
                near(adapter.opening_boundary->mass_kilograms, 100.0) &&
                second.session->committed_epoch() == 0U &&
                second.session->committed_tick() == 0 &&
                !second.session->frame_open() &&
                same_state_blocks(second.session->state_blocks(),
                                  second_state) &&
                same_committed_outputs(second_outputs_after_first,
                                       second_outputs) &&
                !second_outputs.empty() &&
                std::all_of(
                    second_outputs_after_first.begin(),
                    second_outputs_after_first.end(),
                    [](const auto& output) { return !output.present; }) &&
                adapter.trace->live_object_count() ==
                    live_after_initialization + second_outputs.size(),
            "one Session boundary leaked mutable state into another Session");
    first.session.reset();
    require(adapter.trace->live_object_count() * 2U ==
                live_after_initialization,
            "destroying one Session changed the other's object lifetime");
    require(static_cast<bool>(
                gnc::tests::ref_yyz::read_mass_candidate_for_qualification(
                    *second.session, adapter, second_candidate_mass)) &&
                near(second_candidate_mass, 100.0),
            "destroying one Session changed the other's candidate value");
    const auto second_boundary = second.session->execute_opening_boundary();
    const auto second_outputs_after_boundary =
        second.session->committed_outputs();
    require(static_cast<bool>(second_boundary) &&
                std::all_of(
                    second_outputs_after_boundary.begin(),
                    second_outputs_after_boundary.end(),
                    [](const auto& output) { return output.present; }) &&
                same_state_blocks(second.session->state_blocks(),
                                  second_state),
            "destroying one Session damaged the remaining Session");
    second.session.reset();
    require(adapter.trace->live_object_count() == 0U,
            "shared Image/provider Sessions leaked owned objects");
}

void run() {
    const auto image = build_image();
    std::vector<std::uint32_t> normal_order;
    verify_success(image, false, normal_order);
    std::vector<std::uint32_t> reverse_order;
    verify_success(image, true, reverse_order);
    require(reverse_order == normal_order,
            "provider registration order affected execution order");
    verify_display_name_independence(image, normal_order);
    verify_shared_image_provider_isolation(image);
    verify_failure(
        image,
        [](auto& options) {
            options.wrong_writer_token_boundary_ordinal = 0U;
        },
        SessionError::WriterAuthorizationFailure,
        "wrong writer token did not close the frame and preserve stores");
    verify_failure(
        image,
        [](auto& options) { options.omit_output_boundary_ordinal = 0U; },
        SessionError::FrameSlotAbsent,
        "absent input did not close the frame and preserve stores");
    verify_failure(
        image,
        [](auto& options) {
            options.failure = {FailurePhase::Boundary, 4U};
        },
        SessionError::InvocationFailed,
        "mid-boundary failure did not close the frame and preserve stores");
    verify_failure(
        image,
        [](auto& options) {
            options.fail_after_output_boundary_ordinal = 6U;
        },
        SessionError::InvocationFailed,
        "post-held-write failure published a committed output");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_opening_boundary_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 deterministic opening boundary: PASS"
                  << " (max_abs_difference=" << std::setprecision(17)
                  << maximum_observed_absolute_difference << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 deterministic opening boundary: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
