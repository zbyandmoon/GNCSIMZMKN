#pragma once

#include <utility>

namespace gnc::model_sdk {

// Process-local, package-typed operation bundles. R2 exact-links the getter
// and freezes its stable witness; it never calls the getter or any operation.
// These are intentionally not serializers, registries, or cross-process ABI.
template <typename CloneCall, typename ValidateCall,
          typename FiniteValidationCall,
          typename InvariantValidationCall, typename SwapCall,
          typename ProjectCall>
struct InProcessStateCodec {
    CloneCall clone = nullptr;
    ValidateCall validate = nullptr;
    FiniteValidationCall validate_finite = nullptr;
    InvariantValidationCall validate_invariants = nullptr;
    SwapCall noexcept_swap = nullptr;
    ProjectCall project = nullptr;
};

template <typename CopyCall, typename ValidateCall, typename ProjectCall>
struct InProcessSlotCodec {
    CopyCall copy = nullptr;
    ValidateCall validate = nullptr;
    ProjectCall project = nullptr;
};

template <typename Codec>
using InProcessCodecGetter = const Codec& (*)() noexcept;

template <typename Value>
[[nodiscard]] inline Value copy_in_process_value(const Value& value) {
    return value;
}

template <typename Value>
[[nodiscard]] inline bool validate_in_process_value_type(
    const Value&) noexcept {
    return true;
}

template <typename Value>
[[nodiscard]] inline Value project_in_process_value(const Value& value) {
    return value;
}

template <typename Value>
using TypedInProcessSlotCodec = InProcessSlotCodec<
    Value (*)(const Value&), bool (*)(const Value&) noexcept,
    Value (*)(const Value&)>;

template <typename Value>
[[nodiscard]] inline const TypedInProcessSlotCodec<Value>&
typed_in_process_slot_codec() noexcept {
    static const TypedInProcessSlotCodec<Value> codec{
        &copy_in_process_value<Value>,
        &validate_in_process_value_type<Value>,
        &project_in_process_value<Value>};
    return codec;
}

} // namespace gnc::model_sdk
