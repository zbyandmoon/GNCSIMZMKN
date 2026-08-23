#include "gnc/kernel/session.hpp"

#include "support/ref_yyz_complete_composition.hpp"
#include "support/ref_yyz_session_adapter.hpp"

#include <yyz/mass_commit.hpp>
#include <yyz/qualification_00a.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace ref_yyz = gnc::tests::ref_yyz;
namespace yyz = gnc::packages::yyz;

using Image = contracts::ExecutionPlanImage;

constexpr std::string_view kIntervalOneFingerprint =
    "7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14";
constexpr std::string_view kCanonicalFingerprint =
    "e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c";

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Outcome>
[[nodiscard]] std::string diagnostics(const Outcome& outcome) {
    std::string text;
    for (const auto& diagnostic : outcome.diagnostics) {
        if (!text.empty()) {
            text += "; ";
        }
        text += std::string(compiler::to_string(diagnostic.code));
        text += " ";
        text += diagnostic.subject;
        text += ": ";
        text += diagnostic.detail;
    }
    return text;
}

[[nodiscard]] bool near(double lhs, double rhs,
                        double tolerance = 2.0e-12) {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] yyz::AerodynamicTableAsset accepted_aerodynamic_asset() {
    yyz::AerodynamicTableAsset table;
    table.asset_schema_id =
        std::string(yyz::kAerodynamicTableAssetSchemaIdentity);
    table.asset_id = "aero-table.fixture.yyz.multiaffine@1";
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
    return table;
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
                source.clock.clock_id ==
                    "clock.yyz.00a.100hz@1" &&
                source.clock.base_step_seconds == 0.01 &&
                source.clock.terminal_tick == 3000 &&
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
                image->clock().terminal_tick == 3000,
            "canonical source did not survive Plan/Proof/Image");

    const auto interval_one = ref_yyz::compile_complete_image();
    const auto target_rate =
        ref_yyz::compile_00a_target_rate_image(3000);
    require(interval_one.succeeded() && target_rate.succeeded() &&
                interval_one.value->fingerprint() ==
                    kIntervalOneFingerprint &&
                image->fingerprint() == kCanonicalFingerprint &&
                image->fingerprint() !=
                    interval_one.value->fingerprint() &&
                image->fingerprint() !=
                    target_rate.value->fingerprint() &&
                image->source_semantic_hash() !=
                    target_rate.value->source_semantic_hash(),
            "canonical profile reused an old Image identity or changed the interval-1 fingerprint");

    const auto& aero = occurrence_for(
        source, yyz::kAerodynamicTableModelIdentity);
    require(aero.asset_bindings.size() == 1U &&
                aero.asset_bindings.front().asset_id ==
                    "aero-table.fixture.yyz.multiaffine@1" &&
                mapped.value().inertial_frame.id ==
                    yyz::kCanonical00ALaunchLocalEnuFrameId &&
                mapped.value().body_frame.id ==
                    yyz::kCanonical00ABodyFrameId,
            "canonical source changed the accepted aero asset or mapped frames");
}

void verify_real_session_domain_outcome(
    const std::shared_ptr<const Image>& image) {
    auto adapter = ref_yyz::make_session_adapter(*image);
    require(static_cast<bool>(adapter), adapter.error);
    auto created = kernel::create_session(image, adapter.provider);
    require(static_cast<bool>(created), "canonical Session creation failed");
    const auto initialized = created.session->initialize(
        {kernel::RunId("run:00a-canonical-domain"),
         kernel::exact_run_binding(*image)});
    require(static_cast<bool>(initialized),
            "canonical Session initialization failed");

    ref_yyz::CommittedRigidMassProbe opening;
    require(ref_yyz::read_committed_rigid_mass_for_qualification(
                *created.session, adapter, opening) &&
                near(opening.position[0], 0.0) &&
                near(opening.position[1], 0.0) &&
                near(opening.position[2], 1000.0) &&
                near(opening.velocity[0], 220.0) &&
                near(opening.velocity[1], 0.0) &&
                near(opening.velocity[2], 0.0) &&
                near(opening.angular_rate[0], 0.0) &&
                near(opening.angular_rate[1], 0.0) &&
                near(opening.angular_rate[2], 0.0) &&
                near(opening.center_of_mass[0], 0.2) &&
                near(opening.center_of_mass[1], 0.0) &&
                near(opening.center_of_mass[2], 0.0) &&
                near(opening.mass_kilograms, 680.0) &&
                opening.mass_sample_tick == 0,
            "mapped canonical opening state did not reach Session storage");

    const auto step = created.session->execute_step();
    require(!step && step.status == kernel::StepStatus::Failed &&
                step.result.error == kernel::SessionError::InvocationFailed &&
                step.result.detail ==
                    "controlled boundary preparation failed" &&
                step.tick_before == 0 && step.tick_after == 0 &&
                created.session->committed_tick() == 0 &&
                created.session->committed_epoch() == 0U &&
                created.session->state() == kernel::SessionState::Failed &&
                step.primary_diagnostic.has_value() &&
                step.primary_diagnostic->tick == 0 &&
                step.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::BoundaryInvocation &&
                adapter.opening_boundary->environment_query_calls == 1U &&
                adapter.opening_boundary->aerodynamic_query_calls == 1U &&
                near(adapter.opening_boundary->guidance_altitude_error,
                     0.0) &&
                near(adapter.opening_boundary->guidance_raw_command,
                     0.0) &&
                near(adapter.opening_boundary->guidance_command, 0.0) &&
                near(adapter.opening_boundary->controller_pitch_error,
                     0.0) &&
                near(adapter.opening_boundary->controller_raw_moment,
                     0.0) &&
                near(adapter.opening_boundary->controller_moment, 0.0) &&
                near(adapter.opening_boundary->actuator_moment[0], 0.0) &&
                near(adapter.opening_boundary->actuator_moment[1], 0.0) &&
                near(adapter.opening_boundary->actuator_moment[2], 0.0) &&
                near(adapter.opening_boundary->propulsion_force[0], 100.0) &&
                near(adapter.opening_boundary->propulsion_force[1], 0.0) &&
                near(adapter.opening_boundary->propulsion_force[2], 0.0) &&
                near(adapter.opening_boundary->mass_flow_rate, 0.5),
            "canonical horizon did not preserve the tick-0 product-domain failure");

    const auto mapped = yyz::Canonical00AInitialMappingQuery::evaluate(
        yyz::canonical_00a_initial_mapping_definition(),
        yyz::canonical_00a_author_input());
    require(mapped.has_value(), "canonical mapping failed during domain audit");
    const gnc::foundation::Vec3 relative_inertial =
        mapped.value().initial_rigid_state.velocity.value -
        gnc::foundation::Vec3{10.0, 0.0, 0.0};
    yyz::AerodynamicTableQueryInput query;
    query.mach = relative_inertial.norm() / 340.0;
    query.alpha_radians = 0.0;
    query.beta_radians = 0.0;

    const auto package = yyz::describe_yyz_rigid_step_package();
    const auto source = ref_yyz::make_00a_canonical_source(package);
    const auto& aero_occurrence = occurrence_for(
        source, yyz::kAerodynamicTableModelIdentity);
    const auto definition = yyz::build_aerodynamic_table_definition(
        aero_occurrence.configuration,
        aero_occurrence.asset_bindings.front().asset_id);
    require(definition.has_value(),
            "canonical aero definition did not rebuild");
    const auto prepared = yyz::prepare_aerodynamic_table_model(
        definition.value(), accepted_aerodynamic_asset());
    require(prepared.has_value(), "accepted aero asset did not prepare");
    const auto lookup = yyz::AerodynamicTableQueryKernel::evaluate(
        prepared.value(), query);
    require(!lookup.has_value() &&
                lookup.status() ==
                    gnc::foundation::NumericalStatus::OutOfRange &&
                lookup.evidence().algorithm.id ==
                    yyz::kAerodynamicTableQueryIdentity.id &&
                lookup.evidence().detail == "table-query" &&
                query.mach > prepared.value().asset().mach_axis.back() &&
                near(query.mach, 0.61764705882352944, 2.0e-15),
            "exact canonical Mach input did not reproduce the accepted aero-table domain failure");

    std::cout << std::setprecision(17)
              << "canonical_00a_probe {\"image_fingerprint\":\""
              << image->fingerprint()
              << "\",\"horizon_ticks\":3000,\"failure_tick\":0"
              << ",\"model_id\":\""
              << yyz::kAerodynamicTableModelIdentity
              << "\",\"asset_id\":\""
              << prepared.value().asset().asset_id
              << "\",\"position_enu_m\":[" << opening.position[0]
              << ',' << opening.position[1] << ',' << opening.position[2]
              << "],\"velocity_enu_mps\":[" << opening.velocity[0]
              << ',' << opening.velocity[1] << ',' << opening.velocity[2]
              << "],\"q_i_b_wxyz\":[" << opening.attitude_wxyz[0]
              << ',' << opening.attitude_wxyz[1] << ','
              << opening.attitude_wxyz[2] << ','
              << opening.attitude_wxyz[3]
              << "],\"angular_rate_body_radps\":["
              << opening.angular_rate[0] << ',' << opening.angular_rate[1]
              << ',' << opening.angular_rate[2]
              << "],\"center_of_mass_body_m\":["
              << opening.center_of_mass[0] << ',' << opening.center_of_mass[1]
              << ',' << opening.center_of_mass[2]
              << "],\"mass_kg\":" << opening.mass_kilograms
              << ",\"guidance_altitude_error_m\":"
              << adapter.opening_boundary->guidance_altitude_error
              << ",\"guidance_raw_command_rad\":"
              << adapter.opening_boundary->guidance_raw_command
              << ",\"guidance_command_rad\":"
              << adapter.opening_boundary->guidance_command
              << ",\"controller_pitch_error_rad\":"
              << adapter.opening_boundary->controller_pitch_error
              << ",\"controller_raw_moment_nm\":"
              << adapter.opening_boundary->controller_raw_moment
              << ",\"controller_moment_nm\":"
              << adapter.opening_boundary->controller_moment
              << ",\"actuator_moment_nm\":["
              << adapter.opening_boundary->actuator_moment[0] << ','
              << adapter.opening_boundary->actuator_moment[1] << ','
              << adapter.opening_boundary->actuator_moment[2]
              << "],\"propulsion_force_n\":["
              << adapter.opening_boundary->propulsion_force[0] << ','
              << adapter.opening_boundary->propulsion_force[1] << ','
              << adapter.opening_boundary->propulsion_force[2]
              << "],\"mass_flow_kgps\":"
              << adapter.opening_boundary->mass_flow_rate
              << ",\"gravity_enu_mps2\":[0,0,-9.8066500000000003]"
              << ",\"wind_enu_mps\":[10,0,0]"
              << ",\"density_kgpm3\":1.225"
              << ",\"speed_of_sound_mps\":340"
              << ",\"relative_velocity_enu_mps\":["
              << relative_inertial(0) << ',' << relative_inertial(1)
              << ',' << relative_inertial(2)
              << "],\"airspeed_mps\":" << relative_inertial.norm()
              << ",\"mach\":" << query.mach
              << ",\"domain_mach\":["
              << prepared.value().asset().mach_axis.front() << ','
              << prepared.value().asset().mach_axis.back()
              << "],\"status\":\""
              << gnc::foundation::to_string(lookup.status())
              << "\",\"detail\":\"" << lookup.evidence().detail
              << "\",\"session_error\":\""
              << kernel::to_string(step.result.error)
              << "\",\"committed_intervals\":0}\n";
}

} // namespace

int main() {
    try {
        const auto image = canonical_image();
        verify_source_plan_proof_image(image);
        verify_real_session_domain_outcome(image);
        std::cout << "R3 YYZ canonical 00A abstract-engineering: DOMAIN-LOCKED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kernel-yyz-00a-canonical: " << error.what() << '\n';
        return 1;
    }
}
