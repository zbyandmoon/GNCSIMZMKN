#include "gnc/compiler/complete_execution_plan.hpp"
#include "gnc/kernel/session.hpp"
#include "gnc/model_sdk/static_implementation.hpp"
#include "support/session_qualification_access.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace command_allocation_fault {

thread_local std::int64_t fail_after = -1;

void arm(std::int64_t allocations_before_failure) noexcept {
    fail_after = allocations_before_failure;
}

void disarm() noexcept { fail_after = -1; }

[[nodiscard]] bool should_fail() noexcept {
    if (fail_after < 0) {
        return false;
    }
    if (fail_after == 0) {
        fail_after = -1;
        return true;
    }
    --fail_after;
    return false;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void release(void* address) noexcept {
    std::free(address);
}

} // namespace command_allocation_fault

void* operator new(std::size_t size) {
    if (command_allocation_fault::should_fail()) {
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(size == 0U ? 1U : size)) {
        return result;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* address) noexcept {
    command_allocation_fault::release(address);
}
void operator delete[](void* address) noexcept {
    command_allocation_fault::release(address);
}
void operator delete(void* address, std::size_t) noexcept {
    command_allocation_fault::release(address);
}
void operator delete[](void* address, std::size_t) noexcept {
    command_allocation_fault::release(address);
}

namespace {

namespace compiler = gnc::compiler;
namespace contracts = gnc::contracts;
namespace kernel = gnc::kernel;
namespace sdk = gnc::model_sdk;

constexpr std::string_view kPackageId =
    "gnc.qualification.mode-owner.package@1";
constexpr std::string_view kPackageVersion = "1.0.0";
constexpr std::string_view kBuildFingerprint =
    "build.qualification.mode-owner.release";
constexpr std::string_view kModelId =
    "gnc.qualification.mode-owner.model@1";
constexpr std::string_view kModelVersion = "1.0.0";
constexpr std::string_view kOccurrenceId = "mode-owner.fixture";
constexpr std::string_view kEntityId = "vehicle.fixture.mode-owner@1";
constexpr std::string_view kStateSchema =
    "gnc.qualification.mode-state.schema@1";
constexpr std::string_view kStateLayout =
    "gnc.qualification.mode-state.layout@1";
constexpr std::string_view kCommandSchema =
    "gnc.qualification.mode-command@1";
constexpr std::string_view kEventSchema =
    "gnc.qualification.mode-event@1";
constexpr std::string_view kConsumerResultSchema =
    "gnc.qualification.mode-event-consumed@1";
constexpr std::string_view kObservationContract =
    "gnc.qualification.mode-observation@1";
constexpr std::string_view kObservationLayout =
    "gnc.qualification.mode-observation.layout@1";
constexpr std::string_view kConfigurationSchema =
    "gnc.qualification.mode-owner.config@1";
constexpr std::string_view kInitialSchema =
    "gnc.qualification.mode-owner.initial@1";
constexpr std::uint32_t kDecisionAuthority = 41U;
constexpr std::uint32_t kQueueCapacity = 4U;

enum class Mode : std::uint8_t {
    Standby,
    Active,
};

enum class RequestedDecision : std::uint8_t {
    Apply,
    Reject,
    Defer,
};

struct ModeState {
    Mode mode = Mode::Standby;
    std::uint64_t revision = 0U;
};

struct ModeCommand {
    Mode target = Mode::Standby;
    RequestedDecision decision = RequestedDecision::Apply;
};

struct WrongCommand {
    std::uint32_t value = 0U;
};

struct ModeEvent {
    Mode from = Mode::Standby;
    Mode to = Mode::Standby;
    std::uint64_t revision = 0U;
};

struct ModeConsumed {
    Mode observed = Mode::Standby;
    std::uint64_t revision = 0U;
};

struct ModeObservation {
    Mode mode = Mode::Standby;
    std::uint64_t revision = 0U;
};

struct ModeCell {
    std::uint32_t marker = 0x4D4F4445U;
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] compiler::SourceRef source_ref(std::string path) {
    return {"fixture://qualification/mode-owner", std::move(path)};
}

using StubCall = void (*)() noexcept;

void definition_builder_stub() noexcept {}
void runtime_cell_factory_stub() noexcept {}
void state_codec_stub() noexcept {}
void initial_state_stub() noexcept {}
void projection_stub() noexcept {}
void reduction_stub() noexcept {}
void consumption_stub() noexcept {}
void observation_codec_stub() noexcept {}

[[nodiscard]] sdk::StaticSlotCodecDescriptor observation_codec() {
    return {std::string(kObservationLayout),
            "gnc.qualification.mode-observation.codec@1",
            "1.0.0",
            "gnc.call-shape.qualification.mode-observation-codec@1",
            "gnc.operation.qualification.mode-observation.copy@1",
            "gnc.operation.qualification.mode-observation.validate@1",
            "gnc.operation.qualification.mode-observation.project@1"};
}

[[nodiscard]] sdk::StaticModelDescriptor mode_owner_model(
    bool resettable = true) {
    sdk::StaticModelDescriptor model;
    model.definition = {std::string(kModelId), std::string(kModelVersion),
                        sdk::ModelExecutionForm::RuntimeComponent};
    model.placement = sdk::ModelPlacement::VehicleProcess;
    model.configuration =
        {std::string(kConfigurationSchema), 1U,
         {{"configuration-revision",
           sdk::CanonicalConfigValueKind::Integer}}};

    sdk::StaticPortDescriptor observation;
    observation.port_id = "mode-observation";
    observation.contract_id = std::string(kObservationContract);
    observation.direction = sdk::StaticPortDirection::Output;
    observation.binding_kind = sdk::BindingKind::SampledSignal;
    observation.cardinality = sdk::PortCardinality::OneOrMore;
    observation.temporal_relation =
        sdk::TemporalRelation::CurrentCycle;
    observation.slot_codec = observation_codec();
    model.ports.push_back(std::move(observation));

    sdk::StaticRuntimeComponentDescriptor runtime;
    runtime.recipe_id = "gnc.recipe.qualification.mode-owner@1";
    runtime.profile = sdk::RuntimeCellProfile::ModeOwner;
    runtime.obligations = {
        sdk::RuntimeExecutionObligation::PublishProjection,
        sdk::RuntimeExecutionObligation::CommandReduction,
        sdk::RuntimeExecutionObligation::EventConsumption};
    runtime.schedule = {sdk::StaticScheduleTrigger::EveryBoundary, 1U, 0U,
                        sdk::HoldPolicy::ZeroOrderHold, 0U};
    runtime.lifecycle_capabilities = {
        sdk::RuntimeLifecycleCapability::Instantiate};
    if (resettable) {
        runtime.lifecycle_capabilities.push_back(
            sdk::RuntimeLifecycleCapability::Resettable);
    }
    runtime.lifecycle_capabilities.push_back(
        sdk::RuntimeLifecycleCapability::Dispose);
    runtime.definition_builder_id =
        "gnc.qualification.mode-owner.definition-builder@1";
    runtime.definition_builder_version = "1.0.0";
    runtime.definition_builder_call_shape_id =
        "gnc.call-shape.qualification.mode-owner.definition-builder@1";
    runtime.runtime_cell_factory_id =
        "gnc.qualification.mode-owner.runtime-cell-factory@1";
    runtime.runtime_cell_factory_version = "1.0.0";
    runtime.runtime_cell_factory_call_shape_id =
        "gnc.call-shape.qualification.mode-owner.runtime-cell-factory@1";
    runtime.resource_plan_id = "gnc.resource-plan.none@1";
    runtime.resource_workspace_requirement =
        sdk::StaticWorkspaceRequirement::None;

    sdk::StaticStateOwnerDescriptor owner;
    owner.schema =
        {std::string(kStateSchema), 1U, std::string(kStateLayout),
         {{"mode", "uint8", "1", "vehicle-mode"},
          {"revision", "uint64", "1", "mode-revision"}}};
    owner.initial_state_builder_id =
        "gnc.qualification.mode-owner.initial-state@1";
    owner.initial_state_builder_version = "1.0.0";
    owner.initial_state_input_schema =
        {std::string(kInitialSchema), 1U,
         {{"initial-mode", sdk::CanonicalConfigValueKind::Enum}}};
    owner.evolution = sdk::StaticStateEvolution::InstantPatch;
    owner.initial_state_builder_call_shape_id =
        "gnc.call-shape.qualification.mode-owner.initial-state@1";
    owner.codec = {
        "gnc.qualification.mode-state.codec@1",
        "1.0.0",
        "gnc.call-shape.qualification.mode-state-codec@1",
        "gnc.operation.qualification.mode-state.clone@1",
        "gnc.operation.qualification.mode-state.validate@1",
        "gnc.operation.qualification.mode-state.finite@1",
        "gnc.operation.qualification.mode-state.invariant@1",
        "gnc.operation.qualification.mode-state.swap@1",
        "gnc.operation.qualification.mode-state.project@1"};
    runtime.state_owner = std::move(owner);

    sdk::StaticRuntimeObligationEntryDescriptor publish;
    publish.obligation =
        sdk::RuntimeExecutionObligation::PublishProjection;
    publish.phase = sdk::CoarsePhase::Publish;
    publish.entry_id = "gnc.qualification.mode-owner.projection@1";
    publish.entry_version = "1.0.0";
    publish.request_contract_id =
        "gnc.qualification.mode-projection-request@1";
    publish.result_contract_id = std::string(kObservationContract);
    publish.workspace_requirement = sdk::StaticWorkspaceRequirement::None;
    publish.output_port_ids = {"mode-observation"};
    publish.state_read = sdk::StaticStateReadKind::Committed;
    publish.state_write = sdk::StaticStateWriteKind::None;
    publish.call_shape_id =
        "gnc.call-shape.qualification.mode-owner.projection@1";

    sdk::StaticRuntimeObligationEntryDescriptor reduce;
    reduce.obligation = sdk::RuntimeExecutionObligation::CommandReduction;
    reduce.phase = sdk::CoarsePhase::Process;
    reduce.entry_id = "gnc.qualification.mode-owner.reduction@1";
    reduce.entry_version = "1.0.0";
    reduce.request_contract_id = std::string(kCommandSchema);
    reduce.result_contract_id = std::string(kEventSchema);
    reduce.workspace_requirement = sdk::StaticWorkspaceRequirement::None;
    reduce.state_read = sdk::StaticStateReadKind::Committed;
    reduce.state_write = sdk::StaticStateWriteKind::InstantPatch;
    reduce.call_shape_id =
        "gnc.call-shape.qualification.mode-owner.reduction@1";

    sdk::StaticRuntimeObligationEntryDescriptor consume;
    consume.obligation = sdk::RuntimeExecutionObligation::EventConsumption;
    consume.phase = sdk::CoarsePhase::Output;
    consume.entry_id = "gnc.qualification.mode-owner.consumption@1";
    consume.entry_version = "1.0.0";
    consume.request_contract_id = std::string(kEventSchema);
    consume.result_contract_id = std::string(kConsumerResultSchema);
    consume.workspace_requirement = sdk::StaticWorkspaceRequirement::None;
    consume.state_read = sdk::StaticStateReadKind::None;
    consume.state_write = sdk::StaticStateWriteKind::None;
    consume.call_shape_id =
        "gnc.call-shape.qualification.mode-owner.consumption@1";

    runtime.obligation_entries =
        {std::move(publish), std::move(reduce), std::move(consume)};
    model.runtime_component = std::move(runtime);
    return model;
}

[[nodiscard]] sdk::StaticPackageDescriptor mode_owner_package(
    bool resettable = true) {
    return {std::string(kPackageId), std::string(kPackageVersion),
            {mode_owner_model(resettable)}, {}};
}

template <auto Callable>
[[nodiscard]] sdk::StaticImplementationEntry implementation_entry(
    std::string entry_id, std::string entry_version,
    sdk::StaticEntryKind kind, std::string signature,
    std::string call_shape, std::string state_layout = {}) {
    return sdk::make_static_implementation_entry<Callable, StubCall>(
        std::move(entry_id), std::move(entry_version), kind,
        std::move(signature),
        sdk::make_static_callable_contract<StubCall>(
            std::move(call_shape)),
        std::move(state_layout));
}

[[nodiscard]] sdk::StaticPackageImplementation mode_owner_implementation(
    const sdk::StaticPackageDescriptor& package) {
    const auto& model = package.models.front();
    const auto& runtime = *model.runtime_component;
    const auto& owner = *runtime.state_owner;
    sdk::StaticPackageImplementation implementation;
    implementation.package_id = package.package_id;
    implementation.package_version = package.package_version;
    implementation.build_fingerprint = std::string(kBuildFingerprint);

    implementation.entries.push_back(
        implementation_entry<&definition_builder_stub>(
            runtime.definition_builder_id,
            runtime.definition_builder_version,
            sdk::StaticEntryKind::DefinitionBuilder,
            sdk::canonical_definition_builder_signature(model),
            runtime.definition_builder_call_shape_id));
    implementation.entries.push_back(
        implementation_entry<&runtime_cell_factory_stub>(
            runtime.runtime_cell_factory_id,
            runtime.runtime_cell_factory_version,
            sdk::StaticEntryKind::RuntimeCellFactory,
            sdk::canonical_runtime_cell_factory_signature(model),
            runtime.runtime_cell_factory_call_shape_id));

    auto state_codec_entry = implementation_entry<&state_codec_stub>(
        owner.codec.entry_id, owner.codec.entry_version,
        sdk::StaticEntryKind::StateCodec,
        sdk::canonical_state_codec_signature(model),
        owner.codec.call_shape_id, owner.schema.layout_id);
    state_codec_entry = sdk::with_static_state_codec_witness(
        std::move(state_codec_entry),
        {owner.schema.layout_id, owner.codec.clone_operation_id,
         owner.codec.validate_operation_id,
         owner.codec.finite_validation_operation_id,
         owner.codec.invariant_validation_operation_id,
         owner.codec.noexcept_swap_operation_id,
         owner.codec.project_operation_id, true});
    implementation.entries.push_back(std::move(state_codec_entry));

    implementation.entries.push_back(
        implementation_entry<&initial_state_stub>(
            owner.initial_state_builder_id,
            owner.initial_state_builder_version,
            sdk::StaticEntryKind::InitialState,
            sdk::canonical_initial_state_signature(model),
            owner.initial_state_builder_call_shape_id,
            owner.schema.layout_id));

    for (const auto& entry : runtime.obligation_entries) {
        sdk::StaticEntryKind kind = sdk::StaticEntryKind::PublishProjection;
        StubCall callable = &projection_stub;
        if (entry.obligation ==
            sdk::RuntimeExecutionObligation::CommandReduction) {
            kind = sdk::StaticEntryKind::CommandReduction;
            callable = &reduction_stub;
        } else if (entry.obligation ==
                   sdk::RuntimeExecutionObligation::EventConsumption) {
            kind = sdk::StaticEntryKind::EventConsumption;
            callable = &consumption_stub;
        }
        const auto state_layout =
            entry.state_read != sdk::StaticStateReadKind::None ||
                    entry.state_write != sdk::StaticStateWriteKind::None
                ? owner.schema.layout_id
                : std::string{};
        if (callable == &projection_stub) {
            implementation.entries.push_back(
                implementation_entry<&projection_stub>(
                    entry.entry_id, entry.entry_version, kind,
                    sdk::canonical_runtime_entry_signature(model, entry),
                    entry.call_shape_id, state_layout));
        } else if (callable == &reduction_stub) {
            implementation.entries.push_back(
                implementation_entry<&reduction_stub>(
                    entry.entry_id, entry.entry_version, kind,
                    sdk::canonical_runtime_entry_signature(model, entry),
                    entry.call_shape_id, state_layout));
        } else {
            implementation.entries.push_back(
                implementation_entry<&consumption_stub>(
                    entry.entry_id, entry.entry_version, kind,
                    sdk::canonical_runtime_entry_signature(model, entry),
                    entry.call_shape_id, state_layout));
        }
    }

    const auto& output = model.ports.front();
    const auto& codec = *output.slot_codec;
    auto slot_codec_entry = implementation_entry<&observation_codec_stub>(
        codec.entry_id, codec.entry_version,
        sdk::StaticEntryKind::SlotCodec,
        sdk::canonical_slot_codec_signature(model, output),
        codec.call_shape_id);
    slot_codec_entry = sdk::with_static_slot_codec_witness(
        std::move(slot_codec_entry),
        {output.contract_id, codec.layout_id, codec.copy_operation_id,
         codec.validate_operation_id, codec.project_operation_id});
    implementation.entries.push_back(std::move(slot_codec_entry));

    implementation.state_layouts.push_back(
        {std::string(kStateLayout), sizeof(ModeState), alignof(ModeState)});
    implementation.value_layouts.push_back(
        {std::string(kObservationContract), sizeof(ModeObservation),
         alignof(ModeObservation), std::string(kObservationLayout)});
    return implementation;
}

[[nodiscard]] compiler::CompleteStaticCompositionSource mode_owner_source(
    std::int64_t terminal_tick = 2) {
    compiler::CompleteStaticCompositionSource source;
    source.source_version =
        std::string(compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id = "mission.qualification.mode-owner@1";
    source.plan_id = "plan.qualification.mode-owner";
    source.mission_source = source_ref("/mission");
    source.clock = {"clock.qualification.mode-owner@1", 0.25, 0,
                    terminal_tick,
                    source_ref("/clock")};
    source.entities.push_back(
        {std::string(kEntityId),
         compiler::EntityLifecycle::ActiveAtInitialize,
         source_ref("/entities/vehicle/id"),
         source_ref("/entities/vehicle/lifecycle")});
    const compiler::ScopeKey scope{compiler::ScopeKind::Vehicle,
                                   std::string(kEntityId)};
    source.scopes.push_back({scope, source_ref("/scopes/vehicle")});

    compiler::CompleteSourceOccurrence occurrence;
    occurrence.occurrence_id = std::string(kOccurrenceId);
    occurrence.model_id = std::string(kModelId);
    occurrence.model_version = std::string(kModelVersion);
    occurrence.source = source_ref("/occurrences/mode-owner");
    occurrence.subject_entity_id = std::string(kEntityId);
    occurrence.subject_source =
        source_ref("/occurrences/mode-owner/subject");
    occurrence.scope = scope;
    occurrence.scope_source =
        source_ref("/occurrences/mode-owner/scope");
    occurrence.placement = sdk::ModelPlacement::VehicleProcess;
    occurrence.placement_source =
        source_ref("/occurrences/mode-owner/placement");
    occurrence.configuration =
        {std::string(kConfigurationSchema), 1U,
         {{"configuration-revision", std::int64_t{1}}}};
    occurrence.configuration_source =
        source_ref("/occurrences/mode-owner/configuration");
    occurrence.configuration_field_sources.push_back(
        {"configuration-revision",
         source_ref(
             "/occurrences/mode-owner/configuration/configuration-revision")});
    source.occurrences.push_back(std::move(occurrence));

    compiler::CompleteSourceInitialBinding initial;
    initial.owner_occurrence_id = std::string(kOccurrenceId);
    initial.builder_inputs =
        {std::string(kInitialSchema), 1U,
         {{"initial-mode", sdk::CanonicalEnumValue{"standby"}}}};
    initial.field_sources.push_back(
        {"initial-mode", source_ref("/initial/mode-owner/initial-mode")});
    initial.source = source_ref("/initial/mode-owner");
    source.initial_bindings.push_back(std::move(initial));

    source.transactions.push_back(
        {"transaction.qualification.mode-owner", scope,
         {std::string(kOccurrenceId)}, source_ref("/transactions/mode-owner"),
         {}});
    source.package_build_locks.push_back(
        {std::string(kPackageId), std::string(kPackageVersion),
         std::string(kBuildFingerprint), source_ref("/packages/mode-owner")});
    return source;
}

[[nodiscard]] compiler::CommandRouteSpec mode_owner_route() {
    compiler::CommandRouteSpec route;
    route.route_id = "mode-control";
    route.target_occurrence_id = std::string(kOccurrenceId);
    route.payload_schema_id = std::string(kCommandSchema);
    route.decision_authority = kDecisionAuthority;
    route.queue_capacity = kQueueCapacity;
    route.queue_policy = contracts::CommandQueuePolicy::RejectNewest;
    route.supersession_policy =
        contracts::CommandSupersessionPolicy::LatestDuePerKey;
    route.effective_point =
        contracts::CommandEffectivePoint::TransactionStart;
    route.cutoff_policy =
        contracts::CommandCutoffPolicy::LedgerSequenceAtTransactionStart;
    route.event_schema_id = std::string(kEventSchema);
    route.event_consumer_occurrence_id = std::string(kOccurrenceId);
    route.source = source_ref("/command-routes/mode-control");
    return route;
}

struct CompiledFixture {
    sdk::StaticPackageDescriptor package;
    sdk::StaticPackageImplementation implementation;
    compiler::CompleteStaticCompositionSource source;
    compiler::CompleteStaticCompilation base;
    compiler::CompleteExecutionPlanDescriptor extended_plan;
    compiler::PlanProofIndex extended_proofs;
    contracts::ExecutionPlanImage image;
};

[[nodiscard]] CompiledFixture compile_fixture(
    bool resettable = true, std::int64_t terminal_tick = 2) {
    auto package = mode_owner_package(resettable);
    auto implementation = mode_owner_implementation(package);
    auto source = mode_owner_source(terminal_tick);
    const auto base_outcome =
        compiler::compile_complete_execution_plan(source, {package});
    require(base_outcome.succeeded(),
            "ModeOwner base plan compilation failed");
    auto base = *base_outcome.value;
    const auto route_outcome = compiler::compile_command_event_routes(
        base, {mode_owner_route()});
    require(route_outcome.succeeded(),
            "ModeOwner command/event route lowering failed");
    auto extended = *route_outcome.value;
    const auto image_outcome = compiler::link_complete_execution_plan(
        extended.plan, extended.proofs, {implementation});
    require(image_outcome.succeeded(), "ModeOwner Image link failed");
    return {std::move(package), std::move(implementation),
            std::move(source), std::move(base),
            std::move(extended.plan), std::move(extended.proofs),
            *image_outcome.value};
}

struct RuntimeControl {
    kernel::Session* session = nullptr;
    kernel::RunId run_id;
    std::size_t reducer_failures_remaining = 0U;
    std::size_t consumer_failures_remaining = 0U;
    std::size_t invalid_candidates_remaining = 0U;
    std::size_t invalid_decisions_remaining = 0U;
    std::size_t omitted_projections_remaining = 0U;
    std::size_t observation_seal_clone_failures_remaining = 0U;
    std::size_t cancel_in_reducer_remaining = 0U;
    std::size_t cancel_on_state_swap_remaining = 0U;
    std::uint64_t cancellation_sequence = 0U;
    std::optional<kernel::CommandRequest> command_after_cutoff;
    std::optional<kernel::CommandSubmissionOutcome>
        command_after_cutoff_outcome;
};

template <typename Value>
class TypedOperations final : public kernel::InProcessObjectOperations {
  public:
    TypedOperations(std::string layout_id, std::uint32_t codec_handle,
                    RuntimeControl* control = nullptr) noexcept
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
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        if constexpr (std::is_same_v<Value, ModeObservation>) {
            ++copy_count_;
            if (copy_count_ % 2U == 0U && control_ != nullptr &&
                control_->observation_seal_clone_failures_remaining != 0U) {
                --control_->observation_seal_clone_failures_remaining;
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
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        try {
            *static_cast<Value*>(destination) =
                *static_cast<const Value*>(source);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool validate(const void* object) const noexcept override {
        if (object == nullptr) {
            return false;
        }
        if constexpr (std::is_same_v<Value, ModeState>) {
            const auto& state = *static_cast<const ModeState*>(object);
            return state.mode == Mode::Standby || state.mode == Mode::Active;
        } else {
            return true;
        }
    }

    [[nodiscard]] bool supports_nofail_swap() const noexcept override {
        return std::is_same_v<Value, ModeState>;
    }

    void nofail_swap(void* lhs, void* rhs) const noexcept override {
        if constexpr (std::is_same_v<Value, ModeState>) {
            using std::swap;
            swap(*static_cast<Value*>(lhs), *static_cast<Value*>(rhs));
            if (control_ != nullptr &&
                control_->cancel_on_state_swap_remaining != 0U &&
                control_->session != nullptr) {
                --control_->cancel_on_state_swap_remaining;
                ++control_->cancellation_sequence;
                static_cast<void>(control_->session->request_cancel(
                    {kernel::CancellationRequestId(
                         "cancel-on-model-commit-" +
                         std::to_string(control_->cancellation_sequence)),
                     control_->run_id}));
            }
        }
    }

    void destroy(void* object) const noexcept override {
        if (object != nullptr) {
            static_cast<Value*>(object)->~Value();
        }
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
        if (destination == nullptr) {
            return false;
        }
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

class ProjectionEntry final : public kernel::SessionInvocationEntry {
  public:
    ProjectionEntry(kernel::SessionInvocationIdentity identity,
                    std::uint32_t state_block_handle,
                    std::uint32_t output_slot_handle,
                    std::uint32_t writer_token_handle,
                    RuntimeControl& control) noexcept
        : identity_(identity), state_block_handle_(state_block_handle),
          output_slot_handle_(output_slot_handle),
          writer_token_handle_(writer_token_handle), control_(&control) {}

    [[nodiscard]] kernel::SessionInvocationIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult invoke(
        const kernel::SessionInvocationContext& context)
        const noexcept override {
        kernel::SessionObjectIdentityView view;
        auto result = context.committed().read(state_block_handle_, view);
        if (!result || view.type_identity != &typeid(ModeState)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    state_block_handle_,
                    "ModeOwner projection committed-state type mismatch"};
        }
        if (control_->omitted_projections_remaining != 0U) {
            --control_->omitted_projections_remaining;
            return {};
        }
        const auto& state = *static_cast<const ModeState*>(view.address);
        const ModeObservation observation{state.mode, state.revision};
        result = context.outputs().write(
            output_slot_handle_, writer_token_handle_,
            {&observation, sizeof(observation), alignof(ModeObservation),
             &typeid(ModeObservation)});
        if (!result) {
            return result;
        }
        if (control_->command_after_cutoff.has_value() &&
            control_->session != nullptr) {
            control_->command_after_cutoff_outcome.emplace(
                control_->session->submit_command(
                    std::move(*control_->command_after_cutoff)));
            control_->command_after_cutoff.reset();
        }
        return {};
    }

  private:
    kernel::SessionInvocationIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t output_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    RuntimeControl* control_ = nullptr;
};

class ReductionEntry final : public kernel::SessionCommandReducerEntry {
  public:
    ReductionEntry(kernel::SessionCommandReducerIdentity identity,
                   std::uint32_t state_block_handle,
                   std::uint32_t candidate_slot_handle,
                   std::uint32_t writer_token_handle,
                   RuntimeControl& control) noexcept
        : identity_(identity), state_block_handle_(state_block_handle),
          candidate_slot_handle_(candidate_slot_handle),
          writer_token_handle_(writer_token_handle), control_(&control) {}

    [[nodiscard]] kernel::SessionCommandReducerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult reduce(
        const kernel::SessionCommandReductionContext& context,
        kernel::SessionCommandReductionResult& result)
        const noexcept override {
        if (control_->reducer_failures_remaining != 0U) {
            --control_->reducer_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "ModeOwner reducer injected failure"};
        }
        if (context.payload().type_identity != &typeid(ModeCommand)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.route_handle,
                    "ModeOwner reducer payload type mismatch"};
        }
        const auto& command =
            *static_cast<const ModeCommand*>(context.payload().object);
        if (context.decision_authority() != kDecisionAuthority) {
            return {kernel::SessionError::InvocationFailed,
                    identity_.route_handle,
                    "ModeOwner reducer authority mismatch"};
        }
        kernel::SessionObjectIdentityView committed;
        auto status = context.committed().read(state_block_handle_, committed);
        if (!status || committed.type_identity != &typeid(ModeState)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    state_block_handle_,
                    "ModeOwner reducer committed-state type mismatch"};
        }
        const auto& prior = *static_cast<const ModeState*>(committed.address);
        if (control_->invalid_decisions_remaining != 0U) {
            --control_->invalid_decisions_remaining;
            result.decision =
                static_cast<kernel::CommandApplicationDecision>(255U);
            return {};
        }
        if (command.decision == RequestedDecision::Reject) {
            result.decision = kernel::CommandApplicationDecision::Rejected;
            result.application_code = 200U;
            return {};
        }
        if (command.decision == RequestedDecision::Defer) {
            result.decision = kernel::CommandApplicationDecision::Deferred;
            result.application_code = 300U;
            return {};
        }
        if ((command.target != Mode::Standby &&
             command.target != Mode::Active) ||
            command.target == prior.mode) {
            result.decision = kernel::CommandApplicationDecision::Rejected;
            result.application_code = 201U;
            return {};
        }
        ModeState candidate{command.target, prior.revision + 1U};
        if (control_->invalid_candidates_remaining != 0U) {
            --control_->invalid_candidates_remaining;
            candidate.mode = static_cast<Mode>(255U);
        }
        status = context.candidates().write(
            candidate_slot_handle_, writer_token_handle_,
            {&candidate, sizeof(candidate), alignof(ModeState),
             &typeid(ModeState)});
        if (!status) {
            return status;
        }
        result.decision = kernel::CommandApplicationDecision::Applied;
        result.application_code = 100U;
        result.event_payload = kernel::InProcessOwnedValue::make(
            ModeEvent{prior.mode, command.target, candidate.revision});

        if (control_->cancel_in_reducer_remaining != 0U &&
            control_->session != nullptr) {
            --control_->cancel_in_reducer_remaining;
            ++control_->cancellation_sequence;
            static_cast<void>(control_->session->request_cancel(
                {kernel::CancellationRequestId(
                     "cancel-after-reducer-" +
                     std::to_string(control_->cancellation_sequence)),
                 control_->run_id}));
        }
        return {};
    }

  private:
    kernel::SessionCommandReducerIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t candidate_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    RuntimeControl* control_ = nullptr;
};

class ConsumptionEntry final : public kernel::SessionEventConsumerEntry {
  public:
    ConsumptionEntry(kernel::SessionEventConsumerIdentity identity,
                     RuntimeControl& control) noexcept
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
                    "ModeOwner event consumer injected failure"};
        }
        if (context.payload().type_identity != &typeid(ModeEvent)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.delivery_handle,
                    "ModeOwner event payload type mismatch"};
        }
        const auto& event =
            *static_cast<const ModeEvent*>(context.payload().object);
        output = kernel::InProcessOwnedValue::make(
            ModeConsumed{event.to, event.revision});
        return {};
    }

  private:
    kernel::SessionEventConsumerIdentity identity_;
    RuntimeControl* control_ = nullptr;
};

class ModeOwnerProvider final : public kernel::SessionMaterializationProvider {
  public:
    explicit ModeOwnerProvider(const contracts::ExecutionPlanImage& image) {
        require(image.runtime_components().size() == 1U &&
                    image.state_blocks().size() == 1U &&
                    image.initial_bindings().size() == 1U &&
                    !image.command_routes().empty() &&
                    !image.event_deliveries().empty(),
                "ModeOwner Image cardinality differs from fixture contract");
        const auto& component = image.runtime_components().front();
        const auto& state = image.state_blocks().front();
        const auto& initial = image.initial_bindings().front();
        const auto& route = image.command_routes().front();
        const auto& delivery = image.event_deliveries().front();
        const auto* projection = find_callsite(
            image, "PublishProjection");
        const auto* reduction = find_callsite(image, "CommandReduction");
        const auto* consumption = find_callsite(image, "EventConsumption");
        require(projection != nullptr && reduction != nullptr &&
                    consumption != nullptr &&
                    projection->output_slot_handles.size() == 1U &&
                    projection->output_writer_token_handles.size() == 1U &&
                    reduction->output_writer_token_handles.size() == 1U,
                "ModeOwner callsite shape differs from fixture contract");
        const auto* output_slot = find_slot(
            image, projection->output_slot_handles.front());
        require(output_slot != nullptr,
                "ModeOwner projection output slot is absent");

        runtime_ = std::make_unique<TypedMaterializer<ModeCell>>(
            kernel::SessionMaterializerIdentity{
                component.handle, kernel::SessionObjectRole::RuntimeCell,
                component.runtime_cell_factory_entry_handle, 0U},
            "gnc.qualification.mode-cell.layout@1", ModeCell{}, &control);
        initial_ = std::make_unique<TypedMaterializer<ModeState>>(
            kernel::SessionMaterializerIdentity{
                initial.handle,
                kernel::SessionObjectRole::InitialStateValue,
                initial.builder_entry_handle, state.codec_entry_handle},
            state.layout_id, ModeState{}, &control);
        output_ = std::make_unique<TypedMaterializer<ModeObservation>>(
            kernel::SessionMaterializerIdentity{
                output_slot->handle,
                kernel::SessionObjectRole::CycleFrameValue, 0U,
                output_slot->codec_entry_handle},
            output_slot->layout_id, ModeObservation{}, &control);
        projection_ = std::make_unique<ProjectionEntry>(
            kernel::SessionInvocationIdentity{
                projection->handle, component.handle,
                projection->entry_handle},
            state.handle, projection->output_slot_handles.front(),
            projection->output_writer_token_handles.front(), control);
        reduction_ = std::make_unique<ReductionEntry>(
            kernel::SessionCommandReducerIdentity{
                route.handle, reduction->handle, component.handle,
                reduction->entry_handle, &typeid(ModeCommand),
                &typeid(ModeEvent)},
            state.handle, state.candidate_slot_handle,
            reduction->output_writer_token_handles.front(), control);
        consumption_ = std::make_unique<ConsumptionEntry>(
            kernel::SessionEventConsumerIdentity{
                delivery.handle, consumption->handle, component.handle,
                consumption->entry_handle, &typeid(ModeEvent),
                &typeid(ModeConsumed)},
            control);
        component_handle_ = component.handle;
        state_block_handle_ = state.handle;
        route_handle_ = route.handle;
        delivery_handle_ = delivery.handle;
        output_slot_handle_ = output_slot->handle;
    }

    [[nodiscard]] const kernel::SessionObjectMaterializer* preparation(
        std::uint32_t) const noexcept override {
        return nullptr;
    }

    [[nodiscard]] const kernel::SessionObjectMaterializer* runtime_component(
        std::uint32_t handle) const noexcept override {
        return runtime_ != nullptr && runtime_->identity().image_object_handle ==
                                          handle
                   ? runtime_.get()
                   : nullptr;
    }

    [[nodiscard]] const kernel::SessionObjectMaterializer* slot(
        std::uint32_t handle) const noexcept override {
        return output_ != nullptr && output_->identity().image_object_handle ==
                                         handle
                   ? output_.get()
                   : nullptr;
    }

    [[nodiscard]] const kernel::SessionObjectMaterializer* initial_state(
        std::uint32_t handle) const noexcept override {
        return initial_ != nullptr && initial_->identity().image_object_handle ==
                                          handle
                   ? initial_.get()
                   : nullptr;
    }

    [[nodiscard]] const kernel::SessionInvocationEntry* invocation(
        std::uint32_t handle) const noexcept override {
        return projection_ != nullptr &&
                       projection_->identity().callsite_handle == handle
                   ? projection_.get()
                   : nullptr;
    }

    [[nodiscard]] const kernel::SessionCommandReducerEntry* command_reducer(
        std::uint32_t handle) const noexcept override {
        return reduction_ != nullptr &&
                       reduction_->identity().route_handle == handle
                   ? reduction_.get()
                   : nullptr;
    }

    [[nodiscard]] const kernel::SessionEventConsumerEntry* event_consumer(
        std::uint32_t handle) const noexcept override {
        return consumption_ != nullptr &&
                       consumption_->identity().delivery_handle == handle
                   ? consumption_.get()
                   : nullptr;
    }

    RuntimeControl control;
    std::uint32_t component_handle_ = 0U;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t route_handle_ = 0U;
    std::uint32_t delivery_handle_ = 0U;
    std::uint32_t output_slot_handle_ = 0U;

  private:
    [[nodiscard]] static const contracts::PlanImageCallsite* find_callsite(
        const contracts::ExecutionPlanImage& image,
        std::string_view obligation) noexcept {
        const auto found = std::find_if(
            image.callsites().begin(), image.callsites().end(),
            [obligation](const auto& callsite) {
                return callsite.obligation == obligation;
            });
        return found == image.callsites().end() ? nullptr : &*found;
    }

    [[nodiscard]] static const contracts::PlanImageSlot* find_slot(
        const contracts::ExecutionPlanImage& image,
        std::uint32_t handle) noexcept {
        const auto found = std::find_if(
            image.slots().begin(), image.slots().end(),
            [handle](const auto& slot) { return slot.handle == handle; });
        return found == image.slots().end() ? nullptr : &*found;
    }

    std::unique_ptr<TypedMaterializer<ModeCell>> runtime_;
    std::unique_ptr<TypedMaterializer<ModeState>> initial_;
    std::unique_ptr<TypedMaterializer<ModeObservation>> output_;
    std::unique_ptr<ProjectionEntry> projection_;
    std::unique_ptr<ReductionEntry> reduction_;
    std::unique_ptr<ConsumptionEntry> consumption_;
};

struct LiveSession {
    std::shared_ptr<const contracts::ExecutionPlanImage> image;
    std::shared_ptr<ModeOwnerProvider> provider;
    std::unique_ptr<kernel::Session> session;
    kernel::RunId run_id;
};

[[nodiscard]] LiveSession initialize_session(
    const contracts::ExecutionPlanImage& image,
    std::string run_id_text) {
    auto shared_image =
        std::make_shared<const contracts::ExecutionPlanImage>(image);
    auto provider = std::make_shared<ModeOwnerProvider>(*shared_image);
    auto creation = kernel::create_session(shared_image, provider);
    require(static_cast<bool>(creation), "ModeOwner Session creation failed");
    auto session = std::move(creation.session);
    kernel::RunId run_id(std::move(run_id_text));
    provider->control.session = session.get();
    provider->control.run_id = run_id;
    const auto initialized = session->initialize(
        {run_id, kernel::exact_run_binding(*shared_image)});
    require(static_cast<bool>(initialized),
            "ModeOwner Session initialization failed");
    return {std::move(shared_image), std::move(provider),
            std::move(session), std::move(run_id)};
}

[[nodiscard]] kernel::CommandRequest command_request(
    const LiveSession& live, std::string id, Mode target,
    RequestedDecision decision = RequestedDecision::Apply,
    std::int64_t effective_tick = 0,
    std::optional<std::int64_t> expiry_tick = std::nullopt,
    std::string supersession_key = "mode") {
    const auto& route = live.image->command_routes().front();
    return {kernel::CommandId(std::move(id)), live.run_id, route.handle,
            route.target_runtime_component_handle, route.payload_schema_id,
            route.decision_authority, effective_tick, expiry_tick,
            std::move(supersession_key),
            kernel::InProcessOwnedValue::make(
                ModeCommand{target, decision})};
}

[[nodiscard]] ModeState committed_mode(const LiveSession& live) {
    kernel::SessionObjectIdentityView view;
    const auto result =
        kernel::qualification::SessionAccess::read_committed(
            *live.session, live.provider->state_block_handle_, view);
    require(static_cast<bool>(result) &&
                view.type_identity == &typeid(ModeState),
            "ModeOwner committed state is unavailable");
    return *static_cast<const ModeState*>(view.address);
}

[[nodiscard]] bool has_complete_diagnostic(
    const std::vector<compiler::CompleteDiagnostic>& diagnostics,
    compiler::CompleteDiagnosticCode code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto& diagnostic) {
                           return diagnostic.code == code;
                       });
}

void verify_compiler_and_image_contracts(const CompiledFixture& fixture) {
    require(fixture.base.plan.command_routes.empty() &&
                fixture.base.plan.event_deliveries.empty(),
            "base compiler path acquired command/event runtime facts");
    require(fixture.extended_plan.command_routes.size() == 1U &&
                fixture.extended_plan.event_deliveries.size() == 1U &&
                fixture.image.command_routes().size() == 1U &&
                fixture.image.event_deliveries().size() == 1U,
            "command/event lowering did not preserve singular route shape");
    require(fixture.extended_plan.descriptor_semantic_hash !=
                fixture.base.plan.descriptor_semantic_hash,
            "command/event runtime facts were absent from the plan hash");
    const auto& route = fixture.image.command_routes().front();
    const auto& delivery = fixture.image.event_deliveries().front();
    require(route.handle != 0U && route.transaction_handle != 0U &&
                route.target_runtime_component_handle != 0U &&
                route.target_state_block_handle != 0U &&
                route.reducer_callsite_handle != 0U &&
                route.event_delivery_handle == delivery.handle &&
                delivery.producer_command_route_handle == route.handle &&
                delivery.producer_callsite_handle ==
                    route.reducer_callsite_handle &&
                delivery.consumer_callsite_handle != 0U,
            "Image command/event references were not lowered to exact handles");

    const auto expect_route_rejection = [&](compiler::CommandRouteSpec spec,
                                            auto code,
                                            std::string_view message) {
        const auto outcome = compiler::compile_command_event_routes(
            fixture.base.plan, {std::move(spec)});
        require(!outcome.succeeded() &&
                    has_complete_diagnostic(outcome.diagnostics, code),
                message);
    };
    auto mutation = mode_owner_route();
    mutation.target_occurrence_id = "missing-owner";
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidCommandRoute,
        "unknown command target passed route lowering");
    mutation = mode_owner_route();
    mutation.payload_schema_id = "wrong.command.schema@1";
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidCommandRoute,
        "command schema mismatch passed route lowering");
    mutation = mode_owner_route();
    mutation.decision_authority = 0U;
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidCommandRoute,
        "zero decision authority passed route lowering");
    mutation = mode_owner_route();
    mutation.queue_capacity = 0U;
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidCommandRoute,
        "zero queue capacity passed route lowering");
    mutation = mode_owner_route();
    mutation.event_schema_id = "wrong.event.schema@1";
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidEventDelivery,
        "event schema mismatch passed route lowering");
    mutation = mode_owner_route();
    mutation.event_consumer_occurrence_id = "missing-consumer";
    expect_route_rejection(
        mutation, compiler::CompleteDiagnosticCode::InvalidEventDelivery,
        "unknown event consumer passed route lowering");

    const auto duplicate = compiler::compile_command_event_routes(
        fixture.base.plan, {mode_owner_route(), mode_owner_route()});
    require(!duplicate.succeeded() &&
                has_complete_diagnostic(
                    duplicate.diagnostics,
                    compiler::CompleteDiagnosticCode::InvalidCommandRoute),
            "duplicate command route identity passed route lowering");

    auto duplicate_target = mode_owner_route();
    duplicate_target.route_id = "mode-control-duplicate-target";
    duplicate_target.source =
        source_ref("/command-routes/mode-control-duplicate-target");
    const auto duplicated_target = compiler::compile_command_event_routes(
        fixture.base.plan, {mode_owner_route(), duplicate_target});
    require(!duplicated_target.succeeded() &&
                has_complete_diagnostic(
                    duplicated_target.diagnostics,
                    compiler::CompleteDiagnosticCode::InvalidCommandRoute),
            "multiple command reducers for one target owner passed route lowering");

    {
        auto unknown_transaction = fixture.extended_plan;
        unknown_transaction.command_routes.front().transaction_id =
            "transaction.missing";
        std::vector<compiler::CompleteDiagnostic> diagnostics;
        require(!compiler::complete_plan_detail::
                     validate_command_event_routes(unknown_transaction,
                                                   diagnostics) &&
                    has_complete_diagnostic(
                        diagnostics,
                        compiler::CompleteDiagnosticCode::
                            InvalidCommandRoute),
                "unknown command transaction passed descriptor validation");
    }

    {
        auto cross_transaction = fixture.extended_plan;
        const auto consumer = std::find_if(
            cross_transaction.runtime_callsites.begin(),
            cross_transaction.runtime_callsites.end(),
            [](const auto& callsite) {
                return callsite.obligation ==
                       contracts::ExecutionObligation::EventConsumption;
            });
        require(consumer != cross_transaction.runtime_callsites.end(),
                "qualification plan lacks an event consumer callsite");
        auto foreign_consumer = *consumer;
        foreign_consumer.callsite_id += ".foreign";
        foreign_consumer.plan_element_id += ".foreign";
        foreign_consumer.occurrence_id += ".foreign";
        cross_transaction.runtime_callsites.push_back(foreign_consumer);

        auto foreign_component =
            cross_transaction.runtime_components.front();
        foreign_component.plan_element_id += ".foreign";
        foreign_component.occurrence_id = foreign_consumer.occurrence_id;
        foreign_component.callsite_ids = {foreign_consumer.callsite_id};
        auto foreign_transaction = cross_transaction.transactions.front();
        foreign_transaction.plan_element_id += ".foreign";
        foreign_transaction.transaction_id += ".foreign";
        foreign_component.transaction_ids = {
            foreign_transaction.transaction_id};
        cross_transaction.runtime_components.push_back(
            std::move(foreign_component));
        cross_transaction.transactions.push_back(
            std::move(foreign_transaction));
        cross_transaction.event_deliveries.front().consumer_callsite_id =
            foreign_consumer.callsite_id;

        std::vector<compiler::CompleteDiagnostic> diagnostics;
        require(!compiler::complete_plan_detail::
                     validate_command_event_routes(cross_transaction,
                                                   diagnostics) &&
                    has_complete_diagnostic(
                        diagnostics,
                        compiler::CompleteDiagnosticCode::
                            InvalidEventDelivery),
                "cross-transaction event consumer passed descriptor validation");
    }

    auto implementation = fixture.implementation;
    const auto reducer = std::find_if(
        implementation.entries.begin(), implementation.entries.end(),
        [](const auto& entry) {
            return entry.kind == sdk::StaticEntryKind::CommandReduction;
        });
    require(reducer != implementation.entries.end(),
            "qualification implementation lacks reducer entry");
    reducer->call_shape_id += ".tampered";
    const auto mismatched_link = compiler::link_complete_execution_plan(
        fixture.extended_plan, fixture.extended_proofs, {implementation});
    require(!mismatched_link.succeeded() &&
                has_complete_diagnostic(
                    mismatched_link.diagnostics,
                    compiler::CompleteDiagnosticCode::ImplementationMismatch),
            "reducer implementation mismatch passed Image link");

    const auto expect_fingerprint_change =
        [&](auto mutate, std::string_view message) {
            auto changed = fixture.image.data();
            mutate(changed);
            require(compiler::complete_plan_detail::image_fingerprint(
                        changed) != fixture.image.fingerprint(),
                    message);
        };
    expect_fingerprint_change(
        [](auto& image) {
            ++image.command_routes.front().decision_authority;
        },
        "command authority was absent from the Image fingerprint");
    expect_fingerprint_change(
        [](auto& image) {
            --image.command_routes.front().queue_capacity;
        },
        "command capacity was absent from the Image fingerprint");
    expect_fingerprint_change(
        [](auto& image) {
            image.command_routes.front().cutoff_policy =
                static_cast<contracts::CommandCutoffPolicy>(255U);
        },
        "command cutoff policy was absent from the Image fingerprint");
    expect_fingerprint_change(
        [](auto& image) {
            ++image.event_deliveries.front().stable_order;
        },
        "event delivery order was absent from the Image fingerprint");

    auto tampered = fixture.image.data();
    tampered.command_routes.front().payload_schema_id =
        "tampered.command.schema@1";
    tampered.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(tampered);
    const auto tampered_image =
        contracts::ExecutionPlanImage::freeze(std::move(tampered));
    auto shared_image =
        std::make_shared<const contracts::ExecutionPlanImage>(tampered_image);
    auto provider = std::make_shared<ModeOwnerProvider>(*shared_image);
    auto creation = kernel::create_session(shared_image, provider);
    require(static_cast<bool>(creation),
            "tampered Image fixture failed before validation boundary");
    const auto initialized = creation.session->initialize(
        {kernel::RunId("run.tampered"),
         kernel::exact_run_binding(*shared_image)});
    require(!initialized &&
                initialized.result.error ==
                    kernel::SessionError::InvalidImageStructure,
            "tampered numeric command route entered an initialized Session");

    auto wrong_order = fixture.image.data();
    ++wrong_order.event_deliveries.front().stable_order;
    wrong_order.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(wrong_order);
    const auto wrong_order_image =
        contracts::ExecutionPlanImage::freeze(std::move(wrong_order));
    auto wrong_order_shared =
        std::make_shared<const contracts::ExecutionPlanImage>(
            wrong_order_image);
    auto wrong_order_provider =
        std::make_shared<ModeOwnerProvider>(*wrong_order_shared);
    auto wrong_order_creation =
        kernel::create_session(wrong_order_shared, wrong_order_provider);
    require(static_cast<bool>(wrong_order_creation),
            "wrong-order Image fixture failed before validation boundary");
    const auto wrong_order_initialized =
        wrong_order_creation.session->initialize(
            {kernel::RunId("run.wrong-event-order"),
             kernel::exact_run_binding(*wrong_order_shared)});
    require(!wrong_order_initialized &&
                wrong_order_initialized.result.error ==
                    kernel::SessionError::InvalidImageStructure,
            "wrong event delivery order entered an initialized Session");

    auto unknown_transaction = fixture.image.data();
    unknown_transaction.command_routes.front().transaction_handle +=
        100000U;
    unknown_transaction.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(
            unknown_transaction);
    auto unknown_transaction_image =
        std::make_shared<const contracts::ExecutionPlanImage>(
            contracts::ExecutionPlanImage::freeze(
                std::move(unknown_transaction)));
    auto unknown_transaction_provider =
        std::make_shared<ModeOwnerProvider>(*unknown_transaction_image);
    auto unknown_transaction_creation = kernel::create_session(
        unknown_transaction_image, unknown_transaction_provider);
    require(unknown_transaction_creation &&
                !unknown_transaction_creation.session->initialize(
                    {kernel::RunId("run.unknown-transaction"),
                     kernel::exact_run_binding(
                         *unknown_transaction_image)}) &&
                unknown_transaction_creation.session->last_result().error ==
                    kernel::SessionError::InvalidImageStructure,
            "unknown numeric command transaction entered an initialized Session");

    auto second_route = fixture.image.data();
    auto extra_route = second_route.command_routes.front();
    auto extra_delivery = second_route.event_deliveries.front();
    extra_route.handle += 100000U;
    extra_route.plan_element_id += ".second";
    extra_delivery.handle += 100000U;
    extra_delivery.plan_element_id += ".second";
    extra_delivery.stable_order = 1U;
    extra_route.event_delivery_handle = extra_delivery.handle;
    extra_delivery.producer_command_route_handle = extra_route.handle;
    second_route.command_routes.push_back(std::move(extra_route));
    second_route.event_deliveries.push_back(std::move(extra_delivery));
    second_route.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(second_route);
    auto second_route_image =
        std::make_shared<const contracts::ExecutionPlanImage>(
            contracts::ExecutionPlanImage::freeze(
                std::move(second_route)));
    auto second_route_provider =
        std::make_shared<ModeOwnerProvider>(*second_route_image);
    auto second_route_creation = kernel::create_session(
        second_route_image, second_route_provider);
    require(second_route_creation &&
                !second_route_creation.session->initialize(
                    {kernel::RunId("run.second-route"),
                     kernel::exact_run_binding(*second_route_image)}) &&
                second_route_creation.session->last_result().error ==
                    kernel::SessionError::InvalidImageStructure,
            "a second numeric command route entered an initialized Session");
}

void verify_submission_and_idempotency(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.submission");
    const auto reject = [&](kernel::CommandRequest request,
                            kernel::CommandSubmissionReason reason,
                            std::string_view message) {
        const auto requested_route = request.route_handle;
        const auto requested_run_id = request.run_id;
        const bool route_known = std::any_of(
            live.image->command_routes().begin(),
            live.image->command_routes().end(),
            [requested_route](const auto& route) {
                return route.handle == requested_route;
            });
        const bool route_source =
            route_known &&
            (reason == kernel::CommandSubmissionReason::TargetMismatch ||
             reason == kernel::CommandSubmissionReason::SchemaMismatch ||
             reason == kernel::CommandSubmissionReason::AuthorityMismatch ||
             reason == kernel::CommandSubmissionReason::CapacityExceeded ||
             reason == kernel::CommandSubmissionReason::PayloadTypeMismatch ||
             reason == kernel::CommandSubmissionReason::PayloadRejected);
        const auto expected_source_field = [&] {
            if (route_source) return kernel::RuntimeApiField::None;
            switch (reason) {
            case kernel::CommandSubmissionReason::InvalidLifecycle:
                return kernel::RuntimeApiField::SessionLifecycle;
            case kernel::CommandSubmissionReason::EmptyCommandId:
            case kernel::CommandSubmissionReason::CommandIdConflict:
                return kernel::RuntimeApiField::CommandId;
            case kernel::CommandSubmissionReason::WrongRunId:
                return kernel::RuntimeApiField::CommandRunId;
            case kernel::CommandSubmissionReason::UnknownRoute:
                return kernel::RuntimeApiField::CommandRoute;
            case kernel::CommandSubmissionReason::InvalidTiming:
                return kernel::RuntimeApiField::CommandTiming;
            case kernel::CommandSubmissionReason::InvalidSupersessionKey:
                return kernel::RuntimeApiField::CommandSupersessionKey;
            case kernel::CommandSubmissionReason::MissingPayload:
                return kernel::RuntimeApiField::CommandPayload;
            case kernel::CommandSubmissionReason::TransactionOpen:
                return kernel::RuntimeApiField::TransactionCutoff;
            case kernel::CommandSubmissionReason::AllocationFailure:
                return kernel::RuntimeApiField::RuntimeAllocation;
            case kernel::CommandSubmissionReason::None:
            case kernel::CommandSubmissionReason::TargetMismatch:
            case kernel::CommandSubmissionReason::SchemaMismatch:
            case kernel::CommandSubmissionReason::AuthorityMismatch:
            case kernel::CommandSubmissionReason::CapacityExceeded:
            case kernel::CommandSubmissionReason::PayloadTypeMismatch:
            case kernel::CommandSubmissionReason::PayloadRejected:
                return kernel::RuntimeApiField::None;
            }
            return kernel::RuntimeApiField::RuntimeExecution;
        }();
        const auto expected_subject_kind = [&] {
            switch (reason) {
            case kernel::CommandSubmissionReason::InvalidLifecycle:
                return kernel::RuntimeDiagnosticSubjectKind::Session;
            case kernel::CommandSubmissionReason::WrongRunId:
                return kernel::RuntimeDiagnosticSubjectKind::Run;
            case kernel::CommandSubmissionReason::UnknownRoute:
            case kernel::CommandSubmissionReason::TargetMismatch:
            case kernel::CommandSubmissionReason::SchemaMismatch:
            case kernel::CommandSubmissionReason::AuthorityMismatch:
            case kernel::CommandSubmissionReason::CapacityExceeded:
            case kernel::CommandSubmissionReason::PayloadTypeMismatch:
            case kernel::CommandSubmissionReason::PayloadRejected:
                return kernel::RuntimeDiagnosticSubjectKind::CommandRoute;
            case kernel::CommandSubmissionReason::TransactionOpen:
                return kernel::RuntimeDiagnosticSubjectKind::Transaction;
            case kernel::CommandSubmissionReason::None:
            case kernel::CommandSubmissionReason::EmptyCommandId:
            case kernel::CommandSubmissionReason::InvalidTiming:
            case kernel::CommandSubmissionReason::MissingPayload:
            case kernel::CommandSubmissionReason::CommandIdConflict:
            case kernel::CommandSubmissionReason::AllocationFailure:
            case kernel::CommandSubmissionReason::InvalidSupersessionKey:
                return kernel::RuntimeDiagnosticSubjectKind::Command;
            }
            return kernel::RuntimeDiagnosticSubjectKind::Command;
        }();
        const bool route_subject =
            expected_subject_kind ==
            kernel::RuntimeDiagnosticSubjectKind::CommandRoute;
        const auto expected_subject_reference =
            route_subject
                ? (route_known
                       ? kernel::RuntimeDiagnosticSubjectReferenceKind::
                             ImageHandle
                       : kernel::RuntimeDiagnosticSubjectReferenceKind::
                             NumericValue)
                : kernel::RuntimeDiagnosticSubjectReferenceKind::None;
        const auto outcome = live.session->submit_command(std::move(request));
        require(!outcome && outcome.reason == reason &&
                    live.session->active_run_id() != nullptr &&
                    outcome.active_run_id ==
                        *live.session->active_run_id() &&
                    outcome.primary_diagnostic.has_value() &&
                    outcome.primary_diagnostic->code ==
                        kernel::RuntimeDiagnosticCode::
                            CommandSubmissionRejected &&
                    outcome.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::CommandSubmission &&
                    outcome.primary_diagnostic->operation ==
                        kernel::RuntimeOperation::SubmitCommand &&
                    outcome.primary_diagnostic->source_kind ==
                        (route_source
                             ? kernel::RuntimeDiagnosticSourceKind::
                                   ImageConformance
                             : kernel::RuntimeDiagnosticSourceKind::
                                   RuntimeApi) &&
                    outcome.primary_diagnostic->source_handle ==
                        (route_source ? requested_route : 0U) &&
                    outcome.primary_diagnostic->source_field ==
                        expected_source_field &&
                    outcome.primary_diagnostic->subject_kind ==
                        expected_subject_kind &&
                    outcome.primary_diagnostic->subject_reference_kind ==
                        expected_subject_reference &&
                    outcome.primary_diagnostic->subject_handle ==
                        (route_subject ? requested_route : 0U) &&
                    outcome.primary_diagnostic->run_id == requested_run_id &&
                    outcome.primary_diagnostic->run_context_present ==
                        !requested_run_id.empty() &&
                    outcome.primary_diagnostic->tick ==
                        live.session->committed_tick() &&
                    outcome.primary_diagnostic->base_epoch ==
                        live.session->committed_epoch() &&
                    outcome.primary_diagnostic
                        ->simulation_context_present &&
                    outcome.primary_diagnostic->cause_kind ==
                        kernel::RuntimeDiagnosticCauseKind::
                            CommandSubmissionReason &&
                    outcome.primary_diagnostic->cause_code ==
                        kernel::SessionError::None &&
                    outcome.primary_diagnostic->cause_ref ==
                        static_cast<std::uint32_t>(reason) &&
                    outcome.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Valid &&
                    outcome.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::RejectCommand &&
                    kernel::to_string(outcome.primary_diagnostic->code) ==
                        "GNC-RUN-CMD-0001",
                message);
    };

    auto request = command_request(live, "", Mode::Active);
    reject(std::move(request),
           kernel::CommandSubmissionReason::EmptyCommandId,
           "empty command id passed submission validation");
    request = command_request(live, "wrong-run", Mode::Active);
    request.run_id = kernel::RunId("run.other");
    reject(std::move(request), kernel::CommandSubmissionReason::WrongRunId,
           "wrong RunId command passed submission validation");
    request = command_request(live, "wrong-route", Mode::Active);
    request.route_handle += 100000U;
    reject(std::move(request), kernel::CommandSubmissionReason::UnknownRoute,
           "unknown route command passed submission validation");
    request = command_request(live, "wrong-target", Mode::Active);
    request.target_runtime_component_handle += 100000U;
    reject(std::move(request), kernel::CommandSubmissionReason::TargetMismatch,
           "wrong command target passed submission validation");
    request = command_request(live, "wrong-schema", Mode::Active);
    request.payload_schema_id = "wrong.command.schema@1";
    reject(std::move(request), kernel::CommandSubmissionReason::SchemaMismatch,
           "wrong command schema passed submission validation");
    request = command_request(live, "wrong-authority", Mode::Active);
    request.decision_authority += 1U;
    reject(std::move(request),
           kernel::CommandSubmissionReason::AuthorityMismatch,
           "wrong decision authority passed submission validation");
    request = command_request(live, "wrong-timing", Mode::Active);
    request.effective_tick = 3;
    reject(std::move(request), kernel::CommandSubmissionReason::InvalidTiming,
           "out-of-range effective tick passed submission validation");
    request = command_request(live, "empty-supersession", Mode::Active);
    request.supersession_key.clear();
    reject(std::move(request),
           kernel::CommandSubmissionReason::InvalidSupersessionKey,
           "empty supersession key passed submission validation");
    request = command_request(live, "missing-payload", Mode::Active);
    request.payload = {};
    reject(std::move(request), kernel::CommandSubmissionReason::MissingPayload,
           "missing command payload passed submission validation");
    request = command_request(live, "wrong-payload", Mode::Active);
    request.payload = kernel::InProcessOwnedValue::make(WrongCommand{7U});
    reject(std::move(request),
           kernel::CommandSubmissionReason::PayloadTypeMismatch,
           "wrong process-local command type passed submission validation");

    auto accepted_request =
        command_request(live, "accepted", Mode::Active, RequestedDecision::Apply,
                        1, std::nullopt, "accepted");
    const auto accepted = live.session->submit_command(accepted_request);
    require(accepted && !accepted.duplicate_retry &&
                !accepted.primary_diagnostic.has_value() &&
                accepted.ledger_sequence ==
                    live.session->command_ledger_sequence(),
            "valid command did not enter the per-Session ledger and queue");
    const auto sequence_after_accept =
        live.session->command_ledger_sequence();
    const auto duplicate_retry =
        live.session->submit_command(accepted_request);
    require(duplicate_retry && duplicate_retry.duplicate_retry &&
                duplicate_retry.ledger_sequence == accepted.ledger_sequence &&
                live.session->command_ledger_sequence() ==
                    sequence_after_accept &&
                live.session->pending_command_count() == 1U,
            "exact command retry was not idempotent");

    auto conflict = accepted_request;
    conflict.supersession_key = "conflicting-reuse";
    reject(std::move(conflict),
           kernel::CommandSubmissionReason::CommandIdConflict,
           "conflicting command-id reuse passed idempotency validation");

    for (std::uint32_t index = 0U; index < kQueueCapacity - 1U; ++index) {
        const auto queued = live.session->submit_command(command_request(
            live, "capacity-" + std::to_string(index), Mode::Standby,
            RequestedDecision::Defer, 2, std::nullopt,
            "capacity-" + std::to_string(index)));
        require(static_cast<bool>(queued),
                "bounded queue rejected an in-capacity command");
    }
    reject(command_request(live, "capacity-overflow", Mode::Standby,
                           RequestedDecision::Defer, 2, std::nullopt,
                           "capacity-overflow"),
           kernel::CommandSubmissionReason::CapacityExceeded,
           "route queue capacity did not reject the newest command");
}

void verify_submission_allocation_failure(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.submission-allocation");
    auto request = command_request(live, "allocation", Mode::Active);
    command_allocation_fault::arm(0);
    const auto outcome = live.session->submit_command(std::move(request));
    command_allocation_fault::disarm();
    require(!outcome &&
                outcome.reason ==
                    kernel::CommandSubmissionReason::AllocationFailure &&
                outcome.active_run_id == live.run_id &&
                outcome.ledger_sequence == 0U &&
                outcome.primary_diagnostic.has_value() &&
                outcome.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::CommandSubmissionRejected &&
                outcome.primary_diagnostic->source_kind ==
                    kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                outcome.primary_diagnostic->source_handle == 0U &&
                outcome.primary_diagnostic->source_field ==
                    kernel::RuntimeApiField::RuntimeAllocation &&
                outcome.primary_diagnostic->subject_kind ==
                    kernel::RuntimeDiagnosticSubjectKind::Command &&
                outcome.primary_diagnostic->subject_reference_kind ==
                    kernel::RuntimeDiagnosticSubjectReferenceKind::None &&
                outcome.primary_diagnostic->subject_handle == 0U &&
                outcome.primary_diagnostic->run_context_present &&
                outcome.primary_diagnostic->simulation_context_present &&
                outcome.primary_diagnostic->cause_ref ==
                    static_cast<std::uint32_t>(
                        kernel::CommandSubmissionReason::AllocationFailure) &&
                outcome.primary_diagnostic->validity_effect ==
                    contracts::EvidenceValidity::Valid &&
                outcome.primary_diagnostic->disposition ==
                    kernel::RuntimeFailureDisposition::RejectCommand &&
                live.session->command_ledger_sequence() == 0U &&
                live.session->command_submission_outcomes().empty() &&
                live.session->pending_command_count() == 0U,
            "submission allocation failure mutated command evidence or lost its public diagnostic");
}

void verify_atomic_application(const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.atomic-application");
    const auto accepted = live.session->submit_command(
        command_request(live, "apply", Mode::Active));
    require(accepted && live.session->pending_command_count() == 1U &&
                live.session->command_application_receipts().empty() &&
                live.session->committed_events().empty() &&
                committed_mode(live).revision == 0U,
            "submission outcome was treated as command application evidence");
    const auto before = committed_mode(live);
    const auto step = live.session->execute_step();
    const auto after = committed_mode(live);
    require(step.status == kernel::StepStatus::Committed &&
                before.mode == Mode::Standby && before.revision == 0U &&
                after.mode == Mode::Active && after.revision == 1U &&
                live.session->pending_command_count() == 0U &&
                live.session->command_application_receipts().size() == 1U &&
                live.session->committed_events().size() == 1U,
            "ModelCommit did not publish state, receipt, event, and queue consumption atomically");
    const auto& receipt =
        live.session->command_application_receipts().front();
    const auto& event = live.session->committed_events().front();
    const auto& step_summary = live.session->last_step_summary();
    const auto reducer_handle =
        live.image->command_routes().front().reducer_callsite_handle;
    const auto consumer_handle =
        live.image->event_deliveries().front().consumer_callsite_handle;
    const auto reducer_position = std::find(
        step_summary.executed_callsite_handles.begin(),
        step_summary.executed_callsite_handles.end(), reducer_handle);
    const auto consumer_position = std::find(
        step_summary.executed_callsite_handles.begin(),
        step_summary.executed_callsite_handles.end(), consumer_handle);
    const auto* payload = event.payload.get_if<ModeEvent>();
    const auto* consumed = event.consumer_output.get_if<ModeConsumed>();
    require(receipt.decision == kernel::CommandApplicationDecision::Applied &&
                receipt.command_id == accepted.command_id &&
                receipt.committed_epoch == live.session->committed_epoch() &&
                event.event_id.tick == receipt.tick &&
                event.event_id.delivery_handle == event.delivery_handle &&
                event.event_id.command_ledger_sequence ==
                    receipt.command_ledger_sequence &&
                event.command_id == accepted.command_id &&
                event.committed_epoch == live.session->committed_epoch() &&
                payload != nullptr && consumed != nullptr &&
                payload->from == Mode::Standby &&
                payload->to == Mode::Active && payload->revision == 1U &&
                consumed->observed == Mode::Active &&
                consumed->revision == 1U &&
                reducer_position !=
                    step_summary.executed_callsite_handles.end() &&
                consumer_position !=
                    step_summary.executed_callsite_handles.end() &&
                reducer_position < consumer_position,
            "typed later-phase event evidence differs from the committed state replacement");
}

void verify_transaction_cutoff(const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.cutoff");
    const auto before = live.session->submit_command(
        command_request(live, "before-cutoff", Mode::Active));
    require(static_cast<bool>(before),
            "pre-cutoff command was not queued");
    live.provider->control.command_after_cutoff =
        command_request(live, "after-cutoff", Mode::Standby);
    const auto step = live.session->execute_step();
    const auto& after_cutoff =
        live.provider->control.command_after_cutoff_outcome;
    require(step.status == kernel::StepStatus::Committed &&
                after_cutoff.has_value() && !*after_cutoff &&
                 after_cutoff->reason ==
                     kernel::CommandSubmissionReason::TransactionOpen &&
                 after_cutoff->primary_diagnostic.has_value() &&
                 after_cutoff->primary_diagnostic->operation ==
                     kernel::RuntimeOperation::SubmitCommand &&
                 after_cutoff->primary_diagnostic->source_kind ==
                     kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                 after_cutoff->primary_diagnostic->source_field ==
                     kernel::RuntimeApiField::TransactionCutoff &&
                 after_cutoff->primary_diagnostic->subject_kind ==
                     kernel::RuntimeDiagnosticSubjectKind::Transaction &&
                 after_cutoff->primary_diagnostic
                         ->subject_reference_kind ==
                     kernel::RuntimeDiagnosticSubjectReferenceKind::
                         ImageHandle &&
                 after_cutoff->primary_diagnostic->subject_handle ==
                     live.image->transactions().front().handle &&
                 after_cutoff->primary_diagnostic->run_context_present &&
                 after_cutoff->primary_diagnostic
                     ->simulation_context_present &&
                 after_cutoff->primary_diagnostic->cause_ref ==
                     static_cast<std::uint32_t>(
                         kernel::CommandSubmissionReason::TransactionOpen) &&
                 after_cutoff->primary_diagnostic->disposition ==
                     kernel::RuntimeFailureDisposition::RejectCommand &&
                live.session->command_ledger_sequence() ==
                    before.ledger_sequence &&
                live.session->command_submission_outcomes().size() == 1U &&
                live.session->command_application_receipts().size() == 1U &&
                live.session->pending_command_count() == 0U &&
                committed_mode(live).mode == Mode::Active &&
                committed_mode(live).revision == 1U,
            "post-cutoff submission entered or disturbed the open transaction");

    const auto next_boundary = live.session->submit_command(
        command_request(live, "after-cutoff", Mode::Standby,
                        RequestedDecision::Apply, 1));
    require(next_boundary &&
                next_boundary.ledger_sequence ==
                    before.ledger_sequence + 1U &&
                live.session->execute_step().status ==
                    kernel::StepStatus::Committed &&
                committed_mode(live).mode == Mode::Standby &&
                committed_mode(live).revision == 2U,
            "post-cutoff command did not enter the next committed boundary");
}

void verify_decisions_and_queue_maintenance(
    const contracts::ExecutionPlanImage& image) {
    {
        auto live = initialize_session(image, "run.reject-defer");
        require(static_cast<bool>(live.session->submit_command(command_request(
                    live, "reject", Mode::Standby,
                    RequestedDecision::Apply))),
                "rejected-decision command was not queued");
        const auto rejected = live.session->execute_step();
        require(rejected.status == kernel::StepStatus::Committed &&
                    committed_mode(live).revision == 0U &&
                    live.session->pending_command_count() == 0U &&
                    live.session->command_application_receipts().back().decision ==
                        kernel::CommandApplicationDecision::Rejected &&
                    live.session->committed_events().empty(),
                "reducer Rejected decision acquired state or event evidence");

        require(static_cast<bool>(live.session->submit_command(command_request(
                    live, "defer", Mode::Active,
                    RequestedDecision::Defer, 1))),
                "deferred-decision command was not queued");
        const auto deferred = live.session->execute_step();
        require(deferred.status == kernel::StepStatus::Committed &&
                    committed_mode(live).revision == 0U &&
                    live.session->pending_command_count() == 1U &&
                    live.session->command_application_receipts().back().decision ==
                        kernel::CommandApplicationDecision::Deferred,
                "reducer Deferred decision was consumed before terminal policy");
    }

    {
        auto live = initialize_session(image, "run.supersession");
        require(live.session->submit_command(command_request(
                    live, "old", Mode::Standby,
                    RequestedDecision::Apply, 0, std::nullopt, "same-key")) &&
                    live.session->submit_command(command_request(
                        live, "new", Mode::Active,
                        RequestedDecision::Apply, 0, std::nullopt,
                        "same-key")),
                "supersession commands were not queued");
        const auto epoch_before = live.session->committed_epoch();
        const auto ledger_before = live.session->command_ledger_sequence();
        const auto step = live.session->execute_step();
        require(step.status == kernel::StepStatus::Committed &&
                    live.session->committed_epoch() == epoch_before + 1U &&
                    live.session->command_ledger_sequence() ==
                        ledger_before + 1U &&
                    committed_mode(live).mode == Mode::Active &&
                    live.session->command_maintenance_receipts().size() == 1U &&
                    live.session->command_maintenance_receipts().front().disposition ==
                        kernel::CommandMaintenanceDisposition::Superseded &&
                    live.session->command_maintenance_receipts().front().command_id ==
                        kernel::CommandId("old"),
                "latest-due-per-key supersession was not deterministic");
    }

    {
        auto live = initialize_session(image, "run.expiry-termination");
        require(live.session->submit_command(command_request(
                    live, "occupy-tick-one", Mode::Active,
                    RequestedDecision::Reject, 1, std::nullopt, "occupy")) &&
                    live.session->submit_command(command_request(
                        live, "expire", Mode::Standby,
                        RequestedDecision::Apply, 1, 1, "expire")) &&
                    live.session->submit_command(command_request(
                        live, "terminal-defer", Mode::Standby,
                        RequestedDecision::Defer, 2, std::nullopt,
                        "terminal-defer")),
                "expiry/termination commands were not queued");
        require(live.session->execute_step().status ==
                    kernel::StepStatus::Committed &&
                    live.session->execute_step().status ==
                    kernel::StepStatus::Committed,
                "pre-terminal steps failed in expiry fixture");
        const auto epoch_before_terminal = live.session->committed_epoch();
        const auto ledger_before_terminal =
            live.session->command_ledger_sequence();
        const auto applications_before_terminal =
            live.session->command_application_receipts().size();
        const auto events_before_terminal =
            live.session->committed_events().size();
        const auto terminal = live.session->execute_step();
        require(terminal.status == kernel::StepStatus::Terminated &&
                    live.session->state() == kernel::SessionState::Completed &&
                    live.session->committed_epoch() ==
                        epoch_before_terminal + 1U &&
                    live.session->command_ledger_sequence() ==
                        ledger_before_terminal + 2U &&
                    live.session->pending_command_count() == 0U &&
                    committed_mode(live).revision == 0U &&
                    live.session->command_application_receipts().size() ==
                        applications_before_terminal &&
                    live.session->committed_events().size() ==
                        events_before_terminal,
                "terminal priority executed or evidenced a pending command");
        const auto& maintenance =
            live.session->command_maintenance_receipts();
        require(std::any_of(
                    maintenance.begin(), maintenance.end(),
                    [](const auto& receipt) {
                        return receipt.command_id == kernel::CommandId("expire") &&
                               receipt.disposition ==
                                   kernel::CommandMaintenanceDisposition::Expired;
                    }) &&
                    std::any_of(
                        maintenance.begin(), maintenance.end(),
                        [](const auto& receipt) {
                            return receipt.command_id ==
                                       kernel::CommandId("terminal-defer") &&
                                   receipt.disposition ==
                                       kernel::CommandMaintenanceDisposition::Terminated;
                        }),
                "expiry or terminal-deferred maintenance receipt is absent");
    }
}

void verify_failure_rollback_and_retry(
    const contracts::ExecutionPlanImage& image) {
    const auto run_failure_case = [&](std::string run_id,
                                      auto configure,
                                      kernel::RuntimeDiagnosticStage stage,
                                      std::string_view message) {
        auto live = initialize_session(image, std::move(run_id));
        require(static_cast<bool>(live.session->submit_command(
                    command_request(live, "retry", Mode::Active))),
                "rollback fixture command was not queued");
        configure(*live.provider);
        const auto before_epoch = live.session->committed_epoch();
        const auto before_tick = live.session->committed_tick();
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    live.session->state() == kernel::SessionState::Initialized &&
                    live.session->committed_epoch() == before_epoch &&
                    live.session->committed_tick() == before_tick &&
                    committed_mode(live).revision == 0U &&
                    live.session->pending_command_count() == 1U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty() &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage == stage,
                message);
        const auto retried = live.session->execute_step();
        require(retried.status == kernel::StepStatus::Committed &&
                    committed_mode(live).mode == Mode::Active &&
                    committed_mode(live).revision == 1U &&
                    live.session->pending_command_count() == 0U &&
                    live.session->command_application_receipts().size() == 1U &&
                    live.session->committed_events().size() == 1U,
                "same due command did not succeed exactly once on retry");
    };

    run_failure_case(
        "run.reducer-failure",
        [](ModeOwnerProvider& provider) {
            provider.control.reducer_failures_remaining = 1U;
        },
        kernel::RuntimeDiagnosticStage::CommandReduction,
        "reducer failure leaked staged state, receipt, event, or queue consumption");
    run_failure_case(
        "run.consumer-failure",
        [](ModeOwnerProvider& provider) {
            provider.control.consumer_failures_remaining = 1U;
        },
        kernel::RuntimeDiagnosticStage::EventConsumption,
        "event-consumer failure leaked staged state, receipt, event, or queue consumption");
    run_failure_case(
        "run.observation-failure",
        [](ModeOwnerProvider& provider) {
            provider.control.observation_seal_clone_failures_remaining = 1U;
        },
        kernel::RuntimeDiagnosticStage::ObservationSeal,
        "observation-seal failure leaked staged state, receipt, event, or queue consumption");
    {
        auto live = initialize_session(image, "run.precommit-fatal");
        require(static_cast<bool>(live.session->submit_command(
                    command_request(live, "precommit-fatal", Mode::Active))),
                "precommit-failure command was not queued");
        const auto before_epoch = live.session->committed_epoch();
        const auto before_tick = live.session->committed_tick();
        live.provider->control.invalid_candidates_remaining = 1U;
        const auto failed = live.session->execute_step();
        const auto* outcome = live.session->run_outcome();
        require(failed.status == kernel::StepStatus::Failed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::Precommit &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    failed.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::FailOperation &&
                    live.session->state() == kernel::SessionState::Failed &&
                    live.session->committed_epoch() == before_epoch &&
                    live.session->committed_tick() == before_tick &&
                    committed_mode(live).revision == 0U &&
                    live.session->pending_command_count() == 1U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty() &&
                    outcome != nullptr &&
                    outcome->final_status ==
                        kernel::RunFinalStatus::Failed &&
                    outcome->validity ==
                        contracts::EvidenceValidity::Invalid &&
                    live.session->execute_step().status ==
                        kernel::StepStatus::Failed,
                "candidate precommit failure did not freeze an Invalid run");
    }

    {
        auto live = initialize_session(image, "run.invalid-decision-fatal");
        require(static_cast<bool>(live.session->submit_command(
                    command_request(live, "invalid-decision", Mode::Active))),
                "invalid-decision command was not queued");
        live.provider->control.invalid_decisions_remaining = 1U;
        const auto failed = live.session->execute_step();
        const auto* outcome = live.session->run_outcome();
        require(failed.status == kernel::StepStatus::Failed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::CommandReduction &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    failed.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::FailOperation &&
                    live.session->state() == kernel::SessionState::Failed &&
                    committed_mode(live).revision == 0U &&
                    live.session->pending_command_count() == 1U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty() &&
                    outcome != nullptr &&
                    outcome->final_status ==
                        kernel::RunFinalStatus::Failed &&
                    outcome->validity ==
                        contracts::EvidenceValidity::Invalid,
                "invalid reducer decision did not fail closed");
    }

    {
        auto live = initialize_session(image, "run.maintenance-rollback");
        require(live.session->submit_command(command_request(
                    live, "old", Mode::Standby, RequestedDecision::Apply, 0,
                    std::nullopt, "same-key")) &&
                    live.session->submit_command(command_request(
                        live, "new", Mode::Active,
                        RequestedDecision::Apply, 0, std::nullopt,
                        "same-key")),
                "maintenance rollback commands were not queued");
        const auto ledger_before = live.session->command_ledger_sequence();
        live.provider->control.consumer_failures_remaining = 1U;
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    live.session->state() == kernel::SessionState::Initialized &&
                    live.session->command_ledger_sequence() == ledger_before &&
                    live.session->command_maintenance_receipts().empty() &&
                    live.session->pending_command_count() == 2U &&
                    committed_mode(live).revision == 0U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty(),
                "failed transaction published staged supersession maintenance");
        const auto retried = live.session->execute_step();
        require(retried.status == kernel::StepStatus::Committed &&
                    live.session->command_ledger_sequence() ==
                        ledger_before + 1U &&
                    live.session->command_maintenance_receipts().size() == 1U &&
                    live.session->command_maintenance_receipts().front().disposition ==
                        kernel::CommandMaintenanceDisposition::Superseded &&
                    live.session->pending_command_count() == 0U &&
                    committed_mode(live).revision == 1U &&
                    live.session->command_application_receipts().size() == 1U &&
                    live.session->committed_events().size() == 1U,
                "retry did not atomically commit one supersession and one application");
    }

    {
        auto live = initialize_session(image, "run.projection-failure");
        require(static_cast<bool>(live.session->submit_command(
                    command_request(live, "projection-fatal", Mode::Active))),
                "projection-failure command was not queued");
        const auto before_epoch = live.session->committed_epoch();
        const auto before_tick = live.session->committed_tick();
        const auto before_outputs = live.session->committed_outputs();
        const auto before_histories = live.session->committed_histories();
        live.provider->control.omitted_projections_remaining = 1U;
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    failed.result.error ==
                        kernel::SessionError::FrameSlotAbsent &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::BoundaryInvocation &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    failed.primary_diagnostic->disposition ==
                        kernel::RuntimeFailureDisposition::FailOperation &&
                    live.session->state() == kernel::SessionState::Failed &&
                    live.session->committed_epoch() == before_epoch &&
                    live.session->committed_tick() == before_tick &&
                    committed_mode(live).revision == 0U &&
                    live.session->committed_outputs().size() ==
                        before_outputs.size() &&
                    live.session->committed_histories().size() ==
                        before_histories.size() &&
                    live.session->pending_command_count() == 1U &&
                    kernel::qualification::SessionAccess::
                            command_queue_storage_count(*live.session) ==
                        1U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty() &&
                    live.session->run_outcome() != nullptr &&
                    live.session->run_outcome()->final_status ==
                        kernel::RunFinalStatus::Failed &&
                    live.session->run_outcome()->validity ==
                        contracts::EvidenceValidity::Invalid &&
                    live.session->execute_step().status ==
                        kernel::StepStatus::Failed,
                "projection failure leaked staged effects or remained retryable");
    }
}

void verify_queue_storage_compaction(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.queue-compaction");
    const auto accepted_count =
        static_cast<std::size_t>(kQueueCapacity) + 3U;
    for (std::size_t index = 0U; index < accepted_count; ++index) {
        const auto accepted = live.session->submit_command(command_request(
            live, "compact-" + std::to_string(index), Mode::Standby,
            RequestedDecision::Reject,
            live.session->committed_tick(), std::nullopt,
            "compact-" + std::to_string(index)));
        require(accepted &&
                    kernel::qualification::SessionAccess::
                            command_queue_storage_count(*live.session) <=
                        kQueueCapacity,
                "active command storage exceeded route capacity before commit");
        const auto step = live.session->execute_step();
        require(step.status == kernel::StepStatus::Committed &&
                    live.session->pending_command_count() == 0U &&
                    kernel::qualification::SessionAccess::
                            command_queue_storage_count(*live.session) ==
                        0U,
                "consumed command tombstone remained in active queue storage");
    }
    require(live.session->command_submission_outcomes().size() ==
                accepted_count &&
                live.session->command_application_receipts().size() ==
                    accepted_count &&
                accepted_count > kQueueCapacity,
            "continuous submit/commit did not exceed cumulative route capacity");
}

void verify_step_allocation_failure_is_structured(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.step-allocation");
    require(static_cast<bool>(live.session->submit_command(
                command_request(live, "allocation", Mode::Active))),
            "allocation-failure command was not queued");
    const auto before_epoch = live.session->committed_epoch();
    const auto before_tick = live.session->committed_tick();
    command_allocation_fault::arm(0);
    const auto failed = live.session->execute_step();
    command_allocation_fault::disarm();
    require(failed.status == kernel::StepStatus::Failed &&
                failed.result.error == kernel::SessionError::AllocationFailure &&
                failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::AllocationFailed &&
                live.session->state() == kernel::SessionState::Failed &&
                live.session->committed_epoch() == before_epoch &&
                live.session->committed_tick() == before_tick &&
                committed_mode(live).revision == 0U &&
                live.session->pending_command_count() == 1U &&
                kernel::qualification::SessionAccess::
                        command_queue_storage_count(*live.session) == 1U,
            "step allocation failure escaped structured handling or changed the committed boundary");
}

void verify_cancellation_boundaries(
    const contracts::ExecutionPlanImage& image) {
    {
        auto live = initialize_session(image, "run.cancel-precommit");
        require(live.session->submit_command(command_request(
                    live, "cancel-pre-old", Mode::Standby,
                    RequestedDecision::Apply, 0, std::nullopt,
                    "cancel-pre")) &&
                    live.session->submit_command(command_request(
                        live, "cancel-pre-new", Mode::Active,
                        RequestedDecision::Apply, 0, std::nullopt,
                        "cancel-pre")),
                "precommit cancellation commands were not queued");
        const auto ledger_before = live.session->command_ledger_sequence();
        live.provider->control.cancel_in_reducer_remaining = 1U;
        const auto cancelled = live.session->execute_step();
        require(cancelled.status == kernel::StepStatus::Cancelled &&
                    live.session->state() == kernel::SessionState::Cancelled &&
                    committed_mode(live).revision == 0U &&
                    live.session->command_ledger_sequence() == ledger_before &&
                    live.session->command_maintenance_receipts().empty() &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty() &&
                    live.session->pending_command_count() == 2U,
                "precommit cancellation published staged command effects");
    }

    {
        auto live = initialize_session(image, "run.cancel-postcommit");
        require(static_cast<bool>(live.session->submit_command(
                    command_request(live, "cancel-post", Mode::Active))),
                "postcommit cancellation command was not queued");
        live.provider->control.cancel_on_state_swap_remaining = 1U;
        const auto cancelled = live.session->execute_step();
        require(cancelled.status == kernel::StepStatus::Committed &&
                    live.session->state() == kernel::SessionState::Cancelled &&
                    committed_mode(live).mode == Mode::Active &&
                    committed_mode(live).revision == 1U &&
                    live.session->pending_command_count() == 0U &&
                    live.session->command_application_receipts().size() == 1U &&
                    live.session->committed_events().size() == 1U,
                "postcommit cancellation hid or rolled back committed command effects");
    }
}

void drive_to_completed(LiveSession& live) {
    while (live.session->state() == kernel::SessionState::Initialized) {
        const auto step = live.session->execute_step();
        require(step.status == kernel::StepStatus::Committed ||
                    step.status == kernel::StepStatus::Terminated,
                "ModeOwner run did not advance to Completed");
    }
    require(live.session->state() == kernel::SessionState::Completed,
            "ModeOwner run did not reach Completed");
}

void verify_reset_isolation_and_dispose(
    const CompiledFixture& resettable,
    const CompiledFixture& non_resettable) {
    auto first = initialize_session(resettable.image, "run.first");
    auto second = initialize_session(resettable.image, "run.second");
    require(static_cast<bool>(first.session->submit_command(
                command_request(first, "first-only", Mode::Active))),
            "first Session command was not queued");
    require(first.session->pending_command_count() == 1U &&
                second.session->pending_command_count() == 0U &&
                second.session->command_ledger_sequence() == 0U,
            "command ledger or queue crossed Session ownership");
    require(first.session->execute_step().status ==
                kernel::StepStatus::Committed &&
                committed_mode(first).mode == Mode::Active &&
                committed_mode(second).mode == Mode::Standby &&
                second.session->committed_events().empty(),
            "state or event evidence crossed Session ownership");
    drive_to_completed(first);
    const auto completed_ledger = first.session->command_ledger_sequence();
    const auto completed_submissions =
        first.session->command_submission_outcomes().size();
    const auto after_complete = first.session->submit_command(
        command_request(first, "after-complete", Mode::Standby));
    require(!after_complete &&
                after_complete.reason ==
                    kernel::CommandSubmissionReason::InvalidLifecycle &&
                after_complete.primary_diagnostic.has_value() &&
                after_complete.primary_diagnostic->source_kind ==
                    kernel::RuntimeDiagnosticSourceKind::RuntimeApi &&
                after_complete.primary_diagnostic->source_field ==
                    kernel::RuntimeApiField::SessionLifecycle &&
                after_complete.primary_diagnostic->subject_kind ==
                    kernel::RuntimeDiagnosticSubjectKind::Session &&
                after_complete.primary_diagnostic->subject_reference_kind ==
                    kernel::RuntimeDiagnosticSubjectReferenceKind::None &&
                after_complete.primary_diagnostic->subject_handle == 0U &&
                after_complete.active_run_id == first.run_id &&
                after_complete.primary_diagnostic->run_context_present &&
                after_complete.primary_diagnostic
                    ->simulation_context_present &&
                after_complete.primary_diagnostic->validity_effect ==
                    contracts::EvidenceValidity::Valid &&
                after_complete.primary_diagnostic->disposition ==
                    kernel::RuntimeFailureDisposition::RejectCommand &&
                first.session->command_ledger_sequence() == completed_ledger &&
                first.session->command_submission_outcomes().size() ==
                    completed_submissions,
            "Completed Session command attempt mutated frozen run control evidence");
    const kernel::RunId reset_run("run.first.reset");
    const auto reset = first.session->reset(
        {reset_run, kernel::exact_run_binding(*first.image)});
    require(reset && first.session->state() == kernel::SessionState::Initialized &&
                committed_mode(first).mode == Mode::Standby &&
                committed_mode(first).revision == 0U &&
                first.session->command_ledger_sequence() == 0U &&
                first.session->pending_command_count() == 0U &&
                first.session->command_submission_outcomes().empty() &&
                first.session->command_maintenance_receipts().empty() &&
                first.session->command_application_receipts().empty() &&
                first.session->committed_events().empty(),
            "successful Completed-only ResetCommit did not rebuild state and clear command control");

    auto no_reset = initialize_session(non_resettable.image,
                                       "run.no-reset-capability");
    require(static_cast<bool>(no_reset.session->submit_command(
                command_request(no_reset, "applied-before-reset", Mode::Active))),
            "non-resettable Session command was not queued");
    drive_to_completed(no_reset);
    const auto ledger_before = no_reset.session->command_ledger_sequence();
    const auto submissions_before =
        no_reset.session->command_submission_outcomes().size();
    const auto maintenance_before =
        no_reset.session->command_maintenance_receipts().size();
    const auto applications_before =
        no_reset.session->command_application_receipts().size();
    const auto events_before = no_reset.session->committed_events().size();
    const auto state_before = committed_mode(no_reset);
    const auto failed_reset = no_reset.session->reset(
        {kernel::RunId("run.no-reset-capability.reset"),
         kernel::exact_run_binding(*no_reset.image)});
    require(!failed_reset &&
                failed_reset.result.error ==
                    kernel::SessionError::ResetCapabilityMissing &&
                no_reset.session->state() == kernel::SessionState::Failed &&
                no_reset.session->command_ledger_sequence() == ledger_before &&
                no_reset.session->command_submission_outcomes().size() ==
                    submissions_before &&
                no_reset.session->command_maintenance_receipts().size() ==
                    maintenance_before &&
                no_reset.session->command_application_receipts().size() ==
                    applications_before &&
                no_reset.session->committed_events().size() == events_before &&
                committed_mode(no_reset).mode == state_before.mode &&
                committed_mode(no_reset).revision == state_before.revision,
            "failed optional-reset validation mutated committed Session evidence");

    drive_to_completed(second);
    require(static_cast<bool>(second.session->dispose()),
            "Completed Session dispose failed");
    require(second.session->state() == kernel::SessionState::Disposed &&
                second.session->runtime_cell_count() == 0U &&
                second.session->committed_state_count() == 0U &&
                second.session->pending_command_count() == 0U &&
                second.session->command_submission_outcomes().empty() &&
                second.session->command_maintenance_receipts().empty() &&
                second.session->command_application_receipts().empty() &&
                second.session->committed_events().empty(),
            "dispose retained Session-owned runtime or command/event objects");
}

void verify_checkpoint_restore_unsupported(
    const contracts::ExecutionPlanImage& image) {
    auto source = initialize_session(image, "run.checkpoint-unsupported");
    const auto state_before = committed_mode(source);
    const auto ledger_before = source.session->command_ledger_sequence();
    const auto checkpoint = source.session->checkpoint();
    require(!checkpoint &&
                checkpoint.result.error ==
                    kernel::SessionError::UnsupportedCheckpointCapability &&
                !checkpoint.barrier_satisfied &&
                !checkpoint.checkpoint_commit &&
                checkpoint.checkpoint == nullptr &&
                checkpoint.primary_diagnostic.has_value() &&
                checkpoint.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::CheckpointUnsupported &&
                checkpoint.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::CheckpointBarrier &&
                source.session->state() ==
                    kernel::SessionState::Initialized &&
                source.session->active_run_id() != nullptr &&
                source.session->active_run_id()->value() ==
                    "run.checkpoint-unsupported" &&
                source.session->committed_epoch() == 0U &&
                source.session->committed_tick() == 0 &&
                source.session->committed_step_count() == 0U &&
                source.session->command_ledger_sequence() == ledger_before &&
                source.session->pending_command_count() == 0U &&
                source.session->command_submission_outcomes().empty() &&
                source.session->command_maintenance_receipts().empty() &&
                source.session->command_application_receipts().empty() &&
                source.session->committed_events().empty() &&
                source.session->run_outcome() == nullptr &&
                committed_mode(source).mode == state_before.mode &&
                committed_mode(source).revision == state_before.revision,
            "command/event checkpoint request changed the source Session");

    auto provider = std::make_shared<ModeOwnerProvider>(*source.image);
    auto created = kernel::create_session(source.image, provider);
    require(static_cast<bool>(created),
            "unsupported restore target Session creation failed");
    const auto restore = created.session->restore(
        {kernel::RunId("run.restore-unsupported"),
         kernel::exact_run_binding(*source.image), nullptr});
    require(!restore &&
                restore.result.error ==
                    kernel::SessionError::UnsupportedCheckpointCapability &&
                !restore.restore_commit &&
                restore.primary_diagnostic.has_value() &&
                restore.primary_diagnostic->code ==
                    kernel::RuntimeDiagnosticCode::CheckpointUnsupported &&
                restore.primary_diagnostic->stage ==
                    kernel::RuntimeDiagnosticStage::RestoreRequest &&
                created.session->state() == kernel::SessionState::Created &&
                created.session->active_run_id() == nullptr &&
                created.session->active_run_binding() == nullptr &&
                !created.session->run_sequence().has_value() &&
                created.session->run_outcome() == nullptr &&
                created.session->restore_lineage() == nullptr &&
                created.session->last_restore_checkpoint() == nullptr &&
                created.session->committed_epoch() == 0U &&
                created.session->committed_tick() == 0 &&
                created.session->committed_step_count() == 0U &&
                created.session->preparation_count() == 0U &&
                created.session->runtime_cell_count() == 0U &&
                created.session->committed_state_count() == 0U &&
                created.session->command_ledger_sequence() == 0U &&
                created.session->pending_command_count() == 0U &&
                created.session->command_submission_outcomes().empty() &&
                created.session->command_maintenance_receipts().empty() &&
                created.session->command_application_receipts().empty() &&
                created.session->committed_events().empty(),
            "command/event restore request changed the Created target Session");
}

std::size_t run_self_check() {
    const auto resettable = compile_fixture(true);
    const auto non_resettable = compile_fixture(false);
    const auto long_running = compile_fixture(true, 12);
    verify_compiler_and_image_contracts(resettable);
    verify_submission_and_idempotency(resettable.image);
    verify_submission_allocation_failure(resettable.image);
    verify_atomic_application(resettable.image);
    verify_transaction_cutoff(resettable.image);
    verify_decisions_and_queue_maintenance(resettable.image);
    verify_failure_rollback_and_retry(resettable.image);
    verify_queue_storage_compaction(long_running.image);
    verify_step_allocation_failure_is_structured(resettable.image);
    verify_cancellation_boundaries(resettable.image);
    verify_reset_isolation_and_dispose(resettable, non_resettable);
    verify_checkpoint_restore_unsupported(resettable.image);
    return 12U;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_command_event_probe --self-check\n";
        return 2;
    }
    try {
        const auto checks = run_self_check();
        std::cout << "kernel command/event self-check passed: " << checks
                  << " contract groups\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kernel command/event self-check failed: "
                  << error.what() << '\n';
        return 1;
    }
}
