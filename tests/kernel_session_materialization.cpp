#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace allocation_fault {

thread_local std::int64_t fail_after = -1;
thread_local bool failure_triggered = false;
thread_local std::size_t allocations_after_failure = 0U;

void arm(std::int64_t allocations_before_failure) noexcept {
    fail_after = allocations_before_failure;
    failure_triggered = false;
    allocations_after_failure = 0U;
}

void disarm() noexcept {
    fail_after = -1;
    failure_triggered = false;
}

[[nodiscard]] std::size_t post_failure_allocation_count() noexcept {
    return allocations_after_failure;
}

[[nodiscard]] bool should_fail() noexcept {
    if (fail_after < 0) {
        if (failure_triggered) ++allocations_after_failure;
        return false;
    }
    if (fail_after == 0) {
        fail_after = -1;
        failure_triggered = true;
        return true;
    }
    --fail_after;
    return false;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void release(void* address) noexcept {
    std::free(address);
}

} // namespace allocation_fault

void* operator new(std::size_t size) {
    if (allocation_fault::should_fail()) throw std::bad_alloc();
    if (auto* result = std::malloc(size == 0U ? 1U : size)) return result;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* address) noexcept {
    allocation_fault::release(address);
}
void operator delete[](void* address) noexcept {
    allocation_fault::release(address);
}
void operator delete(void* address, std::size_t) noexcept {
    allocation_fault::release(address);
}
void operator delete[](void* address, std::size_t) noexcept {
    allocation_fault::release(address);
}

namespace {

using gnc::contracts::ExecutionPlanImage;
using gnc::kernel::SessionError;
using gnc::kernel::SessionState;
using gnc::tests::ref_yyz::AdapterOptions;
using gnc::tests::ref_yyz::CommittedRigidMassProbe;
using gnc::tests::ref_yyz::FailurePhase;
using gnc::tests::ref_yyz::MissionResultProbe;
using gnc::tests::ref_yyz::TraceObjectKind;

template <typename Value, typename = void>
struct has_replace_slot : std::false_type {};

template <typename Value>
struct has_replace_slot<
    Value,
    std::void_t<decltype(std::declval<Value&>().replace_slot(
        std::uint32_t{}, gnc::kernel::InProcessValueView{}))>>
    : std::true_type {};

template <typename Value, typename = void>
struct has_committed_state_object : std::false_type {};

template <typename Value>
struct has_committed_state_object<
    Value,
    std::void_t<decltype(std::declval<const Value&>().committed_state_object(
        std::uint32_t{}))>> : std::true_type {};

template <typename Value, typename = void>
struct has_runtime_cell_object : std::false_type {};

template <typename Value>
struct has_runtime_cell_object<
    Value,
    std::void_t<decltype(std::declval<const Value&>().runtime_cell_object(
        std::uint32_t{}))>> : std::true_type {};

template <typename Value, typename = void>
struct has_candidate_state_object : std::false_type {};

template <typename Value>
struct has_candidate_state_object<
    Value,
    std::void_t<decltype(std::declval<const Value&>().candidate_state_object(
        std::uint32_t{}))>> : std::true_type {};

template <typename Value, typename = void>
struct has_slot_object : std::false_type {};

template <typename Value>
struct has_slot_object<
    Value,
    std::void_t<decltype(std::declval<const Value&>().slot_object(
        std::uint32_t{}))>> : std::true_type {};

template <typename Value, typename = void>
struct has_clone_committed_to_candidate : std::false_type {};

template <typename Value>
struct has_clone_committed_to_candidate<
    Value,
    std::void_t<decltype(
        std::declval<Value&>().clone_committed_to_candidate(
            std::uint32_t{}))>> : std::true_type {};

template <typename Value, typename = void>
struct has_address_member : std::false_type {};

template <typename Value>
struct has_address_member<
    Value,
    std::void_t<decltype(std::declval<const Value&>().address)>>
    : std::true_type {};

template <typename Value, typename = void>
struct has_execute_opening_boundary : std::false_type {};

template <typename Value>
struct has_execute_opening_boundary<
    Value,
    std::void_t<decltype(
        std::declval<Value&>().execute_opening_boundary())>>
    : std::true_type {};

static_assert(!has_replace_slot<gnc::kernel::Session>::value,
              "Session must not expose a generic slot writer");
static_assert(!has_committed_state_object<gnc::kernel::Session>::value,
              "Session must not expose committed-state object addresses");
static_assert(!has_runtime_cell_object<gnc::kernel::Session>::value,
              "Session must not expose Runtime Cell object addresses");
static_assert(!has_candidate_state_object<gnc::kernel::Session>::value,
              "Session must not expose candidate-state object addresses");
static_assert(!has_slot_object<gnc::kernel::Session>::value,
              "Session must not expose slot object addresses");
static_assert(
    !has_clone_committed_to_candidate<gnc::kernel::Session>::value,
    "Session must not expose committed-to-candidate mutation");
static_assert(!has_address_member<gnc::kernel::SessionStorageExtent>::value,
              "Session storage metadata must not expose arena addresses");
static_assert(!has_execute_opening_boundary<gnc::kernel::Session>::value,
              "opening boundary must remain a qualification-only phase");

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
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

[[nodiscard]] bool same_histories(
    const std::vector<gnc::kernel::SessionCommittedHistoryInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedHistoryInfo>& rhs)
    noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (lhs[index].history_handle != rhs[index].history_handle ||
            lhs[index].history_depth != rhs[index].history_depth ||
            lhs[index].sample_count != rhs[index].sample_count ||
            lhs[index].member_count != rhs[index].member_count ||
            lhs[index].first_tick != rhs[index].first_tick ||
            lhs[index].last_tick != rhs[index].last_tick) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_outputs(
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& lhs,
    const std::vector<gnc::kernel::SessionCommittedOutputInfo>& rhs)
    noexcept {
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
    const gnc::kernel::RuntimeDiagnostic& lhs,
    const gnc::kernel::RuntimeDiagnostic& rhs) noexcept {
    return lhs.code == rhs.code && lhs.stage == rhs.stage &&
           lhs.operation == rhs.operation &&
           lhs.source_kind == rhs.source_kind &&
           lhs.source_handle == rhs.source_handle &&
           lhs.source_field == rhs.source_field &&
           lhs.subject_kind == rhs.subject_kind &&
           lhs.subject_reference_kind == rhs.subject_reference_kind &&
           lhs.subject_handle == rhs.subject_handle &&
           lhs.run_id == rhs.run_id &&
           lhs.run_context_present == rhs.run_context_present &&
           lhs.tick == rhs.tick &&
           lhs.base_epoch == rhs.base_epoch &&
           lhs.simulation_context_present ==
               rhs.simulation_context_present &&
           lhs.cause_kind == rhs.cause_kind &&
           lhs.cause_code == rhs.cause_code &&
           lhs.cause_ref == rhs.cause_ref &&
           lhs.validity_effect == rhs.validity_effect &&
           lhs.disposition == rhs.disposition &&
           lhs.message_key == rhs.message_key && lhs.detail == rhs.detail;
}

[[nodiscard]] bool exactly_same(const gnc::kernel::RunOutcome& lhs,
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

[[nodiscard]] gnc::kernel::InitializationRequest initialization_request(
    const ExecutionPlanImage& image,
    std::string run_id = "run:materialization") {
    return {gnc::kernel::RunId{std::move(run_id)},
            gnc::kernel::exact_run_binding(image)};
}

[[nodiscard]] gnc::kernel::ResetRequest reset_request(
    const ExecutionPlanImage& image, std::string run_id) {
    return {gnc::kernel::RunId{std::move(run_id)},
            gnc::kernel::exact_run_binding(image)};
}

[[nodiscard]] CommittedRigidMassProbe committed_probe(
    const gnc::kernel::Session& session,
    const gnc::tests::ref_yyz::RefYyzSessionAdapter& adapter) {
    CommittedRigidMassProbe result;
    const auto read =
        gnc::tests::ref_yyz::read_committed_rigid_mass_for_qualification(
            session, adapter, result);
    require(static_cast<bool>(read),
            "committed state qualification read failed");
    return result;
}

[[nodiscard]] MissionResultProbe mission_result_probe(
    const gnc::kernel::Session& session,
    const gnc::tests::ref_yyz::RefYyzSessionAdapter& adapter) {
    MissionResultProbe result;
    const auto read =
        gnc::tests::ref_yyz::read_mission_result_for_qualification(
            session, adapter, result);
    require(static_cast<bool>(read),
            "mission-result qualification read failed");
    return result;
}

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage> image_from(
    gnc::contracts::ExecutionPlanImageData data) {
    return std::make_shared<const ExecutionPlanImage>(
        ExecutionPlanImage::freeze(std::move(data)));
}

[[nodiscard]] std::shared_ptr<const ExecutionPlanImage>
build_deterministic_image() {
    const auto first =
        gnc::tests::ref_yyz::compile_complete_image();
    const auto second =
        gnc::tests::ref_yyz::compile_complete_image();
    require(first.succeeded() && second.succeeded(),
            "deterministic REF-YYZ compilation failed");
    require(first.value->source_semantic_hash() ==
                    second.value->source_semantic_hash() &&
                first.value->descriptor_semantic_hash() ==
                    second.value->descriptor_semantic_hash() &&
                first.value->fingerprint() == second.value->fingerprint(),
            "identical REF-YYZ sources produced different Images");
    return std::make_shared<const ExecutionPlanImage>(*first.value);
}

void require_reverse_lifetime(
    const gnc::tests::ref_yyz::MaterializationTrace& trace,
    TraceObjectKind kind, std::string_view message) {
    auto constructed = trace.constructed_handles(kind);
    auto destroyed = trace.destroyed_handles(kind);
    std::reverse(constructed.begin(), constructed.end());
    require(constructed == destroyed, message);
}

void verify_created_and_initialized(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "Image-only Session creation failed");
    require(creation.session->state() == SessionState::Created &&
                creation.session->preparation_count() == 0U &&
                creation.session->runtime_cell_count() == 0U &&
                creation.session->committed_state_count() == 0U &&
                creation.session->storage_extents().empty(),
            "Created Session exposed initialized objects");

    const auto initialized = creation.session->initialize(
        initialization_request(*image));
    require(static_cast<bool>(initialized) &&
                creation.session->state() == SessionState::Initialized,
            "Image-only Session initialization failed");
    require(creation.session->preparation_count() == 3U &&
                creation.session->runtime_cell_count() == 7U &&
                creation.session->committed_state_count() == 2U,
            "Session materialized the wrong exact object counts");
    require(creation.session->preparation_handles() ==
                    image->lifecycle().preparation_handles &&
                creation.session->runtime_component_handles() ==
                    image->lifecycle().runtime_component_handles,
            "Session inspection lost stable lifecycle handles");

    const auto state_handles =
        creation.session->committed_state_block_handles();
    require(state_handles.size() == image->state_blocks().size(),
            "Session inspection lost state block handles");
    const auto state_info = creation.session->state_blocks();
    require(state_info.size() == image->state_blocks().size() &&
                std::all_of(state_info.begin(), state_info.end(),
                            [](const auto& block) {
                                return block.state_block_handle != 0U &&
                                       block.owner_runtime_component_handle !=
                                           0U &&
                                       block.committed_slot_handle != 0U &&
                                       block.candidate_slot_handle != 0U &&
                                       block.codec_entry_handle != 0U &&
                                       block.committed_epoch == 0U;
                            }),
            "Session state-store metadata is incomplete");

    const auto extents = creation.session->storage_extents();
    require(extents.size() == image->storage_layouts().size(),
            "Session did not allocate every Image storage extent");
    for (const auto& layout : image->storage_layouts()) {
        const auto found = std::find_if(
            extents.begin(), extents.end(), [&layout](const auto& extent) {
                return extent.layout_handle == layout.handle;
            });
        require(found != extents.end() &&
                    found->size_bytes == layout.size_bytes &&
                    found->alignment_bytes == layout.alignment_bytes &&
                    found->storage_class == layout.storage_class,
                "Session storage metadata differs from Image extent");
    }

    const auto non_trivial =
        gnc::tests::ref_yyz::exercise_non_trivial_objects(
            *creation.session, adapter);
    require(non_trivial.state_is_non_trivial &&
                non_trivial.output_is_non_trivial &&
                non_trivial.state_store_cloned_twice &&
                non_trivial.frame_values_deferred,
            "non-trivial state-store clone or deferred frame lifetime failed");

    require(adapter.trace->live_object_count() > 0U,
            "initialized Session has no tracked live objects");
    creation.session.reset();
    require(adapter.trace->live_object_count() == 0U,
            "Session destruction leaked constructed objects");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::Preparation,
                             "preparations were not destroyed in reverse");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::RuntimeCell,
                             "Runtime Cells were not destroyed in reverse");
}

void verify_creation_diagnostics(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);

    const auto null_image = gnc::kernel::create_session(
        std::shared_ptr<const ExecutionPlanImage>{}, adapter.provider);
    require(!null_image && null_image.session == nullptr &&
                null_image.result.error == SessionError::NullImage &&
                null_image.primary_diagnostic.has_value() &&
                null_image.primary_diagnostic->code ==
                    gnc::kernel::RuntimeDiagnosticCode::
                        ImageValidationFailed &&
                null_image.primary_diagnostic->stage ==
                    gnc::kernel::RuntimeDiagnosticStage::SessionCreation &&
                null_image.primary_diagnostic->operation ==
                    gnc::kernel::RuntimeOperation::CreateSession &&
                null_image.primary_diagnostic->source_kind ==
                    gnc::kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                null_image.primary_diagnostic->source_handle == 0U &&
                null_image.primary_diagnostic->source_field ==
                    gnc::kernel::RuntimeApiField::Image &&
                null_image.primary_diagnostic->subject_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectKind::Image &&
                null_image.primary_diagnostic->subject_reference_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectReferenceKind::None &&
                null_image.primary_diagnostic->subject_handle == 0U &&
                !null_image.primary_diagnostic->run_context_present &&
                null_image.primary_diagnostic->tick == 0 &&
                null_image.primary_diagnostic->base_epoch == 0U &&
                !null_image.primary_diagnostic->simulation_context_present &&
                null_image.primary_diagnostic->cause_kind ==
                    gnc::kernel::RuntimeDiagnosticCauseKind::SessionError &&
                null_image.primary_diagnostic->cause_code ==
                    SessionError::NullImage &&
                null_image.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Invalid,
            "null Image creation failure lost its public diagnostic context");

    const auto null_provider = gnc::kernel::create_session(image, nullptr);
    require(!null_provider && null_provider.session == nullptr &&
                null_provider.result.error ==
                    SessionError::NullMaterializationProvider &&
                null_provider.primary_diagnostic.has_value() &&
                null_provider.primary_diagnostic->operation ==
                    gnc::kernel::RuntimeOperation::CreateSession &&
                null_provider.primary_diagnostic->source_kind ==
                    gnc::kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                null_provider.primary_diagnostic->source_handle == 0U &&
                null_provider.primary_diagnostic->source_field ==
                    gnc::kernel::RuntimeApiField::MaterializationProvider &&
                null_provider.primary_diagnostic->subject_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectKind::
                        MaterializationProvider &&
                null_provider.primary_diagnostic->subject_reference_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectReferenceKind::None &&
                null_provider.primary_diagnostic->subject_handle == 0U &&
                !null_provider.primary_diagnostic->run_context_present &&
                null_provider.primary_diagnostic->cause_code ==
                    SessionError::NullMaterializationProvider &&
                null_provider.primary_diagnostic->tick == 0 &&
                null_provider.primary_diagnostic->base_epoch == 0U &&
                !null_provider.primary_diagnostic
                     ->simulation_context_present &&
                null_provider.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Invalid,
            "null provider creation failure lost its public diagnostic context");

    allocation_fault::arm(0);
    const auto allocation_failure =
        gnc::kernel::create_session(image, adapter.provider);
    const auto allocations_after_failure =
        allocation_fault::post_failure_allocation_count();
    allocation_fault::disarm();
    require(!allocation_failure && allocation_failure.session == nullptr &&
                allocation_failure.result.error ==
                    SessionError::AllocationFailure &&
                allocation_failure.primary_diagnostic.has_value() &&
                allocation_failure.primary_diagnostic->code ==
                    gnc::kernel::RuntimeDiagnosticCode::AllocationFailed &&
                allocation_failure.primary_diagnostic->operation ==
                    gnc::kernel::RuntimeOperation::CreateSession &&
                allocation_failure.primary_diagnostic->source_kind ==
                    gnc::kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                allocation_failure.primary_diagnostic->source_field ==
                    gnc::kernel::RuntimeApiField::RuntimeAllocation &&
                allocation_failure.primary_diagnostic->subject_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectKind::Session &&
                allocation_failure.primary_diagnostic
                        ->subject_reference_kind ==
                    gnc::kernel::RuntimeDiagnosticSubjectReferenceKind::None &&
                !allocation_failure.primary_diagnostic
                     ->run_context_present &&
                !allocation_failure.primary_diagnostic
                     ->simulation_context_present &&
                allocation_failure.primary_diagnostic->cause_code ==
                    SessionError::AllocationFailure &&
                allocation_failure.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Invalid &&
                allocations_after_failure == 0U,
            "creation allocation failure lost its allocation-safe diagnostic");
}

void verify_failure_unwind(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    FailurePhase phase, std::size_t ordinal, SessionError expected) {
    AdapterOptions options;
    options.failure = {phase, ordinal};
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "failure Session creation failed");
    const auto result = creation.session->initialize(
        initialization_request(*image, "run:materialization-failure"));
    const auto expected_stage =
        phase == FailurePhase::InitialState
            ? gnc::kernel::RuntimeDiagnosticStage::InitialState
            : gnc::kernel::RuntimeDiagnosticStage::Materialization;
    require(!static_cast<bool>(result) && result.result.error == expected &&
                creation.session->state() == SessionState::Failed &&
                !result.initialization_commit &&
                result.primary_diagnostic.has_value() &&
                result.primary_diagnostic->code ==
                    gnc::kernel::RuntimeDiagnosticCode::
                        MaterializationFailed &&
                 result.primary_diagnostic->stage == expected_stage &&
                 result.primary_diagnostic->operation ==
                     gnc::kernel::RuntimeOperation::Initialize &&
                 result.primary_diagnostic->source_kind ==
                     gnc::kernel::RuntimeDiagnosticSourceKind::
                         ImageConformance &&
                 result.primary_diagnostic->source_handle ==
                     result.primary_diagnostic->subject_handle &&
                 result.primary_diagnostic->source_field ==
                     gnc::kernel::RuntimeApiField::None &&
                 result.primary_diagnostic->subject_kind !=
                     gnc::kernel::RuntimeDiagnosticSubjectKind::ImageObject &&
                 result.primary_diagnostic->subject_reference_kind ==
                     gnc::kernel::RuntimeDiagnosticSubjectReferenceKind::
                         ImageHandle &&
                 result.primary_diagnostic->run_context_present &&
                 result.primary_diagnostic->simulation_context_present &&
                 result.primary_diagnostic->cause_kind ==
                     gnc::kernel::RuntimeDiagnosticCauseKind::SessionError &&
                 result.primary_diagnostic->validity_effect ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                creation.session->active_run_id() == nullptr &&
                !creation.session->run_sequence().has_value() &&
                creation.session->run_outcome() != nullptr &&
                creation.session->run_outcome()->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                creation.session->run_outcome()->validity ==
                    gnc::contracts::EvidenceValidity::Unknown &&
                !creation.session->run_outcome()
                     ->run_start_committed &&
                creation.session->run_outcome()->finalization_status ==
                    gnc::kernel::RunFinalizationStatus::NotStarted &&
                creation.session->run_outcome()->run_start_kind ==
                    gnc::kernel::RunStartKind::Initialize &&
                creation.session->run_outcome()
                    ->final_committed_epoch == 0U &&
                creation.session->run_outcome()->final_tick == 0 &&
                creation.session->preparation_count() == 0U &&
                creation.session->runtime_cell_count() == 0U &&
                creation.session->committed_state_count() == 0U &&
                creation.session->storage_extents().empty() &&
                adapter.trace->live_object_count() == 0U,
            "failed initialization exposed or leaked partial state");
    const auto* failed_outcome = creation.session->run_outcome();
    const auto event_count_before_dispose = adapter.trace->events.size();
    require(failed_outcome != nullptr && creation.session->dispose() &&
                creation.session->state() == SessionState::Disposed &&
                creation.session->preparation_count() == 0U &&
                creation.session->runtime_cell_count() == 0U &&
                creation.session->committed_state_count() == 0U &&
                creation.session->storage_extents().empty() &&
                creation.session->run_outcome() == failed_outcome &&
                creation.session->run_outcome_for_sequence(0U) ==
                    failed_outcome &&
                &creation.session->image() == image.get() &&
                adapter.trace->events.size() ==
                    event_count_before_dispose,
            "initialization-failed dispose repeated cleanup or lost outcome identity");
    creation.session.reset();
    require(adapter.trace->events.size() == event_count_before_dispose,
            "initialization-failed destructor repeated cleanup");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::Preparation,
                             "failure did not reverse preparations");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::RuntimeCell,
                             "failure did not reverse Runtime Cells");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::Slot,
                             "failure did not reverse constructed slots");
    require_reverse_lifetime(*adapter.trace, TraceObjectKind::State,
                             "failure did not reverse constructed states");

    auto clean_adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(clean_adapter), clean_adapter.error);
    auto next = gnc::kernel::create_session(image, clean_adapter.provider);
    require(static_cast<bool>(next) &&
                static_cast<bool>(next.session->initialize(
                    initialization_request(*image,
                                           "run:materialization-recovery"))) &&
                next.session->state() == SessionState::Initialized,
            "Session after injected initialization failure did not succeed");
    next.session.reset();
    require(clean_adapter.trace->live_object_count() == 0U,
            "successful retry Session leaked objects");
}

template <typename Mutate>
void require_metadata_failure(
    const std::shared_ptr<const ExecutionPlanImage>& good_image,
    const std::shared_ptr<const gnc::kernel::SessionMaterializationProvider>&
        provider,
    const std::shared_ptr<
        gnc::tests::ref_yyz::MaterializationTrace>& trace,
    Mutate mutate, SessionError expected, std::string_view message) {
    auto data = good_image->data();
    mutate(data);
    auto malformed = image_from(std::move(data));
    auto creation = gnc::kernel::create_session(malformed, provider);
    require(static_cast<bool>(creation),
            "malformed Image could not reach initialization validation");
    const auto result = creation.session->initialize(
        initialization_request(*malformed,
                               "run:materialization-metadata-failure"));
    const bool numeric_image_subject =
        expected == SessionError::UnsupportedImageRevision ||
        expected == SessionError::InvalidImageHandle;
    require(!static_cast<bool>(result) && result.result.error == expected &&
                result.primary_diagnostic.has_value() &&
                result.primary_diagnostic->operation ==
                    gnc::kernel::RuntimeOperation::Initialize &&
                (!numeric_image_subject ||
                 (result.primary_diagnostic->source_kind ==
                      gnc::kernel::RuntimeDiagnosticSourceKind::ImageField &&
                  result.primary_diagnostic->source_handle ==
                      result.result.image_handle &&
                  result.primary_diagnostic->subject_kind ==
                      (expected == SessionError::UnsupportedImageRevision
                           ? gnc::kernel::RuntimeDiagnosticSubjectKind::Image
                           : gnc::kernel::RuntimeDiagnosticSubjectKind::
                                 ImageObject) &&
                  result.primary_diagnostic->subject_reference_kind ==
                      gnc::kernel::
                          RuntimeDiagnosticSubjectReferenceKind::NumericValue &&
                  result.primary_diagnostic->subject_handle ==
                      result.result.image_handle)) &&
                creation.session->state() == SessionState::Failed &&
                trace->events.empty() && trace->live_object_count() == 0U,
            message);
}

void verify_metadata_failures(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) { ++data.state_blocks.front().size_bytes; },
        SessionError::ObjectSizeMismatch,
        "wrong state size did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) { data.state_blocks.front().alignment_bytes *= 2U; },
        SessionError::ObjectAlignmentMismatch,
        "wrong state alignment did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) { data.state_blocks.front().layout_id += ".wrong"; },
        SessionError::ObjectLayoutMismatch,
        "wrong state layout did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) { data.state_blocks.front().codec_entry_handle += 1000U; },
        SessionError::ObjectCodecMismatch,
        "wrong state codec did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) { data.revision = 6U; },
        SessionError::UnsupportedImageRevision,
        "unsupported Image revision reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            auto layout = std::find_if(
                data.storage_layouts.begin(), data.storage_layouts.end(),
                [](const auto& value) {
                    return value.ordered_slot_handles.size() >= 2U;
                });
            const auto first = std::find_if(
                data.slots.begin(), data.slots.end(), [&layout](const auto& slot) {
                    return slot.handle == layout->ordered_slot_handles[0U];
                });
            const auto second = std::find_if(
                data.slots.begin(), data.slots.end(), [&layout](const auto& slot) {
                    return slot.handle == layout->ordered_slot_handles[1U];
                });
            first->offset_bytes = 0U;
            second->offset_bytes = 0U;
        },
        SessionError::StorageOverlap,
        "overlapping Image slots reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.storage_layouts.front().ordered_slot_handles.pop_back();
        },
        SessionError::InvalidImageStructure,
        "missing ordered storage membership reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            auto& members =
                data.storage_layouts.front().ordered_slot_handles;
            members.push_back(members.front());
        },
        SessionError::InvalidStorageLayout,
        "duplicate ordered storage membership reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            auto first = data.storage_layouts.begin();
            auto second = std::find_if(
                data.storage_layouts.begin() + 1,
                data.storage_layouts.end(), [&first](const auto& value) {
                    return !value.ordered_slot_handles.empty() &&
                           value.handle != first->handle;
                });
            first->ordered_slot_handles.front() =
                second->ordered_slot_handles.front();
        },
        SessionError::InvalidImageStructure,
        "cross-layout storage membership reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.lifecycle.runtime_component_handles.pop_back();
        },
        SessionError::InvalidImageStructure,
        "incomplete lifecycle reached placement");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.cancellation_policy.safe_points.front().kind =
                static_cast<gnc::contracts::
                                PlanImageCancellationSafePointKind>(255U);
        },
        SessionError::InvalidImageStructure,
        "unknown cancellation safe-point kind reached execution");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.cancellation_policy.safe_points.back().handle =
                data.cancellation_policy.safe_points.front().handle;
        },
        SessionError::InvalidImageHandle,
        "duplicate cancellation safe-point handle reached execution");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            auto duplicate = data.cancellation_policy.safe_points.front();
            ++duplicate.handle;
            while (std::any_of(
                data.cancellation_policy.safe_points.begin(),
                data.cancellation_policy.safe_points.end(),
                [&duplicate](const auto& point) {
                    return point.handle == duplicate.handle;
                })) {
                ++duplicate.handle;
            }
            data.cancellation_policy.safe_points.push_back(duplicate);
        },
        SessionError::InvalidImageStructure,
        "duplicate cancellation safe-point tuple reached execution");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.cancellation_policy.safe_points.front()
                .transaction_handle += 100000U;
        },
        SessionError::InvalidImageHandle,
        "unknown cancellation transaction reached execution");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            auto& point = data.cancellation_policy.safe_points.front();
            point.kind = gnc::contracts::
                PlanImageCancellationSafePointKind::AfterBoundaryCallsite;
            point.subject_handle =
                data.transactions.front().candidates.front().producer_handle;
        },
        SessionError::InvalidImageStructure,
        "incompatible cancellation subject reached execution");
    require_metadata_failure(
        image, adapter.provider, adapter.trace,
        [](auto& data) {
            data.cancellation_policy.safe_points.pop_back();
        },
        SessionError::InvalidImageStructure,
        "incomplete cancellation policy reached execution");

    AdapterOptions missing_options;
    missing_options.omit_first_slot_materializer = true;
    auto missing = gnc::tests::ref_yyz::make_session_adapter(
        *image, missing_options);
    require(static_cast<bool>(missing), missing.error);
    auto creation = gnc::kernel::create_session(image, missing.provider);
    require(static_cast<bool>(creation), "missing-provider Session create failed");
    const auto result = creation.session->initialize(
        initialization_request(*image,
                               "run:materialization-missing-provider"));
    require(!static_cast<bool>(result) &&
                result.result.error == SessionError::MissingMaterializer &&
                result.result.image_handle ==
                    missing.first_non_state_slot_handle &&
                missing.trace->events.empty() &&
                missing.trace->live_object_count() == 0U,
            "missing slot materializer did not fail deterministically");
}

void verify_materializer_identity_failures(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    const auto run = [&image](AdapterOptions options, SessionError expected,
                              bool preparation_allowed,
                              std::string_view message) {
        auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image,
                                                                 options);
        require(static_cast<bool>(adapter), adapter.error);
        auto creation = gnc::kernel::create_session(image, adapter.provider);
        require(static_cast<bool>(creation),
                "identity-failure Session creation failed");
        const auto result = creation.session->initialize(
            initialization_request(*image,
                                   "run:materialization-identity-failure"));
        require(!result && result.result.error == expected &&
                    creation.session->state() == SessionState::Failed &&
                    adapter.trace->constructed_handles(
                        TraceObjectKind::RuntimeCell).empty() &&
                    adapter.trace->live_object_count() == 0U &&
                    (preparation_allowed || adapter.trace->events.empty()),
                message);
    };

    AdapterOptions swapped;
    swapped.swap_first_two_preparation_materializers = true;
    run(swapped, SessionError::InvalidMaterializerIdentity, false,
        "provider returned a materializer for the wrong valid prep handle");

    AdapterOptions disguised;
    disguised.disguise_second_preparation_as_first = true;
    run(disguised, SessionError::ObjectTypeMismatch, true,
        "equal-size wrong prepared type reached a Runtime Cell factory");

    AdapterOptions wrong_factory;
    wrong_factory.wrong_first_runtime_factory_identity = true;
    run(wrong_factory, SessionError::InvalidMaterializerIdentity, false,
        "wrong Runtime Cell factory identity reached placement");

    AdapterOptions undeclared;
    undeclared.request_undeclared_preparation = true;
    auto undeclared_adapter =
        gnc::tests::ref_yyz::make_session_adapter(*image, undeclared);
    require(static_cast<bool>(undeclared_adapter),
            undeclared_adapter.error);
    auto undeclared_creation = gnc::kernel::create_session(
        image, undeclared_adapter.provider);
    require(static_cast<bool>(undeclared_creation),
            "undeclared-dependency Session creation failed");
    const auto undeclared_result =
        undeclared_creation.session->initialize(
            initialization_request(*image,
                                   "run:materialization-dependency-failure"));
    require(!undeclared_result &&
                undeclared_result.result.error ==
                    SessionError::RuntimeCellFailed &&
                undeclared_adapter.undeclared_preparation_visible != nullptr &&
                !*undeclared_adapter.undeclared_preparation_visible &&
                undeclared_adapter.trace->live_object_count() == 0U,
            "undeclared valid preparation was visible to a Runtime Cell factory");

    auto data = image->data();
    require(data.invocations.size() >= 2U,
            "preparation-handle mutation needs two invocations");
    const auto replacement = std::find_if(
        data.invocations.begin() + 1, data.invocations.end(),
        [&data](const auto& invocation) {
            return invocation.provider_preparation_handle !=
                   data.invocations.front().provider_preparation_handle;
        });
    require(replacement != data.invocations.end(),
            "preparation-handle mutation lacks a distinct valid handle");
    data.invocations.front().provider_preparation_handle =
        replacement->provider_preparation_handle;
    auto malformed = image_from(std::move(data));
    auto malformed_adapter =
        gnc::tests::ref_yyz::make_session_adapter(*malformed);
    require(static_cast<bool>(malformed_adapter), malformed_adapter.error);
    auto creation = gnc::kernel::create_session(
        malformed, malformed_adapter.provider);
    require(static_cast<bool>(creation),
            "mutated-dependency Session creation failed");
    const auto result = creation.session->initialize(
        initialization_request(*malformed,
                               "run:materialization-mutated-dependency"));
    require(!result &&
                result.result.error == SessionError::ObjectTypeMismatch &&
                malformed_adapter.trace
                    ->constructed_handles(TraceObjectKind::RuntimeCell)
                    .empty() &&
                malformed_adapter.trace->live_object_count() == 0U,
            "valid but wrong invocation preparation reached a factory cast");
}

void verify_allocation_failure_unwind(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    adapter.trace->events.reserve(2048U);
    bool observed_preplacement_failure = false;
    bool observed_partial_object_failure = false;
    bool observed_history_allocation_failure = false;
    bool observed_success = false;
    for (std::int64_t fail_after = 0;
         fail_after < 512 && !observed_success; ++fail_after) {
        adapter.trace->events.clear();
        auto creation = gnc::kernel::create_session(image, adapter.provider);
        require(static_cast<bool>(creation),
                "allocation-fault Session creation failed before injection");
        auto request = initialization_request(
            *image, "run:materialization-allocation-failure");
        allocation_fault::arm(fail_after);
        const auto result = creation.session->initialize(std::move(request));
        allocation_fault::disarm();
        if (result) {
            observed_success = true;
            require(creation.session->state() == SessionState::Initialized,
                    "successful allocation-fault pass has wrong state");
        } else {
            require(creation.session->state() == SessionState::Failed &&
                        !result.result.detail.empty() &&
                        result.primary_diagnostic.has_value() &&
                        result.primary_diagnostic->operation ==
                            gnc::kernel::RuntimeOperation::Initialize &&
                        result.primary_diagnostic->run_context_present &&
                        (result.primary_diagnostic->subject_handle != 0U ||
                         result.primary_diagnostic->subject_kind ==
                             gnc::kernel::RuntimeDiagnosticSubjectKind::Run),
                    "allocation failure escaped or lost stable diagnostics");
            observed_history_allocation_failure =
                observed_history_allocation_failure ||
                (result.primary_diagnostic->stage ==
                     gnc::kernel::RuntimeDiagnosticStage::History &&
                 result.result.detail ==
                     "Session initialization allocation failed");
            observed_preplacement_failure =
                observed_preplacement_failure ||
                adapter.trace->events.empty();
            observed_partial_object_failure =
                observed_partial_object_failure ||
                !adapter.trace->events.empty();
        }
        creation.session.reset();
        require(adapter.trace->live_object_count() == 0U,
                "allocation fault leaked a placed Session object");
    }
    require(observed_preplacement_failure &&
                observed_partial_object_failure &&
                observed_history_allocation_failure && observed_success,
            "allocation injection did not cover reserve, placement, history and success");
}

void verify_reset_allocation_failure_atomicity(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    bool observed_outcome_staging_failure = false;
    bool observed_history_staging_failure = false;
    bool observed_success = false;
    for (std::int64_t fail_after = 0;
         fail_after < 256 && !observed_success; ++fail_after) {
        auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
        require(static_cast<bool>(adapter), adapter.error);
        adapter.trace->events.reserve(4096U);
        auto creation = gnc::kernel::create_session(image, adapter.provider);
        require(creation &&
                    creation.session->initialize(initialization_request(
                        *image,
                        std::string("run:reset-allocation-base:") +
                            std::to_string(fail_after))) &&
                    creation.session->run_to_terminal(),
                "reset allocation fixture could not complete its first run");

        const auto before_state = committed_probe(*creation.session, adapter);
        const auto before_blocks = creation.session->state_blocks();
        const auto before_histories =
            creation.session->committed_histories();
        const auto before_outputs = creation.session->committed_outputs();
        const auto before_result = mission_result_probe(*creation.session,
                                                        adapter);
        const auto* first_outcome = creation.session->run_outcome();
        require(first_outcome != nullptr,
                "reset allocation fixture lacks a completed outcome");
        const auto first_outcome_copy = *first_outcome;
        auto request = reset_request(
            *image, std::string("run:reset-allocation-attempt:") +
                        std::to_string(fail_after));

        allocation_fault::arm(fail_after);
        const auto reset = creation.session->reset(std::move(request));
        const auto post_failure_allocations =
            allocation_fault::post_failure_allocation_count();
        allocation_fault::disarm();

        if (reset) {
            observed_success = true;
            require(creation.session->state() == SessionState::Initialized &&
                        observed_outcome_staging_failure &&
                        observed_history_staging_failure,
                    "reset allocation sweep reached success before both staging failures");
        } else {
            observed_outcome_staging_failure =
                observed_outcome_staging_failure ||
                reset.result.detail ==
                    "reset outcome staging allocation failed";
            observed_history_staging_failure =
                observed_history_staging_failure ||
                reset.result.detail ==
                    "reset history staging allocation failed";
            if (reset.result.detail ==
                    "reset outcome staging allocation failed" ||
                reset.result.detail ==
                    "reset history staging allocation failed") {
                require(reset.result.error == SessionError::AllocationFailure &&
                            reset.primary_diagnostic.has_value() &&
                            reset.primary_diagnostic->code ==
                                gnc::kernel::RuntimeDiagnosticCode::
                                    AllocationFailed &&
                            reset.primary_diagnostic->stage ==
                                gnc::kernel::RuntimeDiagnosticStage::
                                    ResetPrecommit,
                        "reset staging allocation failure lost its diagnostic");
            }
            const auto* failed_outcome = creation.session->run_outcome();
            require(post_failure_allocations == 0U &&
                        creation.session->state() == SessionState::Failed &&
                        !reset.reset_commit &&
                        reset.proposed_run_sequence == 1U &&
                        reset.committed_epoch == 3U &&
                        reset.committed_tick == 2 &&
                        creation.session->active_run_id() == nullptr &&
                        creation.session->run_sequence().has_value() &&
                        *creation.session->run_sequence() == 0U &&
                        creation.session->committed_epoch() == 3U &&
                        creation.session->committed_tick() == 2 &&
                        creation.session->committed_step_count() == 3U &&
                        exactly_same(committed_probe(*creation.session,
                                                     adapter),
                                     before_state) &&
                        same_state_blocks(creation.session->state_blocks(),
                                          before_blocks) &&
                        same_histories(
                            creation.session->committed_histories(),
                            before_histories) &&
                        same_outputs(creation.session->committed_outputs(),
                                     before_outputs) &&
                        exactly_same(mission_result_probe(*creation.session,
                                                          adapter),
                                     before_result) &&
                        creation.session->run_outcome_for_sequence(0U) ==
                            first_outcome &&
                        exactly_same(*first_outcome, first_outcome_copy) &&
                        failed_outcome != nullptr &&
                        failed_outcome != first_outcome &&
                        failed_outcome->run_sequence == 1U &&
                        failed_outcome->run_start_kind ==
                            gnc::kernel::RunStartKind::Reset &&
                        !failed_outcome->run_start_committed &&
                        failed_outcome->final_status ==
                            gnc::kernel::RunFinalStatus::Failed &&
                        failed_outcome->validity ==
                            gnc::contracts::EvidenceValidity::Unknown &&
                        failed_outcome->primary_diagnostic.has_value() &&
                        failed_outcome->finalization_status ==
                            gnc::kernel::RunFinalizationStatus::NotStarted &&
                        creation.session->run_outcome_for_sequence(1U) ==
                            failed_outcome,
                    "reset allocation failure allocated while freezing or changed committed evidence");
            require(creation.session->dispose() &&
                        creation.session->state() == SessionState::Disposed &&
                        creation.session->preparation_count() == 0U &&
                        creation.session->runtime_cell_count() == 0U &&
                        creation.session->committed_state_count() == 0U &&
                        adapter.trace->live_object_count() == 0U,
                    "failed reset allocation pass did not dispose cleanly");
        }
        creation.session.reset();
        require(adapter.trace->live_object_count() == 0U,
                "reset allocation sweep leaked a Session object");
    }
    require(observed_outcome_staging_failure &&
                observed_history_staging_failure && observed_success,
            "reset allocation sweep missed outcome/history staging or success");
}

void verify_checkpoint_restore_allocation_failure_atomicity(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto source_adapter =
        gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(source_adapter), source_adapter.error);
    source_adapter.trace->events.reserve(4096U);
    const auto source_trace = source_adapter.trace;
    auto source_creation = gnc::kernel::create_session(
        image, source_adapter.provider);
    require(source_creation &&
                source_creation.session->initialize(
                    initialization_request(
                        *image, "run:checkpoint-allocation-parent")) &&
                source_creation.session->execute_step(),
            "checkpoint allocation fixture did not reach tick one");
    const auto state_before = committed_probe(
        *source_creation.session, source_adapter);
    const auto blocks_before = source_creation.session->state_blocks();
    const auto histories_before =
        source_creation.session->committed_histories();
    const auto outputs_before =
        source_creation.session->committed_outputs();
    const auto live_before = source_trace->live_object_count();

    allocation_fault::arm(0);
    const auto failed_checkpoint =
        source_creation.session->checkpoint();
    const auto checkpoint_post_failure_allocations =
        allocation_fault::post_failure_allocation_count();
    allocation_fault::disarm();
    require(!failed_checkpoint &&
                failed_checkpoint.result.error ==
                    SessionError::AllocationFailure &&
                failed_checkpoint.primary_diagnostic.has_value() &&
                failed_checkpoint.primary_diagnostic->code ==
                    gnc::kernel::RuntimeDiagnosticCode::AllocationFailed &&
                failed_checkpoint.primary_diagnostic->stage ==
                    gnc::kernel::RuntimeDiagnosticStage::CheckpointClone &&
                checkpoint_post_failure_allocations == 0U &&
                source_creation.session->state() ==
                    SessionState::Initialized &&
                source_creation.session->committed_epoch() == 1U &&
                source_creation.session->committed_tick() == 1 &&
                source_creation.session->committed_step_count() == 1U &&
                exactly_same(committed_probe(*source_creation.session,
                                             source_adapter),
                             state_before) &&
                same_state_blocks(source_creation.session->state_blocks(),
                                  blocks_before) &&
                same_histories(
                    source_creation.session->committed_histories(),
                    histories_before) &&
                same_outputs(source_creation.session->committed_outputs(),
                             outputs_before) &&
                source_trace->live_object_count() == live_before,
            "checkpoint allocation failure changed the committed boundary or allocated while reporting failure");

    auto checkpoint = source_creation.session->checkpoint().checkpoint;
    require(checkpoint != nullptr,
            "checkpoint allocation recovery did not publish a checkpoint");

    auto target_adapter =
        gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(target_adapter), target_adapter.error);
    target_adapter.trace->events.reserve(4096U);
    auto target_creation = gnc::kernel::create_session(
        image, target_adapter.provider);
    require(static_cast<bool>(target_creation),
            "restore allocation target creation failed");
    gnc::kernel::RestoreRequest request{
        gnc::kernel::RunId("run:restore-allocation-child"),
        gnc::kernel::exact_run_binding(*image), checkpoint};
    allocation_fault::arm(0);
    const auto failed_restore =
        target_creation.session->restore(std::move(request));
    const auto restore_post_failure_allocations =
        allocation_fault::post_failure_allocation_count();
    allocation_fault::disarm();
    const auto* failed_outcome = target_creation.session->run_outcome();
    require(!failed_restore &&
                failed_restore.result.error ==
                    SessionError::AllocationFailure &&
                failed_restore.primary_diagnostic.has_value() &&
                failed_restore.primary_diagnostic->code ==
                    gnc::kernel::RuntimeDiagnosticCode::AllocationFailed &&
                restore_post_failure_allocations == 0U &&
                target_creation.session->state() == SessionState::Failed &&
                target_creation.session->active_run_id() == nullptr &&
                !target_creation.session->run_sequence().has_value() &&
                target_creation.session->committed_epoch() == 0U &&
                target_creation.session->committed_tick() == 0 &&
                target_creation.session->committed_step_count() == 0U &&
                target_creation.session->preparation_count() == 0U &&
                target_creation.session->runtime_cell_count() == 0U &&
                target_creation.session->committed_state_count() == 0U &&
                target_creation.session->committed_histories().empty() &&
                target_creation.session->committed_outputs().empty() &&
                target_creation.session->last_restore_checkpoint() ==
                    checkpoint.get() &&
                target_creation.session->restore_lineage() == nullptr &&
                failed_outcome != nullptr &&
                failed_outcome->run_start_kind ==
                    gnc::kernel::RunStartKind::RestoreBranch &&
                !failed_outcome->run_start_committed &&
                failed_outcome->final_status ==
                    gnc::kernel::RunFinalStatus::Failed &&
                failed_outcome->primary_diagnostic.has_value() &&
                target_adapter.trace->live_object_count() == 0U,
            "restore allocation failure published partial state or allocated while freezing failure");

    require(static_cast<bool>(target_creation.session->dispose()),
            "failed restore allocation target did not dispose");
    target_creation.session.reset();
    source_creation.session.reset();
    checkpoint.reset();
    require(target_adapter.trace->live_object_count() == 0U &&
                source_trace->live_object_count() == 0U,
            "checkpoint/restore allocation test leaked typed objects");
}

void run() {
    const auto image = build_deterministic_image();
    require(image->preparations().size() == 3U &&
                image->runtime_components().size() == 7U &&
                image->state_blocks().size() == 2U &&
                image->storage_layouts().size() == 5U,
            "REF-YYZ Image shape changed before Session consumption");
    verify_creation_diagnostics(image);
    verify_created_and_initialized(image);
    verify_failure_unwind(image, FailurePhase::Preparation, 1U,
                          SessionError::PreparationFailed);
    verify_failure_unwind(image, FailurePhase::RuntimeCell, 3U,
                          SessionError::RuntimeCellFailed);
    verify_failure_unwind(image, FailurePhase::InitialState, 1U,
                          SessionError::InitialStateFailed);
    verify_metadata_failures(image);
    verify_materializer_identity_failures(image);
    verify_allocation_failure_unwind(image);
    verify_reset_allocation_failure_atomicity(image);
    verify_checkpoint_restore_allocation_failure_atomicity(image);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_session_materialization_probe "
                     "--self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 Image-backed Session materialization: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 Image-backed Session materialization: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
