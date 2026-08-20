#include <gnc/compiler/static_mission_compiler.hpp>
#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
using gnc::foundation::NumericalPolicy;
using gnc::foundation::QuaternionNormalizationPolicy;
using gnc::foundation::QuaternionPolicy;
using gnc::foundation::Vec3;
using gnc::model_sdk::CoarsePhase;
using gnc::model_sdk::RuntimeExecutionObligation;
using gnc::model_sdk::StaticEntryKind;
using gnc::model_sdk::StaticModelDescriptor;
using gnc::model_sdk::StaticRuntimeObligationEntryDescriptor;
using gnc::model_sdk::StaticStateReadKind;
using gnc::model_sdk::StaticStateWriteKind;
using namespace gnc::packages::yyz;

static_assert(std::is_same_v<
              decltype(UniformEnvironmentQueryEvaluation{}.output),
              EnvironmentInput>);
static_assert(std::is_same_v<
              decltype(AerodynamicTableQueryEvaluation{}.output),
              AerodynamicTableQueryOutput>);
static_assert(std::is_same_v<
              decltype(ForceMomentClosureEvaluation{}.output),
              RigidFormInput>);
static_assert(std::is_same_v<
              decltype(ControlledRigidBoundaryEvaluation{}.output),
              RigidFormInput>);
static_assert(!std::is_assignable_v<
              decltype((std::declval<ControlledRigidRuntimeCell&>()
                            .definition)),
              ControlledRigidBoundaryEvaluationDefinition>);
static_assert(!std::is_assignable_v<
              decltype((std::declval<ScalarBurnMassRuntimeCell&>()
                            .bindings)),
              ScalarBurnMassRuntimeCellBindings>);

constexpr std::string_view kInertialFrame =
    "frame.fixture.yyz.inertial-cartesian@1";
constexpr std::string_view kBodyFrame = "frame.fixture.yyz.body@1";
constexpr std::string_view kClock = "clock.fixture.yyz.simulation@1";
constexpr std::string_view kMassStateId = "mass.fixture.yyz.vehicle@1";

std::size_t environment_query_calls = 0U;
std::size_t aerodynamic_query_calls = 0U;
std::size_t force_moment_closure_calls = 0U;

[[nodiscard]] auto counting_environment_query(
    const PreparedUniformEnvironmentModel& model,
    const UniformEnvironmentQueryInput& input)
    -> decltype(UniformEnvironmentQueryKernel::evaluate(model, input)) {
    ++environment_query_calls;
    return UniformEnvironmentQueryKernel::evaluate(model, input);
}

[[nodiscard]] auto counting_aerodynamic_query(
    const PreparedAerodynamicTableModel& model,
    const AerodynamicTableQueryInput& input)
    -> decltype(AerodynamicTableQueryKernel::evaluate(model, input)) {
    ++aerodynamic_query_calls;
    return AerodynamicTableQueryKernel::evaluate(model, input);
}

[[nodiscard]] auto counting_force_moment_closure(
    const PreparedForceMomentClosureModel& model,
    const ForceMomentClosureInput& input)
    -> decltype(ForceMomentClosureKernel::evaluate(model, input)) {
    ++force_moment_closure_calls;
    return ForceMomentClosureKernel::evaluate(model, input);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Builder>
void require_rejects_truncated_definition(
    gnc::model_sdk::CanonicalConfigBlock configuration,
    Builder builder,
    const gnc::foundation::AlgorithmIdentity& identity,
    std::string_view message) {
    require(!configuration.fields.empty(),
            "definition fixture has no canonical fields");
    configuration.fields.pop_back();
    const auto result = builder(configuration);
    require(!result.has_value() &&
                result.status() ==
                    gnc::foundation::NumericalStatus::DomainError &&
                result.evidence().algorithm.id == identity.id &&
                result.evidence().algorithm.version == identity.version &&
                result.evidence().detail == "canonical-config",
            message);
}

[[nodiscard]] bool exactly(const Vec3& lhs, const Vec3& rhs) {
    return lhs(0) == rhs(0) && lhs(1) == rhs(1) && lhs(2) == rhs(2);
}

[[nodiscard]] bool exactly(const Mat3& lhs, const Mat3& rhs) {
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            if (lhs(row, column) != rhs(row, column)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool exactly(
    const gnc::foundation::QuaternionStorage& lhs,
    const gnc::foundation::QuaternionStorage& rhs) {
    const auto lhs_wxyz = gnc::foundation::quaternion_to_wxyz(lhs);
    const auto rhs_wxyz = gnc::foundation::quaternion_to_wxyz(rhs);
    return lhs_wxyz == rhs_wxyz;
}

[[nodiscard]] bool exactly(const RigidState& lhs,
                           const RigidState& rhs) {
    return exactly(lhs.position.value, rhs.position.value) &&
           exactly(lhs.velocity.value, rhs.velocity.value) &&
           exactly(lhs.attitude.value, rhs.attitude.value) &&
           exactly(lhs.angular_rate.value, rhs.angular_rate.value);
}

[[nodiscard]] bool exactly(const SampleContext& lhs,
                           const SampleContext& rhs) {
    return lhs.frame == rhs.frame &&
           lhs.clock_domain == rhs.clock_domain &&
           lhs.sample_time.tick == rhs.sample_time.tick &&
           lhs.sample_time.seconds == rhs.sample_time.seconds &&
           lhs.configuration_revision == rhs.configuration_revision &&
           lhs.quality == rhs.quality;
}

[[nodiscard]] NumericalPolicy numerical_policy() {
    NumericalPolicy policy;
    policy.absolute_tolerance = 2.0e-12;
    policy.relative_tolerance = 2.0e-12;
    policy.finite_check = FiniteCheck::EveryStage;
    policy.zero_tolerance = 1.0e-14;
    policy.condition_limit = 1.0e12;
    return policy;
}

[[nodiscard]] QuaternionPolicy quaternion_policy() {
    QuaternionPolicy policy;
    policy.numerical = numerical_policy();
    policy.normalization =
        QuaternionNormalizationPolicy::NormalizeWithFlag;
    return policy;
}

[[nodiscard]] SampleContext sample_context(std::string_view frame) {
    return {FrameIdentity{std::string(frame)},
            ClockDomainIdentity{std::string(kClock)},
            SimulationInstant{0, 0.0}, 11, DataQuality::Valid};
}

[[nodiscard]] IntervalSampleContext interval_context(
    std::string_view frame) {
    return {sample_context(frame),
            HalfOpenValidityInterval{SimulationInstant{0, 0.0},
                                     SimulationInstant{1, 0.1}}};
}

[[nodiscard]] RigidStepModelDefinition rigid_definition() {
    RigidStepModelDefinition definition;
    definition.inertial_frame = FrameIdentity{std::string(kInertialFrame)};
    definition.force_moment_closure.metadata = {
        std::string(kForceMomentClosureModelIdentity),
        std::string(kForceMomentClosureModelVersion),
        gnc::model_sdk::ModelExecutionForm::Closure};
    definition.force_moment_closure.body_frame =
        FrameIdentity{std::string(kBodyFrame)};
    definition.force_moment_closure.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    definition.force_moment_closure.configuration_revision = 11;
    definition.force_moment_closure.numerical_policy = numerical_policy();
    definition.algorithm.fixed_step_seconds = 0.1;
    definition.algorithm.numerical_policy = numerical_policy();
    definition.algorithm.attitude_evaluation_policy = quaternion_policy();
    definition.algorithm.candidate_attitude_policy = quaternion_policy();

    auto& aerodynamics = definition.aerodynamics;
    aerodynamics.metadata = {
        std::string(kAerodynamicTableModelIdentity),
        std::string(kAerodynamicTableModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    aerodynamics.source_id = "aero.body";
    aerodynamics.configuration_id = "configuration.fixture.yyz.clean@1";
    aerodynamics.reference_area_square_meters = 1.0;
    aerodynamics.reference_span_meters = 1.0;
    aerodynamics.reference_chord_meters = 1.0;
    aerodynamics.body_origin_to_application.value =
        Vec3{0.2, 0.0, -25.0 / 18.0};
    aerodynamics.table_asset_id =
        "aero-table.fixture.yyz.multiaffine@1";

    auto& table = definition.aerodynamic_table;
    table.asset_schema_id =
        std::string(kAerodynamicTableAssetSchemaIdentity);
    table.asset_id = aerodynamics.table_asset_id;
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

[[nodiscard]] UniformEnvironmentDefinition environment_definition() {
    UniformEnvironmentDefinition definition;
    definition.metadata = {
        std::string(kUniformEnvironmentModelIdentity),
        std::string(kUniformEnvironmentModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    definition.inertial_frame = FrameIdentity{std::string(kInertialFrame)};
    definition.clock_domain = ClockDomainIdentity{std::string(kClock)};
    definition.configuration_revision = 11;
    definition.gravity.value = Vec3{0.0, 0.0, -9.80665};
    definition.velocity_airmass.value = Vec3{10.0, 0.0, 0.0};
    definition.density_kilograms_per_cubic_meter = 1.225;
    definition.speed_of_sound_meters_per_second = 340.0;
    return definition;
}

[[nodiscard]] RigidStepInput rigid_input() {
    RigidStepInput input;
    input.context = {FrameIdentity{std::string(kInertialFrame)},
                     FrameIdentity{std::string(kBodyFrame)},
                     ClockDomainIdentity{std::string(kClock)},
                     SimulationInstant{0, 0.0},
                     SimulationInstant{1, 0.1}, 11,
                     DataQuality::Valid};
    input.committed_state.position.value = Vec3{0.0, 0.0, 1000.0};
    input.committed_state.velocity.value = Vec3{110.0, 0.0, 0.0};
    input.committed_state.attitude.value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
    input.committed_state.angular_rate.value = Vec3::Zero();
    input.environment.context = sample_context(kInertialFrame);
    input.environment.gravity.value = Vec3{0.0, 0.0, -9.80665};
    input.environment.velocity_airmass.value = Vec3{10.0, 0.0, 0.0};
    input.environment.density_kilograms_per_cubic_meter = 1.225;
    input.environment.speed_of_sound_meters_per_second = 340.0;
    input.mass_properties.context = interval_context(kBodyFrame);
    input.mass_properties.mass_state_id = std::string(kMassStateId);
    input.mass_properties.mass_kilograms = 100.0;
    input.mass_properties.body_origin_to_center_of_mass.value =
        Vec3{0.2, 0.0, 0.0};
    input.mass_properties.inertia_about_center_of_mass.value = Mat3::Zero();
    input.mass_properties.inertia_about_center_of_mass.value.diagonal() =
        Vec3{10.0, 20.0, 30.0};
    input.supplied_wrench.context = interval_context(kBodyFrame);
    input.supplied_wrench.source_id = "propulsion+actuator";
    input.supplied_wrench.force.value = Vec3{100.0, 0.0, 0.0};
    input.supplied_wrench.body_origin_to_application.value =
        Vec3{0.2, 0.2, 0.0};
    input.supplied_wrench.intrinsic_moment_at_application.value =
        Vec3{0.0, 0.0, 20.0};
    return input;
}

[[nodiscard]] const StaticModelDescriptor& find_model(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::string_view model_id) {
    const auto found = std::find_if(
        package.models.begin(), package.models.end(),
        [model_id](const auto& model) {
            return model.definition.model_id == model_id;
        });
    require(found != package.models.end(), "static model is missing");
    return *found;
}

[[nodiscard]] const StaticRuntimeObligationEntryDescriptor& find_entry(
    const StaticModelDescriptor& model,
    RuntimeExecutionObligation obligation) {
    require(model.runtime_component.has_value(),
            "runtime descriptor is missing");
    const auto& entries = model.runtime_component->obligation_entries;
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [obligation](const auto& entry) {
            return entry.obligation == obligation;
        });
    require(found != entries.end(), "runtime obligation entry is missing");
    return *found;
}

[[nodiscard]] StaticEntryKind entry_kind(
    RuntimeExecutionObligation obligation) {
    switch (obligation) {
    case RuntimeExecutionObligation::PublishProjection:
        return StaticEntryKind::PublishProjection;
    case RuntimeExecutionObligation::BoundaryEvaluation:
        return StaticEntryKind::BoundaryEvaluation;
    case RuntimeExecutionObligation::IntervalEvolution:
        return StaticEntryKind::IntervalEvolution;
    case RuntimeExecutionObligation::DerivativeEvaluation:
        return StaticEntryKind::DerivativeEvaluation;
    }
    return StaticEntryKind::BoundaryEvaluation;
}

void verify_catalog_and_descriptors() {
    const auto package = describe_yyz_rigid_step_package();
    const auto catalog = gnc::compiler::Catalog::build({package});
    if (!catalog.succeeded()) {
        for (const auto& diagnostic : catalog.diagnostics) {
            std::cerr << diagnostic.subject << ": "
                      << diagnostic.detail << '\n';
        }
    }
    require(catalog.succeeded(), "Catalog rejected YYZ static products");
    require(package.models.size() == 10U,
            "Wave A package model inventory changed");
    for (const auto& model : package.models) {
        require(std::is_sorted(
                    model.configuration.fields.begin(),
                    model.configuration.fields.end(),
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.field_id < rhs.field_id;
                    }),
                "configuration schema is not canonical");
        if (model.runtime_component.has_value()) {
            const auto& runtime = *model.runtime_component;
            require(!runtime.definition_builder_id.empty() &&
                        !runtime.definition_builder_version.empty() &&
                        !runtime.definition_builder_call_shape_id.empty(),
                    "runtime definition builder facts are incomplete");
        }
    }

    const auto& closure =
        find_model(package, kForceMomentClosureModelIdentity);
    const auto& aero = find_model(package, kAerodynamicTableModelIdentity);
    const auto& environment =
        find_model(package, kUniformEnvironmentModelIdentity);
    require(closure.closure.has_value() &&
                closure.closure->request_contract_id ==
                    kForceMomentClosureInputContractIdentity &&
                aero.pure_query.has_value() &&
                aero.pure_query->request_contract_id ==
                    kAerodynamicOperatingPointContractIdentity &&
                environment.pure_query.has_value() &&
                environment.pure_query->request_contract_id ==
                    kEnvironmentQueryContractIdentity &&
                !closure.preparation_call_shape_id.empty() &&
                !closure.closure->closure_call_shape_id.empty() &&
                !aero.preparation_call_shape_id.empty() &&
                !aero.pure_query->query_call_shape_id.empty() &&
                !environment.preparation_call_shape_id.empty() &&
                !environment.pure_query->query_call_shape_id.empty(),
            "query or closure request contract changed");

    const auto& rigid = find_model(package, kRigidStepModelIdentity);
    const auto& rigid_runtime = *rigid.runtime_component;
    require(rigid_runtime.state_owner.has_value() &&
                rigid_runtime.state_owner->schema.layout_id ==
                    kRigidStateLayoutIdentity &&
                !rigid_runtime.state_owner->initial_state_input_schema.fields
                     .empty(),
            "rigid state owner facts changed");
    const auto& rigid_publish = find_entry(
        rigid, RuntimeExecutionObligation::PublishProjection);
    const auto& rigid_boundary = find_entry(
        rigid, RuntimeExecutionObligation::BoundaryEvaluation);
    const auto& rigid_derivative = find_entry(
        rigid, RuntimeExecutionObligation::DerivativeEvaluation);
    require(rigid_publish.phase == CoarsePhase::Publish &&
                rigid_publish.state_read == StaticStateReadKind::Committed,
            "rigid publish ownership facts changed");
    require(rigid_boundary.phase == CoarsePhase::Form &&
                rigid_boundary.entry_id ==
                    kControlledRigidBoundaryEvaluationIdentity.id &&
                rigid_boundary.request_contract_id ==
                    kControlledRigidBoundaryInputContractIdentity &&
                rigid_boundary.result_contract_id ==
                    kControlledRigidBoundaryPreparationContractIdentity,
            "rigid boundary identity or contracts changed");
    require(rigid_boundary.result_codec.has_value(),
            "rigid boundary result codec is missing");
    require(rigid_boundary.result_codec->layout_id ==
                kControlledRigidBoundaryPreparationLayoutIdentity,
            std::string("rigid boundary result codec layout changed: ") +
                rigid_boundary.result_codec->layout_id);
    require(rigid_boundary.invocation_requirements.size() == 3U,
            "rigid boundary invocation inventory changed");
    require(rigid_derivative.phase == CoarsePhase::Form &&
                rigid_derivative.state_read ==
                    StaticStateReadKind::Candidate &&
                rigid_derivative.state_write == StaticStateWriteKind::None,
            "rigid derivative ownership facts changed");

    const auto& mass = find_model(package, kScalarBurnMassModelIdentity);
    const auto& mass_evolution = find_entry(
        mass, RuntimeExecutionObligation::IntervalEvolution);
    require(mass.runtime_component->state_owner.has_value() &&
                mass.runtime_component->state_owner->schema.layout_id ==
                    kMassStateLayoutIdentity &&
                mass_evolution.phase == CoarsePhase::Form &&
                mass_evolution.state_read == StaticStateReadKind::Committed &&
                mass_evolution.state_write ==
                    StaticStateWriteKind::IntervalCandidate,
            "mass owner or evolution facts changed");

    const auto& propulsion =
        find_model(package, kSuppliedPropulsionModelIdentity);
    const auto& propulsion_entry = find_entry(
        propulsion, RuntimeExecutionObligation::BoundaryEvaluation);
    require(std::none_of(
                propulsion.ports.begin(), propulsion.ports.end(),
                [](const auto& port) {
                    return port.direction ==
                           gnc::model_sdk::StaticPortDirection::Input;
                }) &&
                propulsion_entry.entry_id ==
                    kFixedSuppliedPropulsionBoundaryIdentity.id &&
                propulsion_entry.request_contract_id ==
                    kFixedSuppliedPropulsionRequestContractIdentity &&
                propulsion_entry.input_port_ids.empty(),
            "fixed supplied propulsion ceased to be config-driven");

    const auto& evaluator =
        find_model(package, kCommittedMissionResultModelIdentity);
    const auto& evaluator_entry = find_entry(
        evaluator, RuntimeExecutionObligation::BoundaryEvaluation);
    require(evaluator.runtime_component->schedule.trigger ==
                gnc::model_sdk::StaticScheduleTrigger::TerminalSequenceReady &&
                evaluator.runtime_component->schedule.step_interval == 0U &&
                evaluator_entry.phase == CoarsePhase::Evaluation &&
                evaluator_entry.entry_id ==
                    kCommittedMissionHistoryEvaluationIdentity.id &&
                evaluator.runtime_component->evaluator_history_shape
                    .has_value() &&
                evaluator.runtime_component->evaluator_history_shape
                        ->request_contract_id ==
                    kCommittedRigidMassSequenceContractIdentity &&
                evaluator.runtime_component->evaluator_history_shape->depth ==
                    kCommittedMissionHistoryDepth &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members.size() == 2U &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[0U].member_id ==
                    kCommittedMissionRigidHistoryMemberId &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[0U].state_schema_id ==
                    kRigidStateSchemaIdentity &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[0U].state_layout_id ==
                    kRigidStateLayoutIdentity &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[1U].member_id ==
                    kCommittedMissionMassHistoryMemberId &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[1U].state_schema_id ==
                    kMassStateSchemaIdentity &&
                evaluator.runtime_component->evaluator_history_shape
                        ->ordered_members[1U].state_layout_id ==
                    kMassStateLayoutIdentity,
            "terminal evaluator schedule changed");
}

void verify_implementation_table() {
    const auto package = describe_yyz_rigid_step_package();
    const auto implementation =
        describe_yyz_rigid_step_implementation("yyz-static-contract-test");
    require(implementation.package_id == package.package_id &&
                implementation.package_version == package.package_version &&
                implementation.build_fingerprint ==
                    "yyz-static-contract-test" &&
                implementation.entries.size() == 44U &&
                implementation.state_layouts.size() == 2U &&
                implementation.value_layouts.size() == 10U,
            "static implementation inventory changed");

    std::set<std::string> exact_entries;
    std::size_t evaluator_history_witness_count = 0U;
    for (const auto& entry : implementation.entries) {
        require(entry.typed_entry.has_value() &&
                    !entry.call_shape_id.empty() &&
                    entry.callable_contract_type != nullptr &&
                    entry.typed_entry.type() ==
                        *entry.callable_contract_type &&
                    entry.link_anchor != nullptr &&
                    exact_entries
                        .insert(entry.entry_id + "@" + entry.entry_version)
                        .second,
                "implementation entry is null or duplicated");
        if (entry.evaluator_history_witness.has_value()) {
            ++evaluator_history_witness_count;
            require(entry.entry_id ==
                        kCommittedMissionHistoryEvaluationIdentity.id,
                    "history witness attached to a non-evaluator entry");
        }
    }
    require(evaluator_history_witness_count == 1U,
            "terminal evaluator history witness is missing or duplicated");
    const auto find_implementation = [&](std::string_view id,
                                         std::string_view version)
        -> const gnc::model_sdk::StaticImplementationEntry& {
        const auto found = std::find_if(
            implementation.entries.begin(), implementation.entries.end(),
            [id, version](const auto& entry) {
                return entry.entry_id == id &&
                       entry.entry_version == version;
            });
        require(found != implementation.entries.end(),
                "typed implementation entry is missing");
        return *found;
    };
    const auto require_exact_callable =
        [&](const gnc::foundation::AlgorithmIdentity& identity,
            StaticEntryKind kind, auto expected, std::string_view message) {
            using Expected = decltype(expected);
            const auto& entry =
                find_implementation(identity.id, identity.version);
            const auto typed = std::any_cast<Expected>(&entry.typed_entry);
            require(entry.kind == kind && typed != nullptr &&
                        *typed == expected,
                    message);
        };

    for (const auto& model : package.models) {
        if (model.pure_query.has_value()) {
            const auto& prepare = find_implementation(
                model.preparation_algorithm_id,
                model.preparation_algorithm_version);
            const auto& query = find_implementation(
                model.pure_query->query_entry_id,
                model.pure_query->query_entry_version);
            require(prepare.kind == StaticEntryKind::Prepare &&
                        prepare.signature_id ==
                            gnc::model_sdk::canonical_prepare_signature(model) &&
                        prepare.call_shape_id ==
                            model.preparation_call_shape_id &&
                        query.kind == StaticEntryKind::PureQuery &&
                        query.signature_id ==
                            gnc::model_sdk::canonical_query_signature(model) &&
                        query.call_shape_id ==
                            model.pure_query->query_call_shape_id,
                    "PureQuery implementation signature changed");
        } else if (model.closure.has_value()) {
            const auto& prepare = find_implementation(
                model.preparation_algorithm_id,
                model.preparation_algorithm_version);
            const auto& closure = find_implementation(
                model.closure->closure_entry_id,
                model.closure->closure_entry_version);
            require(prepare.kind == StaticEntryKind::Prepare &&
                        prepare.signature_id ==
                            gnc::model_sdk::canonical_prepare_signature(model) &&
                        prepare.call_shape_id ==
                            model.preparation_call_shape_id &&
                        closure.kind == StaticEntryKind::Closure &&
                        closure.signature_id ==
                            gnc::model_sdk::canonical_closure_signature(model) &&
                        closure.call_shape_id ==
                            model.closure->closure_call_shape_id,
                    "Closure implementation signature changed");
        }
        if (!model.runtime_component.has_value()) {
            continue;
        }
        const auto& runtime = *model.runtime_component;
        const auto& definition_builder = find_implementation(
            runtime.definition_builder_id,
            runtime.definition_builder_version);
        require(definition_builder.kind ==
                    StaticEntryKind::DefinitionBuilder &&
                    definition_builder.signature_id ==
                        gnc::model_sdk::canonical_definition_builder_signature(
                            model) &&
                    definition_builder.call_shape_id ==
                        runtime.definition_builder_call_shape_id,
                "definition-builder implementation signature changed");
        const auto& factory = find_implementation(
            runtime.runtime_cell_factory_id,
            runtime.runtime_cell_factory_version);
        require(factory.kind == StaticEntryKind::RuntimeCellFactory &&
                    factory.signature_id ==
                        gnc::model_sdk::
                            canonical_runtime_cell_factory_signature(model) &&
                    factory.call_shape_id ==
                        runtime.runtime_cell_factory_call_shape_id,
                "runtime-cell factory implementation signature changed");
        std::string layout;
        if (runtime.state_owner.has_value()) {
            layout = runtime.state_owner->schema.layout_id;
            const auto& initial = find_implementation(
                runtime.state_owner->initial_state_builder_id,
                runtime.state_owner->initial_state_builder_version);
            require(initial.kind == StaticEntryKind::InitialState &&
                        initial.signature_id ==
                            gnc::model_sdk::canonical_initial_state_signature(
                                model) &&
                        initial.call_shape_id ==
                            runtime.state_owner
                                ->initial_state_builder_call_shape_id &&
                        initial.state_layout_id == layout,
                    "initial-state implementation signature changed");
        }
        for (const auto& obligation : runtime.obligation_entries) {
            const auto& entry = find_implementation(
                obligation.entry_id, obligation.entry_version);
            const bool reads_or_writes_state =
                obligation.state_read != StaticStateReadKind::None ||
                obligation.state_write != StaticStateWriteKind::None;
            require(entry.kind == entry_kind(obligation.obligation) &&
                        entry.signature_id ==
                            gnc::model_sdk::canonical_runtime_entry_signature(
                                model, obligation) &&
                        entry.call_shape_id == obligation.call_shape_id &&
                        entry.state_layout_id ==
                            (reads_or_writes_state ? layout : std::string{}),
                    "runtime implementation signature changed");
        }
    }

    const auto& boundary_implementation = find_implementation(
        kControlledRigidBoundaryEvaluationIdentity.id,
        kControlledRigidBoundaryEvaluationIdentity.version);
    const auto boundary_entry =
        std::any_cast<ControlledRigidBoundaryRuntimeCall>(
            &boundary_implementation.typed_entry);
    const auto& evaluator_implementation = find_implementation(
        kCommittedMissionHistoryEvaluationIdentity.id,
        kCommittedMissionHistoryEvaluationIdentity.version);
    const auto evaluator_entry =
        std::any_cast<CommittedMissionHistoryEvaluationCall>(
        &evaluator_implementation.typed_entry);
    const auto& evaluator_history =
        evaluator_implementation.evaluator_history_witness;
    require(boundary_entry != nullptr &&
                *boundary_entry ==
                    &ControlledRigidBoundaryEvaluationKernel::
                        prepare_for_integration &&
                evaluator_entry != nullptr &&
                *evaluator_entry ==
                    &CommittedMissionHistoryEvaluationKernel::evaluate &&
                evaluator_history.has_value() &&
                evaluator_history->request_contract_id ==
                    kCommittedRigidMassSequenceContractIdentity &&
                evaluator_history->depth ==
                    kCommittedMissionHistoryDepth &&
                evaluator_history->ordered_members.size() == 2U &&
                evaluator_history->ordered_members[0U].member_id ==
                    kCommittedMissionRigidHistoryMemberId &&
                evaluator_history->ordered_members[0U].state_schema_id ==
                    kRigidStateSchemaIdentity &&
                evaluator_history->ordered_members[0U].state_layout_id ==
                    kRigidStateLayoutIdentity &&
                evaluator_history->ordered_members[1U].member_id ==
                    kCommittedMissionMassHistoryMemberId &&
                evaluator_history->ordered_members[1U].state_schema_id ==
                    kMassStateSchemaIdentity &&
                evaluator_history->ordered_members[1U].state_layout_id ==
                    kMassStateLayoutIdentity,
            "image-facing boundary or evaluator typed entry changed");

    require_exact_callable(
        kControlledRigidRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_controlled_rigid_runtime_cell,
        "controlled-rigid runtime-cell factory changed");
    require_exact_callable(
        kScalarBurnMassRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_scalar_burn_mass_runtime_cell,
        "mass runtime-cell factory changed");
    require_exact_callable(
        kAltitudePitchGuidanceRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_altitude_pitch_guidance_runtime_cell,
        "guidance runtime-cell factory changed");
    require_exact_callable(
        kPitchMomentControllerRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_pitch_moment_controller_runtime_cell,
        "controller runtime-cell factory changed");
    require_exact_callable(
        kIdealBodyMomentActuatorRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_ideal_body_moment_actuator_runtime_cell,
        "actuator runtime-cell factory changed");
    require_exact_callable(
        kFixedSuppliedPropulsionRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_fixed_supplied_propulsion_runtime_cell,
        "propulsion runtime-cell factory changed");
    require_exact_callable(
        kCommittedMissionResultRuntimeCellFactoryIdentity,
        StaticEntryKind::RuntimeCellFactory,
        &create_committed_mission_result_runtime_cell,
        "evaluator runtime-cell factory changed");

    const auto rigid_layout = std::find_if(
        implementation.state_layouts.begin(),
        implementation.state_layouts.end(), [](const auto& layout) {
            return layout.layout_id == kRigidStateLayoutIdentity;
        });
    const auto mass_layout = std::find_if(
        implementation.state_layouts.begin(),
        implementation.state_layouts.end(), [](const auto& layout) {
            return layout.layout_id == kMassStateLayoutIdentity;
        });
    require(rigid_layout != implementation.state_layouts.end() &&
                rigid_layout->size_bytes == sizeof(RigidState) &&
                rigid_layout->alignment_bytes == alignof(RigidState) &&
                mass_layout != implementation.state_layouts.end() &&
                mass_layout->size_bytes == sizeof(MassState) &&
                mass_layout->alignment_bytes == alignof(MassState),
            "process-local state layout facts changed");

    std::set<std::string> exact_value_layouts;
    for (const auto& layout : implementation.value_layouts) {
        require(!layout.contract_id.empty() && !layout.layout_id.empty() &&
                    layout.size_bytes > 0U &&
                    layout.alignment_bytes > 0U &&
                    exact_value_layouts.insert(layout.contract_id).second,
                "value layout is invalid or duplicated");
    }
    const auto require_value_layout =
        [&](std::string_view contract_id, std::size_t size_bytes,
            std::size_t alignment_bytes) {
            const auto found = std::find_if(
                implementation.value_layouts.begin(),
                implementation.value_layouts.end(),
                [contract_id](const auto& layout) {
                    return layout.contract_id == contract_id;
                });
            require(found != implementation.value_layouts.end() &&
                        found->size_bytes == size_bytes &&
                        found->alignment_bytes == alignment_bytes,
                    "process-local value layout fact changed");
        };
    require_value_layout(kRigidFormInputContractIdentity,
                         sizeof(RigidFormInput), alignof(RigidFormInput));
    require_value_layout(kMassPropertiesContractIdentity,
                         sizeof(MassPropertiesInput),
                         alignof(MassPropertiesInput));
    require_value_layout(kSuppliedPropulsionBodyWrenchContractIdentity,
                         sizeof(SuppliedPropulsionBodyWrench),
                         alignof(SuppliedPropulsionBodyWrench));
    require_value_layout(kIdealBodyMomentActuatorOutputContractIdentity,
                         sizeof(IdealBodyMomentActuatorOutput),
                         alignof(IdealBodyMomentActuatorOutput));
    require_value_layout(kRigidObservationContractIdentity,
                         sizeof(CommittedRigidObservation),
                         alignof(CommittedRigidObservation));
    require_value_layout(kMassFlowIntervalContractIdentity,
                         sizeof(MassFlowIntervalInput),
                         alignof(MassFlowIntervalInput));
    require_value_layout(kAltitudePitchGuidanceOutputContractIdentity,
                         sizeof(AltitudePitchGuidanceOutput),
                         alignof(AltitudePitchGuidanceOutput));
    require_value_layout(kPitchMomentControllerOutputContractIdentity,
                         sizeof(PitchMomentControllerOutput),
                         alignof(PitchMomentControllerOutput));
    require_value_layout(kCommittedMissionResultContractIdentity,
                         sizeof(CommittedMissionResultOutput),
                         alignof(CommittedMissionResultOutput));
    require_value_layout(kControlledRigidBoundaryPreparationContractIdentity,
                         sizeof(ControlledRigidBoundaryPreparationOutput),
                         alignof(ControlledRigidBoundaryPreparationOutput));
}

void verify_uniform_environment() {
    const UniformEnvironmentDefinition definition =
        environment_definition();
    const auto configuration = canonical_uniform_environment_config(definition);
    const auto rebuilt = build_uniform_environment_definition(configuration);
    require(rebuilt.has_value() &&
                canonical_uniform_environment_config(rebuilt.value()) ==
                    configuration,
            "uniform environment config did not round-trip");
    const auto prepared = prepare_uniform_environment_model(rebuilt.value());
    require(prepared.has_value(), "uniform environment did not prepare");
    UniformEnvironmentQueryInput request;
    request.context = sample_context(kInertialFrame);
    request.position.value = Vec3{1.0, 2.0, 3.0};
    const auto response = UniformEnvironmentQueryKernel::evaluate(
        prepared.value(), request);
    require(response.has_value() &&
                exactly(response.value().output.gravity.value,
                        definition.gravity.value) &&
                exactly(response.value().output.velocity_airmass.value,
                        definition.velocity_airmass.value) &&
                response.value().output.density_kilograms_per_cubic_meter ==
                    definition.density_kilograms_per_cubic_meter &&
                response.value().output.speed_of_sound_meters_per_second ==
                    definition.speed_of_sound_meters_per_second,
            "uniform environment response changed");
}

void verify_rigid_extractions() {
    const auto prepared = prepare_rigid_step_model(rigid_definition());
    require(prepared.has_value(), "rigid fixture did not prepare");
    const RigidStepInput input = rigid_input();
    const auto compatibility =
        RigidStepKernel::evaluate(prepared.value(), input);
    const auto frozen =
        RigidFrozenFormKernel::evaluate(prepared.value(), input);
    require(compatibility.has_value() && frozen.has_value(),
            "rigid compatibility or frozen-form entry failed");
    const RigidFrozenFormRuntimeDefinition runtime_definition{
        prepared.value().definition().inertial_frame,
        prepared.value().definition().algorithm};
    RigidFrozenFormInvocationSet invocations;
    invocations.aerodynamic = {
        1U, 1U, 1U,
        &prepared.value().aerodynamic_table_model(),
        &counting_aerodynamic_query};
    invocations.closure = {
        2U, 2U, 2U,
        &prepared.value().force_moment_closure_model(),
        &counting_force_moment_closure};
    aerodynamic_query_calls = 0U;
    force_moment_closure_calls = 0U;
    const auto invoked_full =
        RigidFrozenFormKernel::evaluate_with_invocation_results(
            runtime_definition, invocations, input);
    require(invoked_full.has_value(),
            "invocation-result frozen form failed");
    const auto& invoked = invoked_full.value().evaluation;
    require(invoked_full.status() == frozen.status() &&
                invoked_full.evidence().flags == frozen.evidence().flags &&
                invoked_full.evidence().evaluations ==
                    frozen.evidence().evaluations &&
                invoked_full.evidence().algorithm.id ==
                    frozen.evidence().algorithm.id &&
                invoked_full.evidence().algorithm.version ==
                    frozen.evidence().algorithm.version &&
                invoked_full.evidence().detail == frozen.evidence().detail &&
                exactly(invoked.output.form_input.force_total.value,
                        frozen.value().output.form_input.force_total.value) &&
                exactly(invoked.output.form_input
                            .moment_total_about_center_of_mass.value,
                        frozen.value()
                            .output.form_input
                            .moment_total_about_center_of_mass.value) &&
                invoked.telemetry.aerodynamic_query.output
                        .coefficients_ca_cy_cn_cl_cm_cn ==
                    frozen.value()
                        .telemetry.aerodynamic_query.output
                        .coefficients_ca_cy_cn_cl_cm_cn &&
                invoked_full.value()
                        .invocation_results.aerodynamic_coefficients
                        .coefficients_ca_cy_cn_cl_cm_cn ==
                    invoked.telemetry.aerodynamic_query.output
                        .coefficients_ca_cy_cn_cl_cm_cn &&
                aerodynamic_query_calls == 1U &&
                force_moment_closure_calls == 1U,
            "invocation-set frozen form changed the compatibility result");
    RigidStepInput invalid_input = input;
    invalid_input.mass_properties.mass_kilograms = -1.0;
    const auto compatibility_failure =
        RigidFrozenFormKernel::evaluate(prepared.value(), invalid_input);
    aerodynamic_query_calls = 0U;
    force_moment_closure_calls = 0U;
    const auto invoked_failure =
        RigidFrozenFormKernel::evaluate_with_invocation_results(
            runtime_definition, invocations, invalid_input);
    require(!compatibility_failure.has_value() &&
                !invoked_failure.has_value() &&
                invoked_failure.status() == compatibility_failure.status() &&
                invoked_failure.evidence().flags ==
                    compatibility_failure.evidence().flags &&
                invoked_failure.evidence().algorithm.id ==
                    compatibility_failure.evidence().algorithm.id &&
                invoked_failure.evidence().algorithm.version ==
                    compatibility_failure.evidence().algorithm.version &&
                invoked_failure.evidence().detail ==
                    compatibility_failure.evidence().detail &&
                aerodynamic_query_calls == 0U &&
                force_moment_closure_calls == 0U,
            "invocation-set frozen form changed failure identity or detail");
    const auto held = RigidStepKernel::evaluate_held_form(
        prepared.value(), input, frozen.value(), frozen.status(),
        frozen.evidence());
    require(held.has_value() && held.status() == compatibility.status() &&
                held.evidence().flags == compatibility.evidence().flags &&
                held.evidence().evaluations ==
                    compatibility.evidence().evaluations &&
                exactly(held.value().output.candidate.state,
                        compatibility.value().output.candidate.state) &&
                exactly(held.value().telemetry.air_data.velocity_relative_body
                            .value,
                        compatibility.value()
                            .telemetry.air_data.velocity_relative_body.value) &&
                exactly(held.value().telemetry.force_moment_closure.output
                            .force_total.value,
                        compatibility.value()
                            .telemetry.force_moment_closure.output.force_total
                            .value),
            "held-form extraction changed the compatibility result");

    const auto derivative = RigidDerivativeKernel::evaluate(
        prepared.value().definition().algorithm,
        RigidDerivativeInput{
            input.committed_state, input.mass_properties.mass_kilograms,
            input.mass_properties.inertia_about_center_of_mass,
            frozen.value().output.form_input, input.environment.gravity});
    const auto& compatibility_derivative =
        compatibility.value().telemetry.derivative_at_interval_start;
    require(derivative.has_value() &&
                exactly(derivative.value().acceleration.value,
                        compatibility_derivative.acceleration.value) &&
                exactly(derivative.value().angular_acceleration.value,
                        compatibility_derivative.angular_acceleration.value) &&
                exactly(derivative.value().attitude_derivative.value,
                        compatibility_derivative.attitude_derivative.value),
            "pure derivative entry changed the interval-start derivative");

    RigidInitialStateInput initial{input.committed_state};
    const auto built = RigidInitialStateBuilder::build(
        prepared.value().definition().algorithm, initial);
    const auto observation = project_committed_rigid_observation(
        sample_context(kInertialFrame), input.committed_state);
    require(built.has_value() && exactly(built.value(), input.committed_state) &&
                exactly(observation.state, input.committed_state) &&
                exactly(observation.context,
                        sample_context(kInertialFrame)),
            "rigid initial builder or publish projection changed");
}

void verify_controlled_boundary() {
    const auto prepared = prepare_rigid_step_model(rigid_definition());
    require(prepared.has_value(), "controlled rigid fixture did not prepare");
    const RigidStepInput input = rigid_input();
    SuppliedPropulsionBodyWrench propulsion;
    propulsion.context = interval_context(kBodyFrame);
    propulsion.source_id = "propulsion.main";
    propulsion.force.value = input.supplied_wrench.force.value;
    propulsion.center_of_mass_to_application.value = Vec3{0.0, 0.2, 0.0};
    propulsion.intrinsic_moment_at_application.value = Vec3::Zero();
    IdealBodyMomentActuatorOutput actuator;
    actuator.context = interval_context(kBodyFrame);
    actuator.source_id = "actuator.pitch";
    actuator.moment_about_center_of_mass.value = Vec3{0.0, 0.0, 20.0};
    const ControlledRigidBoundaryEvaluationDefinition definition{
        {prepared.value().definition().inertial_frame,
         prepared.value().definition().algorithm},
        {input.supplied_wrench.source_id}};
    const auto configuration =
        canonical_controlled_rigid_boundary_config(definition);
    const auto rebuilt =
        build_controlled_rigid_boundary_definition(configuration);
    require(rebuilt.has_value() &&
                canonical_controlled_rigid_boundary_config(rebuilt.value()) ==
                    configuration,
            "controlled rigid runtime config did not round-trip");
    require_rejects_truncated_definition(
        configuration, &build_controlled_rigid_boundary_definition,
        kControlledRigidDefinitionBuilderIdentity,
        "controlled rigid builder accepted an incomplete config");
    const auto adapted = ControlledBodyWrenchAdapterKernel::evaluate(
        definition.wrench_adapter,
        {input.mass_properties.body_origin_to_center_of_mass, propulsion,
         actuator});
    require(adapted.has_value() &&
                exactly(adapted.value().force.value,
                        input.supplied_wrench.force.value) &&
                exactly(adapted.value().body_origin_to_application.value,
                        input.supplied_wrench.body_origin_to_application.value) &&
                exactly(adapted.value().intrinsic_moment_at_application.value,
                        input.supplied_wrench.intrinsic_moment_at_application
                            .value),
            "controlled-wrench adapter changed existing arithmetic");

    const auto direct_frozen =
        RigidFrozenFormKernel::evaluate(prepared.value(), input);
    RigidFrozenFormInvocationSet frozen_invocations;
    frozen_invocations.aerodynamic = {
        1U, 1U, 1U,
        &prepared.value().aerodynamic_table_model(),
        &AerodynamicTableQueryKernel::evaluate};
    frozen_invocations.closure = {
        2U, 2U, 2U,
        &prepared.value().force_moment_closure_model(),
        &ForceMomentClosureKernel::evaluate};
    const auto resolved = ControlledRigidBoundaryEvaluationKernel::
        evaluate_resolved_environment(
            rebuilt.value(), frozen_invocations,
            {input.context, input.committed_state, input.environment,
             input.mass_properties, propulsion, actuator});
    const auto prepared_environment =
        prepare_uniform_environment_model(environment_definition());
    require(prepared_environment.has_value(),
            "controlled environment provider did not prepare");
    environment_query_calls = 0U;
    aerodynamic_query_calls = 0U;
    force_moment_closure_calls = 0U;
    ControlledRigidBoundaryInvocationSet counted_invocations;
    counted_invocations.environment = {
        1U, 1U, 1U, &prepared_environment.value(),
        &counting_environment_query};
    counted_invocations.frozen_form.aerodynamic = {
        2U, 2U, 2U,
        &prepared.value().aerodynamic_table_model(),
        &counting_aerodynamic_query};
    counted_invocations.frozen_form.closure = {
        3U, 3U, 3U,
        &prepared.value().force_moment_closure_model(),
        &counting_force_moment_closure};
    const auto combined = ControlledRigidBoundaryEvaluationKernel::
        evaluate_with_invocation_results(
        rebuilt.value(),
        counted_invocations,
        {input.context, input.committed_state, input.mass_properties,
         propulsion, actuator});
    ControlledRigidBoundaryInvocationSet compatibility_invocations;
    compatibility_invocations.environment = {
        1U, 1U, 1U, &prepared_environment.value(),
        &UniformEnvironmentQueryKernel::evaluate};
    compatibility_invocations.frozen_form = frozen_invocations;
    const auto compatibility_boundary =
        ControlledRigidBoundaryEvaluationKernel::evaluate(
            rebuilt.value(), compatibility_invocations,
            {input.context, input.committed_state, input.mass_properties,
             propulsion, actuator});
    require(direct_frozen.has_value() && resolved.has_value() &&
                combined.has_value() && compatibility_boundary.has_value() &&
                environment_query_calls == 1U &&
                aerodynamic_query_calls == 1U &&
                force_moment_closure_calls == 1U &&
                exactly(combined.value().evaluation.telemetry.controlled_wrench
                            .force.value,
                        input.supplied_wrench.force.value) &&
                exactly(combined.value().evaluation.telemetry.environment_response
                            .context,
                        input.environment.context) &&
                exactly(combined.value().invocation_results
                            .environment_response
                            .gravity.value,
                        input.environment.gravity.value) &&
                exactly(combined.value().invocation_results
                            .environment_response
                            .velocity_airmass.value,
                        input.environment.velocity_airmass.value) &&
                exactly(combined.value().evaluation.output.force_total.value,
                        direct_frozen.value().output.form_input.force_total
                            .value) &&
                exactly(combined.value().evaluation.output
                            .moment_total_about_center_of_mass.value,
                        direct_frozen.value()
                            .output.form_input
                            .moment_total_about_center_of_mass.value) &&
                exactly(compatibility_boundary.value().output.force_total.value,
                        combined.value().evaluation.output.force_total.value) &&
                exactly(resolved.value().frozen_form.output.form_input
                            .force_total.value,
                        combined.value().evaluation.output.force_total.value) &&
                combined.value().invocation_results
                        .aerodynamic_coefficients
                        .coefficients_ca_cy_cn_cl_cm_cn ==
                    direct_frozen.value()
                        .telemetry.aerodynamic_query.output
                        .coefficients_ca_cy_cn_cl_cm_cn,
            "combined controlled boundary changed adapter/frozen-form order");
}

void verify_mass_and_fixed_propulsion() {
    MassState state;
    state.context = sample_context(kBodyFrame);
    state.mass_state_id = std::string(kMassStateId);
    state.mass_kilograms = 100.0;
    state.body_origin_to_center_of_mass.value = Vec3{0.2, 0.0, 0.0};
    state.inertia_about_center_of_mass.value = Mat3::Zero();
    state.inertia_about_center_of_mass.value.diagonal() =
        Vec3{10.0, 20.0, 30.0};
    const ScalarBurnMassDefinition mass_definition{
        std::string(kScalarBurnMassModelIdentity),
        std::string(kScalarBurnMassModelVersion), std::string(kMassStateId),
        numerical_policy()};
    const auto mass_configuration =
        canonical_scalar_burn_mass_config(mass_definition);
    const auto rebuilt_mass =
        build_scalar_burn_mass_definition(mass_configuration);
    require(rebuilt_mass.has_value() &&
                canonical_scalar_burn_mass_config(rebuilt_mass.value()) ==
                    mass_configuration,
            "mass runtime config did not round-trip");
    require_rejects_truncated_definition(
        mass_configuration, &build_scalar_burn_mass_definition,
        kScalarBurnMassDefinitionBuilderIdentity,
        "mass builder accepted an incomplete config");
    auto invalid_mass_configuration = mass_configuration;
    invalid_mass_configuration.fields[1U].value = -1.0;
    require(!build_scalar_burn_mass_definition(
                 invalid_mass_configuration)
                 .has_value(),
            "mass builder accepted an invalid numerical policy");
    const auto built = build_scalar_burn_mass_initial_state(
        rebuilt_mass.value(), MassInitialStateInput{state});
    const auto projection = project_committed_mass_properties(
        interval_context(kBodyFrame), state);
    require(built.has_value() && built.value().mass_kilograms == 100.0 &&
                exactly(built.value().inertia_about_center_of_mass.value,
                        state.inertia_about_center_of_mass.value) &&
                projection.mass_kilograms == state.mass_kilograms &&
                exactly(projection.body_origin_to_center_of_mass.value,
                        state.body_origin_to_center_of_mass.value),
            "mass initial builder or publish projection changed");

    MassFlowIntervalInput mass_flow;
    mass_flow.context = interval_context(kBodyFrame);
    mass_flow.mass_state_id = std::string(kMassStateId);
    mass_flow.fuel_consumption_rate_kilograms_per_second = 0.5;
    const auto original_mass_evolution = ScalarBurnMassKernel::evaluate(
        mass_definition, state, mass_flow, numerical_policy());
    const auto rebuilt_mass_evolution = evaluate_scalar_burn_mass_interval(
        rebuilt_mass.value(), state, mass_flow);
    require(original_mass_evolution.has_value() &&
                rebuilt_mass_evolution.has_value() &&
                rebuilt_mass_evolution.status() ==
                    original_mass_evolution.status() &&
                rebuilt_mass_evolution.evidence().flags ==
                    original_mass_evolution.evidence().flags &&
                rebuilt_mass_evolution.value().current_committed_mass_kilograms ==
                    original_mass_evolution.value()
                        .current_committed_mass_kilograms &&
                rebuilt_mass_evolution.value().integration_mass_kilograms ==
                    original_mass_evolution.value()
                        .integration_mass_kilograms &&
                rebuilt_mass_evolution.value().consumed_mass_kilograms ==
                    original_mass_evolution.value()
                        .consumed_mass_kilograms &&
                rebuilt_mass_evolution.value().candidate.state
                        .mass_kilograms ==
                    original_mass_evolution.value().candidate.state
                        .mass_kilograms,
            "mass definition builder changed interval evolution");

    SuppliedPropulsionDefinition supplied;
    supplied.model_id = std::string(kSuppliedPropulsionModelIdentity);
    supplied.model_version = std::string(kSuppliedPropulsionModelVersion);
    supplied.source_id = "propulsion.main";
    supplied.body_frame = FrameIdentity{std::string(kBodyFrame)};
    supplied.clock_domain = ClockDomainIdentity{std::string(kClock)};
    supplied.mass_state_id = std::string(kMassStateId);
    supplied.numerical_policy = numerical_policy();
    SuppliedPropulsionInput request;
    request.context = interval_context(kBodyFrame);
    request.thrust_magnitude_newtons = 100.0;
    request.thrust_direction.value = Vec3{1.0, 0.0, 0.0};
    request.center_of_mass_to_application.value = Vec3{0.0, 0.2, 0.0};
    request.intrinsic_moment_at_application.value = Vec3{1.0, 2.0, 3.0};
    request.fuel_consumption_rate_kilograms_per_second = 0.5;
    const FixedSuppliedPropulsionDefinition fixed{
        supplied, request.thrust_magnitude_newtons,
        request.thrust_direction, request.center_of_mass_to_application,
        request.intrinsic_moment_at_application,
        request.fuel_consumption_rate_kilograms_per_second};
    const auto propulsion_configuration =
        canonical_fixed_supplied_propulsion_config(fixed);
    const auto rebuilt_propulsion =
        build_fixed_supplied_propulsion_definition(
            propulsion_configuration);
    require(rebuilt_propulsion.has_value() &&
                canonical_fixed_supplied_propulsion_config(
                    rebuilt_propulsion.value()) ==
                    propulsion_configuration,
            "fixed propulsion runtime config did not round-trip");
    require_rejects_truncated_definition(
        propulsion_configuration,
        &build_fixed_supplied_propulsion_definition,
        kFixedSuppliedPropulsionDefinitionBuilderIdentity,
        "fixed propulsion builder accepted an incomplete config");
    const auto direct = SuppliedPropulsionKernel::evaluate(supplied, request);
    const auto fixed_result = FixedSuppliedPropulsionBoundaryKernel::evaluate(
        rebuilt_propulsion.value(), request.context);
    require(direct.has_value() && fixed_result.has_value() &&
                fixed_result.status() == direct.status() &&
                fixed_result.evidence().flags == direct.evidence().flags &&
                fixed_result.evidence().evaluations ==
                    direct.evidence().evaluations &&
                exactly(fixed_result.value().supplied_body_wrench.force.value,
                        direct.value().supplied_body_wrench.force.value) &&
                exactly(fixed_result.value().lever_arm_moment.value,
                        direct.value().lever_arm_moment.value) &&
                exactly(fixed_result.value().moment_about_center_of_mass.value,
                        direct.value().moment_about_center_of_mass.value) &&
                fixed_result.value().mass_flow
                        .fuel_consumption_rate_kilograms_per_second ==
                    direct.value().mass_flow
                        .fuel_consumption_rate_kilograms_per_second,
            "config-driven fixed propulsion changed supplied-kernel output");
}

void verify_guidance_control_actuation_definition_builders() {
    AltitudePitchGuidanceDefinition guidance_definition;
    guidance_definition.model_id =
        std::string(kAltitudePitchGuidanceModelIdentity);
    guidance_definition.model_version =
        std::string(kAltitudePitchGuidanceModelVersion);
    guidance_definition.inertial_frame =
        FrameIdentity{std::string(kInertialFrame)};
    guidance_definition.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    guidance_definition.configuration_revision = 11;
    guidance_definition.target_altitude_meters = 1100.0;
    guidance_definition.altitude_error_gain_radians_per_meter = 0.001;
    guidance_definition.vertical_speed_gain_radian_seconds_per_meter = 0.01;
    guidance_definition.pitch_command_limit_radians = 0.5;
    guidance_definition.attitude_policy = quaternion_policy();
    const auto guidance_configuration =
        canonical_altitude_pitch_guidance_config(guidance_definition);
    const auto rebuilt_guidance =
        build_altitude_pitch_guidance_definition(guidance_configuration);
    require(rebuilt_guidance.has_value() &&
                canonical_altitude_pitch_guidance_config(
                    rebuilt_guidance.value()) == guidance_configuration,
            "guidance runtime config did not round-trip");
    require_rejects_truncated_definition(
        guidance_configuration,
        &build_altitude_pitch_guidance_definition,
        kAltitudePitchGuidanceDefinitionBuilderIdentity,
        "guidance builder accepted an incomplete config");

    const auto observation = project_committed_rigid_observation(
        sample_context(kInertialFrame), rigid_input().committed_state);
    const auto original_guidance = AltitudePitchGuidanceKernel::evaluate(
        guidance_definition, observation);
    const auto rebuilt_guidance_output =
        AltitudePitchGuidanceKernel::evaluate(rebuilt_guidance.value(),
                                              observation);
    require(original_guidance.has_value() &&
                rebuilt_guidance_output.has_value() &&
                rebuilt_guidance_output.status() ==
                    original_guidance.status() &&
                rebuilt_guidance_output.evidence().flags ==
                    original_guidance.evidence().flags &&
                rebuilt_guidance_output.value().measured_pitch_radians ==
                    original_guidance.value().measured_pitch_radians &&
                rebuilt_guidance_output.value().pitch_command_radians ==
                    original_guidance.value().pitch_command_radians &&
                rebuilt_guidance_output.value().saturated ==
                    original_guidance.value().saturated,
            "guidance definition builder changed kernel output");

    PitchMomentControllerDefinition controller_definition;
    controller_definition.model_id =
        std::string(kPitchMomentControllerModelIdentity);
    controller_definition.model_version =
        std::string(kPitchMomentControllerModelVersion);
    controller_definition.body_frame =
        FrameIdentity{std::string(kBodyFrame)};
    controller_definition.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    controller_definition.configuration_revision = 11;
    controller_definition.pitch_error_gain_newton_meters_per_radian = 50.0;
    controller_definition.pitch_rate_gain_newton_meter_seconds_per_radian =
        5.0;
    controller_definition.moment_command_limit_newton_meters = 100.0;
    controller_definition.numerical_policy = numerical_policy();
    const auto controller_configuration =
        canonical_pitch_moment_controller_config(controller_definition);
    const auto rebuilt_controller =
        build_pitch_moment_controller_definition(controller_configuration);
    require(rebuilt_controller.has_value() &&
                canonical_pitch_moment_controller_config(
                    rebuilt_controller.value()) == controller_configuration,
            "controller runtime config did not round-trip");
    require_rejects_truncated_definition(
        controller_configuration,
        &build_pitch_moment_controller_definition,
        kPitchMomentControllerDefinitionBuilderIdentity,
        "controller builder accepted an incomplete config");
    const auto original_controller = PitchMomentControllerKernel::evaluate(
        controller_definition, original_guidance.value());
    const auto rebuilt_controller_output =
        PitchMomentControllerKernel::evaluate(
            rebuilt_controller.value(), rebuilt_guidance_output.value());
    require(original_controller.has_value() &&
                rebuilt_controller_output.has_value() &&
                rebuilt_controller_output.status() ==
                    original_controller.status() &&
                rebuilt_controller_output.evidence().flags ==
                    original_controller.evidence().flags &&
                rebuilt_controller_output.value()
                        .moment_command_newton_meters ==
                    original_controller.value()
                        .moment_command_newton_meters &&
                rebuilt_controller_output.value().saturated ==
                    original_controller.value().saturated,
            "controller definition builder changed kernel output");

    IdealBodyMomentActuatorDefinition actuator_definition;
    actuator_definition.model_id =
        std::string(kIdealBodyMomentActuatorModelIdentity);
    actuator_definition.model_version =
        std::string(kIdealBodyMomentActuatorModelVersion);
    actuator_definition.source_id = "actuator.pitch";
    actuator_definition.body_frame = FrameIdentity{std::string(kBodyFrame)};
    actuator_definition.clock_domain =
        ClockDomainIdentity{std::string(kClock)};
    actuator_definition.configuration_revision = 11;
    actuator_definition.realization_gain = 1.0;
    actuator_definition.numerical_policy = numerical_policy();
    const auto actuator_configuration =
        canonical_ideal_body_moment_actuator_config(actuator_definition);
    const auto rebuilt_actuator =
        build_ideal_body_moment_actuator_definition(actuator_configuration);
    require(rebuilt_actuator.has_value() &&
                canonical_ideal_body_moment_actuator_config(
                    rebuilt_actuator.value()) == actuator_configuration,
            "actuator runtime config did not round-trip");
    require_rejects_truncated_definition(
        actuator_configuration,
        &build_ideal_body_moment_actuator_definition,
        kIdealBodyMomentActuatorDefinitionBuilderIdentity,
        "actuator builder accepted an incomplete config");
    const auto context = interval_context(kBodyFrame);
    const auto original_actuator = IdealBodyMomentActuatorKernel::evaluate(
        actuator_definition, context, original_controller.value());
    const auto rebuilt_actuator_output =
        IdealBodyMomentActuatorKernel::evaluate(
            rebuilt_actuator.value(), context,
            rebuilt_controller_output.value());
    require(original_actuator.has_value() &&
                rebuilt_actuator_output.has_value() &&
                rebuilt_actuator_output.status() ==
                    original_actuator.status() &&
                rebuilt_actuator_output.evidence().flags ==
                    original_actuator.evidence().flags &&
                rebuilt_actuator_output.value().source_id ==
                    original_actuator.value().source_id &&
                exactly(rebuilt_actuator_output.value()
                            .moment_about_center_of_mass.value,
                        original_actuator.value()
                            .moment_about_center_of_mass.value),
            "actuator definition builder changed kernel output");
}

void verify_committed_history_evaluator() {
    CommittedMissionResultDefinition definition;
    definition.model_id =
        std::string(kCommittedMissionResultModelIdentity);
    definition.model_version =
        std::string(kCommittedMissionResultModelVersion);
    definition.subject = "vehicle.fixture.yyz@1";
    definition.inertial_frame =
        FrameIdentity{std::string(kInertialFrame)};
    definition.body_frame = FrameIdentity{std::string(kBodyFrame)};
    definition.clock_domain = ClockDomainIdentity{std::string(kClock)};
    definition.mass_state_id = std::string(kMassStateId);
    definition.configuration_revision = 11;
    definition.numerical_policy = numerical_policy();
    definition.predicates = {{
        {"downrange-goal", MissionMetric::DownrangeMeters,
         MissionRelation::GreaterThanOrEqual, 20.0,
         MissionAction::Complete, "downrange-goal", 200},
        {"duration-limit", MissionMetric::DurationSeconds,
         MissionRelation::GreaterThanOrEqual, 0.2,
         MissionAction::Complete, "duration-complete", 100},
        {"remaining-mass-floor",
         MissionMetric::RemainingMassKilograms,
         MissionRelation::LessThanOrEqual, 98.0,
         MissionAction::Abort, "remaining-mass-floor", 300},
    }};
    const auto evaluator_configuration =
        canonical_committed_mission_result_config(definition);
    const auto rebuilt_definition =
        build_committed_mission_result_definition(
            evaluator_configuration);
    require(rebuilt_definition.has_value() &&
                canonical_committed_mission_result_config(
                    rebuilt_definition.value()) ==
                    evaluator_configuration,
            "evaluator runtime config did not round-trip");
    require_rejects_truncated_definition(
        evaluator_configuration,
        &build_committed_mission_result_definition,
        kCommittedMissionResultDefinitionBuilderIdentity,
        "evaluator builder accepted an incomplete config");

    CommittedMissionStateHistoryInput history;
    CommittedMissionResultInput assembled;
    for (std::size_t index = 0U;
         index < kCommittedMissionHistoryDepth; ++index) {
        const auto tick = static_cast<std::int64_t>(index);
        const double seconds = 0.1 * static_cast<double>(index);
        RigidState rigid;
        rigid.position.value =
            Vec3{index == 2U
                     ? 21.0
                     : 10.0 * static_cast<double>(index),
                 0.0, 1000.0};
        rigid.velocity.value =
            Vec3{110.0 - 5.0 * static_cast<double>(index), 0.0, 0.0};
        rigid.attitude.value =
            gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
        rigid.angular_rate.value = Vec3::Zero();

        MassState mass;
        mass.context = {
            FrameIdentity{std::string(kBodyFrame)},
            ClockDomainIdentity{std::string(kClock)},
            SimulationInstant{tick, seconds}, 11, DataQuality::Valid};
        mass.mass_state_id = std::string(kMassStateId);
        mass.mass_kilograms =
            100.0 - 0.05 * static_cast<double>(index);
        mass.body_origin_to_center_of_mass.value = Vec3{0.2, 0.0, 0.0};
        mass.inertia_about_center_of_mass.value = Mat3::Zero();
        mass.inertia_about_center_of_mass.value.diagonal() =
            Vec3{10.0, 20.0, 30.0};

        history.rigid_states[index] = rigid;
        history.mass_states[index] = mass;
        assembled.committed_samples[index] = {
            SampleContext{
                definition.inertial_frame, definition.clock_domain,
                mass.context.sample_time, definition.configuration_revision,
                mass.context.quality},
            rigid, mass};
    }

    const auto direct =
        CommittedMissionResultKernel::evaluate(definition, assembled);
    const auto adapted =
        CommittedMissionHistoryEvaluationKernel::evaluate(
            rebuilt_definition.value(), history);
    require(direct.has_value() && adapted.has_value() &&
                adapted.status() == direct.status() &&
                adapted.evidence().flags == direct.evidence().flags &&
                adapted.evidence().evaluations ==
                    direct.evidence().evaluations &&
                adapted.evidence().algorithm.id ==
                    direct.evidence().algorithm.id &&
                adapted.evidence().algorithm.version ==
                    direct.evidence().algorithm.version &&
                adapted.evidence().detail == direct.evidence().detail &&
                adapted.evidence().last_step ==
                    direct.evidence().last_step &&
                adapted.value().status == direct.value().status &&
                adapted.value().initial_tick == direct.value().initial_tick &&
                adapted.value().final_tick == direct.value().final_tick &&
                adapted.value().final_time_seconds ==
                    direct.value().final_time_seconds &&
                adapted.value().termination.action ==
                    direct.value().termination.action &&
                adapted.value().termination.reason_code ==
                    direct.value().termination.reason_code &&
                adapted.value().termination.priority ==
                    direct.value().termination.priority &&
                adapted.value().metrics.evaluated_sample_count ==
                    direct.value().metrics.evaluated_sample_count &&
                adapted.value().metrics.terminal.downrange_meters ==
                    direct.value().metrics.terminal.downrange_meters &&
                adapted.value().terminal_predicates[0].predicate_id ==
                    direct.value().terminal_predicates[0].predicate_id &&
                adapted.value().terminal_predicates[0].met ==
                    direct.value().terminal_predicates[0].met &&
                exactly(adapted.value().terminal_boundary.rigid_state,
                        direct.value().terminal_boundary.rigid_state) &&
                adapted.value().terminal_boundary.mass_state
                        .mass_kilograms ==
                    direct.value().terminal_boundary.mass_state
                        .mass_kilograms &&
                exactly(adapted.value().terminal_boundary.rigid_context,
                        direct.value().terminal_boundary.rigid_context) &&
                exactly(adapted.value().terminal_boundary.mass_state.context,
                        direct.value().terminal_boundary.mass_state.context),
            "committed-history adapter changed evaluator input or result");
}

} // namespace

int main() {
    try {
        verify_catalog_and_descriptors();
        verify_implementation_table();
        verify_uniform_environment();
        verify_rigid_extractions();
        verify_controlled_boundary();
        verify_mass_and_fixed_propulsion();
        verify_guidance_control_actuation_definition_builders();
        verify_committed_history_evaluator();
        std::cout << "yyz-static-product-contracts: 8 checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "yyz-static-product-contracts: " << error.what()
                  << '\n';
        return 1;
    }
}
