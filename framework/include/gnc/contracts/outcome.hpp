#pragma once

#include <cstdint>
#include <string_view>

namespace gnc::contracts {

// Shared validity authority for in-process observations and outcomes. This
// header defines value semantics only; no persistence or wire format follows.
enum class EvidenceValidity : std::uint8_t {
    Valid,
    ValidWithCaveats,
    Partial,
    Invalid,
    Unknown,
};

[[nodiscard]] constexpr std::string_view to_string(
    EvidenceValidity value) noexcept {
    switch (value) {
    case EvidenceValidity::Valid: return "Valid";
    case EvidenceValidity::ValidWithCaveats: return "ValidWithCaveats";
    case EvidenceValidity::Partial: return "Partial";
    case EvidenceValidity::Invalid: return "Invalid";
    case EvidenceValidity::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace gnc::contracts
