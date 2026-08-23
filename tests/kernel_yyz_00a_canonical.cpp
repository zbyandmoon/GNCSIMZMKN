#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <yyz/mass_commit.hpp>
#include <yyz/qualification_00a.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace ref_yyz = gnc::tests::ref_yyz;
namespace yyz = gnc::packages::yyz;

using Image = contracts::ExecutionPlanImage;
using CommittedHistorySampleProbe =
    ref_yyz::CommittedHistorySampleProbe;
using CommittedRigidMassProbe = ref_yyz::CommittedRigidMassProbe;
using MissionAccumulatorProbe = ref_yyz::MissionAccumulatorProbe;
using MissionResultProbe = ref_yyz::MissionResultProbe;

constexpr std::string_view kIntervalOneFingerprint =
    "7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14";
constexpr std::string_view kPriorCanonicalFingerprint =
    "e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c";
constexpr std::string_view kCanonicalFingerprint =
    "65e00515234efcabfae14399e9fe6838d65248af2d9a598f50b09a3470131078";
constexpr std::int64_t kTerminalTick = 3000;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Outcome>
[[nodiscard]] std::string diagnostics(const Outcome& outcome) {
    std::string text;
    for (const auto& diagnostic : outcome.diagnostics) {
        if (!text.empty()) text += "; ";
        text += std::string(compiler::to_string(diagnostic.code));
        text += " ";
        text += diagnostic.subject;
        text += ": ";
        text += diagnostic.detail;
    }
    return text;
}

[[nodiscard]] bool near(double lhs, double rhs,
                        double tolerance = 2.0e-12) noexcept {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool exact_double(double lhs, double rhs) noexcept {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t lhs_bits = 0U;
    std::uint64_t rhs_bits = 0U;
    std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
    return lhs_bits == rhs_bits;
}

template <std::size_t Size>
[[nodiscard]] bool exact_array(const std::array<double, Size>& lhs,
                               const std::array<double, Size>& rhs) noexcept {
    for (std::size_t index = 0U; index < Size; ++index) {
        if (!exact_double(lhs[index], rhs[index])) return false;
    }
    return true;
}

[[nodiscard]] bool exactly_same(const CommittedRigidMassProbe& lhs,
                                const CommittedRigidMassProbe& rhs) noexcept {
    return exact_array(lhs.position, rhs.position) &&
           exact_array(lhs.velocity, rhs.velocity) &&
           exact_array(lhs.attitude_wxyz, rhs.attitude_wxyz) &&
           exact_array(lhs.angular_rate, rhs.angular_rate) &&
           exact_double(lhs.mass_kilograms, rhs.mass_kilograms) &&
           exact_array(lhs.center_of_mass, rhs.center_of_mass) &&
           exact_array(lhs.inertia, rhs.inertia) &&
           lhs.mass_sample_tick == rhs.mass_sample_tick;
}

[[nodiscard]] bool exactly_same(
    const std::vector<CommittedHistorySampleProbe>& lhs,
    const std::vector<CommittedHistorySampleProbe>& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (lhs[index].tick != rhs[index].tick ||
            lhs[index].committed_epoch != rhs[index].committed_epoch ||
            !exactly_same(lhs[index].state, rhs[index].state)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exactly_same(const MissionResultProbe& lhs,
                                const MissionResultProbe& rhs) noexcept {
    return lhs.present == rhs.present && lhs.completed == rhs.completed &&
           lhs.initial_tick == rhs.initial_tick &&
           lhs.final_tick == rhs.final_tick &&
           exact_double(lhs.final_time_seconds, rhs.final_time_seconds) &&
           lhs.reason_code == rhs.reason_code &&
           lhs.priority == rhs.priority &&
           lhs.evaluated_sample_count == rhs.evaluated_sample_count &&
           exact_double(lhs.duration_seconds, rhs.duration_seconds) &&
           exact_double(lhs.downrange_meters, rhs.downrange_meters) &&
           exact_double(lhs.vertical_displacement_meters,
                        rhs.vertical_displacement_meters) &&
           exact_double(lhs.remaining_mass_kilograms,
                        rhs.remaining_mass_kilograms) &&
           exact_double(lhs.consumed_mass_kilograms,
                        rhs.consumed_mass_kilograms) &&
           exact_double(lhs.terminal_speed_meters_per_second,
                        rhs.terminal_speed_meters_per_second) &&
           exact_double(lhs.peak_speed_meters_per_second,
                        rhs.peak_speed_meters_per_second) &&
           lhs.peak_speed_tick == rhs.peak_speed_tick &&
           exact_double(lhs.maximum_downrange_meters,
                        rhs.maximum_downrange_meters) &&
           lhs.maximum_downrange_tick == rhs.maximum_downrange_tick &&
           exact_double(lhs.minimum_remaining_mass_kilograms,
                        rhs.minimum_remaining_mass_kilograms) &&
           lhs.minimum_remaining_mass_tick ==
               rhs.minimum_remaining_mass_tick &&
           lhs.terminal_tick == rhs.terminal_tick;
}

[[nodiscard]] bool exactly_same(
    const MissionAccumulatorProbe& lhs,
    const MissionAccumulatorProbe& rhs) noexcept {
    return lhs.present == rhs.present &&
           lhs.initialized == rhs.initialized &&
           lhs.opening_tick == rhs.opening_tick &&
           lhs.latest_tick == rhs.latest_tick &&
           lhs.evaluated_sample_count == rhs.evaluated_sample_count &&
           exact_double(lhs.duration_seconds, rhs.duration_seconds) &&
           exact_double(lhs.downrange_meters, rhs.downrange_meters) &&
           exact_double(lhs.vertical_displacement_meters,
                        rhs.vertical_displacement_meters) &&
           exact_double(lhs.remaining_mass_kilograms,
                        rhs.remaining_mass_kilograms) &&
           exact_double(lhs.consumed_mass_kilograms,
                        rhs.consumed_mass_kilograms) &&
           exact_double(lhs.terminal_speed_meters_per_second,
                        rhs.terminal_speed_meters_per_second) &&
           exact_double(lhs.peak_speed_meters_per_second,
                        rhs.peak_speed_meters_per_second) &&
           lhs.peak_speed_tick == rhs.peak_speed_tick &&
           exact_double(lhs.maximum_downrange_meters,
                        rhs.maximum_downrange_meters) &&
           lhs.maximum_downrange_tick == rhs.maximum_downrange_tick &&
           exact_double(lhs.minimum_remaining_mass_kilograms,
                        rhs.minimum_remaining_mass_kilograms) &&
           lhs.minimum_remaining_mass_tick ==
               rhs.minimum_remaining_mass_tick &&
           lhs.terminal_result_present == rhs.terminal_result_present;
}

[[nodiscard]] bool exactly_same(const kernel::RuntimeDiagnostic& lhs,
                                const kernel::RuntimeDiagnostic& rhs) noexcept {
    return lhs.code == rhs.code && lhs.stage == rhs.stage &&
           lhs.operation == rhs.operation &&
           lhs.source_kind == rhs.source_kind &&
           lhs.source_handle == rhs.source_handle &&
           lhs.source_field == rhs.source_field &&
           lhs.subject_kind == rhs.subject_kind &&
           lhs.subject_reference_kind == rhs.subject_reference_kind &&
           lhs.subject_handle == rhs.subject_handle &&
           lhs.run_id == rhs.run_id &&
           lhs.run_context_present == rhs.run_context_present &&
           lhs.tick == rhs.tick && lhs.base_epoch == rhs.base_epoch &&
           lhs.simulation_context_present ==
               rhs.simulation_context_present &&
           lhs.cause_kind == rhs.cause_kind &&
           lhs.cause_code == rhs.cause_code &&
           lhs.cause_ref == rhs.cause_ref &&
           lhs.validity_effect == rhs.validity_effect &&
           lhs.disposition == rhs.disposition &&
           lhs.message_key == rhs.message_key && lhs.detail == rhs.detail;
}

[[nodiscard]] bool exactly_same(const kernel::RunOutcome& lhs,
                                const kernel::RunOutcome& rhs) noexcept {
    const bool diagnostic_equal =
        lhs.primary_diagnostic.has_value() ==
            rhs.primary_diagnostic.has_value() &&
        (!lhs.primary_diagnostic.has_value() ||
         exactly_same(*lhs.primary_diagnostic, *rhs.primary_diagnostic));
    return lhs.image_fingerprint == rhs.image_fingerprint &&
           lhs.plan_id == rhs.plan_id && lhs.mission_id == rhs.mission_id &&
           lhs.source_semantic_hash == rhs.source_semantic_hash &&
           lhs.descriptor_semantic_hash == rhs.descriptor_semantic_hash &&
           lhs.run_start_kind == rhs.run_start_kind &&
           lhs.run_start_committed == rhs.run_start_committed &&
           lhs.final_status == rhs.final_status &&
           lhs.validity == rhs.validity &&
           lhs.initial_tick == rhs.initial_tick &&
           lhs.final_tick == rhs.final_tick &&
           lhs.initial_committed_epoch == rhs.initial_committed_epoch &&
           lhs.final_committed_epoch == rhs.final_committed_epoch &&
           lhs.committed_step_count == rhs.committed_step_count &&
           lhs.terminal_branch_committed ==
               rhs.terminal_branch_committed &&
           lhs.mission_result_available == rhs.mission_result_available &&
           lhs.finalization_status == rhs.finalization_status &&
           diagnostic_equal;
}

[[nodiscard]] const compiler::CompleteSourceOccurrence& occurrence_for(
    const compiler::CompleteStaticCompositionSource& source,
    std::string_view model_id) {
    const auto occurrence = std::find_if(
        source.occurrences.begin(), source.occurrences.end(),
        [model_id](const auto& value) {
            return value.model_id == model_id;
        });
    require(occurrence != source.occurrences.end(),
            "canonical source occurrence is missing");
    return *occurrence;
}

[[nodiscard]] std::shared_ptr<const Image> canonical_image() {
    const auto linked = ref_yyz::compile_00a_canonical_image();
    require(linked.succeeded(),
            std::string("canonical 00A Image failed: ") +
                diagnostics(linked));
    return std::make_shared<const Image>(*linked.value);
}

[[nodiscard]] yyz::AerodynamicTableDefinition aerodynamic_definition(
    std::string asset_id) {
    yyz::AerodynamicTableDefinition definition;
    definition.metadata = {
        std::string(yyz::kAerodynamicTableModelIdentity),
        std::string(yyz::kAerodynamicTableModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    definition.source_id = "aero.body";
    definition.configuration_id =
        "configuration.fixture.yyz.clean@1";
    definition.reference_area_square_meters = 1.0;
    definition.reference_span_meters = 1.0;
    definition.reference_chord_meters = 1.0;
    definition.body_origin_to_application.value =
        gnc::foundation::Vec3{0.2, 0.0, -25.0 / 18.0};
    definition.table_asset_id = std::move(asset_id);
    return definition;
}

struct PreparedAssets {
    yyz::AerodynamicTableAsset baseline_asset;
    yyz::AerodynamicTableAsset canonical_asset;
    yyz::PreparedAerodynamicTableModel baseline;
    yyz::PreparedAerodynamicTableModel canonical;
    yyz::AerodynamicTableQueryInput opening_query;
    yyz::AerodynamicTableQueryEvaluation opening_lookup;
};

[[nodiscard]] PreparedAssets verify_aerodynamic_successor() {
    auto baseline_asset = ref_yyz::make_aerodynamic_fixture_asset(
        ref_yyz::kBaselineAerodynamicAssetId);
    auto canonical_asset = ref_yyz::make_aerodynamic_fixture_asset(
        ref_yyz::kCanonicalAerodynamicAssetId);
    const auto repeated_asset = ref_yyz::make_aerodynamic_fixture_asset(
        ref_yyz::kCanonicalAerodynamicAssetId);
    require(baseline_asset.asset_id ==
                ref_yyz::kBaselineAerodynamicAssetId &&
                baseline_asset.mach_axis ==
                    std::vector<double>({0.2, 0.6}) &&
                baseline_asset.coefficient_rows_ca_cy_cn_cl_cm_cn.size() ==
                    8U &&
                canonical_asset.asset_id ==
                    ref_yyz::kCanonicalAerodynamicAssetId &&
                canonical_asset.mach_axis ==
                    std::vector<double>({0.2, 0.6, 0.8}) &&
                canonical_asset.alpha_axis_radians == std::vector<double>({
                    -3.14159265358979323846, -0.1, 0.1,
                    3.14159265358979323846}) &&
                canonical_asset.beta_axis_radians == std::vector<double>({
                    -1.57079632679489661923, -0.05, 0.05,
                    1.57079632679489661923}) &&
                canonical_asset.coefficient_rows_ca_cy_cn_cl_cm_cn.size() ==
                    48U &&
                repeated_asset.mach_axis == canonical_asset.mach_axis &&
                repeated_asset.alpha_axis_radians ==
                    canonical_asset.alpha_axis_radians &&
                repeated_asset.beta_axis_radians ==
                    canonical_asset.beta_axis_radians &&
                repeated_asset.coefficient_rows_ca_cy_cn_cl_cm_cn ==
                    canonical_asset
                        .coefficient_rows_ca_cy_cn_cl_cm_cn,
            "synthetic successor changed the frozen baseline asset");
    for (std::size_t mach = 0U; mach < 2U; ++mach) {
        for (std::size_t alpha = 0U; alpha < 2U; ++alpha) {
            for (std::size_t beta = 0U; beta < 2U; ++beta) {
                const auto baseline_row = mach * 4U + alpha * 2U + beta;
                const auto canonical_row =
                    (mach * 4U + (alpha + 1U)) * 4U + (beta + 1U);
                require(exact_array(
                            baseline_asset
                                .coefficient_rows_ca_cy_cn_cl_cm_cn[
                                    baseline_row],
                            canonical_asset
                                .coefficient_rows_ca_cy_cn_cl_cm_cn[
                                    canonical_row]),
                        "synthetic successor changed an old-domain corner");
            }
        }
    }

    auto baseline_outcome = yyz::prepare_aerodynamic_table_model(
        aerodynamic_definition(std::string(
            ref_yyz::kBaselineAerodynamicAssetId)),
        baseline_asset);
    auto canonical_outcome = yyz::prepare_aerodynamic_table_model(
        aerodynamic_definition(std::string(
            ref_yyz::kCanonicalAerodynamicAssetId)),
        canonical_asset);
    require(baseline_outcome.has_value() && canonical_outcome.has_value(),
            "accepted aerodynamic fixtures did not prepare");
    auto baseline = std::move(baseline_outcome.value());
    auto canonical = std::move(canonical_outcome.value());

    for (const double mach : {0.2, 0.35, 0.599}) {
        for (const double alpha : {-0.099, -0.017, 0.099}) {
            for (const double beta : {-0.049, 0.011, 0.049}) {
                const yyz::AerodynamicTableQueryInput point{
                    mach, alpha, beta};
                const auto old_value =
                    yyz::AerodynamicTableQueryKernel::evaluate(
                        baseline, point);
                const auto new_value =
                    yyz::AerodynamicTableQueryKernel::evaluate(
                        canonical, point);
                require(old_value.has_value() && new_value.has_value() &&
                            old_value.value()
                                    .output
                                    .coefficients_ca_cy_cn_cl_cm_cn ==
                                new_value.value()
                                    .output
                                    .coefficients_ca_cy_cn_cl_cm_cn,
                        "successor changed a baseline-domain query result");
            }
        }
    }

    const yyz::AerodynamicTableQueryInput opening_query{
        210.0 / 340.0, 0.0, 0.0};
    const auto old_opening = yyz::AerodynamicTableQueryKernel::evaluate(
        baseline, opening_query);
    const auto new_opening = yyz::AerodynamicTableQueryKernel::evaluate(
        canonical, opening_query);
    require(!old_opening.has_value() &&
                old_opening.status() ==
                    gnc::foundation::NumericalStatus::OutOfRange &&
                old_opening.evidence().detail == "table-query" &&
                new_opening.has_value() &&
                new_opening.value().telemetry.domain_status ==
                    gnc::foundation::InterpolationDomainStatus::Inside &&
                near(opening_query.mach,
                     0.61764705882352944, 2.0e-15),
            "old/new assets did not preserve and resolve the opening query");

    auto malformed_asset = canonical_asset;
    malformed_asset.coefficient_rows_ca_cy_cn_cl_cm_cn.pop_back();
    const auto malformed = yyz::prepare_aerodynamic_table_model(
        aerodynamic_definition(std::string(
            ref_yyz::kCanonicalAerodynamicAssetId)),
        malformed_asset);
    require(!malformed.has_value(),
            "malformed successor asset did not fail closed");

    return {std::move(baseline_asset), std::move(canonical_asset),
            std::move(baseline), std::move(canonical), opening_query,
            new_opening.value()};
}

void verify_source_plan_proof_image(
    const std::shared_ptr<const Image>& image) {
    const auto package = yyz::describe_yyz_rigid_step_package();
    const auto source = ref_yyz::make_00a_canonical_source(package);
    const auto mapped = yyz::Canonical00AInitialMappingQuery::evaluate(
        yyz::canonical_00a_initial_mapping_definition(),
        yyz::canonical_00a_author_input());
    require(mapped.has_value(), "package-owned canonical mapping failed");
    require(source.mission_id ==
                "mission.yyz.00a.abstract-engineering-baseline@1" &&
                source.plan_id ==
                    "plan.yyz.00a.abstract-engineering-baseline" &&
                source.mission_source.document_uri ==
                    "fixtures/ref-yyz-00a-canonical/source.json" &&
                source.clock.clock_id == "clock.yyz.00a.100hz@1" &&
                source.clock.base_step_seconds == 0.01 &&
                source.clock.terminal_tick == kTerminalTick &&
                source.entities.size() == 1U &&
                source.entities.front().entity_id ==
                    "vehicle.yyz.00a.abstract-engineering@1" &&
                source.observation_schedules.size() == 1U &&
                source.observation_schedules.front().step_interval == 4U,
            "canonical identity, clock, or observation source facts changed");

    const auto compiled = compiler::compile_complete_execution_plan(
        source, {package});
    require(compiled.succeeded(),
            std::string("canonical plan/proof compilation failed: ") +
                diagnostics(compiled));
    const auto observation_proof = std::find_if(
        compiled.value->proofs.records.begin(),
        compiled.value->proofs.records.end(), [](const auto& proof) {
            return proof.proof_id ==
                   "proof/observation-schedule/observation.00a.canonical.committed-rigid-mass";
        });
    require(observation_proof != compiled.value->proofs.records.end() &&
                observation_proof->kind ==
                    compiler::PlanProofKind::TemporalCompatibility &&
                image->mission_id() == source.mission_id &&
                image->plan_id() == source.plan_id &&
                image->clock().base_step_seconds == 0.01 &&
                image->clock().terminal_tick == kTerminalTick,
            "canonical source did not survive Plan/Proof/Image");

    const auto interval_one = ref_yyz::compile_complete_image();
    const auto target_rate =
        ref_yyz::compile_00a_target_rate_image(kTerminalTick);
    require(interval_one.succeeded(), diagnostics(interval_one));
    require(target_rate.succeeded(), diagnostics(target_rate));
    require(interval_one.value->fingerprint() == kIntervalOneFingerprint,
            std::string("unexpected interval-one fingerprint: ") +
                interval_one.value->fingerprint());
    require(image->fingerprint() == kCanonicalFingerprint,
            std::string("unexpected canonical fingerprint: ") +
                image->fingerprint() + "; descriptor=" +
                image->descriptor_semantic_hash() + "; proof=" +
                image->proof_index_hash());
    require(
                image->fingerprint() != kPriorCanonicalFingerprint &&
                image->fingerprint() != interval_one.value->fingerprint() &&
                image->fingerprint() != target_rate.value->fingerprint() &&
                image->source_semantic_hash() !=
                    target_rate.value->source_semantic_hash(),
            "successor asset did not receive a distinct Image identity");

    const auto& aero = occurrence_for(
        source, yyz::kAerodynamicTableModelIdentity);
    require(aero.asset_bindings.size() == 1U &&
                aero.asset_bindings.front().asset_id ==
                    ref_yyz::kCanonicalAerodynamicAssetId &&
                mapped.value().inertial_frame.id ==
                    yyz::kCanonical00ALaunchLocalEnuFrameId &&
                mapped.value().body_frame.id ==
                    yyz::kCanonical00ABodyFrameId,
            "canonical source did not select the successor asset or mapped frames");

    auto malformed_data = image->data();
    const auto malformed_occurrence = std::find_if(
        malformed_data.occurrences.begin(),
        malformed_data.occurrences.end(), [](const auto& value) {
            return value.definition_id ==
                   yyz::kAerodynamicTableModelIdentity;
        });
    require(malformed_occurrence != malformed_data.occurrences.end() &&
                malformed_occurrence->asset_bindings.size() == 1U,
            "canonical Image lacks its aerodynamic binding");
    malformed_occurrence->asset_bindings.front().asset_id =
        "aero-table.fixture.yyz.missing@1";
    const auto malformed_image = Image::freeze(std::move(malformed_data));
    const auto malformed_adapter =
        ref_yyz::make_session_adapter(malformed_image);
    require(!malformed_adapter &&
                malformed_adapter.error.find(
                    "unknown aerodynamic fixture asset") !=
                    std::string::npos,
            "Image with an unknown aerodynamic asset did not fail closed");
}

struct AirPoint {
    double airspeed = 0.0;
    double mach = 0.0;
    double alpha = 0.0;
    double beta = 0.0;
};

[[nodiscard]] AirPoint air_point(
    const CommittedRigidMassProbe& state) {
    const auto attitude = gnc::foundation::quaternion_from_wxyz(
        state.attitude_wxyz);
    const auto body_to_inertial =
        gnc::foundation::passive_rotation_matrix(attitude);
    require(body_to_inertial.has_value(),
            "failed-state attitude could not be inspected");
    const gnc::foundation::Vec3 relative_inertial{
        state.velocity[0U] - 10.0,
        state.velocity[1U],
        state.velocity[2U]};
    const auto relative_body =
        body_to_inertial.value().transpose() * relative_inertial;
    const double airspeed = relative_body.norm();
    return {airspeed, airspeed / 340.0,
            std::atan2(relative_body(2), relative_body(0)),
            std::atan2(relative_body(1),
                       std::hypot(relative_body(0), relative_body(2)))};
}

struct SessionCapture {
    ref_yyz::RefYyzSessionAdapter adapter;
    std::unique_ptr<kernel::Session> session;
    CommittedRigidMassProbe opening;
    CommittedRigidMassProbe tick_one;
    CommittedRigidMassProbe final_state;
    std::vector<CommittedHistorySampleProbe> history;
    MissionAccumulatorProbe accumulator;
    MissionResultProbe terminal_window;
    MissionResultProbe runwide;
    kernel::RunOutcome outcome;
};

[[nodiscard]] CommittedRigidMassProbe committed_state(
    const kernel::Session& session,
    const ref_yyz::RefYyzSessionAdapter& adapter) {
    CommittedRigidMassProbe state;
    const auto read = ref_yyz::read_committed_rigid_mass_for_qualification(
        session, adapter, state);
    require(static_cast<bool>(read), "committed state read failed");
    return state;
}

[[nodiscard]] SessionCapture run_canonical_session(
    const std::shared_ptr<const Image>& image, std::string run_id) {
    auto adapter = ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto created = kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(created), "canonical Session creation failed");
    const auto initialized = created.session->initialize(
        {kernel::RunId(std::move(run_id)),
         kernel::exact_run_binding(*image)});
    require(static_cast<bool>(initialized),
            "canonical Session initialization failed");
    const auto opening = committed_state(*created.session, adapter);

    const auto first_step = created.session->execute_step();
    if (!first_step) {
        const auto point = air_point(opening);
        throw std::runtime_error(
            "canonical tick-0 step failed: error=" +
            std::string(kernel::to_string(first_step.result.error)) +
            " detail=" + std::string(first_step.result.detail) +
            " mach=" + std::to_string(point.mach) +
            " alpha=" + std::to_string(point.alpha) +
            " beta=" + std::to_string(point.beta));
    }
    const auto tick_one = committed_state(*created.session, adapter);
    require(first_step.status == kernel::StepStatus::Committed &&
                first_step.tick_before == 0 && first_step.tick_after == 1 &&
                created.session->committed_tick() == 1 &&
                created.session->committed_epoch() == 1U &&
                adapter.step_execution->completed_intervals.size() == 1U &&
                adapter.step_execution->completed_intervals.front()
                        .rk4_derivative_evaluations == 4U,
            "canonical tick 1 did not commit through RK4");

    const auto drive = created.session->run_to_terminal();
    if (!drive ||
        created.session->state() != kernel::SessionState::Completed) {
        const auto failed_state = committed_state(*created.session, adapter);
        const auto point = air_point(failed_state);
        throw std::runtime_error(
            "canonical long run stopped: error=" +
            std::string(kernel::to_string(drive.result.error)) +
            " committed_tick=" +
            std::to_string(created.session->committed_tick()) +
            " detail=" + std::string(drive.result.detail) +
            " mach=" + std::to_string(point.mach) +
            " alpha=" + std::to_string(point.alpha) +
            " beta=" + std::to_string(point.beta));
    }

    const auto final_state = committed_state(*created.session, adapter);
    std::vector<CommittedHistorySampleProbe> history;
    MissionAccumulatorProbe accumulator;
    MissionResultProbe terminal_window;
    MissionResultProbe runwide;
    require(ref_yyz::read_committed_history_for_qualification(
                *created.session, history) &&
                ref_yyz::read_mission_accumulator_for_qualification(
                    *created.session, adapter, accumulator) &&
                ref_yyz::read_mission_result_for_qualification(
                    *created.session, adapter, terminal_window) &&
                ref_yyz::read_runwide_mission_result_for_qualification(
                    *created.session, adapter, runwide),
            "canonical terminal evidence read failed");
    require(created.session->run_outcome() != nullptr,
            "canonical RunOutcome is missing");
    const auto outcome = *created.session->run_outcome();
    return {std::move(adapter), std::move(created.session), opening,
            tick_one, final_state, std::move(history), accumulator,
            terminal_window, runwide, outcome};
}

[[nodiscard]] std::vector<std::int64_t> ticks(
    std::int64_t last, std::int64_t interval) {
    std::vector<std::int64_t> result;
    for (std::int64_t tick = 0; tick <= last; tick += interval) {
        result.push_back(tick);
    }
    return result;
}

void verify_cadence_and_held_ages(const SessionCapture& capture) {
    const auto& probe = *capture.adapter.opening_boundary;
    require(probe.navigation_output_ticks == ticks(kTerminalTick, 1) &&
                probe.navigation_source_ticks == ticks(kTerminalTick, 1) &&
                probe.guidance_output_ticks == ticks(kTerminalTick, 5) &&
                probe.controller_output_ticks == ticks(kTerminalTick, 2) &&
                probe.controller_output_context_ticks ==
                    ticks(kTerminalTick, 2) &&
                probe.actuator_output_ticks == ticks(kTerminalTick, 1),
            "canonical 1/5/2/1 cadence changed");
    for (const auto& sample : probe.navigation_input_samples) {
        require(sample.consumer_tick == sample.sample_tick &&
                    sample.age_steps == 0U && sample.fresh &&
                    sample.quality_valid,
                "navigation lost same-tick truth provenance");
    }
    std::uint64_t maximum_guidance_age = 0U;
    for (std::size_t index = 0U;
         index < probe.controller_guidance_samples.size(); ++index) {
        const auto& sample = probe.controller_guidance_samples[index];
        const auto source_tick = (sample.consumer_tick / 5) * 5;
        const auto age = static_cast<std::uint64_t>(
            sample.consumer_tick - source_tick);
        maximum_guidance_age = std::max(maximum_guidance_age, age);
        require(sample.sample_tick == source_tick &&
                    sample.age_steps == age &&
                    probe.controller_guidance_source_ticks[index] ==
                        source_tick &&
                    sample.fresh == (age == 0U) &&
                    sample.quality_valid,
                "guidance-to-controller HeldLatest age changed");
    }
    std::uint64_t maximum_controller_age = 0U;
    for (std::size_t index = 0U;
         index < probe.actuator_controller_samples.size(); ++index) {
        const auto& sample = probe.actuator_controller_samples[index];
        const auto source_tick = (sample.consumer_tick / 2) * 2;
        const auto age = static_cast<std::uint64_t>(
            sample.consumer_tick - source_tick);
        maximum_controller_age = std::max(maximum_controller_age, age);
        require(sample.sample_tick == source_tick &&
                    sample.age_steps == age &&
                    sample.fresh == (age == 0U) &&
                    sample.quality_valid &&
                    exact_double(
                        probe.actuator_pitch_moments[index],
                        probe.controller_moments[
                            static_cast<std::size_t>(source_tick / 2)]),
                "controller-to-actuator HeldLatest age changed");
    }
    require(maximum_guidance_age == 4U &&
                maximum_controller_age == 1U,
            "canonical HeldLatest maximum ages changed");

    require(capture.adapter.step_execution->integration_attempts ==
                static_cast<std::size_t>(kTerminalTick) &&
                capture.adapter.step_execution->completed_intervals.size() ==
                    static_cast<std::size_t>(kTerminalTick),
            "canonical integration interval count changed");
    bool actuator_reached_rk4 = false;
    for (std::size_t index = 0U;
         index < capture.adapter.step_execution->completed_intervals.size();
         ++index) {
        const auto& interval =
            capture.adapter.step_execution->completed_intervals[index];
        require(interval.opening_tick ==
                    static_cast<std::int64_t>(index) &&
                    interval.rk4_derivative_evaluations == 4U,
                "canonical interval did not use four RK4 derivatives");
        actuator_reached_rk4 =
            actuator_reached_rk4 ||
            (probe.actuator_pitch_moments[index] != 0.0 &&
             interval.held_pitch_moment_newton_meters != 0.0);
    }
    require(actuator_reached_rk4,
            "nonzero actuator output did not reach an RK4 held form");
}

[[nodiscard]] double speed_of(
    const CommittedRigidMassProbe& state) noexcept {
    return std::sqrt(state.velocity[0U] * state.velocity[0U] +
                     state.velocity[1U] * state.velocity[1U] +
                     state.velocity[2U] * state.velocity[2U]);
}

void verify_terminal_consistency(const SessionCapture& capture) {
    const auto& outcome = capture.outcome;
    const auto& aggregate = capture.accumulator;
    const auto& window = capture.terminal_window;
    const auto& result = capture.runwide;
    const auto& final_state = capture.final_state;
    require(capture.session->committed_tick() == kTerminalTick &&
                capture.session->committed_epoch() ==
                    static_cast<std::uint64_t>(kTerminalTick + 1) &&
                capture.session->state() == kernel::SessionState::Completed &&
                outcome.final_status == kernel::RunFinalStatus::Completed &&
                outcome.validity == contracts::EvidenceValidity::Valid &&
                outcome.initial_tick == 0 &&
                outcome.final_tick == kTerminalTick &&
                outcome.committed_step_count ==
                    static_cast<std::uint64_t>(kTerminalTick + 1) &&
                outcome.final_committed_epoch ==
                    static_cast<std::uint64_t>(kTerminalTick + 1) &&
                outcome.terminal_branch_committed &&
                outcome.mission_result_available,
            "canonical completed RunOutcome is inconsistent");
    require(capture.history.size() ==
                yyz::kCommittedMissionHistoryDepth &&
                capture.history[0U].tick == 2998 &&
                capture.history[1U].tick == 2999 &&
                capture.history[2U].tick == kTerminalTick &&
                exactly_same(capture.history.back().state, final_state),
            "canonical terminal history window is inconsistent");
    require(aggregate.present && aggregate.initialized &&
                aggregate.opening_tick == 0 &&
                aggregate.latest_tick == 2999 &&
                aggregate.evaluated_sample_count == 3000U &&
                near(aggregate.duration_seconds, 29.99) &&
                !aggregate.terminal_result_present &&
                window.present && window.completed &&
                window.initial_tick == 2998 &&
                window.final_tick == kTerminalTick &&
                window.terminal_tick == kTerminalTick &&
                window.reason_code == "duration-complete" &&
                window.evaluated_sample_count == 3U &&
                near(window.duration_seconds, 0.02) &&
                result.present && result.completed &&
                result.initial_tick == 0 &&
                result.final_tick == kTerminalTick &&
                result.terminal_tick == kTerminalTick &&
                result.reason_code == "duration-complete" &&
                result.priority == 100 &&
                result.evaluated_sample_count == 3001U &&
                near(result.duration_seconds, 30.0) &&
                near(result.downrange_meters, final_state.position[0U]) &&
                near(result.vertical_displacement_meters,
                     final_state.position[2U] - 1000.0) &&
                near(result.remaining_mass_kilograms,
                     final_state.mass_kilograms) &&
                near(result.remaining_mass_kilograms, 665.0, 1.0e-9) &&
                near(result.consumed_mass_kilograms, 15.0, 1.0e-9) &&
                near(result.terminal_speed_meters_per_second,
                     speed_of(final_state)),
            "canonical aggregate or terminal result is inconsistent");
}

void verify_opening_and_determinism(
    const std::shared_ptr<const Image>& image,
    const PreparedAssets& assets,
    const SessionCapture& first,
    const SessionCapture& second) {
    const auto& opening = first.opening;
    const auto& probe = *first.adapter.opening_boundary;
    require(near(opening.position[0U], 0.0) &&
                near(opening.position[1U], 0.0) &&
                near(opening.position[2U], 1000.0) &&
                near(opening.velocity[0U], 220.0) &&
                near(opening.velocity[1U], 0.0) &&
                near(opening.velocity[2U], 0.0) &&
                near(opening.mass_kilograms, 680.0) &&
                opening.mass_sample_tick == 0 &&
                first.tick_one.mass_sample_tick == 1 &&
                near(first.tick_one.mass_kilograms, 679.995, 1.0e-12),
            "canonical author opening or tick-1 mass changed");
    require(probe.aerodynamic_envelope_initialized &&
                near(probe.opening_airspeed, 210.0, 2.0e-12) &&
                near(probe.opening_mach, assets.opening_query.mach,
                     2.0e-15) &&
                near(probe.opening_alpha, 0.0) &&
                near(probe.opening_beta, 0.0) &&
                probe.opening_aerodynamic_coefficients ==
                    assets.opening_lookup.output
                        .coefficients_ca_cy_cn_cl_cm_cn &&
                probe.minimum_mach >=
                    assets.canonical_asset.mach_axis.front() &&
                probe.maximum_mach <=
                    assets.canonical_asset.mach_axis.back() &&
                probe.minimum_alpha >=
                    assets.canonical_asset.alpha_axis_radians.front() &&
                probe.maximum_alpha <=
                    assets.canonical_asset.alpha_axis_radians.back() &&
                probe.minimum_beta >=
                    assets.canonical_asset.beta_axis_radians.front() &&
                probe.maximum_beta <=
                    assets.canonical_asset.beta_axis_radians.back(),
            "canonical opening lookup or actual aerodynamic envelope changed");
    const auto& second_probe = *second.adapter.opening_boundary;
    require(exactly_same(first.opening, second.opening) &&
                exactly_same(first.tick_one, second.tick_one) &&
                exactly_same(first.final_state, second.final_state) &&
                exactly_same(first.history, second.history) &&
                exactly_same(first.accumulator, second.accumulator) &&
                exactly_same(first.terminal_window, second.terminal_window) &&
                exactly_same(first.runwide, second.runwide) &&
                exactly_same(first.outcome, second.outcome) &&
                exact_double(probe.minimum_mach,
                             second_probe.minimum_mach) &&
                exact_double(probe.maximum_mach,
                             second_probe.maximum_mach) &&
                exact_double(probe.minimum_alpha,
                             second_probe.minimum_alpha) &&
                exact_double(probe.maximum_alpha,
                             second_probe.maximum_alpha) &&
                exact_double(probe.minimum_beta,
                             second_probe.minimum_beta) &&
                exact_double(probe.maximum_beta,
                             second_probe.maximum_beta) &&
                probe.navigation_output_ticks ==
                    second_probe.navigation_output_ticks &&
                probe.guidance_output_ticks ==
                    second_probe.guidance_output_ticks &&
                probe.controller_output_ticks ==
                    second_probe.controller_output_ticks &&
                probe.actuator_output_ticks ==
                    second_probe.actuator_output_ticks,
            "two canonical 3000-tick Sessions were not bit-deterministic");

    std::cout << std::setprecision(17)
              << "canonical_00a_probe {\"old_asset_id\":\""
              << assets.baseline_asset.asset_id
              << "\",\"new_asset_id\":\""
              << assets.canonical_asset.asset_id
              << "\",\"old_opening_status\":\"OutOfRange\""
              << ",\"new_opening_status\":\"Success\""
              << ",\"old_domain\":{\"mach\":[0.2,0.6],\"alpha\":[-0.1,0.1],\"beta\":[-0.05,0.05]}"
              << ",\"new_domain\":{\"mach\":["
              << assets.canonical_asset.mach_axis.front() << ','
              << assets.canonical_asset.mach_axis.back()
              << "],\"alpha\":["
              << assets.canonical_asset.alpha_axis_radians.front() << ','
              << assets.canonical_asset.alpha_axis_radians.back()
              << "],\"beta\":["
              << assets.canonical_asset.beta_axis_radians.front() << ','
              << assets.canonical_asset.beta_axis_radians.back() << "]}"
              << ",\"opening\":{\"airspeed_mps\":"
              << probe.opening_airspeed << ",\"mach\":"
              << probe.opening_mach << ",\"alpha_rad\":"
              << probe.opening_alpha << ",\"beta_rad\":"
              << probe.opening_beta << ",\"dynamic_pressure_pa\":"
              << probe.opening_dynamic_pressure
              << ",\"coefficients\":[";
    for (std::size_t index = 0U;
         index < probe.opening_aerodynamic_coefficients.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << probe.opening_aerodynamic_coefficients[index];
    }
    std::cout << "]},\"image_fingerprint\":\""
              << image->fingerprint()
              << "\",\"tick_one_committed\":true"
              << ",\"terminal_status\":\"Completed\""
              << ",\"terminal_reason\":\""
              << first.runwide.reason_code
              << "\",\"terminal_tick\":" << first.runwide.terminal_tick
              << ",\"committed_intervals\":"
              << first.adapter.step_execution->completed_intervals.size()
              << ",\"terminal_state\":{\"position_enu_m\":["
              << first.final_state.position[0U] << ','
              << first.final_state.position[1U] << ','
              << first.final_state.position[2U]
              << "],\"velocity_enu_mps\":["
              << first.final_state.velocity[0U] << ','
              << first.final_state.velocity[1U] << ','
              << first.final_state.velocity[2U]
              << "],\"mass_kg\":"
              << first.final_state.mass_kilograms << "}"
              << ",\"actual_envelope\":{\"mach\":["
              << probe.minimum_mach << ',' << probe.maximum_mach
              << "],\"alpha_rad\":[" << probe.minimum_alpha << ','
              << probe.maximum_alpha << "],\"beta_rad\":["
              << probe.minimum_beta << ',' << probe.maximum_beta << "]}"
              << ",\"cadence_counts\":{\"navigation\":"
              << probe.navigation_output_ticks.size()
              << ",\"guidance\":" << probe.guidance_output_ticks.size()
              << ",\"controller\":"
              << probe.controller_output_ticks.size()
              << ",\"actuator\":" << probe.actuator_output_ticks.size()
              << ",\"observation_interval_ticks\":4}"
              << ",\"held_latest_max_age\":{\"guidance_to_controller\":4,\"controller_to_actuator\":1}"
              << ",\"actuator_reached_rk4\":true"
              << ",\"deterministic\":true}\n";
}

void run() {
    const auto assets = verify_aerodynamic_successor();
    const auto image = canonical_image();
    verify_source_plan_proof_image(image);
    auto first = run_canonical_session(
        image, "run:00a-canonical-successor-first");
    auto second = run_canonical_session(
        image, "run:00a-canonical-successor-second");
    verify_cadence_and_held_ages(first);
    verify_cadence_and_held_ages(second);
    verify_terminal_consistency(first);
    verify_terminal_consistency(second);
    verify_opening_and_determinism(image, assets, first, second);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_yyz_00a_canonical_probe --self-check\n";
        return 2;
    }
    try {
        run();
        std::cout <<
            "R3 YYZ canonical 00A: PASS "
            "(abstract_engineering_target_conformance)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kernel-yyz-00a-canonical: " << error.what() << '\n';
        return 1;
    }
}
