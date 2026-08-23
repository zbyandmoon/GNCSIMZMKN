#pragma once

#include "yyz/rigid_step.hpp"

#include <cstdint>

namespace gnc::packages::yyz {

inline constexpr gnc::foundation::AlgorithmIdentity
    kCanonical00AInitialMappingIdentity{
        "gnc.yyz.qualification-00a.launch-local-enu-mapping@1",
        "1.0.0"};

inline constexpr const char* kCanonical00ALaunchLocalEnuFrameId =
    "frame.yyz.00a.launch-local-enu@1";
inline constexpr const char* kCanonical00ABodyFrameId =
    "frame.yyz.body-forward-right-down@1";

enum class Canonical00AInertialAxisConvention : std::uint8_t {
    EastNorthUp,
};

enum class Canonical00ABodyAxisConvention : std::uint8_t {
    ForwardRightDown,
};

enum class Canonical00AHeadingConvention : std::uint8_t {
    ClockwiseFromNorth,
};

// Immutable-by-use definition for the one approved 00A launch anchor. This
// value type intentionally contains no ellipsoid or general geodesy model.
struct Canonical00AInitialMappingDefinition {
    gnc::contracts::FrameIdentity launch_local_enu_frame;
    gnc::contracts::FrameIdentity body_frame;
    double launch_anchor_latitude_degrees = 0.0;
    double launch_anchor_longitude_degrees = 0.0;
    double launch_anchor_datum_altitude_meters = 0.0;
    Canonical00AInertialAxisConvention inertial_axes =
        Canonical00AInertialAxisConvention::EastNorthUp;
    Canonical00ABodyAxisConvention body_axes =
        Canonical00ABodyAxisConvention::ForwardRightDown;
    Canonical00AHeadingConvention heading_convention =
        Canonical00AHeadingConvention::ClockwiseFromNorth;
    bool flight_path_angle_positive_up = true;
    bool bank_positive_right_wing_down = true;
};

// Author-level 00A facts. Rates are integer hertz so their exact base-clock
// divisibility is checked before interval-tick values are produced.
struct Canonical00AAuthorInput {
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
    double altitude_meters = 0.0;
    double speed_meters_per_second = 0.0;
    double heading_degrees = 0.0;
    double flight_path_angle_degrees = 0.0;
    double bank_degrees = 0.0;
    double initial_mass_kilograms = 0.0;
    double altitude_command_meters = 0.0;
    double duration_seconds = 0.0;
    std::int64_t base_rate_hertz = 0;
    std::int64_t navigation_rate_hertz = 0;
    std::int64_t guidance_rate_hertz = 0;
    std::int64_t controller_rate_hertz = 0;
    std::int64_t actuator_rate_hertz = 0;
    std::int64_t observation_rate_hertz = 0;
};

// Product-ready output consumed by the canonical MissionSource builder. The
// attitude is q_I_B under the repository's passive Hamilton convention.
struct Canonical00AProductProfile {
    gnc::contracts::FrameIdentity inertial_frame;
    gnc::contracts::FrameIdentity body_frame;
    RigidState initial_rigid_state;
    double initial_mass_kilograms = 0.0;
    double altitude_command_meters = 0.0;
    double fixed_step_seconds = 0.0;
    std::int64_t terminal_tick = 0;
    std::int64_t navigation_interval_ticks = 0;
    std::int64_t guidance_interval_ticks = 0;
    std::int64_t controller_interval_ticks = 0;
    std::int64_t actuator_interval_ticks = 0;
    std::int64_t observation_interval_ticks = 0;
};

[[nodiscard]] Canonical00AInitialMappingDefinition
canonical_00a_initial_mapping_definition();

[[nodiscard]] Canonical00AAuthorInput canonical_00a_author_input();

class Canonical00AInitialMappingQuery {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        Canonical00AProductProfile>
    evaluate(const Canonical00AInitialMappingDefinition& definition,
             const Canonical00AAuthorInput& input);
};

} // namespace gnc::packages::yyz
