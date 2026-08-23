#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gnc::contracts::ClockDomainIdentity;
using gnc::contracts::DataQuality;
using gnc::contracts::FrameIdentity;
using gnc::contracts::HalfOpenValidityInterval;
using gnc::contracts::IntervalSampleContext;
using gnc::contracts::SampleContext;
using gnc::contracts::SimulationInstant;
using gnc::foundation::FiniteCheck;
using gnc::foundation::Mat3;
using gnc::foundation::NumericalOutcome;
using gnc::foundation::NumericalPolicy;
using gnc::foundation::NumericalStatus;
using gnc::foundation::QuaternionNormalizationPolicy;
using gnc::foundation::QuaternionPolicy;
using gnc::foundation::Vec3;
using namespace gnc::packages::yyz;

constexpr std::string_view kFixtureId =
    "REF-YYZ-TWO-INTERVAL-MASS-COMMIT-001";
constexpr std::string_view kOracleId =
    "ORACLE-YYZ-TWO-INTERVAL-MASS-COMMIT-001";
constexpr std::string_view kReferenceModelId =
    "MODEL-YYZ-TWO-INTERVAL-MASS-COMMIT-001";
constexpr std::string_view kPropulsionFixtureId =
    "REF-YYZ-PROPULSION-RESPONSE-001";
constexpr std::string_view kPropulsionOracleId =
    "ORACLE-YYZ-PROPULSION-RESPONSE-001";
constexpr std::string_view kPropulsionReferenceModelId =
    "MODEL-YYZ-PROPULSION-RESPONSE-001";
constexpr std::string_view kMissionFixtureId =
    "REF-YYZ-MISSION-COMPOSITION-001";
constexpr std::string_view kMissionOracleId =
    "ORACLE-YYZ-MISSION-COMPOSITION-001";
constexpr std::string_view kMissionReferenceModelId =
    "MODEL-YYZ-FIXTURE-MISSION-COMPOSITION-004";
constexpr std::string_view kPropulsionSourceId = "propulsion.main";
constexpr std::string_view kMassStateId =
    "mass.fixture.yyz.vehicle@1";
constexpr std::string_view kInertialFrame =
    "frame.fixture.yyz.inertial-cartesian@1";
constexpr std::string_view kBodyFrame =
    "frame.fixture.yyz.body@1";
constexpr std::string_view kClock =
    "clock.fixture.yyz.simulation@1";
constexpr double kAbsolute = 2.0e-12;
constexpr double kRelative = 2.0e-12;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] bool near(double actual, double expected,
                        double absolute = kAbsolute,
                        double relative = kRelative) {
    const double scale =
        std::max({1.0, std::abs(actual), std::abs(expected)});
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::abs(actual - expected) <= absolute + relative * scale;
}

[[nodiscard]] bool near(const Vec3& actual, const Vec3& expected) {
    return near(actual(0), expected(0)) &&
           near(actual(1), expected(1)) &&
           near(actual(2), expected(2));
}

const BodyWrenchContribution& supplied_contribution(
    const RigidStepTelemetry& telemetry) {
    require(telemetry.force_moment_closure.telemetry.contributions.size() ==
                2U,
            "rigid closure contribution count differs");
    return telemetry.force_moment_closure.telemetry.contributions.back();
}

const ForceMomentClosureOutput& closure_output(
    const RigidStepTelemetry& telemetry) {
    return telemetry.force_moment_closure.output;
}

template <typename Value>
const Value& require_value(const NumericalOutcome<Value>& outcome,
                           std::string_view message) {
    require(outcome.has_value(), message);
    return outcome.value();
}

template <typename Value>
void expect_failure(const NumericalOutcome<Value>& outcome,
                    NumericalStatus expected,
                    std::string_view message) {
    require(!outcome.has_value() && outcome.status() == expected, message);
}

NumericalPolicy fixture_numerical_policy() {
    NumericalPolicy policy;
    policy.absolute_tolerance = kAbsolute;
    policy.relative_tolerance = kRelative;
    policy.finite_check = FiniteCheck::EveryStage;
    policy.zero_tolerance = 1.0e-14;
    policy.condition_limit = 1.0e12;
    return policy;
}

QuaternionPolicy fixture_quaternion_policy() {
    QuaternionPolicy policy;
    policy.numerical = fixture_numerical_policy();
    policy.normalization =
        QuaternionNormalizationPolicy::NormalizeWithFlag;
    return policy;
}

NumericalPolicy propulsion_numerical_policy() {
    NumericalPolicy policy = fixture_numerical_policy();
    policy.absolute_tolerance = 1.0e-12;
    policy.relative_tolerance = 1.0e-12;
    return policy;
}

RigidStepModelDefinition fixture_rigid_definition() {
    RigidStepModelDefinition definition;
    definition.force_moment_closure.metadata = {
        std::string(kForceMomentClosureModelIdentity),
        std::string(kForceMomentClosureModelVersion),
        gnc::model_sdk::ModelExecutionForm::Closure,
    };
    definition.inertial_frame = FrameIdentity{std::string(kInertialFrame)};
    definition.force_moment_closure.body_frame =
        FrameIdentity{std::string(kBodyFrame)};
    definition.force_moment_closure.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    definition.force_moment_closure.configuration_revision = 11;
    definition.force_moment_closure.numerical_policy =
        fixture_numerical_policy();
    definition.algorithm.fixed_step_seconds = 0.1;
    definition.algorithm.numerical_policy = fixture_numerical_policy();
    definition.algorithm.attitude_evaluation_policy =
        fixture_quaternion_policy();
    definition.algorithm.candidate_attitude_policy =
        fixture_quaternion_policy();

    auto& aero = definition.aerodynamics;
    aero.metadata = {
        std::string(kAerodynamicTableModelIdentity),
        std::string(kAerodynamicTableModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery,
    };
    aero.source_id = "aero.zero-force";
    aero.table_asset_id = "aero-table.fixture.yyz.zero@1";
    aero.configuration_id = "configuration.fixture.yyz.clean@1";
    aero.reference_area_square_meters = 1.0;
    aero.reference_span_meters = 1.0;
    aero.reference_chord_meters = 1.0;
    aero.body_origin_to_application.value = Vec3::Zero();
    auto& table = definition.aerodynamic_table;
    table.asset_schema_id =
        std::string(kAerodynamicTableAssetSchemaIdentity);
    table.asset_id = aero.table_asset_id;
    table.mach_axis = {0.05, 0.2};
    table.alpha_axis_radians = {-0.1, 0.1};
    table.beta_axis_radians = {-0.1, 0.1};
    table.coefficient_rows_ca_cy_cn_cl_cm_cn.assign(8U, {});
    return definition;
}

ScalarBurnMassDefinition fixture_mass_definition() {
    return {
        std::string(kScalarBurnMassModelIdentity),
        "0.1.0",
        std::string(kMassStateId),
        fixture_numerical_policy(),
    };
}

SuppliedPropulsionDefinition fixture_propulsion_definition() {
    SuppliedPropulsionDefinition definition;
    definition.model_id = std::string(kSuppliedPropulsionModelIdentity);
    definition.model_version = "0.1.0";
    definition.source_id = std::string(kPropulsionSourceId);
    definition.body_frame = FrameIdentity{std::string(kBodyFrame)};
    definition.clock_domain = ClockDomainIdentity{std::string(kClock)};
    definition.mass_state_id = std::string(kMassStateId);
    definition.numerical_policy = propulsion_numerical_policy();
    return definition;
}

RigidStepModelDefinition mission_rigid_definition() {
    RigidStepModelDefinition definition = fixture_rigid_definition();
    auto& aero = definition.aerodynamics;
    aero.source_id = "aero.body";
    aero.table_asset_id = "aero-table.fixture.yyz.multiaffine@1";
    aero.configuration_id = "configuration.fixture.yyz.clean@1";
    aero.body_origin_to_application.value =
        Vec3{0.2, 0.0, -25.0 / 18.0};
    auto& table = definition.aerodynamic_table;
    table.asset_id = aero.table_asset_id;
    table.mach_axis = {0.2, 0.6};
    table.alpha_axis_radians = {-0.1, 0.1};
    table.beta_axis_radians = {-0.05, 0.05};
    table.coefficient_rows_ca_cy_cn_cl_cm_cn = {
        {0.006, 0.0245, -0.0795, 0.005, 0.014, -0.00755},
        {0.006, -0.0245, -0.0805, -0.005, 0.014, 0.00755},
        {0.05, 0.0245, 0.0795, 0.005, -0.106, -0.00785},
        {0.05, -0.0245, 0.0805, -0.005, -0.106, 0.00785},
        {0.018, 0.0235, -0.0795, 0.005, 0.022, -0.00795},
        {0.018, -0.0235, -0.0805, -0.005, 0.022, 0.00795},
        {0.07, 0.0235, 0.0795, 0.005, -0.098, -0.00825},
        {0.07, -0.0235, 0.0805, -0.005, -0.098, 0.00825},
    };
    return definition;
}

ControlledPropelledRigidMassStepDefinition mission_control_definition() {
    ControlledPropelledRigidMassStepDefinition definition;
    definition.model_id =
        std::string(kControlledPropelledRigidMassStepModelIdentity);
    definition.model_version = "0.1.0";
    definition.combined_wrench_source_id =
        "propulsion.main+actuation.pitch-moment";
    definition.guidance.model_id =
        std::string(kAltitudePitchGuidanceModelIdentity);
    definition.guidance.model_version = "0.1.0";
    definition.guidance.inertial_frame =
        FrameIdentity{std::string(kInertialFrame)};
    definition.guidance.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    definition.guidance.configuration_revision = 11;
    definition.guidance.target_altitude_meters = 1000.0;
    definition.guidance.altitude_error_gain_radians_per_meter = 0.02;
    definition.guidance.vertical_speed_gain_radian_seconds_per_meter = 0.05;
    definition.guidance.pitch_command_limit_radians = 0.04;
    definition.guidance.attitude_policy = fixture_quaternion_policy();
    definition.controller.model_id =
        std::string(kPitchMomentControllerModelIdentity);
    definition.controller.model_version = "0.1.0";
    definition.controller.body_frame =
        FrameIdentity{std::string(kBodyFrame)};
    definition.controller.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    definition.controller.configuration_revision = 11;
    definition.controller.pitch_error_gain_newton_meters_per_radian =
        500.0;
    definition.controller
        .pitch_rate_gain_newton_meter_seconds_per_radian = 80.0;
    definition.controller.moment_command_limit_newton_meters = 25.0;
    definition.controller.numerical_policy = fixture_numerical_policy();
    definition.actuator.model_id =
        std::string(kIdealBodyMomentActuatorModelIdentity);
    definition.actuator.model_version = "0.1.0";
    definition.actuator.source_id =
        "actuation.fixture.yyz.ideal-body-moment@1";
    definition.actuator.body_frame =
        FrameIdentity{std::string(kBodyFrame)};
    definition.actuator.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    definition.actuator.configuration_revision = 11;
    definition.actuator.realization_gain = 1.0;
    definition.actuator.numerical_policy = fixture_numerical_policy();
    return definition;
}

CommittedMissionResultDefinition mission_result_definition() {
    CommittedMissionResultDefinition definition;
    definition.model_id =
        std::string(kCommittedMissionResultModelIdentity);
    definition.model_version = "0.1.0";
    definition.subject = "vehicle.fixture.yyz@1";
    definition.inertial_frame =
        FrameIdentity{std::string(kInertialFrame)};
    definition.body_frame = FrameIdentity{std::string(kBodyFrame)};
    definition.clock_domain = ClockDomainIdentity{std::string(kClock)};
    definition.mass_state_id = std::string(kMassStateId);
    definition.configuration_revision = 11;
    definition.numerical_policy = fixture_numerical_policy();
    definition.predicates = {{
        {
            "downrange-goal",
            MissionMetric::DownrangeMeters,
            MissionRelation::GreaterThanOrEqual,
            20.0,
            MissionAction::Complete,
            "downrange-goal",
            200,
        },
        {
            "duration-limit",
            MissionMetric::DurationSeconds,
            MissionRelation::GreaterThanOrEqual,
            0.2,
            MissionAction::Complete,
            "duration-complete",
            100,
        },
        {
            "remaining-mass-floor",
            MissionMetric::RemainingMassKilograms,
            MissionRelation::LessThanOrEqual,
            99.85,
            MissionAction::Abort,
            "remaining-mass-floor",
            300,
        },
    }};
    return definition;
}

[[nodiscard]] SimulationInstant instant_at(std::int64_t tick,
                                           double base_dt_seconds) {
    return {tick, base_dt_seconds * static_cast<double>(tick)};
}

[[nodiscard]] SampleContext sample_context_at(
    std::string_view frame, std::int64_t tick, double base_dt_seconds,
    std::int64_t configuration_revision) {
    return {
        FrameIdentity{std::string(frame)},
        ClockDomainIdentity{std::string(kClock)},
        instant_at(tick, base_dt_seconds),
        configuration_revision,
        DataQuality::Valid,
    };
}

[[nodiscard]] IntervalSampleContext interval_context_at(
    std::string_view frame, std::int64_t start_tick,
    std::int64_t end_tick, double base_dt_seconds,
    std::int64_t configuration_revision) {
    return {
        sample_context_at(frame, start_tick, base_dt_seconds,
                          configuration_revision),
        HalfOpenValidityInterval{
            instant_at(start_tick, base_dt_seconds),
            instant_at(end_tick, base_dt_seconds)},
    };
}

[[nodiscard]] SimulationInstant instant(std::int64_t tick) {
    return instant_at(tick, 0.1);
}

[[nodiscard]] SampleContext sample_context(
    std::string_view frame, std::int64_t tick) {
    return sample_context_at(frame, tick, 0.1, 11);
}

[[nodiscard]] IntervalSampleContext interval_context(
    std::string_view frame, std::int64_t tick) {
    return {
        sample_context(frame, tick),
        HalfOpenValidityInterval{instant(tick), instant(tick + 1)},
    };
}

[[nodiscard]] CommittedRigidMassBoundary mission_boundary(
    std::int64_t tick) {
    CommittedRigidMassBoundary boundary;
    boundary.rigid_context = sample_context(kInertialFrame, tick);
    boundary.mass_state.context = sample_context(kBodyFrame, tick);
    boundary.mass_state.mass_state_id = std::string(kMassStateId);
    boundary.mass_state.body_origin_to_center_of_mass.value =
        Vec3{0.2, 0.0, 0.0};
    boundary.mass_state.inertia_about_center_of_mass.value = Mat3::Zero();
    boundary.mass_state.inertia_about_center_of_mass.value.diagonal() =
        Vec3{10.0, 20.0, 30.0};
    boundary.rigid_state.attitude.value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
    if (tick == 0) {
        boundary.rigid_state.position.value = Vec3{0.0, 0.0, 1000.0};
        boundary.rigid_state.velocity.value = Vec3{110.0, 0.0, 0.0};
        boundary.rigid_state.angular_rate.value = Vec3::Zero();
        boundary.mass_state.mass_kilograms = 100.0;
        return boundary;
    }
    require(tick == 1, "unsupported mission opening tick");
    boundary.rigid_state.position.value =
        Vec3{10.995272058823529, 0.0, 999.95096675};
    boundary.rigid_state.velocity.value =
        Vec3{109.90544117647059, 0.0, -0.980665};
    boundary.rigid_state.angular_rate.value = Vec3::Zero();
    boundary.mass_state.mass_kilograms = 99.95;
    return boundary;
}

[[nodiscard]] PropelledRigidMassIntervalInput mission_control_interval(
    std::int64_t tick) {
    PropelledRigidMassIntervalInput interval;
    interval.context = {
        FrameIdentity{std::string(kInertialFrame)},
        FrameIdentity{std::string(kBodyFrame)},
        ClockDomainIdentity{std::string(kClock)},
        instant(tick),
        instant(tick + 1),
        11,
        DataQuality::Valid,
    };
    interval.environment.context = sample_context(kInertialFrame, tick);
    interval.environment.gravity.value = Vec3{0.0, 0.0, -9.80665};
    interval.environment.velocity_airmass.value = Vec3{10.0, 0.0, 0.0};
    interval.environment.density_kilograms_per_cubic_meter = 1.225;
    interval.environment.speed_of_sound_meters_per_second = 340.0;
    interval.propulsion.context = interval_context(kBodyFrame, tick);
    interval.propulsion.thrust_magnitude_newtons = 100.0;
    interval.propulsion.thrust_direction.value = Vec3::UnitX();
    interval.propulsion.center_of_mass_to_application.value =
        Vec3{0.0, 0.2, 0.0};
    interval.propulsion.intrinsic_moment_at_application.value =
        Vec3{0.0, 0.0, 20.0};
    interval.propulsion.fuel_consumption_rate_kilograms_per_second = 0.5;
    return interval;
}

[[nodiscard]] RigidMassIntervalInput interval_input(std::int64_t tick) {
    RigidMassIntervalInput interval;
    interval.context = {
        FrameIdentity{std::string(kInertialFrame)},
        FrameIdentity{std::string(kBodyFrame)},
        ClockDomainIdentity{std::string(kClock)},
        instant(tick),
        instant(tick + 1),
        11,
        DataQuality::Valid,
    };
    interval.environment.context = sample_context(kInertialFrame, tick);
    interval.environment.gravity.value = Vec3::Zero();
    interval.environment.velocity_airmass.value = Vec3::Zero();
    interval.environment.density_kilograms_per_cubic_meter = 1.0;
    interval.environment.speed_of_sound_meters_per_second = 100.0;
    interval.supplied_wrench.context = interval_context(kBodyFrame, tick);
    interval.supplied_wrench.source_id = "propulsion.supplied-force";
    interval.supplied_wrench.force.value = Vec3{240.0, 0.0, 0.0};
    interval.supplied_wrench.body_origin_to_application.value =
        Vec3{0.2, -0.1, 0.05};
    interval.supplied_wrench.intrinsic_moment_at_application.value =
        Vec3::Zero();
    interval.mass_flow.context = interval_context(kBodyFrame, tick);
    interval.mass_flow.mass_state_id = std::string(kMassStateId);
    interval.mass_flow.fuel_consumption_rate_kilograms_per_second = 0.5;
    return interval;
}

[[nodiscard]] TwoIntervalMassCommitInput fixture_input() {
    TwoIntervalMassCommitInput input;
    input.opening_boundary.rigid_context =
        sample_context(kInertialFrame, 0);
    input.opening_boundary.rigid_state.position.value = Vec3::Zero();
    input.opening_boundary.rigid_state.velocity.value =
        Vec3{10.0, 0.0, 0.0};
    input.opening_boundary.rigid_state.attitude.value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
    input.opening_boundary.rigid_state.angular_rate.value = Vec3::Zero();

    auto& mass = input.opening_boundary.mass_state;
    mass.context = sample_context(kBodyFrame, 0);
    mass.mass_state_id = std::string(kMassStateId);
    mass.mass_kilograms = 120.0;
    mass.body_origin_to_center_of_mass.value = Vec3{0.2, -0.1, 0.05};
    mass.inertia_about_center_of_mass.value <<
        12.0, 1.0, 0.5,
        1.0, 20.0, 2.0,
        0.5, 2.0, 30.0;
    input.intervals = {interval_input(0), interval_input(1)};
    return input;
}

struct PropulsionCaseInput {
    std::string_view id;
    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0;
    double base_dt_seconds = 0.0;
    std::int64_t configuration_revision = 0;
    double thrust_magnitude_newtons = 0.0;
    Vec3 thrust_direction = Vec3::UnitX();
    Vec3 center_of_mass_to_application = Vec3::Zero();
    Vec3 intrinsic_moment_at_application = Vec3::Zero();
    double fuel_consumption_rate_kilograms_per_second = 0.0;
    double committed_mass_kilograms = 0.0;
};

[[nodiscard]] std::array<PropulsionCaseInput, 3U>
fixture_propulsion_cases() {
    return {{
        {
            "CASE-YYZ-PROPULSION-OFF-AXIS-CONSUMERS",
            20,
            25,
            0.1,
            8,
            500.0,
            Vec3{0.6, 0.8, 0.0},
            Vec3{-0.5, 0.25, 0.1},
            Vec3{1.0, -2.0, 3.0},
            0.5,
            120.0,
        },
        {
            "CASE-YYZ-PROPULSION-THREE-DIMENSIONAL-WRENCH",
            3,
            7,
            0.25,
            9,
            50.0,
            Vec3{0.8, 0.0, 0.6},
            Vec3{0.2, -0.1, 0.3},
            Vec3{-4.0, 5.0, -6.0},
            0.0,
            10.0,
        },
        {
            "CASE-YYZ-PROPULSION-ZERO-RESPONSE",
            100,
            101,
            0.02,
            10,
            0.0,
            Vec3{1.0, 0.0, 0.0},
            Vec3{-2.0, 3.0, -4.0},
            Vec3::Zero(),
            0.0,
            5.0,
        },
    }};
}

[[nodiscard]] SuppliedPropulsionInput propulsion_input(
    const PropulsionCaseInput& fixture) {
    SuppliedPropulsionInput input;
    input.context = interval_context_at(
        kBodyFrame, fixture.start_tick, fixture.end_tick,
        fixture.base_dt_seconds, fixture.configuration_revision);
    input.thrust_magnitude_newtons = fixture.thrust_magnitude_newtons;
    input.thrust_direction.value = fixture.thrust_direction;
    input.center_of_mass_to_application.value =
        fixture.center_of_mass_to_application;
    input.intrinsic_moment_at_application.value =
        fixture.intrinsic_moment_at_application;
    input.fuel_consumption_rate_kilograms_per_second =
        fixture.fuel_consumption_rate_kilograms_per_second;
    return input;
}

[[nodiscard]] MassState propulsion_mass_state(
    const PropulsionCaseInput& fixture) {
    MassState state;
    state.context = sample_context_at(
        kBodyFrame, fixture.start_tick, fixture.base_dt_seconds,
        fixture.configuration_revision);
    state.mass_state_id = std::string(kMassStateId);
    state.mass_kilograms = fixture.committed_mass_kilograms;
    state.body_origin_to_center_of_mass.value = Vec3::Zero();
    state.inertia_about_center_of_mass.value = Mat3::Identity();
    return state;
}

struct PropulsionCaseResult {
    std::string id;
    SuppliedPropulsionInput input;
    SuppliedPropulsionOutput response;
    ScalarBurnMassOutput mass;
    double committed_mass_kilograms = 0.0;
};

[[nodiscard]] PropulsionCaseResult evaluate_propulsion_case(
    const SuppliedPropulsionDefinition& propulsion_definition,
    const ScalarBurnMassDefinition& mass_definition,
    const PropulsionCaseInput& fixture) {
    const SuppliedPropulsionInput input = propulsion_input(fixture);
    const auto response_outcome = SuppliedPropulsionKernel::evaluate(
        propulsion_definition, input);
    const auto& response = require_value(
        response_outcome, "supplied propulsion response failed");
    const auto mass_outcome = ScalarBurnMassKernel::evaluate(
        mass_definition, propulsion_mass_state(fixture),
        response.mass_flow, fixture_numerical_policy());
    const auto& mass = require_value(
        mass_outcome, "propulsion mass consumer failed");
    return {
        std::string(fixture.id),
        input,
        response,
        mass,
        fixture.committed_mass_kilograms,
    };
}

[[nodiscard]] double max_abs_difference(const Vec3& lhs,
                                        const Vec3& rhs) {
    return (lhs - rhs).cwiseAbs().maxCoeff();
}

struct PropulsionEquivalenceResult {
    double force_and_response_max_abs_difference = 0.0;
    double application_wrench_max_abs_difference = 0.0;
    double summed_consumed_fuel_mass_kilograms = 0.0;
    double consumed_fuel_mass_difference_kilograms = 0.0;
    double sequential_final_mass_candidate_kilograms = 0.0;
    double final_mass_candidate_difference_kilograms = 0.0;
};

[[nodiscard]] PropulsionEquivalenceResult propulsion_partition_equivalence(
    const SuppliedPropulsionDefinition& propulsion_definition,
    const ScalarBurnMassDefinition& mass_definition,
    const PropulsionCaseInput& whole_fixture) {
    const PropulsionCaseResult whole = evaluate_propulsion_case(
        propulsion_definition, mass_definition, whole_fixture);
    PropulsionCaseInput first_fixture = whole_fixture;
    first_fixture.end_tick = 22;
    const PropulsionCaseResult first = evaluate_propulsion_case(
        propulsion_definition, mass_definition, first_fixture);
    PropulsionCaseInput second_fixture = whole_fixture;
    second_fixture.start_tick = 22;
    second_fixture.committed_mass_kilograms =
        first.mass.candidate.state.mass_kilograms;
    const PropulsionCaseResult second = evaluate_propulsion_case(
        propulsion_definition, mass_definition, second_fixture);

    const auto response_difference = [&](const PropulsionCaseResult& part) {
        return std::max({
            max_abs_difference(
                whole.response.supplied_body_wrench.force.value,
                part.response.supplied_body_wrench.force.value),
            max_abs_difference(
                whole.response.supplied_body_wrench
                    .center_of_mass_to_application.value,
                part.response.supplied_body_wrench
                    .center_of_mass_to_application.value),
            max_abs_difference(
                whole.response.supplied_body_wrench
                    .intrinsic_moment_at_application.value,
                part.response.supplied_body_wrench
                    .intrinsic_moment_at_application.value),
        });
    };
    const auto wrench_difference = [&](const PropulsionCaseResult& part) {
        return std::max({
            max_abs_difference(
                whole.response.supplied_body_wrench.force.value,
                part.response.supplied_body_wrench.force.value),
            max_abs_difference(
                whole.response.supplied_body_wrench
                    .intrinsic_moment_at_application.value,
                part.response.supplied_body_wrench
                    .intrinsic_moment_at_application.value),
            max_abs_difference(whole.response.lever_arm_moment.value,
                               part.response.lever_arm_moment.value),
            max_abs_difference(
                whole.response.moment_about_center_of_mass.value,
                part.response.moment_about_center_of_mass.value),
        });
    };
    const double summed_consumed =
        first.mass.consumed_mass_kilograms +
        second.mass.consumed_mass_kilograms;
    PropulsionEquivalenceResult result;
    result.force_and_response_max_abs_difference =
        std::max(response_difference(first), response_difference(second));
    result.application_wrench_max_abs_difference =
        std::max(wrench_difference(first), wrench_difference(second));
    result.summed_consumed_fuel_mass_kilograms = summed_consumed;
    result.consumed_fuel_mass_difference_kilograms =
        std::abs(summed_consumed - whole.mass.consumed_mass_kilograms);
    result.sequential_final_mass_candidate_kilograms =
        second.mass.candidate.state.mass_kilograms;
    result.final_mass_candidate_difference_kilograms = std::abs(
        second.mass.candidate.state.mass_kilograms -
        whole.mass.candidate.state.mass_kilograms);
    require(near(result.force_and_response_max_abs_difference, 0.0) &&
                near(result.application_wrench_max_abs_difference, 0.0) &&
                near(result.consumed_fuel_mass_difference_kilograms, 0.0) &&
                near(result.final_mass_candidate_difference_kilograms, 0.0),
            "propulsion interval partition equivalence failed");
    return result;
}

struct PropulsionProbeBundle {
    std::vector<PropulsionCaseResult> cases;
    PropulsionEquivalenceResult equivalence;
    std::vector<std::string> invalid_input_rejections;
    PropelledFrozenRigidMassStepOutput consumer;
    std::vector<std::string> direct_checks;
};

[[nodiscard]] PropulsionProbeBundle run_propulsion_probe(
    const PreparedRigidStepModel& prepared,
    const ScalarBurnMassDefinition& mass_definition,
    const CommittedRigidMassBoundary& opening_boundary) {
    const SuppliedPropulsionDefinition propulsion_definition =
        fixture_propulsion_definition();
    const auto fixtures = fixture_propulsion_cases();
    std::vector<PropulsionCaseResult> cases;
    cases.reserve(fixtures.size());
    for (const auto& fixture : fixtures) {
        cases.emplace_back(evaluate_propulsion_case(
            propulsion_definition, mass_definition, fixture));
    }

    const auto& off_axis = cases[0];
    require(near(off_axis.response.supplied_body_wrench.force.value,
                 Vec3{300.0, 400.0, 0.0}) &&
                near(off_axis.response.lever_arm_moment.value,
                     Vec3{-40.0, 30.0, -275.0}) &&
                near(off_axis.response.moment_about_center_of_mass.value,
                     Vec3{-39.0, 28.0, -272.0}) &&
                near(off_axis.mass.consumed_mass_kilograms, 0.25) &&
                near(off_axis.mass.candidate.state.mass_kilograms, 119.75),
            "off-axis propulsion anchors differ");
    require(near(cases[1].response.supplied_body_wrench.force.value,
                 Vec3{40.0, 0.0, 30.0}) &&
                near(cases[1].response.lever_arm_moment.value,
                     Vec3{-3.0, 6.0, 4.0}) &&
                near(cases[1].response.moment_about_center_of_mass.value,
                     Vec3{-7.0, 11.0, -2.0}) &&
                near(cases[1].mass.candidate.state.mass_kilograms, 10.0),
            "three-dimensional propulsion anchors differ");
    require(near(cases[2].response.supplied_body_wrench.force.value,
                 Vec3::Zero()) &&
                near(cases[2].response.moment_about_center_of_mass.value,
                     Vec3::Zero()) &&
                near(cases[2].mass.candidate.state.mass_kilograms, 5.0),
            "zero propulsion anchors differ");
    std::vector<std::string> checks{
        "propulsion-three-oracle-cases"};

    const PropulsionEquivalenceResult equivalence =
        propulsion_partition_equivalence(
            propulsion_definition, mass_definition, fixtures[0]);
    checks.emplace_back("propulsion-interval-partition-equivalence");

    std::vector<std::string> invalid;
    const auto reject_response = [&](std::string_view id,
                                     const SuppliedPropulsionInput& input,
                                     NumericalStatus status) {
        expect_failure(SuppliedPropulsionKernel::evaluate(
                           propulsion_definition, input),
                       status, id);
        invalid.emplace_back(id);
    };
    SuppliedPropulsionInput mutated = propulsion_input(fixtures[0]);
    mutated.context.sample.frame.id = "frame.other@1";
    reject_response("INVALID-YYZ-PROPULSION-FRAME-MISMATCH", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.context.sample.clock_domain.id = "clock.other@1";
    reject_response("INVALID-YYZ-PROPULSION-CLOCK-MISMATCH", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.context.sample.sample_time = instant_at(21, 0.1);
    reject_response(
        "INVALID-YYZ-PROPULSION-SAMPLE-INTERVAL-MISMATCH", mutated,
        NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.context.sample.configuration_revision = -1;
    reject_response("INVALID-YYZ-PROPULSION-REVISION", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.context.validity.effective_until.seconds =
        mutated.context.validity.effective_from.seconds;
    reject_response("INVALID-YYZ-PROPULSION-NONPOSITIVE-DT", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.thrust_magnitude_newtons = -1.0;
    reject_response("INVALID-YYZ-PROPULSION-NEGATIVE-THRUST", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.thrust_direction.value = Vec3{2.0, 0.0, 0.0};
    reject_response("INVALID-YYZ-PROPULSION-NONUNIT-DIRECTION", mutated,
                    NumericalStatus::DomainError);
    mutated = propulsion_input(fixtures[0]);
    mutated.intrinsic_moment_at_application.value(0) =
        std::numeric_limits<double>::infinity();
    reject_response("INVALID-YYZ-PROPULSION-NONFINITE-MOMENT", mutated,
                    NumericalStatus::NonFiniteInput);
    mutated = propulsion_input(fixtures[0]);
    mutated.fuel_consumption_rate_kilograms_per_second = -1.0;
    reject_response("INVALID-YYZ-PROPULSION-NEGATIVE-CONSUMPTION", mutated,
                    NumericalStatus::DomainError);

    const auto depleted_response = SuppliedPropulsionKernel::evaluate(
        propulsion_definition, propulsion_input(fixtures[0]));
    const auto& depleted_response_value = require_value(
        depleted_response, "depleted-mass response setup failed");
    PropulsionCaseInput depleted_fixture = fixtures[0];
    depleted_fixture.committed_mass_kilograms = 0.1;
    expect_failure(ScalarBurnMassKernel::evaluate(
                       mass_definition,
                       propulsion_mass_state(depleted_fixture),
                       depleted_response_value.mass_flow,
                       fixture_numerical_policy()),
                   NumericalStatus::DomainError,
                   "INVALID-YYZ-PROPULSION-DEPLETED-MASS");
    invalid.emplace_back("INVALID-YYZ-PROPULSION-DEPLETED-MASS");
    require(invalid.size() == 10U,
            "propulsion invalid-input coverage differs");
    checks.emplace_back("propulsion-ten-invalid-input-rejections");

    PropulsionCaseInput consumer_fixture = fixtures[0];
    consumer_fixture.start_tick = 0;
    consumer_fixture.end_tick = 1;
    consumer_fixture.configuration_revision = 11;
    consumer_fixture.committed_mass_kilograms =
        opening_boundary.mass_state.mass_kilograms;
    PropelledRigidMassIntervalInput consumer_input;
    const RigidMassIntervalInput established_interval = interval_input(0);
    consumer_input.context = established_interval.context;
    consumer_input.environment = established_interval.environment;
    consumer_input.propulsion = propulsion_input(consumer_fixture);
    const auto consumer_outcome =
        PropelledFrozenRigidMassStepKernel::evaluate(
            prepared, mass_definition, propulsion_definition,
            opening_boundary, consumer_input);
    const auto& consumer = require_value(
        consumer_outcome,
        "propulsion-to-atomic-boundary consumer failed");
    require(near(consumer.propulsion.supplied_body_wrench.force.value,
                 Vec3{300.0, 400.0, 0.0}) &&
                near(consumer.propulsion.moment_about_center_of_mass.value,
                     Vec3{-39.0, 28.0, -272.0}) &&
                near(supplied_contribution(
                         consumer.atomic_boundary.rigid_step.telemetry)
                         .force.value,
                     Vec3{300.0, 400.0, 0.0}) &&
                near(supplied_contribution(
                         consumer.atomic_boundary.rigid_step.telemetry)
                         .moment_about_center_of_mass.value,
                     Vec3{-39.0, 28.0, -272.0}) &&
                near(consumer.atomic_boundary.mass_evolution
                         .consumed_mass_kilograms,
                     0.05) &&
                near(consumer.atomic_boundary.mass_evolution
                         .candidate.state.mass_kilograms,
                     119.95) &&
                consumer.atomic_boundary.candidate.effective_at.tick == 1,
            "propulsion atomic consumer closure differs");
    checks.emplace_back("propulsion-to-atomic-boundary-single-transport");

    return {
        std::move(cases),
        equivalence,
        std::move(invalid),
        consumer,
        std::move(checks),
    };
}

struct MissionControlProbeBundle {
    ControlledPropelledRigidMassStepOutput accepted;
    std::vector<std::string> direct_checks;
};

[[nodiscard]] MissionControlProbeBundle run_mission_control_probe() {
    const auto prepared_outcome =
        prepare_rigid_step_model(mission_rigid_definition());
    const auto& prepared = require_value(
        prepared_outcome, "mission rigid model preparation failed");
    const ScalarBurnMassDefinition mass_definition =
        fixture_mass_definition();
    const SuppliedPropulsionDefinition propulsion_definition =
        fixture_propulsion_definition();
    const ControlledPropelledRigidMassStepDefinition control_definition =
        mission_control_definition();
    const CommittedRigidMassBoundary opening = mission_boundary(1);
    const PropelledRigidMassIntervalInput interval =
        mission_control_interval(1);
    const auto accepted_outcome =
        ControlledPropelledRigidMassStepKernel::evaluate(
            prepared, mass_definition, propulsion_definition,
            control_definition, opening, interval);
    const auto& accepted = require_value(
        accepted_outcome, "mission control product evaluation failed");

    require(accepted.observation.context.sample_time.tick == 1 &&
                near(accepted.observation.state.position.value,
                     opening.rigid_state.position.value) &&
                near(accepted.guidance.measured_pitch_radians, 0.0) &&
                near(accepted.guidance.altitude_error_meters, 0.04903325) &&
                near(accepted.guidance.altitude_feedback_radians,
                     0.000980665) &&
                near(accepted.guidance.vertical_speed_feedback_radians,
                     0.04903325) &&
                near(accepted.guidance.raw_pitch_command_radians,
                     0.050013915) &&
                near(accepted.guidance.pitch_command_radians, 0.04) &&
                accepted.guidance.saturated,
            "committed observation or guidance anchors differ");
    std::vector<std::string> checks{
        "mission-committed-observation-to-bounded-guidance"};

    require(near(accepted.controller.pitch_error_radians, 0.04) &&
                near(accepted.controller.proportional_moment_newton_meters,
                     20.0) &&
                near(accepted.controller.rate_damping_moment_newton_meters,
                     0.0) &&
                near(accepted.controller.raw_moment_command_newton_meters,
                     20.0) &&
                near(accepted.controller.moment_command_newton_meters,
                     20.0) &&
                !accepted.controller.saturated &&
                accepted.actuator.context.sample.sample_time.tick == 1 &&
                accepted.actuator.context.validity.effective_until.tick ==
                    2 &&
                near(accepted.actuator.moment_about_center_of_mass.value,
                     Vec3{0.0, 20.0, 0.0}),
            "controller or ideal actuation anchors differ");
    checks.emplace_back("mission-controller-to-current-cycle-actuation");

    const auto& boundary = accepted.atomic_boundary;
    require(near(accepted.propulsion.moment_about_center_of_mass.value,
                 Vec3::Zero()) &&
                near(supplied_contribution(boundary.rigid_step.telemetry)
                         .force.value,
                     Vec3{100.0, 0.0, 0.0}) &&
                near(supplied_contribution(boundary.rigid_step.telemetry)
                         .moment_about_center_of_mass.value,
                     Vec3{0.0, 20.0, 0.0}) &&
                near(closure_output(boundary.rigid_step.telemetry)
                         .moment_total_about_center_of_mass.value,
                     Vec3{0.0, 36.766216427351054, 0.0}) &&
                near(boundary.mass_evolution.candidate.state.mass_kilograms,
                     99.9) &&
                boundary.candidate.effective_at.tick == 2,
            "controlled propulsion atomic boundary differs");
    checks.emplace_back("mission-control-propulsion-single-transport");

    require(near(boundary.rigid_step.output.candidate.state.position.value,
                 Vec3{21.981798901675346, 0.0,
                      999.8062748637297}) &&
                near(boundary.rigid_step.output.candidate.state.velocity.value,
                     Vec3{109.82516983067299, 0.0,
                          -1.9130498687217244}) &&
                near(boundary.rigid_step.output.candidate.state.angular_rate
                         .value,
                     Vec3{0.0, 0.18383108213675527, 0.0}),
            "controlled interval candidate differs from mission oracle");
    checks.emplace_back("mission-controlled-candidate-oracle-anchors");

    CommittedRigidMassBoundary non_pitch = opening;
    non_pitch.rigid_state.attitude.value =
        gnc::foundation::quaternion_from_wxyz(
            std::sqrt(1.0 - 0.01 * 0.01), 0.01, 0.0, 0.0);
    const auto rolled_outcome =
        ControlledPropelledRigidMassStepKernel::evaluate(
            prepared, mass_definition, propulsion_definition,
            control_definition, non_pitch, interval);
    const auto& rolled = require_value(
        rolled_outcome, "roll-only observation was rejected");
    require(near(rolled.guidance.measured_pitch_radians, 0.0) &&
                near(rolled.guidance.pitch_command_radians,
                     accepted.guidance.pitch_command_radians) &&
                near(rolled.controller.moment_command_newton_meters,
                     accepted.controller.moment_command_newton_meters),
            "roll-only attitude changed the forward-axis pitch control");
    ControlledPropelledRigidMassStepDefinition nonideal =
        control_definition;
    nonideal.actuator.realization_gain = 1.1;
    expect_failure(ControlledPropelledRigidMassStepKernel::evaluate(
                       prepared, mass_definition, propulsion_definition,
                       nonideal, opening, interval),
                   NumericalStatus::DomainError,
                   "nonunit ideal actuator gain survived");
    CommittedRigidMassBoundary stale = opening;
    stale.rigid_context.sample_time = instant(0);
    expect_failure(ControlledPropelledRigidMassStepKernel::evaluate(
                       prepared, mass_definition, propulsion_definition,
                       control_definition, stale, interval),
                   NumericalStatus::DomainError,
                   "stale committed observation survived");
    checks.emplace_back(
        "mission-control-general-attitude-and-two-invalid-inputs");
    return {accepted, std::move(checks)};
}

struct MissionTwoIntervalProbeBundle {
    TwoIntervalControlledPropelledCommitOutput accepted;
    std::vector<std::string> direct_checks;
};

[[nodiscard]] MissionTwoIntervalProbeBundle
run_mission_two_interval_probe() {
    const auto prepared_outcome =
        prepare_rigid_step_model(mission_rigid_definition());
    const auto& prepared = require_value(
        prepared_outcome,
        "two-interval mission rigid model preparation failed");
    const ScalarBurnMassDefinition mass_definition =
        fixture_mass_definition();
    const SuppliedPropulsionDefinition propulsion_definition =
        fixture_propulsion_definition();
    const ControlledPropelledRigidMassStepDefinition control_definition =
        mission_control_definition();
    TwoIntervalControlledPropelledCommitInput input;
    input.opening_boundary = mission_boundary(0);
    input.intervals = {
        mission_control_interval(0), mission_control_interval(1)};
    const auto accepted_outcome =
        TwoIntervalControlledPropelledCommitKernel::evaluate(
            prepared, mass_definition, propulsion_definition,
            control_definition, input);
    const auto& accepted = require_value(
        accepted_outcome,
        "two-interval mission product evaluation failed");
    const auto& first = accepted.intervals[0];
    const auto& second = accepted.intervals[1];

    require(first.staged.observation.context.sample_time.tick == 0 &&
                near(first.staged.guidance.pitch_command_radians, 0.0) &&
                near(first.staged.controller
                         .moment_command_newton_meters,
                     0.0) &&
                near(first.staged.actuator
                         .moment_about_center_of_mass.value,
                     Vec3::Zero()),
            "opening committed feedback differs");
    std::vector<std::string> checks{
        "mission-interval-zero-committed-feedback"};

    const CommittedRigidMassBoundary tick_one_reference =
        mission_boundary(1);
    require(first.closing_commit.rigid_context.sample_time.tick == 1 &&
                first.closing_commit.mass_state.context.sample_time.tick ==
                    1 &&
                near(first.closing_commit.rigid_state.position.value,
                     tick_one_reference.rigid_state.position.value) &&
                near(first.closing_commit.rigid_state.velocity.value,
                     tick_one_reference.rigid_state.velocity.value) &&
                near(first.closing_commit.mass_state.mass_kilograms,
                     99.95),
            "mission tick-one atomic commit differs");
    checks.emplace_back("mission-first-controlled-atomic-commit");

    require(second.staged.observation.context.sample_time.tick == 1 &&
                near(second.staged.observation.state.position.value,
                     first.closing_commit.rigid_state.position.value) &&
                near(second.staged.observation.state.velocity.value,
                     first.closing_commit.rigid_state.velocity.value) &&
                near(second.staged.atomic_boundary
                         .projected_committed_mass.mass_kilograms,
                     first.closing_commit.mass_state.mass_kilograms) &&
                near(second.staged.guidance.pitch_command_radians, 0.04) &&
                near(second.staged.controller
                         .moment_command_newton_meters,
                     20.0) &&
                near(second.staged.actuator
                         .moment_about_center_of_mass.value,
                     Vec3{0.0, 20.0, 0.0}),
            "second interval did not recompute from committed pair");
    checks.emplace_back("mission-next-interval-recomputes-feedback");

    const auto terminal_quaternion =
        gnc::foundation::quaternion_to_wxyz(
            accepted.terminal_boundary.rigid_state.attitude.value);
    require(accepted.terminal_boundary.rigid_context.sample_time.tick == 2 &&
                accepted.terminal_boundary.mass_state.context.sample_time
                        .tick == 2 &&
                near(accepted.terminal_boundary.rigid_state.position.value,
                     Vec3{21.981798901675346, 0.0,
                          999.8062748637297}) &&
                near(accepted.terminal_boundary.rigid_state.velocity.value,
                     Vec3{109.82516983067299, 0.0,
                          -1.9130498687217244}) &&
                near(terminal_quaternion[0], 0.9999894394538129) &&
                near(terminal_quaternion[2],
                     -0.004595756830941491) &&
                near(accepted.terminal_boundary.rigid_state
                         .angular_rate.value,
                     Vec3{0.0, 0.18383108213675527, 0.0}) &&
                near(accepted.terminal_boundary.mass_state.mass_kilograms,
                     99.9),
            "mission terminal committed boundary differs");
    checks.emplace_back("mission-two-interval-terminal-oracle-anchors");

    TwoIntervalControlledPropelledCommitInput gap = input;
    gap.intervals[1] = mission_control_interval(2);
    expect_failure(
        TwoIntervalControlledPropelledCommitKernel::evaluate(
            prepared, mass_definition, propulsion_definition,
            control_definition, gap),
        NumericalStatus::DomainError,
        "controlled interval gap survived");
    checks.emplace_back("mission-controlled-interval-gap-rejection");
    return {accepted, std::move(checks)};
}

struct MissionResultProbeBundle {
    CommittedMissionResultOutput accepted;
    std::vector<std::string> direct_checks;
};

[[nodiscard]] MissionResultProbeBundle run_mission_result_probe(
    const TwoIntervalControlledPropelledCommitOutput& trajectory) {
    CommittedMissionResultInput input;
    input.committed_samples = {
        mission_boundary(0),
        trajectory.intervals[0].closing_commit,
        trajectory.intervals[1].closing_commit,
    };
    const CommittedMissionResultDefinition definition =
        mission_result_definition();
    const auto accepted_outcome =
        CommittedMissionResultKernel::evaluate(definition, input);
    const auto& accepted = require_value(
        accepted_outcome, "committed mission result evaluation failed");

    const auto& terminal = accepted.metrics.terminal;
    require(accepted.status == MissionResultStatus::Completed &&
                accepted.initial_tick == 0 && accepted.final_tick == 2 &&
                near(accepted.final_time_seconds, 0.2) &&
                accepted.metrics.evaluated_sample_count == 3U &&
                near(terminal.duration_seconds, 0.2) &&
                near(terminal.downrange_meters,
                     21.981798901675346) &&
                near(terminal.vertical_displacement_meters,
                     -0.1937251362702962) &&
                near(terminal.remaining_mass_kilograms, 99.9) &&
                near(terminal.consumed_mass_kilograms, 0.1) &&
                near(terminal.speed_meters_per_second,
                     109.84183032040381),
            "committed mission metric anchors differ");
    std::vector<std::string> checks{
        "mission-result-committed-sample-metrics"};

    require(accepted.termination.action == MissionAction::Complete &&
                accepted.termination.reason_code == "downrange-goal" &&
                accepted.termination.priority == 200 &&
                near(accepted.termination.trigger_time_seconds, 0.2) &&
                accepted.terminal_predicates[0].predicate_id ==
                    "downrange-goal" &&
                accepted.terminal_predicates[0].met &&
                accepted.terminal_predicates[1].predicate_id ==
                    "duration-limit" &&
                accepted.terminal_predicates[1].met &&
                accepted.terminal_predicates[2].predicate_id ==
                    "remaining-mass-floor" &&
                !accepted.terminal_predicates[2].met,
            "mission predicate priority decision differs");
    checks.emplace_back("mission-result-inclusive-priority-decision");

    require(near(accepted.metrics.peak_speed_meters_per_second, 110.0) &&
                accepted.metrics.peak_speed_tick == 0 &&
                near(accepted.metrics.maximum_downrange_meters,
                     terminal.downrange_meters) &&
                accepted.metrics.maximum_downrange_tick == 2 &&
                near(accepted.metrics.minimum_remaining_mass_kilograms,
                     99.9) &&
                accepted.metrics.minimum_remaining_mass_tick == 2 &&
                accepted.terminal_boundary.rigid_context.sample_time.tick ==
                    trajectory.terminal_boundary.rigid_context.sample_time
                        .tick &&
                near(accepted.terminal_boundary.rigid_state.position.value,
                     trajectory.terminal_boundary.rigid_state.position.value) &&
                near(accepted.terminal_boundary.mass_state.mass_kilograms,
                     trajectory.terminal_boundary.mass_state.mass_kilograms),
            "mission summary or terminal committed identity differs");
    checks.emplace_back("mission-result-terminal-committed-boundary");

    CommittedMissionResultDefinition earliest_definition = definition;
    earliest_definition.predicates[1].threshold = 10.0;
    CommittedMissionResultInput earliest_input = input;
    const double opening_downrange =
        earliest_input.committed_samples[0]
            .rigid_state.position.value(0);
    earliest_input.committed_samples[1]
        .rigid_state.position.value(0) = opening_downrange + 25.0;
    earliest_input.committed_samples[2]
        .rigid_state.position.value(0) = opening_downrange + 10.0;
    earliest_input.committed_samples[2].mass_state.mass_kilograms =
        99.8;
    const auto earliest_outcome =
        CommittedMissionResultKernel::evaluate(
            earliest_definition, earliest_input);
    const auto& earliest = require_value(
        earliest_outcome,
        "earliest committed terminal boundary evaluation failed");
    require(earliest.status == MissionResultStatus::Completed &&
                earliest.initial_tick == 0 && earliest.final_tick == 1 &&
                earliest.metrics.evaluated_sample_count == 2U &&
                near(earliest.metrics.terminal.duration_seconds, 0.1) &&
                near(earliest.metrics.terminal.downrange_meters, 25.0) &&
                earliest.termination.action == MissionAction::Complete &&
                earliest.termination.reason_code == "downrange-goal" &&
                earliest.termination.priority == 200 &&
                near(earliest.termination.trigger_time_seconds, 0.1) &&
                earliest.terminal_boundary.rigid_context.sample_time.tick ==
                    1 &&
                earliest.terminal_predicates[0].met &&
                earliest_input.committed_samples[2]
                        .rigid_state.position.value(0) -
                        opening_downrange <
                    earliest_definition.predicates[0].threshold &&
                earliest_input.committed_samples[2]
                        .mass_state.mass_kilograms <=
                    earliest_definition.predicates[2].threshold,
            "a later boundary replaced the earliest committed termination");
    checks.emplace_back("mission-result-earliest-committed-boundary");

    CommittedMissionResultDefinition no_terminal = definition;
    no_terminal.predicates[0].threshold = 1000.0;
    no_terminal.predicates[1].threshold = 10.0;
    no_terminal.predicates[2].threshold = 0.0;
    expect_failure(CommittedMissionResultKernel::evaluate(
                       no_terminal, input),
                   NumericalStatus::DomainError,
                   "mission without terminal predicate survived");
    CommittedMissionResultInput noncontiguous = input;
    noncontiguous.committed_samples[1].rigid_context.sample_time =
        SimulationInstant{3, 0.3};
    noncontiguous.committed_samples[1].mass_state.context.sample_time =
        SimulationInstant{3, 0.3};
    expect_failure(CommittedMissionResultKernel::evaluate(
                       definition, noncontiguous),
                   NumericalStatus::DomainError,
                   "noncontiguous committed sample survived");
    CommittedMissionResultDefinition duplicate = definition;
    duplicate.predicates[1].predicate_id =
        duplicate.predicates[0].predicate_id;
    expect_failure(CommittedMissionResultKernel::evaluate(
                       duplicate, input),
                   NumericalStatus::DomainError,
                   "duplicate mission predicate survived");
    checks.emplace_back("mission-result-three-invalid-input-rejections");
    return {accepted, std::move(checks)};
}

struct ProbeBundle {
    TwoIntervalMassCommitOutput accepted;
    std::vector<std::string> direct_checks;
    PropulsionProbeBundle propulsion;
    MissionControlProbeBundle mission_control;
    MissionTwoIntervalProbeBundle mission_two_interval;
    MissionResultProbeBundle mission_result;
};

ProbeBundle run_probe() {
    const auto prepared_outcome =
        prepare_rigid_step_model(fixture_rigid_definition());
    const PreparedRigidStepModel& prepared = require_value(
        prepared_outcome, "two-interval rigid model preparation failed");
    const ScalarBurnMassDefinition mass_definition =
        fixture_mass_definition();
    const TwoIntervalMassCommitInput accepted_input = fixture_input();
    const auto accepted_outcome = TwoIntervalMassCommitKernel::evaluate(
        prepared, mass_definition, accepted_input);
    const auto& accepted = require_value(
        accepted_outcome, "two-interval product evaluation failed");
    const auto& first = accepted.intervals[0];
    const auto& second = accepted.intervals[1];

    require(near(first.staged.projected_committed_mass.mass_kilograms,
                 120.0) &&
                near(first.staged.mass_evolution
                         .integration_mass_kilograms,
                     120.0) &&
                near(first.staged.mass_evolution
                         .candidate.state.mass_kilograms,
                     119.95) &&
                near(first.staged.rigid_step.telemetry
                         .derivative_at_interval_start
                         .acceleration.value,
                     Vec3{2.0, 0.0, 0.0}) &&
                near(first.staged.rigid_step.output.candidate.state.position
                         .value,
                     Vec3{1.01, 0.0, 0.0}) &&
                near(first.staged.rigid_step.output.candidate.state.velocity
                         .value,
                     Vec3{10.2, 0.0, 0.0}),
            "interval zero did not use committed mass");
    std::vector<std::string> checks{
        "interval-zero-committed-mass-and-hidden-candidate"};

    require(first.closing_commit.rigid_context.sample_time.tick == 1 &&
                first.closing_commit.mass_state.context.sample_time.tick == 1 &&
                near(first.closing_commit.mass_state.mass_kilograms,
                     119.95) &&
                near(first.closing_commit.rigid_state.position.value,
                     Vec3{1.01, 0.0, 0.0}),
            "first atomic boundary differs");
    checks.emplace_back("first-atomic-boundary");

    require(second.staged.opening_boundary.rigid_context.sample_time.tick ==
                first.closing_commit.rigid_context.sample_time.tick &&
                second.staged.opening_boundary.mass_state.context.sample_time
                        .tick ==
                    first.closing_commit.mass_state.context.sample_time.tick &&
                near(second.staged.opening_boundary.rigid_state.position.value,
                     first.closing_commit.rigid_state.position.value) &&
                near(second.staged.opening_boundary.rigid_state.velocity.value,
                     first.closing_commit.rigid_state.velocity.value) &&
                near(second.staged.projected_committed_mass.mass_kilograms,
                     first.closing_commit.mass_state.mass_kilograms) &&
                near(second.staged.rigid_step.telemetry
                         .derivative_at_interval_start
                         .acceleration.value,
                     Vec3{240.0 / 119.95, 0.0, 0.0}),
            "interval one did not consume the complete committed pair");
    checks.emplace_back("next-interval-consumes-committed-pair");

    require(accepted.terminal_boundary.rigid_context.sample_time.tick == 2 &&
                accepted.terminal_boundary.mass_state.context.sample_time.tick ==
                    2 &&
                near(accepted.terminal_boundary.mass_state.mass_kilograms,
                     119.9) &&
                near(accepted.terminal_boundary.rigid_state.position.value,
                     Vec3{2.0400041684035015, 0.0, 0.0}) &&
                near(accepted.terminal_boundary.rigid_state.velocity.value,
                     Vec3{10.40008336807003, 0.0, 0.0}),
            "terminal committed boundary differs from oracle anchors");
    checks.emplace_back("accepted-terminal-oracle-anchors");

    require(near(accepted.terminal_boundary.mass_state
                     .body_origin_to_center_of_mass.value,
                 Vec3{0.2, -0.1, 0.05}) &&
                near(accepted.terminal_boundary.mass_state
                         .inertia_about_center_of_mass.value(0, 1),
                     1.0) &&
                near(accepted.terminal_boundary.mass_state
                         .inertia_about_center_of_mass.value(1, 2),
                     2.0),
            "constant-geometry mass state changed geometry");
    checks.emplace_back("constant-geometry-preserved");

    MassFlowIntervalInput zero_flow = accepted_input.intervals[0].mass_flow;
    zero_flow.fuel_consumption_rate_kilograms_per_second = 0.0;
    const auto zero_flow_outcome = ScalarBurnMassKernel::evaluate(
        mass_definition, accepted_input.opening_boundary.mass_state,
        zero_flow, fixture_numerical_policy());
    const auto& zero_flow_output = require_value(
        zero_flow_outcome, "zero-flow mass evolution failed");
    require(near(zero_flow_output.candidate.state.mass_kilograms, 120.0) &&
                near(zero_flow_output.consumed_mass_kilograms, 0.0),
            "zero-flow mass candidate changed mass");
    checks.emplace_back("zero-flow-mass-candidate");

    MassFlowIntervalInput negative_flow =
        accepted_input.intervals[0].mass_flow;
    negative_flow.fuel_consumption_rate_kilograms_per_second = -0.5;
    expect_failure(ScalarBurnMassKernel::evaluate(
                       mass_definition,
                       accepted_input.opening_boundary.mass_state,
                       negative_flow, fixture_numerical_policy()),
                   NumericalStatus::DomainError,
                   "negative fuel-consumption rate survived");
    checks.emplace_back("negative-flow-rejection");

    MassState indefinite_mass =
        accepted_input.opening_boundary.mass_state;
    indefinite_mass.inertia_about_center_of_mass.value(2, 2) = -1.0;
    expect_failure(ScalarBurnMassKernel::evaluate(
                       mass_definition, indefinite_mass,
                       accepted_input.intervals[0].mass_flow,
                       fixture_numerical_policy()),
                   NumericalStatus::DomainError,
                   "indefinite mass inertia survived");
    checks.emplace_back("mass-invariant-rejection");

    TwoIntervalMassCommitInput depleted = accepted_input;
    depleted.intervals[0].mass_flow
        .fuel_consumption_rate_kilograms_per_second = 1200.0;
    expect_failure(TwoIntervalMassCommitKernel::evaluate(
                       prepared, mass_definition, depleted),
                   NumericalStatus::DomainError,
                   "mass failure exposed a partial rigid boundary");
    checks.emplace_back("atomic-discard-on-mass-failure");

    TwoIntervalMassCommitInput rigid_failure = accepted_input;
    rigid_failure.intervals[1].environment
        .speed_of_sound_meters_per_second = 1.0;
    expect_failure(TwoIntervalMassCommitKernel::evaluate(
                       prepared, mass_definition, rigid_failure),
                   NumericalStatus::OutOfRange,
                   "rigid failure exposed a partial mass boundary");
    checks.emplace_back("atomic-discard-on-rigid-failure");

    TwoIntervalMassCommitInput gap = accepted_input;
    gap.intervals[1] = interval_input(2);
    expect_failure(TwoIntervalMassCommitKernel::evaluate(
                       prepared, mass_definition, gap),
                   NumericalStatus::DomainError,
                   "non-contiguous interval survived");
    checks.emplace_back("contiguous-boundary-rejection");

    PropulsionProbeBundle propulsion = run_propulsion_probe(
        prepared, mass_definition, accepted_input.opening_boundary);
    MissionControlProbeBundle mission_control =
        run_mission_control_probe();
    MissionTwoIntervalProbeBundle mission_two_interval =
        run_mission_two_interval_probe();
    MissionResultProbeBundle mission_result = run_mission_result_probe(
        mission_two_interval.accepted);
    return {accepted, std::move(checks), std::move(propulsion),
            std::move(mission_control),
            std::move(mission_two_interval),
            std::move(mission_result)};
}

void write_number(double value) {
    std::cout << (value == 0.0 ? 0.0 : value);
}

void write_vec3(const Vec3& value) {
    std::cout << '[';
    for (Eigen::Index index = 0; index < 3; ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        write_number(value(index));
    }
    std::cout << ']';
}

void write_quaternion(const gnc::foundation::QuaternionStorage& value) {
    const auto coefficients = gnc::foundation::quaternion_to_wxyz(value);
    std::cout << '[';
    for (std::size_t index = 0U; index < coefficients.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        write_number(coefficients[index]);
    }
    std::cout << ']';
}

void write_matrix(const Mat3& value) {
    std::cout << '[';
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (row != 0 || column != 0) {
                std::cout << ',';
            }
            write_number(value(row, column));
        }
    }
    std::cout << ']';
}

void write_rigid_state(const RigidState& state) {
    std::cout << "{\"position_I_m\":";
    write_vec3(state.position.value);
    std::cout << ",\"velocity_I_mps\":";
    write_vec3(state.velocity.value);
    std::cout << ",\"q_I_B_wxyz\":";
    write_quaternion(state.attitude.value);
    std::cout << ",\"omega_BI_B_radps\":";
    write_vec3(state.angular_rate.value);
    std::cout << '}';
}

void write_interval(const TwoIntervalMassCommitIntervalOutput& interval) {
    const auto& staged = interval.staged;
    const auto& mass = staged.mass_evolution;
    std::cout << "{\"sample_tick\":"
              << staged.opening_boundary.rigid_context.sample_time.tick
              << ",\"valid_from_tick\":"
              << staged.opening_boundary.rigid_context.sample_time.tick
              << ",\"valid_until_tick\":"
              << staged.candidate.effective_at.tick
              << ",\"current_committed_mass_kg\":";
    write_number(mass.current_committed_mass_kilograms);
    std::cout << ",\"integration_mass_kg\":";
    write_number(mass.integration_mass_kilograms);
    std::cout << ",\"consumed_mass_kg\":";
    write_number(mass.consumed_mass_kilograms);
    std::cout << ",\"pending_mass_candidate_kg\":";
    write_number(mass.candidate.state.mass_kilograms);
    std::cout << ",\"pending_visibility_before_commit\":\"candidate-only\""
              << ",\"acceleration_I_mps2\":";
    write_vec3(staged.rigid_step.telemetry.derivative_at_interval_start
                   .acceleration.value);
    std::cout << ",\"initial_rigid_state\":";
    write_rigid_state(staged.opening_boundary.rigid_state);
    std::cout << ",\"rigid_candidate\":";
    write_rigid_state(staged.candidate.rigid.state);
    std::cout << ",\"closing_commit\":{\"tick\":"
              << interval.closing_commit.rigid_context.sample_time.tick
              << ",\"kind\":\"atomic-rigid-and-mass\",\"mass_kg\":";
    write_number(interval.closing_commit.mass_state.mass_kilograms);
    std::cout << ",\"r_body_origin_to_CoM_B_m\":";
    write_vec3(interval.closing_commit.mass_state
                   .body_origin_to_center_of_mass.value);
    std::cout << ",\"inertia_about_CoM_B_kgm2_row_major\":";
    write_matrix(interval.closing_commit.mass_state
                     .inertia_about_center_of_mass.value);
    std::cout << ",\"rigid_state\":";
    write_rigid_state(interval.closing_commit.rigid_state);
    std::cout << "}}";
}

void write_propulsion_case(const PropulsionCaseResult& result) {
    const auto& response = result.response;
    const auto& wrench = response.supplied_body_wrench;
    const auto& context = wrench.context;
    const auto& mass = result.mass;
    const double duration_seconds =
        context.validity.effective_until.seconds -
        context.validity.effective_from.seconds;
    std::cout << "{\"id\":\"" << result.id
              << "\",\"response\":{\"model_id\":\""
              << kSuppliedPropulsionModelIdentity
              << "\",\"source_id\":\"" << wrench.source_id
              << "\",\"quality\":\"Valid\",\"body_frame_id\":\""
              << context.sample.frame.id
              << "\",\"sample_tick\":"
              << context.sample.sample_time.tick
              << ",\"clock_domain\":\""
              << context.sample.clock_domain.id
              << "\",\"configuration_revision\":"
              << context.sample.configuration_revision
              << ",\"valid_from_tick\":"
              << context.validity.effective_from.tick
              << ",\"valid_until_tick\":"
              << context.validity.effective_until.tick
              << ",\"force_B_N\":";
    write_vec3(wrench.force.value);
    std::cout << ",\"r_CoM_to_application_B_m\":";
    write_vec3(wrench.center_of_mass_to_application.value);
    std::cout << ",\"moment_at_application_B_Nm\":";
    write_vec3(wrench.intrinsic_moment_at_application.value);
    std::cout << ",\"fuel_consumption_rate_kgps\":";
    write_number(
        response.mass_flow
            .fuel_consumption_rate_kilograms_per_second);
    std::cout << "},\"closure_consumer\":{\"source_id\":\""
              << wrench.source_id << "\",\"body_frame_id\":\""
              << context.sample.frame.id
              << "\",\"sample_tick\":"
              << context.sample.sample_time.tick
              << ",\"clock_domain\":\""
              << context.sample.clock_domain.id
              << "\",\"configuration_revision\":"
              << context.sample.configuration_revision
              << ",\"force_B_N\":";
    write_vec3(wrench.force.value);
    std::cout << ",\"moment_at_application_B_Nm\":";
    write_vec3(wrench.intrinsic_moment_at_application.value);
    std::cout << ",\"lever_arm_moment_B_Nm\":";
    write_vec3(response.lever_arm_moment.value);
    std::cout << ",\"moment_about_CoM_B_Nm\":";
    write_vec3(response.moment_about_center_of_mass.value);
    std::cout << "},\"mass_consumer\":{\"mass_state_id\":\""
              << response.mass_flow.mass_state_id
              << "\",\"clock_domain\":\""
              << context.sample.clock_domain.id
              << "\",\"configuration_revision\":"
              << context.sample.configuration_revision
              << ",\"valid_from_tick\":"
              << context.validity.effective_from.tick
              << ",\"valid_until_tick\":"
              << context.validity.effective_until.tick
              << ",\"interval_duration_s\":";
    write_number(duration_seconds);
    std::cout << ",\"fuel_consumption_rate_kgps\":";
    write_number(
        response.mass_flow
            .fuel_consumption_rate_kilograms_per_second);
    std::cout << ",\"consumed_fuel_mass_kg\":";
    write_number(mass.consumed_mass_kilograms);
    std::cout << ",\"mass_delta_kg\":";
    write_number(mass.candidate.state.mass_kilograms -
                 result.committed_mass_kilograms);
    std::cout << ",\"committed_mass_kg\":";
    write_number(result.committed_mass_kilograms);
    std::cout << ",\"mass_candidate_kg\":";
    write_number(mass.candidate.state.mass_kilograms);
    std::cout << "}}";
}

void write_propulsion(const PropulsionProbeBundle& propulsion) {
    std::cout << "{\"product_model_id\":\""
              << kSuppliedPropulsionModelIdentity
              << "\",\"contract_id\":\""
              << kSuppliedPropulsionContractIdentity
              << "\",\"source_fixture_id\":\""
              << kPropulsionFixtureId
              << "\",\"source_oracle_id\":\""
              << kPropulsionOracleId
              << "\",\"reference_model_id\":\""
              << kPropulsionReferenceModelId
              << "\",\"status\":\"passed\",\"cases\":[";
    for (std::size_t index = 0U; index < propulsion.cases.size();
         ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        write_propulsion_case(propulsion.cases[index]);
    }
    const auto& equivalence = propulsion.equivalence;
    std::cout << "],\"equivalence_results\":[{\"id\":\"EQUIV-YYZ-PROPULSION-MASS-INTERVAL-PARTITION\",\"status\":\"passed\",\"force_and_response_max_abs_difference\":";
    write_number(equivalence.force_and_response_max_abs_difference);
    std::cout << ",\"application_wrench_max_abs_difference\":";
    write_number(equivalence.application_wrench_max_abs_difference);
    std::cout << ",\"summed_consumed_fuel_mass_kg\":";
    write_number(equivalence.summed_consumed_fuel_mass_kilograms);
    std::cout << ",\"consumed_fuel_mass_difference_kg\":";
    write_number(
        equivalence.consumed_fuel_mass_difference_kilograms);
    std::cout << ",\"sequential_final_mass_candidate_kg\":";
    write_number(
        equivalence.sequential_final_mass_candidate_kilograms);
    std::cout << ",\"final_mass_candidate_difference_kg\":";
    write_number(
        equivalence.final_mass_candidate_difference_kilograms);
    std::cout << "}],\"invalid_input_rejections\":[";
    for (std::size_t index = 0U;
         index < propulsion.invalid_input_rejections.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << propulsion.invalid_input_rejections[index]
                  << '\"';
    }
    const auto& consumer = propulsion.consumer;
    std::cout << "],\"atomic_boundary_consumer\":{\"force_B_N\":";
    write_vec3(supplied_contribution(
                   consumer.atomic_boundary.rigid_step.telemetry)
                   .force.value);
    std::cout << ",\"moment_about_CoM_B_Nm\":";
    write_vec3(supplied_contribution(
                   consumer.atomic_boundary.rigid_step.telemetry)
                   .moment_about_center_of_mass.value);
    std::cout << ",\"consumed_fuel_mass_kg\":";
    write_number(consumer.atomic_boundary.mass_evolution
                     .consumed_mass_kilograms);
    std::cout << ",\"mass_candidate_kg\":";
    write_number(consumer.atomic_boundary.mass_evolution
                     .candidate.state.mass_kilograms);
    std::cout << ",\"candidate_tick\":"
              << consumer.atomic_boundary.candidate.effective_at.tick
              << "},\"direct_checks\":[";
    for (std::size_t index = 0U;
         index < propulsion.direct_checks.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << propulsion.direct_checks[index] << '\"';
    }
    std::cout << "]}";
}

void write_mission_control(
    const MissionControlProbeBundle& mission_control) {
    const auto& value = mission_control.accepted;
    const auto& guidance = value.guidance;
    const auto& controller = value.controller;
    const auto& boundary = value.atomic_boundary;
    std::cout << "{\"product_model_id\":\""
              << kControlledPropelledRigidMassStepModelIdentity
              << "\",\"source_fixture_id\":\"" << kMissionFixtureId
              << "\",\"source_oracle_id\":\"" << kMissionOracleId
              << "\",\"reference_model_id\":\""
              << kMissionReferenceModelId
              << "\",\"status\":\"passed\",\"observation_tick\":"
              << value.observation.context.sample_time.tick
              << ",\"guidance\":{\"altitude_error_m\":";
    write_number(guidance.altitude_error_meters);
    std::cout << ",\"altitude_feedback_rad\":";
    write_number(guidance.altitude_feedback_radians);
    std::cout << ",\"vertical_speed_feedback_rad\":";
    write_number(guidance.vertical_speed_feedback_radians);
    std::cout << ",\"raw_pitch_command_rad\":";
    write_number(guidance.raw_pitch_command_radians);
    std::cout << ",\"pitch_command_rad\":";
    write_number(guidance.pitch_command_radians);
    std::cout << ",\"saturated\":"
              << (guidance.saturated ? "true" : "false")
              << "},\"controller\":{\"pitch_error_rad\":";
    write_number(controller.pitch_error_radians);
    std::cout << ",\"proportional_moment_Nm\":";
    write_number(controller.proportional_moment_newton_meters);
    std::cout << ",\"rate_damping_moment_Nm\":";
    write_number(controller.rate_damping_moment_newton_meters);
    std::cout << ",\"raw_moment_command_Nm\":";
    write_number(controller.raw_moment_command_newton_meters);
    std::cout << ",\"moment_command_Nm\":";
    write_number(controller.moment_command_newton_meters);
    std::cout << ",\"saturated\":"
              << (controller.saturated ? "true" : "false")
              << "},\"actuator\":{\"valid_from_tick\":"
              << value.actuator.context.validity.effective_from.tick
              << ",\"valid_until_tick\":"
              << value.actuator.context.validity.effective_until.tick
              << ",\"moment_about_CoM_B_Nm\":";
    write_vec3(value.actuator.moment_about_center_of_mass.value);
    std::cout << "},\"atomic_boundary\":{\"supplied_force_B_N\":";
    write_vec3(supplied_contribution(boundary.rigid_step.telemetry)
                   .force.value);
    std::cout << ",\"supplied_moment_about_CoM_B_Nm\":";
    write_vec3(supplied_contribution(boundary.rigid_step.telemetry)
                   .moment_about_center_of_mass.value);
    std::cout << ",\"total_moment_about_CoM_B_Nm\":";
    write_vec3(closure_output(boundary.rigid_step.telemetry)
                   .moment_total_about_center_of_mass.value);
    std::cout << ",\"candidate_tick\":"
              << boundary.candidate.effective_at.tick
              << ",\"candidate_mass_kg\":";
    write_number(boundary.mass_evolution.candidate.state.mass_kilograms);
    std::cout << ",\"candidate_rigid_state\":";
    write_rigid_state(boundary.rigid_step.output.candidate.state);
    std::cout << "},\"direct_checks\":[";
    for (std::size_t index = 0U;
         index < mission_control.direct_checks.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << mission_control.direct_checks[index] << '\"';
    }
    std::cout << "]}";
}

void write_mission_two_interval_entry(
    const TwoIntervalControlledPropelledCommitIntervalOutput& interval) {
    const auto& staged = interval.staged;
    const auto& boundary = staged.atomic_boundary;
    std::cout << "{\"observation_tick\":"
              << staged.observation.context.sample_time.tick
              << ",\"pitch_command_rad\":";
    write_number(staged.guidance.pitch_command_radians);
    std::cout << ",\"moment_command_Nm\":";
    write_number(staged.controller.moment_command_newton_meters);
    std::cout << ",\"realized_moment_B_Nm\":";
    write_vec3(staged.actuator.moment_about_center_of_mass.value);
    std::cout << ",\"integration_mass_kg\":";
    write_number(boundary.projected_committed_mass.mass_kilograms);
    std::cout << ",\"consumed_mass_kg\":";
    write_number(boundary.mass_evolution.consumed_mass_kilograms);
    std::cout << ",\"supplied_moment_about_CoM_B_Nm\":";
    write_vec3(supplied_contribution(boundary.rigid_step.telemetry)
                   .moment_about_center_of_mass.value);
    std::cout << ",\"total_moment_about_CoM_B_Nm\":";
    write_vec3(closure_output(boundary.rigid_step.telemetry)
                   .moment_total_about_center_of_mass.value);
    std::cout << ",\"closing_tick\":"
              << interval.closing_commit.rigid_context.sample_time.tick
              << ",\"closing_mass_kg\":";
    write_number(interval.closing_commit.mass_state.mass_kilograms);
    std::cout << ",\"closing_rigid_state\":";
    write_rigid_state(interval.closing_commit.rigid_state);
    std::cout << '}';
}

void write_mission_two_interval(
    const MissionTwoIntervalProbeBundle& mission) {
    std::cout << "{\"product_model_id\":\""
              << kTwoIntervalControlledPropelledCommitModelIdentity
              << "\",\"source_fixture_id\":\"" << kMissionFixtureId
              << "\",\"source_oracle_id\":\"" << kMissionOracleId
              << "\",\"reference_model_id\":\""
              << kMissionReferenceModelId
              << "\",\"status\":\"passed\",\"intervals\":[";
    write_mission_two_interval_entry(mission.accepted.intervals[0]);
    std::cout << ',';
    write_mission_two_interval_entry(mission.accepted.intervals[1]);
    std::cout << "],\"terminal_tick\":"
              << mission.accepted.terminal_boundary.rigid_context
                     .sample_time.tick
              << ",\"direct_checks\":[";
    for (std::size_t index = 0U;
         index < mission.direct_checks.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << mission.direct_checks[index] << '\"';
    }
    std::cout << "]}";
}

[[nodiscard]] std::string_view mission_action_text(MissionAction action) {
    return action == MissionAction::Complete ? "Complete" : "Abort";
}

[[nodiscard]] std::string_view mission_status_text(
    MissionResultStatus status) {
    return status == MissionResultStatus::Completed ? "Completed" :
                                                      "Aborted";
}

void write_mission_result(const MissionResultProbeBundle& mission) {
    const auto& value = mission.accepted;
    const auto& summary = value.metrics;
    const auto& terminal = summary.terminal;
    std::cout << "{\"product_model_id\":\""
              << kCommittedMissionResultModelIdentity
              << "\",\"source_fixture_id\":\"" << kMissionFixtureId
              << "\",\"source_oracle_id\":\"" << kMissionOracleId
              << "\",\"reference_model_id\":\""
              << kMissionReferenceModelId << "\",\"status\":\""
              << mission_status_text(value.status)
              << "\",\"initial_tick\":" << value.initial_tick
              << ",\"final_tick\":" << value.final_tick
              << ",\"final_time_s\":";
    write_number(value.final_time_seconds);
    std::cout << ",\"termination\":{\"action\":\""
              << mission_action_text(value.termination.action)
              << "\",\"reason_code\":\""
              << value.termination.reason_code
              << "\",\"trigger_time_s\":";
    write_number(value.termination.trigger_time_seconds);
    std::cout << ",\"priority\":" << value.termination.priority
              << "},\"metrics\":{\"evaluated_sample_count\":"
              << summary.evaluated_sample_count
              << ",\"duration_s\":";
    write_number(terminal.duration_seconds);
    std::cout << ",\"downrange_m\":";
    write_number(terminal.downrange_meters);
    std::cout << ",\"vertical_displacement_m\":";
    write_number(terminal.vertical_displacement_meters);
    std::cout << ",\"remaining_mass_kg\":";
    write_number(terminal.remaining_mass_kilograms);
    std::cout << ",\"consumed_mass_kg\":";
    write_number(terminal.consumed_mass_kilograms);
    std::cout << ",\"terminal_speed_mps\":";
    write_number(terminal.speed_meters_per_second);
    std::cout << ",\"peak_speed_mps\":";
    write_number(summary.peak_speed_meters_per_second);
    std::cout << ",\"peak_speed_tick\":" << summary.peak_speed_tick
              << ",\"maximum_downrange_m\":";
    write_number(summary.maximum_downrange_meters);
    std::cout << ",\"maximum_downrange_tick\":"
              << summary.maximum_downrange_tick
              << ",\"minimum_remaining_mass_kg\":";
    write_number(summary.minimum_remaining_mass_kilograms);
    std::cout << ",\"minimum_remaining_mass_tick\":"
              << summary.minimum_remaining_mass_tick
              << "},\"terminal_sample\":{\"tick\":"
              << value.terminal_boundary.rigid_context.sample_time.tick
              << ",\"committed_mass_kg\":";
    write_number(value.terminal_boundary.mass_state.mass_kilograms);
    std::cout << "},\"terminal_predicates\":[";
    for (std::size_t index = 0U;
         index < value.terminal_predicates.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        const auto& predicate = value.terminal_predicates[index];
        std::cout << "{\"predicate_id\":\"" << predicate.predicate_id
                  << "\",\"observed\":";
        write_number(predicate.observed);
        std::cout << ",\"met\":" << (predicate.met ? "true" : "false")
                  << '}';
    }
    std::cout << "],\"direct_checks\":[";
    for (std::size_t index = 0U;
         index < mission.direct_checks.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << mission.direct_checks[index] << '\"';
    }
    std::cout << "]}";
}

void write_json(const ProbeBundle& bundle) {
    const auto& terminal = bundle.accepted.terminal_boundary;
    std::cout << std::setprecision(17)
              << "{\"schema_version\":\"gnczmkn.yyz-two-interval-mass-commit-product-probe/5\""
              << ",\"product_model_id\":\""
              << kTwoIntervalMassCommitModelIdentity
              << "\",\"mass_model_id\":\""
              << kScalarBurnMassModelIdentity
              << "\",\"contract_id\":\""
              << kTwoIntervalMassCommitContractIdentity
              << "\",\"source_fixture_id\":\"" << kFixtureId
              << "\",\"source_oracle_id\":\"" << kOracleId
              << "\",\"reference_model_id\":\"" << kReferenceModelId
              << "\",\"status\":\"passed\",\"accepted\":{\"intervals\":[";
    write_interval(bundle.accepted.intervals[0]);
    std::cout << ',';
    write_interval(bundle.accepted.intervals[1]);
    std::cout << "],\"terminal\":{\"tick\":"
              << terminal.rigid_context.sample_time.tick
              << ",\"time_s\":";
    write_number(terminal.rigid_context.sample_time.seconds);
    std::cout << ",\"termination_kind\":\"duration_exact_grid\""
              << ",\"committed_mass_kg\":";
    write_number(terminal.mass_state.mass_kilograms);
    std::cout << ",\"r_body_origin_to_CoM_B_m\":";
    write_vec3(terminal.mass_state.body_origin_to_center_of_mass.value);
    std::cout << ",\"inertia_about_CoM_B_kgm2_row_major\":";
    write_matrix(terminal.mass_state.inertia_about_center_of_mass.value);
    std::cout << ",\"rigid_state\":";
    write_rigid_state(terminal.rigid_state);
    std::cout << "}},\"direct_checks\":[";
    for (std::size_t index = 0U; index < bundle.direct_checks.size();
         ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '\"' << bundle.direct_checks[index] << '\"';
    }
    std::cout << "],\"propulsion\":";
    write_propulsion(bundle.propulsion);
    std::cout << ",\"mission_control\":";
    write_mission_control(bundle.mission_control);
    std::cout << ",\"mission_two_interval\":";
    write_mission_two_interval(bundle.mission_two_interval);
    std::cout << ",\"mission_result\":";
    write_mission_result(bundle.mission_result);
    std::cout << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--self-check") {
        std::cerr <<
            "usage: gnc_yyz_two_interval_mass_commit_product_probe --self-check\n";
        return 2;
    }
    try {
        write_json(run_probe());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
