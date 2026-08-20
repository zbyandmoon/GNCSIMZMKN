#include "../include/yyz/mass_commit.hpp"

#include "gnc/foundation/spd_cholesky_3x3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
using gnc::foundation::Mat3;
using gnc::foundation::NumericalEvidence;
using gnc::foundation::NumericalFlags;
using gnc::foundation::NumericalOutcome;
using gnc::foundation::NumericalPolicy;
using gnc::foundation::NumericalStatus;
using gnc::foundation::Vec3;

[[nodiscard]] NumericalEvidence mass_commit_evidence(
    gnc::foundation::AlgorithmIdentity algorithm, std::string_view detail,
    NumericalFlags flags = 0U) {
    NumericalEvidence evidence;
    evidence.algorithm = algorithm;
    evidence.detail = detail;
    evidence.flags = flags;
    return evidence;
}

template <typename Value>
[[nodiscard]] NumericalOutcome<Value> mass_commit_failure(
    gnc::foundation::AlgorithmIdentity algorithm,
    NumericalStatus status, std::string_view detail,
    NumericalFlags flags = 0U) {
    return NumericalOutcome<Value>::failure(
        status, mass_commit_evidence(algorithm, detail, flags));
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

[[nodiscard]] bool finite(
    const gnc::foundation::QuaternionStorage& value) noexcept {
    return std::isfinite(value.w()) && std::isfinite(value.x()) &&
           std::isfinite(value.y()) && std::isfinite(value.z());
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

[[nodiscard]] bool valid_sample_at(
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

[[nodiscard]] bool valid_interval_at(
    const IntervalSampleContext& context,
    const gnc::contracts::FrameIdentity& expected_frame,
    const gnc::contracts::ClockDomainIdentity& expected_clock,
    const SimulationInstant& expected_start,
    const SimulationInstant& expected_end,
    std::int64_t expected_revision,
    const NumericalPolicy& policy) noexcept {
    return valid_sample_at(context.sample, expected_frame, expected_clock,
                           expected_start, expected_revision, policy) &&
           same_instant(context.validity.effective_from, expected_start,
                        policy) &&
           same_instant(context.validity.effective_until, expected_end,
                        policy);
}

[[nodiscard]] bool valid_propulsion_interval(
    const IntervalSampleContext& context,
    const SuppliedPropulsionDefinition& definition) noexcept {
    const NumericalPolicy& policy = definition.numerical_policy;
    const SimulationInstant& start = context.validity.effective_from;
    const SimulationInstant& end = context.validity.effective_until;
    return context.sample.frame == definition.body_frame &&
           context.sample.clock_domain == definition.clock_domain &&
           context.sample.configuration_revision >= 0 &&
           context.sample.quality == DataQuality::Valid &&
           same_instant(context.sample.sample_time, start, policy) &&
           start.tick >= 0 && end.tick > start.tick &&
           std::isfinite(start.seconds) && std::isfinite(end.seconds) &&
           end.seconds > start.seconds;
}

[[nodiscard]] bool approximate_status(NumericalStatus status) noexcept {
    return status == NumericalStatus::Approximate ||
           status == NumericalStatus::Extrapolated;
}

[[nodiscard]] std::string finite_check_token(
    gnc::foundation::FiniteCheck value) {
    switch (value) {
    case gnc::foundation::FiniteCheck::Disabled:
        return "disabled";
    case gnc::foundation::FiniteCheck::InputAndOutput:
        return "input-and-output";
    case gnc::foundation::FiniteCheck::EveryStage:
        return "every-stage";
    }
    return {};
}

[[nodiscard]] std::optional<gnc::foundation::FiniteCheck>
parse_finite_check(std::string_view token) {
    if (token == "disabled") {
        return gnc::foundation::FiniteCheck::Disabled;
    }
    if (token == "input-and-output") {
        return gnc::foundation::FiniteCheck::InputAndOutput;
    }
    if (token == "every-stage") {
        return gnc::foundation::FiniteCheck::EveryStage;
    }
    return std::nullopt;
}

[[nodiscard]] std::string normalization_token(
    gnc::foundation::QuaternionNormalizationPolicy value) {
    switch (value) {
    case gnc::foundation::QuaternionNormalizationPolicy::Error:
        return "error";
    case gnc::foundation::QuaternionNormalizationPolicy::NormalizeWithFlag:
        return "normalize-with-flag";
    }
    return {};
}

[[nodiscard]] std::optional<
    gnc::foundation::QuaternionNormalizationPolicy>
parse_normalization(std::string_view token) {
    if (token == "error") {
        return gnc::foundation::QuaternionNormalizationPolicy::Error;
    }
    if (token == "normalize-with-flag") {
        return gnc::foundation::QuaternionNormalizationPolicy::
            NormalizeWithFlag;
    }
    return std::nullopt;
}

[[nodiscard]] bool canonical_double(double value) noexcept {
    return std::isfinite(value) &&
           !(value == 0.0 && std::signbit(value));
}

template <std::size_t Size>
[[nodiscard]] bool exact_config_fields(
    const gnc::model_sdk::CanonicalConfigBlock& configuration,
    std::string_view schema_id, std::uint32_t schema_version,
    const std::array<std::string_view, Size>& fields) {
    if (configuration.schema_id != schema_id ||
        configuration.schema_version != schema_version ||
        configuration.fields.size() != fields.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < fields.size(); ++index) {
        if (configuration.fields[index].field_id != fields[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<NumericalPolicy> config_numerical_policy(
    const gnc::model_sdk::CanonicalConfigBlock& configuration,
    std::size_t absolute_index, std::size_t condition_index,
    std::size_t finite_index, std::size_t relative_index,
    std::size_t zero_index) {
    const auto* absolute =
        std::get_if<double>(&configuration.fields[absolute_index].value);
    const auto* condition =
        std::get_if<double>(&configuration.fields[condition_index].value);
    const auto* finite =
        std::get_if<gnc::model_sdk::CanonicalEnumValue>(
            &configuration.fields[finite_index].value);
    const auto* relative =
        std::get_if<double>(&configuration.fields[relative_index].value);
    const auto* zero =
        std::get_if<double>(&configuration.fields[zero_index].value);
    if (absolute == nullptr || condition == nullptr || finite == nullptr ||
        relative == nullptr || zero == nullptr ||
        !canonical_double(*absolute) || !canonical_double(*condition) ||
        !canonical_double(*relative) || !canonical_double(*zero)) {
        return std::nullopt;
    }
    const auto finite_check = parse_finite_check(finite->token);
    if (!finite_check.has_value()) {
        return std::nullopt;
    }
    NumericalPolicy policy{
        *absolute, *relative, *finite_check, *zero, *condition};
    return gnc::foundation::valid_numerical_policy(policy)
               ? std::optional<NumericalPolicy>{policy}
               : std::nullopt;
}

[[nodiscard]] std::string mission_metric_token(MissionMetric value) {
    switch (value) {
    case MissionMetric::DurationSeconds:
        return "duration_s";
    case MissionMetric::DownrangeMeters:
        return "downrange_m";
    case MissionMetric::RemainingMassKilograms:
        return "remaining_mass_kg";
    }
    return {};
}

[[nodiscard]] std::optional<MissionMetric> parse_mission_metric(
    std::string_view token) {
    if (token == "duration_s") {
        return MissionMetric::DurationSeconds;
    }
    if (token == "downrange_m") {
        return MissionMetric::DownrangeMeters;
    }
    if (token == "remaining_mass_kg") {
        return MissionMetric::RemainingMassKilograms;
    }
    return std::nullopt;
}

[[nodiscard]] std::string mission_relation_token(MissionRelation value) {
    switch (value) {
    case MissionRelation::LessThanOrEqual:
        return "<=";
    case MissionRelation::GreaterThanOrEqual:
        return ">=";
    }
    return {};
}

[[nodiscard]] std::optional<MissionRelation> parse_mission_relation(
    std::string_view token) {
    if (token == "<=") {
        return MissionRelation::LessThanOrEqual;
    }
    if (token == ">=") {
        return MissionRelation::GreaterThanOrEqual;
    }
    return std::nullopt;
}

[[nodiscard]] std::string mission_action_token(MissionAction value) {
    switch (value) {
    case MissionAction::Complete:
        return "Complete";
    case MissionAction::Abort:
        return "Abort";
    }
    return {};
}

[[nodiscard]] std::optional<MissionAction> parse_mission_action(
    std::string_view token) {
    if (token == "Complete") {
        return MissionAction::Complete;
    }
    if (token == "Abort") {
        return MissionAction::Abort;
    }
    return std::nullopt;
}

[[nodiscard]] CommittedRigidMassBoundary promote_candidate(
    const RigidStepContext& context,
    const AtomicRigidMassCandidate& candidate) {
    CommittedRigidMassBoundary committed;
    committed.rigid_context = {
        context.inertial_frame,
        context.clock_domain,
        candidate.effective_at,
        context.configuration_revision,
        DataQuality::Valid,
    };
    committed.rigid_state = candidate.rigid.state;
    committed.mass_state = candidate.mass.state;
    return committed;
}

[[nodiscard]] const gnc::model_sdk::StaticModelDescriptor*
find_static_model(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::string_view model_id) {
    const auto found = std::find_if(
        package.models.begin(), package.models.end(),
        [model_id](const auto& model) {
            return model.definition.model_id == model_id;
        });
    return found == package.models.end() ? nullptr : &*found;
}

[[nodiscard]] const gnc::model_sdk::StaticRuntimeObligationEntryDescriptor*
find_runtime_entry(
    const gnc::model_sdk::StaticModelDescriptor& model,
    gnc::model_sdk::RuntimeExecutionObligation obligation) {
    if (!model.runtime_component.has_value()) {
        return nullptr;
    }
    const auto& entries = model.runtime_component->obligation_entries;
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [obligation](const auto& entry) {
            return entry.obligation == obligation;
        });
    return found == entries.end() ? nullptr : &*found;
}

[[nodiscard]] const gnc::model_sdk::StaticPortDescriptor* find_static_port(
    const gnc::model_sdk::StaticModelDescriptor& model,
    std::string_view port_id) {
    const auto found = std::find_if(
        model.ports.begin(), model.ports.end(),
        [port_id](const auto& port) { return port.port_id == port_id; });
    return found == model.ports.end() ? nullptr : &*found;
}

// Authored independently of the descriptor history shape. The linker compares
// this implementation witness against that descriptor before producing an
// Image entry for the terminal evaluator callable.
const gnc::model_sdk::StaticEvaluatorHistoryWitness
    kCommittedMissionHistoryImplementationWitness{
        std::string(kCommittedRigidMassSequenceContractIdentity),
        static_cast<std::uint32_t>(kCommittedMissionHistoryDepth),
        {{std::string(kCommittedMissionRigidHistoryMemberId),
          std::string(kRigidStateSchemaIdentity),
          std::string(kRigidStateLayoutIdentity)},
         {std::string(kCommittedMissionMassHistoryMemberId),
          std::string(kMassStateSchemaIdentity),
          std::string(kMassStateLayoutIdentity)}}};

template <typename ExpectedCallable, auto Callable>
void append_static_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::foundation::AlgorithmIdentity& identity,
    gnc::model_sdk::StaticEntryKind kind, std::string signature,
    std::string call_shape,
    std::string state_layout = {}) {
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            std::string(identity.id), std::string(identity.version), kind,
            std::move(signature),
            gnc::model_sdk::make_static_callable_contract<
                ExpectedCallable>(std::move(call_shape)),
            std::move(state_layout)));
}

template <typename ExpectedCallable, auto Callable>
void append_state_codec_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model) {
    const auto& owner = *model.runtime_component->state_owner;
    auto entry = gnc::model_sdk::make_static_implementation_entry<
        Callable, ExpectedCallable>(
        owner.codec.entry_id, owner.codec.entry_version,
        gnc::model_sdk::StaticEntryKind::StateCodec,
        gnc::model_sdk::canonical_state_codec_signature(model),
        gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
            owner.codec.call_shape_id),
        owner.schema.layout_id);
    entry = gnc::model_sdk::with_static_state_codec_witness(
        std::move(entry),
        {owner.schema.layout_id, owner.codec.clone_operation_id,
         owner.codec.validate_operation_id,
         owner.codec.finite_validation_operation_id,
         owner.codec.invariant_validation_operation_id,
         owner.codec.noexcept_swap_operation_id,
         owner.codec.project_operation_id, true});
    implementation.entries.push_back(std::move(entry));
}

template <typename ExpectedCallable, auto Callable>
void append_slot_codec_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model,
    const gnc::model_sdk::StaticPortDescriptor& port) {
    const auto& codec = *port.slot_codec;
    auto entry = gnc::model_sdk::make_static_implementation_entry<
        Callable, ExpectedCallable>(
        codec.entry_id, codec.entry_version,
        gnc::model_sdk::StaticEntryKind::SlotCodec,
        gnc::model_sdk::canonical_slot_codec_signature(model, port),
        gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
            codec.call_shape_id));
    entry = gnc::model_sdk::with_static_slot_codec_witness(
        std::move(entry),
        {port.contract_id, codec.layout_id, codec.copy_operation_id,
         codec.validate_operation_id, codec.project_operation_id});
    implementation.entries.push_back(std::move(entry));
}

template <typename ExpectedCallable, auto Callable>
void append_result_slot_codec_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model,
    const gnc::model_sdk::StaticRuntimeObligationEntryDescriptor& obligation) {
    const auto& codec = *obligation.result_codec;
    auto entry = gnc::model_sdk::make_static_implementation_entry<
        Callable, ExpectedCallable>(
        codec.entry_id, codec.entry_version,
        gnc::model_sdk::StaticEntryKind::SlotCodec,
        gnc::model_sdk::canonical_result_slot_codec_signature(
            model, obligation),
        gnc::model_sdk::make_static_callable_contract<ExpectedCallable>(
            codec.call_shape_id));
    entry = gnc::model_sdk::with_static_slot_codec_witness(
        std::move(entry),
        {obligation.result_contract_id, codec.layout_id,
         codec.copy_operation_id, codec.validate_operation_id,
         codec.project_operation_id});
    implementation.entries.push_back(std::move(entry));
}

template <typename ExpectedCallable, auto Callable>
void append_runtime_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model,
    gnc::model_sdk::RuntimeExecutionObligation obligation,
    gnc::model_sdk::StaticEntryKind kind,
    std::string state_layout = {},
    const gnc::model_sdk::StaticEvaluatorHistoryWitness*
        evaluator_history_witness = nullptr) {
    const auto* entry = find_runtime_entry(model, obligation);
    if (entry == nullptr) {
        return;
    }
    auto implementation_entry =
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            entry->entry_id, entry->entry_version, kind,
            gnc::model_sdk::canonical_runtime_entry_signature(model,
                                                               *entry),
            gnc::model_sdk::make_static_callable_contract<
                ExpectedCallable>(entry->call_shape_id),
            std::move(state_layout));
    if (evaluator_history_witness != nullptr) {
        implementation_entry =
            gnc::model_sdk::with_static_evaluator_history_witness(
                std::move(implementation_entry),
                *evaluator_history_witness);
    }
    implementation.entries.push_back(std::move(implementation_entry));
}

template <typename ExpectedCallable, auto Callable>
void append_definition_builder_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model) {
    if (!model.runtime_component.has_value()) {
        return;
    }
    const auto& runtime = *model.runtime_component;
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            runtime.definition_builder_id,
            runtime.definition_builder_version,
            gnc::model_sdk::StaticEntryKind::DefinitionBuilder,
            gnc::model_sdk::canonical_definition_builder_signature(model),
            gnc::model_sdk::make_static_callable_contract<
                ExpectedCallable>(
                runtime.definition_builder_call_shape_id)));
}

template <typename ExpectedCallable, auto Callable>
void append_runtime_cell_factory_entry(
    gnc::model_sdk::StaticPackageImplementation& implementation,
    const gnc::model_sdk::StaticModelDescriptor& model) {
    if (!model.runtime_component.has_value()) {
        return;
    }
    const auto& runtime = *model.runtime_component;
    implementation.entries.push_back(
        gnc::model_sdk::make_static_implementation_entry<
            Callable, ExpectedCallable>(
            runtime.runtime_cell_factory_id,
            runtime.runtime_cell_factory_version,
            gnc::model_sdk::StaticEntryKind::RuntimeCellFactory,
            gnc::model_sdk::canonical_runtime_cell_factory_signature(model),
            gnc::model_sdk::make_static_callable_contract<
                ExpectedCallable>(
                runtime.runtime_cell_factory_call_shape_id)));
}

[[nodiscard]] bool valid_runtime_cell_factory_context(
    const gnc::model_sdk::RuntimeCellFactoryContext& context) noexcept {
    // Zero remains the reserved invalid handle in every compiled table.
    return context.runtime_instance_id.value != 0U &&
           context.runtime_component_handle != 0U &&
           context.resources.handle != 0U &&
           context.resources.workspace_layout_id ==
               "gnc.workspace.none@1";
}

} // namespace

gnc::model_sdk::StaticPackageDescriptor
describe_yyz_rigid_step_package() {
    auto package = detail::describe_yyz_rigid_step_base_package();

    const auto periodic_schedule = [] {
        gnc::model_sdk::StaticRuntimeScheduleDescriptor schedule;
        schedule.trigger =
            gnc::model_sdk::StaticScheduleTrigger::EveryBoundary;
        schedule.step_interval = 1U;
        schedule.offset = 0U;
        schedule.output_hold =
            gnc::model_sdk::HoldPolicy::ZeroOrderHold;
        schedule.max_input_age_steps = 0U;
        return schedule;
    };
    const std::vector<gnc::model_sdk::RuntimeLifecycleCapability>
        lifecycle{
            gnc::model_sdk::RuntimeLifecycleCapability::Instantiate,
            gnc::model_sdk::RuntimeLifecycleCapability::Dispose};

    const auto rigid_found = std::find_if(
        package.models.begin(), package.models.end(),
        [](const auto& candidate) {
            return candidate.definition.model_id ==
                   kRigidStepModelIdentity;
        });
    if (rigid_found != package.models.end() &&
        rigid_found->runtime_component.has_value()) {
        auto& rigid = *rigid_found;
        rigid.configuration.fields.insert(
            rigid.configuration.fields.begin() + 12,
            {"combined_wrench_source_id",
             gnc::model_sdk::CanonicalConfigValueKind::String});
        rigid.ports = {
            {"mass-properties",
             std::string(kMassPropertiesContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::IntervalModel,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::IntervalModel},
            {"propulsion-body-wrench",
             std::string(
                 kSuppliedPropulsionBodyWrenchContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::IntervalModel,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::IntervalModel},
            {"actuator-output",
             std::string(
                 kIdealBodyMomentActuatorOutputContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::IntervalModel,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::IntervalModel},
            {"environment-sample",
             std::string(kEnvironmentSampleContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::PureQuery,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::NotApplicable},
            {"aerodynamic-coefficients",
             std::string(kAerodynamicCoefficientsContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::PureQuery,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::NotApplicable},
            {"form-input",
             std::string(kRigidFormInputContractIdentity),
             gnc::model_sdk::StaticPortDirection::Input,
             gnc::model_sdk::BindingKind::ContinuousClosureLink,
             gnc::model_sdk::PortCardinality::ExactlyOne,
             gnc::model_sdk::TemporalRelation::IntervalModel},
            {"committed-rigid-observation",
             std::string(kRigidObservationContractIdentity),
             gnc::model_sdk::StaticPortDirection::Output,
             gnc::model_sdk::BindingKind::SampledSignal,
             gnc::model_sdk::PortCardinality::OneOrMore,
             gnc::model_sdk::TemporalRelation::CurrentCycle,
             make_yyz_slot_codec_descriptor(
                 kRigidObservationSlotCodecIdentity,
                 kRigidObservationSlotCodecCallShapeIdentity,
                 kRigidObservationLayoutIdentity,
                 "gnc.operation.yyz.committed-rigid-observation")},
        };
        auto& entries =
            rigid.runtime_component->obligation_entries;
        const auto boundary = std::find_if(
            entries.begin(), entries.end(), [](const auto& entry) {
                return entry.obligation ==
                       gnc::model_sdk::RuntimeExecutionObligation::
                           BoundaryEvaluation;
            });
        if (boundary != entries.end()) {
            boundary->entry_id = std::string(
                kControlledRigidBoundaryEvaluationIdentity.id);
            boundary->entry_version = std::string(
                kControlledRigidBoundaryEvaluationIdentity.version);
            boundary->request_contract_id = std::string(
                kControlledRigidBoundaryInputContractIdentity);
            boundary->result_contract_id =
                std::string(
                    kControlledRigidBoundaryPreparationContractIdentity);
            boundary->input_port_ids = {
                "mass-properties", "propulsion-body-wrench",
                "actuator-output", "environment-sample",
                "aerodynamic-coefficients", "form-input"};
            boundary->call_shape_id = std::string(
                kControlledRigidBoundaryRuntimeCallShapeIdentity);
            boundary->result_codec = make_yyz_slot_codec_descriptor(
                kControlledRigidBoundaryPreparationSlotCodecIdentity,
                kControlledRigidBoundaryPreparationSlotCodecCallShapeIdentity,
                kControlledRigidBoundaryPreparationLayoutIdentity,
                "gnc.operation.yyz.controlled-rigid-boundary-preparation");
        }
        const auto derivative = std::find_if(
            entries.begin(), entries.end(), [](const auto& entry) {
                return entry.obligation ==
                       gnc::model_sdk::RuntimeExecutionObligation::
                           DerivativeEvaluation;
            });
        if (derivative != entries.end()) {
            derivative->input_port_ids = {
                "mass-properties", "environment-sample", "form-input"};
            derivative->state_read =
                gnc::model_sdk::StaticStateReadKind::Candidate;
            derivative->state_write =
                gnc::model_sdk::StaticStateWriteKind::None;
        }
        rigid.runtime_component->definition_builder_id = std::string(
            kControlledRigidDefinitionBuilderIdentity.id);
        rigid.runtime_component->definition_builder_version = std::string(
            kControlledRigidDefinitionBuilderIdentity.version);
        rigid.runtime_component->definition_builder_call_shape_id =
            std::string(
                kControlledRigidDefinitionBuilderCallShapeIdentity);
        rigid.runtime_component->runtime_cell_factory_id = std::string(
            kControlledRigidRuntimeCellFactoryIdentity.id);
        rigid.runtime_component->runtime_cell_factory_version = std::string(
            kControlledRigidRuntimeCellFactoryIdentity.version);
        rigid.runtime_component->runtime_cell_factory_call_shape_id =
            std::string(
                kControlledRigidRuntimeCellFactoryCallShapeIdentity);
    }

    gnc::model_sdk::StaticModelDescriptor mass;
    mass.definition = {
        std::string(kScalarBurnMassModelIdentity),
        std::string(kScalarBurnMassModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    mass.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    mass.configuration.schema_id =
        std::string(kScalarBurnMassConfigSchemaIdentity);
    mass.configuration.schema_version =
        kScalarBurnMassConfigSchemaVersion;
    mass.configuration.fields = {
        {"mass_state_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64}};
    mass.ports = {
        {"mass-flow-interval",
         std::string(kMassFlowIntervalContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::IntervalModel},
        {"mass-properties",
         std::string(kMassPropertiesContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         make_yyz_slot_codec_descriptor(
             kMassPropertiesSlotCodecIdentity,
             kMassPropertiesSlotCodecCallShapeIdentity,
             kMassPropertiesLayoutIdentity,
             "gnc.operation.yyz.mass-properties")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor mass_runtime;
    mass_runtime.recipe_id = std::string(kScalarBurnMassRecipeIdentity);
    mass_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::DiscreteStateProcessor;
    mass_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
        gnc::model_sdk::RuntimeExecutionObligation::IntervalEvolution};
    mass_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
         gnc::model_sdk::CoarsePhase::Publish,
         std::string(kMassPublishProjectionIdentity.id),
         std::string(kMassPublishProjectionIdentity.version),
         std::string(kMassPublishProjectionInputContractIdentity),
         std::string(kMassPropertiesContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {}, {"mass-properties"},
         gnc::model_sdk::StaticStateReadKind::Committed,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kMassPublishProjectionCallShapeIdentity)},
        {gnc::model_sdk::RuntimeExecutionObligation::IntervalEvolution,
         gnc::model_sdk::CoarsePhase::Form,
         std::string(kScalarBurnMassKernelIdentity.id),
         std::string(kScalarBurnMassKernelIdentity.version),
         std::string(kMassFlowIntervalContractIdentity),
         std::string(kScalarBurnMassOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"mass-flow-interval"}, {},
         gnc::model_sdk::StaticStateReadKind::Committed,
         gnc::model_sdk::StaticStateWriteKind::IntervalCandidate, {},
         std::string(kMassIntervalEvolutionCallShapeIdentity)}};
    mass_runtime.schedule = periodic_schedule();
    mass_runtime.lifecycle_capabilities = lifecycle;
    gnc::model_sdk::StaticStateOwnerDescriptor mass_owner;
    mass_owner.schema = {
        std::string(kMassStateSchemaIdentity), 1U,
        std::string(kMassStateLayoutIdentity),
        {{"context", "gnc.contracts.SampleContext", "1", "sample"},
         {"mass-state-id", "utf8", "1", "none"},
         {"mass", "float64", "kg", "none"},
         {"body-origin-to-center-of-mass", "vec3.float64", "m", "body"},
         {"inertia-about-center-of-mass", "mat3.float64", "kg*m^2",
          "body"}}};
    mass_owner.initial_state_builder_id =
        std::string(kMassInitialStateBuilderIdentity.id);
    mass_owner.initial_state_builder_version =
        std::string(kMassInitialStateBuilderIdentity.version);
    mass_owner.initial_state_input_schema = {
        std::string(kMassInitialStateInputSchemaIdentity),
        kMassInitialStateInputSchemaVersion,
        {{"body_origin_to_center_of_mass.x_meters",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"body_origin_to_center_of_mass.y_meters",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"body_origin_to_center_of_mass.z_meters",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"context.clock_domain_id",
          gnc::model_sdk::CanonicalConfigValueKind::String},
         {"context.configuration_revision",
          gnc::model_sdk::CanonicalConfigValueKind::Integer},
         {"context.frame_id",
          gnc::model_sdk::CanonicalConfigValueKind::String},
         {"context.quality",
          gnc::model_sdk::CanonicalConfigValueKind::Enum},
         {"context.sample_time.seconds",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"context.sample_time.tick",
          gnc::model_sdk::CanonicalConfigValueKind::Integer},
         {"inertia_about_center_of_mass.xx_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.xy_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.xz_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.yx_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.yy_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.yz_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.zx_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.zy_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"inertia_about_center_of_mass.zz_kilogram_meters_squared",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"mass_kilograms",
          gnc::model_sdk::CanonicalConfigValueKind::Float64},
         {"mass_state_id",
          gnc::model_sdk::CanonicalConfigValueKind::String}}};
    mass_owner.evolution =
        gnc::model_sdk::StaticStateEvolution::IntervalCandidate;
    mass_owner.initial_state_builder_call_shape_id =
        std::string(kMassInitialStateCallShapeIdentity);
    mass_owner.codec = {
        std::string(kMassStateCodecIdentity.id),
        std::string(kMassStateCodecIdentity.version),
        std::string(kMassStateCodecCallShapeIdentity),
        "gnc.operation.yyz.mass-state.clone@1",
        "gnc.operation.yyz.mass-state.validate@1",
        "gnc.operation.yyz.mass-state.validate-finite@1",
        "gnc.operation.yyz.mass-state.validate-invariants@1",
        "gnc.operation.yyz.mass-state.noexcept-swap@1",
        std::string(kMassPublishProjectionIdentity.id)};
    mass_runtime.state_owner = std::move(mass_owner);
    mass_runtime.definition_builder_id =
        std::string(kScalarBurnMassDefinitionBuilderIdentity.id);
    mass_runtime.definition_builder_version =
        std::string(kScalarBurnMassDefinitionBuilderIdentity.version);
    mass_runtime.definition_builder_call_shape_id = std::string(
        kScalarBurnMassDefinitionBuilderCallShapeIdentity);
    mass_runtime.runtime_cell_factory_id = std::string(
        kScalarBurnMassRuntimeCellFactoryIdentity.id);
    mass_runtime.runtime_cell_factory_version = std::string(
        kScalarBurnMassRuntimeCellFactoryIdentity.version);
    mass_runtime.runtime_cell_factory_call_shape_id = std::string(
        kScalarBurnMassRuntimeCellFactoryCallShapeIdentity);
    mass_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    mass_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    mass.runtime_component = std::move(mass_runtime);

    gnc::model_sdk::StaticModelDescriptor guidance;
    guidance.definition = {
        std::string(kAltitudePitchGuidanceModelIdentity),
        std::string(kAltitudePitchGuidanceModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    guidance.placement =
        gnc::model_sdk::ModelPlacement::VehicleProcess;
    guidance.configuration.schema_id =
        std::string(kAltitudePitchGuidanceConfigSchemaIdentity);
    guidance.configuration.schema_version =
        kAltitudePitchGuidanceConfigSchemaVersion;
    guidance.configuration.fields = {
        {"altitude_error_gain_radians_per_meter",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.normalization",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"attitude.numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"attitude.numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"inertial_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"pitch_command_limit_radians",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"target_altitude_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"vertical_speed_gain_radian_seconds_per_meter",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    guidance.ports = {
        {"committed-rigid-observation",
         std::string(kCommittedRigidObservationContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"guidance-output",
         std::string(kAltitudePitchGuidanceOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         make_yyz_slot_codec_descriptor(
             kGuidanceOutputSlotCodecIdentity,
             kGuidanceOutputSlotCodecCallShapeIdentity,
             kGuidanceOutputLayoutIdentity,
             "gnc.operation.yyz.guidance-output")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor runtime;
    runtime.recipe_id =
        std::string(kAltitudePitchGuidanceRecipeIdentity);
    runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::SampledTransform;
    runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation};
    runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Process,
         std::string(kAltitudePitchGuidanceKernelIdentity.id),
         std::string(kAltitudePitchGuidanceKernelIdentity.version),
         std::string(kCommittedRigidObservationContractIdentity),
         std::string(kAltitudePitchGuidanceOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"committed-rigid-observation"}, {"guidance-output"},
         gnc::model_sdk::StaticStateReadKind::None,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kAltitudePitchGuidanceCallShapeIdentity)}};
    runtime.schedule = periodic_schedule();
    runtime.lifecycle_capabilities = lifecycle;
    runtime.definition_builder_id = std::string(
        kAltitudePitchGuidanceDefinitionBuilderIdentity.id);
    runtime.definition_builder_version = std::string(
        kAltitudePitchGuidanceDefinitionBuilderIdentity.version);
    runtime.definition_builder_call_shape_id = std::string(
        kAltitudePitchGuidanceDefinitionBuilderCallShapeIdentity);
    runtime.runtime_cell_factory_id = std::string(
        kAltitudePitchGuidanceRuntimeCellFactoryIdentity.id);
    runtime.runtime_cell_factory_version = std::string(
        kAltitudePitchGuidanceRuntimeCellFactoryIdentity.version);
    runtime.runtime_cell_factory_call_shape_id = std::string(
        kAltitudePitchGuidanceRuntimeCellFactoryCallShapeIdentity);
    runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    guidance.runtime_component = std::move(runtime);

    gnc::model_sdk::StaticModelDescriptor controller;
    controller.definition = {
        std::string(kPitchMomentControllerModelIdentity),
        std::string(kPitchMomentControllerModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    controller.placement =
        gnc::model_sdk::ModelPlacement::VehicleProcess;
    controller.configuration.schema_id =
        std::string(kPitchMomentControllerConfigSchemaIdentity);
    controller.configuration.schema_version =
        kPitchMomentControllerConfigSchemaVersion;
    controller.configuration.fields = {
        {"body_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"moment_command_limit_newton_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"pitch_error_gain_newton_meters_per_radian",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"pitch_rate_gain_newton_meter_seconds_per_radian",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    controller.ports = {
        {"guidance-output",
         std::string(kAltitudePitchGuidanceOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"controller-output",
         std::string(kPitchMomentControllerOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         make_yyz_slot_codec_descriptor(
             kControllerOutputSlotCodecIdentity,
             kControllerOutputSlotCodecCallShapeIdentity,
             kControllerOutputLayoutIdentity,
             "gnc.operation.yyz.controller-output")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor controller_runtime;
    controller_runtime.recipe_id =
        std::string(kPitchMomentControllerRecipeIdentity);
    controller_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::SampledTransform;
    controller_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation};
    controller_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Process,
         std::string(kPitchMomentControllerKernelIdentity.id),
         std::string(kPitchMomentControllerKernelIdentity.version),
         std::string(kAltitudePitchGuidanceOutputContractIdentity),
         std::string(kPitchMomentControllerOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"guidance-output"}, {"controller-output"},
         gnc::model_sdk::StaticStateReadKind::None,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kPitchMomentControllerCallShapeIdentity)}};
    controller_runtime.schedule = periodic_schedule();
    controller_runtime.lifecycle_capabilities = lifecycle;
    controller_runtime.definition_builder_id = std::string(
        kPitchMomentControllerDefinitionBuilderIdentity.id);
    controller_runtime.definition_builder_version = std::string(
        kPitchMomentControllerDefinitionBuilderIdentity.version);
    controller_runtime.definition_builder_call_shape_id = std::string(
        kPitchMomentControllerDefinitionBuilderCallShapeIdentity);
    controller_runtime.runtime_cell_factory_id = std::string(
        kPitchMomentControllerRuntimeCellFactoryIdentity.id);
    controller_runtime.runtime_cell_factory_version = std::string(
        kPitchMomentControllerRuntimeCellFactoryIdentity.version);
    controller_runtime.runtime_cell_factory_call_shape_id = std::string(
        kPitchMomentControllerRuntimeCellFactoryCallShapeIdentity);
    controller_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    controller_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    controller.runtime_component = std::move(controller_runtime);

    gnc::model_sdk::StaticModelDescriptor actuator;
    actuator.definition = {
        std::string(kIdealBodyMomentActuatorModelIdentity),
        std::string(kIdealBodyMomentActuatorModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    actuator.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    actuator.configuration.schema_id =
        std::string(kIdealBodyMomentActuatorConfigSchemaIdentity);
    actuator.configuration.schema_version =
        kIdealBodyMomentActuatorConfigSchemaVersion;
    actuator.configuration.fields = {
        {"body_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"realization_gain",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"source_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
    };
    actuator.ports = {
        {"controller-output",
         std::string(kPitchMomentControllerOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"actuator-output",
         std::string(kIdealBodyMomentActuatorOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         make_yyz_slot_codec_descriptor(
             kActuatorOutputSlotCodecIdentity,
             kActuatorOutputSlotCodecCallShapeIdentity,
             kActuatorOutputLayoutIdentity,
             "gnc.operation.yyz.actuator-output")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor actuator_runtime;
    actuator_runtime.recipe_id =
        std::string(kIdealBodyMomentActuatorRecipeIdentity);
    actuator_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::SampledTransform;
    actuator_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation};
    actuator_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Output,
         std::string(kIdealBodyMomentActuatorKernelIdentity.id),
         std::string(kIdealBodyMomentActuatorKernelIdentity.version),
         std::string(kIdealBodyMomentActuatorRequestContractIdentity),
         std::string(kIdealBodyMomentActuatorOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"controller-output"}, {"actuator-output"},
         gnc::model_sdk::StaticStateReadKind::None,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kIdealBodyMomentActuatorCallShapeIdentity)}};
    actuator_runtime.schedule = periodic_schedule();
    actuator_runtime.lifecycle_capabilities = lifecycle;
    actuator_runtime.definition_builder_id = std::string(
        kIdealBodyMomentActuatorDefinitionBuilderIdentity.id);
    actuator_runtime.definition_builder_version = std::string(
        kIdealBodyMomentActuatorDefinitionBuilderIdentity.version);
    actuator_runtime.definition_builder_call_shape_id = std::string(
        kIdealBodyMomentActuatorDefinitionBuilderCallShapeIdentity);
    actuator_runtime.runtime_cell_factory_id = std::string(
        kIdealBodyMomentActuatorRuntimeCellFactoryIdentity.id);
    actuator_runtime.runtime_cell_factory_version = std::string(
        kIdealBodyMomentActuatorRuntimeCellFactoryIdentity.version);
    actuator_runtime.runtime_cell_factory_call_shape_id = std::string(
        kIdealBodyMomentActuatorRuntimeCellFactoryCallShapeIdentity);
    actuator_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    actuator_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    actuator.runtime_component = std::move(actuator_runtime);

    gnc::model_sdk::StaticModelDescriptor propulsion;
    propulsion.definition = {
        std::string(kSuppliedPropulsionModelIdentity),
        std::string(kSuppliedPropulsionModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    propulsion.placement = gnc::model_sdk::ModelPlacement::VehicleOutput;
    propulsion.configuration.schema_id =
        std::string(kSuppliedPropulsionConfigSchemaIdentity);
    propulsion.configuration.schema_version =
        kSuppliedPropulsionConfigSchemaVersion;
    propulsion.configuration.fields = {
        {"body_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"center_of_mass_to_application.x_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"center_of_mass_to_application.y_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"center_of_mass_to_application.z_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"fuel_consumption_rate_kilograms_per_second",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"intrinsic_moment_at_application.x_newton_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"intrinsic_moment_at_application.y_newton_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"intrinsic_moment_at_application.z_newton_meters",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"mass_state_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"source_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"thrust_direction.x_unit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"thrust_direction.y_unit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"thrust_direction.z_unit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"thrust_magnitude_newtons",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    propulsion.ports = {
        {"propulsion-body-wrench",
         std::string(kSuppliedPropulsionBodyWrenchContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         make_yyz_slot_codec_descriptor(
             kPropulsionWrenchSlotCodecIdentity,
             kPropulsionWrenchSlotCodecCallShapeIdentity,
             kPropulsionWrenchLayoutIdentity,
             "gnc.operation.yyz.propulsion-wrench")},
        {"mass-flow-interval",
         std::string(kMassFlowIntervalContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::IntervalModel,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::IntervalModel,
         make_yyz_slot_codec_descriptor(
             kMassFlowSlotCodecIdentity,
             kMassFlowSlotCodecCallShapeIdentity,
             kMassFlowLayoutIdentity,
             "gnc.operation.yyz.mass-flow")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor propulsion_runtime;
    propulsion_runtime.recipe_id =
        std::string(kSuppliedPropulsionRecipeIdentity);
    propulsion_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::SampledTransform;
    propulsion_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation};
    propulsion_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Output,
         std::string(kFixedSuppliedPropulsionBoundaryIdentity.id),
         std::string(kFixedSuppliedPropulsionBoundaryIdentity.version),
         std::string(kFixedSuppliedPropulsionRequestContractIdentity),
         std::string(kSuppliedPropulsionOutputContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {},
         {"propulsion-body-wrench", "mass-flow-interval"},
         gnc::model_sdk::StaticStateReadKind::None,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(kFixedSuppliedPropulsionCallShapeIdentity)}};
    propulsion_runtime.schedule = periodic_schedule();
    propulsion_runtime.lifecycle_capabilities = lifecycle;
    propulsion_runtime.definition_builder_id = std::string(
        kFixedSuppliedPropulsionDefinitionBuilderIdentity.id);
    propulsion_runtime.definition_builder_version = std::string(
        kFixedSuppliedPropulsionDefinitionBuilderIdentity.version);
    propulsion_runtime.definition_builder_call_shape_id = std::string(
        kFixedSuppliedPropulsionDefinitionBuilderCallShapeIdentity);
    propulsion_runtime.runtime_cell_factory_id = std::string(
        kFixedSuppliedPropulsionRuntimeCellFactoryIdentity.id);
    propulsion_runtime.runtime_cell_factory_version = std::string(
        kFixedSuppliedPropulsionRuntimeCellFactoryIdentity.version);
    propulsion_runtime.runtime_cell_factory_call_shape_id = std::string(
        kFixedSuppliedPropulsionRuntimeCellFactoryCallShapeIdentity);
    propulsion_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    propulsion_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    propulsion.runtime_component = std::move(propulsion_runtime);

    gnc::model_sdk::StaticModelDescriptor evaluator;
    evaluator.definition = {
        std::string(kCommittedMissionResultModelIdentity),
        std::string(kCommittedMissionResultModelVersion),
        gnc::model_sdk::ModelExecutionForm::RuntimeComponent};
    evaluator.placement = gnc::model_sdk::ModelPlacement::Evaluation;
    evaluator.configuration.schema_id =
        std::string(kCommittedMissionResultConfigSchemaIdentity);
    evaluator.configuration.schema_version =
        kCommittedMissionResultConfigSchemaVersion;
    evaluator.configuration.fields = {
        {"body_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"clock_domain_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"configuration_revision",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"inertial_frame_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"mass_state_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"numerical.absolute_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.condition_limit",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.finite_check",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"numerical.relative_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"numerical.zero_tolerance",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"predicates.0.action",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.0.metric",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.0.predicate_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.0.priority",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"predicates.0.reason_code",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.0.relation",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.0.threshold",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"predicates.1.action",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.1.metric",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.1.predicate_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.1.priority",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"predicates.1.reason_code",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.1.relation",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.1.threshold",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"predicates.2.action",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.2.metric",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.2.predicate_id",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.2.priority",
         gnc::model_sdk::CanonicalConfigValueKind::Integer},
        {"predicates.2.reason_code",
         gnc::model_sdk::CanonicalConfigValueKind::String},
        {"predicates.2.relation",
         gnc::model_sdk::CanonicalConfigValueKind::Enum},
        {"predicates.2.threshold",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"subject",
         gnc::model_sdk::CanonicalConfigValueKind::String},
    };
    evaluator.ports = {
        {"committed-rigid-mass-sequence",
         std::string(kCommittedRigidMassSequenceContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::CurrentCycle},
        {"committed-mission-result",
         std::string(kCommittedMissionResultContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::SampledSignal,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::CurrentCycle,
         make_yyz_slot_codec_descriptor(
             kMissionResultSlotCodecIdentity,
             kMissionResultSlotCodecCallShapeIdentity,
             kMissionResultLayoutIdentity,
             "gnc.operation.yyz.mission-result")},
    };
    gnc::model_sdk::StaticRuntimeComponentDescriptor evaluator_runtime;
    evaluator_runtime.recipe_id =
        std::string(kCommittedMissionResultRecipeIdentity);
    evaluator_runtime.profile =
        gnc::model_sdk::RuntimeCellProfile::Evaluator;
    evaluator_runtime.obligations = {
        gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation};
    evaluator_runtime.obligation_entries = {
        {gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
         gnc::model_sdk::CoarsePhase::Evaluation,
         std::string(kCommittedMissionHistoryEvaluationIdentity.id),
         std::string(kCommittedMissionHistoryEvaluationIdentity.version),
         std::string(kCommittedRigidMassSequenceContractIdentity),
         std::string(kCommittedMissionResultContractIdentity),
         gnc::model_sdk::StaticWorkspaceRequirement::None,
         {"committed-rigid-mass-sequence"},
         {"committed-mission-result"},
         gnc::model_sdk::StaticStateReadKind::None,
         gnc::model_sdk::StaticStateWriteKind::None, {},
         std::string(
             kCommittedMissionHistoryEvaluationCallShapeIdentity)}};
    evaluator_runtime.schedule.trigger =
        gnc::model_sdk::StaticScheduleTrigger::TerminalSequenceReady;
    evaluator_runtime.schedule.step_interval = 0U;
    evaluator_runtime.schedule.offset = 0U;
    evaluator_runtime.schedule.output_hold =
        gnc::model_sdk::HoldPolicy::ZeroOrderHold;
    evaluator_runtime.schedule.max_input_age_steps = 0U;
    evaluator_runtime.lifecycle_capabilities = lifecycle;
    evaluator_runtime.definition_builder_id = std::string(
        kCommittedMissionResultDefinitionBuilderIdentity.id);
    evaluator_runtime.definition_builder_version = std::string(
        kCommittedMissionResultDefinitionBuilderIdentity.version);
    evaluator_runtime.definition_builder_call_shape_id = std::string(
        kCommittedMissionResultDefinitionBuilderCallShapeIdentity);
    evaluator_runtime.runtime_cell_factory_id = std::string(
        kCommittedMissionResultRuntimeCellFactoryIdentity.id);
    evaluator_runtime.runtime_cell_factory_version = std::string(
        kCommittedMissionResultRuntimeCellFactoryIdentity.version);
    evaluator_runtime.runtime_cell_factory_call_shape_id = std::string(
        kCommittedMissionResultRuntimeCellFactoryCallShapeIdentity);
    evaluator_runtime.resource_plan_id =
        std::string(kYyzNoWorkspaceResourcePlanIdentity);
    evaluator_runtime.resource_workspace_requirement =
        gnc::model_sdk::StaticWorkspaceRequirement::None;
    evaluator_runtime.evaluator_history_shape =
        gnc::model_sdk::StaticEvaluatorHistoryShapeDescriptor{
            std::string(kCommittedRigidMassSequenceContractIdentity),
            static_cast<std::uint32_t>(kCommittedMissionHistoryDepth),
            {{std::string(kCommittedMissionRigidHistoryMemberId),
              std::string(kRigidStateSchemaIdentity),
              std::string(kRigidStateLayoutIdentity)},
             {std::string(kCommittedMissionMassHistoryMemberId),
              std::string(kMassStateSchemaIdentity),
              std::string(kMassStateLayoutIdentity)}}};
    evaluator.runtime_component = std::move(evaluator_runtime);

    package.models.push_back(std::move(mass));
    package.models.push_back(std::move(guidance));
    package.models.push_back(std::move(controller));
    package.models.push_back(std::move(actuator));
    package.models.push_back(std::move(propulsion));
    package.models.push_back(std::move(evaluator));
    return package;
}

gnc::model_sdk::StaticPackageImplementation
describe_yyz_rigid_step_implementation(std::string build_fingerprint) {
    const auto package = describe_yyz_rigid_step_package();
    gnc::model_sdk::StaticPackageImplementation implementation;
    implementation.package_id = package.package_id;
    implementation.package_version = package.package_version;
    implementation.build_fingerprint = std::move(build_fingerprint);

    if (const auto* environment =
            find_static_model(package, kUniformEnvironmentModelIdentity);
        environment != nullptr && environment->pure_query.has_value()) {
        append_static_entry<UniformEnvironmentPreparationCall,
                            &prepare_uniform_environment_model>(
            implementation, kUniformEnvironmentPreparationIdentity,
            gnc::model_sdk::StaticEntryKind::Prepare,
            gnc::model_sdk::canonical_prepare_signature(*environment),
            environment->preparation_call_shape_id);
        append_static_entry<UniformEnvironmentQueryCall,
                            &UniformEnvironmentQueryKernel::evaluate>(
            implementation, kUniformEnvironmentQueryIdentity,
            gnc::model_sdk::StaticEntryKind::PureQuery,
            gnc::model_sdk::canonical_query_signature(*environment),
            environment->pure_query->query_call_shape_id);
    }

    if (const auto* aerodynamics =
            find_static_model(package, kAerodynamicTableModelIdentity);
        aerodynamics != nullptr && aerodynamics->pure_query.has_value()) {
        append_static_entry<AerodynamicTablePreparationCall,
                            &prepare_aerodynamic_table_model>(
            implementation, kAerodynamicTablePreparationIdentity,
            gnc::model_sdk::StaticEntryKind::Prepare,
            gnc::model_sdk::canonical_prepare_signature(*aerodynamics),
            aerodynamics->preparation_call_shape_id);
        append_static_entry<AerodynamicTableQueryCall,
                            &AerodynamicTableQueryKernel::evaluate>(
            implementation, kAerodynamicTableQueryIdentity,
            gnc::model_sdk::StaticEntryKind::PureQuery,
            gnc::model_sdk::canonical_query_signature(*aerodynamics),
            aerodynamics->pure_query->query_call_shape_id);
    }

    if (const auto* closure =
            find_static_model(package, kForceMomentClosureModelIdentity);
        closure != nullptr && closure->closure.has_value()) {
        append_static_entry<ForceMomentClosurePreparationCall,
                            &prepare_force_moment_closure_model>(
            implementation, kForceMomentClosurePreparationIdentity,
            gnc::model_sdk::StaticEntryKind::Prepare,
            gnc::model_sdk::canonical_prepare_signature(*closure),
            closure->preparation_call_shape_id);
        append_static_entry<ForceMomentClosureCall,
                            &ForceMomentClosureKernel::evaluate>(
            implementation, kForceMomentClosureKernelIdentity,
            gnc::model_sdk::StaticEntryKind::Closure,
            gnc::model_sdk::canonical_closure_signature(*closure),
            closure->closure->closure_call_shape_id);
        if (const auto* output = find_static_port(*closure, "form-input");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                RigidFormInputSlotCodecGetter,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    RigidFormInput>>(implementation, *closure, *output);
        }
    }

    if (const auto* rigid =
            find_static_model(package, kRigidStepModelIdentity);
        rigid != nullptr && rigid->runtime_component.has_value()) {
        append_definition_builder_entry<
            ControlledRigidDefinitionBuilderCall,
            &build_controlled_rigid_boundary_definition>(implementation,
                                                         *rigid);
        append_runtime_cell_factory_entry<
            ControlledRigidRuntimeCellFactoryCall,
            &create_controlled_rigid_runtime_cell>(implementation, *rigid);
        append_state_codec_entry<RigidStateCodecGetter,
                                 &rigid_state_codec>(implementation, *rigid);
        append_static_entry<RigidInitialStateCall,
                            &RigidInitialStateBuilder::build>(
            implementation, kRigidInitialStateBuilderIdentity,
            gnc::model_sdk::StaticEntryKind::InitialState,
            gnc::model_sdk::canonical_initial_state_signature(*rigid),
            rigid->runtime_component->state_owner
                ->initial_state_builder_call_shape_id,
            std::string(kRigidStateLayoutIdentity));
        append_runtime_entry<RigidPublishProjectionCall,
                             &project_committed_rigid_observation>(
            implementation, *rigid,
            gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
            gnc::model_sdk::StaticEntryKind::PublishProjection,
            std::string(kRigidStateLayoutIdentity));
        append_runtime_entry<ControlledRigidBoundaryRuntimeCall,
            &ControlledRigidBoundaryEvaluationKernel::
                prepare_for_integration>(
            implementation, *rigid,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation,
            std::string(kRigidStateLayoutIdentity));
        append_runtime_entry<RigidDerivativeCall,
                             &RigidDerivativeKernel::evaluate>(
            implementation, *rigid,
            gnc::model_sdk::RuntimeExecutionObligation::DerivativeEvaluation,
            gnc::model_sdk::StaticEntryKind::DerivativeEvaluation,
            std::string(kRigidStateLayoutIdentity));
        if (const auto* output = find_static_port(
                *rigid, "committed-rigid-observation");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                RigidObservationSlotCodecGetter,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    CommittedRigidObservation>>(
                implementation, *rigid, *output);
        }
        if (const auto* boundary = find_runtime_entry(
                *rigid,
                gnc::model_sdk::RuntimeExecutionObligation::
                    BoundaryEvaluation);
            boundary != nullptr && boundary->result_codec.has_value()) {
            append_result_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<
                    ControlledRigidBoundaryPreparationSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    ControlledRigidBoundaryPreparationOutput>>(
                implementation, *rigid, *boundary);
        }
    }

    if (const auto* mass =
            find_static_model(package, kScalarBurnMassModelIdentity);
        mass != nullptr && mass->runtime_component.has_value()) {
        append_definition_builder_entry<
            ScalarBurnMassDefinitionBuilderCall,
            &build_scalar_burn_mass_definition>(implementation, *mass);
        append_runtime_cell_factory_entry<
            ScalarBurnMassRuntimeCellFactoryCall,
            &create_scalar_burn_mass_runtime_cell>(implementation, *mass);
        append_state_codec_entry<MassStateCodecGetter,
                                 &mass_state_codec>(implementation, *mass);
        append_static_entry<MassInitialStateCall,
                            &build_scalar_burn_mass_initial_state>(
            implementation, kMassInitialStateBuilderIdentity,
            gnc::model_sdk::StaticEntryKind::InitialState,
            gnc::model_sdk::canonical_initial_state_signature(*mass),
            mass->runtime_component->state_owner
                ->initial_state_builder_call_shape_id,
            std::string(kMassStateLayoutIdentity));
        append_runtime_entry<MassPublishProjectionCall,
                             &project_committed_mass_properties>(
            implementation, *mass,
            gnc::model_sdk::RuntimeExecutionObligation::PublishProjection,
            gnc::model_sdk::StaticEntryKind::PublishProjection,
            std::string(kMassStateLayoutIdentity));
        append_runtime_entry<MassIntervalEvolutionCall,
                             &evaluate_scalar_burn_mass_interval>(
            implementation, *mass,
            gnc::model_sdk::RuntimeExecutionObligation::IntervalEvolution,
            gnc::model_sdk::StaticEntryKind::IntervalEvolution,
            std::string(kMassStateLayoutIdentity));
        if (const auto* output = find_static_port(*mass, "mass-properties");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<
                    MassPropertiesSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    MassPropertiesInput>>(implementation, *mass, *output);
        }
    }

    if (const auto* guidance =
            find_static_model(package, kAltitudePitchGuidanceModelIdentity);
        guidance != nullptr) {
        append_definition_builder_entry<
            AltitudePitchGuidanceDefinitionBuilderCall,
            &build_altitude_pitch_guidance_definition>(implementation,
                                                       *guidance);
        append_runtime_cell_factory_entry<
            AltitudePitchGuidanceRuntimeCellFactoryCall,
            &create_altitude_pitch_guidance_runtime_cell>(implementation,
                                                          *guidance);
        append_runtime_entry<AltitudePitchGuidanceCall,
                             &AltitudePitchGuidanceKernel::evaluate>(
            implementation, *guidance,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation);
        if (const auto* output = find_static_port(*guidance, "guidance-output");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<GuidanceOutputSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    AltitudePitchGuidanceOutput>>(
                implementation, *guidance, *output);
        }
    }
    if (const auto* controller =
            find_static_model(package, kPitchMomentControllerModelIdentity);
        controller != nullptr) {
        append_definition_builder_entry<
            PitchMomentControllerDefinitionBuilderCall,
            &build_pitch_moment_controller_definition>(implementation,
                                                       *controller);
        append_runtime_cell_factory_entry<
            PitchMomentControllerRuntimeCellFactoryCall,
            &create_pitch_moment_controller_runtime_cell>(implementation,
                                                          *controller);
        append_runtime_entry<PitchMomentControllerCall,
                             &PitchMomentControllerKernel::evaluate>(
            implementation, *controller,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation);
        if (const auto* output = find_static_port(
                *controller, "controller-output");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<
                    ControllerOutputSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    PitchMomentControllerOutput>>(
                implementation, *controller, *output);
        }
    }
    if (const auto* actuator =
            find_static_model(package, kIdealBodyMomentActuatorModelIdentity);
        actuator != nullptr) {
        append_definition_builder_entry<
            IdealBodyMomentActuatorDefinitionBuilderCall,
            &build_ideal_body_moment_actuator_definition>(implementation,
                                                          *actuator);
        append_runtime_cell_factory_entry<
            IdealBodyMomentActuatorRuntimeCellFactoryCall,
            &create_ideal_body_moment_actuator_runtime_cell>(
                implementation, *actuator);
        append_runtime_entry<IdealBodyMomentActuatorCall,
                             &IdealBodyMomentActuatorKernel::evaluate>(
            implementation, *actuator,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation);
        if (const auto* output = find_static_port(*actuator, "actuator-output");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<ActuatorOutputSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    IdealBodyMomentActuatorOutput>>(
                implementation, *actuator, *output);
        }
    }
    if (const auto* propulsion =
            find_static_model(package, kSuppliedPropulsionModelIdentity);
        propulsion != nullptr) {
        append_definition_builder_entry<
            FixedSuppliedPropulsionDefinitionBuilderCall,
            &build_fixed_supplied_propulsion_definition>(implementation,
                                                         *propulsion);
        append_runtime_cell_factory_entry<
            FixedSuppliedPropulsionRuntimeCellFactoryCall,
            &create_fixed_supplied_propulsion_runtime_cell>(
                implementation, *propulsion);
        append_runtime_entry<FixedSuppliedPropulsionCall,
                             &FixedSuppliedPropulsionBoundaryKernel::evaluate>(
            implementation, *propulsion,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation);
        if (const auto* output = find_static_port(
                *propulsion, "propulsion-body-wrench");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<
                    PropulsionWrenchSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    SuppliedPropulsionBodyWrench>>(
                implementation, *propulsion, *output);
        }
        if (const auto* output = find_static_port(
                *propulsion, "mass-flow-interval");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<MassFlowSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    MassFlowIntervalInput>>(
                implementation, *propulsion, *output);
        }
    }
    if (const auto* evaluator =
            find_static_model(package, kCommittedMissionResultModelIdentity);
        evaluator != nullptr) {
        append_definition_builder_entry<
            CommittedMissionResultDefinitionBuilderCall,
            &build_committed_mission_result_definition>(implementation,
                                                        *evaluator);
        append_runtime_cell_factory_entry<
            CommittedMissionResultRuntimeCellFactoryCall,
            &create_committed_mission_result_runtime_cell>(implementation,
                                                           *evaluator);
        append_runtime_entry<CommittedMissionHistoryEvaluationCall,
            &CommittedMissionHistoryEvaluationKernel::evaluate>(
            implementation, *evaluator,
            gnc::model_sdk::RuntimeExecutionObligation::BoundaryEvaluation,
            gnc::model_sdk::StaticEntryKind::BoundaryEvaluation, {},
            &kCommittedMissionHistoryImplementationWitness);
        if (const auto* output = find_static_port(
                *evaluator, "committed-mission-result");
            output != nullptr && output->slot_codec.has_value()) {
            append_slot_codec_entry<
                gnc::model_sdk::InProcessCodecGetter<MissionResultSlotCodec>,
                &gnc::model_sdk::typed_in_process_slot_codec<
                    CommittedMissionResultOutput>>(
                implementation, *evaluator, *output);
        }
    }

    implementation.state_layouts = {
        {std::string(kRigidStateLayoutIdentity), sizeof(RigidState),
         alignof(RigidState)},
        {std::string(kMassStateLayoutIdentity), sizeof(MassState),
         alignof(MassState)},
    };
    implementation.value_layouts = {
        {std::string(kRigidFormInputContractIdentity),
         sizeof(RigidFormInput), alignof(RigidFormInput),
         std::string(kRigidFormInputLayoutIdentity)},
        {std::string(kMassPropertiesContractIdentity),
         sizeof(MassPropertiesInput), alignof(MassPropertiesInput),
         std::string(kMassPropertiesLayoutIdentity)},
        {std::string(kSuppliedPropulsionBodyWrenchContractIdentity),
         sizeof(SuppliedPropulsionBodyWrench),
         alignof(SuppliedPropulsionBodyWrench),
         std::string(kPropulsionWrenchLayoutIdentity)},
        {std::string(kIdealBodyMomentActuatorOutputContractIdentity),
         sizeof(IdealBodyMomentActuatorOutput),
         alignof(IdealBodyMomentActuatorOutput),
         std::string(kActuatorOutputLayoutIdentity)},
        {std::string(kRigidObservationContractIdentity),
         sizeof(CommittedRigidObservation),
         alignof(CommittedRigidObservation),
         std::string(kRigidObservationLayoutIdentity)},
        {std::string(kMassFlowIntervalContractIdentity),
         sizeof(MassFlowIntervalInput), alignof(MassFlowIntervalInput),
         std::string(kMassFlowLayoutIdentity)},
        {std::string(kAltitudePitchGuidanceOutputContractIdentity),
         sizeof(AltitudePitchGuidanceOutput),
         alignof(AltitudePitchGuidanceOutput),
         std::string(kGuidanceOutputLayoutIdentity)},
        {std::string(kPitchMomentControllerOutputContractIdentity),
         sizeof(PitchMomentControllerOutput),
         alignof(PitchMomentControllerOutput),
         std::string(kControllerOutputLayoutIdentity)},
        {std::string(kCommittedMissionResultContractIdentity),
         sizeof(CommittedMissionResultOutput),
         alignof(CommittedMissionResultOutput),
         std::string(kMissionResultLayoutIdentity)},
        {std::string(kControlledRigidBoundaryPreparationContractIdentity),
         sizeof(ControlledRigidBoundaryPreparationOutput),
         alignof(ControlledRigidBoundaryPreparationOutput),
         std::string(kControlledRigidBoundaryPreparationLayoutIdentity)},
    };
    return implementation;
}

NumericalOutcome<ControlledRigidRuntimeCell>
create_controlled_rigid_runtime_cell(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ControlledRigidRuntimeCellBindings& bindings) {
    const auto& invocations = bindings.bound_invocations;
    const bool callsites_are_unique =
        bindings.publish_projection_callsite_handle !=
            bindings.boundary_evaluation_callsite_handle &&
        bindings.publish_projection_callsite_handle !=
            bindings.derivative_evaluation_callsite_handle &&
        bindings.boundary_evaluation_callsite_handle !=
            bindings.derivative_evaluation_callsite_handle;
    const bool invocations_are_unique =
        invocations.environment.invocation_handle !=
            invocations.frozen_form.aerodynamic.invocation_handle &&
        invocations.environment.invocation_handle !=
            invocations.frozen_form.closure.invocation_handle &&
        invocations.frozen_form.aerodynamic.invocation_handle !=
            invocations.frozen_form.closure.invocation_handle;
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.state_block_handle == 0U ||
        bindings.publish_projection_callsite_handle == 0U ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.derivative_evaluation_callsite_handle == 0U ||
        bindings.observation_output.slot_handle == 0U ||
        bindings.observation_output.writer_token.value == 0U ||
        bindings.boundary_preparation_output.slot_handle == 0U ||
        bindings.boundary_preparation_output.writer_token.value == 0U ||
        bindings.mass_properties_input_slot_handle == 0U ||
        bindings.propulsion_body_wrench_input_slot_handle == 0U ||
        bindings.actuator_output_input_slot_handle == 0U ||
        bindings.held_form_result_slot_handle == 0U ||
        !callsites_are_unique || !invocations_are_unique ||
        bindings.publish_projection == nullptr ||
        bindings.boundary_evaluation == nullptr ||
        bindings.derivative_evaluation == nullptr ||
        invocations.environment.invocation_handle == 0U ||
        invocations.environment.provider_plan_handle == 0U ||
        invocations.environment.entry_handle == 0U ||
        invocations.environment.prepared_model == nullptr ||
        invocations.environment.callable == nullptr ||
        invocations.frozen_form.aerodynamic.invocation_handle == 0U ||
        invocations.frozen_form.aerodynamic.provider_plan_handle == 0U ||
        invocations.frozen_form.aerodynamic.entry_handle == 0U ||
        invocations.frozen_form.aerodynamic.prepared_model == nullptr ||
        invocations.frozen_form.aerodynamic.callable == nullptr ||
        invocations.frozen_form.closure.invocation_handle == 0U ||
        invocations.frozen_form.closure.provider_plan_handle == 0U ||
        invocations.frozen_form.closure.entry_handle == 0U ||
        invocations.frozen_form.closure.prepared_model == nullptr ||
        invocations.frozen_form.closure.callable == nullptr) {
        return mass_commit_failure<ControlledRigidRuntimeCell>(
            kControlledRigidRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<ControlledRigidRuntimeCell>::with_value(
        NumericalStatus::Success,
        ControlledRigidRuntimeCell{definition, context, bindings},
        mass_commit_evidence(kControlledRigidRuntimeCellFactoryIdentity,
                             "runtime-cell"));
}

NumericalOutcome<ScalarBurnMassRuntimeCell>
create_scalar_burn_mass_runtime_cell(
    const ScalarBurnMassDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ScalarBurnMassRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.state_block_handle == 0U ||
        bindings.publish_projection_callsite_handle == 0U ||
        bindings.interval_evolution_callsite_handle == 0U ||
        bindings.publish_projection_callsite_handle ==
            bindings.interval_evolution_callsite_handle ||
        bindings.mass_properties_output.slot_handle == 0U ||
        bindings.mass_properties_output.writer_token.value == 0U ||
        bindings.candidate_state_writer.value == 0U ||
        bindings.mass_flow_input_slot_handle == 0U ||
        bindings.publish_projection == nullptr ||
        bindings.interval_evolution == nullptr) {
        return mass_commit_failure<ScalarBurnMassRuntimeCell>(
            kScalarBurnMassRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<ScalarBurnMassRuntimeCell>::with_value(
        NumericalStatus::Success,
        ScalarBurnMassRuntimeCell{definition, context, bindings},
        mass_commit_evidence(kScalarBurnMassRuntimeCellFactoryIdentity,
                             "runtime-cell"));
}

NumericalOutcome<AltitudePitchGuidanceRuntimeCell>
create_altitude_pitch_guidance_runtime_cell(
    const AltitudePitchGuidanceDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const AltitudePitchGuidanceRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.observation_input_slot_handle == 0U ||
        bindings.guidance_output.slot_handle == 0U ||
        bindings.guidance_output.writer_token.value == 0U ||
        bindings.boundary_evaluation == nullptr) {
        return mass_commit_failure<AltitudePitchGuidanceRuntimeCell>(
            kAltitudePitchGuidanceRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<AltitudePitchGuidanceRuntimeCell>::with_value(
        NumericalStatus::Success,
        AltitudePitchGuidanceRuntimeCell{definition, context, bindings},
        mass_commit_evidence(
            kAltitudePitchGuidanceRuntimeCellFactoryIdentity,
            "runtime-cell"));
}

NumericalOutcome<PitchMomentControllerRuntimeCell>
create_pitch_moment_controller_runtime_cell(
    const PitchMomentControllerDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const PitchMomentControllerRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.guidance_input_slot_handle == 0U ||
        bindings.controller_output.slot_handle == 0U ||
        bindings.controller_output.writer_token.value == 0U ||
        bindings.boundary_evaluation == nullptr) {
        return mass_commit_failure<PitchMomentControllerRuntimeCell>(
            kPitchMomentControllerRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<PitchMomentControllerRuntimeCell>::with_value(
        NumericalStatus::Success,
        PitchMomentControllerRuntimeCell{definition, context, bindings},
        mass_commit_evidence(
            kPitchMomentControllerRuntimeCellFactoryIdentity,
            "runtime-cell"));
}

NumericalOutcome<IdealBodyMomentActuatorRuntimeCell>
create_ideal_body_moment_actuator_runtime_cell(
    const IdealBodyMomentActuatorDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const IdealBodyMomentActuatorRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.controller_input_slot_handle == 0U ||
        bindings.actuator_output.slot_handle == 0U ||
        bindings.actuator_output.writer_token.value == 0U ||
        bindings.boundary_evaluation == nullptr) {
        return mass_commit_failure<IdealBodyMomentActuatorRuntimeCell>(
            kIdealBodyMomentActuatorRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<IdealBodyMomentActuatorRuntimeCell>::with_value(
        NumericalStatus::Success,
        IdealBodyMomentActuatorRuntimeCell{definition, context, bindings},
        mass_commit_evidence(
            kIdealBodyMomentActuatorRuntimeCellFactoryIdentity,
            "runtime-cell"));
}

NumericalOutcome<FixedSuppliedPropulsionRuntimeCell>
create_fixed_supplied_propulsion_runtime_cell(
    const FixedSuppliedPropulsionDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const FixedSuppliedPropulsionRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.propulsion_wrench_output.slot_handle == 0U ||
        bindings.propulsion_wrench_output.writer_token.value == 0U ||
        bindings.mass_flow_output.slot_handle == 0U ||
        bindings.mass_flow_output.writer_token.value == 0U ||
        bindings.boundary_evaluation == nullptr) {
        return mass_commit_failure<FixedSuppliedPropulsionRuntimeCell>(
            kFixedSuppliedPropulsionRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<FixedSuppliedPropulsionRuntimeCell>::with_value(
        NumericalStatus::Success,
        FixedSuppliedPropulsionRuntimeCell{definition, context, bindings},
        mass_commit_evidence(
            kFixedSuppliedPropulsionRuntimeCellFactoryIdentity,
            "runtime-cell"));
}

NumericalOutcome<CommittedMissionResultRuntimeCell>
create_committed_mission_result_runtime_cell(
    const CommittedMissionResultDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const CommittedMissionResultRuntimeCellBindings& bindings) {
    if (!valid_runtime_cell_factory_context(context) ||
        bindings.boundary_evaluation_callsite_handle == 0U ||
        bindings.committed_history_handle == 0U ||
        bindings.mission_result_output.slot_handle == 0U ||
        bindings.mission_result_output.writer_token.value == 0U ||
        bindings.boundary_evaluation == nullptr) {
        return mass_commit_failure<CommittedMissionResultRuntimeCell>(
            kCommittedMissionResultRuntimeCellFactoryIdentity,
            NumericalStatus::DomainError, "compiled-bindings");
    }
    return NumericalOutcome<CommittedMissionResultRuntimeCell>::with_value(
        NumericalStatus::Success,
        CommittedMissionResultRuntimeCell{definition, context, bindings},
        mass_commit_evidence(
            kCommittedMissionResultRuntimeCellFactoryIdentity,
            "runtime-cell"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_scalar_burn_mass_config(
    const ScalarBurnMassDefinition& definition) {
    const auto& policy = definition.numerical_policy;
    return {
        std::string(kScalarBurnMassConfigSchemaIdentity),
        kScalarBurnMassConfigSchemaVersion,
        {
            {"mass_state_id", definition.mass_state_id},
            {"numerical.absolute_tolerance", policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance", policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
        },
    };
}

NumericalOutcome<ScalarBurnMassDefinition>
build_scalar_burn_mass_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return mass_commit_failure<ScalarBurnMassDefinition>(
            kScalarBurnMassDefinitionBuilderIdentity,
            NumericalStatus::DomainError, "canonical-config");
    };
    static constexpr std::array<std::string_view, 6U> kFields{
        "mass_state_id", "numerical.absolute_tolerance",
        "numerical.condition_limit", "numerical.finite_check",
        "numerical.relative_tolerance", "numerical.zero_tolerance"};
    if (!exact_config_fields(
            configuration, kScalarBurnMassConfigSchemaIdentity,
            kScalarBurnMassConfigSchemaVersion, kFields)) {
        return failure();
    }
    const auto* mass_state_id =
        std::get_if<std::string>(&configuration.fields[0U].value);
    const auto policy =
        config_numerical_policy(configuration, 1U, 2U, 3U, 4U, 5U);
    if (mass_state_id == nullptr || mass_state_id->empty() ||
        !policy.has_value()) {
        return failure();
    }
    return NumericalOutcome<ScalarBurnMassDefinition>::with_value(
        NumericalStatus::Success,
        ScalarBurnMassDefinition{
            std::string(kScalarBurnMassModelIdentity),
            std::string(kScalarBurnMassModelVersion), *mass_state_id,
            *policy},
        mass_commit_evidence(kScalarBurnMassDefinitionBuilderIdentity,
                             "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_pitch_moment_controller_config(
    const PitchMomentControllerDefinition& definition) {
    const auto& policy = definition.numerical_policy;
    return {
        std::string(kPitchMomentControllerConfigSchemaIdentity),
        kPitchMomentControllerConfigSchemaVersion,
        {
            {"body_frame_id", definition.body_frame.id},
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision", definition.configuration_revision},
            {"moment_command_limit_newton_meters",
             definition.moment_command_limit_newton_meters},
            {"numerical.absolute_tolerance", policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance", policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
            {"pitch_error_gain_newton_meters_per_radian",
             definition.pitch_error_gain_newton_meters_per_radian},
            {"pitch_rate_gain_newton_meter_seconds_per_radian",
             definition
                 .pitch_rate_gain_newton_meter_seconds_per_radian},
        },
    };
}

NumericalOutcome<PitchMomentControllerDefinition>
build_pitch_moment_controller_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return mass_commit_failure<PitchMomentControllerDefinition>(
            kPitchMomentControllerDefinitionBuilderIdentity,
            NumericalStatus::DomainError, "canonical-config");
    };
    static constexpr std::array<std::string_view, 11U> kFields{
        "body_frame_id",
        "clock_domain_id",
        "configuration_revision",
        "moment_command_limit_newton_meters",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
        "pitch_error_gain_newton_meters_per_radian",
        "pitch_rate_gain_newton_meter_seconds_per_radian",
    };
    if (!exact_config_fields(
            configuration, kPitchMomentControllerConfigSchemaIdentity,
            kPitchMomentControllerConfigSchemaVersion, kFields)) {
        return failure();
    }
    const auto* body_frame =
        std::get_if<std::string>(&configuration.fields[0U].value);
    const auto* clock_domain =
        std::get_if<std::string>(&configuration.fields[1U].value);
    const auto* revision =
        std::get_if<std::int64_t>(&configuration.fields[2U].value);
    const auto* command_limit =
        std::get_if<double>(&configuration.fields[3U].value);
    const auto policy =
        config_numerical_policy(configuration, 4U, 5U, 6U, 7U, 8U);
    const auto* error_gain =
        std::get_if<double>(&configuration.fields[9U].value);
    const auto* rate_gain =
        std::get_if<double>(&configuration.fields[10U].value);
    if (body_frame == nullptr || body_frame->empty() ||
        clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 || command_limit == nullptr ||
        error_gain == nullptr || rate_gain == nullptr ||
        !canonical_double(*command_limit) ||
        !canonical_double(*error_gain) || !canonical_double(*rate_gain) ||
        *command_limit <= 0.0 || *error_gain < 0.0 || *rate_gain < 0.0 ||
        !policy.has_value()) {
        return failure();
    }
    PitchMomentControllerDefinition definition;
    definition.model_id = std::string(kPitchMomentControllerModelIdentity);
    definition.model_version =
        std::string(kPitchMomentControllerModelVersion);
    definition.body_frame.id = *body_frame;
    definition.clock_domain.id = *clock_domain;
    definition.configuration_revision = *revision;
    definition.pitch_error_gain_newton_meters_per_radian = *error_gain;
    definition.pitch_rate_gain_newton_meter_seconds_per_radian = *rate_gain;
    definition.moment_command_limit_newton_meters = *command_limit;
    definition.numerical_policy = *policy;
    return NumericalOutcome<PitchMomentControllerDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        mass_commit_evidence(
            kPitchMomentControllerDefinitionBuilderIdentity,
            "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_ideal_body_moment_actuator_config(
    const IdealBodyMomentActuatorDefinition& definition) {
    const auto& policy = definition.numerical_policy;
    return {
        std::string(kIdealBodyMomentActuatorConfigSchemaIdentity),
        kIdealBodyMomentActuatorConfigSchemaVersion,
        {
            {"body_frame_id", definition.body_frame.id},
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision", definition.configuration_revision},
            {"numerical.absolute_tolerance", policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance", policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
            {"realization_gain", definition.realization_gain},
            {"source_id", definition.source_id},
        },
    };
}

NumericalOutcome<IdealBodyMomentActuatorDefinition>
build_ideal_body_moment_actuator_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return mass_commit_failure<IdealBodyMomentActuatorDefinition>(
            kIdealBodyMomentActuatorDefinitionBuilderIdentity,
            NumericalStatus::DomainError, "canonical-config");
    };
    static constexpr std::array<std::string_view, 10U> kFields{
        "body_frame_id",
        "clock_domain_id",
        "configuration_revision",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
        "realization_gain",
        "source_id",
    };
    if (!exact_config_fields(
            configuration, kIdealBodyMomentActuatorConfigSchemaIdentity,
            kIdealBodyMomentActuatorConfigSchemaVersion, kFields)) {
        return failure();
    }
    const auto* body_frame =
        std::get_if<std::string>(&configuration.fields[0U].value);
    const auto* clock_domain =
        std::get_if<std::string>(&configuration.fields[1U].value);
    const auto* revision =
        std::get_if<std::int64_t>(&configuration.fields[2U].value);
    const auto policy =
        config_numerical_policy(configuration, 3U, 4U, 5U, 6U, 7U);
    const auto* gain =
        std::get_if<double>(&configuration.fields[8U].value);
    const auto* source_id =
        std::get_if<std::string>(&configuration.fields[9U].value);
    if (body_frame == nullptr || body_frame->empty() ||
        clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 || gain == nullptr ||
        !canonical_double(*gain) || source_id == nullptr ||
        source_id->empty() || !policy.has_value() ||
        !near(*gain, 1.0, *policy)) {
        return failure();
    }
    IdealBodyMomentActuatorDefinition definition;
    definition.model_id =
        std::string(kIdealBodyMomentActuatorModelIdentity);
    definition.model_version =
        std::string(kIdealBodyMomentActuatorModelVersion);
    definition.source_id = *source_id;
    definition.body_frame.id = *body_frame;
    definition.clock_domain.id = *clock_domain;
    definition.configuration_revision = *revision;
    definition.realization_gain = *gain;
    definition.numerical_policy = *policy;
    return NumericalOutcome<IdealBodyMomentActuatorDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        mass_commit_evidence(
            kIdealBodyMomentActuatorDefinitionBuilderIdentity,
            "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_fixed_supplied_propulsion_config(
    const FixedSuppliedPropulsionDefinition& definition) {
    const auto& policy = definition.propulsion.numerical_policy;
    return {
        std::string(kSuppliedPropulsionConfigSchemaIdentity),
        kSuppliedPropulsionConfigSchemaVersion,
        {
            {"body_frame_id", definition.propulsion.body_frame.id},
            {"center_of_mass_to_application.x_meters",
             definition.center_of_mass_to_application.value(0)},
            {"center_of_mass_to_application.y_meters",
             definition.center_of_mass_to_application.value(1)},
            {"center_of_mass_to_application.z_meters",
             definition.center_of_mass_to_application.value(2)},
            {"clock_domain_id", definition.propulsion.clock_domain.id},
            {"fuel_consumption_rate_kilograms_per_second",
             definition.fuel_consumption_rate_kilograms_per_second},
            {"intrinsic_moment_at_application.x_newton_meters",
             definition.intrinsic_moment_at_application.value(0)},
            {"intrinsic_moment_at_application.y_newton_meters",
             definition.intrinsic_moment_at_application.value(1)},
            {"intrinsic_moment_at_application.z_newton_meters",
             definition.intrinsic_moment_at_application.value(2)},
            {"mass_state_id", definition.propulsion.mass_state_id},
            {"numerical.absolute_tolerance", policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance", policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
            {"source_id", definition.propulsion.source_id},
            {"thrust_direction.x_unit", definition.thrust_direction.value(0)},
            {"thrust_direction.y_unit", definition.thrust_direction.value(1)},
            {"thrust_direction.z_unit", definition.thrust_direction.value(2)},
            {"thrust_magnitude_newtons",
             definition.thrust_magnitude_newtons},
        },
    };
}

NumericalOutcome<FixedSuppliedPropulsionDefinition>
build_fixed_supplied_propulsion_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return mass_commit_failure<FixedSuppliedPropulsionDefinition>(
            kFixedSuppliedPropulsionDefinitionBuilderIdentity,
            NumericalStatus::DomainError, "canonical-config");
    };
    static constexpr std::array<std::string_view, 20U> kFields{
        "body_frame_id",
        "center_of_mass_to_application.x_meters",
        "center_of_mass_to_application.y_meters",
        "center_of_mass_to_application.z_meters",
        "clock_domain_id",
        "fuel_consumption_rate_kilograms_per_second",
        "intrinsic_moment_at_application.x_newton_meters",
        "intrinsic_moment_at_application.y_newton_meters",
        "intrinsic_moment_at_application.z_newton_meters",
        "mass_state_id",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
        "source_id",
        "thrust_direction.x_unit",
        "thrust_direction.y_unit",
        "thrust_direction.z_unit",
        "thrust_magnitude_newtons",
    };
    if (!exact_config_fields(
            configuration, kSuppliedPropulsionConfigSchemaIdentity,
            kSuppliedPropulsionConfigSchemaVersion, kFields)) {
        return failure();
    }
    const auto* body_frame =
        std::get_if<std::string>(&configuration.fields[0U].value);
    const auto* clock_domain =
        std::get_if<std::string>(&configuration.fields[4U].value);
    const auto* mass_state_id =
        std::get_if<std::string>(&configuration.fields[9U].value);
    const auto policy = config_numerical_policy(
        configuration, 10U, 11U, 12U, 13U, 14U);
    const auto* source_id =
        std::get_if<std::string>(&configuration.fields[15U].value);
    const std::array<std::size_t, 11U> kNumberIndices{
        1U, 2U, 3U, 5U, 6U, 7U, 8U, 16U, 17U, 18U, 19U};
    std::array<const double*, kNumberIndices.size()> numbers{};
    for (std::size_t index = 0U; index < kNumberIndices.size(); ++index) {
        numbers[index] = std::get_if<double>(
            &configuration.fields[kNumberIndices[index]].value);
    }
    if (body_frame == nullptr || body_frame->empty() ||
        clock_domain == nullptr || clock_domain->empty() ||
        mass_state_id == nullptr || mass_state_id->empty() ||
        source_id == nullptr || source_id->empty() || !policy.has_value() ||
        std::any_of(numbers.begin(), numbers.end(), [](const double* value) {
            return value == nullptr || !canonical_double(*value);
        })) {
        return failure();
    }
    FixedSuppliedPropulsionDefinition definition;
    definition.propulsion.model_id =
        std::string(kSuppliedPropulsionModelIdentity);
    definition.propulsion.model_version =
        std::string(kSuppliedPropulsionModelVersion);
    definition.propulsion.source_id = *source_id;
    definition.propulsion.body_frame.id = *body_frame;
    definition.propulsion.clock_domain.id = *clock_domain;
    definition.propulsion.mass_state_id = *mass_state_id;
    definition.propulsion.numerical_policy = *policy;
    definition.center_of_mass_to_application.value =
        Vec3{*numbers[0U], *numbers[1U], *numbers[2U]};
    definition.fuel_consumption_rate_kilograms_per_second = *numbers[3U];
    definition.intrinsic_moment_at_application.value =
        Vec3{*numbers[4U], *numbers[5U], *numbers[6U]};
    definition.thrust_direction.value =
        Vec3{*numbers[7U], *numbers[8U], *numbers[9U]};
    definition.thrust_magnitude_newtons = *numbers[10U];
    const double direction_norm = definition.thrust_direction.value.norm();
    if (definition.thrust_magnitude_newtons < 0.0 ||
        definition.fuel_consumption_rate_kilograms_per_second < 0.0 ||
        !std::isfinite(direction_norm) ||
        !near(direction_norm, 1.0, definition.propulsion.numerical_policy)) {
        return failure();
    }
    return NumericalOutcome<FixedSuppliedPropulsionDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        mass_commit_evidence(
            kFixedSuppliedPropulsionDefinitionBuilderIdentity,
            "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_controlled_rigid_boundary_config(
    const ControlledRigidBoundaryEvaluationDefinition& definition) {
    const auto& algorithm = definition.rigid.algorithm;
    const auto& candidate = algorithm.candidate_attitude_policy;
    const auto& evaluation = algorithm.attitude_evaluation_policy;
    const auto& numerical = algorithm.numerical_policy;
    return {
        std::string(kRigidStepConfigSchemaIdentity),
        kRigidStepConfigSchemaVersion,
        {
            {"attitude.candidate.normalization",
             gnc::model_sdk::CanonicalEnumValue{
                 normalization_token(candidate.normalization)}},
            {"attitude.candidate.numerical.absolute_tolerance",
             candidate.numerical.absolute_tolerance},
            {"attitude.candidate.numerical.condition_limit",
             candidate.numerical.condition_limit},
            {"attitude.candidate.numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(candidate.numerical.finite_check)}},
            {"attitude.candidate.numerical.relative_tolerance",
             candidate.numerical.relative_tolerance},
            {"attitude.candidate.numerical.zero_tolerance",
             candidate.numerical.zero_tolerance},
            {"attitude.evaluation.normalization",
             gnc::model_sdk::CanonicalEnumValue{
                 normalization_token(evaluation.normalization)}},
            {"attitude.evaluation.numerical.absolute_tolerance",
             evaluation.numerical.absolute_tolerance},
            {"attitude.evaluation.numerical.condition_limit",
             evaluation.numerical.condition_limit},
            {"attitude.evaluation.numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(evaluation.numerical.finite_check)}},
            {"attitude.evaluation.numerical.relative_tolerance",
             evaluation.numerical.relative_tolerance},
            {"attitude.evaluation.numerical.zero_tolerance",
             evaluation.numerical.zero_tolerance},
            {"combined_wrench_source_id",
             definition.wrench_adapter.combined_wrench_source_id},
            {"fixed_step_seconds", algorithm.fixed_step_seconds},
            {"inertial_frame_id", definition.rigid.inertial_frame.id},
            {"numerical.absolute_tolerance",
             numerical.absolute_tolerance},
            {"numerical.condition_limit", numerical.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(numerical.finite_check)}},
            {"numerical.relative_tolerance",
             numerical.relative_tolerance},
            {"numerical.zero_tolerance", numerical.zero_tolerance},
        },
    };
}

NumericalOutcome<ControlledRigidBoundaryEvaluationDefinition>
build_controlled_rigid_boundary_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return NumericalOutcome<
            ControlledRigidBoundaryEvaluationDefinition>::failure(
                NumericalStatus::DomainError,
                mass_commit_evidence(
                    kControlledRigidDefinitionBuilderIdentity,
                    "canonical-config"));
    };
    static constexpr std::array<std::string_view, 20U> kFields{
        "attitude.candidate.normalization",
        "attitude.candidate.numerical.absolute_tolerance",
        "attitude.candidate.numerical.condition_limit",
        "attitude.candidate.numerical.finite_check",
        "attitude.candidate.numerical.relative_tolerance",
        "attitude.candidate.numerical.zero_tolerance",
        "attitude.evaluation.normalization",
        "attitude.evaluation.numerical.absolute_tolerance",
        "attitude.evaluation.numerical.condition_limit",
        "attitude.evaluation.numerical.finite_check",
        "attitude.evaluation.numerical.relative_tolerance",
        "attitude.evaluation.numerical.zero_tolerance",
        "combined_wrench_source_id",
        "fixed_step_seconds",
        "inertial_frame_id",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
    };
    if (configuration.schema_id != kRigidStepConfigSchemaIdentity ||
        configuration.schema_version != kRigidStepConfigSchemaVersion ||
        configuration.fields.size() != kFields.size()) {
        return failure();
    }
    for (std::size_t index = 0U; index < kFields.size(); ++index) {
        if (configuration.fields[index].field_id != kFields[index]) {
            return failure();
        }
    }
    const auto number = [&](std::size_t index) {
        return std::get_if<double>(&configuration.fields[index].value);
    };
    const auto enumeration = [&](std::size_t index) {
        return std::get_if<gnc::model_sdk::CanonicalEnumValue>(
            &configuration.fields[index].value);
    };
    const auto text = [&](std::size_t index) {
        return std::get_if<std::string>(
            &configuration.fields[index].value);
    };
    const auto* candidate_normalization = enumeration(0U);
    const auto* candidate_absolute = number(1U);
    const auto* candidate_condition = number(2U);
    const auto* candidate_finite = enumeration(3U);
    const auto* candidate_relative = number(4U);
    const auto* candidate_zero = number(5U);
    const auto* evaluation_normalization = enumeration(6U);
    const auto* evaluation_absolute = number(7U);
    const auto* evaluation_condition = number(8U);
    const auto* evaluation_finite = enumeration(9U);
    const auto* evaluation_relative = number(10U);
    const auto* evaluation_zero = number(11U);
    const auto* combined_source = text(12U);
    const auto* fixed_step = number(13U);
    const auto* inertial_frame = text(14U);
    const auto* absolute = number(15U);
    const auto* condition = number(16U);
    const auto* finite_check = enumeration(17U);
    const auto* relative = number(18U);
    const auto* zero = number(19U);
    const std::array<const double*, 13U> numbers{
        candidate_absolute, candidate_condition, candidate_relative,
        candidate_zero, evaluation_absolute, evaluation_condition,
        evaluation_relative, evaluation_zero, fixed_step, absolute,
        condition, relative, zero};
    if (candidate_normalization == nullptr || candidate_finite == nullptr ||
        evaluation_normalization == nullptr ||
        evaluation_finite == nullptr || combined_source == nullptr ||
        combined_source->empty() || inertial_frame == nullptr ||
        inertial_frame->empty() || finite_check == nullptr ||
        std::any_of(numbers.begin(), numbers.end(),
                    [](const double* value) {
                        return value == nullptr ||
                               !canonical_double(*value);
                    })) {
        return failure();
    }
    const auto candidate_normalization_value =
        parse_normalization(candidate_normalization->token);
    const auto candidate_finite_value =
        parse_finite_check(candidate_finite->token);
    const auto evaluation_normalization_value =
        parse_normalization(evaluation_normalization->token);
    const auto evaluation_finite_value =
        parse_finite_check(evaluation_finite->token);
    const auto finite_check_value =
        parse_finite_check(finite_check->token);
    if (!candidate_normalization_value.has_value() ||
        !candidate_finite_value.has_value() ||
        !evaluation_normalization_value.has_value() ||
        !evaluation_finite_value.has_value() ||
        !finite_check_value.has_value()) {
        return failure();
    }

    ControlledRigidBoundaryEvaluationDefinition definition;
    definition.rigid.inertial_frame.id = *inertial_frame;
    definition.rigid.algorithm.fixed_step_seconds = *fixed_step;
    definition.rigid.algorithm.numerical_policy = {
        *absolute, *relative, *finite_check_value, *zero, *condition};
    definition.rigid.algorithm.candidate_attitude_policy.numerical = {
        *candidate_absolute, *candidate_relative,
        *candidate_finite_value, *candidate_zero, *candidate_condition};
    definition.rigid.algorithm.candidate_attitude_policy.normalization =
        *candidate_normalization_value;
    definition.rigid.algorithm.attitude_evaluation_policy.numerical = {
        *evaluation_absolute, *evaluation_relative,
        *evaluation_finite_value, *evaluation_zero, *evaluation_condition};
    definition.rigid.algorithm.attitude_evaluation_policy.normalization =
        *evaluation_normalization_value;
    definition.wrench_adapter.combined_wrench_source_id =
        *combined_source;
    if (definition.rigid.algorithm.fixed_step_seconds <= 0.0 ||
        !gnc::foundation::valid_numerical_policy(
            definition.rigid.algorithm.numerical_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            definition.rigid.algorithm.candidate_attitude_policy) ||
        !gnc::foundation::valid_quaternion_policy(
            definition.rigid.algorithm.attitude_evaluation_policy)) {
        return failure();
    }
    return NumericalOutcome<
        ControlledRigidBoundaryEvaluationDefinition>::with_value(
            NumericalStatus::Success, std::move(definition),
            mass_commit_evidence(
                kControlledRigidDefinitionBuilderIdentity,
                "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_altitude_pitch_guidance_config(
    const AltitudePitchGuidanceDefinition& definition) {
    const auto& policy = definition.attitude_policy.numerical;
    return {
        std::string(kAltitudePitchGuidanceConfigSchemaIdentity),
        kAltitudePitchGuidanceConfigSchemaVersion,
        {
            {"altitude_error_gain_radians_per_meter",
             definition.altitude_error_gain_radians_per_meter},
            {"attitude.normalization",
             gnc::model_sdk::CanonicalEnumValue{
                 normalization_token(
                     definition.attitude_policy.normalization)}},
            {"attitude.numerical.absolute_tolerance",
             policy.absolute_tolerance},
            {"attitude.numerical.condition_limit",
             policy.condition_limit},
            {"attitude.numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"attitude.numerical.relative_tolerance",
             policy.relative_tolerance},
            {"attitude.numerical.zero_tolerance",
             policy.zero_tolerance},
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision",
             definition.configuration_revision},
            {"inertial_frame_id", definition.inertial_frame.id},
            {"pitch_command_limit_radians",
             definition.pitch_command_limit_radians},
            {"target_altitude_meters",
             definition.target_altitude_meters},
            {"vertical_speed_gain_radian_seconds_per_meter",
             definition.vertical_speed_gain_radian_seconds_per_meter},
        },
    };
}

NumericalOutcome<AltitudePitchGuidanceDefinition>
build_altitude_pitch_guidance_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return NumericalOutcome<AltitudePitchGuidanceDefinition>::failure(
            NumericalStatus::DomainError,
            mass_commit_evidence(
                kAltitudePitchGuidanceDefinitionBuilderIdentity,
                "canonical-config"));
    };
    static constexpr std::array<std::string_view, 13U> kFields{
        "altitude_error_gain_radians_per_meter",
        "attitude.normalization",
        "attitude.numerical.absolute_tolerance",
        "attitude.numerical.condition_limit",
        "attitude.numerical.finite_check",
        "attitude.numerical.relative_tolerance",
        "attitude.numerical.zero_tolerance",
        "clock_domain_id",
        "configuration_revision",
        "inertial_frame_id",
        "pitch_command_limit_radians",
        "target_altitude_meters",
        "vertical_speed_gain_radian_seconds_per_meter",
    };
    if (configuration.schema_id !=
            kAltitudePitchGuidanceConfigSchemaIdentity ||
        configuration.schema_version !=
            kAltitudePitchGuidanceConfigSchemaVersion ||
        configuration.fields.size() != kFields.size()) {
        return failure();
    }
    for (std::size_t index = 0U; index < kFields.size(); ++index) {
        if (configuration.fields[index].field_id != kFields[index]) {
            return failure();
        }
    }

    const auto* altitude_gain =
        std::get_if<double>(&configuration.fields[0U].value);
    const auto* normalization =
        std::get_if<gnc::model_sdk::CanonicalEnumValue>(
            &configuration.fields[1U].value);
    const auto* absolute =
        std::get_if<double>(&configuration.fields[2U].value);
    const auto* condition =
        std::get_if<double>(&configuration.fields[3U].value);
    const auto* finite_token =
        std::get_if<gnc::model_sdk::CanonicalEnumValue>(
            &configuration.fields[4U].value);
    const auto* relative =
        std::get_if<double>(&configuration.fields[5U].value);
    const auto* zero =
        std::get_if<double>(&configuration.fields[6U].value);
    const auto* clock_domain =
        std::get_if<std::string>(&configuration.fields[7U].value);
    const auto* revision =
        std::get_if<std::int64_t>(&configuration.fields[8U].value);
    const auto* inertial_frame =
        std::get_if<std::string>(&configuration.fields[9U].value);
    const auto* command_limit =
        std::get_if<double>(&configuration.fields[10U].value);
    const auto* target =
        std::get_if<double>(&configuration.fields[11U].value);
    const auto* vertical_gain =
        std::get_if<double>(&configuration.fields[12U].value);
    if (altitude_gain == nullptr || normalization == nullptr ||
        absolute == nullptr || condition == nullptr ||
        finite_token == nullptr || relative == nullptr || zero == nullptr ||
        clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 || inertial_frame == nullptr ||
        inertial_frame->empty() || command_limit == nullptr ||
        target == nullptr || vertical_gain == nullptr) {
        return failure();
    }
    const std::array<const double*, 7U> numbers{
        altitude_gain, absolute, condition, relative, zero,
        command_limit, target};
    if (std::any_of(numbers.begin(), numbers.end(),
                    [](const double* value) {
                        return !canonical_double(*value);
                    }) ||
        !canonical_double(*vertical_gain)) {
        return failure();
    }
    const auto finite_check = parse_finite_check(finite_token->token);
    const auto normalization_policy =
        parse_normalization(normalization->token);
    if (!finite_check.has_value() ||
        !normalization_policy.has_value()) {
        return failure();
    }

    AltitudePitchGuidanceDefinition definition;
    definition.model_id =
        std::string(kAltitudePitchGuidanceModelIdentity);
    definition.model_version =
        std::string(kAltitudePitchGuidanceModelVersion);
    definition.inertial_frame.id = *inertial_frame;
    definition.clock_domain.id = *clock_domain;
    definition.configuration_revision = *revision;
    definition.target_altitude_meters = *target;
    definition.altitude_error_gain_radians_per_meter = *altitude_gain;
    definition.vertical_speed_gain_radian_seconds_per_meter =
        *vertical_gain;
    definition.pitch_command_limit_radians = *command_limit;
    definition.attitude_policy.numerical = {
        *absolute, *relative, *finite_check, *zero, *condition};
    definition.attitude_policy.normalization =
        *normalization_policy;
    if (!gnc::foundation::valid_quaternion_policy(
            definition.attitude_policy) ||
        definition.altitude_error_gain_radians_per_meter < 0.0 ||
        definition.vertical_speed_gain_radian_seconds_per_meter < 0.0 ||
        definition.pitch_command_limit_radians <= 0.0) {
        return failure();
    }
    return NumericalOutcome<AltitudePitchGuidanceDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        mass_commit_evidence(
            kAltitudePitchGuidanceDefinitionBuilderIdentity,
            "canonical-config"));
}

gnc::model_sdk::CanonicalConfigBlock
canonical_committed_mission_result_config(
    const CommittedMissionResultDefinition& definition) {
    const auto& policy = definition.numerical_policy;
    gnc::model_sdk::CanonicalConfigBlock configuration{
        std::string(kCommittedMissionResultConfigSchemaIdentity),
        kCommittedMissionResultConfigSchemaVersion,
        {
            {"body_frame_id", definition.body_frame.id},
            {"clock_domain_id", definition.clock_domain.id},
            {"configuration_revision", definition.configuration_revision},
            {"inertial_frame_id", definition.inertial_frame.id},
            {"mass_state_id", definition.mass_state_id},
            {"numerical.absolute_tolerance", policy.absolute_tolerance},
            {"numerical.condition_limit", policy.condition_limit},
            {"numerical.finite_check",
             gnc::model_sdk::CanonicalEnumValue{
                 finite_check_token(policy.finite_check)}},
            {"numerical.relative_tolerance", policy.relative_tolerance},
            {"numerical.zero_tolerance", policy.zero_tolerance},
        },
    };
    for (std::size_t index = 0U; index < definition.predicates.size();
         ++index) {
        const auto prefix = "predicates." + std::to_string(index) + ".";
        const auto& predicate = definition.predicates[index];
        configuration.fields.push_back(
            {prefix + "action",
             gnc::model_sdk::CanonicalEnumValue{
                 mission_action_token(predicate.action)}});
        configuration.fields.push_back(
            {prefix + "metric",
             gnc::model_sdk::CanonicalEnumValue{
                 mission_metric_token(predicate.metric)}});
        configuration.fields.push_back(
            {prefix + "predicate_id", predicate.predicate_id});
        configuration.fields.push_back(
            {prefix + "priority", predicate.priority});
        configuration.fields.push_back(
            {prefix + "reason_code", predicate.reason_code});
        configuration.fields.push_back(
            {prefix + "relation",
             gnc::model_sdk::CanonicalEnumValue{
                 mission_relation_token(predicate.relation)}});
        configuration.fields.push_back(
            {prefix + "threshold", predicate.threshold});
    }
    configuration.fields.push_back({"subject", definition.subject});
    return configuration;
}

NumericalOutcome<CommittedMissionResultDefinition>
build_committed_mission_result_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration) {
    const auto failure = [] {
        return mass_commit_failure<CommittedMissionResultDefinition>(
            kCommittedMissionResultDefinitionBuilderIdentity,
            NumericalStatus::DomainError, "canonical-config");
    };
    static constexpr std::array<std::string_view, 10U> kBaseFields{
        "body_frame_id",
        "clock_domain_id",
        "configuration_revision",
        "inertial_frame_id",
        "mass_state_id",
        "numerical.absolute_tolerance",
        "numerical.condition_limit",
        "numerical.finite_check",
        "numerical.relative_tolerance",
        "numerical.zero_tolerance",
    };
    static constexpr std::array<std::string_view, 7U> kPredicateFields{
        "action", "metric", "predicate_id", "priority", "reason_code",
        "relation", "threshold"};
    constexpr std::size_t kSubjectIndex =
        kBaseFields.size() + 3U * kPredicateFields.size();
    if (configuration.schema_id !=
            kCommittedMissionResultConfigSchemaIdentity ||
        configuration.schema_version !=
            kCommittedMissionResultConfigSchemaVersion ||
        configuration.fields.size() != kSubjectIndex + 1U) {
        return failure();
    }
    for (std::size_t index = 0U; index < kBaseFields.size(); ++index) {
        if (configuration.fields[index].field_id != kBaseFields[index]) {
            return failure();
        }
    }
    for (std::size_t predicate_index = 0U; predicate_index < 3U;
         ++predicate_index) {
        const auto prefix =
            "predicates." + std::to_string(predicate_index) + ".";
        for (std::size_t field_index = 0U;
             field_index < kPredicateFields.size(); ++field_index) {
            const auto index = kBaseFields.size() +
                               predicate_index * kPredicateFields.size() +
                               field_index;
            if (configuration.fields[index].field_id !=
                prefix + std::string(kPredicateFields[field_index])) {
                return failure();
            }
        }
    }
    if (configuration.fields[kSubjectIndex].field_id != "subject") {
        return failure();
    }

    const auto* body_frame =
        std::get_if<std::string>(&configuration.fields[0U].value);
    const auto* clock_domain =
        std::get_if<std::string>(&configuration.fields[1U].value);
    const auto* revision =
        std::get_if<std::int64_t>(&configuration.fields[2U].value);
    const auto* inertial_frame =
        std::get_if<std::string>(&configuration.fields[3U].value);
    const auto* mass_state_id =
        std::get_if<std::string>(&configuration.fields[4U].value);
    const auto policy =
        config_numerical_policy(configuration, 5U, 6U, 7U, 8U, 9U);
    const auto* subject = std::get_if<std::string>(
        &configuration.fields[kSubjectIndex].value);
    if (body_frame == nullptr || body_frame->empty() ||
        clock_domain == nullptr || clock_domain->empty() ||
        revision == nullptr || *revision < 0 || inertial_frame == nullptr ||
        inertial_frame->empty() || mass_state_id == nullptr ||
        mass_state_id->empty() || subject == nullptr || subject->empty() ||
        !policy.has_value()) {
        return failure();
    }

    CommittedMissionResultDefinition definition;
    definition.model_id =
        std::string(kCommittedMissionResultModelIdentity);
    definition.model_version =
        std::string(kCommittedMissionResultModelVersion);
    definition.subject = *subject;
    definition.inertial_frame.id = *inertial_frame;
    definition.body_frame.id = *body_frame;
    definition.clock_domain.id = *clock_domain;
    definition.mass_state_id = *mass_state_id;
    definition.configuration_revision = *revision;
    definition.numerical_policy = *policy;
    for (std::size_t predicate_index = 0U;
         predicate_index < definition.predicates.size(); ++predicate_index) {
        const auto offset = kBaseFields.size() +
                            predicate_index * kPredicateFields.size();
        const auto* action =
            std::get_if<gnc::model_sdk::CanonicalEnumValue>(
                &configuration.fields[offset].value);
        const auto* metric =
            std::get_if<gnc::model_sdk::CanonicalEnumValue>(
                &configuration.fields[offset + 1U].value);
        const auto* predicate_id = std::get_if<std::string>(
            &configuration.fields[offset + 2U].value);
        const auto* priority = std::get_if<std::int64_t>(
            &configuration.fields[offset + 3U].value);
        const auto* reason = std::get_if<std::string>(
            &configuration.fields[offset + 4U].value);
        const auto* relation =
            std::get_if<gnc::model_sdk::CanonicalEnumValue>(
                &configuration.fields[offset + 5U].value);
        const auto* threshold = std::get_if<double>(
            &configuration.fields[offset + 6U].value);
        if (action == nullptr || metric == nullptr ||
            predicate_id == nullptr || predicate_id->empty() ||
            priority == nullptr || *priority < 0 || reason == nullptr ||
            reason->empty() || relation == nullptr || threshold == nullptr ||
            !canonical_double(*threshold)) {
            return failure();
        }
        const auto action_value = parse_mission_action(action->token);
        const auto metric_value = parse_mission_metric(metric->token);
        const auto relation_value =
            parse_mission_relation(relation->token);
        if (!action_value.has_value() || !metric_value.has_value() ||
            !relation_value.has_value()) {
            return failure();
        }
        definition.predicates[predicate_index] = {
            *predicate_id, *metric_value, *relation_value, *threshold,
            *action_value, *reason, *priority};
        for (std::size_t previous = 0U; previous < predicate_index;
             ++previous) {
            if (definition.predicates[previous].predicate_id ==
                definition.predicates[predicate_index].predicate_id) {
                return failure();
            }
        }
    }
    return NumericalOutcome<CommittedMissionResultDefinition>::with_value(
        NumericalStatus::Success, std::move(definition),
        mass_commit_evidence(
            kCommittedMissionResultDefinitionBuilderIdentity,
            "canonical-config"));
}

NumericalOutcome<MassState> MassInitialStateBuilder::build(
    const ScalarBurnMassDefinition& definition,
    const MassInitialStateInput& input,
    const NumericalPolicy& policy) {
    const MassState& state = input.state;
    if (definition.model_id != kScalarBurnMassModelIdentity ||
        definition.model_version != kScalarBurnMassModelVersion ||
        definition.mass_state_id.empty() ||
        !gnc::foundation::valid_numerical_policy(policy)) {
        return mass_commit_failure<MassState>(
            kMassInitialStateBuilderIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    if (state.mass_state_id != definition.mass_state_id ||
        state.context.frame.id.empty() ||
        state.context.clock_domain.id.empty() ||
        state.context.configuration_revision < 0 ||
        state.context.quality != DataQuality::Valid ||
        state.context.sample_time.tick < 0 ||
        !std::isfinite(state.context.sample_time.seconds)) {
        return mass_commit_failure<MassState>(
            kMassInitialStateBuilderIdentity,
            NumericalStatus::DomainError, "initial-context-or-identity");
    }
    if (!std::isfinite(state.mass_kilograms) ||
        !finite(state.body_origin_to_center_of_mass.value) ||
        !finite(state.inertia_about_center_of_mass.value)) {
        return mass_commit_failure<MassState>(
            kMassInitialStateBuilderIdentity,
            NumericalStatus::NonFiniteInput, "initial-state");
    }
    if (state.mass_kilograms <= 0.0) {
        return mass_commit_failure<MassState>(
            kMassInitialStateBuilderIdentity,
            NumericalStatus::DomainError, "initial-mass");
    }
    const auto inertia = gnc::foundation::solve_spd_3x3(
        state.inertia_about_center_of_mass.value, Vec3::Zero(), policy);
    if (!inertia.has_value()) {
        return mass_commit_failure<MassState>(
            kMassInitialStateBuilderIdentity, inertia.status(),
            "initial-inertia", inertia.evidence().flags);
    }
    NumericalEvidence evidence = mass_commit_evidence(
        kMassInitialStateBuilderIdentity, "initial-state",
        inertia.evidence().flags);
    evidence.evaluations = inertia.evidence().evaluations;
    return NumericalOutcome<MassState>::with_value(
        approximate_status(inertia.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        state, evidence);
}

NumericalOutcome<MassState> build_scalar_burn_mass_initial_state(
    const ScalarBurnMassDefinition& definition,
    const MassInitialStateInput& input) {
    return MassInitialStateBuilder::build(
        definition, input, definition.numerical_policy);
}

MassPropertiesInput project_committed_mass_properties(
    const IntervalSampleContext& interval_context,
    const MassState& state) {
    MassPropertiesInput projected;
    projected.context = interval_context;
    projected.mass_state_id = state.mass_state_id;
    projected.mass_kilograms = state.mass_kilograms;
    projected.body_origin_to_center_of_mass =
        state.body_origin_to_center_of_mass;
    projected.inertia_about_center_of_mass =
        state.inertia_about_center_of_mass;
    return projected;
}

MassState clone_mass_state(const MassState& state) {
    return state;
}

bool validate_mass_state_finite(const MassState& state) noexcept {
    return std::isfinite(state.context.sample_time.seconds) &&
           std::isfinite(state.mass_kilograms) &&
           finite(state.body_origin_to_center_of_mass.value) &&
           finite(state.inertia_about_center_of_mass.value);
}

bool validate_mass_state_invariants(const MassState& state) noexcept {
    if (state.context.frame.id.empty() ||
        state.context.clock_domain.id.empty() ||
        state.context.configuration_revision < 0 ||
        state.context.quality != DataQuality::Valid ||
        state.context.sample_time.tick < 0 ||
        state.mass_state_id.empty() || state.mass_kilograms <= 0.0) {
        return false;
    }
    const auto inertia = gnc::foundation::solve_spd_3x3(
        state.inertia_about_center_of_mass.value, Vec3::Zero());
    return inertia.has_value();
}

bool validate_mass_state(const MassState& state) noexcept {
    return validate_mass_state_finite(state) &&
           validate_mass_state_invariants(state);
}

void swap_mass_state(MassState& lhs, MassState& rhs) noexcept {
    static_assert(std::is_nothrow_swappable_v<MassState>,
                  "MassState codec requires noexcept swap");
    using std::swap;
    swap(lhs, rhs);
}

const MassStateCodec& mass_state_codec() noexcept {
    static const MassStateCodec codec{
        &clone_mass_state, &validate_mass_state,
        &validate_mass_state_finite,
        &validate_mass_state_invariants,
        &swap_mass_state, &project_committed_mass_properties};
    return codec;
}

NumericalOutcome<SuppliedPropulsionOutput>
SuppliedPropulsionKernel::evaluate(
    const SuppliedPropulsionDefinition& definition,
    const SuppliedPropulsionInput& input) {
    if (definition.model_id != kSuppliedPropulsionModelIdentity ||
        definition.model_version.empty() || definition.source_id.empty() ||
        definition.body_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.mass_state_id.empty() ||
        !gnc::foundation::valid_numerical_policy(
            definition.numerical_policy)) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    if (!valid_propulsion_interval(input.context, definition)) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::DomainError, "response-context");
    }
    const Vec3& direction = input.thrust_direction.value;
    const Vec3& radius =
        input.center_of_mass_to_application.value;
    const Vec3& intrinsic_moment =
        input.intrinsic_moment_at_application.value;
    if (!std::isfinite(input.thrust_magnitude_newtons) ||
        !std::isfinite(
            input.fuel_consumption_rate_kilograms_per_second) ||
        !finite(direction) || !finite(radius) ||
        !finite(intrinsic_moment)) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::NonFiniteInput, "physical-input");
    }
    if (input.thrust_magnitude_newtons < 0.0 ||
        input.fuel_consumption_rate_kilograms_per_second < 0.0) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::DomainError, "physical-domain");
    }
    const double direction_norm = direction.norm();
    if (!std::isfinite(direction_norm) ||
        !near(direction_norm, 1.0, definition.numerical_policy)) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::DomainError, "thrust-direction-unit");
    }

    const Vec3 force =
        input.thrust_magnitude_newtons * direction;
    const Vec3 lever_arm_moment = radius.cross(force);
    const Vec3 moment_about_center_of_mass =
        intrinsic_moment + lever_arm_moment;
    if (!finite(force) || !finite(lever_arm_moment) ||
        !finite(moment_about_center_of_mass)) {
        return mass_commit_failure<SuppliedPropulsionOutput>(
            kSuppliedPropulsionKernelIdentity,
            NumericalStatus::NonFiniteIntermediate,
            "response-closure-preview");
    }

    SuppliedPropulsionOutput output;
    output.supplied_body_wrench.context = input.context;
    output.supplied_body_wrench.source_id = definition.source_id;
    output.supplied_body_wrench.force.value = force;
    output.supplied_body_wrench.center_of_mass_to_application =
        input.center_of_mass_to_application;
    output.supplied_body_wrench.intrinsic_moment_at_application =
        input.intrinsic_moment_at_application;
    output.lever_arm_moment.value = lever_arm_moment;
    output.moment_about_center_of_mass.value =
        moment_about_center_of_mass;
    output.mass_flow.context = input.context;
    output.mass_flow.mass_state_id = definition.mass_state_id;
    output.mass_flow.fuel_consumption_rate_kilograms_per_second =
        input.fuel_consumption_rate_kilograms_per_second;

    NumericalEvidence evidence = mass_commit_evidence(
        kSuppliedPropulsionKernelIdentity, "supplied-response");
    evidence.evaluations = 1U;
    evidence.last_step = input.context.validity.effective_until.seconds -
                         input.context.validity.effective_from.seconds;
    return NumericalOutcome<SuppliedPropulsionOutput>::with_value(
        NumericalStatus::Success, std::move(output), evidence);
}

NumericalOutcome<SuppliedPropulsionOutput>
FixedSuppliedPropulsionBoundaryKernel::evaluate(
    const FixedSuppliedPropulsionDefinition& definition,
    const IntervalSampleContext& context) {
    SuppliedPropulsionInput input;
    input.context = context;
    input.thrust_magnitude_newtons =
        definition.thrust_magnitude_newtons;
    input.thrust_direction = definition.thrust_direction;
    input.center_of_mass_to_application =
        definition.center_of_mass_to_application;
    input.intrinsic_moment_at_application =
        definition.intrinsic_moment_at_application;
    input.fuel_consumption_rate_kilograms_per_second =
        definition.fuel_consumption_rate_kilograms_per_second;
    auto result = SuppliedPropulsionKernel::evaluate(
        definition.propulsion, input);
    auto evidence = result.evidence();
    evidence.algorithm = kFixedSuppliedPropulsionBoundaryIdentity;
    evidence.detail = "fixed-supplied-boundary";
    if (!result.has_value()) {
        return NumericalOutcome<SuppliedPropulsionOutput>::failure(
            result.status(), evidence);
    }
    return NumericalOutcome<SuppliedPropulsionOutput>::with_value(
        result.status(), std::move(result.value()), evidence);
}

NumericalOutcome<ScalarBurnMassOutput> ScalarBurnMassKernel::evaluate(
    const ScalarBurnMassDefinition& definition,
    const MassState& committed_state,
    const MassFlowIntervalInput& flow,
    const NumericalPolicy& policy) {
    if (definition.model_id != kScalarBurnMassModelIdentity ||
        definition.model_version.empty() || definition.mass_state_id.empty() ||
        !gnc::foundation::valid_numerical_policy(policy)) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::DomainError,
            "definition-or-policy");
    }
    if (committed_state.mass_state_id != definition.mass_state_id ||
        flow.mass_state_id != definition.mass_state_id ||
        committed_state.context.frame.id.empty() ||
        committed_state.context.clock_domain.id.empty() ||
        committed_state.context.configuration_revision < 0 ||
        committed_state.context.frame != flow.context.sample.frame ||
        committed_state.context.clock_domain !=
            flow.context.sample.clock_domain ||
        committed_state.context.configuration_revision !=
            flow.context.sample.configuration_revision ||
        committed_state.context.quality != DataQuality::Valid ||
        flow.context.sample.quality != DataQuality::Valid ||
        !same_instant(committed_state.context.sample_time,
                      flow.context.sample.sample_time, policy) ||
        !same_instant(flow.context.sample.sample_time,
                      flow.context.validity.effective_from, policy)) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::DomainError,
            "state-or-flow-identity");
    }
    const SimulationInstant start = flow.context.validity.effective_from;
    const SimulationInstant end = flow.context.validity.effective_until;
    if (start.tick < 0 || end.tick <= start.tick ||
        !std::isfinite(start.seconds) || !std::isfinite(end.seconds)) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::DomainError,
            "flow-time");
    }
    const double duration_seconds = end.seconds - start.seconds;
    if (!std::isfinite(committed_state.mass_kilograms) ||
        !std::isfinite(
            flow.fuel_consumption_rate_kilograms_per_second) ||
        !finite(committed_state.body_origin_to_center_of_mass.value) ||
        !finite(committed_state.inertia_about_center_of_mass.value)) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::NonFiniteInput,
            "mass-input");
    }
    if (duration_seconds <= 0.0 || committed_state.mass_kilograms <= 0.0 ||
        flow.fuel_consumption_rate_kilograms_per_second < 0.0) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::DomainError,
            "mass-domain");
    }
    const auto inertia_check = gnc::foundation::solve_spd_3x3(
        committed_state.inertia_about_center_of_mass.value,
        Vec3::Zero(), policy);
    if (!inertia_check.has_value()) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, inertia_check.status(),
            "mass-inertia", inertia_check.evidence().flags);
    }
    const double consumed =
        flow.fuel_consumption_rate_kilograms_per_second * duration_seconds;
    const double candidate_mass =
        committed_state.mass_kilograms - consumed;
    if (!std::isfinite(consumed) || !std::isfinite(candidate_mass)) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity,
            NumericalStatus::NonFiniteIntermediate, "mass-evolution");
    }
    if (candidate_mass <= 0.0) {
        return mass_commit_failure<ScalarBurnMassOutput>(
            kScalarBurnMassKernelIdentity, NumericalStatus::DomainError,
            "mass-depletion");
    }

    MassState candidate_state = committed_state;
    candidate_state.context.sample_time = end;
    candidate_state.mass_kilograms = candidate_mass;
    ScalarBurnMassOutput output;
    output.current_committed_mass_kilograms =
        committed_state.mass_kilograms;
    output.integration_mass_kilograms = committed_state.mass_kilograms;
    output.consumed_mass_kilograms = consumed;
    output.candidate.effective_at = end;
    output.candidate.state = std::move(candidate_state);
    NumericalEvidence evidence = mass_commit_evidence(
        kScalarBurnMassKernelIdentity, "mass-candidate",
        inertia_check.evidence().flags);
    evidence.evaluations = 1U + inertia_check.evidence().evaluations;
    evidence.last_step = duration_seconds;
    return NumericalOutcome<ScalarBurnMassOutput>::with_value(
        approximate_status(inertia_check.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        std::move(output), evidence);
}

NumericalOutcome<ScalarBurnMassOutput>
evaluate_scalar_burn_mass_interval(
    const ScalarBurnMassDefinition& definition,
    const MassState& committed_state,
    const MassFlowIntervalInput& flow) {
    return ScalarBurnMassKernel::evaluate(
        definition, committed_state, flow, definition.numerical_policy);
}

NumericalOutcome<AltitudePitchGuidanceOutput>
AltitudePitchGuidanceKernel::evaluate(
    const AltitudePitchGuidanceDefinition& definition,
    const CommittedRigidObservation& observation) {
    const NumericalPolicy& policy = definition.attitude_policy.numerical;
    if (definition.model_id != kAltitudePitchGuidanceModelIdentity ||
        definition.model_version.empty() ||
        definition.inertial_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.configuration_revision < 0 ||
        !gnc::foundation::valid_quaternion_policy(
            definition.attitude_policy) ||
        !std::isfinite(definition.target_altitude_meters) ||
        !std::isfinite(
            definition.altitude_error_gain_radians_per_meter) ||
        !std::isfinite(
            definition.vertical_speed_gain_radian_seconds_per_meter) ||
        !std::isfinite(definition.pitch_command_limit_radians) ||
        definition.altitude_error_gain_radians_per_meter < 0.0 ||
        definition.vertical_speed_gain_radian_seconds_per_meter < 0.0 ||
        definition.pitch_command_limit_radians <= 0.0) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    if (!valid_sample_at(
            observation.context, definition.inertial_frame,
            definition.clock_domain, observation.context.sample_time,
            definition.configuration_revision, policy) ||
        observation.context.sample_time.tick < 0 ||
        !std::isfinite(observation.context.sample_time.seconds)) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity,
            NumericalStatus::DomainError, "committed-observation-context");
    }
    if (!finite(observation.state.position.value) ||
        !finite(observation.state.velocity.value) ||
        !finite(observation.state.angular_rate.value)) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity,
            NumericalStatus::NonFiniteInput, "committed-observation-state");
    }
    const auto attitude = gnc::foundation::prepare_passive_quaternion(
        observation.state.attitude.value, definition.attitude_policy);
    if (!attitude.has_value()) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity, attitude.status(),
            "committed-observation-attitude", attitude.evidence().flags);
    }
    const auto& q = attitude.value();
    if (!near(q.x(), 0.0, policy) || !near(q.z(), 0.0, policy) ||
        q.w() <= 0.0) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity,
            NumericalStatus::DomainError, "pure-pitch-projection",
            attitude.evidence().flags);
    }

    const double pitch = -2.0 * std::atan2(q.y(), q.w());
    const double pitch_rate = observation.state.angular_rate.value(1);
    const double altitude_error = definition.target_altitude_meters -
                                  observation.state.position.value(2);
    const double altitude_feedback =
        definition.altitude_error_gain_radians_per_meter * altitude_error;
    const double vertical_speed_feedback =
        -definition.vertical_speed_gain_radian_seconds_per_meter *
        observation.state.velocity.value(2);
    const double raw_pitch_command =
        altitude_feedback + vertical_speed_feedback;
    const double pitch_command = std::clamp(
        raw_pitch_command, -definition.pitch_command_limit_radians,
        definition.pitch_command_limit_radians);
    if (!std::isfinite(pitch) || !std::isfinite(pitch_rate) ||
        !std::isfinite(altitude_error) ||
        !std::isfinite(altitude_feedback) ||
        !std::isfinite(vertical_speed_feedback) ||
        !std::isfinite(raw_pitch_command) ||
        !std::isfinite(pitch_command)) {
        return mass_commit_failure<AltitudePitchGuidanceOutput>(
            kAltitudePitchGuidanceKernelIdentity,
            NumericalStatus::NonFiniteIntermediate, "guidance-formula",
            attitude.evidence().flags);
    }

    AltitudePitchGuidanceOutput output;
    output.source_observation = observation;
    output.measured_pitch_radians = pitch;
    output.measured_pitch_rate_radians_per_second = pitch_rate;
    output.altitude_error_meters = altitude_error;
    output.altitude_feedback_radians = altitude_feedback;
    output.vertical_speed_feedback_radians = vertical_speed_feedback;
    output.raw_pitch_command_radians = raw_pitch_command;
    output.pitch_command_radians = pitch_command;
    output.saturated = pitch_command != raw_pitch_command;
    NumericalEvidence evidence = mass_commit_evidence(
        kAltitudePitchGuidanceKernelIdentity, "committed-altitude-pitch",
        attitude.evidence().flags);
    evidence.evaluations = attitude.evidence().evaluations + 1U;
    evidence.residual_norm = attitude.evidence().residual_norm;
    return NumericalOutcome<AltitudePitchGuidanceOutput>::with_value(
        approximate_status(attitude.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        std::move(output), evidence);
}

NumericalOutcome<PitchMomentControllerOutput>
PitchMomentControllerKernel::evaluate(
    const PitchMomentControllerDefinition& definition,
    const AltitudePitchGuidanceOutput& guidance) {
    if (definition.model_id != kPitchMomentControllerModelIdentity ||
        definition.model_version.empty() || definition.body_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.configuration_revision < 0 ||
        !gnc::foundation::valid_numerical_policy(
            definition.numerical_policy) ||
        !std::isfinite(
            definition.pitch_error_gain_newton_meters_per_radian) ||
        !std::isfinite(
            definition.pitch_rate_gain_newton_meter_seconds_per_radian) ||
        !std::isfinite(
            definition.moment_command_limit_newton_meters) ||
        definition.pitch_error_gain_newton_meters_per_radian < 0.0 ||
        definition.pitch_rate_gain_newton_meter_seconds_per_radian < 0.0 ||
        definition.moment_command_limit_newton_meters <= 0.0) {
        return mass_commit_failure<PitchMomentControllerOutput>(
            kPitchMomentControllerKernelIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    const auto& source = guidance.source_observation.context;
    if (source.clock_domain != definition.clock_domain ||
        source.configuration_revision !=
            definition.configuration_revision ||
        source.quality != DataQuality::Valid ||
        source.sample_time.tick < 0 ||
        !std::isfinite(source.sample_time.seconds)) {
        return mass_commit_failure<PitchMomentControllerOutput>(
            kPitchMomentControllerKernelIdentity,
            NumericalStatus::DomainError, "guidance-context");
    }
    if (!std::isfinite(guidance.measured_pitch_radians) ||
        !std::isfinite(guidance.measured_pitch_rate_radians_per_second) ||
        !std::isfinite(guidance.pitch_command_radians)) {
        return mass_commit_failure<PitchMomentControllerOutput>(
            kPitchMomentControllerKernelIdentity,
            NumericalStatus::NonFiniteInput, "guidance-value");
    }

    const double pitch_error = guidance.pitch_command_radians -
                               guidance.measured_pitch_radians;
    const double proportional =
        definition.pitch_error_gain_newton_meters_per_radian * pitch_error;
    const double damping =
        -definition.pitch_rate_gain_newton_meter_seconds_per_radian *
        guidance.measured_pitch_rate_radians_per_second;
    const double raw_command = proportional + damping;
    const double command = std::clamp(
        raw_command, -definition.moment_command_limit_newton_meters,
        definition.moment_command_limit_newton_meters);
    if (!std::isfinite(pitch_error) || !std::isfinite(proportional) ||
        !std::isfinite(damping) || !std::isfinite(raw_command) ||
        !std::isfinite(command)) {
        return mass_commit_failure<PitchMomentControllerOutput>(
            kPitchMomentControllerKernelIdentity,
            NumericalStatus::NonFiniteIntermediate, "controller-formula");
    }

    PitchMomentControllerOutput output;
    output.context = source;
    output.context.frame = definition.body_frame;
    output.pitch_error_radians = pitch_error;
    output.proportional_moment_newton_meters = proportional;
    output.rate_damping_moment_newton_meters = damping;
    output.raw_moment_command_newton_meters = raw_command;
    output.moment_command_newton_meters = command;
    output.saturated = command != raw_command;
    NumericalEvidence evidence = mass_commit_evidence(
        kPitchMomentControllerKernelIdentity, "pitch-moment-command");
    evidence.evaluations = 1U;
    return NumericalOutcome<PitchMomentControllerOutput>::with_value(
        NumericalStatus::Success, std::move(output), evidence);
}

NumericalOutcome<IdealBodyMomentActuatorOutput>
IdealBodyMomentActuatorKernel::evaluate(
    const IdealBodyMomentActuatorDefinition& definition,
    const IntervalSampleContext& context,
    const PitchMomentControllerOutput& controller) {
    if (definition.model_id != kIdealBodyMomentActuatorModelIdentity ||
        definition.model_version.empty() || definition.source_id.empty() ||
        definition.body_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.configuration_revision < 0 ||
        !gnc::foundation::valid_numerical_policy(
            definition.numerical_policy) ||
        !std::isfinite(definition.realization_gain) ||
        !near(definition.realization_gain, 1.0,
              definition.numerical_policy)) {
        return mass_commit_failure<IdealBodyMomentActuatorOutput>(
            kIdealBodyMomentActuatorKernelIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    if (!valid_interval_at(
            context, definition.body_frame, definition.clock_domain,
            context.validity.effective_from,
            context.validity.effective_until,
            definition.configuration_revision,
            definition.numerical_policy) ||
        context.validity.effective_from.tick < 0 ||
        context.validity.effective_until.tick <=
            context.validity.effective_from.tick ||
        !std::isfinite(context.validity.effective_from.seconds) ||
        !std::isfinite(context.validity.effective_until.seconds) ||
        context.validity.effective_until.seconds <=
            context.validity.effective_from.seconds ||
        controller.context.frame != definition.body_frame ||
        controller.context.clock_domain != definition.clock_domain ||
        controller.context.configuration_revision !=
            definition.configuration_revision ||
        controller.context.quality != DataQuality::Valid ||
        !same_instant(controller.context.sample_time,
                      context.validity.effective_from,
                      definition.numerical_policy)) {
        return mass_commit_failure<IdealBodyMomentActuatorOutput>(
            kIdealBodyMomentActuatorKernelIdentity,
            NumericalStatus::DomainError, "controller-or-interval-context");
    }
    if (!std::isfinite(controller.moment_command_newton_meters)) {
        return mass_commit_failure<IdealBodyMomentActuatorOutput>(
            kIdealBodyMomentActuatorKernelIdentity,
            NumericalStatus::NonFiniteInput, "moment-command");
    }
    const double realized = definition.realization_gain *
                            controller.moment_command_newton_meters;
    if (!std::isfinite(realized)) {
        return mass_commit_failure<IdealBodyMomentActuatorOutput>(
            kIdealBodyMomentActuatorKernelIdentity,
            NumericalStatus::NonFiniteIntermediate,
            "moment-realization");
    }

    IdealBodyMomentActuatorOutput output;
    output.context = context;
    output.source_id = definition.source_id;
    output.moment_about_center_of_mass.value = Vec3{0.0, realized, 0.0};
    NumericalEvidence evidence = mass_commit_evidence(
        kIdealBodyMomentActuatorKernelIdentity,
        "current-cycle-ideal-moment");
    evidence.evaluations = 1U;
    evidence.last_step = context.validity.effective_until.seconds -
                         context.validity.effective_from.seconds;
    return NumericalOutcome<IdealBodyMomentActuatorOutput>::with_value(
        NumericalStatus::Success, std::move(output), evidence);
}

NumericalOutcome<AppliedBodyWrenchInput>
ControlledBodyWrenchAdapterKernel::evaluate(
    const ControlledBodyWrenchAdapterDefinition& definition,
    const ControlledBodyWrenchAdapterInput& input) {
    if (definition.combined_wrench_source_id.empty() ||
        input.propulsion.source_id.empty() ||
        input.actuator.source_id.empty()) {
        return mass_commit_failure<AppliedBodyWrenchInput>(
            kControlledBodyWrenchAdapterIdentity,
            NumericalStatus::DomainError, "source-identity");
    }
    const auto same_time = [](const SimulationInstant& lhs,
                              const SimulationInstant& rhs) {
        return lhs.tick == rhs.tick && lhs.seconds == rhs.seconds;
    };
    const auto& propulsion_context = input.propulsion.context;
    const auto& actuator_context = input.actuator.context;
    if (propulsion_context.sample.frame != actuator_context.sample.frame ||
        propulsion_context.sample.clock_domain !=
            actuator_context.sample.clock_domain ||
        !same_time(propulsion_context.sample.sample_time,
                   actuator_context.sample.sample_time) ||
        propulsion_context.sample.configuration_revision !=
            actuator_context.sample.configuration_revision ||
        propulsion_context.sample.quality !=
            actuator_context.sample.quality ||
        !same_time(propulsion_context.validity.effective_from,
                   actuator_context.validity.effective_from) ||
        !same_time(propulsion_context.validity.effective_until,
                   actuator_context.validity.effective_until)) {
        return mass_commit_failure<AppliedBodyWrenchInput>(
            kControlledBodyWrenchAdapterIdentity,
            NumericalStatus::DomainError, "interval-context");
    }
    if (!finite(input.body_origin_to_center_of_mass.value) ||
        !finite(input.propulsion.force.value) ||
        !finite(input.propulsion.center_of_mass_to_application.value) ||
        !finite(input.propulsion.intrinsic_moment_at_application.value) ||
        !finite(input.actuator.moment_about_center_of_mass.value)) {
        return mass_commit_failure<AppliedBodyWrenchInput>(
            kControlledBodyWrenchAdapterIdentity,
            NumericalStatus::NonFiniteInput, "adapter-input");
    }

    AppliedBodyWrenchInput output;
    output.context = input.propulsion.context;
    output.source_id = definition.combined_wrench_source_id;
    output.force = input.propulsion.force;
    output.body_origin_to_application.value =
        input.body_origin_to_center_of_mass.value +
        input.propulsion.center_of_mass_to_application.value;
    output.intrinsic_moment_at_application.value =
        input.propulsion.intrinsic_moment_at_application.value +
        input.actuator.moment_about_center_of_mass.value;
    if (!finite(output.body_origin_to_application.value) ||
        !finite(output.intrinsic_moment_at_application.value)) {
        return mass_commit_failure<AppliedBodyWrenchInput>(
            kControlledBodyWrenchAdapterIdentity,
            NumericalStatus::NonFiniteIntermediate,
            "controlled-wrench-adapter");
    }
    NumericalEvidence evidence = mass_commit_evidence(
        kControlledBodyWrenchAdapterIdentity, "controlled-body-wrench");
    evidence.evaluations = 1U;
    return NumericalOutcome<AppliedBodyWrenchInput>::with_value(
        NumericalStatus::Success, std::move(output), evidence);
}

NumericalOutcome<ControlledRigidBoundaryEvaluation>
ControlledRigidBoundaryEvaluationKernel::evaluate(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const ControlledRigidBoundaryInvocationSet& invocations,
    const ControlledRigidBoundaryEvaluationInput& input) {
    const auto result = evaluate_with_invocation_results(
        definition, invocations, input);
    if (!result.has_value()) {
        return NumericalOutcome<ControlledRigidBoundaryEvaluation>::failure(
            result.status(), result.evidence());
    }
    return NumericalOutcome<ControlledRigidBoundaryEvaluation>::with_value(
        result.status(), result.value().evaluation, result.evidence());
}

NumericalOutcome<ControlledRigidBoundaryPreparationEvaluation>
ControlledRigidBoundaryEvaluationKernel::prepare_for_integration(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const ControlledRigidBoundaryInvocationSet& invocations,
    const ControlledRigidBoundaryEvaluationInput& input) {
    if (invocations.environment.prepared_model == nullptr ||
        invocations.environment.callable == nullptr) {
        return mass_commit_failure<
            ControlledRigidBoundaryPreparationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                NumericalStatus::DomainError,
                "environment-invocation-set");
    }

    UniformEnvironmentQueryInput environment_request;
    environment_request.context = {
        input.context.inertial_frame,
        input.context.clock_domain,
        input.context.interval_start,
        input.context.configuration_revision,
        input.context.quality};
    environment_request.position = input.committed_state.position;
    const auto environment = invocations.environment.callable(
        *invocations.environment.prepared_model, environment_request);
    if (!environment.has_value()) {
        return mass_commit_failure<
            ControlledRigidBoundaryPreparationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                environment.status(), "environment-query",
                environment.evidence().flags);
    }

    const auto controlled_wrench =
        ControlledBodyWrenchAdapterKernel::evaluate(
            definition.wrench_adapter,
            ControlledBodyWrenchAdapterInput{
                input.mass_properties.body_origin_to_center_of_mass,
                input.propulsion,
                input.actuator});
    if (!controlled_wrench.has_value()) {
        return mass_commit_failure<
            ControlledRigidBoundaryPreparationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                controlled_wrench.status(),
                controlled_wrench.evidence().detail,
                environment.evidence().flags |
                    controlled_wrench.evidence().flags);
    }

    RigidStepInput rigid_input;
    rigid_input.context = input.context;
    rigid_input.committed_state = input.committed_state;
    rigid_input.environment = environment.value().output;
    rigid_input.mass_properties = input.mass_properties;
    rigid_input.supplied_wrench = controlled_wrench.value();
    const auto prepared = RigidFrozenFormKernel::prepare_closure_request(
        definition.rigid,
        RigidFrozenFormQuerySet{invocations.frozen_form.aerodynamic},
        rigid_input);
    if (!prepared.has_value()) {
        return mass_commit_failure<
            ControlledRigidBoundaryPreparationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                prepared.status(), prepared.evidence().detail,
                environment.evidence().flags |
                    controlled_wrench.evidence().flags |
                    prepared.evidence().flags);
    }

    const NumericalFlags flags = environment.evidence().flags |
                                 controlled_wrench.evidence().flags |
                                 prepared.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kControlledRigidBoundaryEvaluationIdentity,
        "controlled-boundary-preparation", flags);
    evidence.evaluations = environment.evidence().evaluations +
                           controlled_wrench.evidence().evaluations +
                           prepared.evidence().evaluations;
    evidence.last_step = definition.rigid.algorithm.fixed_step_seconds;
    return NumericalOutcome<
        ControlledRigidBoundaryPreparationEvaluation>::with_value(
            approximate_status(environment.status()) ||
                    approximate_status(controlled_wrench.status()) ||
                    approximate_status(prepared.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            ControlledRigidBoundaryPreparationEvaluation{
                ControlledRigidBoundaryPreparationOutput{
                    environment.value().output,
                    prepared.value().invocation_results
                        .aerodynamic_coefficients,
                    prepared.value().evaluation.output.closure_request},
                ControlledRigidBoundaryPreparationTelemetry{
                    controlled_wrench.value(),
                    environment.value().telemetry,
                    prepared.value().evaluation.telemetry}},
            evidence);
}

NumericalOutcome<ControlledRigidBoundaryInvocationEvaluation>
ControlledRigidBoundaryEvaluationKernel::evaluate_with_invocation_results(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const ControlledRigidBoundaryInvocationSet& invocations,
    const ControlledRigidBoundaryEvaluationInput& input) {
    if (invocations.frozen_form.closure.prepared_model == nullptr ||
        invocations.frozen_form.closure.callable == nullptr) {
        return mass_commit_failure<
                ControlledRigidBoundaryInvocationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                NumericalStatus::DomainError,
                "closure-invocation-set");
    }
    const auto prepared = prepare_for_integration(
        definition, invocations, input);
    if (!prepared.has_value()) {
        return mass_commit_failure<
                ControlledRigidBoundaryInvocationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                prepared.status(), prepared.evidence().detail,
                prepared.evidence().flags);
    }
    const auto closure = invocations.frozen_form.closure.callable(
        *invocations.frozen_form.closure.prepared_model,
        prepared.value().output.closure_request);
    if (!closure.has_value()) {
        return mass_commit_failure<
                ControlledRigidBoundaryInvocationEvaluation>(
                kControlledRigidBoundaryEvaluationIdentity,
                closure.status(), closure.evidence().detail,
                prepared.evidence().flags |
                    closure.evidence().flags);
    }
    const NumericalFlags flags = prepared.evidence().flags |
                                 closure.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kControlledRigidBoundaryEvaluationIdentity,
        "controlled-frozen-form", flags);
    evidence.evaluations = prepared.evidence().evaluations +
                           closure.evidence().evaluations;
    evidence.last_step = definition.rigid.algorithm.fixed_step_seconds;
    return NumericalOutcome<
        ControlledRigidBoundaryInvocationEvaluation>::with_value(
            approximate_status(prepared.status()) ||
                    approximate_status(closure.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            ControlledRigidBoundaryInvocationEvaluation{
                ControlledRigidBoundaryEvaluation{
                    closure.value().output,
                    ControlledRigidBoundaryTelemetry{
                        prepared.value().telemetry.controlled_wrench,
                        prepared.value().output.environment_response,
                        RigidFrozenFormTelemetry{
                            prepared.value().telemetry.frozen_form.air_data,
                            prepared.value().telemetry.frozen_form
                                .aerodynamic_lookup,
                            prepared.value().telemetry.frozen_form
                                .aerodynamic_query,
                            closure.value()}}},
                ControlledRigidBoundaryInvocationResults{
                    prepared.value().output.environment_response,
                    prepared.value().output.aerodynamic_coefficients}},
            evidence);
}

NumericalOutcome<ControlledRigidBoundaryEvaluationOutput>
ControlledRigidBoundaryEvaluationKernel::evaluate_resolved_environment(
    const ControlledRigidBoundaryEvaluationDefinition& definition,
    const RigidFrozenFormInvocationSet& invocations,
    const ControlledRigidBoundaryResolvedEnvironmentInput& input) {
    const auto controlled_wrench =
        ControlledBodyWrenchAdapterKernel::evaluate(
            definition.wrench_adapter,
            ControlledBodyWrenchAdapterInput{
                input.mass_properties.body_origin_to_center_of_mass,
                input.propulsion,
                input.actuator});
    if (!controlled_wrench.has_value()) {
        return mass_commit_failure<
            ControlledRigidBoundaryEvaluationOutput>(
                kControlledRigidBoundaryEvaluationIdentity,
                controlled_wrench.status(),
                controlled_wrench.evidence().detail,
                controlled_wrench.evidence().flags);
    }

    RigidStepInput rigid_input;
    rigid_input.context = input.context;
    rigid_input.committed_state = input.committed_state;
    rigid_input.environment = input.environment;
    rigid_input.mass_properties = input.mass_properties;
    rigid_input.supplied_wrench = controlled_wrench.value();
    const auto frozen =
        RigidFrozenFormKernel::evaluate_with_invocation_results(
            definition.rigid, invocations, rigid_input);
    if (!frozen.has_value()) {
        return mass_commit_failure<
            ControlledRigidBoundaryEvaluationOutput>(
                kControlledRigidBoundaryEvaluationIdentity,
                frozen.status(), frozen.evidence().detail,
                controlled_wrench.evidence().flags |
                    frozen.evidence().flags);
    }

    ControlledRigidBoundaryEvaluationOutput output;
    output.controlled_wrench = controlled_wrench.value();
    output.environment_response = input.environment;
    output.frozen_form = frozen.value().evaluation;
    output.frozen_form_invocation_results =
        frozen.value().invocation_results;
    output.frozen_form_status = frozen.status();
    output.frozen_form_evidence = frozen.evidence();
    const NumericalFlags flags = controlled_wrench.evidence().flags |
                                 frozen.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kControlledRigidBoundaryEvaluationIdentity,
        "controlled-frozen-form", flags);
    evidence.evaluations = controlled_wrench.evidence().evaluations +
                           frozen.evidence().evaluations;
    evidence.last_step = definition.rigid.algorithm.fixed_step_seconds;
    return NumericalOutcome<
        ControlledRigidBoundaryEvaluationOutput>::with_value(
            approximate_status(controlled_wrench.status()) ||
                    approximate_status(frozen.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            std::move(output), evidence);
}

namespace {

NumericalOutcome<FrozenRigidMassStepOutput>
evaluate_frozen_rigid_mass_step(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const CommittedRigidMassBoundary& opening_boundary,
    const RigidMassIntervalInput& interval,
    const RigidFrozenFormEvaluation* held_form,
    NumericalStatus held_form_status,
    const NumericalEvidence* held_form_evidence) {
    const auto& rigid_definition = rigid_model.definition();
    const auto& closure = rigid_definition.force_moment_closure;
    const auto& policy = rigid_definition.algorithm.numerical_policy;
    if (!valid_sample_at(
            opening_boundary.rigid_context,
            rigid_definition.inertial_frame,
            closure.clock_domain,
            interval.context.interval_start,
            closure.configuration_revision, policy) ||
        !valid_sample_at(
            opening_boundary.mass_state.context,
            closure.body_frame,
            closure.clock_domain,
            interval.context.interval_start,
            closure.configuration_revision, policy) ||
        !valid_interval_at(
            interval.mass_flow.context, closure.body_frame,
            closure.clock_domain,
            interval.context.interval_start,
            interval.context.interval_end,
            closure.configuration_revision, policy) ||
        opening_boundary.mass_state.mass_state_id !=
            mass_definition.mass_state_id ||
        interval.mass_flow.mass_state_id != mass_definition.mass_state_id) {
        return mass_commit_failure<FrozenRigidMassStepOutput>(
            kFrozenRigidMassStepKernelIdentity,
            NumericalStatus::DomainError, "opening-boundary-or-flow");
    }

    const MassPropertiesInput projected_mass =
        project_committed_mass_properties(
            interval.mass_flow.context, opening_boundary.mass_state);

    RigidStepInput rigid_input;
    rigid_input.context = interval.context;
    rigid_input.committed_state = opening_boundary.rigid_state;
    rigid_input.environment = interval.environment;
    rigid_input.mass_properties = projected_mass;
    rigid_input.supplied_wrench = interval.supplied_wrench;
    const auto rigid = held_form == nullptr
        ? RigidStepKernel::evaluate(rigid_model, rigid_input)
        : RigidStepKernel::evaluate_held_form(
              rigid_model, rigid_input, *held_form,
              held_form_status, *held_form_evidence);
    if (!rigid.has_value()) {
        return mass_commit_failure<FrozenRigidMassStepOutput>(
            kFrozenRigidMassStepKernelIdentity, rigid.status(),
            "rigid-candidate", rigid.evidence().flags);
    }

    const auto mass = ScalarBurnMassKernel::evaluate(
        mass_definition, opening_boundary.mass_state,
        interval.mass_flow, policy);
    if (!mass.has_value()) {
        return mass_commit_failure<FrozenRigidMassStepOutput>(
            kFrozenRigidMassStepKernelIdentity, mass.status(),
            "mass-candidate", rigid.evidence().flags |
                                  mass.evidence().flags);
    }
    if (!same_instant(rigid.value().output.candidate.effective_at,
                      mass.value().candidate.effective_at, policy)) {
        return mass_commit_failure<FrozenRigidMassStepOutput>(
            kFrozenRigidMassStepKernelIdentity,
            NumericalStatus::InternalFailure,
            "candidate-closing-time", rigid.evidence().flags |
                                          mass.evidence().flags);
    }

    FrozenRigidMassStepOutput output;
    output.opening_boundary = opening_boundary;
    output.projected_committed_mass = projected_mass;
    output.rigid_step = rigid.value();
    output.mass_evolution = mass.value();
    output.candidate.effective_at =
        rigid.value().output.candidate.effective_at;
    output.candidate.rigid = rigid.value().output.candidate;
    output.candidate.mass = mass.value().candidate;
    const NumericalFlags flags =
        rigid.evidence().flags | mass.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kFrozenRigidMassStepKernelIdentity, "atomic-candidate", flags);
    evidence.evaluations = rigid.evidence().evaluations +
                           mass.evidence().evaluations;
    evidence.last_step = rigid_definition.algorithm.fixed_step_seconds;
    return NumericalOutcome<FrozenRigidMassStepOutput>::with_value(
        approximate_status(rigid.status()) || approximate_status(mass.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        std::move(output), evidence);
}

} // namespace

NumericalOutcome<FrozenRigidMassStepOutput>
FrozenRigidMassStepKernel::evaluate(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const CommittedRigidMassBoundary& opening_boundary,
    const RigidMassIntervalInput& interval) {
    return evaluate_frozen_rigid_mass_step(
        rigid_model, mass_definition, opening_boundary, interval,
        nullptr, NumericalStatus::InternalFailure, nullptr);
}

NumericalOutcome<FrozenRigidMassStepOutput>
FrozenRigidMassStepKernel::evaluate_held_form(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const CommittedRigidMassBoundary& opening_boundary,
    const RigidMassIntervalInput& interval,
    const RigidFrozenFormEvaluation& frozen_form,
    NumericalStatus frozen_form_status,
    const NumericalEvidence& frozen_form_evidence) {
    return evaluate_frozen_rigid_mass_step(
        rigid_model, mass_definition, opening_boundary, interval,
        &frozen_form, frozen_form_status, &frozen_form_evidence);
}

NumericalOutcome<PropelledFrozenRigidMassStepOutput>
PropelledFrozenRigidMassStepKernel::evaluate(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const SuppliedPropulsionDefinition& propulsion_definition,
    const CommittedRigidMassBoundary& opening_boundary,
    const PropelledRigidMassIntervalInput& interval) {
    const auto propulsion = SuppliedPropulsionKernel::evaluate(
        propulsion_definition, interval.propulsion);
    if (!propulsion.has_value()) {
        return mass_commit_failure<
            PropelledFrozenRigidMassStepOutput>(
                kPropelledFrozenRigidMassStepKernelIdentity,
                propulsion.status(), "propulsion-response",
                propulsion.evidence().flags);
    }

    RigidMassIntervalInput atomic_input;
    atomic_input.context = interval.context;
    atomic_input.environment = interval.environment;
    const auto& response = propulsion.value();
    atomic_input.supplied_wrench.context =
        response.supplied_body_wrench.context;
    atomic_input.supplied_wrench.source_id =
        response.supplied_body_wrench.source_id;
    atomic_input.supplied_wrench.force =
        response.supplied_body_wrench.force;
    atomic_input.supplied_wrench.body_origin_to_application.value =
        opening_boundary.mass_state.body_origin_to_center_of_mass.value +
        response.supplied_body_wrench
            .center_of_mass_to_application.value;
    atomic_input.supplied_wrench.intrinsic_moment_at_application =
        response.supplied_body_wrench
            .intrinsic_moment_at_application;
    atomic_input.mass_flow = response.mass_flow;
    if (!finite(atomic_input.supplied_wrench
                    .body_origin_to_application.value)) {
        return mass_commit_failure<
            PropelledFrozenRigidMassStepOutput>(
                kPropelledFrozenRigidMassStepKernelIdentity,
                NumericalStatus::NonFiniteIntermediate,
                "application-point-adapter",
                propulsion.evidence().flags);
    }

    const auto boundary = FrozenRigidMassStepKernel::evaluate(
        rigid_model, mass_definition, opening_boundary, atomic_input);
    if (!boundary.has_value()) {
        return mass_commit_failure<
            PropelledFrozenRigidMassStepOutput>(
                kPropelledFrozenRigidMassStepKernelIdentity,
                boundary.status(), "atomic-boundary",
                propulsion.evidence().flags |
                    boundary.evidence().flags);
    }

    PropelledFrozenRigidMassStepOutput output;
    output.propulsion = response;
    output.atomic_boundary = boundary.value();
    const NumericalFlags flags =
        propulsion.evidence().flags | boundary.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kPropelledFrozenRigidMassStepKernelIdentity,
        "propulsion-to-atomic-boundary", flags);
    evidence.evaluations = propulsion.evidence().evaluations +
                           boundary.evidence().evaluations;
    evidence.last_step =
        rigid_model.definition().algorithm.fixed_step_seconds;
    return NumericalOutcome<
        PropelledFrozenRigidMassStepOutput>::with_value(
            approximate_status(propulsion.status()) ||
                    approximate_status(boundary.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            std::move(output), evidence);
}

NumericalOutcome<ControlledPropelledRigidMassStepOutput>
ControlledPropelledRigidMassStepKernel::evaluate(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const SuppliedPropulsionDefinition& propulsion_definition,
    const ControlledPropelledRigidMassStepDefinition& definition,
    const CommittedRigidMassBoundary& opening_boundary,
    const PropelledRigidMassIntervalInput& interval) {
    const auto& rigid_definition = rigid_model.definition();
    const auto& closure = rigid_definition.force_moment_closure;
    if (definition.model_id !=
            kControlledPropelledRigidMassStepModelIdentity ||
        definition.model_version.empty() ||
        definition.combined_wrench_source_id.empty() ||
        definition.guidance.inertial_frame !=
            rigid_definition.inertial_frame ||
        definition.controller.body_frame != closure.body_frame ||
        definition.actuator.body_frame != closure.body_frame ||
        definition.guidance.clock_domain !=
            closure.clock_domain ||
        definition.controller.clock_domain !=
            closure.clock_domain ||
        definition.actuator.clock_domain !=
            closure.clock_domain ||
        definition.guidance.configuration_revision !=
            closure.configuration_revision ||
        definition.controller.configuration_revision !=
            closure.configuration_revision ||
        definition.actuator.configuration_revision !=
            closure.configuration_revision ||
        propulsion_definition.body_frame != closure.body_frame ||
        propulsion_definition.clock_domain !=
            closure.clock_domain) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                NumericalStatus::DomainError,
                "definition-identity-closure");
    }

    CommittedRigidObservation observation =
        project_committed_rigid_observation(
            opening_boundary.rigid_context,
            opening_boundary.rigid_state);
    const auto guidance = AltitudePitchGuidanceKernel::evaluate(
        definition.guidance, observation);
    if (!guidance.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                guidance.status(), "guidance",
                guidance.evidence().flags);
    }
    const auto controller = PitchMomentControllerKernel::evaluate(
        definition.controller, guidance.value());
    if (!controller.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                controller.status(), "controller",
                guidance.evidence().flags |
                    controller.evidence().flags);
    }
    const auto actuator = IdealBodyMomentActuatorKernel::evaluate(
        definition.actuator, interval.propulsion.context,
        controller.value());
    if (!actuator.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                actuator.status(), "actuator",
                guidance.evidence().flags |
                    controller.evidence().flags |
                    actuator.evidence().flags);
    }
    const auto propulsion = SuppliedPropulsionKernel::evaluate(
        propulsion_definition, interval.propulsion);
    if (!propulsion.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                propulsion.status(), "propulsion-response",
                guidance.evidence().flags |
                    controller.evidence().flags |
                    actuator.evidence().flags |
                    propulsion.evidence().flags);
    }

    RigidMassIntervalInput atomic_input;
    atomic_input.context = interval.context;
    atomic_input.environment = interval.environment;
    const auto& response = propulsion.value();
    const MassPropertiesInput projected_mass =
        project_committed_mass_properties(
            response.mass_flow.context,
            opening_boundary.mass_state);
    const auto controlled_boundary =
        ControlledRigidBoundaryEvaluationKernel::evaluate_resolved_environment(
            ControlledRigidBoundaryEvaluationDefinition{
                {rigid_model.definition().inertial_frame,
                 rigid_model.definition().algorithm},
                {definition.combined_wrench_source_id}},
            RigidFrozenFormInvocationSet{
                BoundAerodynamicTableQuery{
                    0U, 0U, 0U,
                    &rigid_model.aerodynamic_table_model(),
                    &AerodynamicTableQueryKernel::evaluate},
                BoundForceMomentClosure{
                    0U, 0U, 0U,
                    &rigid_model.force_moment_closure_model(),
                    &ForceMomentClosureKernel::evaluate}},
            ControlledRigidBoundaryResolvedEnvironmentInput{
                interval.context,
                opening_boundary.rigid_state,
                interval.environment,
                projected_mass,
                response.supplied_body_wrench,
                actuator.value()});
    if (!controlled_boundary.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                controlled_boundary.status(),
                controlled_boundary.evidence().detail,
                guidance.evidence().flags |
                    controller.evidence().flags |
                    actuator.evidence().flags |
                    propulsion.evidence().flags |
                    controlled_boundary.evidence().flags);
    }
    atomic_input.supplied_wrench =
        controlled_boundary.value().controlled_wrench;
    atomic_input.mass_flow = response.mass_flow;
    const auto boundary = FrozenRigidMassStepKernel::evaluate_held_form(
        rigid_model, mass_definition, opening_boundary, atomic_input,
        controlled_boundary.value().frozen_form,
        controlled_boundary.value().frozen_form_status,
        controlled_boundary.value().frozen_form_evidence);
    if (!boundary.has_value()) {
        return mass_commit_failure<
            ControlledPropelledRigidMassStepOutput>(
                kControlledPropelledRigidMassStepKernelIdentity,
                boundary.status(), "atomic-boundary",
                guidance.evidence().flags |
                    controller.evidence().flags |
                    actuator.evidence().flags |
                    propulsion.evidence().flags |
                    boundary.evidence().flags);
    }

    ControlledPropelledRigidMassStepOutput output;
    output.observation = std::move(observation);
    output.guidance = guidance.value();
    output.controller = controller.value();
    output.actuator = actuator.value();
    output.propulsion = propulsion.value();
    output.atomic_boundary = boundary.value();
    const NumericalFlags flags =
        guidance.evidence().flags | controller.evidence().flags |
        actuator.evidence().flags | propulsion.evidence().flags |
        boundary.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kControlledPropelledRigidMassStepKernelIdentity,
        "committed-control-to-atomic-boundary", flags);
    evidence.evaluations = guidance.evidence().evaluations +
                           controller.evidence().evaluations +
                           actuator.evidence().evaluations +
                           propulsion.evidence().evaluations +
                           boundary.evidence().evaluations;
    evidence.last_step =
        rigid_definition.algorithm.fixed_step_seconds;
    return NumericalOutcome<
        ControlledPropelledRigidMassStepOutput>::with_value(
            approximate_status(guidance.status()) ||
                    approximate_status(controller.status()) ||
                    approximate_status(actuator.status()) ||
                    approximate_status(propulsion.status()) ||
                    approximate_status(boundary.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            std::move(output), evidence);
}

NumericalOutcome<TwoIntervalControlledPropelledCommitOutput>
TwoIntervalControlledPropelledCommitKernel::evaluate(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const SuppliedPropulsionDefinition& propulsion_definition,
    const ControlledPropelledRigidMassStepDefinition& definition,
    const TwoIntervalControlledPropelledCommitInput& input) {
    const auto first = ControlledPropelledRigidMassStepKernel::evaluate(
        rigid_model, mass_definition, propulsion_definition, definition,
        input.opening_boundary, input.intervals[0]);
    if (!first.has_value()) {
        return mass_commit_failure<
            TwoIntervalControlledPropelledCommitOutput>(
                kTwoIntervalControlledPropelledCommitKernelIdentity,
                first.status(), "interval-0", first.evidence().flags);
    }
    CommittedRigidMassBoundary first_commit = promote_candidate(
        input.intervals[0].context,
        first.value().atomic_boundary.candidate);

    const auto second = ControlledPropelledRigidMassStepKernel::evaluate(
        rigid_model, mass_definition, propulsion_definition, definition,
        first_commit, input.intervals[1]);
    if (!second.has_value()) {
        return mass_commit_failure<
            TwoIntervalControlledPropelledCommitOutput>(
                kTwoIntervalControlledPropelledCommitKernelIdentity,
                second.status(), "interval-1",
                first.evidence().flags | second.evidence().flags);
    }
    CommittedRigidMassBoundary second_commit = promote_candidate(
        input.intervals[1].context,
        second.value().atomic_boundary.candidate);

    TwoIntervalControlledPropelledCommitOutput output;
    output.intervals[0].staged = first.value();
    output.intervals[0].closing_commit = first_commit;
    output.intervals[1].staged = second.value();
    output.intervals[1].closing_commit = second_commit;
    output.terminal_boundary = second_commit;
    const NumericalFlags flags =
        first.evidence().flags | second.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kTwoIntervalControlledPropelledCommitKernelIdentity,
        "two-interval-committed-control-feedback", flags);
    evidence.evaluations = first.evidence().evaluations +
                           second.evidence().evaluations;
    evidence.last_step =
        rigid_model.definition().algorithm.fixed_step_seconds;
    return NumericalOutcome<
        TwoIntervalControlledPropelledCommitOutput>::with_value(
            approximate_status(first.status()) ||
                    approximate_status(second.status())
                ? NumericalStatus::Approximate
                : NumericalStatus::Success,
            std::move(output), evidence);
}

NumericalOutcome<CommittedMissionResultOutput>
CommittedMissionResultKernel::evaluate(
    const CommittedMissionResultDefinition& definition,
    const CommittedMissionResultInput& input) {
    const auto valid_metric = [](MissionMetric metric) {
        switch (metric) {
        case MissionMetric::DurationSeconds:
        case MissionMetric::DownrangeMeters:
        case MissionMetric::RemainingMassKilograms:
            return true;
        }
        return false;
    };
    const auto valid_relation = [](MissionRelation relation) {
        switch (relation) {
        case MissionRelation::LessThanOrEqual:
        case MissionRelation::GreaterThanOrEqual:
            return true;
        }
        return false;
    };
    const auto valid_action = [](MissionAction action) {
        switch (action) {
        case MissionAction::Complete:
        case MissionAction::Abort:
            return true;
        }
        return false;
    };
    if (definition.model_id != kCommittedMissionResultModelIdentity ||
        definition.model_version.empty() || definition.subject.empty() ||
        definition.inertial_frame.id.empty() ||
        definition.body_frame.id.empty() ||
        definition.clock_domain.id.empty() ||
        definition.mass_state_id.empty() ||
        definition.configuration_revision < 0 ||
        !gnc::foundation::valid_numerical_policy(
            definition.numerical_policy)) {
        return mass_commit_failure<CommittedMissionResultOutput>(
            kCommittedMissionResultKernelIdentity,
            NumericalStatus::DomainError, "definition-or-policy");
    }
    for (std::size_t index = 0U;
         index < definition.predicates.size(); ++index) {
        const auto& predicate = definition.predicates[index];
        if (predicate.predicate_id.empty() ||
            predicate.reason_code.empty() ||
            !valid_metric(predicate.metric) ||
            !valid_relation(predicate.relation) ||
            !valid_action(predicate.action) ||
            !std::isfinite(predicate.threshold) ||
            predicate.priority < 0) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                NumericalStatus::DomainError,
                "termination-predicate");
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (predicate.predicate_id ==
                definition.predicates[previous].predicate_id) {
                return mass_commit_failure<
                    CommittedMissionResultOutput>(
                        kCommittedMissionResultKernelIdentity,
                        NumericalStatus::DomainError,
                        "duplicate-predicate-id");
            }
        }
    }

    const NumericalPolicy& policy = definition.numerical_policy;
    NumericalFlags validation_flags = 0U;
    std::uint64_t validation_evaluations = 0U;
    for (std::size_t index = 0U;
         index < input.committed_samples.size(); ++index) {
        const auto& sample = input.committed_samples[index];
        const auto& rigid_context = sample.rigid_context;
        const auto& mass = sample.mass_state;
        if (!valid_sample_at(
                rigid_context, definition.inertial_frame,
                definition.clock_domain, rigid_context.sample_time,
                definition.configuration_revision, policy) ||
            !valid_sample_at(
                mass.context, definition.body_frame,
                definition.clock_domain, rigid_context.sample_time,
                definition.configuration_revision, policy) ||
            mass.mass_state_id != definition.mass_state_id ||
            rigid_context.sample_time.tick < 0 ||
            !std::isfinite(rigid_context.sample_time.seconds)) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                NumericalStatus::DomainError,
                "committed-sample-context", validation_flags);
        }
        if (!finite(sample.rigid_state.position.value) ||
            !finite(sample.rigid_state.velocity.value) ||
            !finite(sample.rigid_state.attitude.value) ||
            !finite(sample.rigid_state.angular_rate.value) ||
            !std::isfinite(mass.mass_kilograms) ||
            !finite(mass.body_origin_to_center_of_mass.value) ||
            !finite(mass.inertia_about_center_of_mass.value)) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                NumericalStatus::NonFiniteInput,
                "committed-sample-state", validation_flags);
        }
        const double attitude_norm =
            sample.rigid_state.attitude.value.norm();
        if (mass.mass_kilograms <= 0.0 ||
            !std::isfinite(attitude_norm) ||
            !near(attitude_norm, 1.0, policy)) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                NumericalStatus::DomainError,
                "committed-sample-domain", validation_flags);
        }
        const auto inertia = gnc::foundation::solve_spd_3x3(
            mass.inertia_about_center_of_mass.value,
            Vec3::Zero(), policy);
        validation_flags |= inertia.evidence().flags;
        validation_evaluations += inertia.evidence().evaluations;
        if (!inertia.has_value()) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                inertia.status(), "committed-sample-inertia",
                validation_flags);
        }
        if (index > 0U) {
            const auto& previous = input.committed_samples[index - 1U];
            if (rigid_context.sample_time.tick !=
                    previous.rigid_context.sample_time.tick + 1 ||
                rigid_context.sample_time.seconds <=
                    previous.rigid_context.sample_time.seconds ||
                (mass.mass_kilograms >
                     previous.mass_state.mass_kilograms &&
                 !near(mass.mass_kilograms,
                       previous.mass_state.mass_kilograms, policy))) {
                return mass_commit_failure<CommittedMissionResultOutput>(
                    kCommittedMissionResultKernelIdentity,
                    NumericalStatus::DomainError,
                    "committed-sample-sequence", validation_flags);
            }
        }
    }

    const auto& initial = input.committed_samples[0];
    const double initial_time = initial.rigid_context.sample_time.seconds;
    const double initial_downrange =
        initial.rigid_state.position.value(0);
    const double initial_altitude =
        initial.rigid_state.position.value(2);
    const double initial_mass = initial.mass_state.mass_kilograms;
    MissionMetricSummary summary;
    bool summary_initialized = false;
    for (std::size_t sample_index = 0U;
         sample_index < input.committed_samples.size(); ++sample_index) {
        const auto& sample = input.committed_samples[sample_index];
        MissionMetrics metrics;
        metrics.duration_seconds =
            sample.rigid_context.sample_time.seconds - initial_time;
        metrics.downrange_meters =
            sample.rigid_state.position.value(0) - initial_downrange;
        metrics.vertical_displacement_meters =
            sample.rigid_state.position.value(2) - initial_altitude;
        metrics.remaining_mass_kilograms =
            sample.mass_state.mass_kilograms;
        metrics.consumed_mass_kilograms =
            initial_mass - sample.mass_state.mass_kilograms;
        metrics.speed_meters_per_second =
            sample.rigid_state.velocity.value.norm();
        if (!std::isfinite(metrics.duration_seconds) ||
            !std::isfinite(metrics.downrange_meters) ||
            !std::isfinite(metrics.vertical_displacement_meters) ||
            !std::isfinite(metrics.remaining_mass_kilograms) ||
            !std::isfinite(metrics.consumed_mass_kilograms) ||
            !std::isfinite(metrics.speed_meters_per_second) ||
            metrics.duration_seconds < 0.0 ||
            (metrics.consumed_mass_kilograms < 0.0 &&
             !near(metrics.consumed_mass_kilograms, 0.0, policy))) {
            return mass_commit_failure<CommittedMissionResultOutput>(
                kCommittedMissionResultKernelIdentity,
                NumericalStatus::NonFiniteIntermediate,
                "mission-metrics", validation_flags);
        }

        const std::int64_t tick =
            sample.rigid_context.sample_time.tick;
        summary.evaluated_sample_count = sample_index + 1U;
        summary.terminal = metrics;
        if (!summary_initialized ||
            metrics.speed_meters_per_second >
                summary.peak_speed_meters_per_second) {
            summary.peak_speed_meters_per_second =
                metrics.speed_meters_per_second;
            summary.peak_speed_tick = tick;
        }
        if (!summary_initialized ||
            metrics.downrange_meters >
                summary.maximum_downrange_meters) {
            summary.maximum_downrange_meters =
                metrics.downrange_meters;
            summary.maximum_downrange_tick = tick;
        }
        if (!summary_initialized ||
            metrics.remaining_mass_kilograms <
                summary.minimum_remaining_mass_kilograms) {
            summary.minimum_remaining_mass_kilograms =
                metrics.remaining_mass_kilograms;
            summary.minimum_remaining_mass_tick = tick;
        }
        summary_initialized = true;

        std::array<MissionPredicateEvaluation, 3U> evaluations;
        const MissionTerminationPredicate* selected = nullptr;
        for (std::size_t predicate_index = 0U;
             predicate_index < definition.predicates.size();
             ++predicate_index) {
            const auto& predicate =
                definition.predicates[predicate_index];
            double observed = 0.0;
            switch (predicate.metric) {
            case MissionMetric::DurationSeconds:
                observed = metrics.duration_seconds;
                break;
            case MissionMetric::DownrangeMeters:
                observed = metrics.downrange_meters;
                break;
            case MissionMetric::RemainingMassKilograms:
                observed = metrics.remaining_mass_kilograms;
                break;
            }
            const bool met =
                predicate.relation == MissionRelation::LessThanOrEqual
                    ? observed <= predicate.threshold
                    : observed >= predicate.threshold;
            evaluations[predicate_index] = {
                predicate.predicate_id,
                observed,
                met,
                predicate.action,
                predicate.reason_code,
                predicate.priority,
            };
            if (met &&
                (selected == nullptr ||
                 predicate.priority > selected->priority ||
                 (predicate.priority == selected->priority &&
                  predicate.predicate_id < selected->predicate_id))) {
                selected = &predicate;
            }
        }
        if (selected != nullptr) {
            CommittedMissionResultOutput output;
            output.status = selected->action == MissionAction::Complete
                                ? MissionResultStatus::Completed
                                : MissionResultStatus::Aborted;
            output.initial_tick =
                initial.rigid_context.sample_time.tick;
            output.final_tick = tick;
            output.final_time_seconds =
                sample.rigid_context.sample_time.seconds;
            output.termination = {
                selected->action,
                selected->reason_code,
                output.final_time_seconds,
                selected->priority,
            };
            output.metrics = summary;
            output.terminal_predicates = std::move(evaluations);
            output.terminal_boundary = sample;
            NumericalEvidence evidence = mass_commit_evidence(
                kCommittedMissionResultKernelIdentity,
                "first-terminal-committed-sample", validation_flags);
            evidence.evaluations = validation_evaluations +
                                   summary.evaluated_sample_count *
                                       definition.predicates.size();
            evidence.last_step = metrics.duration_seconds;
            return NumericalOutcome<
                CommittedMissionResultOutput>::with_value(
                    validation_flags == 0U
                        ? NumericalStatus::Success
                        : NumericalStatus::Approximate,
                    std::move(output), evidence);
        }
    }
    return mass_commit_failure<CommittedMissionResultOutput>(
        kCommittedMissionResultKernelIdentity,
        NumericalStatus::DomainError, "no-terminal-committed-sample",
        validation_flags);
}

NumericalOutcome<CommittedMissionResultOutput>
CommittedMissionHistoryEvaluationKernel::evaluate(
    const CommittedMissionResultDefinition& definition,
    const CommittedMissionStateHistoryInput& input) {
    CommittedMissionResultInput assembled;
    for (std::size_t index = 0U;
         index < assembled.committed_samples.size(); ++index) {
        assembled.committed_samples[index] = {
            SampleContext{
                definition.inertial_frame,
                definition.clock_domain,
                input.mass_states[index].context.sample_time,
                definition.configuration_revision,
                input.mass_states[index].context.quality},
            input.rigid_states[index], input.mass_states[index]};
    }
    return CommittedMissionResultKernel::evaluate(definition, assembled);
}

NumericalOutcome<TwoIntervalMassCommitOutput>
TwoIntervalMassCommitKernel::evaluate(
    const PreparedRigidStepModel& rigid_model,
    const ScalarBurnMassDefinition& mass_definition,
    const TwoIntervalMassCommitInput& input) {
    const auto first = FrozenRigidMassStepKernel::evaluate(
        rigid_model, mass_definition, input.opening_boundary,
        input.intervals[0]);
    if (!first.has_value()) {
        return mass_commit_failure<TwoIntervalMassCommitOutput>(
            kTwoIntervalMassCommitKernelIdentity, first.status(),
            "interval-0", first.evidence().flags);
    }
    CommittedRigidMassBoundary first_commit = promote_candidate(
        input.intervals[0].context, first.value().candidate);

    const auto second = FrozenRigidMassStepKernel::evaluate(
        rigid_model, mass_definition, first_commit, input.intervals[1]);
    if (!second.has_value()) {
        return mass_commit_failure<TwoIntervalMassCommitOutput>(
            kTwoIntervalMassCommitKernelIdentity, second.status(),
            "interval-1", first.evidence().flags |
                              second.evidence().flags);
    }
    CommittedRigidMassBoundary second_commit = promote_candidate(
        input.intervals[1].context, second.value().candidate);

    TwoIntervalMassCommitOutput output;
    output.intervals[0].staged = first.value();
    output.intervals[0].closing_commit = first_commit;
    output.intervals[1].staged = second.value();
    output.intervals[1].closing_commit = second_commit;
    output.terminal_boundary = second_commit;
    const NumericalFlags flags =
        first.evidence().flags | second.evidence().flags;
    NumericalEvidence evidence = mass_commit_evidence(
        kTwoIntervalMassCommitKernelIdentity,
        "two-interval-committed-boundaries", flags);
    evidence.evaluations = first.evidence().evaluations +
                           second.evidence().evaluations;
    evidence.last_step =
        rigid_model.definition().algorithm.fixed_step_seconds;
    return NumericalOutcome<TwoIntervalMassCommitOutput>::with_value(
        approximate_status(first.status()) ||
                approximate_status(second.status())
            ? NumericalStatus::Approximate
            : NumericalStatus::Success,
        std::move(output), evidence);
}

} // namespace gnc::packages::yyz
