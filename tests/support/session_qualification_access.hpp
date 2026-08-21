#pragma once

#include "gnc/kernel/session.hpp"

namespace gnc::kernel::qualification {

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
};

} // namespace gnc::kernel::qualification
