#include "gnc/compiler/complete_execution_plan.hpp"
#include "gnc/kernel/session.hpp"
#include "support/session_qualification_access.hpp"

#include <yyz/scheduled_stuck_actuator.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace sdk = gnc::model_sdk;
namespace yyz = gnc::packages::yyz;

constexpr std::string_view kEntityId =
    "vehicle.fixture.yyz.stuck-actuator@1";
constexpr std::string_view kClockId =
    "clock.fixture.yyz.stuck-actuator.10hz@1";
constexpr std::string_view kDemandOccurrence = "stuck-fixture.demand";
constexpr std::string_view kActuatorOccurrence = "stuck-fixture.actuator";
constexpr std::string_view kLoadOccurrence = "stuck-fixture.load";
constexpr std::string_view kRigidOccurrence = "stuck-fixture.rigid";
constexpr std::string_view kContactOccurrence = "stuck-fixture.contact";
constexpr std::string_view kRouteId = "scheduled-stuck-actuator";
constexpr std::string_view kTransactionId =
    "transaction.fixture.yyz.stuck-actuator";
constexpr std::int64_t kFaultTick = 5;
constexpr std::int64_t kLaterDemandTick = 10;
constexpr std::int64_t kTerminalTick = 20;
constexpr double kTolerance = 1.0e-12;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Outcome>
[[nodiscard]] std::string diagnostic_text(const Outcome& outcome) {
    std::string result;
    for (const auto& diagnostic : outcome.diagnostics) {
        if (!result.empty()) result += "; ";
        result += std::string(compiler::to_string(diagnostic.code));
        result += " ";
        result += diagnostic.subject;
        result += ": ";
        result += diagnostic.detail;
    }
    return result;
}

[[nodiscard]] bool near(double lhs, double rhs,
                        double tolerance = kTolerance) noexcept {
    return std::isfinite(lhs) && std::isfinite(rhs) &&
           std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] compiler::SourceRef source_ref(std::string path) {
    return {"fixture://yyz/stuck-actuator", std::move(path)};
}

[[nodiscard]] sdk::CanonicalConfigBlock config(
    std::string schema,
    std::initializer_list<sdk::CanonicalConfigField> fields) {
    return {std::move(schema), 1U,
            std::vector<sdk::CanonicalConfigField>(fields)};
}

void append_field_sources(
    compiler::CompleteSourceOccurrence& occurrence) {
    for (const auto& field : occurrence.configuration.fields) {
        occurrence.configuration_field_sources.push_back(
            {field.field_id,
             source_ref("/occurrences/" + occurrence.occurrence_id +
                        "/configuration/" + field.field_id)});
    }
}

[[nodiscard]] compiler::CompleteSourceOccurrence occurrence(
    std::string occurrence_id, std::string model_id,
    sdk::CanonicalConfigBlock configuration,
    const compiler::ScopeKey& scope,
    sdk::ModelPlacement placement) {
    compiler::CompleteSourceOccurrence result;
    result.occurrence_id = std::move(occurrence_id);
    result.model_id = std::move(model_id);
    result.model_version = std::string(yyz::kStuckQualificationModelVersion);
    result.source = source_ref("/occurrences/" + result.occurrence_id);
    result.subject_entity_id = std::string(kEntityId);
    result.subject_source =
        source_ref("/occurrences/" + result.occurrence_id + "/subject");
    result.scope = scope;
    result.scope_source =
        source_ref("/occurrences/" + result.occurrence_id + "/scope");
    result.placement = placement;
    result.placement_source =
        source_ref("/occurrences/" + result.occurrence_id + "/placement");
    result.configuration = std::move(configuration);
    result.configuration_source = source_ref(
        "/occurrences/" + result.occurrence_id + "/configuration");
    append_field_sources(result);
    return result;
}

[[nodiscard]] compiler::CompleteStaticCompositionSource fixture_source(
    double ground_altitude_meters = 0.0) {
    compiler::CompleteStaticCompositionSource source;
    source.source_version =
        std::string(compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id = "mission.fixture.yyz.stuck-actuator@1";
    source.plan_id = "plan.fixture.yyz.stuck-actuator";
    source.mission_source = source_ref("/mission");
    source.clock = {std::string(kClockId), 0.1, 0, kTerminalTick,
                    source_ref("/clock")};
    source.entities.push_back(
        {std::string(kEntityId),
         compiler::EntityLifecycle::ActiveAtInitialize,
         source_ref("/entities/vehicle/id"),
         source_ref("/entities/vehicle/lifecycle")});
    const compiler::ScopeKey scope{compiler::ScopeKind::Vehicle,
                                   std::string(kEntityId)};
    source.scopes.push_back({scope, source_ref("/scopes/vehicle")});

    source.occurrences.push_back(occurrence(
        std::string(kDemandOccurrence),
        std::string(yyz::kScheduledSurfaceDemandModelId),
        config(std::string(yyz::kScheduledDemandConfigSchemaId),
               {{"first_change_tick", kFaultTick},
                {"first_position_radians", 0.25},
                {"initial_position_radians", 0.0},
                {"later_change_tick", kLaterDemandTick},
                {"later_position_radians", 0.35}}),
        scope, sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kActuatorOccurrence),
        std::string(yyz::kStatefulStuckActuatorModelId),
        config(std::string(yyz::kStuckActuatorConfigSchemaId),
               {{"maximum_position_radians", 0.5},
                {"minimum_position_radians", -0.5}}),
        scope, sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kLoadOccurrence),
        std::string(yyz::kPressureSurfaceLoadModelId),
        config(std::string(yyz::kPressureSurfaceLoadConfigSchemaId),
               {{"dynamic_pressure_pascals", 20000.0},
                {"lift_coefficient_slope_per_radian", 2.0},
                {"moment_arm_meters", 0.5},
                {"reference_area_square_meters", 0.02}}),
        scope, sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kRigidOccurrence),
        std::string(yyz::kVerticalPitchRigidModelId),
        config(std::string(yyz::kVerticalPitchRigidConfigSchemaId),
               {{"fixed_step_seconds", 0.1},
                {"gravity_meters_per_second_squared", 9.81},
                {"mass_kilograms", 10.0},
                {"pitch_inertia_kilogram_meters_squared", 20.0}}),
        scope, sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kContactOccurrence),
        std::string(yyz::kContactImpactEvaluatorModelId),
        config(std::string(yyz::kContactImpactConfigSchemaId),
               {{"ground_altitude_meters", ground_altitude_meters}}),
        scope, sdk::ModelPlacement::Evaluation));

    compiler::CompleteSourceInitialBinding actuator_initial;
    actuator_initial.owner_occurrence_id = std::string(kActuatorOccurrence);
    actuator_initial.builder_inputs =
        config(std::string(yyz::kActuatorInitialStateSchemaId),
               {{"locked_position_radians", 0.0}});
    actuator_initial.field_sources.push_back(
        {"locked_position_radians",
         source_ref("/initial/actuator/locked-position")});
    actuator_initial.source = source_ref("/initial/actuator");
    source.initial_bindings.push_back(std::move(actuator_initial));

    compiler::CompleteSourceInitialBinding rigid_initial;
    rigid_initial.owner_occurrence_id = std::string(kRigidOccurrence);
    rigid_initial.builder_inputs =
        config(std::string(yyz::kRigidInitialStateSchemaId),
               {{"altitude_meters", 5.0},
                {"pitch_radians", 0.0},
                {"pitch_rate_radians_per_second", 0.0},
                {"vertical_velocity_meters_per_second", 0.0}});
    for (const auto& field : rigid_initial.builder_inputs.fields) {
        rigid_initial.field_sources.push_back(
            {field.field_id,
             source_ref("/initial/rigid/" + field.field_id)});
    }
    rigid_initial.source = source_ref("/initial/rigid");
    source.initial_bindings.push_back(std::move(rigid_initial));

    source.bindings = {
        {"binding.stuck.demand-actuator", std::string(kDemandOccurrence),
         "surface-demand", std::string(kActuatorOccurrence),
         "surface-demand", source_ref("/bindings/demand-actuator")},
        {"binding.stuck.actuator-load", std::string(kActuatorOccurrence),
         "actual-surface", std::string(kLoadOccurrence), "actual-surface",
         source_ref("/bindings/actuator-load")},
        {"binding.stuck.load-rigid", std::string(kLoadOccurrence),
         "surface-load", std::string(kRigidOccurrence), "surface-load",
         source_ref("/bindings/load-rigid")}};

    source.transactions.push_back(
        {std::string(kTransactionId), scope,
         {std::string(kActuatorOccurrence), std::string(kRigidOccurrence)},
         source_ref("/transactions/stuck-actuator")});

    compiler::CompleteSourceEvaluatorHistory history;
    history.evaluator_occurrence_id = std::string(kContactOccurrence);
    history.history_id = "history.fixture.yyz.rigid-contact";
    history.committed_history_depth = yyz::kRigidContactHistoryDepth;
    history.owner_occurrence_ids = {std::string(kRigidOccurrence)};
    history.source = source_ref("/evaluators/contact/history");
    source.evaluator_histories.push_back(std::move(history));
    source.package_build_locks.push_back(
        {std::string(yyz::kStuckQualificationPackageId),
         std::string(yyz::kStuckQualificationPackageVersion),
         std::string(yyz::kStuckQualificationBuildFingerprint),
         source_ref("/packages/stuck-actuator")});
    return source;
}

[[nodiscard]] compiler::CommandRouteSpec stuck_route() {
    compiler::CommandRouteSpec route;
    route.route_id = std::string(kRouteId);
    route.target_occurrence_id = std::string(kActuatorOccurrence);
    route.payload_schema_id = std::string(yyz::kStuckActuatorCommandSchemaId);
    route.decision_authority = yyz::kStuckActuatorDecisionAuthority;
    route.queue_capacity = 4U;
    route.queue_policy = contracts::CommandQueuePolicy::RejectNewest;
    route.supersession_policy =
        contracts::CommandSupersessionPolicy::LatestDuePerKey;
    route.effective_point =
        contracts::CommandEffectivePoint::TransactionStart;
    route.cutoff_policy =
        contracts::CommandCutoffPolicy::LedgerSequenceAtTransactionStart;
    route.event_schema_id = std::string(yyz::kStuckActuatorEventSchemaId);
    route.event_consumer_occurrence_id = std::string(kActuatorOccurrence);
    route.source = source_ref("/command-routes/stuck-actuator");
    return route;
}

struct CompiledFixture {
    sdk::StaticPackageDescriptor package;
    sdk::StaticPackageImplementation implementation;
    compiler::CompleteStaticCompositionSource source;
    compiler::CompleteStaticCompilation base;
    compiler::CompleteExecutionPlanDescriptor plan;
    compiler::PlanProofIndex proofs;
    contracts::ExecutionPlanImage image;
};

[[nodiscard]] CompiledFixture compile_fixture(
    double ground_altitude_meters = 0.0) {
    auto package = yyz::describe_scheduled_stuck_actuator_package();
    auto implementation =
        yyz::describe_scheduled_stuck_actuator_implementation();
    auto source = fixture_source(ground_altitude_meters);
    const auto base_outcome =
        compiler::compile_complete_execution_plan(source, {package});
    if (!base_outcome.succeeded()) {
        throw std::runtime_error(
            "stuck-actuator base plan compilation failed: " +
            diagnostic_text(base_outcome));
    }
    auto base = *base_outcome.value;
    const auto routed = compiler::compile_command_event_routes(
        base, {stuck_route()});
    if (!routed.succeeded()) {
        throw std::runtime_error(
            "stuck-actuator route lowering failed: " +
            diagnostic_text(routed));
    }
    auto extended = *routed.value;
    const auto linked = compiler::link_complete_execution_plan(
        extended.plan, extended.proofs, {implementation});
    if (!linked.succeeded()) {
        throw std::runtime_error(
            "stuck-actuator Image link failed: " +
            diagnostic_text(linked));
    }
    return {std::move(package), std::move(implementation),
            std::move(source), std::move(base), std::move(extended.plan),
            std::move(extended.proofs), *linked.value};
}

struct RuntimeControl {
    std::size_t reducer_failures_remaining = 0U;
    std::size_t consumer_failures_remaining = 0U;
    std::size_t load_evaluation_failures_remaining = 0U;
    std::size_t seal_failures_remaining = 0U;
    std::size_t fail_stuck_validation_at = 0U;
    std::size_t stuck_validation_count = 0U;
    std::optional<contracts::TransactionBranch> forced_branch_decision;
};

template <typename Value>
class TypedOperations final : public kernel::InProcessObjectOperations {
  public:
    TypedOperations(std::string layout_id, std::uint32_t codec_handle,
                    RuntimeControl* control = nullptr)
        : layout_id_(std::move(layout_id)), codec_handle_(codec_handle),
          control_(control) {}

    [[nodiscard]] kernel::InProcessObjectLayout layout()
        const noexcept override {
        return {sizeof(Value), alignof(Value), layout_id_, codec_handle_,
                &typeid(Value), std::is_trivially_copyable_v<Value>,
                std::is_trivially_destructible_v<Value>};
    }

    [[nodiscard]] bool copy_construct(
        const void* source, void* destination) const noexcept override {
        if (source == nullptr || destination == nullptr) return false;
        if constexpr (std::is_same_v<Value, yyz::ActualSurfaceOutput>) {
            ++copy_count_;
            if (control_ != nullptr && copy_count_ % 2U == 0U &&
                control_->seal_failures_remaining != 0U) {
                --control_->seal_failures_remaining;
                return false;
            }
        }
        try {
            ::new (destination) Value(*static_cast<const Value*>(source));
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool replace(
        void* destination, const void* source) const noexcept override {
        if (source == nullptr || destination == nullptr) return false;
        try {
            *static_cast<Value*>(destination) =
                *static_cast<const Value*>(source);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool validate(const void* object) const noexcept override {
        if (object == nullptr) return false;
        if constexpr (std::is_same_v<Value, yyz::FaultStateFragment>) {
            const auto& state = *static_cast<const Value*>(object);
            if (!yyz::validate_stateful_actuator_state(state)) return false;
            if (state.mode == yyz::ActuatorFaultMode::Stuck &&
                control_ != nullptr &&
                control_->fail_stuck_validation_at != 0U) {
                ++control_->stuck_validation_count;
                if (control_->stuck_validation_count ==
                    control_->fail_stuck_validation_at) {
                    control_->fail_stuck_validation_at = 0U;
                    return false;
                }
            }
            return true;
        } else if constexpr (std::is_same_v<
                                 Value, yyz::VerticalPitchRigidState>) {
            return yyz::validate_vertical_pitch_rigid_state(
                *static_cast<const Value*>(object));
        } else {
            return true;
        }
    }

    [[nodiscard]] bool supports_nofail_swap() const noexcept override {
        return std::is_same_v<Value, yyz::FaultStateFragment> ||
               std::is_same_v<Value, yyz::VerticalPitchRigidState>;
    }

    void nofail_swap(void* lhs, void* rhs) const noexcept override {
        if constexpr (std::is_same_v<Value, yyz::FaultStateFragment>) {
            yyz::swap_stateful_actuator_state(
                *static_cast<Value*>(lhs), *static_cast<Value*>(rhs));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::VerticalPitchRigidState>) {
            yyz::swap_vertical_pitch_rigid_state(
                *static_cast<Value*>(lhs), *static_cast<Value*>(rhs));
        }
    }

    void destroy(void* object) const noexcept override {
        if (object != nullptr) static_cast<Value*>(object)->~Value();
    }

  private:
    std::string layout_id_;
    std::uint32_t codec_handle_ = 0U;
    RuntimeControl* control_ = nullptr;
    mutable std::size_t copy_count_ = 0U;
};

template <typename Value>
class TypedMaterializer final : public kernel::SessionObjectMaterializer {
  public:
    TypedMaterializer(kernel::SessionMaterializerIdentity identity,
                      std::string layout_id, Value value,
                      RuntimeControl* control = nullptr)
        : identity_(identity),
          operations_(std::move(layout_id), identity.codec_entry_handle,
                      control),
          value_(std::move(value)) {}

    [[nodiscard]] kernel::SessionMaterializerIdentity identity()
        const noexcept override {
        return identity_;
    }
    [[nodiscard]] const kernel::InProcessObjectOperations& operations()
        const noexcept override {
        return operations_;
    }
    [[nodiscard]] bool construct(
        const kernel::SessionObjectAccess&,
        void* destination) const noexcept override {
        if (destination == nullptr) return false;
        try {
            ::new (destination) Value(value_);
            return operations_.validate(destination);
        } catch (...) {
            return false;
        }
    }

  private:
    kernel::SessionMaterializerIdentity identity_;
    TypedOperations<Value> operations_;
    Value value_;
};

struct RuntimeMarker {
    std::uint32_t occurrence_handle = 0U;
};

class LambdaInvocation final : public kernel::SessionInvocationEntry {
  public:
    using Function = std::function<kernel::SessionResult(
        const kernel::SessionInvocationContext&)>;

    LambdaInvocation(kernel::SessionInvocationIdentity identity,
                     Function function)
        : identity_(identity), function_(std::move(function)) {}

    [[nodiscard]] kernel::SessionInvocationIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult invoke(
        const kernel::SessionInvocationContext& context)
        const noexcept override {
        try {
            return function_(context);
        } catch (...) {
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "stuck-actuator invocation threw"};
        }
    }

  private:
    kernel::SessionInvocationIdentity identity_;
    Function function_;
};

[[nodiscard]] const contracts::PlanImageOccurrence* occurrence_for_model(
    const contracts::ExecutionPlanImage& image,
    std::string_view model_id) noexcept {
    const auto found = std::find_if(
        image.occurrences().begin(), image.occurrences().end(),
        [model_id](const auto& value) {
            return value.definition_id == model_id;
        });
    return found == image.occurrences().end() ? nullptr : &*found;
}

[[nodiscard]] const contracts::PlanImageRuntimeComponent*
component_for_occurrence(
    const contracts::ExecutionPlanImage& image,
    std::uint32_t occurrence_handle) noexcept {
    const auto found = std::find_if(
        image.runtime_components().begin(), image.runtime_components().end(),
        [occurrence_handle](const auto& value) {
            return value.occurrence_handle == occurrence_handle;
        });
    return found == image.runtime_components().end() ? nullptr : &*found;
}

[[nodiscard]] const contracts::PlanImageCallsite* callsite_for(
    const contracts::ExecutionPlanImage& image,
    const contracts::PlanImageRuntimeComponent& component,
    std::string_view obligation) noexcept {
    const auto found = std::find_if(
        image.callsites().begin(), image.callsites().end(),
        [&](const auto& value) {
            return value.occurrence_handle == component.occurrence_handle &&
                   value.obligation == obligation;
        });
    return found == image.callsites().end() ? nullptr : &*found;
}

[[nodiscard]] const contracts::PlanImageSlot* slot_for_handle(
    const contracts::ExecutionPlanImage& image,
    std::uint32_t handle) noexcept {
    const auto found = std::find_if(
        image.slots().begin(), image.slots().end(),
        [handle](const auto& value) { return value.handle == handle; });
    return found == image.slots().end() ? nullptr : &*found;
}

[[nodiscard]] std::uint32_t slot_with_contract(
    const contracts::ExecutionPlanImage& image,
    const std::vector<std::uint32_t>& handles,
    std::string_view contract_id) {
    for (const auto handle : handles) {
        const auto* slot = slot_for_handle(image, handle);
        if (slot != nullptr && slot->contract_id == contract_id) return handle;
    }
    throw std::runtime_error("typed slot contract is absent");
}

[[nodiscard]] std::uint32_t candidate_slot(
    const contracts::ExecutionPlanImage& image,
    const std::vector<std::uint32_t>& handles) {
    for (const auto handle : handles) {
        const auto* slot = slot_for_handle(image, handle);
        if (slot != nullptr &&
            slot->kind == contracts::PlanImageSlotKind::CandidateState) {
            return handle;
        }
    }
    throw std::runtime_error("candidate state slot is absent");
}

[[nodiscard]] std::uint32_t writer_for_slot(
    const contracts::PlanImageCallsite& callsite,
    std::uint32_t slot_handle) {
    require(callsite.output_slot_handles.size() ==
                callsite.output_writer_token_handles.size(),
            "callsite output/writer shape differs");
    for (std::size_t index = 0U;
         index < callsite.output_slot_handles.size(); ++index) {
        if (callsite.output_slot_handles[index] == slot_handle) {
            return callsite.output_writer_token_handles[index];
        }
    }
    throw std::runtime_error("writer token for slot is absent");
}

template <typename Value, typename View>
[[nodiscard]] kernel::SessionResult read_typed(
    const View& view, std::uint32_t handle,
    const Value*& value) noexcept {
    value = nullptr;
    kernel::SessionObjectIdentityView object;
    auto result = view.read(handle, object);
    if (!result) return result;
    if (object.type_identity != &typeid(Value) ||
        object.size_bytes != sizeof(Value) ||
        object.alignment_bytes != alignof(Value)) {
        return {kernel::SessionError::ObjectTypeMismatch, handle,
                "typed invocation input differs from Image layout"};
    }
    value = static_cast<const Value*>(object.address);
    return {};
}

template <typename Value>
[[nodiscard]] kernel::SessionResult write_typed(
    const kernel::SessionOutputWriterSet& outputs,
    std::uint32_t slot_handle, std::uint32_t writer_handle,
    const Value& value) noexcept {
    return outputs.write(
        slot_handle, writer_handle,
        {&value, sizeof(Value), alignof(Value), &typeid(Value)});
}

template <typename Value>
[[nodiscard]] kernel::SessionResult write_candidate(
    const kernel::SessionCandidateWriterSet& candidates,
    std::uint32_t slot_handle, std::uint32_t writer_handle,
    const Value& value) noexcept {
    return candidates.write(
        slot_handle, writer_handle,
        {&value, sizeof(Value), alignof(Value), &typeid(Value)});
}

[[nodiscard]] sdk::CanonicalConfigBlock canonical_configuration(
    const contracts::PlanImageOccurrence::ConfigBlock& image) {
    sdk::CanonicalConfigBlock result;
    result.schema_id = image.schema_id;
    result.schema_version = image.schema_version;
    for (const auto& field : image.fields) {
        sdk::CanonicalConfigValue value;
        switch (field.kind) {
        case contracts::PlanImageValueKind::String:
            value = field.string_value;
            break;
        case contracts::PlanImageValueKind::Integer:
            value = field.integer_value;
            break;
        case contracts::PlanImageValueKind::Float64:
            value = field.float64_value;
            break;
        case contracts::PlanImageValueKind::Enum:
            value = sdk::CanonicalEnumValue{field.string_value};
            break;
        }
        result.fields.push_back({field.field_id, std::move(value)});
    }
    return result;
}

template <typename Value>
[[nodiscard]] Value require_numerical(
    gnc::foundation::NumericalOutcome<Value> outcome,
    std::string_view message) {
    require(outcome.succeeded(), message);
    return std::move(outcome.value());
}

class StuckReducer final : public kernel::SessionCommandReducerEntry {
  public:
    StuckReducer(kernel::SessionCommandReducerIdentity identity,
                 std::uint32_t state_block_handle,
                 std::uint32_t candidate_slot_handle,
                 std::uint32_t writer_token_handle,
                 yyz::StatefulActuatorDefinition definition,
                 RuntimeControl& control)
        : identity_(identity), state_block_handle_(state_block_handle),
          candidate_slot_handle_(candidate_slot_handle),
          writer_token_handle_(writer_token_handle), definition_(definition),
          control_(&control) {}

    [[nodiscard]] kernel::SessionCommandReducerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] bool accepts_payload(
        kernel::InProcessValueView payload) const noexcept override {
        return payload.object != nullptr &&
               payload.type_identity == &typeid(yyz::StuckActuatorCommand) &&
               payload.size_bytes == sizeof(yyz::StuckActuatorCommand) &&
               payload.alignment_bytes == alignof(yyz::StuckActuatorCommand) &&
               yyz::validate_stuck_actuator_command(
                   definition_, *static_cast<const yyz::StuckActuatorCommand*>(
                                    payload.object));
    }

    [[nodiscard]] kernel::SessionResult reduce(
        const kernel::SessionCommandReductionContext& context,
        kernel::SessionCommandReductionResult& result)
        const noexcept override {
        if (control_->reducer_failures_remaining != 0U) {
            --control_->reducer_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "stuck reducer injected failure"};
        }
        if (context.payload().type_identity !=
                &typeid(yyz::StuckActuatorCommand) ||
            context.decision_authority() !=
                yyz::kStuckActuatorDecisionAuthority) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.route_handle,
                    "stuck reducer payload or authority mismatch"};
        }
        const yyz::FaultStateFragment* prior = nullptr;
        auto status = read_typed(context.committed(), state_block_handle_,
                                 prior);
        if (!status) return status;
        const auto& command =
            *static_cast<const yyz::StuckActuatorCommand*>(
                context.payload().object);
        const auto reduction = yyz::reduce_stuck_actuator_command(
            definition_, *prior, command);
        if (!reduction.succeeded()) {
            result.decision = kernel::CommandApplicationDecision::Rejected;
            result.application_code = 4102U;
            return {};
        }
        status = write_candidate(
            context.candidates(), candidate_slot_handle_,
            writer_token_handle_, reduction.value().candidate);
        if (!status) return status;
        result.decision = kernel::CommandApplicationDecision::Applied;
        result.application_code = 4101U;
        result.event_payload = kernel::InProcessOwnedValue::make(
            reduction.value().event);
        return {};
    }

  private:
    kernel::SessionCommandReducerIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t candidate_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    yyz::StatefulActuatorDefinition definition_;
    RuntimeControl* control_ = nullptr;
};

class StuckEventConsumer final : public kernel::SessionEventConsumerEntry {
  public:
    StuckEventConsumer(kernel::SessionEventConsumerIdentity identity,
                       RuntimeControl& control)
        : identity_(identity), control_(&control) {}

    [[nodiscard]] kernel::SessionEventConsumerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult consume(
        const kernel::SessionEventConsumptionContext& context,
        kernel::InProcessOwnedValue& output) const noexcept override {
        if (control_->consumer_failures_remaining != 0U) {
            --control_->consumer_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "stuck event consumer injected failure"};
        }
        if (context.payload().type_identity !=
            &typeid(yyz::StuckActuatorEvent)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.delivery_handle,
                    "stuck event payload type mismatch"};
        }
        output = kernel::InProcessOwnedValue::make(
            yyz::consume_stuck_actuator_event(
                *static_cast<const yyz::StuckActuatorEvent*>(
                    context.payload().object)));
        return {};
    }

  private:
    kernel::SessionEventConsumerIdentity identity_;
    RuntimeControl* control_ = nullptr;
};

class FixtureProvider final : public kernel::SessionMaterializationProvider {
  public:
    explicit FixtureProvider(const contracts::ExecutionPlanImage& image) {
        build(image);
    }

    [[nodiscard]] const kernel::SessionObjectMaterializer* preparation(
        std::uint32_t) const noexcept override {
        return nullptr;
    }
    [[nodiscard]] const kernel::SessionObjectMaterializer* runtime_component(
        std::uint32_t handle) const noexcept override {
        return find(runtime_components_, handle);
    }
    [[nodiscard]] const kernel::SessionObjectMaterializer* slot(
        std::uint32_t handle) const noexcept override {
        return find(slots_, handle);
    }
    [[nodiscard]] const kernel::SessionObjectMaterializer* initial_state(
        std::uint32_t handle) const noexcept override {
        return find(initial_states_, handle);
    }
    [[nodiscard]] const kernel::SessionInvocationEntry* invocation(
        std::uint32_t handle) const noexcept override {
        const auto found = invocations_.find(handle);
        return found == invocations_.end() ? nullptr : found->second.get();
    }
    [[nodiscard]] const kernel::SessionCommandReducerEntry* command_reducer(
        std::uint32_t handle) const noexcept override {
        return reducer_ != nullptr &&
                       reducer_->identity().route_handle == handle
                   ? reducer_.get()
                   : nullptr;
    }
    [[nodiscard]] const kernel::SessionEventConsumerEntry* event_consumer(
        std::uint32_t handle) const noexcept override {
        return consumer_ != nullptr &&
                       consumer_->identity().delivery_handle == handle
                   ? consumer_.get()
                   : nullptr;
    }

    RuntimeControl control;
    std::uint32_t actuator_state_block_handle = 0U;
    std::uint32_t rigid_state_block_handle = 0U;
    std::uint32_t actual_surface_slot_handle = 0U;
    std::uint32_t contact_result_slot_handle = 0U;
    std::uint32_t terminal_branch_slot_handle = 0U;

  private:
    using MaterializerMap = std::unordered_map<
        std::uint32_t, std::unique_ptr<kernel::SessionObjectMaterializer>>;

    [[nodiscard]] static const kernel::SessionObjectMaterializer* find(
        const MaterializerMap& values, std::uint32_t handle) noexcept {
        const auto found = values.find(handle);
        return found == values.end() ? nullptr : found->second.get();
    }

    template <typename Value>
    void add_slot(const contracts::PlanImageSlot& slot, Value value) {
        slots_.emplace(
            slot.handle,
            std::make_unique<TypedMaterializer<Value>>(
                kernel::SessionMaterializerIdentity{
                    slot.handle,
                    slot.storage_class ==
                            contracts::SlotStorageClass::TerminalResult
                        ? kernel::SessionObjectRole::TerminalOutputValue
                        : kernel::SessionObjectRole::CycleFrameValue,
                    0U, slot.codec_entry_handle},
                slot.layout_id, std::move(value), &control));
    }

    void build_slots(const contracts::ExecutionPlanImage& image) {
        for (const auto& slot : image.slots()) {
            if (slot.kind == contracts::PlanImageSlotKind::CommittedState ||
                slot.kind == contracts::PlanImageSlotKind::CandidateState) {
                continue;
            }
            if (slot.layout_id == yyz::kScheduledSurfaceDemandLayoutId) {
                add_slot(slot, yyz::ScheduledSurfaceDemand{});
            } else if (slot.layout_id ==
                       yyz::kActuatorStateObservationLayoutId) {
                add_slot(slot, yyz::ActuatorStateObservation{});
            } else if (slot.layout_id ==
                       yyz::kActualSurfaceOutputLayoutId) {
                actual_surface_slot_handle = slot.handle;
                add_slot(slot, yyz::ActualSurfaceOutput{});
            } else if (slot.layout_id ==
                       yyz::kPressureSurfaceLoadLayoutId) {
                add_slot(slot, yyz::PressureSurfaceLoad{});
            } else if (slot.layout_id ==
                       yyz::kVerticalPitchRigidObservationLayoutId) {
                add_slot(slot, yyz::VerticalPitchRigidObservation{});
            } else if (slot.layout_id ==
                       yyz::kContactImpactResultLayoutId) {
                contact_result_slot_handle = slot.handle;
                add_slot(slot, yyz::ContactImpactResult{});
            } else if (slot.layout_id ==
                       yyz::kTerminalBranchDecisionLayoutId) {
                terminal_branch_slot_handle = slot.handle;
                add_slot(slot, contracts::TransactionBranch::Continue);
            } else {
                throw std::runtime_error(
                    "unsupported stuck-actuator slot layout");
            }
        }
    }

    void build(const contracts::ExecutionPlanImage& image) {
        require(image.command_routes().size() == 1U &&
                    image.event_deliveries().size() == 1U &&
                    image.state_blocks().size() == 2U,
                "stuck-actuator Image shape differs from fixture contract");

        const auto* demand_occurrence = occurrence_for_model(
            image, yyz::kScheduledSurfaceDemandModelId);
        const auto* actuator_occurrence = occurrence_for_model(
            image, yyz::kStatefulStuckActuatorModelId);
        const auto* load_occurrence = occurrence_for_model(
            image, yyz::kPressureSurfaceLoadModelId);
        const auto* rigid_occurrence = occurrence_for_model(
            image, yyz::kVerticalPitchRigidModelId);
        const auto* contact_occurrence = occurrence_for_model(
            image, yyz::kContactImpactEvaluatorModelId);
        require(demand_occurrence != nullptr && actuator_occurrence != nullptr &&
                    load_occurrence != nullptr && rigid_occurrence != nullptr &&
                    contact_occurrence != nullptr,
                "stuck-actuator Image occurrence is absent");

        demand_definition_ = require_numerical(
            yyz::build_scheduled_demand_definition(
                canonical_configuration(
                    demand_occurrence->canonical_configuration)),
            "scheduled demand Image configuration was rejected");
        actuator_definition_ = require_numerical(
            yyz::build_stateful_actuator_definition(
                canonical_configuration(
                    actuator_occurrence->canonical_configuration)),
            "actuator Image configuration was rejected");
        load_definition_ = require_numerical(
            yyz::build_pressure_surface_load_definition(
                canonical_configuration(
                    load_occurrence->canonical_configuration)),
            "load Image configuration was rejected");
        rigid_definition_ = require_numerical(
            yyz::build_vertical_pitch_rigid_definition(
                canonical_configuration(
                    rigid_occurrence->canonical_configuration)),
            "rigid Image configuration was rejected");
        contact_definition_ = require_numerical(
            yyz::build_contact_impact_definition(
                canonical_configuration(
                    contact_occurrence->canonical_configuration)),
            "contact Image configuration was rejected");

        for (const auto& component : image.runtime_components()) {
            runtime_components_.emplace(
                component.handle,
                std::make_unique<TypedMaterializer<RuntimeMarker>>(
                    kernel::SessionMaterializerIdentity{
                        component.handle,
                        kernel::SessionObjectRole::RuntimeCell,
                        component.runtime_cell_factory_entry_handle, 0U},
                    "gnc.layout.fixture.yyz.stuck-runtime-marker@1",
                    RuntimeMarker{component.occurrence_handle}));
        }
        build_slots(image);

        for (const auto& block : image.state_blocks()) {
            const auto binding = std::find_if(
                image.initial_bindings().begin(),
                image.initial_bindings().end(), [&](const auto& value) {
                    return value.committed_state_slot_handle ==
                           block.committed_slot_handle;
                });
            require(binding != image.initial_bindings().end(),
                    "state block initial binding is absent");
            if (block.layout_id == yyz::kActuatorStateLayoutId) {
                actuator_state_block_handle = block.handle;
                const auto state = require_numerical(
                    yyz::build_stateful_actuator_initial_state(
                        actuator_definition_, {0.0}),
                    "actuator initial state was rejected");
                initial_states_.emplace(
                    binding->handle,
                    std::make_unique<TypedMaterializer<
                        yyz::FaultStateFragment>>(
                        kernel::SessionMaterializerIdentity{
                            binding->handle,
                            kernel::SessionObjectRole::InitialStateValue,
                            binding->builder_entry_handle,
                            block.codec_entry_handle},
                        block.layout_id, state, &control));
            } else if (block.layout_id ==
                       yyz::kVerticalPitchRigidStateLayoutId) {
                rigid_state_block_handle = block.handle;
                const auto state = require_numerical(
                    yyz::build_vertical_pitch_rigid_initial_state(
                        rigid_definition_, {5.0, 0.0, 0.0, 0.0}),
                    "rigid initial state was rejected");
                initial_states_.emplace(
                    binding->handle,
                    std::make_unique<TypedMaterializer<
                        yyz::VerticalPitchRigidState>>(
                        kernel::SessionMaterializerIdentity{
                            binding->handle,
                            kernel::SessionObjectRole::InitialStateValue,
                            binding->builder_entry_handle,
                            block.codec_entry_handle},
                        block.layout_id, state, &control));
            } else {
                throw std::runtime_error(
                    "unsupported stuck-actuator state layout");
            }
        }

        install_demand(image, *demand_occurrence);
        install_actuator(image, *actuator_occurrence);
        install_load(image, *load_occurrence);
        install_rigid(image, *rigid_occurrence);
        install_contact(image, *contact_occurrence);
    }

    void install_demand(const contracts::ExecutionPlanImage& image,
                        const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* callsite = component == nullptr
                                   ? nullptr
                                   : callsite_for(image, *component,
                                                  "BoundaryEvaluation");
        require(component != nullptr && callsite != nullptr,
                "demand callsite is absent");
        const auto output = slot_with_contract(
            image, callsite->output_slot_handles,
            yyz::kScheduledSurfaceDemandContractId);
        const auto writer = writer_for_slot(*callsite, output);
        invocations_.emplace(
            callsite->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    callsite->handle, component->handle,
                    callsite->entry_handle},
                [this, output, writer](const auto& context) {
                    const auto value = yyz::evaluate_scheduled_surface_demand(
                        demand_definition_, context.tick());
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "scheduled demand evaluation failed"};
                    }
                    return write_typed(context.outputs(), output, writer,
                                       value.value());
                }));
    }

    void install_actuator(
        const contracts::ExecutionPlanImage& image,
        const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* publish = component == nullptr
                                  ? nullptr
                                  : callsite_for(image, *component,
                                                 "PublishProjection");
        const auto* boundary = component == nullptr
                                   ? nullptr
                                   : callsite_for(image, *component,
                                                  "BoundaryEvaluation");
        const auto* reduction = component == nullptr
                                    ? nullptr
                                    : callsite_for(image, *component,
                                                   "CommandReduction");
        const auto* consumption = component == nullptr
                                      ? nullptr
                                      : callsite_for(image, *component,
                                                     "EventConsumption");
        require(component != nullptr && publish != nullptr &&
                    boundary != nullptr && reduction != nullptr &&
                    consumption != nullptr,
                "actuator callsite set is incomplete");

        const auto state_output = slot_with_contract(
            image, publish->output_slot_handles,
            yyz::kActuatorStateObservationContractId);
        const auto state_writer = writer_for_slot(*publish, state_output);
        invocations_.emplace(
            publish->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    publish->handle, component->handle,
                    publish->entry_handle},
                [this, state_output, state_writer](const auto& context) {
                    const yyz::FaultStateFragment* state = nullptr;
                    auto status = read_typed(
                        context.committed(), actuator_state_block_handle,
                        state);
                    if (!status) return status;
                    const auto value = yyz::project_actuator_state(*state);
                    return write_typed(context.outputs(), state_output,
                                       state_writer, value);
                }));

        const auto effective_state_slot =
            candidate_slot(image, boundary->input_slot_handles);
        const auto demand_slot = slot_with_contract(
            image, boundary->input_slot_handles,
            yyz::kScheduledSurfaceDemandContractId);
        const auto actual_output = slot_with_contract(
            image, boundary->output_slot_handles,
            yyz::kActualSurfaceOutputContractId);
        const auto actual_writer =
            writer_for_slot(*boundary, actual_output);
        invocations_.emplace(
            boundary->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    boundary->handle, component->handle,
                    boundary->entry_handle},
                [this, effective_state_slot, demand_slot, actual_output,
                 actual_writer](const auto& context) {
                    const yyz::FaultStateFragment* state = nullptr;
                    const yyz::ScheduledSurfaceDemand* demand = nullptr;
                    auto status = read_typed(context.inputs(),
                                             effective_state_slot, state);
                    if (!status) return status;
                    status = read_typed(context.inputs(), demand_slot, demand);
                    if (!status) return status;
                    const auto value = yyz::evaluate_stateful_actuator(
                        actuator_definition_, *state, *demand);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            actual_output,
                            "stateful actuator evaluation failed"};
                    }
                    return write_typed(context.outputs(), actual_output,
                                       actual_writer, value.value());
                }));

        const auto& route = image.command_routes().front();
        const auto& delivery = image.event_deliveries().front();
        const auto reducer_candidate =
            candidate_slot(image, reduction->output_slot_handles);
        reducer_ = std::make_unique<StuckReducer>(
            kernel::SessionCommandReducerIdentity{
                route.handle, reduction->handle, component->handle,
                reduction->entry_handle,
                &typeid(yyz::StuckActuatorCommand),
                &typeid(yyz::StuckActuatorEvent)},
            actuator_state_block_handle, reducer_candidate,
            writer_for_slot(*reduction, reducer_candidate),
            actuator_definition_, control);
        consumer_ = std::make_unique<StuckEventConsumer>(
            kernel::SessionEventConsumerIdentity{
                delivery.handle, consumption->handle, component->handle,
                consumption->entry_handle,
                &typeid(yyz::StuckActuatorEvent),
                &typeid(yyz::StuckActuatorEventConsumed)},
            control);
    }

    void install_load(const contracts::ExecutionPlanImage& image,
                      const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* callsite = component == nullptr
                                   ? nullptr
                                   : callsite_for(image, *component,
                                                  "BoundaryEvaluation");
        require(component != nullptr && callsite != nullptr,
                "surface load callsite is absent");
        const auto input = slot_with_contract(
            image, callsite->input_slot_handles,
            yyz::kActualSurfaceOutputContractId);
        const auto output = slot_with_contract(
            image, callsite->output_slot_handles,
            yyz::kPressureSurfaceLoadContractId);
        const auto writer = writer_for_slot(*callsite, output);
        invocations_.emplace(
            callsite->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    callsite->handle, component->handle,
                    callsite->entry_handle},
                [this, input, output, writer](const auto& context) {
                    if (control.load_evaluation_failures_remaining != 0U) {
                        --control.load_evaluation_failures_remaining;
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "surface load injected evaluation failure"};
                    }
                    const yyz::ActualSurfaceOutput* actuator = nullptr;
                    auto status =
                        read_typed(context.inputs(), input, actuator);
                    if (!status) return status;
                    const auto value = yyz::evaluate_pressure_surface_load(
                        load_definition_, *actuator);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "surface load evaluation failed"};
                    }
                    return write_typed(context.outputs(), output, writer,
                                       value.value());
                }));
    }

    void install_rigid(const contracts::ExecutionPlanImage& image,
                       const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* publish = component == nullptr
                                  ? nullptr
                                  : callsite_for(image, *component,
                                                 "PublishProjection");
        const auto* evolution = component == nullptr
                                    ? nullptr
                                    : callsite_for(image, *component,
                                                   "IntervalEvolution");
        require(component != nullptr && publish != nullptr &&
                    evolution != nullptr,
                "rigid callsite set is incomplete");
        const auto observation = slot_with_contract(
            image, publish->output_slot_handles,
            yyz::kVerticalPitchRigidObservationContractId);
        const auto observation_writer =
            writer_for_slot(*publish, observation);
        invocations_.emplace(
            publish->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    publish->handle, component->handle,
                    publish->entry_handle},
                [this, observation,
                 observation_writer](const auto& context) {
                    const yyz::VerticalPitchRigidState* state = nullptr;
                    auto status = read_typed(context.committed(),
                                             rigid_state_block_handle, state);
                    if (!status) return status;
                    const auto value = yyz::project_vertical_pitch_rigid(
                        *state, context.tick());
                    return write_typed(context.outputs(), observation,
                                       observation_writer, value);
                }));

        const auto load_slot = slot_with_contract(
            image, evolution->input_slot_handles,
            yyz::kPressureSurfaceLoadContractId);
        const auto next_state_slot =
            candidate_slot(image, evolution->output_slot_handles);
        const auto next_state_writer =
            writer_for_slot(*evolution, next_state_slot);
        invocations_.emplace(
            evolution->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    evolution->handle, component->handle,
                    evolution->entry_handle},
                [this, load_slot, next_state_slot,
                 next_state_writer](const auto& context) {
                    const yyz::VerticalPitchRigidState* prior = nullptr;
                    const yyz::PressureSurfaceLoad* load = nullptr;
                    auto status = read_typed(context.committed(),
                                             rigid_state_block_handle, prior);
                    if (!status) return status;
                    status = read_typed(context.inputs(), load_slot, load);
                    if (!status) return status;
                    const auto value = yyz::evolve_vertical_pitch_rigid(
                        rigid_definition_, *prior, *load);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            next_state_slot,
                            "vertical pitch rigid evolution failed"};
                    }
                    return write_candidate(context.candidates(),
                                           next_state_slot,
                                           next_state_writer, value.value());
                }));
    }

    void install_contact(
        const contracts::ExecutionPlanImage& image,
        const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* callsite = component == nullptr
                                   ? nullptr
                                   : callsite_for(image, *component,
                                                  "BoundaryEvaluation");
        require(component != nullptr && callsite != nullptr &&
                    component->evaluator_history_handles.size() == 1U,
                "contact evaluator callsite/history is absent");
        const auto output = slot_with_contract(
            image, callsite->output_slot_handles,
            yyz::kContactImpactResultContractId);
        const auto writer = writer_for_slot(*callsite, output);
        const auto branch_output = slot_with_contract(
            image, callsite->output_slot_handles,
            yyz::kTerminalBranchDecisionContractId);
        const auto branch_writer = writer_for_slot(*callsite, branch_output);
        const auto history = component->evaluator_history_handles.front();
        invocations_.emplace(
            callsite->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    callsite->handle, component->handle,
                    callsite->entry_handle},
                [this, output, writer, branch_output, branch_writer,
                 history](const auto& context) {
                    kernel::SessionCommittedHistoryInfo info;
                    auto status = context.history().info(history, info);
                    if (!status || info.sample_count == 0U ||
                        info.member_count != 1U) {
                        return kernel::SessionResult{
                            kernel::SessionError::HistoryValidationFailed,
                            history,
                            "contact evaluator history is incomplete"};
                    }
                    std::int64_t sample_tick = 0;
                    kernel::SessionObjectIdentityView object;
                    status = context.history().read(
                        history, info.sample_count - 1U, 0U, sample_tick,
                        object);
                    if (!status) return status;
                    if (object.type_identity !=
                        &typeid(yyz::VerticalPitchRigidState)) {
                        return kernel::SessionResult{
                            kernel::SessionError::ObjectTypeMismatch, history,
                            "contact evaluator history type mismatch"};
                    }
                    const auto value = yyz::evaluate_contact_impact(
                        contact_definition_,
                        *static_cast<const yyz::VerticalPitchRigidState*>(
                            object.address),
                        context.tick());
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "contact impact evaluation failed"};
                    }
                    status = write_typed(context.outputs(), output, writer,
                                         value.value().result);
                    if (!status) return status;
                    const auto branch =
                        control.forced_branch_decision.value_or(
                            value.value().branch);
                    return write_typed(context.outputs(), branch_output,
                                       branch_writer, branch);
                }));
    }

    MaterializerMap runtime_components_;
    MaterializerMap slots_;
    MaterializerMap initial_states_;
    std::unordered_map<std::uint32_t,
                       std::unique_ptr<kernel::SessionInvocationEntry>>
        invocations_;
    std::unique_ptr<StuckReducer> reducer_;
    std::unique_ptr<StuckEventConsumer> consumer_;
    yyz::ScheduledDemandDefinition demand_definition_;
    yyz::StatefulActuatorDefinition actuator_definition_;
    yyz::PressureSurfaceLoadDefinition load_definition_;
    yyz::VerticalPitchRigidDefinition rigid_definition_;
    yyz::ContactImpactDefinition contact_definition_;
};

struct LiveSession {
    std::shared_ptr<const contracts::ExecutionPlanImage> image;
    std::shared_ptr<FixtureProvider> provider;
    std::unique_ptr<kernel::Session> session;
    kernel::RunId run_id;
};

[[nodiscard]] LiveSession initialize_session(
    const contracts::ExecutionPlanImage& image, std::string run_id) {
    auto shared_image =
        std::make_shared<const contracts::ExecutionPlanImage>(image);
    auto provider = std::make_shared<FixtureProvider>(*shared_image);
    auto creation = kernel::create_session(shared_image, provider);
    require(static_cast<bool>(creation),
            "stuck-actuator Session creation failed");
    auto session = std::move(creation.session);
    kernel::RunId id(std::move(run_id));
    const auto initialized = session->initialize(
        {id, kernel::exact_run_binding(*shared_image)});
    require(static_cast<bool>(initialized),
            "stuck-actuator Session initialization failed");
    return {std::move(shared_image), std::move(provider),
            std::move(session), std::move(id)};
}

[[nodiscard]] contracts::ExecutionPlanImage image_from(
    contracts::ExecutionPlanImageData data) {
    data.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(data);
    return contracts::ExecutionPlanImage::freeze(std::move(data));
}

[[nodiscard]] kernel::CommandRequest stuck_command(
    const LiveSession& live, std::string command_id,
    std::int64_t effective_tick = kFaultTick,
    double locked_position = 0.0) {
    const auto& route = live.image->command_routes().front();
    return {kernel::CommandId(std::move(command_id)), live.run_id,
            route.handle, route.target_runtime_component_handle,
            route.payload_schema_id, route.decision_authority,
            effective_tick, std::nullopt, "primary-actuator-stuck",
            kernel::InProcessOwnedValue::make(
                yyz::StuckActuatorCommand{locked_position})};
}

template <typename Value>
[[nodiscard]] Value read_committed_state(
    const LiveSession& live, std::uint32_t state_block_handle) {
    kernel::SessionObjectIdentityView view;
    const auto status =
        kernel::qualification::SessionAccess::read_committed(
            *live.session, state_block_handle, view);
    require(status && view.type_identity == &typeid(Value),
            "committed qualification state is unavailable");
    return *static_cast<const Value*>(view.address);
}

template <typename Value>
[[nodiscard]] Value read_committed_output(
    const LiveSession& live, std::uint32_t slot_handle) {
    kernel::SessionObjectIdentityView view;
    const auto status =
        kernel::qualification::SessionAccess::read_committed_output(
            *live.session, slot_handle, view);
    require(status && view.type_identity == &typeid(Value),
            "committed qualification output is unavailable");
    return *static_cast<const Value*>(view.address);
}

struct ScenarioResult {
    yyz::ActualSurfaceOutput fault_tick_output;
    yyz::ActualSurfaceOutput later_tick_output;
    yyz::FaultStateFragment actuator;
    yyz::VerticalPitchRigidState rigid;
    yyz::ContactImpactResult contact;
    std::size_t application_receipts = 0U;
    std::size_t events = 0U;
    double preterminal_altitude_meters = 0.0;
    kernel::StepOutcome terminal_step;
    kernel::RunOutcome outcome;
};

[[nodiscard]] ScenarioResult run_scenario(
    const contracts::ExecutionPlanImage& image, bool stuck,
    std::string run_id) {
    auto live = initialize_session(image, std::move(run_id));
    if (stuck) {
        const auto submitted = live.session->submit_command(
            stuck_command(live, "scheduled-stuck"));
        require(static_cast<bool>(submitted),
                "scheduled stuck command was rejected");
    }
    ScenarioResult result;
    bool fault_output_seen = false;
    bool later_output_seen = false;
    double prior_opening_altitude =
        read_committed_state<yyz::VerticalPitchRigidState>(
            live, live.provider->rigid_state_block_handle)
            .altitude_meters;
    while (live.session->state() == kernel::SessionState::Initialized) {
        const auto opening_rigid =
            read_committed_state<yyz::VerticalPitchRigidState>(
                live, live.provider->rigid_state_block_handle);
        const auto step = live.session->execute_step();
        require(step.status == kernel::StepStatus::Committed ||
                    step.status == kernel::StepStatus::Terminated,
                "stuck-actuator scenario step failed");
        if (step.status == kernel::StepStatus::Terminated) {
            result.preterminal_altitude_meters = prior_opening_altitude;
            result.terminal_step = step;
        }
        if (step.tick_before == kFaultTick) {
            result.fault_tick_output =
                read_committed_output<yyz::ActualSurfaceOutput>(
                    live, live.provider->actual_surface_slot_handle);
            if (stuck) {
                const auto* occurrence = occurrence_for_model(
                    image, yyz::kStatefulStuckActuatorModelId);
                const auto* component =
                    occurrence == nullptr
                        ? nullptr
                        : component_for_occurrence(image,
                                                   occurrence->handle);
                const auto* boundary =
                    component == nullptr
                        ? nullptr
                        : callsite_for(image, *component,
                                       "BoundaryEvaluation");
                require(boundary != nullptr,
                        "fault-tick actuator boundary is absent");
                const auto& order = live.session->last_step_summary()
                                        .executed_callsite_handles;
                const auto reducer = std::find(
                    order.begin(), order.end(),
                    image.command_routes().front()
                        .reducer_callsite_handle);
                const auto actuator = std::find(
                    order.begin(), order.end(), boundary->handle);
                const auto consumer = std::find(
                    order.begin(), order.end(),
                    image.event_deliveries().front()
                        .consumer_callsite_handle);
                require(reducer != order.end() &&
                            actuator != order.end() &&
                            consumer != order.end() &&
                            reducer < actuator && actuator < consumer,
                        "fault-tick reducer/output/event order is invalid");
            }
            fault_output_seen = true;
        }
        if (step.tick_before == kLaterDemandTick) {
            result.later_tick_output =
                read_committed_output<yyz::ActualSurfaceOutput>(
                    live, live.provider->actual_surface_slot_handle);
            later_output_seen = true;
        }
        prior_opening_altitude = opening_rigid.altitude_meters;
    }
    require(fault_output_seen && later_output_seen &&
                live.session->state() == kernel::SessionState::Completed,
            "stuck-actuator scenario did not reach its terminal boundary");
    result.actuator = read_committed_state<yyz::FaultStateFragment>(
        live, live.provider->actuator_state_block_handle);
    result.rigid = read_committed_state<yyz::VerticalPitchRigidState>(
        live, live.provider->rigid_state_block_handle);
    result.contact = read_committed_output<yyz::ContactImpactResult>(
        live, live.provider->contact_result_slot_handle);
    result.application_receipts =
        live.session->command_application_receipts().size();
    result.events = live.session->committed_events().size();
    const auto* outcome = live.session->run_outcome();
    require(outcome != nullptr, "completed run outcome is absent");
    result.outcome = *outcome;

    if (stuck) {
        const auto& receipt =
            live.session->command_application_receipts().front();
        const auto& event = live.session->committed_events().front();
        const auto* payload = event.payload.get_if<yyz::StuckActuatorEvent>();
        const auto* consumed =
            event.consumer_output.get_if<yyz::StuckActuatorEventConsumed>();
        require(receipt.decision ==
                    kernel::CommandApplicationDecision::Applied &&
                    receipt.tick == kFaultTick && payload != nullptr &&
                    consumed != nullptr &&
                    payload->committed_mode ==
                        yyz::ActuatorFaultMode::Stuck &&
                    near(payload->locked_position_radians, 0.0) &&
                    consumed->observed_mode ==
                        yyz::ActuatorFaultMode::Stuck,
                "typed stuck receipt/event evidence is invalid");
    }
    return result;
}

void verify_image_contract(const CompiledFixture& fixture) {
    require(fixture.base.plan.command_routes.empty() &&
                fixture.plan.command_routes.size() == 1U &&
                fixture.plan.event_deliveries.size() == 1U &&
                fixture.image.command_routes().size() == 1U &&
                fixture.image.event_deliveries().size() == 1U,
            "stuck command route did not lower into one Image route/event");
    const auto* actuator = occurrence_for_model(
        fixture.image, yyz::kStatefulStuckActuatorModelId);
    const auto* component = actuator == nullptr
                                ? nullptr
                                : component_for_occurrence(
                                      fixture.image, actuator->handle);
    const auto model = std::find_if(
        fixture.package.models.begin(), fixture.package.models.end(),
        [](const auto& value) {
            return value.definition.model_id ==
                   yyz::kStatefulStuckActuatorModelId;
        });
    const auto& history = fixture.image.evaluator_histories().front();
    const auto* decision_slot = std::find_if(
        fixture.image.slots().begin(), fixture.image.slots().end(),
        [&](const auto& slot) {
            return slot.handle == history.branch_decision_slot_handle;
        }) == fixture.image.slots().end()
                                    ? nullptr
                                    : &*std::find_if(
                                          fixture.image.slots().begin(),
                                          fixture.image.slots().end(),
                                          [&](const auto& slot) {
                                              return slot.handle ==
                                                  history.branch_decision_slot_handle;
                                          });
    require(component != nullptr &&
                component->profile == "DiscreteStateProcessor" &&
                model != fixture.package.models.end() &&
                model->runtime_component.has_value() &&
                model->runtime_component->state_owner.has_value() &&
                model->runtime_component->state_owner->schema.fields.size() ==
                    3U &&
                model->runtime_component->state_owner->schema.fields[0]
                        .field_id == "mode" &&
                model->runtime_component->state_owner->schema.fields[1]
                        .field_id == "locked_position_radians" &&
                model->runtime_component->state_owner->schema.fields[2]
                        .field_id == "revision" &&
                fixture.image.state_blocks().size() == 2U &&
                fixture.image.transactions().size() == 1U &&
                history.history_depth == 1U &&
                history.branch_decision_slot_handle != 0U &&
                decision_slot != nullptr &&
                decision_slot->storage_class ==
                    contracts::SlotStorageClass::TerminalResult,
            "stuck actuator did not remain a typed discrete state owner");
}

void verify_healthy_stuck_ab(
    const contracts::ExecutionPlanImage& image,
    ScenarioResult& healthy, ScenarioResult& stuck) {
    healthy = run_scenario(image, false, "run.stuck-ab.healthy");
    stuck = run_scenario(image, true, "run.stuck-ab.faulted");

    require(near(healthy.fault_tick_output.demanded_position_radians, 0.25) &&
                near(healthy.fault_tick_output.actual_position_radians, 0.25) &&
                near(healthy.later_tick_output.demanded_position_radians,
                     0.35) &&
                near(healthy.later_tick_output.actual_position_radians, 0.35),
            "healthy actuator did not follow both scheduled demands");
    require(stuck.fault_tick_output.tick == kFaultTick &&
                near(stuck.fault_tick_output.demanded_position_radians,
                     0.25) &&
                near(stuck.fault_tick_output.actual_position_radians, 0.0) &&
                stuck.fault_tick_output.mode ==
                    yyz::ActuatorFaultMode::Stuck,
            "stuck command did not affect the formal actuator output at its target tick");
    require(near(stuck.later_tick_output.demanded_position_radians, 0.35) &&
                near(stuck.later_tick_output.actual_position_radians, 0.0) &&
                stuck.later_tick_output.mode ==
                    yyz::ActuatorFaultMode::Stuck,
            "stuck actuator did not hold after the later demand change");
    require(stuck.application_receipts == 1U && stuck.events == 1U &&
                healthy.application_receipts == 0U && healthy.events == 0U,
            "stuck command did not commit exactly one receipt and event");
    require(healthy.actuator.mode ==
                    yyz::ActuatorFaultMode::Healthy &&
                stuck.actuator.mode ==
                    yyz::ActuatorFaultMode::Stuck &&
                stuck.actuator.revision == 1U &&
                near(stuck.actuator.locked_position_radians, 0.0),
            "healthy and stuck actuator owner states did not diverge");
    require(healthy.rigid.altitude_meters > 0.0 &&
                stuck.rigid.altitude_meters < 0.0 &&
                healthy.rigid.pitch_radians > stuck.rigid.pitch_radians &&
                healthy.rigid.last_lift_force_newtons >
                    stuck.rigid.last_lift_force_newtons,
            "actuator fault did not change pressure load and rigid dynamics");
    require(!healthy.contact.impact && stuck.contact.impact &&
                healthy.contact.reason_code == "clearance-maintained" &&
                stuck.contact.reason_code == "ground-impact" &&
                healthy.outcome.final_status ==
                    kernel::RunFinalStatus::Completed &&
                stuck.outcome.final_status ==
                    kernel::RunFinalStatus::Completed &&
                healthy.outcome.mission_result_available &&
                stuck.outcome.mission_result_available &&
                !stuck.outcome.primary_diagnostic.has_value() &&
                stuck.outcome.related_diagnostics.empty(),
            "contact result or RunOutcome did not distinguish healthy and stuck runs");
    require(stuck.contact.tick == 10 &&
                near(stuck.contact.altitude_meters, -0.3955) &&
                stuck.preterminal_altitude_meters > 0.0 &&
                stuck.rigid.revision == 10U &&
                stuck.terminal_step.status ==
                    kernel::StepStatus::Terminated &&
                stuck.terminal_step.branch ==
                    contracts::TransactionBranch::Terminal &&
                stuck.terminal_step.tick_before == 10 &&
                stuck.terminal_step.tick_after == 10 &&
                stuck.terminal_step.candidates.present_count == 0U &&
                stuck.outcome.final_tick == 10 &&
                stuck.outcome.final_committed_epoch == 11U &&
                stuck.outcome.committed_step_count == 11U &&
                stuck.outcome.terminal_branch_committed &&
                healthy.contact.tick == kTerminalTick &&
                near(healthy.rigid.altitude_meters, 12.799),
            "first satisfying committed boundary was not the terminal step");
}

void verify_invalid_payload(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.stuck-invalid-payload");
    const auto actuator_before =
        read_committed_state<yyz::FaultStateFragment>(
            live, live.provider->actuator_state_block_handle);
    const auto rigid_before = read_committed_state<yyz::VerticalPitchRigidState>(
        live, live.provider->rigid_state_block_handle);
    auto invalid = stuck_command(
        live, "invalid-nonfinite", kFaultTick,
        (std::numeric_limits<double>::quiet_NaN)());
    const auto rejected = live.session->submit_command(invalid);
    const auto retry = live.session->submit_command(std::move(invalid));
    require(!rejected &&
                rejected.reason ==
                    kernel::CommandSubmissionReason::PayloadRejected &&
                !retry && retry.reason == rejected.reason &&
                retry.duplicate_retry &&
                live.session->pending_command_count() == 0U &&
                live.session->command_application_receipts().empty() &&
                live.session->committed_events().empty() &&
                live.session->committed_outputs().empty() &&
                live.session->state() ==
                    kernel::SessionState::Initialized &&
                live.session->run_outcome() == nullptr,
            "invalid stuck payload entered application evidence or output state");
    const auto actuator_after =
        read_committed_state<yyz::FaultStateFragment>(
            live, live.provider->actuator_state_block_handle);
    const auto rigid_after = read_committed_state<yyz::VerticalPitchRigidState>(
        live, live.provider->rigid_state_block_handle);
    require(actuator_after.mode == actuator_before.mode &&
                actuator_after.revision == actuator_before.revision &&
                near(actuator_after.locked_position_radians,
                     actuator_before.locked_position_radians) &&
                rigid_after.revision == rigid_before.revision &&
                near(rigid_after.altitude_meters, rigid_before.altitude_meters),
            "invalid stuck payload changed committed model state");

    const auto out_of_range = live.session->submit_command(
        stuck_command(live, "invalid-range", kFaultTick, 0.75));
    require(!out_of_range &&
                out_of_range.reason ==
                    kernel::CommandSubmissionReason::PayloadRejected &&
                live.session->pending_command_count() == 0U,
            "out-of-range stuck position passed package validation");
}

enum class FailureCase : std::uint8_t {
    Reducer,
    Event,
    Evaluation,
    Seal,
    Precommit,
};

void verify_failure_rollback_retry(
    const contracts::ExecutionPlanImage& image, FailureCase failure_case) {
    const auto suffix = std::to_string(static_cast<unsigned>(failure_case));
    auto live = initialize_session(image, "run.stuck-rollback." + suffix);
    require(static_cast<bool>(live.session->submit_command(
                stuck_command(live, "retry-stuck"))),
            "rollback test command was rejected");
    while (live.session->committed_tick() < kFaultTick) {
        require(live.session->execute_step().status ==
                    kernel::StepStatus::Committed,
                "rollback fixture did not reach the fault tick");
    }
    const auto epoch_before = live.session->committed_epoch();
    const auto actuator_before =
        read_committed_state<yyz::FaultStateFragment>(
            live, live.provider->actuator_state_block_handle);
    const auto rigid_before = read_committed_state<yyz::VerticalPitchRigidState>(
        live, live.provider->rigid_state_block_handle);

    kernel::RuntimeDiagnosticStage expected_stage =
        kernel::RuntimeDiagnosticStage::CommandReduction;
    switch (failure_case) {
    case FailureCase::Reducer:
        live.provider->control.reducer_failures_remaining = 1U;
        expected_stage = kernel::RuntimeDiagnosticStage::CommandReduction;
        break;
    case FailureCase::Event:
        live.provider->control.consumer_failures_remaining = 1U;
        expected_stage = kernel::RuntimeDiagnosticStage::EventConsumption;
        break;
    case FailureCase::Evaluation:
        live.provider->control.load_evaluation_failures_remaining = 1U;
        expected_stage = kernel::RuntimeDiagnosticStage::BoundaryInvocation;
        break;
    case FailureCase::Seal:
        live.provider->control.seal_failures_remaining = 1U;
        expected_stage = kernel::RuntimeDiagnosticStage::ObservationSeal;
        break;
    case FailureCase::Precommit:
        live.provider->control.fail_stuck_validation_at = 2U;
        live.provider->control.stuck_validation_count = 0U;
        expected_stage = kernel::RuntimeDiagnosticStage::Precommit;
        break;
    }

    const auto failed = live.session->execute_step();
    const auto actuator_after_failure =
        read_committed_state<yyz::FaultStateFragment>(
            live, live.provider->actuator_state_block_handle);
    const auto rigid_after_failure =
        read_committed_state<yyz::VerticalPitchRigidState>(
            live, live.provider->rigid_state_block_handle);
    const bool retryable =
        failure_case == FailureCase::Reducer ||
        failure_case == FailureCase::Event ||
        failure_case == FailureCase::Seal;
    require(failed.status == kernel::StepStatus::Failed &&
                failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->stage == expected_stage &&
                failed.primary_diagnostic->validity_effect ==
                    (retryable ? contracts::EvidenceValidity::Unknown
                               : contracts::EvidenceValidity::Invalid) &&
                failed.primary_diagnostic->disposition ==
                    (retryable
                         ? kernel::RuntimeFailureDisposition::RetryStep
                         : kernel::RuntimeFailureDisposition::FailOperation) &&
                live.session->state() ==
                    (retryable ? kernel::SessionState::Initialized
                               : kernel::SessionState::Failed) &&
                live.session->committed_epoch() == epoch_before &&
                live.session->committed_tick() == kFaultTick &&
                actuator_after_failure.mode == actuator_before.mode &&
                actuator_after_failure.revision ==
                    actuator_before.revision &&
                rigid_after_failure.revision == rigid_before.revision &&
                near(rigid_after_failure.altitude_meters,
                     rigid_before.altitude_meters) &&
                live.session->pending_command_count() == 1U &&
                live.session->command_application_receipts().empty() &&
                live.session->committed_events().empty(),
            "failed stuck transaction leaked state, receipt, event, or queue consumption");

    if (!retryable) {
        const auto* outcome = live.session->run_outcome();
        require(outcome != nullptr &&
                    outcome->final_status == kernel::RunFinalStatus::Failed &&
                    outcome->validity ==
                        contracts::EvidenceValidity::Invalid &&
                    outcome->final_tick == kFaultTick &&
                    live.session->execute_step().status ==
                        kernel::StepStatus::Failed,
                "fatal stuck failure did not freeze an Invalid failed run");
        return;
    }

    const auto retried = live.session->execute_step();
    const auto actuator_after_retry =
        read_committed_state<yyz::FaultStateFragment>(
            live, live.provider->actuator_state_block_handle);
    const auto rigid_after_retry =
        read_committed_state<yyz::VerticalPitchRigidState>(
            live, live.provider->rigid_state_block_handle);
    require(retried.status == kernel::StepStatus::Committed &&
                actuator_after_retry.mode ==
                    yyz::ActuatorFaultMode::Stuck &&
                actuator_after_retry.revision == 1U &&
                rigid_after_retry.revision == rigid_before.revision + 1U &&
                live.session->pending_command_count() == 0U &&
                live.session->command_application_receipts().size() == 1U &&
                live.session->committed_events().size() == 1U,
            "due stuck command did not commit exactly once on retry");
}

void verify_session_isolation(
    const contracts::ExecutionPlanImage& image) {
    auto first = initialize_session(image, "run.stuck-isolation.first");
    auto second = initialize_session(image, "run.stuck-isolation.second");
    require(static_cast<bool>(first.session->submit_command(
                stuck_command(first, "first-only", 0))),
            "isolation command was rejected");
    require(first.session->pending_command_count() == 1U &&
                second.session->pending_command_count() == 0U,
            "command queue crossed Session ownership");
    require(first.session->execute_step().status ==
                    kernel::StepStatus::Committed &&
                second.session->execute_step().status ==
                    kernel::StepStatus::Committed,
            "isolated Sessions did not execute their first tick");
    const auto first_state =
        read_committed_state<yyz::FaultStateFragment>(
            first, first.provider->actuator_state_block_handle);
    const auto second_state =
        read_committed_state<yyz::FaultStateFragment>(
            second, second.provider->actuator_state_block_handle);
    require(first_state.mode == yyz::ActuatorFaultMode::Stuck &&
                second_state.mode ==
                    yyz::ActuatorFaultMode::Healthy &&
                first.session->committed_events().size() == 1U &&
                second.session->committed_events().empty(),
            "actuator state or event crossed Session ownership");
}

void verify_determinism(
    const contracts::ExecutionPlanImage& image,
    const ScenarioResult& reference) {
    const auto repeated =
        run_scenario(image, true, "run.stuck-determinism.repeat");
    require(near(repeated.fault_tick_output.actual_position_radians,
                 reference.fault_tick_output.actual_position_radians) &&
                near(repeated.later_tick_output.actual_position_radians,
                     reference.later_tick_output.actual_position_radians) &&
                near(repeated.rigid.altitude_meters,
                     reference.rigid.altitude_meters) &&
                near(repeated.rigid.pitch_radians,
                     reference.rigid.pitch_radians) &&
                repeated.rigid.revision == reference.rigid.revision &&
                repeated.contact.impact == reference.contact.impact &&
                repeated.contact.reason_code == reference.contact.reason_code &&
                repeated.outcome.final_tick == reference.outcome.final_tick &&
                repeated.outcome.committed_step_count ==
                    reference.outcome.committed_step_count,
            "same stuck Image/input produced a different result");
}

void verify_checkpoint_fail_closed(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.stuck-checkpoint");
    const auto checkpoint = live.session->checkpoint();
    require(!checkpoint &&
                checkpoint.result.error ==
                    kernel::SessionError::UnsupportedCheckpointCapability &&
                live.session->state() == kernel::SessionState::Initialized &&
                live.session->committed_tick() == 0 &&
                live.session->committed_epoch() == 0U,
            "command-bearing stuck Image checkpoint did not fail closed");
}

void verify_terminal_decision_fail_closed(
    const contracts::ExecutionPlanImage& image) {
    {
        auto data = image.data();
        data.evaluator_histories.front().branch_decision_slot_handle = 0U;
        auto malformed = image_from(std::move(data));
        auto shared_image =
            std::make_shared<const contracts::ExecutionPlanImage>(malformed);
        auto provider = std::make_shared<FixtureProvider>(*shared_image);
        auto creation = kernel::create_session(shared_image, provider);
        require(static_cast<bool>(creation),
                "missing branch slot did not reach Image validation");
        const auto initialized = creation.session->initialize(
            {kernel::RunId("run.stuck.missing-branch-slot"),
             kernel::exact_run_binding(*shared_image)});
        require(!initialized &&
                    initialized.result.error ==
                        kernel::SessionError::InvalidImageStructure &&
                    creation.session->state() == kernel::SessionState::Failed,
                "missing periodic branch slot did not fail closed");
    }

    {
        auto data = image.data();
        const auto contact_slot = std::find_if(
            data.slots.begin(), data.slots.end(), [](const auto& slot) {
                return slot.contract_id ==
                       yyz::kContactImpactResultContractId;
            });
        require(contact_slot != data.slots.end(),
                "contact-result slot is absent");
        data.evaluator_histories.front().branch_decision_slot_handle =
            contact_slot->handle;
        auto malformed = image_from(std::move(data));
        auto live = initialize_session(
            malformed, "run.stuck.wrong-branch-slot-type");
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    failed.result.error ==
                        kernel::SessionError::ObjectTypeMismatch &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    failed.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::FailOperation &&
                    live.session->state() == kernel::SessionState::Failed,
                "wrong branch-slot type did not freeze an Invalid run");
    }

    {
        auto live = initialize_session(
            image, "run.stuck.invalid-branch-enum");
        live.provider->control.forced_branch_decision =
            contracts::TransactionBranch::Failure;
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    failed.result.error ==
                        kernel::SessionError::TransactionPrecommitFailed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    failed.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::FailOperation &&
                    live.session->state() == kernel::SessionState::Failed,
                "invalid branch enum did not freeze an Invalid run");
    }
}

void verify_terminal_boundary_is_model_driven(
    const CompiledFixture& reference) {
    const auto shifted = compile_fixture(-1.0);
    const auto result = run_scenario(
        shifted.image, true, "run.stuck.shifted-ground");
    require(result.contact.impact && result.contact.tick == 11 &&
                result.preterminal_altitude_meters > -1.0 &&
                result.contact.altitude_meters <= -1.0 &&
                result.terminal_step.tick_before == 11 &&
                shifted.image.source_semantic_hash() !=
                    reference.image.source_semantic_hash(),
            "terminal tick did not follow the compiled ground threshold");
}

void run_self_check() {
    const auto fixture = compile_fixture();
    verify_image_contract(fixture);
    ScenarioResult healthy;
    ScenarioResult stuck;
    verify_healthy_stuck_ab(fixture.image, healthy, stuck);
    verify_invalid_payload(fixture.image);
    verify_failure_rollback_retry(fixture.image, FailureCase::Reducer);
    verify_failure_rollback_retry(fixture.image, FailureCase::Event);
    verify_failure_rollback_retry(fixture.image, FailureCase::Evaluation);
    verify_failure_rollback_retry(fixture.image, FailureCase::Seal);
    verify_failure_rollback_retry(fixture.image, FailureCase::Precommit);
    verify_session_isolation(fixture.image);
    verify_determinism(fixture.image, stuck);
    verify_checkpoint_fail_closed(fixture.image);
    verify_terminal_decision_fail_closed(fixture.image);
    verify_terminal_boundary_is_model_driven(fixture);

    std::cout << "stuck-actuator A/B healthy_altitude="
              << healthy.rigid.altitude_meters
              << " stuck_altitude=" << stuck.rigid.altitude_meters
              << " healthy_terminal=" << healthy.contact.reason_code
              << " stuck_terminal=" << stuck.contact.reason_code
              << " receipt_event=" << stuck.application_receipts << "/"
              << stuck.events << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_stuck_actuator_probe --self-check\n";
        return 2;
    }
    try {
        run_self_check();
        std::cout << "kernel stuck-actuator self-check passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kernel stuck-actuator self-check failed: "
                  << error.what() << '\n';
        return 1;
    }
}
