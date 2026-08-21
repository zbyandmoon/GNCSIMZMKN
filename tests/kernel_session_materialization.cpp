#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
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

void arm(std::int64_t allocations_before_failure) noexcept {
    fail_after = allocations_before_failure;
}

void disarm() noexcept { fail_after = -1; }

[[nodiscard]] bool should_fail() noexcept {
    if (fail_after < 0) return false;
    if (fail_after == 0) {
        fail_after = -1;
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
using gnc::tests::ref_yyz::FailurePhase;
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

    const auto initialized = creation.session->initialize();
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

void verify_failure_unwind(
    const std::shared_ptr<const ExecutionPlanImage>& image,
    FailurePhase phase, std::size_t ordinal, SessionError expected) {
    AdapterOptions options;
    options.failure = {phase, ordinal};
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image, options);
    require(static_cast<bool>(adapter), adapter.error);
    auto creation = gnc::kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(creation), "failure Session creation failed");
    const auto result = creation.session->initialize();
    require(!static_cast<bool>(result) && result.error == expected &&
                creation.session->state() ==
                    SessionState::InitializationFailed &&
                creation.session->preparation_count() == 0U &&
                creation.session->runtime_cell_count() == 0U &&
                creation.session->committed_state_count() == 0U &&
                creation.session->storage_extents().empty() &&
                adapter.trace->live_object_count() == 0U,
            "failed initialization exposed or leaked partial state");
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
                static_cast<bool>(next.session->initialize()) &&
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
    const auto result = creation.session->initialize();
    require(!static_cast<bool>(result) && result.error == expected &&
                creation.session->state() ==
                    SessionState::InitializationFailed &&
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
        [](auto& data) { ++data.revision; },
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

    AdapterOptions missing_options;
    missing_options.omit_first_slot_materializer = true;
    auto missing = gnc::tests::ref_yyz::make_session_adapter(
        *image, missing_options);
    require(static_cast<bool>(missing), missing.error);
    auto creation = gnc::kernel::create_session(image, missing.provider);
    require(static_cast<bool>(creation), "missing-provider Session create failed");
    const auto result = creation.session->initialize();
    require(!static_cast<bool>(result) &&
                result.error == SessionError::MissingMaterializer &&
                result.image_handle == missing.first_non_state_slot_handle &&
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
        const auto result = creation.session->initialize();
        require(!result && result.error == expected &&
                    creation.session->state() ==
                        SessionState::InitializationFailed &&
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
        undeclared_creation.session->initialize();
    require(!undeclared_result &&
                undeclared_result.error == SessionError::RuntimeCellFailed &&
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
    const auto result = creation.session->initialize();
    require(!result && result.error == SessionError::ObjectTypeMismatch &&
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
    bool observed_success = false;
    for (std::int64_t fail_after = 0;
         fail_after < 512 && !observed_success; ++fail_after) {
        adapter.trace->events.clear();
        auto creation = gnc::kernel::create_session(image, adapter.provider);
        require(static_cast<bool>(creation),
                "allocation-fault Session creation failed before injection");
        allocation_fault::arm(fail_after);
        const auto result = creation.session->initialize();
        allocation_fault::disarm();
        if (result) {
            observed_success = true;
            require(creation.session->state() == SessionState::Initialized,
                    "successful allocation-fault pass has wrong state");
        } else {
            require(creation.session->state() ==
                            SessionState::InitializationFailed &&
                        !result.detail.empty(),
                    "allocation failure escaped or lost stable diagnostics");
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
                observed_partial_object_failure && observed_success,
            "allocation injection did not cover reserve, placement and success");
}

void run() {
    const auto image = build_deterministic_image();
    require(image->preparations().size() == 3U &&
                image->runtime_components().size() == 7U &&
                image->state_blocks().size() == 2U &&
                image->storage_layouts().size() == 5U,
            "REF-YYZ Image shape changed before Session consumption");
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
