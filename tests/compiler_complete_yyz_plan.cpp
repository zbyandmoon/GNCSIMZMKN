#include <gnc/compiler/complete_execution_plan.hpp>
#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <any>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gnc::compiler::CompleteDiagnosticCode;
using gnc::compiler::CompleteSourceBinding;
using gnc::compiler::CompleteSourceEvaluatorHistory;
using gnc::compiler::CompleteSourceInitialBinding;
using gnc::compiler::CompleteSourceIntegrationScope;
using gnc::compiler::CompleteSourceInvocationBinding;
using gnc::compiler::CompleteSourceOccurrence;
using gnc::compiler::CompleteSourceTransaction;
using gnc::compiler::CompleteStaticCompositionSource;
using gnc::compiler::ScopeKey;
using gnc::compiler::ScopeKind;
using gnc::compiler::SourceConfigFieldProvenance;
using gnc::compiler::SourceEntity;
using gnc::compiler::SourceRef;
using gnc::compiler::SourceScope;
using gnc::model_sdk::CanonicalConfigBlock;
using gnc::model_sdk::CanonicalConfigField;
using gnc::model_sdk::CanonicalConfigValue;
using gnc::model_sdk::CanonicalConfigValueKind;
using gnc::model_sdk::CanonicalEnumValue;
using gnc::model_sdk::ModelExecutionForm;
using gnc::model_sdk::RuntimeCellProfile;
using gnc::model_sdk::StaticInvocationKind;
using gnc::model_sdk::StaticModelDescriptor;
using gnc::model_sdk::StaticPackageDescriptor;
using gnc::model_sdk::StaticPackageImplementation;

constexpr std::string_view kEntity = "vehicle.fixture.yyz@1";
constexpr std::string_view kClock = "clock.fixture.yyz.simulation@1";
constexpr std::string_view kInertialFrame =
    "frame.fixture.yyz.inertial-cartesian@1";
constexpr std::string_view kBodyFrame = "frame.fixture.yyz.body@1";
constexpr std::string_view kMassState = "mass.fixture.yyz.vehicle@1";
constexpr std::string_view kDocument = "fixtures/ref-yyz-001/source.json";

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] SourceRef ref(std::string path) {
    return {std::string(kDocument), std::move(path)};
}

[[nodiscard]] std::string occurrence_id(std::size_t index) {
    return "occurrence.ref-yyz." + std::to_string(index);
}

[[nodiscard]] CanonicalConfigValue fixture_value(
    std::string_view model_id, std::string_view field_id,
    CanonicalConfigValueKind kind, bool initial_state) {
    switch (kind) {
    case CanonicalConfigValueKind::String:
        if (field_id.find("clock") != std::string_view::npos) {
            return std::string(kClock);
        }
        if (field_id.find("mass_state") != std::string_view::npos) {
            return std::string(kMassState);
        }
        if (field_id.find("inertial") != std::string_view::npos) {
            return std::string(kInertialFrame);
        }
        if (field_id.find("body_frame") != std::string_view::npos ||
            field_id == "context.frame_id") {
            return std::string(kBodyFrame);
        }
        if (field_id.find("configuration_id") != std::string_view::npos) {
            return std::string("configuration.fixture.yyz.clean@1");
        }
        if (field_id.find("subject") != std::string_view::npos) {
            return std::string("vehicle.fixture.yyz@1");
        }
        if (field_id == "predicates.0.predicate_id" ||
            field_id == "predicates.0.reason_code") {
            return std::string("remaining-mass-floor");
        }
        if (field_id == "predicates.1.predicate_id") {
            return std::string("duration-limit");
        }
        if (field_id == "predicates.1.reason_code") {
            return std::string("duration-complete");
        }
        if (field_id == "predicates.2.predicate_id" ||
            field_id == "predicates.2.reason_code") {
            return std::string("downrange-goal");
        }
        if (field_id == "combined_wrench_source_id") {
            return std::string("propulsion.main+actuation.pitch-moment");
        }
        if (field_id == "source_id" &&
            model_id ==
                gnc::packages::yyz::kIdealBodyMomentActuatorModelIdentity) {
            return std::string(
                "actuation.fixture.yyz.ideal-body-moment@1");
        }
        if (field_id == "source_id" &&
            model_id ==
                gnc::packages::yyz::kSuppliedPropulsionModelIdentity) {
            return std::string("propulsion.main");
        }
        throw std::runtime_error("unmapped REF-YYZ string field: " +
                                 std::string(field_id));
    case CanonicalConfigValueKind::Integer:
        if (field_id == "predicates.0.priority") {
            return std::int64_t{300};
        }
        if (field_id == "predicates.1.priority") {
            return std::int64_t{100};
        }
        if (field_id == "predicates.2.priority") {
            return std::int64_t{200};
        }
        if (field_id.find("sample_time.tick") != std::string_view::npos) {
            return std::int64_t{0};
        }
        if (field_id.find("configuration_revision") !=
            std::string_view::npos) {
            return std::int64_t{11};
        }
        throw std::runtime_error("unmapped REF-YYZ integer field: " +
                                 std::string(field_id));
    case CanonicalConfigValueKind::Enum:
        if (field_id.find("normalization") != std::string_view::npos) {
            return CanonicalEnumValue{"normalize-with-flag"};
        }
        if (field_id.find("finite_check") != std::string_view::npos) {
            return CanonicalEnumValue{"every-stage"};
        }
        if (field_id.find("quality") != std::string_view::npos) {
            return CanonicalEnumValue{"valid"};
        }
        if (field_id == "predicates.0.action") {
            return CanonicalEnumValue{"Abort"};
        }
        if (field_id == "predicates.1.action" ||
            field_id == "predicates.2.action") {
            return CanonicalEnumValue{"Complete"};
        }
        if (field_id == "predicates.0.metric") {
            return CanonicalEnumValue{"remaining_mass_kg"};
        }
        if (field_id == "predicates.1.metric") {
            return CanonicalEnumValue{"duration_s"};
        }
        if (field_id == "predicates.2.metric") {
            return CanonicalEnumValue{"downrange_m"};
        }
        if (field_id == "predicates.0.relation") {
            return CanonicalEnumValue{"<="};
        }
        if (field_id.find(".relation") != std::string_view::npos) {
            return CanonicalEnumValue{">="};
        }
        throw std::runtime_error("unmapped REF-YYZ enum field: " +
                                 std::string(field_id));
    case CanonicalConfigValueKind::Float64:
        if (initial_state && field_id == "attitude.w") {
            return 1.0;
        }
        if (initial_state && field_id == "position.z_meters") {
            return 1000.0;
        }
        if (initial_state && field_id == "velocity.x_meters_per_second") {
            return 110.0;
        }
        if (initial_state && field_id == "mass_kilograms") {
            return 100.0;
        }
        if (initial_state &&
            field_id == "body_origin_to_center_of_mass.x_meters") {
            return 0.2;
        }
        if (initial_state &&
            field_id ==
                "inertia_about_center_of_mass.xx_kilogram_meters_squared") {
            return 10.0;
        }
        if (initial_state &&
            field_id ==
                "inertia_about_center_of_mass.yy_kilogram_meters_squared") {
            return 20.0;
        }
        if (initial_state &&
            field_id ==
                "inertia_about_center_of_mass.zz_kilogram_meters_squared") {
            return 30.0;
        }
        if (initial_state) {
            return 0.0;
        }
        if (field_id.find("condition_limit") != std::string_view::npos) {
            return 1.0e12;
        }
        if (field_id.find("zero_tolerance") != std::string_view::npos) {
            return 1.0e-14;
        }
        if (field_id.find("tolerance") != std::string_view::npos) {
            if (model_id ==
                gnc::packages::yyz::kSuppliedPropulsionModelIdentity) {
                return 1.0e-12;
            }
            return 2.0e-12;
        }
        if (field_id.find("fixed_step") != std::string_view::npos) {
            return 0.1;
        }
        if (field_id.find("target_altitude") != std::string_view::npos) {
            return 1000.0;
        }
        if (field_id.find("altitude_error_gain") !=
            std::string_view::npos) {
            return 0.02;
        }
        if (field_id.find("vertical_speed_gain") !=
            std::string_view::npos) {
            return 0.05;
        }
        if (field_id.find("pitch_command_limit_radians") !=
            std::string_view::npos) {
            return 0.04;
        }
        if (field_id.find("pitch_error_gain") != std::string_view::npos) {
            return 500.0;
        }
        if (field_id.find("pitch_rate_gain") != std::string_view::npos) {
            return 80.0;
        }
        if (field_id.find("moment_command_limit") !=
            std::string_view::npos) {
            return 25.0;
        }
        if (field_id.find("realization_gain") != std::string_view::npos) {
            return 1.0;
        }
        if (field_id.find("thrust_magnitude") != std::string_view::npos) {
            return 100.0;
        }
        if (field_id.find("fuel_consumption_rate") !=
            std::string_view::npos) {
            return 0.5;
        }
        if (field_id == "center_of_mass_to_application.y_meters") {
            return 0.2;
        }
        if (field_id ==
            "intrinsic_moment_at_application.z_newton_meters") {
            return 20.0;
        }
        if (field_id == "thrust_direction.x_unit") {
            return 1.0;
        }
        if (field_id.find("thrust_direction") != std::string_view::npos ||
            field_id.find("center_of_mass_to_application") !=
                std::string_view::npos ||
            field_id.find("intrinsic_moment_at_application") !=
                std::string_view::npos) {
            return 0.0;
        }
        if (field_id == "predicates.0.threshold") {
            return 99.85;
        }
        if (field_id == "predicates.1.threshold") {
            return 0.2;
        }
        if (field_id == "predicates.2.threshold") {
            return 20.0;
        }
        throw std::runtime_error("unmapped REF-YYZ float field: " +
                                 std::string(field_id));
    }
    return std::string{};
}

[[nodiscard]] CanonicalConfigBlock config_for(
    const gnc::model_sdk::StaticConfigSchemaDescriptor& schema,
    std::string_view model_id, std::string_view provenance_prefix,
    bool initial_state,
    std::vector<SourceConfigFieldProvenance>& provenance) {
    CanonicalConfigBlock result;
    result.schema_id = schema.schema_id;
    result.schema_version = schema.schema_version;
    for (const auto& field : schema.fields) {
        result.fields.push_back(
            {field.field_id,
             fixture_value(model_id, field.field_id, field.value_kind,
                           initial_state)});
        provenance.push_back(
            {field.field_id,
             ref(std::string(provenance_prefix) + "/" + field.field_id)});
    }
    return result;
}

[[nodiscard]] gnc::foundation::NumericalPolicy numerical_policy() {
    return {2.0e-12, 2.0e-12,
            gnc::foundation::FiniteCheck::EveryStage, 1.0e-14, 1.0e12};
}

void append_configuration_provenance(
    const CanonicalConfigBlock& configuration,
    std::string_view provenance_prefix,
    std::vector<SourceConfigFieldProvenance>& provenance) {
    for (const auto& field : configuration.fields) {
        provenance.push_back(
            {field.field_id,
             ref(std::string(provenance_prefix) + "/" + field.field_id)});
    }
}

[[nodiscard]] CanonicalConfigBlock product_configuration_for(
    const StaticModelDescriptor& model,
    std::string_view provenance_prefix,
    std::vector<SourceConfigFieldProvenance>& provenance) {
    using namespace gnc::packages::yyz;
    CanonicalConfigBlock configuration;
    if (model.definition.model_id == kForceMomentClosureModelIdentity) {
        ForceMomentClosureDefinition definition;
        definition.metadata = {
            std::string(kForceMomentClosureModelIdentity),
            std::string(kForceMomentClosureModelVersion),
            ModelExecutionForm::Closure};
        definition.body_frame.id = std::string(kBodyFrame);
        definition.clock_domain.id = std::string(kClock);
        definition.configuration_revision = 11;
        definition.numerical_policy = numerical_policy();
        configuration = canonical_force_moment_closure_config(definition);
        const auto rebuilt =
            build_force_moment_closure_definition(configuration);
        require(rebuilt.has_value() &&
                    canonical_force_moment_closure_config(rebuilt.value()) ==
                        configuration,
                "REF closure canonical configuration did not round-trip");
    } else if (model.definition.model_id ==
               kAerodynamicTableModelIdentity) {
        AerodynamicTableDefinition definition;
        definition.metadata = {
            std::string(kAerodynamicTableModelIdentity),
            std::string(kAerodynamicTableModelVersion),
            ModelExecutionForm::PureQuery};
        definition.source_id = "aero.body";
        definition.configuration_id =
            "configuration.fixture.yyz.clean@1";
        definition.reference_area_square_meters = 1.0;
        definition.reference_span_meters = 1.0;
        definition.reference_chord_meters = 1.0;
        definition.body_origin_to_application.value =
            gnc::foundation::Vec3{0.2, 0.0, -25.0 / 18.0};
        definition.table_asset_id =
            "aero-table.fixture.yyz.multiaffine@1";
        configuration = canonical_aerodynamic_table_config(definition);
        const auto rebuilt = build_aerodynamic_table_definition(
            configuration, definition.table_asset_id);
        require(rebuilt.has_value() &&
                    canonical_aerodynamic_table_config(rebuilt.value()) ==
                        configuration,
                "REF aerodynamic canonical configuration did not round-trip");
    } else if (model.definition.model_id ==
               kUniformEnvironmentModelIdentity) {
        UniformEnvironmentDefinition definition;
        definition.metadata = {
            std::string(kUniformEnvironmentModelIdentity),
            std::string(kUniformEnvironmentModelVersion),
            ModelExecutionForm::PureQuery};
        definition.inertial_frame.id = std::string(kInertialFrame);
        definition.clock_domain.id = std::string(kClock);
        definition.configuration_revision = 11;
        definition.gravity.value =
            gnc::foundation::Vec3{0.0, 0.0, -9.80665};
        definition.velocity_airmass.value =
            gnc::foundation::Vec3{10.0, 0.0, 0.0};
        definition.density_kilograms_per_cubic_meter = 1.225;
        definition.speed_of_sound_meters_per_second = 340.0;
        configuration = canonical_uniform_environment_config(definition);
        const auto rebuilt =
            build_uniform_environment_definition(configuration);
        require(rebuilt.has_value() &&
                    canonical_uniform_environment_config(rebuilt.value()) ==
                        configuration,
                "REF environment canonical configuration did not round-trip");
    } else if (model.definition.model_id ==
               kAltitudePitchGuidanceModelIdentity) {
        AltitudePitchGuidanceDefinition definition;
        definition.model_id =
            std::string(kAltitudePitchGuidanceModelIdentity);
        definition.model_version =
            std::string(kAltitudePitchGuidanceModelVersion);
        definition.inertial_frame.id = std::string(kInertialFrame);
        definition.clock_domain.id = std::string(kClock);
        definition.configuration_revision = 11;
        definition.target_altitude_meters = 1000.0;
        definition.altitude_error_gain_radians_per_meter = 0.02;
        definition.vertical_speed_gain_radian_seconds_per_meter = 0.05;
        definition.pitch_command_limit_radians = 0.04;
        definition.attitude_policy.numerical = numerical_policy();
        definition.attitude_policy.normalization =
            gnc::foundation::QuaternionNormalizationPolicy::
                NormalizeWithFlag;
        configuration = canonical_altitude_pitch_guidance_config(definition);
        const auto rebuilt =
            build_altitude_pitch_guidance_definition(configuration);
        require(rebuilt.has_value() &&
                    canonical_altitude_pitch_guidance_config(rebuilt.value()) ==
                        configuration,
                "REF guidance canonical configuration did not round-trip");
    } else {
        configuration = config_for(
            model.configuration, model.definition.model_id,
            provenance_prefix, false, provenance);
        return configuration;
    }
    require(configuration.schema_id == model.configuration.schema_id &&
                configuration.schema_version ==
                    model.configuration.schema_version &&
                configuration.fields.size() ==
                    model.configuration.fields.size(),
            "canonical product configuration differs from Catalog schema");
    append_configuration_provenance(
        configuration, provenance_prefix, provenance);
    return configuration;
}

[[nodiscard]] bool is_terminal_evaluator(
    const StaticModelDescriptor& model) {
    return model.runtime_component.has_value() &&
           model.runtime_component->profile == RuntimeCellProfile::Evaluator;
}

[[nodiscard]] CompleteStaticCompositionSource make_source(
    const StaticPackageDescriptor& package) {
    CompleteStaticCompositionSource source;
    source.source_version =
        std::string(gnc::compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id =
        "mission.fixture.yyz.lookup-altitude-hold@1";
    source.plan_id = "plan.ref-yyz.complete";
    source.mission_source = ref("mission");
    source.clock = {std::string(kClock), 0.1, 0, 2, ref("clock")};
    source.entities.push_back(
        {std::string(kEntity),
         gnc::compiler::EntityLifecycle::ActiveAtInitialize,
         ref("entities/vehicle/identity"),
         ref("entities/vehicle/lifecycle")});
    const ScopeKey vehicle_scope{ScopeKind::Vehicle,
                                 std::string(kEntity)};
    source.scopes.push_back({vehicle_scope, ref("scopes/vehicle")});

    std::map<std::string, std::string> occurrence_by_model;
    for (std::size_t index = 0U; index < package.models.size(); ++index) {
        const auto& model = package.models[index];
        CompleteSourceOccurrence occurrence;
        occurrence.occurrence_id = occurrence_id(index);
        occurrence.model_id = model.definition.model_id;
        occurrence.model_version = model.definition.model_version;
        occurrence.source = ref("occurrences/" + occurrence.occurrence_id);
        occurrence.subject_entity_id = std::string(kEntity);
        occurrence.subject_source =
            ref("occurrences/" + occurrence.occurrence_id + "/subject");
        if (model.placement !=
            gnc::model_sdk::ModelPlacement::Environment) {
            occurrence.scope = vehicle_scope;
            occurrence.scope_source =
                ref("occurrences/" + occurrence.occurrence_id + "/scope");
        }
        occurrence.placement = model.placement;
        occurrence.placement_source =
            ref("occurrences/" + occurrence.occurrence_id + "/placement");
        occurrence.configuration_source =
            ref("occurrences/" + occurrence.occurrence_id + "/config");
        occurrence.configuration = product_configuration_for(
            model,
            "occurrences/" + occurrence.occurrence_id + "/config/fields",
            occurrence.configuration_field_sources);
        for (const auto& asset : model.asset_slots) {
            require(asset.role == "aerodynamics",
                    "REF graph contains an unmapped product asset role");
            occurrence.asset_bindings.push_back(
                {asset.role, asset.asset_schema_id,
                 "aero-table.fixture.yyz.multiaffine@1",
                 ref("occurrences/" + occurrence.occurrence_id +
                     "/assets/" + asset.role)});
        }
        occurrence_by_model.emplace(model.definition.model_id,
                                    occurrence.occurrence_id);
        source.occurrences.push_back(std::move(occurrence));

        if (model.runtime_component.has_value() &&
            model.runtime_component->state_owner.has_value()) {
            const auto& state_owner =
                *model.runtime_component->state_owner;
            CompleteSourceInitialBinding initial;
            initial.owner_occurrence_id = occurrence_id(index);
            initial.source =
                ref("initial/" + initial.owner_occurrence_id);
            initial.builder_inputs = config_for(
                state_owner.initial_state_input_schema,
                model.definition.model_id,
                "initial/" + initial.owner_occurrence_id + "/fields",
                true, initial.field_sources);
            source.initial_bindings.push_back(std::move(initial));
        }
    }

    std::size_t binding_index = 0U;
    for (std::size_t consumer_index = 0U;
         consumer_index < package.models.size(); ++consumer_index) {
        const auto& consumer = package.models[consumer_index];
        if (is_terminal_evaluator(consumer)) {
            continue;
        }
        for (const auto& input : consumer.ports) {
            if (input.direction !=
                gnc::model_sdk::StaticPortDirection::Input) {
                continue;
            }
            std::vector<std::pair<std::size_t, const gnc::model_sdk::StaticPortDescriptor*>>
                providers;
            for (std::size_t provider_index = 0U;
                 provider_index < package.models.size(); ++provider_index) {
                for (const auto& output :
                     package.models[provider_index].ports) {
                    if (output.direction ==
                            gnc::model_sdk::StaticPortDirection::Output &&
                        output.contract_id == input.contract_id &&
                        output.binding_kind == input.binding_kind &&
                        output.temporal_relation == input.temporal_relation) {
                        providers.push_back({provider_index, &output});
                    }
                }
            }
            require(providers.size() == 1U,
                    "REF input did not resolve one exact provider");
            const auto binding_id =
                "binding.ref-yyz." + std::to_string(binding_index++);
            source.bindings.push_back(
                {binding_id,
                 occurrence_id(providers.front().first),
                 providers.front().second->port_id,
                 occurrence_id(consumer_index), input.port_id,
                 ref("bindings/" + binding_id)});
        }
    }

    std::size_t invocation_index = 0U;
    std::string continuous_owner;
    std::string closure_invocation;
    for (std::size_t caller_index = 0U;
         caller_index < package.models.size(); ++caller_index) {
        const auto& caller = package.models[caller_index];
        if (!caller.runtime_component.has_value()) {
            continue;
        }
        if (caller.runtime_component->profile ==
            RuntimeCellProfile::ContinuousStateOwner) {
            continuous_owner = occurrence_id(caller_index);
        }
        for (const auto& entry :
             caller.runtime_component->obligation_entries) {
            for (const auto& requirement :
                 entry.invocation_requirements) {
                std::vector<std::size_t> providers;
                for (std::size_t provider_index = 0U;
                     provider_index < package.models.size(); ++provider_index) {
                    const auto& provider = package.models[provider_index];
                    const bool query =
                        requirement.kind == StaticInvocationKind::PureQuery &&
                        provider.definition.execution_form ==
                            ModelExecutionForm::PureQuery &&
                        provider.pure_query.has_value() &&
                        provider.pure_query->request_contract_id ==
                            requirement.contract_id;
                    const bool closure =
                        requirement.kind == StaticInvocationKind::Closure &&
                        provider.definition.execution_form ==
                            ModelExecutionForm::Closure &&
                        provider.closure.has_value() &&
                        provider.closure->request_contract_id ==
                            requirement.contract_id;
                    if (query || closure) {
                        providers.push_back(provider_index);
                    }
                }
                require(providers.size() == 1U,
                        "REF invocation did not resolve one exact provider");
                const auto invocation_id =
                    "invocation.ref-yyz." +
                    std::to_string(invocation_index++);
                source.invocation_bindings.push_back(
                    {invocation_id, occurrence_id(caller_index),
                     entry.obligation, requirement.requirement_id,
                     occurrence_id(providers.front()),
                     ref("invocations/" + invocation_id)});
                if (requirement.kind == StaticInvocationKind::Closure) {
                    closure_invocation = invocation_id;
                }
            }
        }
    }
    require(!continuous_owner.empty() && !closure_invocation.empty(),
            "REF graph lacks continuous owner/closure authorization");
    source.integration_scopes.push_back(
        {"integration.ref-yyz", vehicle_scope, continuous_owner,
         continuous_owner, {closure_invocation},
         ref("integration/ref-yyz")});

    CompleteSourceTransaction transaction;
    transaction.transaction_id = "transaction.ref-yyz";
    transaction.scope = vehicle_scope;
    transaction.source = ref("transactions/ref-yyz");
    CompleteSourceEvaluatorHistory evaluator;
    evaluator.history_id = "history.ref-yyz";
    evaluator.source = ref("evaluators/ref-yyz/history");
    std::map<std::pair<std::string, std::string>, std::string>
        owner_by_schema_layout;
    const gnc::model_sdk::StaticEvaluatorHistoryShapeDescriptor*
        evaluator_shape = nullptr;
    for (std::size_t index = 0U; index < package.models.size(); ++index) {
        const auto& model = package.models[index];
        if (model.runtime_component.has_value() &&
            model.runtime_component->state_owner.has_value()) {
            transaction.owner_occurrence_ids.push_back(occurrence_id(index));
            const auto& schema =
                model.runtime_component->state_owner->schema;
            owner_by_schema_layout.emplace(
                std::make_pair(schema.schema_id, schema.layout_id),
                occurrence_id(index));
        }
        if (is_terminal_evaluator(model)) {
            evaluator.evaluator_occurrence_id = occurrence_id(index);
            require(model.runtime_component->evaluator_history_shape
                        .has_value(),
                    "REF evaluator lacks a package history shape");
            evaluator_shape =
                &*model.runtime_component->evaluator_history_shape;
        }
    }
    require(evaluator_shape != nullptr,
            "REF graph lacks an evaluator history shape");
    evaluator.committed_history_depth = evaluator_shape->depth;
    for (const auto& member : evaluator_shape->ordered_members) {
        const auto owner = owner_by_schema_layout.find(
            {member.state_schema_id, member.state_layout_id});
        require(owner != owner_by_schema_layout.end(),
                "REF evaluator member lacks its exact state owner");
        evaluator.owner_occurrence_ids.push_back(owner->second);
    }
    require(transaction.owner_occurrence_ids.size() == 2U &&
                evaluator.owner_occurrence_ids.size() == 2U &&
                !evaluator.evaluator_occurrence_id.empty(),
            "REF graph lacks two state owners or evaluator");
    source.transactions.push_back(std::move(transaction));
    source.evaluator_histories.push_back(std::move(evaluator));
    source.package_build_locks.push_back(
        {package.package_id, package.package_version,
         "build.ref-yyz.release", ref("packages/yyz/build")});
    return source;
}

[[nodiscard]] bool has_diagnostic(
    const std::vector<gnc::compiler::CompleteDiagnostic>& diagnostics,
    CompleteDiagnosticCode code) {
    return std::any_of(
        diagnostics.begin(), diagnostics.end(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
}

template <typename Outcome>
void print_diagnostics(const Outcome& outcome) {
    for (const auto& diagnostic : outcome.diagnostics) {
        std::cerr << gnc::compiler::to_string(diagnostic.code) << " "
                  << diagnostic.subject << ": " << diagnostic.detail
                  << '\n';
    }
}

[[nodiscard]] const gnc::contracts::PlanImageRuntimeComponent*
find_image_component(
    const gnc::contracts::ExecutionPlanImage& image,
    std::string_view model_id) {
    const auto occurrence = std::find_if(
        image.occurrences().begin(), image.occurrences().end(),
        [&](const auto& candidate) {
            return candidate.definition_id == model_id;
        });
    if (occurrence == image.occurrences().end()) {
        return nullptr;
    }
    const auto component = std::find_if(
        image.runtime_components().begin(),
        image.runtime_components().end(), [&](const auto& candidate) {
            return candidate.occurrence_handle == occurrence->handle;
        });
    return component == image.runtime_components().end() ? nullptr
                                                          : &*component;
}

void relocate(SourceRef& source) {
    if (!source.document_uri.empty()) {
        source.document_uri = "relocated/ref-yyz/source.json";
    }
    if (!source.node_path.empty()) {
        source.node_path = "relocated/" + source.node_path;
    }
}

void relocate(CompleteStaticCompositionSource& source) {
    relocate(source.mission_source);
    relocate(source.clock.source);
    for (auto& entity : source.entities) {
        relocate(entity.identity_source);
        relocate(entity.lifecycle_source);
    }
    for (auto& scope : source.scopes) {
        relocate(scope.source);
    }
    for (auto& occurrence : source.occurrences) {
        relocate(occurrence.source);
        relocate(occurrence.subject_source);
        relocate(occurrence.scope_source);
        relocate(occurrence.placement_source);
        relocate(occurrence.configuration_source);
        for (auto& field : occurrence.configuration_field_sources) {
            relocate(field.source);
        }
        for (auto& asset : occurrence.asset_bindings) {
            relocate(asset.source);
        }
    }
    for (auto& initial : source.initial_bindings) {
        relocate(initial.source);
        for (auto& field : initial.field_sources) {
            relocate(field.source);
        }
    }
    for (auto& binding : source.bindings) {
        relocate(binding.source);
    }
    for (auto& invocation : source.invocation_bindings) {
        relocate(invocation.source);
    }
    for (auto& scope : source.integration_scopes) {
        relocate(scope.source);
    }
    for (auto& transaction : source.transactions) {
        relocate(transaction.source);
    }
    for (auto& evaluator : source.evaluator_histories) {
        relocate(evaluator.source);
    }
    for (auto& package : source.package_build_locks) {
        relocate(package.source);
    }
}

int poison_call_count = 0;

void poison_entry() noexcept {
    ++poison_call_count;
}

void verify_initial_product_builders() {
    using namespace gnc::packages::yyz;
    RigidStepAlgorithmDefinition algorithm;
    algorithm.fixed_step_seconds = 0.1;
    algorithm.numerical_policy = numerical_policy();
    algorithm.attitude_evaluation_policy.numerical = numerical_policy();
    algorithm.attitude_evaluation_policy.normalization =
        gnc::foundation::QuaternionNormalizationPolicy::NormalizeWithFlag;
    algorithm.candidate_attitude_policy =
        algorithm.attitude_evaluation_policy;
    RigidState rigid;
    rigid.position.value = gnc::foundation::Vec3{0.0, 0.0, 1000.0};
    rigid.velocity.value = gnc::foundation::Vec3{110.0, 0.0, 0.0};
    rigid.attitude.value =
        gnc::foundation::quaternion_from_wxyz(1.0, 0.0, 0.0, 0.0);
    rigid.angular_rate.value = gnc::foundation::Vec3::Zero();
    const auto built_rigid =
        RigidInitialStateBuilder::build(algorithm, {rigid});
    require(built_rigid.has_value() &&
                built_rigid.value().position.value(2) == 1000.0 &&
                built_rigid.value().velocity.value(0) == 110.0,
            "REF rigid initial binding was rejected by product builder");

    MassState mass;
    mass.context = {
        {std::string(kBodyFrame)}, {std::string(kClock)}, {0, 0.0}, 11,
        gnc::contracts::DataQuality::Valid};
    mass.mass_state_id = std::string(kMassState);
    mass.mass_kilograms = 100.0;
    mass.body_origin_to_center_of_mass.value =
        gnc::foundation::Vec3{0.2, 0.0, 0.0};
    mass.inertia_about_center_of_mass.value =
        gnc::foundation::Mat3::Zero();
    mass.inertia_about_center_of_mass.value.diagonal() =
        gnc::foundation::Vec3{10.0, 20.0, 30.0};
    const ScalarBurnMassDefinition mass_definition{
        std::string(kScalarBurnMassModelIdentity),
        std::string(kScalarBurnMassModelVersion), std::string(kMassState),
        numerical_policy()};
    const auto built_mass = build_scalar_burn_mass_initial_state(
        mass_definition, {mass});
    require(built_mass.has_value() &&
                built_mass.value().mass_kilograms == 100.0 &&
                built_mass.value()
                        .inertia_about_center_of_mass.value(2, 2) == 30.0,
            "REF mass initial binding was rejected by product builder");
}

[[nodiscard]] gnc::foundation::NumericalOutcome<
    gnc::packages::yyz::RigidDerivativeOutput>
poison_derivative(
    const gnc::packages::yyz::RigidStepAlgorithmDefinition& algorithm,
    const gnc::packages::yyz::RigidDerivativeInput& input) {
    ++poison_call_count;
    return gnc::packages::yyz::RigidDerivativeKernel::evaluate(algorithm,
                                                                input);
}

[[nodiscard]] gnc::foundation::NumericalOutcome<
    gnc::packages::yyz::AltitudePitchGuidanceRuntimeCell>
poison_guidance_runtime_cell_factory(
    const gnc::packages::yyz::AltitudePitchGuidanceDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const gnc::packages::yyz::AltitudePitchGuidanceRuntimeCellBindings&
        bindings) {
    ++poison_call_count;
    return gnc::packages::yyz::create_altitude_pitch_guidance_runtime_cell(
        definition, context, bindings);
}

[[nodiscard]] const gnc::packages::yyz::RigidStateCodec&
poison_rigid_state_codec() noexcept {
    ++poison_call_count;
    return gnc::packages::yyz::rigid_state_codec();
}

void verify_complete_ref_graph() {
    // Factory/codec conformance is link-only: this test never invokes them.
    const auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source = make_source(package);
    const auto compilation = gnc::compiler::compile_complete_execution_plan(
        source, {package});
    if (!compilation.succeeded()) {
        print_diagnostics(compilation);
    }
    require(compilation.succeeded(),
            "REF-YYZ planning/proof compilation failed");
    const auto& plan = compilation.value->plan;
    require(plan.revision == 5U && plan.occurrences.size() == 10U,
            "complete plan occurrence/revision count changed");
    require(plan.preparation_inputs.size() == 3U &&
                plan.queries.size() == 2U && plan.closures.size() == 1U,
            "prepare/query/closure plan count changed");
    require(plan.runtime_components.size() == 7U &&
                plan.runtime_callsites.size() == 10U &&
                plan.state_blocks.size() == 2U &&
                plan.initial_states.size() == 2U,
            "runtime/state plan count changed");
    require(plan.invocation_bindings.size() == 3U &&
                plan.integration_scopes.size() == 1U &&
                plan.transactions.size() == 1U &&
                plan.evaluator_histories.size() == 1U &&
                plan.entry_requirements.size() == 44U,
            "authorization/scope/link requirement count changed");
    require(!compilation.value->proofs.records.empty() &&
                compilation.value->proofs.coverage.size() ==
                    gnc::compiler::complete_plan_detail::all_plan_elements(plan)
                        .size(),
            "proof coverage is not exact for every plan element");

    const auto linked = gnc::compiler::link_complete_execution_plan(
        plan, compilation.value->proofs, {implementation});
    if (!linked.succeeded()) {
        print_diagnostics(linked);
    }
    require(linked.succeeded(),
            "REF-YYZ science-entry link review failed");
    const auto& image = *linked.value;
    require(image.entries().size() == 44U &&
                image.occurrences().size() == 10U &&
                image.preparations().size() == 3U &&
                image.queries().size() == 2U &&
                image.closures().size() == 1U &&
                image.runtime_components().size() == 7U &&
                image.resource_plans().size() == 7U &&
                image.state_blocks().size() == 2U &&
                image.integration_scopes().size() == 1U,
            "linked image exact table counts changed");
    require(!image.fingerprint().empty(),
            "linked image has no deterministic fingerprint");
    require(image.packages().size() == 1U &&
                image.packages().front().build_fingerprint ==
                    "build.ref-yyz.release",
            "linked image lost the exact package build lock");
    for (const auto& image_callsite : image.callsites()) {
        const auto descriptor_callsite = std::find_if(
            plan.runtime_callsites.begin(), plan.runtime_callsites.end(),
            [&](const auto& candidate) {
                return candidate.callsite_id == image_callsite.callsite_id;
            });
        require(descriptor_callsite != plan.runtime_callsites.end() &&
                    descriptor_callsite->plan_element_id ==
                        image_callsite.plan_element_id &&
                    descriptor_callsite->request_contract_id ==
                        image_callsite.request_contract_id &&
                    descriptor_callsite->result_contract_id ==
                        image_callsite.result_contract_id,
                "runtime callsite request/result contracts changed between descriptor and image");
    }
    const auto has_entry_handle = [&](std::uint32_t handle) {
        return std::any_of(
            image.entries().begin(), image.entries().end(),
            [&](const auto& entry) { return entry.handle == handle; });
    };
    const auto has_invocation_handle = [&](std::uint32_t handle) {
        return std::any_of(
            image.invocations().begin(), image.invocations().end(),
            [&](const auto& invocation) {
                return invocation.handle == handle;
            });
    };
    // Query responses are caller-local. The one FrozenInterval Closure owns
    // the coordinator-held interval slot and writer.
    for (const auto& invocation : image.invocations()) {
        const auto binding = std::find_if(
            image.bindings().begin(), image.bindings().end(),
            [&](const auto& candidate) {
                return candidate.handle == invocation.result_binding_handle;
            });
        const auto port = std::find_if(
            image.ports().begin(), image.ports().end(),
            [&](const auto& candidate) {
                return candidate.handle == invocation.consumer_port_handle;
            });
        const auto callsite = std::find_if(
            image.callsites().begin(), image.callsites().end(),
            [&](const auto& candidate) {
                return candidate.handle == invocation.caller_callsite_handle;
            });
        require(binding != image.bindings().end() &&
                    port != image.ports().end() &&
                    callsite != image.callsites().end() &&
                    binding->consumer_port_handle == port->handle &&
                    port->occurrence_handle == callsite->occurrence_handle &&
                    invocation.requirement_cardinality == "exactly-one" &&
                    invocation.requirement_ordinal <
                        callsite->authorized_invocation_handles.size() &&
                    callsite->authorized_invocation_handles.at(
                        invocation.requirement_ordinal) ==
                        invocation.handle &&
                    std::find(callsite->authorized_invocation_handles.begin(),
                              callsite->authorized_invocation_handles.end(),
                              invocation.handle) !=
                        callsite->authorized_invocation_handles.end(),
                "invocation authorization/result binding is missing or ambiguous");
        if (invocation.result_route ==
            gnc::contracts::InvocationResultRoute::CallerLocal) {
            require(binding->provider_slot_handle == 0U &&
                        invocation.result_storage_slot_handle == 0U &&
                        invocation.result_writer_token_handle == 0U,
                    "caller-local Query response acquired CycleFrame storage");
        } else {
            const auto slot = std::find_if(
                image.slots().begin(), image.slots().end(),
                [&](const auto& candidate) {
                    return candidate.handle ==
                           invocation.result_storage_slot_handle;
                });
            const auto writer = std::find_if(
                image.writer_tokens().begin(), image.writer_tokens().end(),
                [&](const auto& candidate) {
                    return candidate.handle ==
                           invocation.result_writer_token_handle;
                });
            require(invocation.result_route ==
                            gnc::contracts::InvocationResultRoute::HeldInterval &&
                        slot != image.slots().end() &&
                        writer != image.writer_tokens().end() &&
                        binding->provider_slot_handle == slot->handle &&
                        slot->writer_token_handle == writer->handle &&
                        slot->storage_class ==
                            gnc::contracts::SlotStorageClass::IntegrationHeld &&
                        slot->hold_policy ==
                            gnc::contracts::SlotHoldPolicy::HoldInterval,
                    "FrozenInterval Closure lacks its unique held slot/writer");
        }
    }
    const auto invocation_for_requirement = [&](std::string_view id) {
        return std::find_if(
            image.invocations().begin(), image.invocations().end(),
            [&](const auto& invocation) {
                return invocation.requirement_id == id;
            });
    };
    const auto environment_invocation =
        invocation_for_requirement("environment-query");
    const auto aerodynamic_invocation =
        invocation_for_requirement("aerodynamic-table-query");
    const auto closure_invocation =
        invocation_for_requirement("force-moment-closure");
    require(environment_invocation != image.invocations().end() &&
                environment_invocation->requirement_ordinal == 0U &&
                aerodynamic_invocation != image.invocations().end() &&
                aerodynamic_invocation->requirement_ordinal == 1U &&
                closure_invocation != image.invocations().end() &&
                closure_invocation->requirement_ordinal == 2U,
            "typed rigid invocation roles lost package-authored ordinal mapping");
    require(image.integration_scopes().size() == 1U &&
                image.integration_scopes().front()
                        .closure_invocation_handles.size() == 1U &&
                image.integration_scopes().front()
                        .closure_invocation_handles.front() ==
                    closure_invocation->handle &&
                image.integration_scopes().front()
                        .held_form_slot_handle ==
                    closure_invocation->result_storage_slot_handle,
            "FrozenInterval closure response is not the single authoritative held form slot");
    for (const auto& query : image.queries()) {
        const auto preparation = std::find_if(
            image.preparations().begin(), image.preparations().end(),
            [&](const auto& candidate) {
                return candidate.handle == query.preparation_handle;
            });
        const auto invocation = std::find_if(
            image.invocations().begin(), image.invocations().end(),
            [&](const auto& candidate) {
                return query.authorized_invocation_handles.size() == 1U &&
                       candidate.handle ==
                           query.authorized_invocation_handles.front();
            });
        require(preparation != image.preparations().end() &&
                     invocation != image.invocations().end() &&
                    preparation->occurrence_handle ==
                        query.occurrence_handle &&
                    has_entry_handle(preparation->prepare_entry_handle) &&
                    has_entry_handle(query.query_entry_handle) &&
                    invocation->result_route ==
                        gnc::contracts::InvocationResultRoute::CallerLocal &&
                    query.workspace_requirement == "None" &&
                    query.authorized_invocation_handles.size() == 1U &&
                    has_invocation_handle(
                        query.authorized_invocation_handles.front()),
                "query image lacks exact numeric preparation/auth linkage");
        require(std::none_of(
                    image.callsites().begin(), image.callsites().end(),
                    [&](const auto& callsite) {
                        return callsite.occurrence_handle ==
                               query.occurrence_handle;
                    }),
                "PureQuery provider was lowered as a scheduler callsite");
    }
    for (const auto& closure : image.closures()) {
        const auto preparation = std::find_if(
            image.preparations().begin(), image.preparations().end(),
            [&](const auto& candidate) {
                return candidate.handle == closure.preparation_handle;
            });
        const auto invocation = std::find_if(
            image.invocations().begin(), image.invocations().end(),
            [&](const auto& candidate) {
                return closure.authorized_invocation_handles.size() == 1U &&
                       candidate.handle ==
                           closure.authorized_invocation_handles.front();
            });
        require(preparation != image.preparations().end() &&
                     invocation != image.invocations().end() &&
                    preparation->occurrence_handle ==
                        closure.occurrence_handle &&
                    has_entry_handle(preparation->prepare_entry_handle) &&
                    has_entry_handle(closure.closure_entry_handle) &&
                    invocation->result_route ==
                        gnc::contracts::InvocationResultRoute::HeldInterval &&
                    closure.strategy == "FrozenInterval" &&
                    closure.workspace_requirement == "None" &&
                    closure.authorized_invocation_handles.size() == 1U &&
                    has_invocation_handle(
                        closure.authorized_invocation_handles.front()),
                "closure image lacks exact numeric preparation/auth linkage");
        require(std::none_of(
                    image.callsites().begin(), image.callsites().end(),
                    [&](const auto& callsite) {
                        return callsite.occurrence_handle ==
                               closure.occurrence_handle;
                    }),
                "Closure provider was lowered as a scheduler callsite");
    }
    const auto environment_occurrence = std::find_if(
        image.occurrences().begin(), image.occurrences().end(),
        [](const auto& occurrence) {
            return occurrence.definition_id ==
                   gnc::packages::yyz::kUniformEnvironmentModelIdentity;
        });
    const auto guidance_occurrence = std::find_if(
        image.occurrences().begin(), image.occurrences().end(),
        [](const auto& occurrence) {
            return occurrence.definition_id ==
                   gnc::packages::yyz::kAltitudePitchGuidanceModelIdentity;
        });
    require(environment_occurrence != image.occurrences().end() &&
                environment_occurrence->placement == "environment" &&
                environment_occurrence->subject_entity_id == kEntity &&
                !environment_occurrence->has_scope &&
                guidance_occurrence != image.occurrences().end() &&
                guidance_occurrence->placement == "vehicle.process" &&
                guidance_occurrence->subject_entity_id == kEntity &&
                guidance_occurrence->has_scope &&
                guidance_occurrence->scope_kind == "Vehicle" &&
                guidance_occurrence->scope_subject_entity_id == kEntity,
            "image occurrence lost subject/placement/scope identity");

    const auto typed_entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::kRigidDerivativeKernelIdentity.id;
        });
    require(typed_entry != image.entries().end(),
            "rigid derivative linked entry is absent");
    const auto exact_derivative = std::any_cast<
        decltype(&gnc::packages::yyz::RigidDerivativeKernel::evaluate)>(
        &typed_entry->typed_entry);
    require(exact_derivative != nullptr &&
                *exact_derivative ==
                    &gnc::packages::yyz::RigidDerivativeKernel::evaluate,
            "image does not retain the exact typed derivative callable");
    const auto boundary_entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kControlledRigidBoundaryEvaluationIdentity.id;
        });
    require(boundary_entry != image.entries().end(),
            "controlled rigid boundary linked entry is absent");
    const auto exact_boundary = std::any_cast<
        gnc::packages::yyz::ControlledRigidBoundaryRuntimeCall>(
        &boundary_entry->typed_entry);
    require(exact_boundary != nullptr &&
                *exact_boundary ==
                    &gnc::packages::yyz::
                        ControlledRigidBoundaryEvaluationKernel::
                            prepare_for_integration,
            "image does not retain the final typed boundary callable");
    const auto evaluator_entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kCommittedMissionHistoryEvaluationIdentity.id;
        });
    require(evaluator_entry != image.entries().end(),
            "committed-history evaluator linked entry is absent");
    const auto exact_evaluator = std::any_cast<
        decltype(&gnc::packages::yyz::
                     CommittedMissionHistoryEvaluationKernel::evaluate)>(
        &evaluator_entry->typed_entry);
    require(exact_evaluator != nullptr &&
                *exact_evaluator ==
                    &gnc::packages::yyz::
                        CommittedMissionHistoryEvaluationKernel::evaluate,
            "image does not retain the final typed evaluator callable");
    const auto guidance_builder_entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kAltitudePitchGuidanceDefinitionBuilderIdentity.id;
        });
    require(guidance_builder_entry != image.entries().end() &&
                guidance_builder_entry->kind ==
                    gnc::contracts::PlanImageEntryKind::DefinitionBuilder &&
                !guidance_builder_entry->call_shape_id.empty(),
            "runtime definition-builder linked entry is absent or untyped");
    const auto exact_guidance_builder = std::any_cast<
        decltype(&gnc::packages::yyz::
                     build_altitude_pitch_guidance_definition)>(
        &guidance_builder_entry->typed_entry);
    require(exact_guidance_builder != nullptr &&
                *exact_guidance_builder ==
                    &gnc::packages::yyz::
                        build_altitude_pitch_guidance_definition,
            "image does not retain the exact typed runtime definition builder");
    const auto guidance_factory_entry = std::find_if(
        image.entries().begin(), image.entries().end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kAltitudePitchGuidanceRuntimeCellFactoryIdentity.id;
        });
    require(guidance_factory_entry != image.entries().end() &&
                guidance_factory_entry->kind ==
                    gnc::contracts::PlanImageEntryKind::RuntimeCellFactory,
            "runtime-cell factory linked entry is absent or has the wrong kind");
    const auto exact_guidance_factory = std::any_cast<
        gnc::packages::yyz::AltitudePitchGuidanceRuntimeCellFactoryCall>(
        &guidance_factory_entry->typed_entry);
    require(exact_guidance_factory != nullptr &&
                *exact_guidance_factory ==
                    &gnc::packages::yyz::
                        create_altitude_pitch_guidance_runtime_cell,
            "image does not retain the exact typed runtime-cell factory");

    const auto* guidance = find_image_component(
        image,
        gnc::packages::yyz::kAltitudePitchGuidanceModelIdentity);
    const auto* evaluator = find_image_component(
        image,
        gnc::packages::yyz::kCommittedMissionResultModelIdentity);
    const auto* rigid = find_image_component(
        image, gnc::packages::yyz::kRigidStepModelIdentity);
    const auto* mass = find_image_component(
        image, gnc::packages::yyz::kScalarBurnMassModelIdentity);
    require(std::all_of(
                image.runtime_components().begin(),
                image.runtime_components().end(), [&](const auto& component) {
                    return has_entry_handle(
                        component.runtime_cell_factory_entry_handle);
                }),
            "runtime component image lost an exact factory handle");
    require(guidance != nullptr &&
                has_entry_handle(
                    guidance->definition_builder_entry_handle) &&
                has_entry_handle(
                    guidance->runtime_cell_factory_entry_handle) &&
                guidance->runtime_cell_factory_entry_handle ==
                    guidance_factory_entry->handle &&
                guidance->state_block_handles.empty() &&
                guidance->schedule_trigger == "EveryBoundary" &&
                guidance->step_interval == 1U && guidance->offset == 0U &&
                guidance->max_input_age_steps == 0U &&
                guidance->lifecycle_capabilities ==
                    std::vector<std::string>{"Instantiate", "Dispose"} &&
                guidance->callsite_handles.size() == 1U,
            "guidance image schedule/lifecycle facts changed");
    require(evaluator != nullptr &&
                has_entry_handle(
                    evaluator->definition_builder_entry_handle) &&
                has_entry_handle(
                    evaluator->runtime_cell_factory_entry_handle) &&
                evaluator->state_block_handles.empty() &&
                evaluator->schedule_trigger == "TerminalSequenceReady" &&
                evaluator->step_interval == 0U && evaluator->offset == 0U &&
                evaluator->max_input_age_steps == 0U &&
                evaluator->lifecycle_capabilities ==
                    std::vector<std::string>{"Instantiate", "Dispose"} &&
                evaluator->callsite_handles.size() == 1U,
            "terminal evaluator image schedule/lifecycle facts changed");
    require(rigid != nullptr && rigid->state_block_handles.size() == 1U &&
                mass != nullptr && mass->state_block_handles.size() == 1U,
            "state-owning runtime-cell factories lost their exact state block handle");
    std::set<std::uint32_t> factory_handles;
    std::set<std::uint32_t> runtime_instance_ids;
    std::set<std::uint32_t> resource_handles;
    for (const auto& component : image.runtime_components()) {
        factory_handles.insert(
            component.runtime_cell_factory_entry_handle);
        runtime_instance_ids.insert(component.runtime_instance_id);
        resource_handles.insert(component.resource_plan_handle);
    }
    require(factory_handles.size() == 7U &&
                runtime_instance_ids.size() == 7U &&
                runtime_instance_ids.count(0U) == 0U &&
                resource_handles.size() == 7U &&
                resource_handles.count(0U) == 0U,
            "runtime factory/instance/resource handles are not unique and nonzero");
    require(image.clock().clock_id == kClock &&
                image.clock().base_step_seconds == 0.1 &&
                image.clock().initial_tick == 0 &&
                image.clock().terminal_tick == 2,
            "REF-YYZ image clock grid changed");
    const auto evaluator_model = std::find_if(
        package.models.begin(), package.models.end(),
        [](const auto& model) { return is_terminal_evaluator(model); });
    require(evaluator_model != package.models.end() &&
                evaluator_model->runtime_component
                    ->evaluator_history_shape.has_value() &&
                image.evaluator_histories().size() == 1U,
            "REF evaluator history descriptor/image is absent");
    const auto& expected_history =
        *evaluator_model->runtime_component->evaluator_history_shape;
    const auto& linked_history = image.evaluator_histories().front();
    require(linked_history.request_contract_id ==
                    expected_history.request_contract_id &&
                linked_history.history_depth == expected_history.depth &&
                linked_history.ordered_members.size() ==
                    expected_history.ordered_members.size(),
            "linked evaluator history does not preserve its exact package shape");
    const auto evaluator_callsite = std::find_if(
        image.callsites().begin(), image.callsites().end(),
        [&](const auto& callsite) {
            return callsite.handle ==
                   linked_history.evaluator_callsite_handle;
        });
    require(evaluator_callsite != image.callsites().end(),
            "linked evaluator history lacks its callsite");
    std::vector<std::uint32_t> expected_history_inputs;
    for (std::size_t index = 0U;
         index < linked_history.ordered_members.size(); ++index) {
        const auto& expected_member =
            expected_history.ordered_members[index];
        const auto& linked_member =
            linked_history.ordered_members[index];
        const auto state = std::find_if(
            image.state_blocks().begin(), image.state_blocks().end(),
            [&](const auto& candidate) {
                return candidate.committed_slot_handle ==
                       linked_member.committed_state_slot_handle;
            });
        require(linked_member.member_id == expected_member.member_id &&
                    linked_member.state_schema_id ==
                        expected_member.state_schema_id &&
                    linked_member.state_layout_id ==
                        expected_member.state_layout_id &&
                    state != image.state_blocks().end() &&
                    state->schema_id == expected_member.state_schema_id &&
                    state->layout_id == expected_member.state_layout_id,
                "linked evaluator history member order/schema/layout changed");
        expected_history_inputs.push_back(
            linked_member.committed_state_slot_handle);
    }
    require(evaluator_callsite->input_slot_handles ==
                expected_history_inputs,
            "evaluator callsite does not consume the exact ordered committed-history shape");
    require(std::all_of(
                image.slots().begin(), image.slots().end(),
                [](const auto& slot) {
                    return slot.size_bytes > 0U &&
                           slot.alignment_bytes > 0U &&
                           slot.codec_entry_handle != 0U &&
                           slot.writer_token_handle != 0U;
                }),
            "image contains a zero runtime/state slot layout, codec, or writer fact");
    std::vector<const gnc::contracts::PlanImageSlot*> ordered_slots;
    ordered_slots.reserve(image.slots().size());
    for (const auto& slot : image.slots()) {
        ordered_slots.push_back(&slot);
    }
    std::sort(ordered_slots.begin(), ordered_slots.end(),
              [](const auto* lhs, const auto* rhs) {
                  return lhs->offset_bytes < rhs->offset_bytes;
              });
    std::uint64_t previous_end = 0U;
    for (const auto* slot : ordered_slots) {
        require(slot->offset_bytes % slot->alignment_bytes == 0U &&
                    slot->offset_bytes >= previous_end &&
                    std::all_of(slot->reader_handles.begin(),
                                slot->reader_handles.end(),
                                [](std::uint32_t handle) {
                                    return handle != 0U;
                                }),
                "slot offsets overlap, violate alignment, or contain a zero reader handle");
        previous_end = slot->offset_bytes + slot->size_bytes;
    }
    const auto& integration = image.integration_scopes().front();
    require(integration.integrator_id ==
                    gnc::foundation::kClassicalRk4FixedStepIdentity.id &&
                integration.integrator_version ==
                    gnc::foundation::kClassicalRk4FixedStepIdentity.version &&
                integration.clock_handle == image.clock().handle &&
                integration.step_ticks == 1U &&
                integration.fixed_step_seconds == 0.1 &&
                integration.absolute_tolerance == 2.0e-12 &&
                integration.relative_tolerance == 2.0e-12 &&
                integration.check_finiteness == "every-stage" &&
                integration.zero_threshold == 1.0e-14 &&
                integration.condition_limit == 1.0e12 &&
                integration.workspace_layout_id == "gnc.workspace.none@1" &&
                integration.candidate_codec_entry_handle != 0U &&
                !integration.candidate_project_operation_id.empty() &&
                !integration.candidate_finite_validation_operation_id.empty() &&
                !integration.candidate_invariant_validation_operation_id.empty(),
            "IntegrationScope lost fixed RK4, clock, numerical policy, workspace, or candidate validation facts");
    require(image.transactions().size() == 1U &&
                image.transactions().front().candidates.size() == 2U &&
                image.transactions().front().held_slot_handles.size() == 1U &&
                image.transactions().front().branches.size() == 3U,
            "atomic rigid+mass transaction table shape changed");
    const auto& transaction = image.transactions().front();
    const auto& continue_branch = transaction.branches.at(0U);
    const auto& terminal_branch = transaction.branches.at(1U);
    const auto& failure_branch = transaction.branches.at(2U);
    require(continue_branch.branch ==
                    gnc::contracts::TransactionBranch::Continue &&
                continue_branch.committed_candidate_slot_handles.size() ==
                    2U &&
                continue_branch.discarded_candidate_slot_handles.empty() &&
                continue_branch.retained_held_slot_handles ==
                    transaction.held_slot_handles &&
                continue_branch.discarded_held_slot_handles.empty() &&
                continue_branch.model_commit &&
                continue_branch.observation_seal &&
                !continue_branch.result_seal_after_observation &&
                continue_branch.epoch_delta == 1 &&
                continue_branch.tick_delta == 1 &&
                terminal_branch.branch ==
                    gnc::contracts::TransactionBranch::Terminal &&
                terminal_branch.committed_candidate_slot_handles.empty() &&
                terminal_branch.discarded_candidate_slot_handles.size() ==
                    2U &&
                terminal_branch.discarded_held_slot_handles ==
                    transaction.held_slot_handles &&
                !terminal_branch.model_commit &&
                terminal_branch.observation_seal &&
                terminal_branch.result_seal_after_observation &&
                terminal_branch.epoch_delta == 0 &&
                terminal_branch.tick_delta == 0 &&
                failure_branch.branch ==
                    gnc::contracts::TransactionBranch::Failure &&
                failure_branch.committed_candidate_slot_handles.empty() &&
                failure_branch.discarded_candidate_slot_handles.size() ==
                    2U &&
                failure_branch.discarded_held_slot_handles ==
                    transaction.held_slot_handles &&
                !failure_branch.model_commit &&
                !failure_branch.observation_seal &&
                !failure_branch.result_seal_after_observation &&
                failure_branch.epoch_delta == 0 &&
                failure_branch.tick_delta == 0,
            "continue/terminal/failure transaction semantics changed");
    require(image.lifecycle().preparation_handles.size() == 3U &&
                image.lifecycle().runtime_component_handles.size() == 7U &&
                image.lifecycle().initial_binding_handles.size() == 2U &&
                image.lifecycle().bound_provider_dispose_handles.size() ==
                    3U &&
                image.lifecycle().runtime_component_dispose_handles ==
                    std::vector<std::uint32_t>(
                        image.lifecycle().runtime_component_handles.rbegin(),
                        image.lifecycle().runtime_component_handles.rend()) &&
                image.lifecycle().preparation_dispose_handles ==
                    std::vector<std::uint32_t>(
                        image.lifecycle().preparation_handles.rbegin(),
                        image.lifecycle().preparation_handles.rend()) &&
                std::all_of(image.preparations().begin(),
                            image.preparations().end(),
                            [](const auto& preparation) {
                                return preparation.ownership ==
                                           gnc::contracts::
                                               PreparationOwnership::
                                                   SessionOwned &&
                                       preparation.phase ==
                                           gnc::contracts::PreparationPhase::
                                               InitializeTime &&
                                       preparation.cache_policy ==
                                           gnc::contracts::
                                               PreparedModelCachePolicy::
                                                   NoSharedCache;
                            }),
            "Session-owned initialize-time no-cache lifecycle order changed");
}

void verify_determinism_and_link_semantics() {
    const auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source = make_source(package);
    const auto baseline = gnc::compiler::compile_complete_execution_plan(
        source, {package});
    require(baseline.succeeded(),
            "baseline compilation for determinism checks failed");
    const auto baseline_image =
        gnc::compiler::link_complete_execution_plan(
            baseline.value->plan, baseline.value->proofs,
            {implementation});
    require(baseline_image.succeeded(),
            "baseline image for determinism checks failed");

    auto relocated_source = source;
    relocate(relocated_source);
    const auto relocated = gnc::compiler::compile_complete_execution_plan(
        relocated_source, {package});
    require(relocated.succeeded() &&
                relocated.value->plan.source_semantic_hash ==
                    baseline.value->plan.source_semantic_hash &&
                relocated.value->plan.descriptor_semantic_hash ==
                    baseline.value->plan.descriptor_semantic_hash &&
                relocated.value->proofs.proof_index_hash ==
                    baseline.value->proofs.proof_index_hash,
            "source relocation changed semantic plan/proof identity");
    const auto relocated_image =
        gnc::compiler::link_complete_execution_plan(
            relocated.value->plan, relocated.value->proofs,
            {implementation});
    require(relocated_image.succeeded() &&
                relocated_image.value->fingerprint() ==
                    baseline_image.value->fingerprint() &&
                !relocated_image.value->conformance().empty() &&
                relocated_image.value->conformance().front()
                        .source_refs.front()
                        .document_uri !=
                    baseline_image.value->conformance().front()
                        .source_refs.front()
                        .document_uri,
            "source relocation changed image identity or lost provenance");

    auto reordered_source = source;
    std::reverse(reordered_source.occurrences.begin(),
                 reordered_source.occurrences.end());
    std::reverse(reordered_source.initial_bindings.begin(),
                 reordered_source.initial_bindings.end());
    std::reverse(reordered_source.bindings.begin(),
                 reordered_source.bindings.end());
    std::reverse(reordered_source.invocation_bindings.begin(),
                 reordered_source.invocation_bindings.end());
    auto reordered_implementation = implementation;
    std::reverse(reordered_implementation.entries.begin(),
                 reordered_implementation.entries.end());
    std::reverse(reordered_implementation.state_layouts.begin(),
                 reordered_implementation.state_layouts.end());
    std::reverse(reordered_implementation.value_layouts.begin(),
                 reordered_implementation.value_layouts.end());
    const auto reordered = gnc::compiler::compile_complete_execution_plan(
        reordered_source, {package});
    require(reordered.succeeded() &&
                reordered.value->plan.source_semantic_hash ==
                    baseline.value->plan.source_semantic_hash &&
                reordered.value->plan.descriptor_semantic_hash ==
                    baseline.value->plan.descriptor_semantic_hash,
            "source container order changed deterministic plan identity");
    const auto reordered_image =
        gnc::compiler::link_complete_execution_plan(
            reordered.value->plan, reordered.value->proofs,
            {reordered_implementation});
    require(reordered_image.succeeded() &&
                reordered_image.value->fingerprint() ==
                    baseline_image.value->fingerprint(),
            "implementation container order changed image fingerprint");

    auto alternate_package = package;
    auto alternate_implementation = implementation;
    const auto alternate_model = std::find_if(
        alternate_package.models.begin(), alternate_package.models.end(),
        [](const auto& model) {
            return model.definition.model_id ==
                   gnc::packages::yyz::kAerodynamicTableModelIdentity;
        });
    require(alternate_model != alternate_package.models.end() &&
                alternate_model->pure_query.has_value(),
            "alternate entry fixture lacks aerodynamic query");
    const auto original_entry_id =
        alternate_model->pure_query->query_entry_id;
    alternate_model->pure_query->query_entry_id += ".alternate";
    const auto alternate_entry = std::find_if(
        alternate_implementation.entries.begin(),
        alternate_implementation.entries.end(), [&](const auto& entry) {
            return entry.entry_id == original_entry_id;
        });
    require(alternate_entry != alternate_implementation.entries.end(),
            "alternate implementation fixture lacks aerodynamic entry");
    alternate_entry->entry_id =
        alternate_model->pure_query->query_entry_id;
    const auto alternate = gnc::compiler::compile_complete_execution_plan(
        source, {alternate_package});
    require(alternate.succeeded() &&
                alternate.value->plan.source_semantic_hash ==
                    baseline.value->plan.source_semantic_hash &&
                alternate.value->plan.descriptor_semantic_hash !=
                    baseline.value->plan.descriptor_semantic_hash,
            "implementation entry identity leaked into source semantics or was absent from the descriptor");
    const auto alternate_image =
        gnc::compiler::link_complete_execution_plan(
            alternate.value->plan, alternate.value->proofs,
            {alternate_implementation});
    require(alternate_image.succeeded() &&
                alternate_image.value->fingerprint() !=
                    baseline_image.value->fingerprint(),
            "alternate exact implementation entry did not relink a distinct image");

    auto alternate_static_package = package;
    auto alternate_static_implementation = implementation;
    const auto guidance_model = std::find_if(
        alternate_static_package.models.begin(),
        alternate_static_package.models.end(), [](const auto& model) {
            return model.definition.model_id ==
                   gnc::packages::yyz::kAltitudePitchGuidanceModelIdentity;
        });
    require(guidance_model != alternate_static_package.models.end() &&
                guidance_model->runtime_component.has_value(),
            "alternate factory fixture lacks its runtime model");
    const auto original_factory_id =
        guidance_model->runtime_component->runtime_cell_factory_id;
    guidance_model->runtime_component->runtime_cell_factory_id +=
        ".alternate";
    const auto factory_entry = std::find_if(
        alternate_static_implementation.entries.begin(),
        alternate_static_implementation.entries.end(),
        [&](const auto& entry) {
            return entry.entry_id == original_factory_id;
        });
    require(factory_entry !=
                    alternate_static_implementation.entries.end(),
            "alternate factory fixture lacks its implementation entry");
    factory_entry->entry_id =
        guidance_model->runtime_component->runtime_cell_factory_id;
    const auto alternate_static =
        gnc::compiler::compile_complete_execution_plan(
            source, {alternate_static_package});
    require(alternate_static.succeeded() &&
                alternate_static.value->plan.source_semantic_hash ==
                    baseline.value->plan.source_semantic_hash &&
                alternate_static.value->plan.descriptor_semantic_hash !=
                    baseline.value->plan.descriptor_semantic_hash,
            "factory implementation identity leaked into source semantics or was absent from the descriptor");
    const auto alternate_static_image =
        gnc::compiler::link_complete_execution_plan(
            alternate_static.value->plan, alternate_static.value->proofs,
            {alternate_static_implementation});
    require(alternate_static_image.succeeded() &&
                alternate_static_image.value->fingerprint() !=
                    baseline_image.value->fingerprint(),
            "alternate factory entry did not relink a distinct image");

    auto poisoned = implementation;
    const auto poisoned_entry = std::find_if(
        poisoned.entries.begin(), poisoned.entries.end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::kRigidDerivativeKernelIdentity.id;
        });
    require(poisoned_entry != poisoned.entries.end(),
            "poison implementation fixture lacks derivative entry");
    poisoned_entry->typed_entry = std::any{&poison_derivative};
    poisoned_entry->link_anchor =
        gnc::model_sdk::make_static_link_anchor<&poison_derivative>();
    poison_call_count = 0;
    const auto poison_link =
        gnc::compiler::link_complete_execution_plan(
            baseline.value->plan, baseline.value->proofs, {poisoned});
    require(poison_link.succeeded() && poison_call_count == 0 &&
                poison_link.value->fingerprint() ==
                    baseline_image.value->fingerprint(),
            "R2 linking invoked a callable or fingerprinted its address");
    const auto poisoned_image_entry = std::find_if(
        poison_link.value->entries().begin(),
        poison_link.value->entries().end(), [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::kRigidDerivativeKernelIdentity.id;
        });
    require(poisoned_image_entry != poison_link.value->entries().end(),
            "poison-linked image lost derivative entry");
    const auto recovered_poison = std::any_cast<
        decltype(&gnc::packages::yyz::RigidDerivativeKernel::evaluate)>(
        &poisoned_image_entry->typed_entry);
    require(recovered_poison != nullptr &&
                *recovered_poison == &poison_derivative,
            "same-signature typed callable substitution was not recoverable");

    auto poisoned_static_entries = implementation;
    const auto poisoned_factory = std::find_if(
        poisoned_static_entries.entries.begin(),
        poisoned_static_entries.entries.end(), [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kAltitudePitchGuidanceRuntimeCellFactoryIdentity.id;
        });
    require(poisoned_factory != poisoned_static_entries.entries.end(),
            "poison factory fixture lacks its exact entry");
    poisoned_factory->typed_entry =
        std::any{&poison_guidance_runtime_cell_factory};
    poisoned_factory->link_anchor =
        gnc::model_sdk::make_static_link_anchor<
            &poison_guidance_runtime_cell_factory>();
    poison_call_count = 0;
    const auto poisoned_static_link =
        gnc::compiler::link_complete_execution_plan(
            baseline.value->plan, baseline.value->proofs,
            {poisoned_static_entries});
    require(poisoned_static_link.succeeded() && poison_call_count == 0 &&
                poisoned_static_link.value->fingerprint() ==
                    baseline_image.value->fingerprint(),
            "R2 linking invoked or address-fingerprinted a RuntimeCell factory");
    const auto recovered_factory = std::find_if(
        poisoned_static_link.value->entries().begin(),
        poisoned_static_link.value->entries().end(), [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::
                       kAltitudePitchGuidanceRuntimeCellFactoryIdentity.id;
        });
    require(recovered_factory !=
                    poisoned_static_link.value->entries().end() &&
                std::any_cast<gnc::packages::yyz::
                                  AltitudePitchGuidanceRuntimeCellFactoryCall>(
                    &recovered_factory->typed_entry) != nullptr,
            "poison-linked factory exact typed entry was not recoverable");

    auto poisoned_codec_entries = implementation;
    const auto poisoned_codec = std::find_if(
        poisoned_codec_entries.entries.begin(),
        poisoned_codec_entries.entries.end(), [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::kRigidStateCodecIdentity.id;
        });
    require(poisoned_codec != poisoned_codec_entries.entries.end(),
            "poison codec fixture lacks its exact entry");
    poisoned_codec->typed_entry = std::any{&poison_rigid_state_codec};
    poisoned_codec->link_anchor =
        gnc::model_sdk::make_static_link_anchor<
            &poison_rigid_state_codec>();
    poison_call_count = 0;
    const auto poisoned_codec_link =
        gnc::compiler::link_complete_execution_plan(
            baseline.value->plan, baseline.value->proofs,
            {poisoned_codec_entries});
    require(poisoned_codec_link.succeeded() && poison_call_count == 0 &&
                poisoned_codec_link.value->fingerprint() ==
                    baseline_image.value->fingerprint(),
            "R2 linking invoked or address-fingerprinted a state codec");
}

void verify_high_value_negatives() {
    auto package = gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source = make_source(package);

    const auto find_occurrence_config_value = [](
        CompleteStaticCompositionSource& candidate,
        CanonicalConfigValueKind kind) -> CanonicalConfigValue* {
        for (auto& occurrence : candidate.occurrences) {
            for (auto& field : occurrence.configuration.fields) {
                if (gnc::model_sdk::canonical_config_value_kind(
                        field.value) == kind) {
                    return &field.value;
                }
            }
        }
        return nullptr;
    };

    auto empty_string_source = source;
    auto* empty_string = find_occurrence_config_value(
        empty_string_source, CanonicalConfigValueKind::String);
    require(empty_string != nullptr,
            "empty-string canonical-config fixture is absent");
    std::get<std::string>(*empty_string).clear();
    const auto empty_string_result =
        gnc::compiler::compile_complete_execution_plan(
            empty_string_source, {package});
    require(!empty_string_result.succeeded() &&
                has_diagnostic(
                    empty_string_result.diagnostics,
                    CompleteDiagnosticCode::InvalidConfiguration),
            "semantic-bytes@3 accepted an empty canonical string");

    auto empty_enum_source = source;
    auto* empty_enum = find_occurrence_config_value(
        empty_enum_source, CanonicalConfigValueKind::Enum);
    require(empty_enum != nullptr,
            "empty-enum canonical-config fixture is absent");
    std::get<CanonicalEnumValue>(*empty_enum).token.clear();
    const auto empty_enum_result =
        gnc::compiler::compile_complete_execution_plan(
            empty_enum_source, {package});
    require(!empty_enum_result.succeeded() &&
                has_diagnostic(
                    empty_enum_result.diagnostics,
                    CompleteDiagnosticCode::InvalidConfiguration),
            "semantic-bytes@3 accepted an empty canonical enum token");

    auto negative_zero_source = source;
    auto* negative_zero = find_occurrence_config_value(
        negative_zero_source, CanonicalConfigValueKind::Float64);
    require(negative_zero != nullptr,
            "negative-zero canonical-config fixture is absent");
    std::get<double>(*negative_zero) = -0.0;
    const auto negative_zero_result =
        gnc::compiler::compile_complete_execution_plan(
            negative_zero_source, {package});
    require(!negative_zero_result.succeeded() &&
                has_diagnostic(
                    negative_zero_result.diagnostics,
                    CompleteDiagnosticCode::InvalidConfiguration),
            "semantic-bytes@3 accepted canonical negative zero");

    auto no_output_package = package;
    const auto no_output_model = std::find_if(
        no_output_package.models.begin(), no_output_package.models.end(),
        [](const auto& model) {
            return model.definition.model_id ==
                   gnc::packages::yyz::kUniformEnvironmentModelIdentity;
        });
    require(no_output_model != no_output_package.models.end(),
            "zero-output provider fixture lacks environment query");
    no_output_model->ports.erase(
        std::remove_if(no_output_model->ports.begin(),
                       no_output_model->ports.end(), [](const auto& port) {
                           return port.direction ==
                                  gnc::model_sdk::StaticPortDirection::Output;
                       }),
        no_output_model->ports.end());
    const auto no_output = gnc::compiler::compile_complete_execution_plan(
        source, {no_output_package});
    require(!no_output.succeeded() &&
                has_diagnostic(no_output.diagnostics,
                               CompleteDiagnosticCode::InvalidCatalog),
            "complete planning accepted a selected zero-output provider");

    auto multiple_output_package = package;
    const auto multiple_output_model = std::find_if(
        multiple_output_package.models.begin(),
        multiple_output_package.models.end(), [](const auto& model) {
            return model.definition.model_id ==
                   gnc::packages::yyz::kUniformEnvironmentModelIdentity;
        });
    const auto existing_output =
        multiple_output_model == multiple_output_package.models.end()
            ? std::vector<gnc::model_sdk::StaticPortDescriptor>::iterator{}
            : std::find_if(
                  multiple_output_model->ports.begin(),
                  multiple_output_model->ports.end(), [](const auto& port) {
                      return port.direction ==
                             gnc::model_sdk::StaticPortDirection::Output;
                  });
    require(multiple_output_model != multiple_output_package.models.end() &&
                existing_output != multiple_output_model->ports.end(),
            "multiple-output provider fixture lacks environment output");
    auto duplicate_output = *existing_output;
    duplicate_output.port_id += ".second";
    multiple_output_model->ports.push_back(std::move(duplicate_output));
    const auto multiple_output =
        gnc::compiler::compile_complete_execution_plan(
            source, {multiple_output_package});
    require(!multiple_output.succeeded() &&
                has_diagnostic(multiple_output.diagnostics,
                               CompleteDiagnosticCode::InvalidCatalog),
            "complete planning accepted a selected multi-output provider");

    auto missing_provider = source;
    missing_provider.bindings.erase(missing_provider.bindings.begin());
    const auto missing = gnc::compiler::compile_complete_execution_plan(
        missing_provider, {package});
    require(!missing.succeeded() &&
                has_diagnostic(missing.diagnostics,
                               CompleteDiagnosticCode::MissingProvider),
            "missing provider negative did not fail precisely");

    auto multiple_provider = source;
    auto duplicate_binding = multiple_provider.bindings.front();
    duplicate_binding.binding_id += ".duplicate";
    duplicate_binding.source = ref("bindings/duplicate");
    multiple_provider.bindings.push_back(std::move(duplicate_binding));
    const auto multiple = gnc::compiler::compile_complete_execution_plan(
        multiple_provider, {package});
    require(!multiple.succeeded() &&
                has_diagnostic(multiple.diagnostics,
                               CompleteDiagnosticCode::MultipleProviders),
            "multiple provider negative did not fail precisely");

    auto missing_auth = source;
    missing_auth.invocation_bindings.pop_back();
    const auto auth = gnc::compiler::compile_complete_execution_plan(
        missing_auth, {package});
    require(!auth.succeeded() &&
                has_diagnostic(
                    auth.diagnostics,
                    CompleteDiagnosticCode::MissingInvocationAuthorization),
            "missing invocation authorization did not fail precisely");

    auto missing_result_flow = source;
    require(!missing_result_flow.invocation_bindings.empty(),
            "result-flow negative fixture lacks an invocation");
    const auto& result_invocation =
        missing_result_flow.invocation_bindings.front();
    const auto result_binding = std::find_if(
        missing_result_flow.bindings.begin(),
        missing_result_flow.bindings.end(), [&](const auto& binding) {
            return binding.provider_occurrence_id ==
                       result_invocation.provider_occurrence_id &&
                   binding.consumer_occurrence_id ==
                       result_invocation.caller_occurrence_id;
        });
    require(result_binding != missing_result_flow.bindings.end(),
            "result-flow negative fixture lacks its response Binding");
    const auto result_binding_value = *result_binding;
    missing_result_flow.bindings.erase(result_binding);
    const auto missing_result =
        gnc::compiler::compile_complete_execution_plan(
            missing_result_flow, {package});
    require(!missing_result.succeeded() &&
                has_diagnostic(
                    missing_result.diagnostics,
                    CompleteDiagnosticCode::MissingInvocationResultFlow),
            "missing invocation result-flow Binding did not fail precisely");

    auto ambiguous_result_flow = source;
    auto duplicate_result = result_binding_value;
    duplicate_result.binding_id += ".ambiguous-result";
    duplicate_result.source = ref("bindings/ambiguous-result");
    ambiguous_result_flow.bindings.push_back(std::move(duplicate_result));
    const auto ambiguous_result =
        gnc::compiler::compile_complete_execution_plan(
            ambiguous_result_flow, {package});
    require(!ambiguous_result.succeeded() &&
                has_diagnostic(
                    ambiguous_result.diagnostics,
                    CompleteDiagnosticCode::AmbiguousInvocationResultFlow),
            "ambiguous invocation result-flow Binding did not fail precisely");

    auto shallow_evaluator_history = source;
    require(shallow_evaluator_history.evaluator_histories.size() == 1U,
            "evaluator-history negative fixture is absent");
    shallow_evaluator_history.evaluator_histories.front()
        .committed_history_depth = 1U;
    const auto shallow_history =
        gnc::compiler::compile_complete_execution_plan(
            shallow_evaluator_history, {package});
    require(!shallow_history.succeeded() &&
                has_diagnostic(
                    shallow_history.diagnostics,
                    CompleteDiagnosticCode::InvalidEvaluatorHistory),
            "wrong evaluator history depth did not fail exact shape validation");

    auto incomplete_evaluator_history = source;
    incomplete_evaluator_history.evaluator_histories.front()
        .owner_occurrence_ids.pop_back();
    const auto incomplete_history =
        gnc::compiler::compile_complete_execution_plan(
            incomplete_evaluator_history, {package});
    require(!incomplete_history.succeeded() &&
                has_diagnostic(
                    incomplete_history.diagnostics,
                    CompleteDiagnosticCode::InvalidEvaluatorHistory),
            "single-owner evaluator history did not fail exact shape validation");

    auto reordered_evaluator_members = source;
    std::reverse(reordered_evaluator_members.evaluator_histories.front()
                     .owner_occurrence_ids.begin(),
                 reordered_evaluator_members.evaluator_histories.front()
                     .owner_occurrence_ids.end());
    const auto reordered_history =
        gnc::compiler::compile_complete_execution_plan(
            reordered_evaluator_members, {package});
    require(!reordered_history.succeeded() &&
                has_diagnostic(
                    reordered_history.diagnostics,
                    CompleteDiagnosticCode::InvalidEvaluatorHistory),
            "reordered evaluator history members did not fail exact shape validation");

    auto duplicate_closure_invocation = source;
    require(!duplicate_closure_invocation.integration_scopes.empty() &&
                duplicate_closure_invocation.integration_scopes.front()
                        .closure_invocation_ids.size() == 1U,
            "integration-scope closure invocation fixture is not exact");
    duplicate_closure_invocation.integration_scopes.front()
        .closure_invocation_ids.push_back(
            duplicate_closure_invocation.integration_scopes.front()
                .closure_invocation_ids.front());
    const auto duplicate_closure =
        gnc::compiler::compile_complete_execution_plan(
            duplicate_closure_invocation, {package});
    require(!duplicate_closure.succeeded() &&
                has_diagnostic(
                    duplicate_closure.diagnostics,
                    CompleteDiagnosticCode::InvalidIntegrationScope),
            "duplicate FrozenInterval closure invocation id did not fail integration-scope validation");

    auto multiple_owner = source;
    const auto rigid_occurrence = std::find_if(
        multiple_owner.occurrences.begin(),
        multiple_owner.occurrences.end(), [](const auto& occurrence) {
            return occurrence.model_id ==
                   gnc::packages::yyz::kRigidStepModelIdentity;
        });
    require(rigid_occurrence != multiple_owner.occurrences.end(),
            "multiple-owner fixture lacks rigid occurrence");
    auto duplicate_owner = *rigid_occurrence;
    duplicate_owner.occurrence_id += ".duplicate-owner";
    duplicate_owner.source = ref("occurrences/duplicate-owner");
    duplicate_owner.subject_source =
        ref("occurrences/duplicate-owner/subject");
    duplicate_owner.scope_source =
        ref("occurrences/duplicate-owner/scope");
    duplicate_owner.placement_source =
        ref("occurrences/duplicate-owner/placement");
    duplicate_owner.configuration_source =
        ref("occurrences/duplicate-owner/config");
    for (auto& field : duplicate_owner.configuration_field_sources) {
        field.source = ref("occurrences/duplicate-owner/config/" +
                           field.field_id);
    }
    for (auto& asset : duplicate_owner.asset_bindings) {
        asset.source = ref("occurrences/duplicate-owner/assets/" +
                           asset.role);
    }
    multiple_owner.occurrences.push_back(std::move(duplicate_owner));
    const auto owner = gnc::compiler::compile_complete_execution_plan(
        multiple_owner, {package});
    require(!owner.succeeded() &&
                has_diagnostic(owner.diagnostics,
                               CompleteDiagnosticCode::MultipleStateOwners),
            "multiple scoped state owner negative did not fail precisely");

    auto scope_mismatch = source;
    constexpr std::string_view kSecondEntity =
        "vehicle.fixture.yyz.second@1";
    scope_mismatch.entities.push_back(
        {std::string(kSecondEntity),
         gnc::compiler::EntityLifecycle::ActiveAtInitialize,
         ref("entities/second/identity"),
         ref("entities/second/lifecycle")});
    const ScopeKey second_scope{ScopeKind::Vehicle,
                                std::string(kSecondEntity)};
    scope_mismatch.scopes.push_back(
        {second_scope, ref("scopes/second")});
    const auto controller_occurrence = std::find_if(
        scope_mismatch.occurrences.begin(),
        scope_mismatch.occurrences.end(), [](const auto& occurrence) {
            return occurrence.model_id ==
                   gnc::packages::yyz::kPitchMomentControllerModelIdentity;
        });
    require(controller_occurrence != scope_mismatch.occurrences.end(),
            "scope mismatch fixture lacks controller occurrence");
    controller_occurrence->subject_entity_id = std::string(kSecondEntity);
    controller_occurrence->subject_source =
        ref("occurrences/controller/second-subject");
    controller_occurrence->scope = second_scope;
    controller_occurrence->scope_source =
        ref("occurrences/controller/second-scope");
    const auto scope = gnc::compiler::compile_complete_execution_plan(
        scope_mismatch, {package});
    require(!scope.succeeded() &&
                has_diagnostic(scope.diagnostics,
                               CompleteDiagnosticCode::ScopeMismatch),
            "cross-scope provider negative did not fail precisely");

    auto incomplete_transaction = source;
    require(!incomplete_transaction.transactions.empty() &&
                incomplete_transaction.transactions.front()
                        .owner_occurrence_ids.size() == 2U,
            "transaction negative fixture lacks two owners");
    incomplete_transaction.transactions.front()
        .owner_occurrence_ids.pop_back();
    const auto transaction =
        gnc::compiler::compile_complete_execution_plan(
            incomplete_transaction, {package});
    require(!transaction.succeeded() &&
                has_diagnostic(transaction.diagnostics,
                               CompleteDiagnosticCode::InvalidTransaction),
            "incomplete atomic transaction negative did not fail precisely");

    const auto positive = gnc::compiler::compile_complete_execution_plan(
        source, {package});
    require(positive.succeeded(), "positive plan for link negatives failed");
    const auto baseline_link =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {implementation});
    require(baseline_link.succeeded(),
            "baseline image for link negatives failed");
    const auto expect_plan_conformance_failure =
        [&](gnc::compiler::CompleteExecutionPlanDescriptor candidate,
            std::string_view message) {
            candidate.descriptor_semantic_hash =
                gnc::compiler::complete_plan_detail::descriptor_hash(
                    candidate);
            const auto candidate_proofs =
                gnc::compiler::complete_plan_detail::derive_proofs(
                    candidate);
            const auto candidate_link =
                gnc::compiler::link_complete_execution_plan(
                    candidate, candidate_proofs, {implementation});
            require(!candidate_link.succeeded() &&
                        (has_diagnostic(
                             candidate_link.diagnostics,
                             CompleteDiagnosticCode::
                                 SourceImageConformanceFailure) ||
                         has_diagnostic(
                             candidate_link.diagnostics,
                             CompleteDiagnosticCode::
                                 ImplementationMismatch)),
                    message);
        };

    auto query_storage_route = positive.value->plan;
    const auto caller_local_invocation = std::find_if(
        query_storage_route.invocation_bindings.begin(),
        query_storage_route.invocation_bindings.end(),
        [](const auto& invocation) {
            return invocation.result_route ==
                   gnc::contracts::InvocationResultRoute::CallerLocal;
        });
    require(caller_local_invocation !=
                query_storage_route.invocation_bindings.end(),
            "caller-local route mutation fixture is absent");
    caller_local_invocation->result_route =
        gnc::contracts::InvocationResultRoute::HeldInterval;
    expect_plan_conformance_failure(
        std::move(query_storage_route),
        "caller-local Query accepted a fabricated held-storage route");

    auto closure_local_route = positive.value->plan;
    const auto held_invocation = std::find_if(
        closure_local_route.invocation_bindings.begin(),
        closure_local_route.invocation_bindings.end(),
        [](const auto& invocation) {
            return invocation.result_route ==
                   gnc::contracts::InvocationResultRoute::HeldInterval;
        });
    require(held_invocation !=
                closure_local_route.invocation_bindings.end(),
            "held Closure route mutation fixture is absent");
    held_invocation->result_route =
        gnc::contracts::InvocationResultRoute::CallerLocal;
    expect_plan_conformance_failure(
        std::move(closure_local_route),
        "FrozenInterval Closure accepted a caller-local result route");

    auto missing_state_codec_plan = positive.value->plan;
    require(!missing_state_codec_plan.state_blocks.empty(),
            "state-codec plan mutation fixture is absent");
    missing_state_codec_plan.state_blocks.front()
        .codec_entry_requirement_id.clear();
    expect_plan_conformance_failure(
        std::move(missing_state_codec_plan),
        "missing state codec plan reference did not fail before image lookup");

    auto swapped_slot_codec_plan = positive.value->plan;
    const auto first_value_slot = std::find_if(
        swapped_slot_codec_plan.slots.begin(),
        swapped_slot_codec_plan.slots.end(), [](const auto& slot) {
            return slot.kind == gnc::compiler::CompleteSlotKind::PortValue;
        });
    const auto second_value_slot =
        first_value_slot == swapped_slot_codec_plan.slots.end()
            ? swapped_slot_codec_plan.slots.end()
            : std::find_if(
                  first_value_slot + 1,
                  swapped_slot_codec_plan.slots.end(),
                  [&](const auto& slot) {
                      return slot.kind ==
                                 gnc::compiler::CompleteSlotKind::PortValue &&
                             slot.contract_id !=
                                 first_value_slot->contract_id &&
                             slot.codec_entry_requirement_id !=
                                 first_value_slot
                                     ->codec_entry_requirement_id;
                  });
    require(first_value_slot != swapped_slot_codec_plan.slots.end() &&
                second_value_slot != swapped_slot_codec_plan.slots.end(),
            "slot-codec plan mutation fixture lacks distinct values");
    first_value_slot->codec_entry_requirement_id =
        second_value_slot->codec_entry_requirement_id;
    expect_plan_conformance_failure(
        std::move(swapped_slot_codec_plan),
        "cross-contract slot codec plan reference passed exact conformance");

    auto missing_resource_plan = positive.value->plan;
    require(!missing_resource_plan.runtime_components.empty(),
            "resource-plan mutation fixture is absent");
    missing_resource_plan.runtime_components.front().resource_plan_id.clear();
    expect_plan_conformance_failure(
        std::move(missing_resource_plan),
        "missing RuntimeComponent resource plan did not fail before image lookup");

    auto coordinated_candidate_swap = positive.value->plan;
    require(coordinated_candidate_swap.transactions.size() == 1U &&
                coordinated_candidate_swap.transactions.front()
                        .candidates.size() == 2U,
            "transaction candidate swap fixture is absent");
    auto& swapped_candidates =
        coordinated_candidate_swap.transactions.front().candidates;
    std::swap(swapped_candidates.at(0U).candidate_state_slot_id,
              swapped_candidates.at(1U).candidate_state_slot_id);
    std::swap(swapped_candidates.at(0U).writer_token_id,
              swapped_candidates.at(1U).writer_token_id);
    std::swap(swapped_candidates.at(0U).producer,
              swapped_candidates.at(1U).producer);
    expect_plan_conformance_failure(
        std::move(coordinated_candidate_swap),
        "coordinated rigid/mass candidate-writer-producer swap passed transaction conformance");

    auto missing_transaction_branch = positive.value->plan;
    require(!missing_transaction_branch.transactions.empty(),
            "transaction branch mutation fixture is absent");
    missing_transaction_branch.transactions.front().branches.pop_back();
    expect_plan_conformance_failure(
        std::move(missing_transaction_branch),
        "transaction with no failure branch passed exact conformance");

    auto invalid_transaction_seal = positive.value->plan;
    require(!invalid_transaction_seal.transactions.empty() &&
                !invalid_transaction_seal.transactions.front()
                     .branches.empty(),
            "transaction seal mutation fixture is absent");
    invalid_transaction_seal.transactions.front().branches.front()
        .result_seal_after_observation = true;
    invalid_transaction_seal.transactions.front().branches.front()
        .tick_delta = 0;
    expect_plan_conformance_failure(
        std::move(invalid_transaction_seal),
        "continue branch accepted a result seal or non-advancing boundary relation");

    auto invalid_preparation_lifecycle = positive.value->plan;
    require(!invalid_preparation_lifecycle.preparation_inputs.empty(),
            "preparation lifecycle mutation fixture is absent");
    invalid_preparation_lifecycle.preparation_inputs.front().cache_policy =
        gnc::contracts::PreparedModelCachePolicy::Unspecified;
    expect_plan_conformance_failure(
        std::move(invalid_preparation_lifecycle),
        "selected provider accepted a non-Session-owned/no-cache lifecycle");

    auto coordinated_lifecycle_order = positive.value->plan;
    require(coordinated_lifecycle_order.preparation_inputs.size() >= 2U &&
                coordinated_lifecycle_order.lifecycle
                        .preparation_input_ids.size() >= 2U,
            "lifecycle order mutation fixture lacks two preparations");
    std::swap(coordinated_lifecycle_order.preparation_inputs.at(0U).order,
              coordinated_lifecycle_order.preparation_inputs.at(1U).order);
    std::swap(coordinated_lifecycle_order.lifecycle
                  .preparation_input_ids.at(0U),
              coordinated_lifecycle_order.lifecycle
                  .preparation_input_ids.at(1U));
    expect_plan_conformance_failure(
        std::move(coordinated_lifecycle_order),
        "coordinated preparation order/list swap bypassed canonical lifecycle conformance");

    auto synchronized_history_package = package;
    const auto synchronized_evaluator = std::find_if(
        synchronized_history_package.models.begin(),
        synchronized_history_package.models.end(),
        [](const auto& model) { return is_terminal_evaluator(model); });
    require(synchronized_evaluator !=
                    synchronized_history_package.models.end() &&
                synchronized_evaluator->runtime_component.has_value() &&
                synchronized_evaluator->runtime_component
                    ->evaluator_history_shape.has_value() &&
                synchronized_evaluator->runtime_component
                        ->evaluator_history_shape->depth > 1U,
            "evaluator implementation-witness negative fixture is absent");
    --synchronized_evaluator->runtime_component
          ->evaluator_history_shape->depth;
    const auto synchronized_history_source =
        make_source(synchronized_history_package);
    const auto synchronized_history_compilation =
        gnc::compiler::compile_complete_execution_plan(
            synchronized_history_source,
            {synchronized_history_package});
    require(synchronized_history_compilation.succeeded(),
            "descriptor/source synchronized evaluator history did not compile");
    const auto stale_history_implementation =
        gnc::compiler::link_complete_execution_plan(
            synchronized_history_compilation.value->plan,
            synchronized_history_compilation.value->proofs,
            {implementation});
    require(!stale_history_implementation.succeeded() &&
                has_diagnostic(
                    stale_history_implementation.diagnostics,
                    CompleteDiagnosticCode::ImplementationMismatch),
            "descriptor/source evaluator history change passed with a stale implementation witness");
    auto mutated_proofs = positive.value->proofs;
    require(!mutated_proofs.records.empty() &&
                !mutated_proofs.records.front().premises.empty(),
            "proof fixture unexpectedly empty");
    mutated_proofs.records.front().premises.front() += ".mutated";
    mutated_proofs.proof_index_hash =
        gnc::compiler::complete_plan_detail::proof_hash(mutated_proofs);
    const auto proof_failure = gnc::compiler::link_complete_execution_plan(
        positive.value->plan, mutated_proofs, {implementation});
    require(!proof_failure.succeeded() &&
                has_diagnostic(proof_failure.diagnostics,
                               CompleteDiagnosticCode::InvalidProofReference),
            "proof premise mutation did not fail exact linker validation");

    auto invalid_result_plan = positive.value->plan;
    const auto invalid_result_invocation = std::find_if(
        invalid_result_plan.invocation_bindings.begin(),
        invalid_result_plan.invocation_bindings.end(), [](const auto& value) {
            return value.result_route ==
                   gnc::contracts::InvocationResultRoute::HeldInterval;
        });
    require(invalid_result_invocation !=
                    invalid_result_plan.invocation_bindings.end() &&
                !invalid_result_plan.state_blocks.empty(),
            "result-flow link mutation fixture is incomplete");
    invalid_result_invocation->result_storage_slot_id =
        invalid_result_plan.state_blocks.front().committed_slot_id;
    invalid_result_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_result_plan);
    const auto invalid_result_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_result_plan);
    const auto invalid_result_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_result_plan, invalid_result_proofs,
            {implementation});
    require(!invalid_result_link.succeeded() &&
                has_diagnostic(
                    invalid_result_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "tampered invocation result-flow slot passed exact linker conformance");

    auto invalid_ordinal_plan = positive.value->plan;
    require(!invalid_ordinal_plan.invocation_bindings.empty(),
            "invocation ordinal mutation fixture is absent");
    ++invalid_ordinal_plan.invocation_bindings.front().requirement_ordinal;
    invalid_ordinal_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_ordinal_plan);
    const auto invalid_ordinal_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_ordinal_plan);
    const auto invalid_ordinal_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_ordinal_plan, invalid_ordinal_proofs,
            {implementation});
    require(!invalid_ordinal_link.succeeded() &&
                has_diagnostic(
                    invalid_ordinal_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "tampered package-authored invocation ordinal passed exact link conformance");

    auto invalid_invocation_cardinality_plan = positive.value->plan;
    require(!invalid_invocation_cardinality_plan.invocation_bindings.empty(),
            "invocation cardinality mutation fixture is absent");
    invalid_invocation_cardinality_plan.invocation_bindings.front()
        .cardinality = gnc::model_sdk::PortCardinality::OneOrMore;
    invalid_invocation_cardinality_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_invocation_cardinality_plan);
    const auto invalid_invocation_cardinality_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_invocation_cardinality_plan);
    const auto invalid_invocation_cardinality_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_invocation_cardinality_plan,
            invalid_invocation_cardinality_proofs, {implementation});
    require(!invalid_invocation_cardinality_link.succeeded() &&
                has_diagnostic(
                    invalid_invocation_cardinality_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "tampered package-authored invocation cardinality passed exact link conformance");

    auto coordinated_ordinal_swap_plan = positive.value->plan;
    const auto find_plan_invocation_for_requirement =
        [&](auto& candidate_plan, std::string_view id) {
        return std::find_if(
            candidate_plan.invocation_bindings.begin(),
            candidate_plan.invocation_bindings.end(),
            [&](const auto& invocation) {
                return invocation.requirement_id == id;
            });
    };
    auto coordinated_environment = find_plan_invocation_for_requirement(
        coordinated_ordinal_swap_plan, "environment-query");
    auto coordinated_aerodynamic = find_plan_invocation_for_requirement(
        coordinated_ordinal_swap_plan, "aerodynamic-table-query");
    require(coordinated_environment !=
                coordinated_ordinal_swap_plan.invocation_bindings.end() &&
                coordinated_aerodynamic !=
                    coordinated_ordinal_swap_plan.invocation_bindings.end() &&
                coordinated_environment->caller_callsite_id ==
                    coordinated_aerodynamic->caller_callsite_id,
            "coordinated invocation-order mutation fixture is incomplete");
    auto coordinated_callsite = std::find_if(
        coordinated_ordinal_swap_plan.runtime_callsites.begin(),
        coordinated_ordinal_swap_plan.runtime_callsites.end(),
        [&](const auto& callsite) {
            return callsite.callsite_id ==
                   coordinated_environment->caller_callsite_id;
        });
    require(coordinated_callsite !=
                coordinated_ordinal_swap_plan.runtime_callsites.end(),
            "coordinated invocation-order mutation lacks its caller");
    auto environment_id_position = std::find(
        coordinated_callsite->invocation_binding_ids.begin(),
        coordinated_callsite->invocation_binding_ids.end(),
        coordinated_environment->invocation_id);
    auto aerodynamic_id_position = std::find(
        coordinated_callsite->invocation_binding_ids.begin(),
        coordinated_callsite->invocation_binding_ids.end(),
        coordinated_aerodynamic->invocation_id);
    require(environment_id_position !=
                coordinated_callsite->invocation_binding_ids.end() &&
                aerodynamic_id_position !=
                    coordinated_callsite->invocation_binding_ids.end(),
            "coordinated invocation-order mutation lacks ordered ids");
    std::swap(coordinated_environment->requirement_ordinal,
              coordinated_aerodynamic->requirement_ordinal);
    std::iter_swap(environment_id_position, aerodynamic_id_position);
    coordinated_ordinal_swap_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            coordinated_ordinal_swap_plan);
    const auto coordinated_ordinal_swap_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            coordinated_ordinal_swap_plan);
    const auto coordinated_ordinal_swap_link =
        gnc::compiler::link_complete_execution_plan(
            coordinated_ordinal_swap_plan,
            coordinated_ordinal_swap_proofs, {implementation});
    require(!coordinated_ordinal_swap_link.succeeded() &&
                has_diagnostic(
                    coordinated_ordinal_swap_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "coordinated invocation ordinal/id-list permutation bypassed the package-authored witness");

    auto mismatched_provider_plan = positive.value->plan;
    auto mismatched_environment = find_plan_invocation_for_requirement(
        mismatched_provider_plan, "environment-query");
    auto mismatched_aerodynamic = find_plan_invocation_for_requirement(
        mismatched_provider_plan, "aerodynamic-table-query");
    require(mismatched_environment !=
                mismatched_provider_plan.invocation_bindings.end() &&
                mismatched_aerodynamic !=
                    mismatched_provider_plan.invocation_bindings.end(),
            "provider-plan mutation fixture lacks two query invocations");
    mismatched_environment->provider_plan_id =
        mismatched_aerodynamic->provider_plan_id;
    mismatched_provider_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            mismatched_provider_plan);
    const auto mismatched_provider_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            mismatched_provider_plan);
    const auto mismatched_provider_link =
        gnc::compiler::link_complete_execution_plan(
            mismatched_provider_plan, mismatched_provider_proofs,
            {implementation});
    require(!mismatched_provider_link.succeeded() &&
                has_diagnostic(
                    mismatched_provider_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "cross-occurrence provider plan passed exact invocation conformance");

    auto mismatched_preparation_plan = positive.value->plan;
    auto mismatched_preparation_environment =
        find_plan_invocation_for_requirement(
            mismatched_preparation_plan, "environment-query");
    auto mismatched_preparation_aerodynamic =
        find_plan_invocation_for_requirement(
            mismatched_preparation_plan, "aerodynamic-table-query");
    require(mismatched_preparation_environment !=
                mismatched_preparation_plan.invocation_bindings.end() &&
                mismatched_preparation_aerodynamic !=
                    mismatched_preparation_plan.invocation_bindings.end(),
            "provider-preparation mutation fixture lacks two query invocations");
    auto mismatched_preparation_environment_query = std::find_if(
        mismatched_preparation_plan.queries.begin(),
        mismatched_preparation_plan.queries.end(), [&](const auto& query) {
            return query.query_plan_id ==
                   mismatched_preparation_environment->provider_plan_id;
        });
    auto mismatched_preparation_aerodynamic_query = std::find_if(
        mismatched_preparation_plan.queries.begin(),
        mismatched_preparation_plan.queries.end(), [&](const auto& query) {
            return query.query_plan_id ==
                   mismatched_preparation_aerodynamic->provider_plan_id;
        });
    require(mismatched_preparation_environment_query !=
                mismatched_preparation_plan.queries.end() &&
                mismatched_preparation_aerodynamic_query !=
                    mismatched_preparation_plan.queries.end(),
            "provider-preparation mutation fixture lacks two query plans");
    mismatched_preparation_environment_query->preparation_input_ref =
        mismatched_preparation_aerodynamic_query->preparation_input_ref;
    mismatched_preparation_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            mismatched_preparation_plan);
    const auto mismatched_preparation_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            mismatched_preparation_plan);
    const auto mismatched_preparation_link =
        gnc::compiler::link_complete_execution_plan(
            mismatched_preparation_plan, mismatched_preparation_proofs,
            {implementation});
    require(!mismatched_preparation_link.succeeded() &&
                has_diagnostic(
                    mismatched_preparation_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "cross-occurrence preparation input passed exact provider conformance");

    auto invalid_workspace_plan = positive.value->plan;
    require(!invalid_workspace_plan.queries.empty(),
            "provider-workspace mutation fixture lacks a query plan");
    invalid_workspace_plan.queries.front().workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::Unspecified;
    invalid_workspace_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_workspace_plan);
    const auto invalid_workspace_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_workspace_plan);
    const auto invalid_workspace_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_workspace_plan, invalid_workspace_proofs,
            {implementation});
    require(!invalid_workspace_link.succeeded() &&
                has_diagnostic(
                    invalid_workspace_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "invalid provider workspace requirement passed exact provider conformance");

    auto invalid_callsite_request_plan = positive.value->plan;
    auto invalid_callsite_request_environment =
        find_plan_invocation_for_requirement(
            invalid_callsite_request_plan, "environment-query");
    require(invalid_callsite_request_environment !=
                invalid_callsite_request_plan.invocation_bindings.end(),
            "runtime callsite request-contract mutation fixture lacks its invocation");
    auto invalid_callsite_request = std::find_if(
        invalid_callsite_request_plan.runtime_callsites.begin(),
        invalid_callsite_request_plan.runtime_callsites.end(),
        [&](const auto& callsite) {
            return callsite.callsite_id ==
                   invalid_callsite_request_environment->caller_callsite_id;
        });
    require(invalid_callsite_request !=
                invalid_callsite_request_plan.runtime_callsites.end(),
            "runtime callsite request-contract mutation fixture lacks its caller");
    invalid_callsite_request->request_contract_id =
        std::string(
            gnc::packages::yyz::kRigidDerivativeInputContractIdentity);
    invalid_callsite_request_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_callsite_request_plan);
    const auto invalid_callsite_request_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_callsite_request_plan);
    const auto invalid_callsite_request_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_callsite_request_plan,
            invalid_callsite_request_proofs, {implementation});
    require(!invalid_callsite_request_link.succeeded() &&
                has_diagnostic(
                    invalid_callsite_request_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "tampered runtime callsite request contract passed exact entry conformance");

    auto invalid_held_form_plan = positive.value->plan;
    require(!invalid_held_form_plan.integration_scopes.empty() &&
                !invalid_held_form_plan.state_blocks.empty(),
            "held-form mutation fixture is absent");
    invalid_held_form_plan.integration_scopes.front().held_form_slot_id =
        invalid_held_form_plan.state_blocks.front().committed_slot_id;
    invalid_held_form_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            invalid_held_form_plan);
    const auto invalid_held_form_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            invalid_held_form_plan);
    const auto invalid_held_form_link =
        gnc::compiler::link_complete_execution_plan(
            invalid_held_form_plan, invalid_held_form_proofs,
            {implementation});
    require(!invalid_held_form_link.succeeded() &&
                has_diagnostic(
                    invalid_held_form_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "tampered IntegrationScope held-form alias passed exact link conformance");

    auto swapped_factory_plan = positive.value->plan;
    require(swapped_factory_plan.runtime_components.size() >= 2U,
            "factory-swap mutation fixture lacks two components");
    std::swap(swapped_factory_plan.runtime_components.at(0)
                  .runtime_cell_factory_entry_requirement_id,
              swapped_factory_plan.runtime_components.at(1)
                  .runtime_cell_factory_entry_requirement_id);
    swapped_factory_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            swapped_factory_plan);
    const auto swapped_factory_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            swapped_factory_plan);
    const auto swapped_factory_link =
        gnc::compiler::link_complete_execution_plan(
            swapped_factory_plan, swapped_factory_proofs,
            {implementation});
    require(!swapped_factory_link.succeeded() &&
                has_diagnostic(
                    swapped_factory_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "cross-component RuntimeCell factory swap passed exact link conformance");

    auto missing_factory_plan = positive.value->plan;
    require(!missing_factory_plan.runtime_components.empty(),
            "missing factory-reference mutation fixture is absent");
    missing_factory_plan.runtime_components.front()
        .runtime_cell_factory_entry_requirement_id.clear();
    missing_factory_plan.descriptor_semantic_hash =
        gnc::compiler::complete_plan_detail::descriptor_hash(
            missing_factory_plan);
    const auto missing_factory_proofs =
        gnc::compiler::complete_plan_detail::derive_proofs(
            missing_factory_plan);
    const auto missing_factory_link =
        gnc::compiler::link_complete_execution_plan(
            missing_factory_plan, missing_factory_proofs,
            {implementation});
    require(!missing_factory_link.succeeded() &&
                has_diagnostic(
                    missing_factory_link.diagnostics,
                    CompleteDiagnosticCode::SourceImageConformanceFailure),
            "missing RuntimeCell factory plan reference did not fail closed before image lookup");

    auto missing_entry = implementation;
    missing_entry.entries.pop_back();
    const auto entry_failure = gnc::compiler::link_complete_execution_plan(
        positive.value->plan, positive.value->proofs, {missing_entry});
    require(!entry_failure.succeeded() &&
                has_diagnostic(entry_failure.diagnostics,
                               CompleteDiagnosticCode::MissingImplementationEntry),
            "missing exact implementation entry did not fail link");

    auto wrong_signature = implementation;
    wrong_signature.entries.front().signature_id += ".wrong";
    const auto signature_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_signature});
    require(!signature_failure.succeeded() &&
                has_diagnostic(signature_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "implementation signature mismatch did not fail link");

    auto wrong_call_shape = implementation;
    wrong_call_shape.entries.front().call_shape_id += ".wrong";
    const auto call_shape_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_call_shape});
    require(!call_shape_failure.succeeded() &&
                has_diagnostic(call_shape_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "descriptor/implementation callable-shape mismatch did not fail link");

    auto missing_definition_builder = implementation;
    const auto definition_builder = std::find_if(
        missing_definition_builder.entries.begin(),
        missing_definition_builder.entries.end(),
        [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::DefinitionBuilder;
        });
    require(definition_builder !=
                missing_definition_builder.entries.end(),
            "definition-builder negative fixture lacks a builder entry");
    missing_definition_builder.entries.erase(definition_builder);
    const auto definition_builder_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_definition_builder});
    require(!definition_builder_failure.succeeded() &&
                has_diagnostic(
                    definition_builder_failure.diagnostics,
                    CompleteDiagnosticCode::MissingImplementationEntry),
            "missing runtime definition builder did not fail exact link");

    auto missing_runtime_factory = implementation;
    const auto runtime_factory = std::find_if(
        missing_runtime_factory.entries.begin(),
        missing_runtime_factory.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::RuntimeCellFactory;
        });
    require(runtime_factory != missing_runtime_factory.entries.end(),
            "runtime-factory negative fixture lacks a factory entry");
    missing_runtime_factory.entries.erase(runtime_factory);
    const auto runtime_factory_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_runtime_factory});
    require(!runtime_factory_failure.succeeded() &&
                has_diagnostic(
                    runtime_factory_failure.diagnostics,
                    CompleteDiagnosticCode::MissingImplementationEntry),
            "missing exact RuntimeCell factory did not fail link");

    auto duplicate_runtime_factory = implementation;
    const auto duplicate_factory = std::find_if(
        duplicate_runtime_factory.entries.begin(),
        duplicate_runtime_factory.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::RuntimeCellFactory;
        });
    require(duplicate_factory != duplicate_runtime_factory.entries.end(),
            "duplicate RuntimeCell factory fixture is absent");
    duplicate_runtime_factory.entries.push_back(*duplicate_factory);
    const auto duplicate_factory_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {duplicate_runtime_factory});
    require(!duplicate_factory_failure.succeeded() &&
                has_diagnostic(
                    duplicate_factory_failure.diagnostics,
                    CompleteDiagnosticCode::MultipleImplementationEntries),
            "duplicate exact RuntimeCell factory did not fail link");

    auto wrong_runtime_factory_kind = implementation;
    const auto wrong_factory_kind = std::find_if(
        wrong_runtime_factory_kind.entries.begin(),
        wrong_runtime_factory_kind.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::RuntimeCellFactory;
        });
    require(wrong_factory_kind !=
                wrong_runtime_factory_kind.entries.end(),
            "wrong RuntimeCell factory kind fixture is absent");
    wrong_factory_kind->kind =
        gnc::model_sdk::StaticEntryKind::DefinitionBuilder;
    const auto wrong_factory_kind_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_runtime_factory_kind});
    require(!wrong_factory_kind_failure.succeeded() &&
                has_diagnostic(
                    wrong_factory_kind_failure.diagnostics,
                    CompleteDiagnosticCode::ImplementationMismatch),
            "wrong RuntimeCell factory entry kind did not fail exact link");

    auto wrong_runtime_factory_shape = implementation;
    const auto wrong_factory_shape = std::find_if(
        wrong_runtime_factory_shape.entries.begin(),
        wrong_runtime_factory_shape.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::RuntimeCellFactory;
        });
    require(wrong_factory_shape !=
                wrong_runtime_factory_shape.entries.end(),
            "wrong RuntimeCell factory call-shape fixture is absent");
    wrong_factory_shape->call_shape_id += ".wrong";
    const auto wrong_factory_shape_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_runtime_factory_shape});
    require(!wrong_factory_shape_failure.succeeded() &&
                has_diagnostic(
                    wrong_factory_shape_failure.diagnostics,
                    CompleteDiagnosticCode::ImplementationMismatch),
            "wrong RuntimeCell factory call shape did not fail exact link");

    auto wrong_factory_package_plan = positive.value->plan;
    require(!wrong_factory_package_plan.runtime_components.empty(),
            "wrong RuntimeCell factory package fixture is absent");
    const auto factory_requirement_id =
        wrong_factory_package_plan.runtime_components.front()
            .runtime_cell_factory_entry_requirement_id;
    const auto wrong_factory_package_requirement = std::find_if(
        wrong_factory_package_plan.entry_requirements.begin(),
        wrong_factory_package_plan.entry_requirements.end(),
        [&](const auto& requirement) {
            return requirement.requirement_id == factory_requirement_id;
        });
    require(wrong_factory_package_requirement !=
                wrong_factory_package_plan.entry_requirements.end(),
            "wrong RuntimeCell factory package requirement is absent");
    wrong_factory_package_requirement->package.package_id += ".foreign";
    expect_plan_conformance_failure(
        std::move(wrong_factory_package_plan),
        "foreign-package RuntimeCell factory requirement passed component conformance");

    auto wrong_slot_codec = implementation;
    const auto slot_codec = std::find_if(
        wrong_slot_codec.entries.begin(),
        wrong_slot_codec.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::SlotCodec;
        });
    require(slot_codec != wrong_slot_codec.entries.end(),
            "slot-codec negative fixture lacks a codec entry");
    slot_codec->call_shape_id += ".wrong";
    const auto slot_codec_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_slot_codec});
    require(!slot_codec_failure.succeeded() &&
                has_diagnostic(
                    slot_codec_failure.diagnostics,
                    CompleteDiagnosticCode::ImplementationMismatch),
            "wrong slot-codec call shape did not fail link");

    auto missing_state_codec = implementation;
    const auto state_codec = std::find_if(
        missing_state_codec.entries.begin(),
        missing_state_codec.entries.end(), [](const auto& entry) {
            return entry.kind ==
                   gnc::model_sdk::StaticEntryKind::StateCodec;
        });
    require(state_codec != missing_state_codec.entries.end(),
            "missing state-codec fixture lacks a codec entry");
    missing_state_codec.entries.erase(state_codec);
    const auto missing_state_codec_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_state_codec});
    require(!missing_state_codec_failure.succeeded() &&
                has_diagnostic(
                    missing_state_codec_failure.diagnostics,
                    CompleteDiagnosticCode::MissingImplementationEntry),
            "missing exact state codec did not fail link");

    auto duplicate_entry = implementation;
    duplicate_entry.entries.push_back(duplicate_entry.entries.front());
    const auto duplicate_entry_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {duplicate_entry});
    require(!duplicate_entry_failure.succeeded() &&
                has_diagnostic(
                    duplicate_entry_failure.diagnostics,
                    CompleteDiagnosticCode::MultipleImplementationEntries),
            "duplicate exact implementation entry did not fail link");

    auto missing_typed_entry = implementation;
    missing_typed_entry.entries.front().typed_entry.reset();
    const auto typed_entry_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_typed_entry});
    require(!typed_entry_failure.succeeded() &&
                has_diagnostic(typed_entry_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "missing process-local typed entry did not fail link");

    auto wrong_typed_entry = implementation;
    const auto wrong_typed_derivative = std::find_if(
        wrong_typed_entry.entries.begin(), wrong_typed_entry.entries.end(),
        [](const auto& entry) {
            return entry.entry_id ==
                   gnc::packages::yyz::kRigidDerivativeKernelIdentity.id;
        });
    require(wrong_typed_derivative != wrong_typed_entry.entries.end(),
            "wrong typed-entry fixture lacks derivative entry");
    wrong_typed_derivative->typed_entry = std::any{&poison_entry};
    wrong_typed_derivative->link_anchor =
        gnc::model_sdk::make_static_link_anchor<&poison_entry>();
    const auto wrong_typed_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_typed_entry});
    require(!wrong_typed_failure.succeeded() &&
                has_diagnostic(wrong_typed_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "wrong process-local typed entry type did not fail link");

    auto wrong_state_layout = implementation;
    const auto stateful_entry = std::find_if(
        wrong_state_layout.entries.begin(), wrong_state_layout.entries.end(),
        [](const auto& entry) { return !entry.state_layout_id.empty(); });
    require(stateful_entry != wrong_state_layout.entries.end(),
            "wrong state-layout fixture lacks a stateful entry");
    stateful_entry->state_layout_id += ".wrong";
    const auto wrong_layout_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {wrong_state_layout});
    require(!wrong_layout_failure.succeeded() &&
                has_diagnostic(wrong_layout_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "wrong entry state layout did not fail link");

    auto missing_state_layout = implementation;
    missing_state_layout.state_layouts.clear();
    const auto missing_layout_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_state_layout});
    require(!missing_layout_failure.succeeded() &&
                has_diagnostic(missing_layout_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "missing state layout did not fail link");

    auto duplicate_state_layout = implementation;
    require(!duplicate_state_layout.state_layouts.empty(),
            "duplicate state-layout fixture has no layout");
    duplicate_state_layout.state_layouts.push_back(
        duplicate_state_layout.state_layouts.front());
    const auto duplicate_layout_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {duplicate_state_layout});
    require(!duplicate_layout_failure.succeeded() &&
                has_diagnostic(duplicate_layout_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "duplicate state layout did not fail link");

    auto missing_value_layout = implementation;
    missing_value_layout.value_layouts.clear();
    const auto missing_value_layout_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {missing_value_layout});
    require(!missing_value_layout_failure.succeeded() &&
                has_diagnostic(missing_value_layout_failure.diagnostics,
                               CompleteDiagnosticCode::ImplementationMismatch),
            "missing runtime value layout did not fail link");

    auto duplicate_value_layout = implementation;
    require(!duplicate_value_layout.value_layouts.empty(),
            "duplicate runtime value-layout fixture has no layout");
    duplicate_value_layout.value_layouts.push_back(
        duplicate_value_layout.value_layouts.front());
    const auto duplicate_value_layout_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {duplicate_value_layout});
    require(!duplicate_value_layout_failure.succeeded() &&
                has_diagnostic(
                    duplicate_value_layout_failure.diagnostics,
                    CompleteDiagnosticCode::ImplementationMismatch),
            "duplicate runtime value layout did not fail link");

    const auto missing_package_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs, {});
    require(!missing_package_failure.succeeded() &&
                has_diagnostic(
                    missing_package_failure.diagnostics,
                    CompleteDiagnosticCode::MissingImplementationPackage),
            "missing implementation package did not fail link");

    const auto duplicate_package_failure =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {implementation, implementation});
    require(!duplicate_package_failure.succeeded() &&
                has_diagnostic(
                    duplicate_package_failure.diagnostics,
                    CompleteDiagnosticCode::MultipleImplementationPackages),
            "duplicate implementation package did not fail link");

    auto alternate_build = implementation;
    alternate_build.build_fingerprint = "build.ref-yyz.release.rebuilt";
    const auto alternate_build_link =
        gnc::compiler::link_complete_execution_plan(
            positive.value->plan, positive.value->proofs,
            {alternate_build});
    require(!alternate_build_link.succeeded() &&
                has_diagnostic(
                    alternate_build_link.diagnostics,
                    CompleteDiagnosticCode::MissingImplementationPackage),
            "wrong package build fingerprint did not fail exact link");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
            std::cerr << "usage: compiler_complete_yyz_plan --self-check\n";
            return 2;
        }
        verify_initial_product_builders();
        verify_complete_ref_graph();
        verify_determinism_and_link_semantics();
        verify_high_value_negatives();
        std::cout <<
            "REF-YYZ R2 planning/proof/science-entry link review checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
