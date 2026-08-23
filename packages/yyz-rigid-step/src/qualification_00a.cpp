#include "../include/yyz/qualification_00a.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gnc::packages::yyz {
namespace {

using gnc::foundation::Mat3;
using gnc::foundation::NumericalEvidence;
using gnc::foundation::NumericalOutcome;
using gnc::foundation::NumericalStatus;
using gnc::foundation::QuaternionNormalizationPolicy;
using gnc::foundation::QuaternionPolicy;
using gnc::foundation::Vec3;

constexpr double kCanonicalLatitudeDegrees = 31.2304;
constexpr double kCanonicalLongitudeDegrees = 121.4737;

[[nodiscard]] NumericalEvidence mapping_evidence(
    std::string_view detail) {
    NumericalEvidence evidence;
    evidence.algorithm = kCanonical00AInitialMappingIdentity;
    evidence.detail = detail;
    return evidence;
}

[[nodiscard]] NumericalOutcome<Canonical00AProductProfile>
mapping_failure(NumericalStatus status, std::string_view detail) {
    return NumericalOutcome<Canonical00AProductProfile>::failure(
        status, mapping_evidence(detail));
}

[[nodiscard]] bool finite_input(
    const Canonical00AAuthorInput& input) noexcept {
    return std::isfinite(input.latitude_degrees) &&
           std::isfinite(input.longitude_degrees) &&
           std::isfinite(input.altitude_meters) &&
           std::isfinite(input.speed_meters_per_second) &&
           std::isfinite(input.heading_degrees) &&
           std::isfinite(input.flight_path_angle_degrees) &&
           std::isfinite(input.bank_degrees) &&
           std::isfinite(input.initial_mass_kilograms) &&
           std::isfinite(input.altitude_command_meters) &&
           std::isfinite(input.duration_seconds);
}

[[nodiscard]] bool finite_matrix(const Mat3& value) noexcept {
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (!std::isfinite(value(row, column))) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] QuaternionPolicy strict_quaternion_policy() {
    QuaternionPolicy policy;
    policy.numerical.absolute_tolerance = 2.0e-12;
    policy.numerical.relative_tolerance = 2.0e-12;
    policy.numerical.finite_check =
        gnc::foundation::FiniteCheck::EveryStage;
    policy.numerical.zero_tolerance = 1.0e-14;
    policy.numerical.condition_limit = 1.0e12;
    policy.normalization = QuaternionNormalizationPolicy::Error;
    return policy;
}

[[nodiscard]] bool mapped_matrix_matches(const Mat3& actual,
                                         const Mat3& expected) noexcept {
    constexpr double tolerance = 8.0e-15;
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (std::abs(actual(row, column) - expected(row, column)) >
                tolerance) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool valid_rate(std::int64_t base_rate,
                              std::int64_t component_rate) noexcept {
    return component_rate > 0 && component_rate <= base_rate &&
           base_rate % component_rate == 0;
}

} // namespace

Canonical00AInitialMappingDefinition
canonical_00a_initial_mapping_definition() {
    Canonical00AInitialMappingDefinition definition;
    definition.launch_local_enu_frame =
        {kCanonical00ALaunchLocalEnuFrameId};
    definition.body_frame = {kCanonical00ABodyFrameId};
    definition.launch_anchor_latitude_degrees =
        kCanonicalLatitudeDegrees;
    definition.launch_anchor_longitude_degrees =
        kCanonicalLongitudeDegrees;
    definition.launch_anchor_datum_altitude_meters = 0.0;
    return definition;
}

Canonical00AAuthorInput canonical_00a_author_input() {
    Canonical00AAuthorInput input;
    input.latitude_degrees = kCanonicalLatitudeDegrees;
    input.longitude_degrees = kCanonicalLongitudeDegrees;
    input.altitude_meters = 1000.0;
    input.speed_meters_per_second = 220.0;
    input.heading_degrees = 90.0;
    input.flight_path_angle_degrees = 0.0;
    input.bank_degrees = 0.0;
    input.initial_mass_kilograms = 680.0;
    input.altitude_command_meters = 1000.0;
    input.duration_seconds = 30.0;
    input.base_rate_hertz = 100;
    input.navigation_rate_hertz = 100;
    input.guidance_rate_hertz = 20;
    input.controller_rate_hertz = 50;
    input.actuator_rate_hertz = 100;
    input.observation_rate_hertz = 25;
    return input;
}

NumericalOutcome<Canonical00AProductProfile>
Canonical00AInitialMappingQuery::evaluate(
    const Canonical00AInitialMappingDefinition& definition,
    const Canonical00AAuthorInput& input) {
    if (definition.launch_local_enu_frame.id !=
            kCanonical00ALaunchLocalEnuFrameId ||
        definition.body_frame.id != kCanonical00ABodyFrameId ||
        definition.launch_anchor_latitude_degrees !=
            kCanonicalLatitudeDegrees ||
        definition.launch_anchor_longitude_degrees !=
            kCanonicalLongitudeDegrees ||
        definition.launch_anchor_datum_altitude_meters != 0.0 ||
        definition.inertial_axes !=
            Canonical00AInertialAxisConvention::EastNorthUp ||
        definition.body_axes !=
            Canonical00ABodyAxisConvention::ForwardRightDown ||
        definition.heading_convention !=
            Canonical00AHeadingConvention::ClockwiseFromNorth ||
        !definition.flight_path_angle_positive_up ||
        !definition.bank_positive_right_wing_down) {
        return mapping_failure(NumericalStatus::DomainError,
                               "00a-definition");
    }
    if (!finite_input(input)) {
        return mapping_failure(NumericalStatus::NonFiniteInput,
                               "author-input");
    }
    if (input.latitude_degrees !=
            definition.launch_anchor_latitude_degrees ||
        input.longitude_degrees !=
            definition.launch_anchor_longitude_degrees) {
        return mapping_failure(NumericalStatus::DomainError,
                               "launch-anchor-only");
    }
    if (input.speed_meters_per_second < 0.0 ||
        input.heading_degrees < 0.0 || input.heading_degrees >= 360.0 ||
        input.flight_path_angle_degrees < -90.0 ||
        input.flight_path_angle_degrees > 90.0 ||
        input.bank_degrees < -180.0 || input.bank_degrees > 180.0 ||
        input.initial_mass_kilograms <= 0.0 ||
        input.duration_seconds <= 0.0 || input.base_rate_hertz <= 0 ||
        !valid_rate(input.base_rate_hertz,
                    input.navigation_rate_hertz) ||
        !valid_rate(input.base_rate_hertz,
                    input.guidance_rate_hertz) ||
        !valid_rate(input.base_rate_hertz,
                    input.controller_rate_hertz) ||
        !valid_rate(input.base_rate_hertz,
                    input.actuator_rate_hertz) ||
        !valid_rate(input.base_rate_hertz,
                    input.observation_rate_hertz)) {
        return mapping_failure(NumericalStatus::DomainError,
                               "author-domain");
    }

    const double terminal_tick_real =
        input.duration_seconds *
        static_cast<double>(input.base_rate_hertz);
    if (!std::isfinite(terminal_tick_real) || terminal_tick_real < 1.0 ||
        terminal_tick_real >
            static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return mapping_failure(NumericalStatus::DomainError,
                               "duration-base-clock");
    }
    const auto terminal_tick =
        static_cast<std::int64_t>(std::llround(terminal_tick_real));
    if (static_cast<double>(terminal_tick) != terminal_tick_real) {
        return mapping_failure(NumericalStatus::DomainError,
                               "duration-base-clock");
    }

    const double radians_per_degree = std::acos(-1.0) / 180.0;
    const double heading = input.heading_degrees * radians_per_degree;
    const double gamma =
        input.flight_path_angle_degrees * radians_per_degree;
    const double bank = input.bank_degrees * radians_per_degree;
    const double cos_heading = std::cos(heading);
    const double sin_heading = std::sin(heading);
    const double cos_gamma = std::cos(gamma);
    const double sin_gamma = std::sin(gamma);
    const double cos_bank = std::cos(bank);
    const double sin_bank = std::sin(bank);

    const Vec3 forward{cos_gamma * sin_heading,
                       cos_gamma * cos_heading, sin_gamma};
    const Vec3 zero_bank_right{cos_heading, -sin_heading, 0.0};
    const Vec3 zero_bank_down = forward.cross(zero_bank_right);
    const Vec3 right = cos_bank * zero_bank_right +
                       sin_bank * zero_bank_down;
    const Vec3 down = -sin_bank * zero_bank_right +
                      cos_bank * zero_bank_down;

    Mat3 body_to_enu;
    body_to_enu.col(0) = forward;
    body_to_enu.col(1) = right;
    body_to_enu.col(2) = down;
    if (!finite_matrix(body_to_enu) ||
        std::abs(body_to_enu.determinant() - 1.0) > 8.0e-15) {
        return mapping_failure(NumericalStatus::NonFiniteIntermediate,
                               "body-basis");
    }

    // Eigen constructs an active matrix from a quaternion. The repository's
    // passive matrix is its transpose, so conversion starts from the
    // transposed body-to-ENU basis and never assumes a quaternion sign.
    gnc::foundation::QuaternionStorage attitude(body_to_enu.transpose());
    const double attitude_norm = attitude.norm();
    if (!std::isfinite(attitude_norm) || attitude_norm <= 1.0e-14) {
        return mapping_failure(NumericalStatus::NonFiniteIntermediate,
                               "attitude-conversion");
    }
    attitude.coeffs() /= attitude_norm;
    const auto recovered = gnc::foundation::passive_rotation_matrix(
        attitude, strict_quaternion_policy());
    if (!recovered.has_value() ||
        !mapped_matrix_matches(recovered.value(), body_to_enu)) {
        return mapping_failure(NumericalStatus::InternalFailure,
                               "attitude-basis-roundtrip");
    }

    Canonical00AProductProfile output;
    output.inertial_frame = definition.launch_local_enu_frame;
    output.body_frame = definition.body_frame;
    output.initial_rigid_state.position.value =
        Vec3{0.0, 0.0,
             input.altitude_meters -
                 definition.launch_anchor_datum_altitude_meters};
    output.initial_rigid_state.velocity.value =
        input.speed_meters_per_second * forward;
    output.initial_rigid_state.attitude.value = attitude;
    output.initial_rigid_state.angular_rate.value.setZero();
    output.initial_mass_kilograms = input.initial_mass_kilograms;
    output.altitude_command_meters = input.altitude_command_meters;
    output.fixed_step_seconds =
        1.0 / static_cast<double>(input.base_rate_hertz);
    output.terminal_tick = terminal_tick;
    output.navigation_interval_ticks =
        input.base_rate_hertz / input.navigation_rate_hertz;
    output.guidance_interval_ticks =
        input.base_rate_hertz / input.guidance_rate_hertz;
    output.controller_interval_ticks =
        input.base_rate_hertz / input.controller_rate_hertz;
    output.actuator_interval_ticks =
        input.base_rate_hertz / input.actuator_rate_hertz;
    output.observation_interval_ticks =
        input.base_rate_hertz / input.observation_rate_hertz;

    NumericalEvidence evidence = mapping_evidence(
        "00a-launch-local-enu-product-profile");
    evidence.evaluations = 3U;
    evidence.residual_norm =
        (recovered.value() - body_to_enu).cwiseAbs().maxCoeff();
    evidence.last_step = output.fixed_step_seconds;
    return NumericalOutcome<Canonical00AProductProfile>::with_value(
        NumericalStatus::Success, std::move(output), evidence);
}

} // namespace gnc::packages::yyz
