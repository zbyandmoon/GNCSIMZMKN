#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"
#include "support/session_qualification_access.hpp"

#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace ref_yyz = gnc::tests::ref_yyz;
namespace yyz = gnc::packages::yyz;

using Image = contracts::ExecutionPlanImage;
using Package = gnc::model_sdk::StaticPackageDescriptor;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool near(double lhs, double rhs,
                        double tolerance = 2.0e-12) noexcept {
    return std::abs(lhs - rhs) <=
           tolerance * (std::max)(1.0, std::abs(rhs));
}

[[nodiscard]] bool has_diagnostic(
    const std::vector<compiler::CompleteDiagnostic>& diagnostics,
    compiler::CompleteDiagnosticCode code) {
    return std::any_of(
        diagnostics.begin(), diagnostics.end(),
        [code](const auto& diagnostic) {
            return diagnostic.code == code;
        });
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

[[nodiscard]] gnc::model_sdk::StaticModelDescriptor& package_model(
    Package& package, std::string_view model_id) {
    const auto found = std::find_if(
        package.models.begin(), package.models.end(),
        [model_id](const auto& model) {
            return model.definition.model_id == model_id;
        });
    require(found != package.models.end(),
            "qualification package model is missing");
    require(found->runtime_component.has_value(),
            "qualification package model has no RuntimeComponent");
    return *found;
}

[[nodiscard]] std::shared_ptr<const Image> link_package(
    const Package& package) {
    const auto source =
        ref_yyz::make_multirate_held_output_qualification_source(
            package);
    const auto implementation =
        yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release", package);
    const auto linked =
        compiler::compile_and_link_complete_execution_plan(
            source, {package}, {implementation});
    require(linked.succeeded(),
            std::string("multi-rate Image compilation failed: ") +
                diagnostic_text(linked));
    return std::make_shared<const Image>(*linked.value);
}

[[nodiscard]] std::shared_ptr<const Image> qualification_image() {
    const auto linked = ref_yyz::
        compile_multirate_held_output_qualification_image();
    require(linked.succeeded(),
            std::string("package-owned qualification Image failed: ") +
                diagnostic_text(linked));
    return std::make_shared<const Image>(*linked.value);
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

[[nodiscard]] const contracts::PlanImageSlot& image_slot(
    const Image& image, std::uint32_t handle) {
    const auto found = std::find_if(
        image.slots().begin(), image.slots().end(),
        [handle](const auto& slot) { return slot.handle == handle; });
    require(found != image.slots().end(), "Image slot is missing");
    return *found;
}

[[nodiscard]] const contracts::PlanImageWriterToken& image_writer(
    const Image& image, std::uint32_t handle) {
    const auto found = std::find_if(
        image.writer_tokens().begin(), image.writer_tokens().end(),
        [handle](const auto& writer) { return writer.handle == handle; });
    require(found != image.writer_tokens().end(),
            "Image writer token is missing");
    return *found;
}

void verify_compiler_contract() {
    constexpr auto profile =
        yyz::YyzRuntimeScheduleProfile::
            MultirateHeldOutputQualification;
    const auto package = yyz::describe_yyz_rigid_step_package(profile);
    const auto source =
        ref_yyz::make_multirate_held_output_qualification_source(
            package);
    const auto compiled =
        compiler::compile_complete_execution_plan(source, {package});
    require(compiled.succeeded(),
            std::string("HeldLatest plan compilation failed: ") +
                diagnostic_text(compiled));
    require(compiled.value->plan.plan_id ==
                    "plan.qualification.yyz.multirate-held-output" &&
                compiled.value->plan.mission_id ==
                    "mission.qualification.yyz.multirate-held-output@1" &&
                compiled.value->plan.held_outputs.size() == 1U,
            "qualification identity or HeldLatest plan cardinality changed");

    const auto& held_plan = compiled.value->plan.held_outputs.front();
    const auto committed_slot = std::find_if(
        compiled.value->plan.slots.begin(),
        compiled.value->plan.slots.end(),
        [&held_plan](const auto& slot) {
            return slot.slot_id == held_plan.committed_slot_id;
        });
    const auto held_proof = std::find_if(
        compiled.value->proofs.records.begin(),
        compiled.value->proofs.records.end(),
        [&held_plan](const auto& proof) {
            return proof.proof_id ==
                   "proof/held-output/" + held_plan.binding_id;
        });
    const auto premise = [&held_proof](std::string_view value) {
        return std::find(held_proof->premises.begin(),
                         held_proof->premises.end(), value) !=
               held_proof->premises.end();
    };
    require(committed_slot != compiled.value->plan.slots.end() &&
                committed_slot->kind ==
                    compiler::CompleteSlotKind::CommittedOutputValue &&
                committed_slot->storage_class ==
                    contracts::SlotStorageClass::CommittedOutput &&
                committed_slot->hold_policy ==
                    contracts::SlotHoldPolicy::HeldLatest &&
                held_plan.max_age_steps == 1U &&
                held_plan.consumer_callsite_ids.size() == 1U &&
                held_proof != compiled.value->proofs.records.end() &&
                premise("max-age-steps=1") &&
                premise("consumer-callsite-count=1"),
            "HeldLatest plan, age, or proof facts are incomplete");

    const auto image = qualification_image();
    const auto& guidance = image_component(
        *image, yyz::kAltitudePitchGuidanceModelIdentity);
    const auto& controller = image_component(
        *image, yyz::kPitchMomentControllerModelIdentity);
    require(guidance.step_interval == 2U && guidance.offset == 0U &&
                guidance.output_hold == "ZeroOrderHold" &&
                controller.step_interval == 1U &&
                controller.offset == 0U &&
                controller.max_input_age_steps == 1U &&
                image->held_outputs().size() == 1U,
            "linked multi-rate schedule facts changed");
    const auto& held = image->held_outputs().front();
    const auto& source_slot = image_slot(*image, held.source_slot_handle);
    const auto& held_slot = image_slot(*image, held.committed_slot_handle);
    const auto& writer = image_writer(
        *image, held_slot.writer_token_handle);
    require(source_slot.kind == contracts::PlanImageSlotKind::PortValue &&
                source_slot.storage_class ==
                    contracts::SlotStorageClass::CycleFrame &&
                source_slot.hold_policy ==
                    contracts::SlotHoldPolicy::CurrentBoundary &&
                held_slot.kind ==
                    contracts::PlanImageSlotKind::CommittedOutputValue &&
                held_slot.storage_class ==
                    contracts::SlotStorageClass::CommittedOutput &&
                held_slot.hold_policy ==
                    contracts::SlotHoldPolicy::HeldLatest &&
                held_slot.layout_id == source_slot.layout_id &&
                held_slot.codec_entry_handle ==
                    source_slot.codec_entry_handle &&
                writer.owner_kind == contracts::
                    PlanImageWriterOwnerKind::CommittedOutputCoordinator &&
                writer.owner_handle == held.producer_callsite_handle &&
                held_slot.reader_handles ==
                    held.consumer_callsite_handles &&
                held.max_age_steps == 1U,
            "Image did not freeze exact HeldLatest storage authority");

    auto current_cycle_package =
        yyz::describe_yyz_rigid_step_package();
    package_model(current_cycle_package,
                  yyz::kAltitudePitchGuidanceModelIdentity)
        .runtime_component->schedule.step_interval = 2U;
    const auto invalid_source =
        ref_yyz::make_complete_source(current_cycle_package);
    const auto invalid = compiler::compile_complete_execution_plan(
        invalid_source, {current_cycle_package});
    require(!invalid.succeeded() &&
                has_diagnostic(invalid.diagnostics,
                               compiler::CompleteDiagnosticCode::
                                   TemporalMismatch),
            "cross-rate CurrentCycle edge was accepted");

    auto wider_age_package = package;
    package_model(wider_age_package,
                  yyz::kPitchMomentControllerModelIdentity)
        .runtime_component->schedule.max_input_age_steps = 2U;
    const auto wider_age_image = link_package(wider_age_package);
    require(wider_age_image->fingerprint() != image->fingerprint() &&
                wider_age_image->descriptor_semantic_hash() !=
                    image->descriptor_semantic_hash(),
            "HeldLatest max age did not affect descriptor/Image identity");

    const auto baseline = ref_yyz::compile_complete_image();
    require(baseline.succeeded() &&
                baseline.value->held_outputs().empty() &&
                baseline.value->fingerprint() != image->fingerprint(),
            "interval-1 REF-YYZ gained a HeldLatest store or reused its identity");
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

[[nodiscard]] kernel::ResetRequest reset_request(
    const Image& image, std::string run_id) {
    return {kernel::RunId(std::move(run_id)),
            kernel::exact_run_binding(image)};
}

[[nodiscard]] kernel::RestoreRequest restore_request(
    const Image& image, std::string run_id,
    std::shared_ptr<const kernel::SessionCheckpoint> checkpoint) {
    return {kernel::RunId(std::move(run_id)),
            kernel::exact_run_binding(image), std::move(checkpoint)};
}

[[nodiscard]] kernel::CancellationRequest cancellation_request(
    std::string request_id, std::string run_id) {
    return {kernel::CancellationRequestId(std::move(request_id)),
            kernel::RunId(std::move(run_id))};
}

[[nodiscard]] SessionBundle create_session(
    const std::shared_ptr<const Image>& image,
    ref_yyz::AdapterOptions options = {}) {
    auto adapter = ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "Session creation failed");
    return {std::move(adapter), std::move(creation.session)};
}

[[nodiscard]] SessionBundle initialize_session(
    const std::shared_ptr<const Image>& image, std::string run_id,
    ref_yyz::AdapterOptions options = {}) {
    auto bundle = create_session(image, options);
    const auto initialized = bundle.session->initialize(
        initialization_request(*image, std::move(run_id)));
    require(static_cast<bool>(initialized),
            std::string("Session initialization failed: ") +
                std::string(kernel::to_string(
                    initialized.result.error)) +
                " / " + std::string(initialized.result.detail));
    return bundle;
}

[[nodiscard]] kernel::SessionCommittedOutputInfo held_info(
    const kernel::Session& session, std::uint32_t held_slot_handle) {
    const auto outputs = session.committed_outputs();
    const auto found = std::find_if(
        outputs.begin(), outputs.end(),
        [held_slot_handle](const auto& output) {
            return output.slot_handle == held_slot_handle;
        });
    require(found != outputs.end() && found->present &&
                !found->terminal_result,
            "committed HeldLatest sample is missing");
    return *found;
}

[[nodiscard]] yyz::AltitudePitchGuidanceOutput held_guidance(
    const kernel::Session& session, std::uint32_t held_slot_handle) {
    kernel::SessionObjectIdentityView view;
    const auto read = kernel::qualification::SessionAccess::
        read_committed_output(session, held_slot_handle, view);
    require(read && view &&
                view.role == kernel::SessionObjectRole::
                                 CommittedOutputValue,
            "typed HeldLatest qualification read failed");
    return *static_cast<const yyz::AltitudePitchGuidanceOutput*>(
        view.address);
}

[[nodiscard]] bool same_held_info(
    const kernel::SessionCommittedOutputInfo& lhs,
    const kernel::SessionCommittedOutputInfo& rhs) noexcept {
    return lhs.slot_handle == rhs.slot_handle &&
           lhs.codec_entry_handle == rhs.codec_entry_handle &&
           lhs.present == rhs.present &&
           lhs.generation == rhs.generation &&
           lhs.sequence == rhs.sequence &&
           lhs.sample_tick == rhs.sample_tick &&
           lhs.sample_time_seconds == rhs.sample_time_seconds &&
           lhs.interval_start_seconds == rhs.interval_start_seconds &&
           lhs.interval_end_seconds == rhs.interval_end_seconds &&
           lhs.quality == rhs.quality &&
           lhs.terminal_result == rhs.terminal_result;
}

[[nodiscard]] bool same_guidance(
    const yyz::AltitudePitchGuidanceOutput& lhs,
    const yyz::AltitudePitchGuidanceOutput& rhs) noexcept {
    return lhs.source_observation.context.sample_time.tick ==
               rhs.source_observation.context.sample_time.tick &&
           lhs.source_observation.context.sample_time.seconds ==
               rhs.source_observation.context.sample_time.seconds &&
           lhs.measured_pitch_radians == rhs.measured_pitch_radians &&
           lhs.measured_pitch_rate_radians_per_second ==
               rhs.measured_pitch_rate_radians_per_second &&
           lhs.altitude_error_meters == rhs.altitude_error_meters &&
           lhs.altitude_feedback_radians ==
               rhs.altitude_feedback_radians &&
           lhs.vertical_speed_feedback_radians ==
               rhs.vertical_speed_feedback_radians &&
           lhs.raw_pitch_command_radians ==
               rhs.raw_pitch_command_radians &&
           lhs.pitch_command_radians == rhs.pitch_command_radians &&
           lhs.saturated == rhs.saturated;
}

[[nodiscard]] std::uint32_t commit_coordination_subject(
    const Image& image) {
    require(image.transactions().size() == 1U &&
                !image.transactions().front().candidates.empty(),
            "qualification transaction candidate is missing");
    const auto candidate_slot = image.transactions()
                                    .front()
                                    .candidates
                                    .front()
                                    .candidate_state_slot_handle;
    const auto state = std::find_if(
        image.state_blocks().begin(), image.state_blocks().end(),
        [candidate_slot](const auto& block) {
            return block.candidate_slot_handle == candidate_slot;
        });
    require(state != image.state_blocks().end(),
            "qualification candidate state block is missing");
    const auto initial = std::find_if(
        image.initial_bindings().begin(),
        image.initial_bindings().end(),
        [&state](const auto& binding) {
            return binding.committed_state_slot_handle ==
                   state->committed_slot_handle;
        });
    require(initial != image.initial_bindings().end(),
            "qualification candidate initial binding is missing");
    return initial->handle;
}

void verify_tick_sequence(
    const std::shared_ptr<const Image>& image) {
    const auto held_slot =
        image->held_outputs().front().committed_slot_handle;
    auto bundle = initialize_session(
        image, "run:multirate-sequence");
    const auto tick_zero = bundle.session->execute_step();
    require(tick_zero &&
                tick_zero.status == kernel::StepStatus::Committed,
            "tick 0 did not commit");
    const auto zero_info = held_info(*bundle.session, held_slot);
    const auto zero_value = held_guidance(*bundle.session, held_slot);
    require(zero_info.sample_tick == 0 &&
                zero_info.quality == contracts::DataQuality::Valid &&
                bundle.adapter.opening_boundary->
                    guidance_output_ticks ==
                    std::vector<std::int64_t>{0} &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples.size() == 1U &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[0].consumer_tick == 0 &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[0].sample_tick == 0 &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[0].sequence ==
                    zero_info.sequence &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[0].sample_seconds ==
                    zero_info.sample_time_seconds &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[0]
                            .interval_start_seconds ==
                    zero_info.interval_start_seconds &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[0]
                            .interval_end_seconds ==
                    zero_info.interval_end_seconds &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[0].quality_valid &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[0].age_steps == 0U &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[0].fresh,
            "tick 0 consumer did not observe a fresh guidance sample");

    const auto tick_one = bundle.session->execute_step();
    require(tick_one &&
                tick_one.status == kernel::StepStatus::Committed,
            std::string("tick 1 did not commit: ") +
                std::string(kernel::to_string(tick_one.result.error)) +
                " / " + std::string(tick_one.result.detail));
    const auto one_info = held_info(*bundle.session, held_slot);
    const auto expected_controller_raw =
        500.0 * (zero_value.pitch_command_radians -
                 zero_value.measured_pitch_radians) -
        80.0 * zero_value.measured_pitch_rate_radians_per_second;
    const auto expected_controller = std::clamp(
        expected_controller_raw, -25.0, 25.0);
    require(same_held_info(zero_info, one_info) &&
                bundle.adapter.opening_boundary->
                    guidance_output_ticks ==
                    std::vector<std::int64_t>{0} &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples.size() == 2U &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[1].consumer_tick == 1 &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[1].sample_tick == 0 &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[1].sequence ==
                    zero_info.sequence &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[1].sample_seconds ==
                    zero_info.sample_time_seconds &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[1]
                            .interval_start_seconds ==
                    zero_info.interval_start_seconds &&
                bundle.adapter.opening_boundary->
                        controller_guidance_samples[1]
                            .interval_end_seconds ==
                    zero_info.interval_end_seconds &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[1].quality_valid &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[1].age_steps == 1U &&
                !bundle.adapter.opening_boundary->
                     controller_guidance_samples[1].fresh &&
                bundle.adapter.opening_boundary->
                    controller_moments.size() == 2U &&
                near(bundle.adapter.opening_boundary->
                         controller_moments[1],
                     expected_controller) &&
                near(bundle.adapter.opening_boundary->
                         controller_moments[0],
                     bundle.adapter.opening_boundary->
                         controller_moments[1]),
            "tick 1 consumer did not observe the committed tick 0 sample");

    const auto tick_two = bundle.session->execute_step();
    require(tick_two &&
                tick_two.status == kernel::StepStatus::Terminated &&
                bundle.session->state() == kernel::SessionState::Completed,
            "tick 2 did not terminate");
    const auto two_info = held_info(*bundle.session, held_slot);
    const auto two_value = held_guidance(*bundle.session, held_slot);
    require(two_info.sample_tick == 2 &&
                two_info.sequence > one_info.sequence &&
                bundle.adapter.opening_boundary->
                    guidance_output_ticks ==
                    std::vector<std::int64_t>({0, 2}) &&
                bundle.adapter.opening_boundary->
                    controller_output_ticks ==
                    std::vector<std::int64_t>({0, 1, 2}) &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples.size() == 3U &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[2].sample_tick == 2 &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[2].age_steps == 0U &&
                bundle.adapter.opening_boundary->
                    controller_guidance_samples[2].fresh &&
                !near(two_value.altitude_error_meters,
                      zero_value.altitude_error_meters, 1.0e-15),
            "tick 2 did not atomically replace the held product sample");
}

void verify_missing_and_expired_fail_closed() {
    constexpr auto profile =
        yyz::YyzRuntimeScheduleProfile::
            MultirateHeldOutputQualification;
    auto missing_package =
        yyz::describe_yyz_rigid_step_package(profile);
    package_model(missing_package,
                  yyz::kAltitudePitchGuidanceModelIdentity)
        .runtime_component->schedule.offset = 1U;
    const auto missing_image = link_package(missing_package);
    auto missing = initialize_session(
        missing_image, "run:held-missing");
    const auto missing_step = missing.session->execute_step();
    require(!missing_step &&
                missing_step.result.error ==
                    kernel::SessionError::HeldOutputMissing &&
                missing_step.primary_diagnostic.has_value() &&
                missing_step.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::HeldOutputMissing &&
                missing_step.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::HeldOutputInjection &&
                missing.session->committed_outputs().empty(),
            "missing initial HeldLatest value did not fail closed");

    auto expired_package =
        yyz::describe_yyz_rigid_step_package(profile);
    package_model(expired_package,
                  yyz::kPitchMomentControllerModelIdentity)
        .runtime_component->schedule.max_input_age_steps = 0U;
    const auto expired_image = link_package(expired_package);
    const auto expired_slot =
        expired_image->held_outputs().front().committed_slot_handle;
    auto expired = initialize_session(
        expired_image, "run:held-expired");
    require(static_cast<bool>(expired.session->execute_step()),
            "expired fixture tick 0 failed");
    const auto before = held_info(*expired.session, expired_slot);
    const auto expired_step = expired.session->execute_step();
    const auto after = held_info(*expired.session, expired_slot);
    require(!expired_step &&
                expired_step.result.error ==
                    kernel::SessionError::HeldOutputExpired &&
                expired_step.primary_diagnostic.has_value() &&
                expired_step.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::HeldOutputExpired &&
                expired_step.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::HeldOutputInjection &&
                after.sample_tick == before.sample_tick &&
                after.sequence == before.sequence,
            "over-age HeldLatest value crossed the transaction boundary");
}

void verify_staging_rollback(
    const std::shared_ptr<const Image>& image) {
    const auto producer =
        image->held_outputs().front().producer_callsite_handle;
    auto discovery = initialize_session(
        image, "run:held-rollback-discovery");
    require(static_cast<bool>(discovery.session->execute_step()),
            "rollback call-order discovery failed");
    const auto found = std::find(
        discovery.adapter.opening_boundary->call_order.begin(),
        discovery.adapter.opening_boundary->call_order.end(), producer);
    require(found !=
                discovery.adapter.opening_boundary->call_order.end(),
            "HeldLatest producer did not execute in discovery step");
    const auto ordinal = static_cast<std::size_t>(std::distance(
        discovery.adapter.opening_boundary->call_order.begin(), found));
    require(ordinal + 1U <
                discovery.adapter.opening_boundary->call_order.size(),
            "HeldLatest producer has no later rollback failure point");

    ref_yyz::AdapterOptions options;
    options.failure = {ref_yyz::FailurePhase::Boundary, ordinal + 1U};
    auto failed = initialize_session(
        image, "run:held-rollback", options);
    const auto step = failed.session->execute_step();
    require(!step &&
                step.result.error == kernel::SessionError::InvocationFailed &&
                failed.adapter.opening_boundary->guidance_output_ticks ==
                    std::vector<std::int64_t>{0} &&
                failed.session->committed_epoch() == 0U &&
                failed.session->committed_tick() == 0 &&
                failed.session->committed_outputs().empty(),
            "later call failure published a staged HeldLatest value");
}

void verify_held_fault_rollback(
    const std::shared_ptr<const Image>& image) {
    using Access = kernel::qualification::SessionAccess;
    using Fault = kernel::qualification::HeldOutputFault;
    const auto slot =
        image->held_outputs().front().committed_slot_handle;

    {
        auto bundle = initialize_session(
            image, "run:held-store-clone-fault");
        Access::fail_next_held_output(*bundle.session,
                                      Fault::StoreClone);
        const auto failed = bundle.session->execute_step();
        require(!failed &&
                    failed.result.error ==
                        kernel::SessionError::HeldOutputCloneFailed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::HeldOutputCommit &&
                    bundle.session->committed_outputs().empty(),
                "HeldLatest store clone fault published a value");
    }

    {
        auto bundle = initialize_session(
            image, "run:held-injection-clone-fault");
        require(static_cast<bool>(bundle.session->execute_step()),
                "injection clone fixture tick 0 failed");
        const auto before = held_info(*bundle.session, slot);
        const auto before_value = held_guidance(*bundle.session, slot);
        Access::fail_next_held_output(*bundle.session,
                                      Fault::InjectionClone);
        const auto failed = bundle.session->execute_step();
        const auto after = held_info(*bundle.session, slot);
        const auto after_value = held_guidance(*bundle.session, slot);
        require(!failed &&
                    failed.result.error ==
                        kernel::SessionError::HeldOutputCloneFailed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::HeldOutputInjection &&
                    same_held_info(before, after) &&
                    same_guidance(before_value, after_value),
                "HeldLatest injection clone fault changed committed storage");
    }

    for (const auto fault : {Fault::Validation, Fault::Precommit}) {
        auto bundle = initialize_session(
            image, fault == Fault::Validation
                       ? "run:held-validation-fault"
                       : "run:held-precommit-fault");
        require(static_cast<bool>(bundle.session->execute_step()) &&
                    static_cast<bool>(bundle.session->execute_step()),
                "HeldLatest precommit fixture did not reach tick 2");
        const auto before = held_info(*bundle.session, slot);
        const auto before_value = held_guidance(*bundle.session, slot);
        Access::fail_next_held_output(*bundle.session, fault);
        const auto failed = bundle.session->execute_step();
        const auto after = held_info(*bundle.session, slot);
        const auto after_value = held_guidance(*bundle.session, slot);
        const auto expected =
            fault == Fault::Validation
                ? kernel::SessionError::HeldOutputValidationFailed
                : kernel::SessionError::TransactionPrecommitFailed;
        require(!failed && failed.result.error == expected &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::HeldOutputCommit &&
                    same_held_info(before, after) &&
                    same_guidance(before_value, after_value),
                "HeldLatest precommit failure replaced the committed sample");
    }
}

void verify_cancellation_publication_boundaries(
    const std::shared_ptr<const Image>& image) {
    const auto slot =
        image->held_outputs().front().committed_slot_handle;
    const auto subject = commit_coordination_subject(*image);

    {
        constexpr std::string_view run_id =
            "run:held-cancel-precommit";
        auto bundle = initialize_session(image, std::string(run_id));
        bundle.adapter.coordination->arm(
            ref_yyz::AdapterCoordinationPoint::FinalPrecommit,
            subject);
        kernel::StepOutcome step;
        std::thread execution(
            [&] { step = bundle.session->execute_step(); });
        const auto reached =
            bundle.adapter.coordination->wait_until_reached();
        if (!reached) {
            bundle.adapter.coordination->release();
            execution.join();
            require(false,
                    "HeldLatest precommit cancellation rendezvous failed");
        }
        const auto accepted = bundle.session->request_cancel(
            cancellation_request("cancel:held-precommit",
                                 std::string(run_id)));
        bundle.adapter.coordination->release();
        execution.join();
        require(accepted &&
                    step.status == kernel::StepStatus::Cancelled &&
                    bundle.session->state() ==
                        kernel::SessionState::Cancelled &&
                    bundle.session->committed_epoch() == 0U &&
                    bundle.session->committed_outputs().empty(),
                "precommit cancellation published HeldLatest storage");
    }

    {
        constexpr std::string_view run_id =
            "run:held-cancel-postcommit";
        auto bundle = initialize_session(image, std::string(run_id));
        bundle.adapter.coordination->arm(
            ref_yyz::AdapterCoordinationPoint::ModelCommit, subject);
        kernel::StepOutcome step;
        std::thread execution(
            [&] { step = bundle.session->execute_step(); });
        const auto reached =
            bundle.adapter.coordination->wait_until_reached();
        if (!reached) {
            bundle.adapter.coordination->release();
            execution.join();
            require(false,
                    "HeldLatest postcommit cancellation rendezvous failed");
        }
        const auto accepted = bundle.session->request_cancel(
            cancellation_request("cancel:held-postcommit",
                                 std::string(run_id)));
        bundle.adapter.coordination->release();
        execution.join();
        const auto committed = held_info(*bundle.session, slot);
        require(accepted &&
                    step.status == kernel::StepStatus::Committed &&
                    bundle.session->state() ==
                        kernel::SessionState::Cancelled &&
                    bundle.session->committed_epoch() == 1U &&
                    committed.sample_tick == 0,
                "postcommit cancellation lost the published HeldLatest sample");
    }
}

void verify_session_isolation(
    const std::shared_ptr<const Image>& image) {
    const auto slot =
        image->held_outputs().front().committed_slot_handle;
    auto adapter = ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto first_creation = kernel::create_session(image, adapter.provider);
    auto second_creation = kernel::create_session(image, adapter.provider);
    require(first_creation && second_creation,
            "shared-provider Session creation failed");
    auto first = std::move(first_creation.session);
    auto second = std::move(second_creation.session);
    require(static_cast<bool>(first->initialize(initialization_request(
                *image, "run:held-isolation-first"))) &&
                static_cast<bool>(second->initialize(initialization_request(
                    *image, "run:held-isolation-second"))),
            "shared-provider Session initialization failed");
    require(static_cast<bool>(first->execute_step()) &&
                second->committed_outputs().empty(),
            "one Session published into an idle sibling");
    require(static_cast<bool>(second->execute_step()),
            "second isolation Session tick 0 failed");
    kernel::SessionObjectIdentityView first_view;
    kernel::SessionObjectIdentityView second_view;
    const auto first_read = kernel::qualification::SessionAccess::
        read_committed_output(*first, slot, first_view);
    const auto second_read = kernel::qualification::SessionAccess::
        read_committed_output(*second, slot, second_view);
    require(first_read && second_read &&
                first_view.address != second_view.address &&
                same_guidance(held_guidance(*first, slot),
                              held_guidance(*second, slot)),
            "HeldLatest storage is shared across Sessions");
}

void verify_reset_and_dispose(
    const std::shared_ptr<const Image>& image) {
    const auto slot =
        image->held_outputs().front().committed_slot_handle;
    {
        auto bundle = initialize_session(
            image, "run:held-reset-success-base");
        require(static_cast<bool>(bundle.session->run_to_terminal()),
                "reset success fixture did not complete");
        require(held_info(*bundle.session, slot).sample_tick == 2,
                "reset success fixture lacks terminal held value");
        const auto reset = bundle.session->reset(
            reset_request(*image, "run:held-reset-success-next"));
        require(reset &&
                    bundle.session->state() ==
                        kernel::SessionState::Initialized &&
                    bundle.session->committed_outputs().empty(),
                "ResetCommit retained HeldLatest or sealed output storage");
        const auto new_tick_zero = bundle.session->execute_step();
        require(new_tick_zero &&
                    new_tick_zero.status == kernel::StepStatus::Committed &&
                    held_info(*bundle.session, slot).sample_tick == 0,
                "reset run did not publish its own fresh tick 0 sample");
        require(static_cast<bool>(bundle.session->run_to_terminal()) &&
                    held_info(*bundle.session, slot).sample_tick == 2,
                "reset run did not rebuild HeldLatest storage");
        require(bundle.adapter.trace->live_object_count() > 0U &&
                    static_cast<bool>(bundle.session->dispose()) &&
                    bundle.session->state() ==
                        kernel::SessionState::Disposed &&
                    bundle.session->committed_outputs().empty() &&
                    bundle.adapter.trace->live_object_count() == 0U,
                "dispose retained HeldLatest typed storage");
    }

    {
        auto bundle = initialize_session(
            image, "run:held-reset-failure-base");
        require(static_cast<bool>(bundle.session->run_to_terminal()),
                "reset failure fixture did not complete");
        const auto before = held_info(*bundle.session, slot);
        const auto before_value = held_guidance(*bundle.session, slot);
        require(bundle.adapter.fail_next_state_copy != nullptr,
                "reset failure seam is missing");
        *bundle.adapter.fail_next_state_copy = true;
        const auto failed = bundle.session->reset(
            reset_request(*image, "run:held-reset-failure-next"));
        const auto after = held_info(*bundle.session, slot);
        const auto after_value = held_guidance(*bundle.session, slot);
        require(!failed &&
                    failed.result.error ==
                        kernel::SessionError::ResetStateFailed &&
                    bundle.session->state() ==
                        kernel::SessionState::Failed &&
                    same_held_info(before, after) &&
                    same_guidance(before_value, after_value),
                "failed reset changed committed HeldLatest storage");
    }
}

[[nodiscard]] std::shared_ptr<const kernel::SessionCheckpoint>
capture_tick_zero_checkpoint(
    const std::shared_ptr<const Image>& image,
    std::string run_id) {
    auto source = initialize_session(image, std::move(run_id));
    require(static_cast<bool>(source.session->execute_step()),
            "checkpoint source tick 0 failed");
    const auto captured = source.session->checkpoint();
    require(captured && captured.checkpoint != nullptr,
            "checkpoint capture failed");
    return captured.checkpoint;
}

void verify_checkpoint_restore(
    const std::shared_ptr<const Image>& image) {
    using Access = kernel::qualification::SessionAccess;
    using CloneFault = kernel::qualification::CheckpointCloneFault;
    using Mutation = kernel::qualification::CheckpointMutation;
    const auto slot =
        image->held_outputs().front().committed_slot_handle;

    auto source = initialize_session(
        image, "run:held-checkpoint-parent");
    require(static_cast<bool>(source.session->execute_step()),
            "checkpoint parent tick 0 failed");
    const auto source_info = held_info(*source.session, slot);
    const auto source_value = held_guidance(*source.session, slot);
    Access::fail_next_checkpoint_clone(*source.session,
                                        CloneFault::HeldOutput);
    const auto clone_failed = source.session->checkpoint();
    require(!clone_failed &&
                clone_failed.result.error ==
                    kernel::SessionError::CheckpointCloneFailed &&
                clone_failed.primary_diagnostic.has_value() &&
                clone_failed.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::CheckpointClone &&
                same_held_info(source_info,
                               held_info(*source.session, slot)) &&
                same_guidance(source_value,
                              held_guidance(*source.session, slot)),
            "checkpoint HeldLatest clone fault changed source storage");
    const auto captured = source.session->checkpoint();
    require(captured && captured.checkpoint != nullptr &&
                captured.identity.committed_epoch == 1U &&
                captured.identity.committed_tick == 1,
            "checkpoint did not recover after held clone fault");
    require(static_cast<bool>(source.session->run_to_terminal()) &&
                static_cast<bool>(source.session->dispose()),
            "checkpoint parent could not continue and dispose");
    source.session.reset();

    auto child_one = create_session(image);
    auto child_two = create_session(image);
    const auto restored_one = child_one.session->restore(
        restore_request(*image, "run:held-checkpoint-child-one",
                        captured.checkpoint));
    const auto restored_two = child_two.session->restore(
        restore_request(*image, "run:held-checkpoint-child-two",
                        captured.checkpoint));
    require(restored_one && restored_two &&
                restored_one.restore_commit && restored_two.restore_commit &&
                restored_one.committed_epoch == 1U &&
                restored_one.committed_tick == 1 &&
                same_held_info(source_info,
                               held_info(*child_one.session, slot)) &&
                same_held_info(source_info,
                               held_info(*child_two.session, slot)) &&
                same_guidance(source_value,
                              held_guidance(*child_one.session, slot)) &&
                same_guidance(source_value,
                              held_guidance(*child_two.session, slot)),
            "RestoreCommit changed the exact HeldLatest samples");
    const auto held_tick_one = child_one.session->execute_step();
    require(held_tick_one &&
                held_tick_one.status == kernel::StepStatus::Committed &&
                child_one.adapter.opening_boundary->
                    controller_guidance_samples.size() == 1U &&
                child_one.adapter.opening_boundary->
                    controller_guidance_samples.front().consumer_tick == 1 &&
                child_one.adapter.opening_boundary->
                    controller_guidance_samples.front().sample_tick == 0 &&
                child_one.adapter.opening_boundary->
                    controller_guidance_samples.front().age_steps == 1U &&
                !child_one.adapter.opening_boundary->
                     controller_guidance_samples.front().fresh &&
                same_held_info(source_info,
                               held_info(*child_two.session, slot)),
            "advancing one restored child changed its idle sibling");
    const auto held_tick_two = child_two.session->execute_step();
    require(held_tick_two &&
                held_tick_two.status == kernel::StepStatus::Committed &&
                child_two.adapter.opening_boundary->
                    controller_guidance_samples.size() == 1U &&
                child_two.adapter.opening_boundary->
                    controller_guidance_samples.front().sample_tick == 0 &&
                child_two.adapter.opening_boundary->
                    controller_guidance_samples.front().age_steps == 1U,
            "second restored child did not reproduce held tick 1");
    const auto terminal_one = child_one.session->execute_step();
    const auto replacement_one = held_info(*child_one.session, slot);
    const auto replacement_value_one =
        held_guidance(*child_one.session, slot);
    require(terminal_one &&
                terminal_one.status == kernel::StepStatus::Terminated &&
                replacement_one.sample_tick == 2 &&
                same_held_info(source_info,
                               held_info(*child_two.session, slot)),
            "first child fresh replacement crossed into second child");
    const auto terminal_two = child_two.session->execute_step();
    const auto replacement_two = held_info(*child_two.session, slot);
    const auto replacement_value_two =
        held_guidance(*child_two.session, slot);
    kernel::SessionObjectIdentityView child_one_view;
    kernel::SessionObjectIdentityView child_two_view;
    const auto child_one_read = Access::read_committed_output(
        *child_one.session, slot, child_one_view);
    const auto child_two_read = Access::read_committed_output(
        *child_two.session, slot, child_two_view);
    require(terminal_two &&
                terminal_two.status == kernel::StepStatus::Terminated &&
                replacement_two.sample_tick == 2 &&
                same_held_info(replacement_one, replacement_two) &&
                same_guidance(replacement_value_one,
                              replacement_value_two) &&
                child_one_read && child_two_read &&
                child_one_view.address != child_two_view.address &&
                child_one.session->run_outcome() != nullptr &&
                child_two.session->run_outcome() != nullptr &&
                child_one.session->run_outcome()->final_status ==
                    kernel::RunFinalStatus::Completed &&
                child_two.session->run_outcome()->final_status ==
                    kernel::RunFinalStatus::Completed,
            "restored child suffixes or HeldLatest stores are not independent");

    for (const auto mutation : {
             Mutation::HeldLayout, Mutation::HeldCodec,
             Mutation::HeldType, Mutation::HeldQuality,
             Mutation::HeldAuthority, Mutation::HeldMissing,
             Mutation::HeldExtra}) {
        auto checkpoint = capture_tick_zero_checkpoint(
            image, "run:held-corruption-parent");
        Access::mutate_checkpoint(checkpoint, mutation);
        auto target = create_session(image);
        const auto failed = target.session->restore(
            restore_request(*image, "run:held-corruption-child",
                            checkpoint));
        require(!failed &&
                    failed.result.error == kernel::SessionError::
                                               RestoreCompatibilityMismatch &&
                    !failed.restore_commit &&
                    target.session->state() ==
                        kernel::SessionState::Failed &&
                    target.session->committed_outputs().empty(),
                "corrupt HeldLatest checkpoint crossed RestoreCommit");
    }

    {
        auto target = create_session(image);
        Access::fail_restore_precommit(*target.session);
        const auto failed = target.session->restore(
            restore_request(*image, "run:held-restore-precommit",
                            captured.checkpoint));
        require(!failed &&
                    failed.result.error ==
                        kernel::SessionError::RestorePrecommitFailed &&
                    !failed.restore_commit &&
                    target.session->state() ==
                        kernel::SessionState::Failed &&
                    target.session->committed_outputs().empty(),
                "restore precommit fault published HeldLatest storage");
    }
}

void verify_checkpoint_cancellation_exclusion(
    const std::shared_ptr<const Image>& image) {
    constexpr std::string_view parent_id =
        "run:held-cancel-parent";
    auto parent = initialize_session(image, std::string(parent_id));
    require(static_cast<bool>(parent.session->execute_step()),
            "cancellation parent tick 0 failed");
    const auto captured = parent.session->checkpoint();
    require(captured && captured.checkpoint != nullptr,
            "cancellation exclusion checkpoint failed");
    const auto accepted_parent = parent.session->request_cancel(
        cancellation_request("cancel:held-parent",
                             std::string(parent_id)));
    const auto cancelled_parent = parent.session->execute_step();
    require(accepted_parent &&
                cancelled_parent.status ==
                    kernel::StepStatus::Cancelled &&
                parent.session->state() ==
                    kernel::SessionState::Cancelled,
            "parent cancellation was not observed after checkpoint");

    constexpr std::string_view child_id =
        "run:held-cancel-child-completes";
    auto child = create_session(image);
    const auto restored = child.session->restore(
        restore_request(*image, std::string(child_id),
                        captured.checkpoint));
    const auto parent_request = child.session->request_cancel(
        cancellation_request("cancel:parent-id-on-child",
                             std::string(parent_id)));
    const auto completed = child.session->run_to_terminal();
    require(restored &&
                !parent_request &&
                parent_request.disposition ==
                    kernel::CancellationDisposition::Rejected &&
                completed &&
                child.session->state() ==
                    kernel::SessionState::Completed &&
                child.session->run_outcome() != nullptr &&
                child.session->run_outcome()->final_status ==
                    kernel::RunFinalStatus::Completed,
            "parent cancellation leaked into restored child run");

    constexpr std::string_view cancelled_child_id =
        "run:held-cancel-child-accepts-own";
    auto cancelled_child = create_session(image);
    require(static_cast<bool>(cancelled_child.session->restore(
                restore_request(*image,
                                std::string(cancelled_child_id),
                                captured.checkpoint))),
            "own-cancellation child restore failed");
    const auto accepted_child =
        cancelled_child.session->request_cancel(
            cancellation_request("cancel:held-child-own",
                                 std::string(cancelled_child_id)));
    const auto cancelled = cancelled_child.session->execute_step();
    require(accepted_child &&
                cancelled.status == kernel::StepStatus::Cancelled &&
                cancelled_child.session->state() ==
                    kernel::SessionState::Cancelled,
            "restored child did not accept its exact RunId cancellation");
}

void verify_interval_one_compatibility() {
    constexpr std::string_view kIntervalOneImageFingerprint =
        "7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14";
    const auto compiled = ref_yyz::compile_complete_image();
    require(compiled.succeeded(),
            "interval-1 compatibility Image compilation failed");
    auto image = std::make_shared<const Image>(*compiled.value);
    require(image->held_outputs().empty() &&
                image->fingerprint() == kIntervalOneImageFingerprint,
            "interval-1 Image identity changed");
    auto bundle = initialize_session(
        image, "run:interval-one-compatibility");
    const auto completed = bundle.session->run_to_terminal();
    const auto& samples = bundle.adapter.opening_boundary->
        controller_guidance_samples;
    require(completed &&
                bundle.session->state() ==
                    kernel::SessionState::Completed &&
                bundle.adapter.opening_boundary->guidance_output_ticks ==
                    std::vector<std::int64_t>({0, 1, 2}) &&
                samples.size() == 3U &&
                std::all_of(samples.begin(), samples.end(),
                            [](const auto& sample) {
                                return sample.fresh &&
                                       sample.age_steps == 0U &&
                                       sample.consumer_tick ==
                                           sample.sample_tick;
                            }) &&
                bundle.session->run_outcome() != nullptr &&
                bundle.session->run_outcome()->mission_result_available,
            "interval-1 runtime oracle changed");
}

void run() {
    verify_compiler_contract();
    const auto image = qualification_image();
    verify_tick_sequence(image);
    verify_missing_and_expired_fail_closed();
    verify_staging_rollback(image);
    verify_held_fault_rollback(image);
    verify_cancellation_publication_boundaries(image);
    verify_session_isolation(image);
    verify_reset_and_dispose(image);
    verify_checkpoint_restore(image);
    verify_checkpoint_cancellation_exclusion(image);
    verify_interval_one_compatibility();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr <<
            "usage: gnc_kernel_multirate_held_output_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout <<
            "R3 multi-rate HeldLatest qualification: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr <<
            "R3 multi-rate HeldLatest qualification: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
