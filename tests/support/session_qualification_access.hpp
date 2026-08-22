#pragma once

#include "gnc/kernel/session.hpp"

namespace gnc::kernel::qualification {

enum class CheckpointCloneFault : std::uint8_t {
    None = 0U,
    State = 1U,
    History = 2U,
    Seal = 3U,
    HeldOutput = 4U,
};

enum class CheckpointBarrierFault : std::uint8_t {
    OpenFrame = 1U,
    ActiveTransaction = 2U,
};

enum class CheckpointMutation : std::uint8_t {
    ImageFingerprint = 1U,
    RunBinding = 2U,
    StateLayout = 3U,
    StateCodec = 4U,
    StateType = 5U,
    StateInvariant = 6U,
    HeldLayout = 7U,
    HeldCodec = 8U,
    HeldType = 9U,
    HeldQuality = 10U,
    HeldAuthority = 11U,
    HeldMissing = 12U,
    HeldExtra = 13U,
};

enum class HeldOutputFault : std::uint8_t {
    None = 0U,
    StoreClone = 1U,
    InjectionClone = 2U,
    Validation = 3U,
    Precommit = 4U,
};

// This friend is compiled only by qualification probes. Product callers have
// no Session API that can obtain or replace candidate-state object storage.
class SessionAccess final {
  public:
    [[nodiscard]] static SessionResult execute_opening_boundary(
        Session& session) noexcept {
        return session.qualification_execute_opening_boundary();
    }

    [[nodiscard]] static SessionResult read_committed(
        const Session& session, std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) noexcept {
        return session.qualification_read_committed(state_block_handle,
                                                    result);
    }

    [[nodiscard]] static SessionResult read_candidate(
        const Session& session, std::uint32_t state_block_handle,
        SessionObjectIdentityView& result) noexcept {
        return session.qualification_read_candidate(state_block_handle,
                                                    result);
    }

    [[nodiscard]] static SessionResult replace_candidate(
        Session& session, std::uint32_t state_block_handle,
        InProcessValueView value) noexcept {
        return session.qualification_replace_candidate(state_block_handle,
                                                       value);
    }

    [[nodiscard]] static SessionResult read_committed_output(
        const Session& session, std::uint32_t slot_handle,
        SessionObjectIdentityView& result) noexcept {
        return session.qualification_read_committed_output(slot_handle,
                                                           result);
    }

    [[nodiscard]] static SessionResult read_history_member(
        const Session& session, std::uint32_t history_handle,
        std::size_t sample_index, std::size_t member_index,
        std::int64_t& sample_tick, std::uint64_t& committed_epoch,
        SessionObjectIdentityView& result) noexcept {
        return session.qualification_read_history_member(
            history_handle, sample_index, member_index, sample_tick,
            committed_epoch, result);
    }

    [[nodiscard]] static std::size_t command_queue_storage_count(
        const Session& session) noexcept {
        return session.qualification_command_queue_storage_count();
    }

    static void fail_next_checkpoint_clone(
        Session& session, CheckpointCloneFault fault) noexcept {
        session.qualification_set_checkpoint_clone_fault(
            static_cast<std::uint8_t>(fault));
    }

    [[nodiscard]] static CheckpointOutcome checkpoint_with_barrier(
        Session& session, CheckpointBarrierFault fault) noexcept {
        return session.qualification_checkpoint_with_barrier(
            static_cast<std::uint8_t>(fault));
    }

    static void fail_restore_precommit(Session& session) noexcept {
        session.qualification_set_restore_precommit_failure(true);
    }

    static void fail_next_held_output(
        Session& session, HeldOutputFault fault) noexcept {
        session.qualification_set_held_output_fault(
            static_cast<std::uint8_t>(fault));
    }

    static void mutate_checkpoint(
        const std::shared_ptr<const SessionCheckpoint>& checkpoint,
        CheckpointMutation mutation) noexcept {
        if (checkpoint != nullptr) {
            checkpoint->qualification_mutate(
                static_cast<std::uint8_t>(mutation));
        }
    }
};

} // namespace gnc::kernel::qualification
