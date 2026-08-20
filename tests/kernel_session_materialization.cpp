#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <algorithm>
#include <cstdint>
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
using gnc::kernel::SessionState;
using gnc::tests::ref_yyz::AdapterOptions;
using gnc::tests::ref_yyz::FailurePhase;
using gnc::tests::ref_yyz::TraceObjectKind;

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
    for (const auto handle : image->lifecycle().preparation_handles) {
        require(creation.session->prepared_object(handle) != nullptr,
                "prepared object is not inspectable by Image handle");
    }
    for (const auto handle : image->lifecycle().runtime_component_handles) {
        require(creation.session->runtime_cell_object(handle) != nullptr,
                "Runtime Cell is not inspectable by Image handle");
    }
    for (const auto& block : image->state_blocks()) {
        require(creation.session->committed_state_object(block.handle) !=
                        nullptr &&
                    creation.session->candidate_state_object(block.handle) !=
                        nullptr,
                "state objects are not inspectable by Image handle");
    }

    const auto extents = creation.session->storage_extents();
    require(extents.size() == image->storage_layouts().size(),
            "Session did not allocate every Image storage extent");
    for (const auto& layout : image->storage_layouts()) {
        const auto found = std::find_if(
            extents.begin(), extents.end(), [&layout](const auto& extent) {
                return extent.layout_handle == layout.handle;
            });
        require(found != extents.end() && found->address != nullptr &&
                    found->size_bytes == layout.size_bytes &&
                    found->alignment_bytes == layout.alignment_bytes &&
                    reinterpret_cast<std::uintptr_t>(found->address) %
                            layout.alignment_bytes ==
                        0U,
                "Session storage allocation differs from Image extent");
    }

    const auto non_trivial =
        gnc::tests::ref_yyz::exercise_non_trivial_objects(
            *creation.session, adapter);
    require(non_trivial.state_is_non_trivial &&
                non_trivial.output_is_non_trivial &&
                non_trivial.initial_mass_string_present &&
                non_trivial.state_clone_restored_string &&
                non_trivial.first_output_replace_succeeded &&
                non_trivial.second_output_replace_succeeded &&
                non_trivial.output_string_replaced,
            "non-trivial YYZ state/output placement or replacement failed");

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
                    SessionState::InitializationFailed,
            message);
}

void verify_metadata_failures(
    const std::shared_ptr<const ExecutionPlanImage>& image) {
    auto adapter = gnc::tests::ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    require_metadata_failure(
        image, adapter.provider,
        [](auto& data) { ++data.state_blocks.front().size_bytes; },
        SessionError::ObjectSizeMismatch,
        "wrong state size did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider,
        [](auto& data) { data.state_blocks.front().alignment_bytes *= 2U; },
        SessionError::ObjectAlignmentMismatch,
        "wrong state alignment did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider,
        [](auto& data) { data.state_blocks.front().layout_id += ".wrong"; },
        SessionError::ObjectLayoutMismatch,
        "wrong state layout did not fail deterministically");
    require_metadata_failure(
        image, adapter.provider,
        [](auto& data) { data.state_blocks.front().codec_entry_handle += 1000U; },
        SessionError::ObjectCodecMismatch,
        "wrong state codec did not fail deterministically");

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
                result.image_handle == missing.first_non_state_slot_handle,
            "missing slot materializer did not fail deterministically");
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
