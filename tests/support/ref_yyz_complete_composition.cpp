#include "ref_yyz_complete_composition.hpp"

#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gnc::tests::ref_yyz {
namespace {

using gnc::compiler::CompleteSourceEvaluatorHistory;
using gnc::compiler::CompleteSourceInitialBinding;
using gnc::compiler::CompleteSourceOccurrence;
using gnc::compiler::CompleteSourceTransaction;
using gnc::compiler::CompleteStaticCompositionSource;
using gnc::compiler::ScopeKey;
using gnc::compiler::ScopeKind;
using gnc::compiler::SourceConfigFieldProvenance;
using gnc::compiler::SourceRef;
using gnc::model_sdk::CanonicalConfigBlock;
using gnc::model_sdk::CanonicalConfigValue;
using gnc::model_sdk::CanonicalConfigValueKind;
using gnc::model_sdk::CanonicalEnumValue;
using gnc::model_sdk::ModelExecutionForm;
using gnc::model_sdk::RuntimeCellProfile;
using gnc::model_sdk::StaticInvocationKind;
using gnc::model_sdk::StaticModelDescriptor;
using gnc::model_sdk::StaticPackageDescriptor;

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

} // namespace

namespace {

[[nodiscard]] CompleteStaticCompositionSource make_source(
    const StaticPackageDescriptor& package, bool include_navigation,
    double base_step_seconds, std::int64_t terminal_tick) {
    CompleteStaticCompositionSource source;
    source.source_version =
        std::string(gnc::compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id =
        "mission.fixture.yyz.lookup-altitude-hold@1";
    source.plan_id = "plan.ref-yyz.complete";
    source.mission_source = ref("mission");
    source.clock = {std::string(kClock), base_step_seconds, 0,
                    terminal_tick, ref("clock")};
    source.entities.push_back(
        {std::string(kEntity),
         gnc::compiler::EntityLifecycle::ActiveAtInitialize,
         ref("entities/vehicle/identity"),
         ref("entities/vehicle/lifecycle")});
    const ScopeKey vehicle_scope{ScopeKind::Vehicle,
                                 std::string(kEntity)};
    source.scopes.push_back({vehicle_scope, ref("scopes/vehicle")});

    std::vector<std::size_t> selected_model_indices;
    for (std::size_t index = 0U; index < package.models.size(); ++index) {
        if (include_navigation ||
            package.models[index].definition.model_id !=
                gnc::packages::yyz::
                    kTruthPassthroughNavigationModelIdentity) {
            selected_model_indices.push_back(index);
        }
    }

    for (const auto index : selected_model_indices) {
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
        for (auto& field : occurrence.configuration.fields) {
            if (field.field_id.find("fixed_step") != std::string::npos) {
                field.value = base_step_seconds;
            }
        }
        for (const auto& asset : model.asset_slots) {
            require(asset.role == "aerodynamics",
                    "REF graph contains an unmapped product asset role");
            occurrence.asset_bindings.push_back(
                {asset.role, asset.asset_schema_id,
                 "aero-table.fixture.yyz.multiaffine@1",
                 ref("occurrences/" + occurrence.occurrence_id +
                     "/assets/" + asset.role)});
        }
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
    for (const auto consumer_index : selected_model_indices) {
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
            for (const auto provider_index : selected_model_indices) {
                const auto& provider_model =
                    package.models[provider_index];
                for (const auto& output :
                     provider_model.ports) {
                    bool target_provider = true;
                    if (include_navigation &&
                        input.contract_id ==
                            gnc::packages::yyz::
                                kCommittedRigidObservationContractIdentity) {
                        if (consumer.definition.model_id ==
                            gnc::packages::yyz::
                                kTruthPassthroughNavigationModelIdentity) {
                            target_provider =
                                provider_model.definition.model_id ==
                                gnc::packages::yyz::
                                    kRigidStepModelIdentity;
                        } else if (consumer.definition.model_id ==
                                   gnc::packages::yyz::
                                       kAltitudePitchGuidanceModelIdentity) {
                            target_provider =
                                provider_model.definition.model_id ==
                                gnc::packages::yyz::
                                    kTruthPassthroughNavigationModelIdentity;
                        }
                    }
                    if (target_provider && output.direction ==
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
    for (const auto caller_index : selected_model_indices) {
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
                for (const auto provider_index : selected_model_indices) {
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
    for (const auto index : selected_model_indices) {
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

[[nodiscard]] CompleteSourceOccurrence& occurrence_for(
    CompleteStaticCompositionSource& source, std::string_view model_id) {
    const auto found = std::find_if(
        source.occurrences.begin(), source.occurrences.end(),
        [&](const auto& occurrence) {
            return occurrence.model_id == model_id;
        });
    require(found != source.occurrences.end(),
            "target-rate source model occurrence is missing");
    return *found;
}

[[nodiscard]] const gnc::compiler::CompleteSourceBinding& binding_for(
    const CompleteStaticCompositionSource& source,
    const CompleteSourceOccurrence& provider,
    std::string_view provider_port,
    const CompleteSourceOccurrence& consumer,
    std::string_view consumer_port) {
    const auto found = std::find_if(
        source.bindings.begin(), source.bindings.end(),
        [&](const auto& binding) {
            return binding.provider_occurrence_id == provider.occurrence_id &&
                   binding.provider_port_id == provider_port &&
                   binding.consumer_occurrence_id == consumer.occurrence_id &&
                   binding.consumer_port_id == consumer_port;
        });
    require(found != source.bindings.end(),
            "target-rate source binding is missing");
    return *found;
}

} // namespace

[[nodiscard]] CompleteStaticCompositionSource make_complete_source(
    const StaticPackageDescriptor& package) {
    return make_source(package, false, 0.1, 2);
}

gnc::compiler::CompleteOutcome<gnc::contracts::ExecutionPlanImage>
compile_complete_image() {
    const auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source = make_complete_source(package);
    return gnc::compiler::compile_and_link_complete_execution_plan(
        source, {package}, {implementation});
}

gnc::compiler::CompleteStaticCompositionSource
make_multirate_held_output_qualification_source(
    const gnc::model_sdk::StaticPackageDescriptor& package) {
    auto source = make_complete_source(package);
    source.mission_id =
        "mission.qualification.yyz.multirate-held-output@1";
    source.plan_id =
        "plan.qualification.yyz.multirate-held-output";
    const auto occurrence_for = [&](std::string_view model_id)
        -> const CompleteSourceOccurrence& {
        const auto found = std::find_if(
            source.occurrences.begin(), source.occurrences.end(),
            [&](const auto& occurrence) {
                return occurrence.model_id == model_id;
            });
        require(found != source.occurrences.end(),
                "multi-rate source model occurrence is missing");
        return *found;
    };
    const auto& guidance = occurrence_for(
        gnc::packages::yyz::kAltitudePitchGuidanceModelIdentity);
    const auto& controller = occurrence_for(
        gnc::packages::yyz::kPitchMomentControllerModelIdentity);
    const auto binding = std::find_if(
        source.bindings.begin(), source.bindings.end(),
        [&](const auto& value) {
            return value.provider_occurrence_id == guidance.occurrence_id &&
                   value.provider_port_id == "guidance-output" &&
                   value.consumer_occurrence_id ==
                       controller.occurrence_id &&
                   value.consumer_port_id == "guidance-output";
        });
    require(binding != source.bindings.end(),
            "multi-rate guidance/controller binding is missing");
    source.occurrence_schedule_overrides.push_back(
        {guidance.occurrence_id, 2U, 0U, 0U,
         ref("schedule-overrides/guidance")});
    source.occurrence_schedule_overrides.push_back(
        {controller.occurrence_id, 1U, 0U, 1U,
         ref("schedule-overrides/controller")});
    source.binding_temporal_overrides.push_back(
        {binding->binding_id,
         binding->provider_occurrence_id,
         binding->provider_port_id,
         binding->consumer_occurrence_id,
         binding->consumer_port_id,
         std::string(
             gnc::packages::yyz::
                 kAltitudePitchGuidanceOutputContractIdentity),
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::TemporalRelation::HeldLatest,
         ref("temporal-overrides/guidance-to-controller")});
    return source;
}

gnc::compiler::CompleteOutcome<gnc::contracts::ExecutionPlanImage>
compile_multirate_held_output_qualification_image() {
    const auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source =
        make_multirate_held_output_qualification_source(package);
    return gnc::compiler::compile_and_link_complete_execution_plan(
        source, {package}, {implementation});
}

gnc::compiler::CompleteStaticCompositionSource
make_00a_target_rate_source(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::int64_t terminal_tick) {
    auto source = make_source(package, true, 0.01, terminal_tick);
    source.mission_id =
        "mission.qualification.yyz.00a-target-rate@1";
    source.plan_id = "plan.qualification.yyz.00a-target-rate";

    const auto& navigation = occurrence_for(
        source,
        gnc::packages::yyz::kTruthPassthroughNavigationModelIdentity);
    const auto& guidance = occurrence_for(
        source, gnc::packages::yyz::kAltitudePitchGuidanceModelIdentity);
    const auto& controller = occurrence_for(
        source, gnc::packages::yyz::kPitchMomentControllerModelIdentity);
    const auto& actuator = occurrence_for(
        source, gnc::packages::yyz::kIdealBodyMomentActuatorModelIdentity);

    source.occurrence_schedule_overrides = {
        {navigation.occurrence_id, 1U, 0U, 0U,
         ref("target-rate/schedules/navigation")},
        {guidance.occurrence_id, 5U, 0U, 0U,
         ref("target-rate/schedules/guidance")},
        {controller.occurrence_id, 2U, 0U, 4U,
         ref("target-rate/schedules/controller")},
        {actuator.occurrence_id, 1U, 0U, 1U,
         ref("target-rate/schedules/actuator")},
    };

    const auto& guidance_to_controller = binding_for(
        source, guidance, "guidance-output", controller,
        "guidance-output");
    const auto& controller_to_actuator = binding_for(
        source, controller, "controller-output", actuator,
        "controller-output");
    source.binding_temporal_overrides = {
        {guidance_to_controller.binding_id,
         guidance_to_controller.provider_occurrence_id,
         guidance_to_controller.provider_port_id,
         guidance_to_controller.consumer_occurrence_id,
         guidance_to_controller.consumer_port_id,
         std::string(
             gnc::packages::yyz::
                 kAltitudePitchGuidanceOutputContractIdentity),
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::TemporalRelation::HeldLatest,
         ref("target-rate/temporal/guidance-to-controller")},
        {controller_to_actuator.binding_id,
         controller_to_actuator.provider_occurrence_id,
         controller_to_actuator.provider_port_id,
         controller_to_actuator.consumer_occurrence_id,
         controller_to_actuator.consumer_port_id,
         std::string(
             gnc::packages::yyz::
                 kPitchMomentControllerOutputContractIdentity),
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::TemporalRelation::HeldLatest,
         ref("target-rate/temporal/controller-to-actuator")},
    };
    source.observation_schedules.push_back(
        {"observation.00a.committed-rigid-mass", 4U, 0U,
         ref("target-rate/observations/committed-rigid-mass")});
    return source;
}

gnc::compiler::CompleteOutcome<gnc::contracts::ExecutionPlanImage>
compile_00a_target_rate_image(std::int64_t terminal_tick) {
    const auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source =
        make_00a_target_rate_source(package, terminal_tick);
    return gnc::compiler::compile_and_link_complete_execution_plan(
        source, {package}, {implementation});
}

gnc::compiler::CompleteOutcome<gnc::contracts::ExecutionPlanImage>
compile_complete_image_without_reset_capability() {
    auto package =
        gnc::packages::yyz::describe_yyz_rigid_step_package();
    const auto component = std::find_if(
        package.models.begin(), package.models.end(), [](const auto& model) {
            return model.runtime_component.has_value();
        });
    if (component == package.models.end()) {
        throw std::runtime_error(
            "REF-YYZ reset capability fixture lacks a Runtime Cell");
    }
    auto& lifecycle = component->runtime_component->lifecycle_capabilities;
    const auto resettable = std::find(
        lifecycle.begin(), lifecycle.end(),
        gnc::model_sdk::RuntimeLifecycleCapability::Resettable);
    if (resettable == lifecycle.end()) {
        throw std::runtime_error(
            "REF-YYZ reset capability fixture lacks Resettable");
    }
    lifecycle.erase(resettable);

    const auto implementation =
        gnc::packages::yyz::describe_yyz_rigid_step_implementation(
            "build.ref-yyz.release");
    const auto source = make_complete_source(package);
    return gnc::compiler::compile_and_link_complete_execution_plan(
        source, {package}, {implementation});
}

} // namespace gnc::tests::ref_yyz
