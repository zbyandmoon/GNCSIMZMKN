#pragma once

#include <cstdint>
#include <string_view>

namespace gnc::contracts {

enum class TemporalRelation : std::uint8_t {
    NotApplicable,
    IntervalModel,
    CandidateStateQuery,
    CurrentCycle,
};

[[nodiscard]] constexpr std::string_view to_string(
    TemporalRelation relation) noexcept {
    switch (relation) {
    case TemporalRelation::NotApplicable:
        return "NotApplicable";
    case TemporalRelation::IntervalModel:
        return "IntervalModel";
    case TemporalRelation::CandidateStateQuery:
        return "CandidateStateQuery";
    case TemporalRelation::CurrentCycle:
        return "CurrentCycle";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_temporal_relation(
    TemporalRelation relation) noexcept {
    return relation == TemporalRelation::NotApplicable ||
           relation == TemporalRelation::IntervalModel ||
           relation == TemporalRelation::CandidateStateQuery ||
           relation == TemporalRelation::CurrentCycle;
}

enum class ExecutionObligation : std::uint8_t {
    // BoundaryEvaluation was the first published R2 value. Keep its numeric
    // value stable and append the owner-specific obligations.
    BoundaryEvaluation = 0U,
    PublishProjection = 1U,
    IntervalEvolution,
    DerivativeEvaluation,
};

[[nodiscard]] constexpr std::string_view to_string(
    ExecutionObligation obligation) noexcept {
    switch (obligation) {
    case ExecutionObligation::PublishProjection:
        return "PublishProjection";
    case ExecutionObligation::BoundaryEvaluation:
        return "BoundaryEvaluation";
    case ExecutionObligation::IntervalEvolution:
        return "IntervalEvolution";
    case ExecutionObligation::DerivativeEvaluation:
        return "DerivativeEvaluation";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_execution_obligation(
    ExecutionObligation obligation) noexcept {
    return obligation == ExecutionObligation::PublishProjection ||
           obligation == ExecutionObligation::BoundaryEvaluation ||
           obligation == ExecutionObligation::IntervalEvolution ||
           obligation == ExecutionObligation::DerivativeEvaluation;
}

// Shared continuous-closure strategy authority. Individual Compiler slices
// still admit only strategies backed by their selected product definitions.
enum class ClosureStrategy : std::uint8_t {
    Unspecified,
    FrozenInterval,
    CandidateState,
    AlgebraicSolve,
};

[[nodiscard]] constexpr std::string_view to_string(
    ClosureStrategy strategy) noexcept {
    switch (strategy) {
    case ClosureStrategy::Unspecified:
        return "Unspecified";
    case ClosureStrategy::FrozenInterval:
        return "FrozenInterval";
    case ClosureStrategy::CandidateState:
        return "CandidateState";
    case ClosureStrategy::AlgebraicSolve:
        return "AlgebraicSolve";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_closure_strategy(
    ClosureStrategy strategy) noexcept {
    return strategy == ClosureStrategy::FrozenInterval ||
           strategy == ClosureStrategy::CandidateState ||
           strategy == ClosureStrategy::AlgebraicSolve;
}

// An invocation result is either consumed directly by the authorized typed
// caller or written once into coordinator-owned interval storage. PureQuery
// responses are never CycleFrame values merely because a logical Binding
// names their consumer.
enum class InvocationResultRoute : std::uint8_t {
    Unspecified,
    CallerLocal,
    HeldInterval,
};

[[nodiscard]] constexpr std::string_view to_string(
    InvocationResultRoute route) noexcept {
    switch (route) {
    case InvocationResultRoute::Unspecified:
        return "Unspecified";
    case InvocationResultRoute::CallerLocal:
        return "CallerLocal";
    case InvocationResultRoute::HeldInterval:
        return "HeldInterval";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool valid_invocation_result_route(
    InvocationResultRoute route) noexcept {
    return route == InvocationResultRoute::CallerLocal ||
           route == InvocationResultRoute::HeldInterval;
}

enum class SlotStorageClass : std::uint8_t {
    Unspecified,
    CycleFrame,
    StateStore,
    IntegrationHeld,
    TerminalResult,
};

[[nodiscard]] constexpr std::string_view to_string(
    SlotStorageClass storage) noexcept {
    switch (storage) {
    case SlotStorageClass::Unspecified:
        return "Unspecified";
    case SlotStorageClass::CycleFrame:
        return "CycleFrame";
    case SlotStorageClass::StateStore:
        return "StateStore";
    case SlotStorageClass::IntegrationHeld:
        return "IntegrationHeld";
    case SlotStorageClass::TerminalResult:
        return "TerminalResult";
    }
    return "Unknown";
}

enum class SlotHoldPolicy : std::uint8_t {
    Unspecified,
    CurrentBoundary,
    HoldInterval,
    Committed,
    Terminal,
};

[[nodiscard]] constexpr std::string_view to_string(
    SlotHoldPolicy hold) noexcept {
    switch (hold) {
    case SlotHoldPolicy::Unspecified:
        return "Unspecified";
    case SlotHoldPolicy::CurrentBoundary:
        return "CurrentBoundary";
    case SlotHoldPolicy::HoldInterval:
        return "HoldInterval";
    case SlotHoldPolicy::Committed:
        return "Committed";
    case SlotHoldPolicy::Terminal:
        return "Terminal";
    }
    return "Unknown";
}

enum class PreparationOwnership : std::uint8_t {
    Unspecified,
    SessionOwned,
};

enum class PreparationPhase : std::uint8_t {
    Unspecified,
    InitializeTime,
};

enum class PreparedModelCachePolicy : std::uint8_t {
    Unspecified,
    NoSharedCache,
};

[[nodiscard]] constexpr std::string_view to_string(
    PreparationOwnership value) noexcept {
    return value == PreparationOwnership::SessionOwned ? "SessionOwned"
                                                       : "Unspecified";
}

[[nodiscard]] constexpr std::string_view to_string(
    PreparationPhase value) noexcept {
    return value == PreparationPhase::InitializeTime ? "InitializeTime"
                                                     : "Unspecified";
}

[[nodiscard]] constexpr std::string_view to_string(
    PreparedModelCachePolicy value) noexcept {
    return value == PreparedModelCachePolicy::NoSharedCache
               ? "NoSharedCache"
               : "Unspecified";
}

enum class TransactionBranch : std::uint8_t {
    Continue,
    Terminal,
    Failure,
};

[[nodiscard]] constexpr std::string_view to_string(
    TransactionBranch branch) noexcept {
    switch (branch) {
    case TransactionBranch::Continue:
        return "Continue";
    case TransactionBranch::Terminal:
        return "Terminal";
    case TransactionBranch::Failure:
        return "Failure";
    }
    return "Unknown";
}

} // namespace gnc::contracts
