#include "../include/yyz/rigid_step.hpp"

#include "gnc/foundation/fixed_rk4.hpp"
#include "gnc/foundation/spd_cholesky_3x3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gnc::packages::yyz {
namespace {

using gnc::contracts::DataQuality;
using gnc::contracts::IntervalSampleContext;
using gnc::contracts::SampleContext;
using gnc::contracts::SimulationInstant;
using gnc::foundation::FiniteCheck;
using gnc::foundation::Mat3;
using gnc::foundation::NumericalEvidence;
using gnc::foundation::NumericalFlags;
using gnc::foundation::NumericalOutcome;
using gnc::foundation::NumericalPolicy;
using gnc::foundation::NumericalStatus;
using gnc::foundation::QuaternionStorage;
using gnc::foundation::Vec3;

using StateVector = std::array<double, 13U>;

struct ValidationFailure {
    NumericalStatus status = NumericalStatus::DomainError;
    std::string_view detail;
};

struct AirLookupComputation {
    AirDataOutput air_data;
    AerodynamicLookupOutput lookup;
    AerodynamicTableQueryOutput aerodynamic_response;
    AerodynamicTableQueryEvaluation aerodynamic_query;
};

[[nodiscard]] NumericalEvidence product_evidence(
    gnc::foundation::AlgorithmIdentity algorithm, std::string_view detail,
    NumericalFlags flags = 0U) {
    NumericalEvidence evidence;
    evidence.algorithm = algorithm;
    evidence.detail = detail;
    evidence.flags = flags;
    return evidence;
}

template <typename Value>
[[nodiscard]] NumericalOutcome<Value> product_failure(
    NumericalStatus status, std::string_view detail,
    NumericalFlags flags = 0U) {
    return NumericalOutcome<Value>::failure(
        status, product_evidence(kRigidStepKernelIdentity, detail, flags));
}

template <typename Value>
[[nodiscard]] NumericalOutcome<Value> closure_failure(
    NumericalStatus status, std::string_view detail,
    NumericalFlags flags = 0U) {
    return NumericalOutcome<Value>::failure(
        status,
        product_evidence(kForceMomentClosureKernelIdentity, detail, flags));
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value(0)) && std::isfinite(value(1)) &&
           std::isfinite(value(2));
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (!std::isfinite(value(row, column))) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool finite(const QuaternionStorage& value) noexcept {
    const auto coefficients = gnc::foundation::quaternion_to_wxyz(value);
    return std::all_of(coefficients.begin(), coefficients.end(),
                       [](double coefficient) {
                           return std::isfinite(coefficient);
                       });
}

[[nodiscard]] bool finite(const RigidState& state) noexcept {
    return finite(state.position.value) && finite(state.velocity.value) &&
           finite(state.attitude.value) && finite(state.angular_rate.value);
}

[[nodiscard]] bool near(double lhs, double rhs,
                        const NumericalPolicy& policy) noexcept {
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return false;
    }
    const double scale = std::max(std::abs(lhs), std::abs(rhs));
    const double limit = policy.absolute_tolerance +
                         policy.relative_tolerance * scale;
    return std::isfinite(limit) && std::abs(lhs - rhs) <= limit;
}

[[nodiscard]] bool same_instant(const SimulationInstant& lhs,
                                const SimulationInstant& rhs,
                                const NumericalPolicy& policy) noexcept {
    return lhs.tick == rhs.tick && near(lhs.seconds, rhs.seconds, policy);
}

[[nodiscard]] bool valid_sample_context(
    const SampleContext& context,
    const gnc::contracts::FrameIdentity& expected_frame,
    const gnc::contracts::ClockDomainIdentity& expected_clock,
    const SimulationInstant& expected_time,
    std::int64_t expected_revision,
    const NumericalPolicy& policy) noexcept {
    return context.frame == expected_frame &&
           context.clock_domain == expected_clock &&
           same_instant(context.sample_time, expected_time, policy) &&
           context.configuration_revision == expected_revision &&
           context.quality == DataQuality::Valid;
}

[[nodiscard]] bool valid_interval_context(
    const IntervalSampleContext& context,
    const gnc::contracts::FrameIdentity& expected_frame,
    const RigidStepContext& step,
    const NumericalPolicy& policy) noexcept {
    return valid_sample_context(
               context.sample, expected_frame, step.clock_domain,
               step.interval_start, step.configuration_revision, policy) &&
           same_instant(context.validity.effective_from,
                        step.interval_start, policy) &&
           same_instant(context.validity.effective_until,
                        step.interval_end, policy);
}

[[nodiscard]] std::optional<ValidationFailure> validate_contexts(
    const RigidStepModelDefinition& definition,
    const RigidStepInput& input) {
    const auto& policy = definition.algorithm.numerical_policy;
    const auto& step = input.context;
    const auto& closure = definition.force_moment_closure;
    if (step.inertial_frame != definition.inertial_frame ||
        step.body_frame != closure.body_frame ||
        step.clock_domain != closure.clock_domain) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "step-frame-or-clock"};
    }
    if (step.configuration_revision !=
            closure.configuration_revision ||
        step.quality != DataQuality::Valid) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "step-revision-or-quality"};
    }
    if (!std::isfinite(step.interval_start.seconds) ||
        !std::isfinite(step.interval_end.seconds)) {
        return ValidationFailure{NumericalStatus::NonFiniteInput,
                                 "step-time"};
    }
    if (step.interval_start.tick < 0 ||
        step.interval_start.tick ==
            std::numeric_limits<std::int64_t>::max() ||
        step.interval_end.tick != step.interval_start.tick + 1) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "step-ticks"};
    }
    const double duration =
        step.interval_end.seconds - step.interval_start.seconds;
    if (!std::isfinite(duration) || duration <= 0.0 ||
        !near(duration, definition.algorithm.fixed_step_seconds, policy)) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "step-duration"};
    }
    if (!valid_sample_context(
            input.environment.context, definition.inertial_frame,
            closure.clock_domain, step.interval_start,
            closure.configuration_revision, policy)) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "environment-context"};
    }
    if (!valid_interval_context(input.mass_properties.context,
                                closure.body_frame, step, policy)) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "mass-context"};
    }
    if (!valid_interval_context(input.supplied_wrench.context,
                                closure.body_frame, step, policy)) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "wrench-context"};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationFailure> validate_values(
    const RigidStepModelDefinition& definition,
    const RigidStepInput& input) {
    if (!finite(input.committed_state) ||
        !finite(input.environment.gravity.value) ||
        !finite(input.environment.velocity_airmass.value) ||
        !std::isfinite(input.environment.density_kilograms_per_cubic_meter) ||
        !std::isfinite(
            input.environment.speed_of_sound_meters_per_second) ||
        !std::isfinite(input.mass_properties.mass_kilograms) ||
        !finite(input.mass_properties.body_origin_to_center_of_mass.value) ||
        !finite(input.mass_properties.inertia_about_center_of_mass.value) ||
        !finite(input.supplied_wrench.force.value) ||
        !finite(input.supplied_wrench.body_origin_to_application.value) ||
        !finite(input.supplied_wrench.intrinsic_moment_at_application.value)) {
        return ValidationFailure{NumericalStatus::NonFiniteInput,
                                 "physical-input"};
    }
    if (input.environment.density_kilograms_per_cubic_meter < 0.0 ||
        input.environment.speed_of_sound_meters_per_second <= 0.0 ||
        input.mass_properties.mass_kilograms <= 0.0 ||
        input.mass_properties.mass_state_id.empty() ||
        input.supplied_wrench.source_id.empty() ||
        input.supplied_wrench.source_id ==
            definition.aerodynamics.source_id) {
        return ValidationFailure{NumericalStatus::DomainError,
                                 "physical-domain"};
    }
    return std::nullopt;
}

[[nodiscard]] bool approximate_status(NumericalStatus status) noexcept {
    return status == NumericalStatus::Approximate ||
           status == NumericalStatus::Extrapolated;
}

[[nodiscard]] NumericalOutcome<AirLookupComputation> compute_air_lookup(
    const RigidStepModelDefinition& definition,
    const PreparedAerodynamicTableModel& aerodynamic_model,
    AerodynamicTableQueryEntry aerodynamic_query,
    const RigidState& state,
    const EnvironmentInput& environment) {
    NumericalFlags flags = 0U;
    bool approximate = false;
    const auto& quaternion_policy =
        definition.algorithm.attitude_evaluation_policy;

    const auto prepared_attitude = gnc::foundation::prepare_passive_quaternion(
        state.attitude.value, quaternion_policy);
    if (!prepared_attitude.has_value()) {
        return product_failure<AirLookupComputation>(
            prepared_attitude.status(), "air-attitude",
            prepared_attitude.evidence().flags);
    }
    flags |= prepared_attitude.evidence().flags;
    approximate = approximate || approximate_status(prepared_attitude.status());

    const Vec3 relative_inertial =
        state.velocity.value - environment.velocity_airmass.value;
    if (!finite(relative_inertial)) {
        return product_failure<AirLookupComputation>(
            NumericalStatus::NonFiniteIntermediate, "relative-air", flags);
    }
    const auto inverse_attitude =
        gnc::foundation::inverse_passive_quaternion(
            prepared_attitude.value(), quaternion_policy);
    if (!inverse_attitude.has_value()) {
        return product_failure<AirLookupComputation>(
            inverse_attitude.status(), "air-attitude-inverse",
            flags | inverse_attitude.evidence().flags);
    }
    flags |= inverse_attitude.evidence().flags;
    approximate = approximate || approximate_status(inverse_attitude.status());
    const auto relative_body = gnc::foundation::rotate_passive(
        inverse_attitude.value(), relative_inertial, quaternion_policy);
    if (!relative_body.has_value()) {
        return product_failure<AirLookupComputation>(
            relative_body.status(), "air-frame-transform",
            flags | relative_body.evidence().flags);
    }
    flags |= relative_body.evidence().flags;
    approximate = approximate || approximate_status(relative_body.status());

    const Vec3 velocity_body = relative_body.value();
    const double airspeed = std::hypot(
        std::hypot(velocity_body(0), velocity_body(1)), velocity_body(2));
    if (!std::isfinite(airspeed)) {
        return product_failure<AirLookupComputation>(
            NumericalStatus::NonFiniteIntermediate, "airspeed", flags);
    }
    if (airspeed <= 0.0) {
        return product_failure<AirLookupComputation>(
            NumericalStatus::DomainError, "zero-airspeed", flags);
    }
    const double horizontal =
        std::hypot(velocity_body(0), velocity_body(2));
    const double alpha = std::atan2(velocity_body(2), velocity_body(0));
    const double beta = std::atan2(velocity_body(1), horizontal);
    const double dynamic_pressure =
        0.5 * environment.density_kilograms_per_cubic_meter *
        airspeed * airspeed;
    const double mach =
        airspeed / environment.speed_of_sound_meters_per_second;
    if (!std::isfinite(alpha) || !std::isfinite(beta) ||
        !std::isfinite(dynamic_pressure) || !std::isfinite(mach)) {
        return product_failure<AirLookupComputation>(
            NumericalStatus::NonFiniteIntermediate, "air-data", flags);
    }

    const auto query = aerodynamic_query(
        aerodynamic_model, AerodynamicTableQueryInput{mach, alpha, beta});
    if (!query.has_value()) {
        return product_failure<AirLookupComputation>(
            query.status(), "aero-query", flags | query.evidence().flags);
    }
    flags |= query.evidence().flags;
    approximate = approximate || approximate_status(query.status());
    const auto& result = query.value();
    const auto& response = result.output;

    AirLookupComputation output;
    output.air_data.velocity_relative_inertial.value = relative_inertial;
    output.air_data.velocity_relative_body.value = velocity_body;
    output.air_data.airspeed_meters_per_second = airspeed;
    output.air_data.alpha_radians = alpha;
    output.air_data.beta_radians = beta;
    output.air_data.dynamic_pressure_pascals = dynamic_pressure;
    output.air_data.mach = mach;
    output.lookup.domain_status = result.telemetry.domain_status;
    output.lookup.weights = result.telemetry.weights;
    output.lookup.coefficients_ca_cy_cn_cl_cm_cn =
        response.coefficients_ca_cy_cn_cl_cm_cn;
    output.aerodynamic_response = response;
    output.aerodynamic_query = result;
    NumericalEvidence evidence = product_evidence(
        kRigidStepKernelIdentity, "air-and-aero-query", flags);
    evidence.evaluations = query.evidence().evaluations;
    return NumericalOutcome<AirLookupComputation>::with_value(
        approximate ? NumericalStatus::Approximate
                    : NumericalStatus::Success,
        std::move(output), evidence);
}

[[nodiscard]] BodyWrenchContribution make_contribution(
    std::string source_id, const Vec3& force,
    const Vec3& body_origin_to_application,
    const Vec3& body_origin_to_center_of_mass,
    const Vec3& moment_at_application) {
    const Vec3 lever =
        body_origin_to_application - body_origin_to_center_of_mass;
    const Vec3 transport = lever.cross(force);
    return {
        std::move(source_id),
        BodyForceNewtons{force},
        BodyMomentNewtonMeters{moment_at_application + transport},
    };
}

[[nodiscard]] NumericalOutcome<RigidDerivativeOutput>
evaluate_derivative_impl(
    const RigidStepAlgorithmDefinition& algorithm,
    const RigidState& state,
    double mass_kilograms,
    const Mat3& inertia_about_center_of_mass,
    const Vec3& force_total_body,
    const Vec3& moment_total_body,
    const Vec3& gravity_inertial) {
    NumericalFlags flags = 0U;
    bool approximate = false;
    const auto prepared_attitude = gnc::foundation::prepare_passive_quaternion(
        state.attitude.value, algorithm.attitude_evaluation_policy);
    if (!prepared_attitude.has_value()) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            prepared_attitude.status(),
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-attitude",
                             prepared_attitude.evidence().flags));
    }
    flags |= prepared_attitude.evidence().flags;
    approximate = approximate || approximate_status(prepared_attitude.status());

    const auto force_inertial = gnc::foundation::rotate_passive(
        prepared_attitude.value(), force_total_body,
        algorithm.attitude_evaluation_policy);
    if (!force_inertial.has_value()) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            force_inertial.status(),
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-force-frame",
                             flags | force_inertial.evidence().flags));
    }
    flags |= force_inertial.evidence().flags;
    approximate = approximate || approximate_status(force_inertial.status());
    const Vec3 acceleration =
        force_inertial.value() / mass_kilograms + gravity_inertial;
    const Vec3 angular_momentum =
        inertia_about_center_of_mass * state.angular_rate.value;
    const Vec3 gyroscopic = state.angular_rate.value.cross(angular_momentum);
    const Vec3 net_moment = moment_total_body - gyroscopic;
    if (!finite(acceleration) || !finite(angular_momentum) ||
        !finite(gyroscopic) || !finite(net_moment)) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            NumericalStatus::NonFiniteIntermediate,
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-balance", flags));
    }

    const auto angular_acceleration = gnc::foundation::solve_spd_3x3(
        inertia_about_center_of_mass, net_moment,
        algorithm.numerical_policy);
    if (!angular_acceleration.has_value()) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            angular_acceleration.status(),
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-inertia",
                             flags | angular_acceleration.evidence().flags));
    }
    flags |= angular_acceleration.evidence().flags;
    approximate = approximate ||
                  approximate_status(angular_acceleration.status());

    const auto attitude_derivative =
        gnc::foundation::passive_quaternion_body_rate_derivative(
            prepared_attitude.value(), state.angular_rate.value,
            algorithm.attitude_evaluation_policy);
    if (!attitude_derivative.has_value()) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            attitude_derivative.status(),
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-attitude-rate",
                             flags | attitude_derivative.evidence().flags));
    }
    flags |= attitude_derivative.evidence().flags;
    approximate = approximate || approximate_status(
        attitude_derivative.status());

    RigidDerivativeOutput output;
    output.force_total_inertial.value = force_inertial.value();
    output.acceleration.value = acceleration;
    output.angular_momentum.value = angular_momentum;
    output.gyroscopic_moment.value = gyroscopic;
    output.net_moment.value = net_moment;
    output.angular_acceleration.value = angular_acceleration.value().solution;
    output.attitude_derivative.value = attitude_derivative.value();
    NumericalEvidence evidence = product_evidence(
        kRigidDerivativeKernelIdentity, "rigid-derivative", flags);
    return NumericalOutcome<RigidDerivativeOutput>::with_value(
        approximate ? NumericalStatus::Approximate
                    : NumericalStatus::Success,
        std::move(output), evidence);
}

[[nodiscard]] StateVector pack_state(const RigidState& state) {
    const auto quaternion =
        gnc::foundation::quaternion_to_wxyz(state.attitude.value);
    return {
        state.position.value(0),
        state.position.value(1),
        state.position.value(2),
        state.velocity.value(0),
        state.velocity.value(1),
        state.velocity.value(2),
        quaternion[0],
        quaternion[1],
        quaternion[2],
        quaternion[3],
        state.angular_rate.value(0),
        state.angular_rate.value(1),
        state.angular_rate.value(2),
    };
}

[[nodiscard]] RigidState unpack_state(const StateVector& value) {
    RigidState state;
    state.position.value = Vec3{value[0], value[1], value[2]};
    state.velocity.value = Vec3{value[3], value[4], value[5]};
    state.attitude.value = gnc::foundation::quaternion_from_wxyz(
        value[6], value[7], value[8], value[9]);
    state.angular_rate.value = Vec3{value[10], value[11], value[12]};
    return state;
}

[[nodiscard]] StateVector pack_derivative(
    const RigidState& state,
    const RigidDerivativeOutput& derivative) {
    const auto quaternion_derivative =
        gnc::foundation::quaternion_to_wxyz(
            derivative.attitude_derivative.value);
    return {
        state.velocity.value(0),
        state.velocity.value(1),
        state.velocity.value(2),
        derivative.acceleration.value(0),
        derivative.acceleration.value(1),
        derivative.acceleration.value(2),
        quaternion_derivative[0],
        quaternion_derivative[1],
        quaternion_derivative[2],
        quaternion_derivative[3],
        derivative.angular_acceleration.value(0),
        derivative.angular_acceleration.value(1),
        derivative.angular_acceleration.value(2),
    };
}

} // namespace

namespace {

[[nodiscard]] std::string finite_check_token(FiniteCheck value) {
    switch (value) {
    case FiniteCheck::Disabled:
        return "disabled";
    case FiniteCheck::InputAndOutput:
        return "input-and-output";
    case FiniteCheck::EveryStage:
        return "every-stage";
    }
    return {};
}

[[nodiscard]] std::optional<FiniteCheck> parse_finite_check(
    std::string_view token) {
    if (token == "disabled") {
        return FiniteCheck::Disabled;
    }
    if (token == "input-and-output") {
        return FiniteCheck::InputAndOutput;
    }
    if (token == "every-stage") {
        return FiniteCheck::EveryStage;
    }
    return std::nullopt;
}

[[nodiscard]] bool canonical_double(double value) noexcept {
    return std::isfinite(value) &&
           !(value == 0.0 && std::signbit(value));
}

} // namespace

gnc::model_sdk::CanonicalConfigBlock
canonical_uniform_environment_config(
    const UniformEnvironmentDefinition& definition) {
    return {
        std::string(kUniformEnvironmentConfigSchemaIdentity),
        kUniformEnvironmentConfigSchemaVersion,
        {
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision",
             definition.configuration_revision},
            {"density_kilograms_per_cubic_meter",
             definition.density_kilograms_per_cubic_meter},
            {"gravity.x_meters_per_second_squared",
             definition.gravity.value(0)},
            {"gravity.y_meters_per_second_squared",
             definition.gravity.value(1)},
            {"gravity.z_meters_per_second_squared",
             definition.gravity.value(2)},
            {"inertial_frame_id", definition.inertial_frame.id},
            {"speed_of_sound_meters_per_second",
             definition.speed_of_sound_meters_per_second},
            {"velocity_airmass.x_meters_per_second",
             definition.velocity_airmass.value(0)},
            {"velocity_airmass.y_meters_per_second",
             definition.velocity_airmass.value(1)},
            {"velocity_airmass.z_meters_per_second",
             definition.velocity_airmass.value(2)},
        },
    };
}

NumericalOutcome<UniformEnvironmentDefinition>
build_uniform_environment_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return NumericalOutcome<UniformEnvironmentDefinition>::failure(
            NumericalStatus::DomainError,
            product_evidence(kUniformEnvironmentPreparationIdentity,
                             "canonical-config"));
    };
    static constexpr std::array<std::string_view, 11U> kFields{
        "clock_domain_id",
        "configuration_revision",
        "density_kilograms_per_cubic_meter",
        "gravity.x_meters_per_second_squared",
        "gravity.y_meters_per_second_squared",
        "gravity.z_meters_per_second_squared",
        "inertial_frame_id",
        "speed_of_sound_meters_per_second",
        "velocity_airmass.x_meters_per_second",
        "velocity_airmass.y_meters_per_second",
        "velocity_airmass.z_meters_per_second",
    };
    if (configuration.schema_id !=
            kUniformEnvironmentConfigSchemaIdentity ||
        configuration.schema_version !=
            kUniformEnvironmentConfigSchemaVersion ||
        configuration.fields.size() != kFields.size()) {
        return failure();
    }
    for (std::size_t index = 0U; index < kFields.size(); ++index) {
        if (configuration.fields[index].field_id != kFields[index]) {
            return failure();
        }
    }

    const auto* clock_domain = std::get_if<std::string>(
        &configuration.fields[0U].value);
    const auto* revision = std::get_if<std::int64_t>(
        &configuration.fields[1U].value);
    const auto* density =
        std::get_if<double>(&configuration.fields[2U].value);
    const auto* gravity_x =
        std::get_if<double>(&configuration.fields[3U].value);
    const auto* gravity_y =
        std::get_if<double>(&configuration.fields[4U].value);
    const auto* gravity_z =
        std::get_if<double>(&configuration.fields[5U].value);
    const auto* inertial_frame = std::get_if<std::string>(
        &configuration.fields[6U].value);
    const auto* speed_of_sound =
        std::get_if<double>(&configuration.fields[7U].value);
    const auto* velocity_x =
        std::get_if<double>(&configuration.fields[8U].value);
    const auto* velocity_y =
        std::get_if<double>(&configuration.fields[9U].value);
    const auto* velocity_z =
        std::get_if<double>(&configuration.fields[10U].value);
    const std::array<const double*, 8U> numbers{
        density, gravity_x, gravity_y, gravity_z,
        speed_of_sound, velocity_x, velocity_y, velocity_z};
    if (clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 ||
        inertial_frame == nullptr || inertial_frame->empty() ||
        std::any_of(numbers.begin(), numbers.end(),
                    [](const double* value) {
                        return value == nullptr ||
                               !canonical_double(*value);
                    }) ||
        *density < 0.0 || *speed_of_sound <= 0.0) {
        return failure();
    }

    UniformEnvironmentDefinition definition;
    definition.metadata = {
        std::string(kUniformEnvironmentModelIdentity),
        std::string(kUniformEnvironmentModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery,
    };
    definition.inertial_frame.id = *inertial_frame;
    definition.clock_domain.id = *clock_domain;
    definition.configuration_revision = *revision;
    definition.gravity.value = Vec3{*gravity_x, *gravity_y, *gravity_z};
    definition.velocity_airmass.value =
        Vec3{*velocity_x, *velocity_y, *velocity_z};
    definition.density_kilograms_per_cubic_meter = *density;
    definition.speed_of_sound_meters_per_second = *speed_of_sound;
    return NumericalOutcome<UniformEnvironmentDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        product_evidence(kUniformEnvironmentPreparationIdentity,
                         "canonical-config"));
}

PreparedUniformEnvironmentModel::PreparedUniformEnvironmentModel(
    std::shared_ptr<const UniformEnvironmentDefinition> definition,
    gnc::model_sdk::PreparedModelMetadata metadata) noexcept
    : definition_(std::move(definition)), metadata_(std::move(metadata)) {}

const UniformEnvironmentDefinition&
PreparedUniformEnvironmentModel::definition() const noexcept {
    return *definition_;
}

const gnc::model_sdk::PreparedModelMetadata&
PreparedUniformEnvironmentModel::metadata() const noexcept {
    return metadata_;
}

NumericalOutcome<PreparedUniformEnvironmentModel>
prepare_uniform_environment_model(UniformEnvironmentDefinition definition) {
    const auto failure = [](NumericalStatus status,
                            std::string_view detail) {
        return NumericalOutcome<PreparedUniformEnvironmentModel>::failure(
            status,
            product_evidence(kUniformEnvironmentPreparationIdentity,
                             detail));
    };
    auto metadata = gnc::model_sdk::prepare_model_metadata(
        definition.metadata, kUniformEnvironmentPreparationIdentity);
    if (!metadata.has_value()) {
        return NumericalOutcome<PreparedUniformEnvironmentModel>::failure(
            metadata.status(), metadata.evidence());
    }
    if (definition.metadata.model_id !=
            kUniformEnvironmentModelIdentity ||
        definition.metadata.model_version !=
            kUniformEnvironmentModelVersion ||
        definition.metadata.execution_form !=
            gnc::model_sdk::ModelExecutionForm::PureQuery ||
        definition.inertial_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.configuration_revision < 0) {
        return failure(NumericalStatus::DomainError,
                       "definition-identity-or-context");
    }
    if (!finite(definition.gravity.value) ||
        !finite(definition.velocity_airmass.value) ||
        !canonical_double(
            definition.density_kilograms_per_cubic_meter) ||
        !canonical_double(
            definition.speed_of_sound_meters_per_second) ||
        definition.density_kilograms_per_cubic_meter < 0.0 ||
        definition.speed_of_sound_meters_per_second <= 0.0) {
        return failure(NumericalStatus::DomainError,
                       "definition-physical-domain");
    }
    return NumericalOutcome<PreparedUniformEnvironmentModel>::with_value(
        NumericalStatus::Success,
        PreparedUniformEnvironmentModel{
            std::make_shared<const UniformEnvironmentDefinition>(
                std::move(definition)),
            std::move(metadata.value())},
        product_evidence(kUniformEnvironmentPreparationIdentity,
                         "prepared"));
}

NumericalOutcome<UniformEnvironmentQueryEvaluation>
UniformEnvironmentQueryKernel::evaluate(
    const PreparedUniformEnvironmentModel& model,
    const UniformEnvironmentQueryInput& input) {
    const auto& definition = model.definition();
    const auto& context = input.context;
    if (context.frame != definition.inertial_frame ||
        context.clock_domain != definition.clock_domain ||
        context.configuration_revision !=
            definition.configuration_revision ||
        context.quality != DataQuality::Valid ||
        context.sample_time.tick < 0 ||
        !std::isfinite(context.sample_time.seconds)) {
        return NumericalOutcome<UniformEnvironmentQueryEvaluation>::failure(
            NumericalStatus::DomainError,
            product_evidence(kUniformEnvironmentQueryIdentity,
                             "query-context"));
    }
    if (!finite(input.position.value)) {
        return NumericalOutcome<UniformEnvironmentQueryEvaluation>::failure(
            NumericalStatus::NonFiniteInput,
            product_evidence(kUniformEnvironmentQueryIdentity,
                             "query-position"));
    }

    EnvironmentInput output;
    output.context = context;
    output.gravity = definition.gravity;
    output.velocity_airmass = definition.velocity_airmass;
    output.density_kilograms_per_cubic_meter =
        definition.density_kilograms_per_cubic_meter;
    output.speed_of_sound_meters_per_second =
        definition.speed_of_sound_meters_per_second;
    NumericalEvidence evidence = product_evidence(
        kUniformEnvironmentQueryIdentity, "uniform-environment");
    evidence.evaluations = 1U;
    return NumericalOutcome<UniformEnvironmentQueryEvaluation>::with_value(
        NumericalStatus::Success,
        UniformEnvironmentQueryEvaluation{
            std::move(output), UniformEnvironmentQueryTelemetry{}},
        evidence);
}

gnc::model_sdk::CanonicalConfigBlock
canonical_force_moment_closure_config(
    const ForceMomentClosureDefinition& definition) {
    const auto& policy = definition.numerical_policy;
    return {
        std::string(kForceMomentClosureConfigSchemaIdentity),
        kForceMomentClosureConfigSchemaVersion,
        {
            {"body_frame_id", definition.body_frame.id},
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision",
             definition.configuration_revision},
            {"numerical.absolute_tolerance",
             policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance",
             policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
        },
    };
}

NumericalOutcome<ForceMomentClosureDefinition>
build_force_moment_closure_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return NumericalOutcome<ForceMomentClosureDefinition>::failure(
            NumericalStatus::DomainError,
            product_evidence(kForceMomentClosurePreparationIdentity,
                             "canonical-config"));
    };
    static constexpr std::array<std::string_view, 8U> kFields{
        "body_frame_id",
        "clock_domain_id",
        "configuration_revision",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
    };
    if (configuration.schema_id !=
            kForceMomentClosureConfigSchemaIdentity ||
        configuration.schema_version !=
            kForceMomentClosureConfigSchemaVersion ||
        configuration.fields.size() != kFields.size()) {
        return failure();
    }
    for (std::size_t index = 0U; index < kFields.size(); ++index) {
        if (configuration.fields[index].field_id != kFields[index]) {
            return failure();
        }
    }

    const auto* body_frame = std::get_if<std::string>(
        &configuration.fields[0U].value);
    const auto* clock_domain = std::get_if<std::string>(
        &configuration.fields[1U].value);
    const auto* revision = std::get_if<std::int64_t>(
        &configuration.fields[2U].value);
    const auto* absolute =
        std::get_if<double>(&configuration.fields[3U].value);
    const auto* condition =
        std::get_if<double>(&configuration.fields[4U].value);
    const auto* finite_token =
        std::get_if<gnc::model_sdk::CanonicalEnumValue>(
            &configuration.fields[5U].value);
    const auto* relative =
        std::get_if<double>(&configuration.fields[6U].value);
    const auto* zero =
        std::get_if<double>(&configuration.fields[7U].value);
    if (body_frame == nullptr || body_frame->empty() ||
        clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 || absolute == nullptr ||
        condition == nullptr || finite_token == nullptr ||
        relative == nullptr || zero == nullptr ||
        !canonical_double(*absolute) ||
        !canonical_double(*condition) ||
        !canonical_double(*relative) || !canonical_double(*zero)) {
        return failure();
    }
    const auto finite_check = parse_finite_check(finite_token->token);
    if (!finite_check.has_value()) {
        return failure();
    }

    ForceMomentClosureDefinition definition;
    definition.metadata = {
        std::string(kForceMomentClosureModelIdentity),
        std::string(kForceMomentClosureModelVersion),
        gnc::model_sdk::ModelExecutionForm::Closure,
    };
    definition.body_frame.id = *body_frame;
    definition.clock_domain.id = *clock_domain;
    definition.configuration_revision = *revision;
    definition.numerical_policy = {
        *absolute, *relative, *finite_check, *zero, *condition,
    };
    const auto prepared = prepare_force_moment_closure_model(definition);
    if (!prepared.has_value()) {
        return NumericalOutcome<ForceMomentClosureDefinition>::failure(
            prepared.status(), prepared.evidence());
    }
    return NumericalOutcome<ForceMomentClosureDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        product_evidence(kForceMomentClosurePreparationIdentity,
                         "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_aerodynamic_table_config(
    const AerodynamicTableDefinition& definition) {
    return {
        std::string(kAerodynamicTableConfigSchemaIdentity),
        kAerodynamicTableConfigSchemaVersion,
        {
            {"body_origin_to_application.x_m",
             definition.body_origin_to_application.value(0)},
            {"body_origin_to_application.y_m",
             definition.body_origin_to_application.value(1)},
            {"body_origin_to_application.z_m",
             definition.body_origin_to_application.value(2)},
            {"configuration_id", definition.configuration_id},
            {"reference_area_square_meters",
             definition.reference_area_square_meters},
            {"reference_chord_meters",
             definition.reference_chord_meters},
            {"reference_span_meters",
             definition.reference_span_meters},
            {"source_id", definition.source_id},
        },
    };
}

NumericalOutcome<AerodynamicTableDefinition>
build_aerodynamic_table_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration,
    std::string table_asset_id) {
    const auto failure = [] {
        return NumericalOutcome<AerodynamicTableDefinition>::failure(
            NumericalStatus::DomainError,
            product_evidence(kAerodynamicTablePreparationIdentity,
                             "canonical-config"));
    };
    static constexpr std::array<std::string_view, 8U> kFields{
        "body_origin_to_application.x_m",
        "body_origin_to_application.y_m",
        "body_origin_to_application.z_m",
        "configuration_id",
        "reference_area_square_meters",
        "reference_chord_meters",
        "reference_span_meters",
        "source_id",
    };
    if (configuration.schema_id !=
            kAerodynamicTableConfigSchemaIdentity ||
        configuration.schema_version !=
            kAerodynamicTableConfigSchemaVersion ||
        configuration.fields.size() != kFields.size() ||
        table_asset_id.empty()) {
        return failure();
    }
    for (std::size_t index = 0U; index < kFields.size(); ++index) {
        if (configuration.fields[index].field_id != kFields[index]) {
            return failure();
        }
    }
    std::array<const double*, 6U> values{
        std::get_if<double>(&configuration.fields[0U].value),
        std::get_if<double>(&configuration.fields[1U].value),
        std::get_if<double>(&configuration.fields[2U].value),
        std::get_if<double>(&configuration.fields[4U].value),
        std::get_if<double>(&configuration.fields[5U].value),
        std::get_if<double>(&configuration.fields[6U].value),
    };
    const auto* configuration_id = std::get_if<std::string>(
        &configuration.fields[3U].value);
    const auto* source_id = std::get_if<std::string>(
        &configuration.fields[7U].value);
    if (std::any_of(values.begin(), values.end(), [](const double* value) {
            return value == nullptr || !canonical_double(*value);
        }) ||
        configuration_id == nullptr || configuration_id->empty() ||
        source_id == nullptr || source_id->empty()) {
        return failure();
    }

    AerodynamicTableDefinition definition;
    definition.metadata = {
        std::string(kAerodynamicTableModelIdentity),
        std::string(kAerodynamicTableModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery,
    };
    definition.body_origin_to_application.value =
        Vec3{*values[0U], *values[1U], *values[2U]};
    definition.configuration_id = *configuration_id;
    definition.reference_area_square_meters = *values[3U];
    definition.reference_chord_meters = *values[4U];
    definition.reference_span_meters = *values[5U];
    definition.source_id = *source_id;
    definition.table_asset_id = std::move(table_asset_id);
    if (definition.reference_area_square_meters <= 0.0 ||
        definition.reference_chord_meters <= 0.0 ||
        definition.reference_span_meters <= 0.0) {
        return failure();
    }
    return NumericalOutcome<AerodynamicTableDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        product_evidence(kAerodynamicTablePreparationIdentity,
                         "canonical-config"));
}

PreparedAerodynamicTableModel::PreparedAerodynamicTableModel(
    std::shared_ptr<const AerodynamicTableDefinition> definition,
    std::shared_ptr<const AerodynamicTableAsset> asset,
    gnc::foundation::PreparedTrilinearTableView<6U> table,
    gnc::model_sdk::PreparedModelMetadata metadata) noexcept
    : definition_(std::move(definition)), asset_(std::move(asset)),
      table_(std::move(table)), metadata_(std::move(metadata)) {}

const AerodynamicTableDefinition&
PreparedAerodynamicTableModel::definition() const noexcept {
    return *definition_;
}

const AerodynamicTableAsset&
PreparedAerodynamicTableModel::asset() const noexcept {
    return *asset_;
}

const gnc::model_sdk::PreparedModelMetadata&
PreparedAerodynamicTableModel::metadata() const noexcept {
    return metadata_;
}

NumericalOutcome<PreparedAerodynamicTableModel>
prepare_aerodynamic_table_model(
    AerodynamicTableDefinition definition,
    AerodynamicTableAsset asset) {
    const auto failure = [](NumericalStatus status,
                            std::string_view detail) {
        return NumericalOutcome<PreparedAerodynamicTableModel>::failure(
            status, product_evidence(
                        kAerodynamicTablePreparationIdentity, detail));
    };
    auto metadata = gnc::model_sdk::prepare_model_metadata(
        definition.metadata, kAerodynamicTablePreparationIdentity);
    if (!metadata.has_value()) {
        return NumericalOutcome<PreparedAerodynamicTableModel>::failure(
            metadata.status(), metadata.evidence());
    }
    if (definition.metadata.model_id != kAerodynamicTableModelIdentity ||
        definition.metadata.model_version !=
            kAerodynamicTableModelVersion ||
        definition.metadata.execution_form !=
            gnc::model_sdk::ModelExecutionForm::PureQuery ||
        definition.source_id.empty() ||
        definition.configuration_id.empty() ||
        definition.table_asset_id.empty() ||
        asset.asset_schema_id != kAerodynamicTableAssetSchemaIdentity ||
        asset.asset_id != definition.table_asset_id) {
        return failure(NumericalStatus::DomainError,
                       "definition-or-asset-identity");
    }
    if (!canonical_double(definition.reference_area_square_meters) ||
        !canonical_double(definition.reference_span_meters) ||
        !canonical_double(definition.reference_chord_meters) ||
        definition.reference_area_square_meters <= 0.0 ||
        definition.reference_span_meters <= 0.0 ||
        definition.reference_chord_meters <= 0.0 ||
        !finite(definition.body_origin_to_application.value)) {
        return failure(NumericalStatus::DomainError,
                       "definition-geometry");
    }

    auto owned_definition =
        std::make_shared<const AerodynamicTableDefinition>(
            std::move(definition));
    auto owned_asset = std::make_shared<const AerodynamicTableAsset>(
        std::move(asset));
    gnc::foundation::TrilinearTableView<6U> view;
    view.x_axis = {owned_asset->mach_axis.data(),
                   owned_asset->mach_axis.size()};
    view.y_axis = {owned_asset->alpha_axis_radians.data(),
                   owned_asset->alpha_axis_radians.size()};
    view.z_axis = {owned_asset->beta_axis_radians.data(),
                   owned_asset->beta_axis_radians.size()};
    view.rows =
        owned_asset->coefficient_rows_ca_cy_cn_cl_cm_cn.data();
    view.row_count =
        owned_asset->coefficient_rows_ca_cy_cn_cl_cm_cn.size();
    auto table = gnc::foundation::prepare_trilinear_table(view);
    if (!table.has_value()) {
        auto evidence = table.evidence();
        evidence.algorithm = kAerodynamicTablePreparationIdentity;
        evidence.detail = "asset-payload";
        return NumericalOutcome<PreparedAerodynamicTableModel>::failure(
            table.status(), evidence);
    }
    auto evidence = table.evidence();
    evidence.algorithm = kAerodynamicTablePreparationIdentity;
    evidence.detail = "prepared";
    return NumericalOutcome<PreparedAerodynamicTableModel>::with_value(
        table.status(),
        PreparedAerodynamicTableModel{
            std::move(owned_definition), std::move(owned_asset),
            table.value(), std::move(metadata.value())},
        evidence);
}

NumericalOutcome<AerodynamicTableQueryEvaluation>
AerodynamicTableQueryKernel::evaluate(
    const PreparedAerodynamicTableModel& model,
    const AerodynamicTableQueryInput& input) {
    const auto query = gnc::foundation::query_trilinear_strict(
        model.table_, input.mach, input.alpha_radians,
        input.beta_radians);
    if (!query.has_value()) {
        auto evidence = query.evidence();
        evidence.algorithm = kAerodynamicTableQueryIdentity;
        evidence.detail = "table-query";
        return NumericalOutcome<AerodynamicTableQueryEvaluation>::failure(
            query.status(), evidence);
    }
    const auto& value = query.value();
    auto evidence = query.evidence();
    evidence.algorithm = kAerodynamicTableQueryIdentity;
    evidence.detail = "table-query";
    return NumericalOutcome<AerodynamicTableQueryEvaluation>::with_value(
        query.status(),
        AerodynamicTableQueryEvaluation{
            AerodynamicTableQueryOutput{value.values},
            AerodynamicTableQueryTelemetry{
                value.domain_status,
                {value.x_bracket.weight, value.y_bracket.weight,
                 value.z_bracket.weight}}},
        evidence);
}

PreparedForceMomentClosureModel::PreparedForceMomentClosureModel(
    std::shared_ptr<const ForceMomentClosureDefinition> definition,
    gnc::model_sdk::PreparedModelMetadata metadata) noexcept
    : definition_(std::move(definition)), metadata_(std::move(metadata)) {}

const ForceMomentClosureDefinition&
PreparedForceMomentClosureModel::definition() const noexcept {
    return *definition_;
}

const gnc::model_sdk::PreparedModelMetadata&
PreparedForceMomentClosureModel::metadata() const noexcept {
    return metadata_;
}

NumericalOutcome<PreparedForceMomentClosureModel>
prepare_force_moment_closure_model(
    ForceMomentClosureDefinition definition) {
    const auto failure = [](NumericalStatus status,
                            std::string_view detail) {
        return NumericalOutcome<PreparedForceMomentClosureModel>::failure(
            status, product_evidence(
                        kForceMomentClosurePreparationIdentity, detail));
    };

    auto metadata = gnc::model_sdk::prepare_model_metadata(
        definition.metadata, kForceMomentClosurePreparationIdentity);
    if (!metadata.has_value()) {
        return NumericalOutcome<PreparedForceMomentClosureModel>::failure(
            metadata.status(), metadata.evidence());
    }
    if (definition.metadata.model_id !=
            kForceMomentClosureModelIdentity ||
        definition.metadata.model_version !=
            kForceMomentClosureModelVersion ||
        definition.metadata.execution_form !=
            gnc::model_sdk::ModelExecutionForm::Closure ||
        definition.body_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.configuration_revision < 0) {
        return failure(NumericalStatus::DomainError,
                       "definition-identity-or-context");
    }
    if (!gnc::foundation::valid_numerical_policy(
            definition.numerical_policy)) {
        return failure(NumericalStatus::DomainError,
                       "definition-numerical-policy");
    }

    return NumericalOutcome<PreparedForceMomentClosureModel>::with_value(
        NumericalStatus::Success,
        PreparedForceMomentClosureModel{
            std::make_shared<const ForceMomentClosureDefinition>(
                std::move(definition)),
            std::move(metadata.value())},
        product_evidence(kForceMomentClosurePreparationIdentity,
                         "prepared", 0U));
}

NumericalOutcome<ForceMomentClosureEvaluation>
ForceMomentClosureKernel::evaluate(
    const PreparedForceMomentClosureModel& model,
    const ForceMomentClosureInput& input) {
    const auto& definition = model.definition();
    const auto& policy = definition.numerical_policy;
    if (!finite(input.body_origin_to_center_of_mass.value)) {
        return closure_failure<ForceMomentClosureEvaluation>(
            NumericalStatus::NonFiniteInput, "center-of-mass");
    }
    if (input.contributions.empty()) {
        return closure_failure<ForceMomentClosureEvaluation>(
            NumericalStatus::DomainError, "empty-closure");
    }

    std::vector<AppliedBodyWrenchInput> ordered = input.contributions;
    std::sort(ordered.begin(), ordered.end(),
              [](const AppliedBodyWrenchInput& lhs,
                 const AppliedBodyWrenchInput& rhs) {
                  return lhs.source_id < rhs.source_id;
              });

    const auto& reference = ordered.front().context;
    const auto valid_context = [&](const IntervalSampleContext& context) {
        return context.sample.frame == definition.body_frame &&
               context.sample.clock_domain == definition.clock_domain &&
               context.sample.configuration_revision ==
                   definition.configuration_revision &&
               context.sample.quality == DataQuality::Valid &&
               context.sample.sample_time.tick >= 0 &&
               std::isfinite(context.sample.sample_time.seconds) &&
               std::isfinite(context.validity.effective_from.seconds) &&
               std::isfinite(context.validity.effective_until.seconds) &&
               same_instant(context.sample.sample_time,
                            context.validity.effective_from, policy) &&
               context.validity.effective_until.tick >
                   context.validity.effective_from.tick &&
               context.validity.effective_until.seconds >
                   context.validity.effective_from.seconds &&
               same_instant(context.sample.sample_time,
                            reference.sample.sample_time, policy) &&
               same_instant(context.validity.effective_from,
                            reference.validity.effective_from, policy) &&
               same_instant(context.validity.effective_until,
                            reference.validity.effective_until, policy);
    };

    Vec3 force_total = Vec3::Zero();
    Vec3 moment_total = Vec3::Zero();
    ForceMomentClosureTelemetry telemetry;
    telemetry.contributions.reserve(ordered.size());
    std::string_view previous_source;
    for (const auto& contribution : ordered) {
        if (contribution.source_id.empty() ||
            contribution.source_id == previous_source ||
            !valid_context(contribution.context)) {
            return closure_failure<ForceMomentClosureEvaluation>(
                NumericalStatus::DomainError,
                "contribution-identity-or-context");
        }
        if (!finite(contribution.force.value) ||
            !finite(contribution.body_origin_to_application.value) ||
            !finite(contribution.intrinsic_moment_at_application.value)) {
            return closure_failure<ForceMomentClosureEvaluation>(
                NumericalStatus::NonFiniteInput,
                "contribution-wrench");
        }

        BodyWrenchContribution transported = make_contribution(
            contribution.source_id, contribution.force.value,
            contribution.body_origin_to_application.value,
            input.body_origin_to_center_of_mass.value,
            contribution.intrinsic_moment_at_application.value);
        force_total += transported.force.value;
        moment_total += transported.moment_about_center_of_mass.value;
        telemetry.contributions.push_back(std::move(transported));
        previous_source = contribution.source_id;
    }
    if (!finite(force_total) || !finite(moment_total)) {
        return closure_failure<ForceMomentClosureEvaluation>(
            NumericalStatus::NonFiniteOutput, "force-moment-closure");
    }

    NumericalEvidence evidence = product_evidence(
        kForceMomentClosureKernelIdentity, "force-moment-closure");
    evidence.evaluations = ordered.size();
    return NumericalOutcome<ForceMomentClosureEvaluation>::with_value(
        NumericalStatus::Success,
        ForceMomentClosureEvaluation{
            RigidFormInput{
                BodyForceNewtons{force_total},
                BodyMomentNewtonMeters{moment_total}},
            std::move(telemetry)},
        evidence);
}

PreparedRigidStepModel::PreparedRigidStepModel(
    std::shared_ptr<const RigidStepModelDefinition> definition,
    PreparedAerodynamicTableModel aerodynamic_table_model,
    PreparedForceMomentClosureModel force_moment_closure_model) noexcept
    : definition_(std::move(definition)),
      aerodynamic_table_model_(std::move(aerodynamic_table_model)),
      force_moment_closure_model_(std::move(force_moment_closure_model)) {}

const RigidStepModelDefinition& PreparedRigidStepModel::definition()
    const noexcept {
    return *definition_;
}

const PreparedForceMomentClosureModel&
PreparedRigidStepModel::force_moment_closure_model() const noexcept {
    return force_moment_closure_model_;
}

const PreparedAerodynamicTableModel&
PreparedRigidStepModel::aerodynamic_table_model() const noexcept {
    return aerodynamic_table_model_;
}

NumericalOutcome<PreparedRigidStepModel> prepare_rigid_step_model(
    RigidStepModelDefinition definition) {
    const auto failure = [](NumericalStatus status, std::string_view detail) {
        return NumericalOutcome<PreparedRigidStepModel>::failure(
            status,
            product_evidence(kRigidStepPreparationIdentity, detail));
    };

    auto closure = prepare_force_moment_closure_model(
        definition.force_moment_closure);
    if (!closure.has_value()) {
        return NumericalOutcome<PreparedRigidStepModel>::failure(
            closure.status(), closure.evidence());
    }

    auto aerodynamics = prepare_aerodynamic_table_model(
        definition.aerodynamics, definition.aerodynamic_table);
    if (!aerodynamics.has_value()) {
        return NumericalOutcome<PreparedRigidStepModel>::failure(
            aerodynamics.status(), aerodynamics.evidence());
    }

    if (definition.inertial_frame.id.empty() ||
        definition.inertial_frame ==
            definition.force_moment_closure.body_frame) {
        return failure(NumericalStatus::DomainError, "definition-identity");
    }
    if (!std::isfinite(definition.algorithm.fixed_step_seconds) ||
        definition.algorithm.fixed_step_seconds <= 0.0 ||
        !gnc::foundation::valid_numerical_policy(
            definition.algorithm.numerical_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            definition.algorithm.attitude_evaluation_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            definition.algorithm.candidate_attitude_policy)) {
        return failure(NumericalStatus::DomainError,
                       "definition-numerical-policy");
    }
    auto owned_definition =
        std::make_shared<const RigidStepModelDefinition>(
            std::move(definition));
    NumericalEvidence evidence = aerodynamics.evidence();
    evidence.algorithm = kRigidStepPreparationIdentity;
    evidence.detail = "prepared-aero-table";
    return NumericalOutcome<PreparedRigidStepModel>::with_value(
        aerodynamics.status(),
        PreparedRigidStepModel{std::move(owned_definition),
                               std::move(aerodynamics.value()),
                               std::move(closure.value())},
        evidence);
}

NumericalOutcome<RigidState> RigidInitialStateBuilder::build(
    const RigidStepAlgorithmDefinition& algorithm,
    const RigidInitialStateInput& input) {
    if (!std::isfinite(algorithm.fixed_step_seconds) ||
        algorithm.fixed_step_seconds <= 0.0 ||
        !gnc::foundation::valid_numerical_policy(
            algorithm.numerical_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            algorithm.attitude_evaluation_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            algorithm.candidate_attitude_policy)) {
        return NumericalOutcome<RigidState>::failure(
            NumericalStatus::DomainError,
            product_evidence(kRigidInitialStateBuilderIdentity,
                             "definition-or-policy"));
    }
    if (!finite(input.state)) {
        return NumericalOutcome<RigidState>::failure(
            NumericalStatus::NonFiniteInput,
            product_evidence(kRigidInitialStateBuilderIdentity,
                             "initial-state"));
    }
    const auto attitude = gnc::foundation::prepare_passive_quaternion(
        input.state.attitude.value, algorithm.candidate_attitude_policy);
    if (!attitude.has_value()) {
        return NumericalOutcome<RigidState>::failure(
            attitude.status(),
            product_evidence(kRigidInitialStateBuilderIdentity,
                             "initial-attitude",
                             attitude.evidence().flags));
    }
    RigidState state = input.state;
    state.attitude.value = attitude.value();
    NumericalEvidence evidence = product_evidence(
        kRigidInitialStateBuilderIdentity, "initial-state",
        attitude.evidence().flags);
    evidence.evaluations = attitude.evidence().evaluations;
    evidence.residual_norm = attitude.evidence().residual_norm;
    return NumericalOutcome<RigidState>::with_value(
        attitude.status(), std::move(state), evidence);
}

CommittedRigidObservation project_committed_rigid_observation(
    const SampleContext& context, const RigidState& state) {
    return CommittedRigidObservation{context, state};
}

RigidState clone_rigid_state(const RigidState& state) {
    return state;
}

bool validate_rigid_state_finite(const RigidState& state) noexcept {
    return finite(state);
}

bool validate_rigid_state_invariants(const RigidState& state) noexcept {
    const double squared_norm = state.attitude.value.squaredNorm();
    return std::isfinite(squared_norm) && squared_norm > 0.0;
}

bool validate_rigid_state(const RigidState& state) noexcept {
    return validate_rigid_state_finite(state) &&
           validate_rigid_state_invariants(state);
}

void swap_rigid_state(RigidState& lhs, RigidState& rhs) noexcept {
    static_assert(std::is_nothrow_swappable_v<RigidState>,
                  "RigidState codec requires noexcept swap");
    using std::swap;
    swap(lhs, rhs);
}

const RigidStateCodec& rigid_state_codec() noexcept {
    static const RigidStateCodec codec{
        &clone_rigid_state, &validate_rigid_state,
        &validate_rigid_state_finite,
        &validate_rigid_state_invariants,
        &swap_rigid_state, &project_committed_rigid_observation};
    return codec;
}

NumericalOutcome<RigidDerivativeOutput>
RigidDerivativeKernel::evaluate(
    const RigidStepAlgorithmDefinition& algorithm,
    const RigidDerivativeInput& input) {
    if (!std::isfinite(input.mass_kilograms) ||
        !finite(input.inertia_about_center_of_mass.value) ||
        !finite(input.frozen_form_input.force_total.value) ||
        !finite(input.frozen_form_input
                    .moment_total_about_center_of_mass.value) ||
        !finite(input.frozen_gravity.value) ||
        !finite(input.candidate_state)) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            NumericalStatus::NonFiniteInput,
            product_evidence(kRigidDerivativeKernelIdentity,
                             "derivative-input"));
    }
    if (input.mass_kilograms <= 0.0 ||
        !gnc::foundation::valid_numerical_policy(
            algorithm.numerical_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            algorithm.attitude_evaluation_policy)) {
        return NumericalOutcome<RigidDerivativeOutput>::failure(
            NumericalStatus::DomainError,
            product_evidence(kRigidDerivativeKernelIdentity,
                             "definition-or-domain"));
    }
    return evaluate_derivative_impl(
        algorithm, input.candidate_state, input.mass_kilograms,
        input.inertia_about_center_of_mass.value,
        input.frozen_form_input.force_total.value,
        input.frozen_form_input.moment_total_about_center_of_mass.value,
        input.frozen_gravity.value);
}

namespace {

NumericalOutcome<RigidFrozenFormPreparationInvocationEvaluation>
prepare_rigid_frozen_form_closure_request(
    const RigidFrozenFormRuntimeDefinition& runtime_definition,
    const RigidFrozenFormQuerySet& queries,
    const RigidStepInput& input) {
    const auto failure = [](NumericalStatus status,
                            std::string_view detail,
                            NumericalFlags flags = 0U) {
        return NumericalOutcome<
            RigidFrozenFormPreparationInvocationEvaluation>::failure(
                status,
                product_evidence(kRigidFrozenFormKernelIdentity,
                                 detail, flags));
    };
    if (queries.aerodynamic.prepared_model == nullptr ||
        queries.aerodynamic.callable == nullptr) {
        return failure(NumericalStatus::DomainError,
                       "aerodynamic-invocation");
    }
    RigidStepModelDefinition definition;
    definition.inertial_frame = runtime_definition.inertial_frame;
    definition.algorithm = runtime_definition.algorithm;
    definition.aerodynamics =
        queries.aerodynamic.prepared_model->definition();
    definition.force_moment_closure.body_frame = input.context.body_frame;
    definition.force_moment_closure.clock_domain =
        input.context.clock_domain;
    definition.force_moment_closure.configuration_revision =
        input.context.configuration_revision;
    definition.force_moment_closure.numerical_policy =
        runtime_definition.algorithm.numerical_policy;
    if (const auto validation = validate_contexts(definition, input)) {
        return failure(validation->status, validation->detail);
    }
    if (const auto validation = validate_values(definition, input)) {
        return failure(validation->status, validation->detail);
    }

    NumericalFlags flags = 0U;
    bool approximate = false;
    const auto inertia_check = gnc::foundation::solve_spd_3x3(
        input.mass_properties.inertia_about_center_of_mass.value,
        Vec3::Zero(), definition.algorithm.numerical_policy);
    if (!inertia_check.has_value()) {
        return failure(inertia_check.status(), "mass-inertia",
                       inertia_check.evidence().flags);
    }
    flags |= inertia_check.evidence().flags;
    approximate = approximate || approximate_status(inertia_check.status());

    const auto air_lookup = compute_air_lookup(
        definition, *queries.aerodynamic.prepared_model,
        queries.aerodynamic.callable, input.committed_state,
        input.environment);
    if (!air_lookup.has_value()) {
        return failure(air_lookup.status(), air_lookup.evidence().detail,
                       flags | air_lookup.evidence().flags);
    }
    flags |= air_lookup.evidence().flags;
    approximate = approximate || approximate_status(air_lookup.status());
    const auto& air = air_lookup.value().air_data;
    const auto& lookup = air_lookup.value().lookup;

    const auto& coefficients = lookup.coefficients_ca_cy_cn_cl_cm_cn;
    const double pressure_area =
        air.dynamic_pressure_pascals *
        definition.aerodynamics.reference_area_square_meters;
    const Vec3 aerodynamic_force{
        -pressure_area * coefficients[0],
        pressure_area * coefficients[1],
        -pressure_area * coefficients[2],
    };
    const Vec3 aerodynamic_moment_at_application{
        pressure_area * definition.aerodynamics.reference_span_meters *
            coefficients[3],
        pressure_area * definition.aerodynamics.reference_chord_meters *
            coefficients[4],
        pressure_area * definition.aerodynamics.reference_span_meters *
            coefficients[5],
    };
    if (!finite(aerodynamic_force) ||
        !finite(aerodynamic_moment_at_application)) {
        return failure(NumericalStatus::NonFiniteIntermediate,
                       "aero-dimensionalization", flags);
    }

    AppliedBodyWrenchInput aerodynamic_wrench;
    aerodynamic_wrench.context = {
        SampleContext{input.context.body_frame,
                      input.context.clock_domain,
                      input.context.interval_start,
                      input.context.configuration_revision,
                      input.context.quality},
        gnc::contracts::HalfOpenValidityInterval{
            input.context.interval_start, input.context.interval_end}};
    aerodynamic_wrench.source_id = definition.aerodynamics.source_id;
    aerodynamic_wrench.force.value = aerodynamic_force;
    aerodynamic_wrench.body_origin_to_application =
        definition.aerodynamics.body_origin_to_application;
    aerodynamic_wrench.intrinsic_moment_at_application.value =
        aerodynamic_moment_at_application;

    ForceMomentClosureInput closure_input;
    closure_input.body_origin_to_center_of_mass =
        input.mass_properties.body_origin_to_center_of_mass;
    closure_input.contributions = {aerodynamic_wrench,
                                   input.supplied_wrench};
    NumericalEvidence evidence = product_evidence(
        kRigidFrozenFormKernelIdentity, "frozen-form-preparation", flags);
    evidence.evaluations = air_lookup.evidence().evaluations;
    evidence.last_step = definition.algorithm.fixed_step_seconds;
    return NumericalOutcome<
        RigidFrozenFormPreparationInvocationEvaluation>::with_value(
            approximate ? NumericalStatus::Approximate
                        : NumericalStatus::Success,
            RigidFrozenFormPreparationInvocationEvaluation{
                RigidFrozenFormPreparationEvaluation{
                    RigidFrozenFormPreparationOutput{
                        input.environment, std::move(closure_input)},
                    RigidFrozenFormPreparationTelemetry{
                        air, lookup,
                        air_lookup.value().aerodynamic_query}},
                RigidFrozenFormInvocationResults{
                    air_lookup.value().aerodynamic_response}},
            evidence);
}

NumericalOutcome<RigidFrozenFormInvocationEvaluation>
evaluate_rigid_frozen_form(
    const RigidFrozenFormRuntimeDefinition& runtime_definition,
    const RigidFrozenFormInvocationSet& invocations,
    const RigidStepInput& input) {
    const auto failure = [](NumericalStatus status,
                            std::string_view detail,
                            NumericalFlags flags = 0U) {
        return NumericalOutcome<RigidFrozenFormInvocationEvaluation>::failure(
            status,
            product_evidence(kRigidFrozenFormKernelIdentity,
                             detail, flags));
    };
    if (invocations.closure.prepared_model == nullptr ||
        invocations.closure.callable == nullptr) {
        return failure(NumericalStatus::DomainError,
                       "closure-invocation");
    }
    const auto prepared = prepare_rigid_frozen_form_closure_request(
        runtime_definition,
        RigidFrozenFormQuerySet{invocations.aerodynamic}, input);
    if (!prepared.has_value()) {
        return failure(prepared.status(), prepared.evidence().detail,
                       prepared.evidence().flags);
    }
    const auto closure = invocations.closure.callable(
        *invocations.closure.prepared_model,
        prepared.value().evaluation.output.closure_request);
    if (!closure.has_value()) {
        return failure(closure.status(), closure.evidence().detail,
                       prepared.evidence().flags |
                           closure.evidence().flags);
    }
    const NumericalFlags flags = prepared.evidence().flags |
                                 closure.evidence().flags;

    NumericalEvidence evidence = product_evidence(
        kRigidFrozenFormKernelIdentity, "frozen-form", flags);
    evidence.evaluations = prepared.evidence().evaluations +
                           closure.evidence().evaluations;
    evidence.last_step = runtime_definition.algorithm.fixed_step_seconds;
    return NumericalOutcome<RigidFrozenFormInvocationEvaluation>::with_value(
        approximate_status(prepared.status()) ||
                approximate_status(closure.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        RigidFrozenFormInvocationEvaluation{
            RigidFrozenFormEvaluation{
                RigidFrozenFormOutput{closure.value().output.form_input()},
                RigidFrozenFormTelemetry{
                    prepared.value().evaluation.telemetry.air_data,
                    prepared.value().evaluation.telemetry.aerodynamic_lookup,
                    prepared.value().evaluation.telemetry.aerodynamic_query,
                    closure.value()}},
            prepared.value().invocation_results},
        evidence);
}

} // namespace

NumericalOutcome<RigidFrozenFormPreparationInvocationEvaluation>
RigidFrozenFormKernel::prepare_closure_request(
    const RigidFrozenFormRuntimeDefinition& definition,
    const RigidFrozenFormQuerySet& queries,
    const RigidStepInput& input) {
    return prepare_rigid_frozen_form_closure_request(
        definition, queries, input);
}

NumericalOutcome<RigidFrozenFormEvaluation>
RigidFrozenFormKernel::evaluate(const PreparedRigidStepModel& model,
                                const RigidStepInput& input) {
    return evaluate(
        RigidFrozenFormRuntimeDefinition{
            model.definition().inertial_frame,
            model.definition().algorithm},
        RigidFrozenFormInvocationSet{
            BoundAerodynamicTableQuery{
                0U, 0U, 0U, &model.aerodynamic_table_model(),
                &AerodynamicTableQueryKernel::evaluate},
            BoundForceMomentClosure{
                0U, 0U, 0U, &model.force_moment_closure_model(),
                &ForceMomentClosureKernel::evaluate}},
        input);
}

NumericalOutcome<RigidFrozenFormEvaluation>
RigidFrozenFormKernel::evaluate(
    const RigidFrozenFormRuntimeDefinition& definition,
    const RigidFrozenFormInvocationSet& invocations,
    const RigidStepInput& input) {
    const auto result = evaluate_with_invocation_results(
        definition, invocations, input);
    if (!result.has_value()) {
        return NumericalOutcome<RigidFrozenFormEvaluation>::failure(
            result.status(), result.evidence());
    }
    return NumericalOutcome<RigidFrozenFormEvaluation>::with_value(
        result.status(), result.value().evaluation, result.evidence());
}

NumericalOutcome<RigidFrozenFormInvocationEvaluation>
RigidFrozenFormKernel::evaluate_with_invocation_results(
    const RigidFrozenFormRuntimeDefinition& definition,
    const RigidFrozenFormInvocationSet& invocations,
    const RigidStepInput& input) {
    return evaluate_rigid_frozen_form(definition, invocations, input);
}

NumericalOutcome<RigidStepEvaluation> RigidStepKernel::evaluate(
    const PreparedRigidStepModel& model,
    const RigidStepInput& input) {
    const auto frozen = RigidFrozenFormKernel::evaluate(model, input);
    if (!frozen.has_value()) {
        return product_failure<RigidStepEvaluation>(
            frozen.status(), frozen.evidence().detail,
            frozen.evidence().flags);
    }
    return evaluate_held_form(model, input, frozen.value(),
                              frozen.status(), frozen.evidence());
}

NumericalOutcome<RigidStepEvaluation>
RigidStepKernel::evaluate_held_form(
    const PreparedRigidStepModel& model,
    const RigidStepInput& input,
    const RigidFrozenFormEvaluation& frozen_form,
    NumericalStatus frozen_form_status,
    const NumericalEvidence& frozen_form_evidence) {
    const auto& definition = model.definition();
    NumericalFlags flags = frozen_form_evidence.flags;
    bool approximate = approximate_status(frozen_form_status);
    const RigidFormInput& form_input = frozen_form.output.form_input;

    const auto initial_derivative = RigidDerivativeKernel::evaluate(
        definition.algorithm,
        RigidDerivativeInput{
            input.committed_state,
            input.mass_properties.mass_kilograms,
            input.mass_properties.inertia_about_center_of_mass,
            form_input,
            input.environment.gravity});
    if (!initial_derivative.has_value()) {
        return product_failure<RigidStepEvaluation>(
            initial_derivative.status(), initial_derivative.evidence().detail,
            flags | initial_derivative.evidence().flags);
    }
    flags |= initial_derivative.evidence().flags;
    approximate = approximate || approximate_status(
        initial_derivative.status());

    const StateVector committed = pack_state(input.committed_state);
    const auto derivative = [&](double, const StateVector& stage_state) {
        const RigidState typed_state = unpack_state(stage_state);
        const auto stage_derivative = RigidDerivativeKernel::evaluate(
            definition.algorithm,
            RigidDerivativeInput{
                typed_state,
                input.mass_properties.mass_kilograms,
                input.mass_properties.inertia_about_center_of_mass,
                form_input,
                input.environment.gravity});
        if (!stage_derivative.has_value()) {
            return NumericalOutcome<StateVector>::failure(
                stage_derivative.status(), stage_derivative.evidence());
        }
        return NumericalOutcome<StateVector>::with_value(
            stage_derivative.status(),
            pack_derivative(typed_state, stage_derivative.value()),
            stage_derivative.evidence());
    };
    const auto integrated = gnc::foundation::fixed_rk4_step(
        committed, input.context.interval_start.seconds,
        definition.algorithm.fixed_step_seconds, derivative,
        definition.algorithm.numerical_policy);
    if (!integrated.has_value()) {
        return product_failure<RigidStepEvaluation>(
            integrated.status(), "rk4", flags | integrated.evidence().flags);
    }
    flags |= integrated.evidence().flags;
    approximate = approximate || approximate_status(integrated.status());

    RigidState candidate_state = unpack_state(integrated.value());
    const auto candidate_attitude =
        gnc::foundation::prepare_passive_quaternion(
            candidate_state.attitude.value,
            definition.algorithm.candidate_attitude_policy);
    if (!candidate_attitude.has_value()) {
        return product_failure<RigidStepEvaluation>(
            candidate_attitude.status(), "candidate-attitude",
            flags | candidate_attitude.evidence().flags);
    }
    flags |= candidate_attitude.evidence().flags;
    approximate = approximate || approximate_status(candidate_attitude.status());
    candidate_state.attitude.value = candidate_attitude.value();
    if (!finite(candidate_state)) {
        return product_failure<RigidStepEvaluation>(
            NumericalStatus::NonFiniteOutput, "candidate", flags);
    }

    RigidStepTelemetry telemetry;
    telemetry.air_data = frozen_form.telemetry.air_data;
    telemetry.aerodynamic_lookup =
        frozen_form.telemetry.aerodynamic_lookup;
    telemetry.force_moment_closure =
        frozen_form.telemetry.force_moment_closure;
    telemetry.derivative_at_interval_start = initial_derivative.value();
    RigidStepOutput output;
    output.candidate.effective_at = input.context.interval_end;
    output.candidate.state = std::move(candidate_state);
    NumericalEvidence evidence = product_evidence(
        kRigidStepKernelIdentity, "one-step-candidate", flags);
    evidence.evaluations = integrated.evidence().evaluations +
                           frozen_form_evidence.evaluations;
    evidence.last_step = definition.algorithm.fixed_step_seconds;
    return NumericalOutcome<RigidStepEvaluation>::with_value(
        approximate ? NumericalStatus::Approximate
                    : NumericalStatus::Success,
        RigidStepEvaluation{std::move(output), std::move(telemetry)},
        evidence);
}

} // namespace gnc::packages::yyz
