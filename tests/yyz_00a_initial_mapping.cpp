#include <yyz/qualification_00a.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gnc::packages::yyz;
using gnc::foundation::FiniteCheck;
using gnc::foundation::NumericalFlag;
using gnc::foundation::NumericalStatus;
using gnc::foundation::QuaternionNormalizationPolicy;
using gnc::foundation::QuaternionPolicy;
using gnc::foundation::Vec3;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] bool near(double lhs, double rhs,
                        double tolerance = 2.0e-13) {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool near(const Vec3& lhs, const Vec3& rhs,
                        double tolerance = 2.0e-13) {
    return (lhs - rhs).cwiseAbs().maxCoeff() <= tolerance;
}

[[nodiscard]] QuaternionPolicy quaternion_policy(
    QuaternionNormalizationPolicy normalization) {
    QuaternionPolicy policy;
    policy.numerical.absolute_tolerance = 2.0e-12;
    policy.numerical.relative_tolerance = 2.0e-12;
    policy.numerical.finite_check = FiniteCheck::EveryStage;
    policy.numerical.zero_tolerance = 1.0e-14;
    policy.numerical.condition_limit = 1.0e12;
    policy.normalization = normalization;
    return policy;
}

void verify_canonical_mapping() {
    const auto definition = canonical_00a_initial_mapping_definition();
    const auto input = canonical_00a_author_input();
    const auto mapped = Canonical00AInitialMappingQuery::evaluate(
        definition, input);
    require(mapped.has_value() && mapped.status() == NumericalStatus::Success,
            "canonical 00A mapping failed");
    const auto& output = mapped.value();
    require(output.inertial_frame.id ==
                kCanonical00ALaunchLocalEnuFrameId &&
                output.body_frame.id == kCanonical00ABodyFrameId &&
                near(output.initial_rigid_state.position.value,
                     Vec3{0.0, 0.0, 1000.0}) &&
                near(output.initial_rigid_state.velocity.value,
                     Vec3{220.0, 0.0, 0.0}) &&
                output.initial_mass_kilograms == 680.0 &&
                output.altitude_command_meters == 1000.0 &&
                output.fixed_step_seconds == 0.01 &&
                output.terminal_tick == 3000 &&
                output.navigation_interval_ticks == 1 &&
                output.guidance_interval_ticks == 5 &&
                output.controller_interval_ticks == 2 &&
                output.actuator_interval_ticks == 1 &&
                output.observation_interval_ticks == 4,
            "canonical author facts did not reach product profile");

    const auto matrix = gnc::foundation::passive_rotation_matrix(
        output.initial_rigid_state.attitude.value,
        quaternion_policy(QuaternionNormalizationPolicy::Error));
    require(matrix.has_value() &&
                near(matrix.value() * Vec3::UnitX(), Vec3::UnitX()) &&
                near(matrix.value() * Vec3::UnitY(), -Vec3::UnitY()) &&
                near(matrix.value() * Vec3::UnitZ(), -Vec3::UnitZ()),
            "canonical passive attitude did not map forward/right/down to east/south/down");

    auto double_cover = output.initial_rigid_state.attitude.value;
    double_cover.coeffs() *= -1.0;
    const auto double_cover_matrix =
        gnc::foundation::passive_rotation_matrix(
            double_cover,
            quaternion_policy(QuaternionNormalizationPolicy::Error));
    const auto double_cover_error =
        gnc::foundation::passive_quaternion_orientation_error(
            output.initial_rigid_state.attitude.value, double_cover,
            quaternion_policy(QuaternionNormalizationPolicy::Error));
    require(double_cover_matrix.has_value() &&
                near((double_cover_matrix.value() - matrix.value())
                         .cwiseAbs()
                         .maxCoeff(),
                     0.0) &&
                double_cover_error.has_value() &&
                near(double_cover_error.value(), 0.0),
            "quaternion double cover changed the mapped attitude");

    auto nonunit = output.initial_rigid_state.attitude.value;
    nonunit.coeffs() *= 2.0;
    const auto strict_nonunit = gnc::foundation::passive_rotation_matrix(
        nonunit, quaternion_policy(QuaternionNormalizationPolicy::Error));
    const auto normalized_nonunit =
        gnc::foundation::passive_rotation_matrix(
            nonunit,
            quaternion_policy(
                QuaternionNormalizationPolicy::NormalizeWithFlag));
    require(!strict_nonunit.has_value() &&
                strict_nonunit.status() == NumericalStatus::DomainError &&
                normalized_nonunit.has_value() &&
                gnc::foundation::has_numerical_flag(
                    normalized_nonunit.evidence().flags,
                    NumericalFlag::Normalized) &&
                near((normalized_nonunit.value() - matrix.value())
                         .cwiseAbs()
                         .maxCoeff(),
                     0.0),
            "nonunit quaternion strategy was not explicit");
}

void verify_fail_closed_domain() {
    const auto definition = canonical_00a_initial_mapping_definition();
    auto input = canonical_00a_author_input();
    input.latitude_degrees += 1.0e-9;
    const auto latitude = Canonical00AInitialMappingQuery::evaluate(
        definition, input);
    require(!latitude.has_value() &&
                latitude.status() == NumericalStatus::DomainError &&
                latitude.evidence().detail == "launch-anchor-only",
            "non-00A latitude did not fail closed");

    input = canonical_00a_author_input();
    input.longitude_degrees -= 1.0e-9;
    const auto longitude = Canonical00AInitialMappingQuery::evaluate(
        definition, input);
    require(!longitude.has_value() &&
                longitude.status() == NumericalStatus::DomainError &&
                longitude.evidence().detail == "launch-anchor-only",
            "non-00A longitude did not fail closed");

    input = canonical_00a_author_input();
    input.guidance_rate_hertz = 30;
    const auto cadence = Canonical00AInitialMappingQuery::evaluate(
        definition, input);
    require(!cadence.has_value() &&
                cadence.status() == NumericalStatus::DomainError &&
                cadence.evidence().detail == "author-domain",
            "non-integral component cadence reached product output");

    auto mutated_definition = definition;
    mutated_definition.launch_local_enu_frame.id =
        "frame.yyz.somewhere-else@1";
    const auto other_definition =
        Canonical00AInitialMappingQuery::evaluate(mutated_definition,
                                                  canonical_00a_author_input());
    require(!other_definition.has_value() &&
                other_definition.status() == NumericalStatus::DomainError &&
                other_definition.evidence().detail == "00a-definition",
            "generic launch frame mutation bypassed 00A definition lock");
}

} // namespace

int main() {
    try {
        verify_canonical_mapping();
        verify_fail_closed_domain();
        std::cout << "yyz-00a-initial-mapping: 2 checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "yyz-00a-initial-mapping: " << error.what() << '\n';
        return 1;
    }
}
