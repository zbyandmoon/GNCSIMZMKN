#include "gnc/compiler/complete_execution_plan.hpp"
#include "gnc/kernel/session.hpp"
#include "support/session_qualification_access.hpp"

#include <yyz/inactive_child_activation.hpp>

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

constexpr std::string_view kParentEntity =
    "entity.fixture.yyz.activation.parent@1";
constexpr std::string_view kChildEntity =
    "entity.fixture.yyz.activation.child@1";
constexpr std::string_view kRelationshipOccurrence =
    "a.activation-relationship";
constexpr std::string_view kParentOccurrence = "b.activation-parent";
constexpr std::string_view kChildOccurrence = "c.activation-child";
constexpr std::string_view kConsumerOccurrence = "d.activation-consumer";
constexpr std::string_view kTransaction =
    "transaction.fixture.yyz.inactive-child-activation";
constexpr std::string_view kRoute = "activate-child";
constexpr std::string_view kActivation = "known-activation.parent-child";
constexpr std::uint32_t kDecisionAuthority = 73U;
constexpr double kTransferMass = 12.0;
constexpr double kChildPosition = 125.0;
constexpr double kChildVelocity = 3.0;
constexpr double kInertiaPerMass = 2.5;
constexpr double kInfluenceGain = 2.0;
constexpr double kTolerance = 1.0e-12;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool near(double lhs, double rhs) noexcept {
    return std::isfinite(lhs) && std::isfinite(rhs) &&
           std::abs(lhs - rhs) <= kTolerance;
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

[[nodiscard]] compiler::SourceRef source_ref(std::string path) {
    return {"fixture://yyz/inactive-child-activation", std::move(path)};
}

[[nodiscard]] sdk::CanonicalConfigBlock config(
    std::string schema,
    std::initializer_list<sdk::CanonicalConfigField> fields) {
    return {std::move(schema), 1U,
            std::vector<sdk::CanonicalConfigField>(fields)};
}

void append_field_sources(compiler::CompleteSourceOccurrence& occurrence) {
    for (const auto& field : occurrence.configuration.fields) {
        occurrence.configuration_field_sources.push_back(
            {field.field_id,
             source_ref("/occurrences/" + occurrence.occurrence_id +
                        "/configuration/" + field.field_id)});
    }
}

[[nodiscard]] compiler::CompleteSourceOccurrence occurrence(
    std::string occurrence_id, std::string model_id,
    sdk::CanonicalConfigBlock configuration, std::string entity_id,
    const compiler::ScopeKey& scope, sdk::ModelPlacement placement) {
    compiler::CompleteSourceOccurrence result;
    result.occurrence_id = std::move(occurrence_id);
    result.model_id = std::move(model_id);
    result.model_version =
        std::string(yyz::kInactiveChildActivationModelVersion);
    result.source = source_ref("/occurrences/" + result.occurrence_id);
    result.subject_entity_id = std::move(entity_id);
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

[[nodiscard]] compiler::CompleteStaticCompositionSource fixture_source() {
    compiler::CompleteStaticCompositionSource source;
    source.source_version =
        std::string(compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id =
        "mission.fixture.yyz.inactive-child-activation@1";
    source.plan_id = "plan.fixture.yyz.inactive-child-activation";
    source.mission_source = source_ref("/mission");
    source.clock = {"clock.fixture.yyz.activation.10hz@1", 0.1, 0, 2,
                    source_ref("/clock")};
    source.entities = {
        {std::string(kParentEntity),
         compiler::EntityLifecycle::ActiveAtInitialize,
         source_ref("/entities/parent/id"),
         source_ref("/entities/parent/lifecycle")},
        {std::string(kChildEntity),
         compiler::EntityLifecycle::InactiveAtInitialize,
         source_ref("/entities/child/id"),
         source_ref("/entities/child/lifecycle")}};
    const compiler::ScopeKey parent_scope{
        compiler::ScopeKind::Vehicle, std::string(kParentEntity)};
    const compiler::ScopeKey child_scope{
        compiler::ScopeKind::Vehicle, std::string(kChildEntity)};
    source.scopes = {{parent_scope, source_ref("/scopes/parent")},
                     {child_scope, source_ref("/scopes/child")}};

    source.occurrences.push_back(occurrence(
        std::string(kRelationshipOccurrence),
        std::string(yyz::kActivationRelationshipModelId),
        config(std::string(yyz::kActivationRelationshipConfigSchemaId),
               {{"exact_transfer_mass", kTransferMass}}),
        std::string(kParentEntity), parent_scope,
        sdk::ModelPlacement::VehicleProcess));
    source.occurrences.push_back(occurrence(
        std::string(kParentOccurrence),
        std::string(yyz::kActivationParentModelId),
        config(std::string(yyz::kActivationParentConfigSchemaId),
               {{"child_inertia_per_mass", kInertiaPerMass},
                {"child_position_meters", kChildPosition},
                {"child_velocity_meters_per_second", kChildVelocity}}),
        std::string(kParentEntity), parent_scope,
        sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kChildOccurrence),
        std::string(yyz::kActivationChildModelId),
        config(std::string(yyz::kActivationChildConfigSchemaId),
               {{"expected_inertia_per_mass", kInertiaPerMass}}),
        std::string(kChildEntity), child_scope,
        sdk::ModelPlacement::VehicleOutput));
    source.occurrences.push_back(occurrence(
        std::string(kConsumerOccurrence),
        std::string(yyz::kActivationChildConsumerModelId),
        config(std::string(yyz::kActivationChildConsumerConfigSchemaId),
               {{"influence_gain", kInfluenceGain}}),
        std::string(kChildEntity), child_scope,
        sdk::ModelPlacement::VehicleOutput));

    compiler::CompleteSourceInitialBinding relationship_initial;
    relationship_initial.owner_occurrence_id =
        std::string(kRelationshipOccurrence);
    relationship_initial.builder_inputs = config(
        std::string(yyz::kActivationRelationshipInitialSchemaId),
        {{"child_active", std::int64_t{0}},
         {"relationship_revision", std::int64_t{0}},
         {"transferred_mass", 0.0}});
    relationship_initial.field_sources = {
        {"child_active", source_ref("/initial/relationship/child-active")},
        {"relationship_revision",
         source_ref("/initial/relationship/revision")},
        {"transferred_mass",
         source_ref("/initial/relationship/transferred-mass")}};
    relationship_initial.source = source_ref("/initial/relationship");
    source.initial_bindings.push_back(std::move(relationship_initial));

    compiler::CompleteSourceInitialBinding parent_initial;
    parent_initial.owner_occurrence_id = std::string(kParentOccurrence);
    parent_initial.builder_inputs = config(
        std::string(yyz::kActivationParentInitialSchemaId),
        {{"available_mass", 100.0},
         {"revision", std::int64_t{0}},
         {"transferred_mass", 0.0}});
    parent_initial.field_sources = {
        {"available_mass", source_ref("/initial/parent/available-mass")},
        {"revision", source_ref("/initial/parent/revision")},
        {"transferred_mass",
         source_ref("/initial/parent/transferred-mass")}};
    parent_initial.source = source_ref("/initial/parent");
    source.initial_bindings.push_back(std::move(parent_initial));

    compiler::CompleteSourceInitialBinding child_initial;
    child_initial.owner_occurrence_id = std::string(kChildOccurrence);
    child_initial.builder_inputs = config(
        std::string(yyz::kActivationChildInitialSchemaId),
        {{"inertia", 0.0},
         {"initialized", std::int64_t{0}},
         {"mass", 0.0},
         {"position_meters", 0.0},
         {"revision", std::int64_t{0}},
         {"velocity_meters_per_second", 0.0}});
    child_initial.field_sources = {
        {"inertia", source_ref("/initial/child/inertia")},
        {"initialized", source_ref("/initial/child/initialized")},
        {"mass", source_ref("/initial/child/mass")},
        {"position_meters", source_ref("/initial/child/position")},
        {"revision", source_ref("/initial/child/revision")},
        {"velocity_meters_per_second",
         source_ref("/initial/child/velocity")}};
    child_initial.source = source_ref("/initial/child");
    source.initial_bindings.push_back(std::move(child_initial));

    compiler::CompleteSourceBinding child_to_consumer;
    child_to_consumer.binding_id = "binding.activation.child-to-consumer";
    child_to_consumer.provider_occurrence_id =
        std::string(kChildOccurrence);
    child_to_consumer.provider_port_id = "child-observation";
    child_to_consumer.consumer_occurrence_id =
        std::string(kConsumerOccurrence);
    child_to_consumer.consumer_port_id = "child-observation";
    child_to_consumer.source = source_ref("/bindings/child-to-consumer");
    source.bindings.push_back(std::move(child_to_consumer));

    compiler::CompleteSourceTransaction transaction;
    transaction.transaction_id = std::string(kTransaction);
    // Multi-scope membership is canonical ScopeKey order; the primary scope
    // is therefore the child Vehicle even though command authority remains
    // with the parent relationship owner.
    transaction.scope = child_scope;
    transaction.owner_occurrence_ids = {
        std::string(kRelationshipOccurrence), std::string(kParentOccurrence),
        std::string(kChildOccurrence)};
    transaction.source = source_ref("/transactions/activation");
    transaction.member_scopes = {child_scope, parent_scope};
    source.transactions.push_back(std::move(transaction));
    source.package_build_locks.push_back(
        {std::string(yyz::kInactiveChildActivationPackageId),
         std::string(yyz::kInactiveChildActivationPackageVersion),
         std::string(yyz::kInactiveChildActivationBuildFingerprint),
         source_ref("/packages/inactive-child-activation")});
    return source;
}

[[nodiscard]] compiler::CommandRouteSpec activation_route() {
    compiler::CommandRouteSpec route;
    route.route_id = std::string(kRoute);
    route.target_occurrence_id = std::string(kRelationshipOccurrence);
    route.payload_schema_id = std::string(yyz::kActivateChildCommandSchemaId);
    route.decision_authority = kDecisionAuthority;
    route.queue_capacity = 4U;
    route.queue_policy = contracts::CommandQueuePolicy::RejectNewest;
    route.supersession_policy =
        contracts::CommandSupersessionPolicy::LatestDuePerKey;
    route.effective_point = contracts::CommandEffectivePoint::TransactionStart;
    route.cutoff_policy =
        contracts::CommandCutoffPolicy::LedgerSequenceAtTransactionStart;
    route.event_schema_id =
        std::string(yyz::kActivationRequestedEventSchemaId);
    route.event_consumer_occurrence_id = std::string(kParentOccurrence);
    route.source = source_ref("/command-routes/activate-child");
    route.chained_event_consumers.push_back(
        {std::string(yyz::kActivationParentMappedEventSchemaId),
         std::string(kChildOccurrence),
         source_ref("/command-routes/activate-child/child-hop")});
    route.known_activation = compiler::KnownActivationSpec{
        std::string(kActivation),
        std::string(kParentEntity),
        std::string(kChildEntity),
        std::string(kRelationshipOccurrence),
        std::string(kParentOccurrence),
        std::string(kChildOccurrence),
        1U,
        source_ref("/activations/parent-child")};
    return route;
}

struct CompiledFixture {
    sdk::StaticPackageDescriptor package;
    sdk::StaticPackageImplementation implementation;
    compiler::CompleteStaticCompositionSource source;
    compiler::CompleteStaticCompilation base;
    compiler::CompleteStaticCompilation extended;
    contracts::ExecutionPlanImage image;
};

[[nodiscard]] CompiledFixture compile_fixture() {
    auto package = yyz::describe_inactive_child_activation_package();
    auto implementation =
        yyz::describe_inactive_child_activation_implementation();
    auto source = fixture_source();
    const auto base_outcome =
        compiler::compile_complete_execution_plan(source, {package});
    if (!base_outcome.succeeded()) {
        throw std::runtime_error(
            "inactive-child base compilation failed: " +
            diagnostic_text(base_outcome));
    }
    const auto extended_outcome = compiler::compile_command_event_routes(
        *base_outcome.value, {activation_route()});
    if (!extended_outcome.succeeded()) {
        throw std::runtime_error(
            "inactive-child route compilation failed: " +
            diagnostic_text(extended_outcome));
    }
    const auto linked = compiler::link_complete_execution_plan(
        extended_outcome.value->plan, extended_outcome.value->proofs,
        {implementation});
    if (!linked.succeeded()) {
        throw std::runtime_error(
            "inactive-child Image link failed: " + diagnostic_text(linked));
    }
    return {std::move(package), std::move(implementation), std::move(source),
            *base_outcome.value, *extended_outcome.value, *linked.value};
}

struct RuntimeControl {
    std::size_t reducer_failures_remaining = 0U;
    std::size_t parent_mapper_failures_remaining = 0U;
    std::size_t child_mapper_failures_remaining = 0U;
    std::size_t invalid_child_candidates_remaining = 0U;
    std::size_t child_consumer_failures_remaining = 0U;
    std::size_t seal_clone_failures_remaining = 0U;
    std::size_t relationship_reductions = 0U;
    std::size_t parent_mappings = 0U;
    std::size_t child_mappings = 0U;
    std::size_t child_projections = 0U;
    std::size_t child_consumptions = 0U;
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
        if constexpr (std::is_same_v<
                          Value, yyz::ActivationRelationshipObservation>) {
            ++copy_count_;
            if (copy_count_ % 2U == 0U && control_ != nullptr &&
                control_->seal_clone_failures_remaining != 0U) {
                --control_->seal_clone_failures_remaining;
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
        if constexpr (std::is_same_v<
                          Value, yyz::ActivationRelationshipState>) {
            return yyz::validate_activation_relationship_state(
                *static_cast<const Value*>(object));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::ActivationParentState>) {
            return yyz::validate_activation_parent_state(
                *static_cast<const Value*>(object));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::ActivationChildState>) {
            return yyz::validate_activation_child_state(
                *static_cast<const Value*>(object));
        } else {
            return true;
        }
    }

    [[nodiscard]] bool supports_nofail_swap() const noexcept override {
        return std::is_same_v<Value, yyz::ActivationRelationshipState> ||
               std::is_same_v<Value, yyz::ActivationParentState> ||
               std::is_same_v<Value, yyz::ActivationChildState>;
    }

    void nofail_swap(void* lhs, void* rhs) const noexcept override {
        if constexpr (std::is_same_v<
                          Value, yyz::ActivationRelationshipState>) {
            yyz::swap_activation_relationship_state(
                *static_cast<Value*>(lhs), *static_cast<Value*>(rhs));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::ActivationParentState>) {
            yyz::swap_activation_parent_state(*static_cast<Value*>(lhs),
                                              *static_cast<Value*>(rhs));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::ActivationChildState>) {
            yyz::swap_activation_child_state(*static_cast<Value*>(lhs),
                                             *static_cast<Value*>(rhs));
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
                    "inactive-child invocation threw"};
        }
    }

  private:
    kernel::SessionInvocationIdentity identity_;
    Function function_;
};

template <typename Value, typename View>
[[nodiscard]] kernel::SessionResult read_typed(
    const View& view, std::uint32_t handle,
    const Value*& value) noexcept {
    value = nullptr;
    kernel::SessionObjectIdentityView object;
    auto status = view.read(handle, object);
    if (!status) return status;
    if (object.type_identity != &typeid(Value) ||
        object.size_bytes != sizeof(Value) ||
        object.alignment_bytes != alignof(Value)) {
        return {kernel::SessionError::ObjectTypeMismatch, handle,
                "inactive-child typed object differs from Image layout"};
    }
    value = static_cast<const Value*>(object.address);
    return {};
}

template <typename Value>
[[nodiscard]] kernel::SessionResult write_output(
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
component_for_occurrence(const contracts::ExecutionPlanImage& image,
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
    std::uint32_t occurrence_handle, std::string_view obligation) noexcept {
    const auto found = std::find_if(
        image.callsites().begin(), image.callsites().end(),
        [&](const auto& value) {
            return value.occurrence_handle == occurrence_handle &&
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
    throw std::runtime_error("inactive-child typed slot is absent");
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
    throw std::runtime_error("inactive-child candidate slot is absent");
}

[[nodiscard]] std::uint32_t writer_for_slot(
    const contracts::PlanImageCallsite& callsite,
    std::uint32_t slot_handle) {
    require(callsite.output_slot_handles.size() ==
                callsite.output_writer_token_handles.size(),
            "inactive-child output/writer shape differs");
    for (std::size_t index = 0U;
         index < callsite.output_slot_handles.size(); ++index) {
        if (callsite.output_slot_handles[index] == slot_handle) {
            return callsite.output_writer_token_handles[index];
        }
    }
    throw std::runtime_error("inactive-child writer token is absent");
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

class ActivationReducer final : public kernel::SessionCommandReducerEntry {
  public:
    ActivationReducer(
        kernel::SessionCommandReducerIdentity identity,
        std::uint32_t state_block_handle,
        std::uint32_t candidate_slot_handle,
        std::uint32_t writer_token_handle,
        yyz::ActivationRelationshipDefinition definition,
        RuntimeControl& control) noexcept
        : identity_(identity), state_block_handle_(state_block_handle),
          candidate_slot_handle_(candidate_slot_handle),
          writer_token_handle_(writer_token_handle),
          definition_(definition), control_(&control) {}

    [[nodiscard]] kernel::SessionCommandReducerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] bool accepts_payload(
        kernel::InProcessValueView payload) const noexcept override {
        if (payload.type_identity != &typeid(yyz::ActivateChildCommand) ||
            payload.object == nullptr) {
            return false;
        }
        const auto& command =
            *static_cast<const yyz::ActivateChildCommand*>(payload.object);
        return std::isfinite(command.transfer_mass) &&
               command.transfer_mass == definition_.exact_transfer_mass;
    }

    [[nodiscard]] kernel::SessionResult reduce(
        const kernel::SessionCommandReductionContext& context,
        kernel::SessionCommandReductionResult& result)
        const noexcept override {
        ++control_->relationship_reductions;
        if (control_->reducer_failures_remaining != 0U) {
            --control_->reducer_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation reducer injected failure"};
        }
        if (context.payload().type_identity !=
                &typeid(yyz::ActivateChildCommand) ||
            context.decision_authority() != kDecisionAuthority) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.route_handle,
                    "activation reducer input identity differs"};
        }
        const yyz::ActivationRelationshipState* prior = nullptr;
        auto status = read_typed(context.committed(), state_block_handle_,
                                 prior);
        if (!status) return status;
        if (prior->child_active) {
            result.decision = kernel::CommandApplicationDecision::Rejected;
            result.application_code = 200U;
            return {};
        }
        const auto& command = *static_cast<const yyz::ActivateChildCommand*>(
            context.payload().object);
        const auto reduction =
            yyz::reduce_activate_child(definition_, *prior, command);
        if (!reduction.succeeded()) {
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation relationship reduction failed"};
        }
        status = write_candidate(context.candidates(), candidate_slot_handle_,
                                 writer_token_handle_,
                                 reduction.value().candidate);
        if (!status) return status;
        result.decision = kernel::CommandApplicationDecision::Applied;
        result.application_code = 100U;
        result.event_payload = kernel::InProcessOwnedValue::make(
            reduction.value().event);
        return {};
    }

  private:
    kernel::SessionCommandReducerIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t candidate_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    yyz::ActivationRelationshipDefinition definition_;
    RuntimeControl* control_ = nullptr;
};

class ParentMapper final : public kernel::SessionEventConsumerEntry {
  public:
    ParentMapper(kernel::SessionEventConsumerIdentity identity,
                 std::uint32_t state_block_handle,
                 std::uint32_t candidate_slot_handle,
                 std::uint32_t writer_token_handle,
                 yyz::ActivationParentDefinition definition,
                 RuntimeControl& control) noexcept
        : identity_(identity), state_block_handle_(state_block_handle),
          candidate_slot_handle_(candidate_slot_handle),
          writer_token_handle_(writer_token_handle),
          definition_(definition), control_(&control) {}

    [[nodiscard]] kernel::SessionEventConsumerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult consume(
        const kernel::SessionEventConsumptionContext& context,
        kernel::InProcessOwnedValue& output) const noexcept override {
        ++control_->parent_mappings;
        if (control_->parent_mapper_failures_remaining != 0U) {
            --control_->parent_mapper_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation parent mapper injected failure"};
        }
        if (context.payload().type_identity !=
            &typeid(yyz::ActivationRequested)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.delivery_handle,
                    "activation parent event type differs"};
        }
        const yyz::ActivationParentState* prior = nullptr;
        auto status = read_typed(context.committed(), state_block_handle_,
                                 prior);
        if (!status) return status;
        const auto& request =
            *static_cast<const yyz::ActivationRequested*>(
                context.payload().object);
        const auto mapping =
            yyz::map_activation_parent(definition_, *prior, request);
        if (!mapping.succeeded()) {
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation parent mapping failed"};
        }
        status = write_candidate(context.candidates(), candidate_slot_handle_,
                                 writer_token_handle_,
                                 mapping.value().candidate);
        if (!status) return status;
        output = kernel::InProcessOwnedValue::make(mapping.value().event);
        return {};
    }

  private:
    kernel::SessionEventConsumerIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t candidate_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    yyz::ActivationParentDefinition definition_;
    RuntimeControl* control_ = nullptr;
};

class ChildMapper final : public kernel::SessionEventConsumerEntry {
  public:
    ChildMapper(kernel::SessionEventConsumerIdentity identity,
                std::uint32_t state_block_handle,
                std::uint32_t candidate_slot_handle,
                std::uint32_t writer_token_handle,
                yyz::ActivationChildDefinition definition,
                RuntimeControl& control) noexcept
        : identity_(identity), state_block_handle_(state_block_handle),
          candidate_slot_handle_(candidate_slot_handle),
          writer_token_handle_(writer_token_handle),
          definition_(definition), control_(&control) {}

    [[nodiscard]] kernel::SessionEventConsumerIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] kernel::SessionResult consume(
        const kernel::SessionEventConsumptionContext& context,
        kernel::InProcessOwnedValue& output) const noexcept override {
        ++control_->child_mappings;
        if (control_->child_mapper_failures_remaining != 0U) {
            --control_->child_mapper_failures_remaining;
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation child mapper injected failure"};
        }
        if (context.payload().type_identity !=
            &typeid(yyz::ActivationParentMapped)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    identity_.delivery_handle,
                    "activation child event type differs"};
        }
        const yyz::ActivationChildState* prior = nullptr;
        auto status = read_typed(context.committed(), state_block_handle_,
                                 prior);
        if (!status) return status;
        const auto& mapped =
            *static_cast<const yyz::ActivationParentMapped*>(
                context.payload().object);
        const auto mapping =
            yyz::map_activation_child(definition_, *prior, mapped);
        if (!mapping.succeeded()) {
            return {kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "activation child mapping failed"};
        }
        auto candidate = mapping.value().candidate;
        if (control_->invalid_child_candidates_remaining != 0U) {
            --control_->invalid_child_candidates_remaining;
            candidate.mass = -1.0;
        }
        status = write_candidate(context.candidates(), candidate_slot_handle_,
                                 writer_token_handle_, candidate);
        if (!status) return status;
        output = kernel::InProcessOwnedValue::make(mapping.value().event);
        return {};
    }

  private:
    kernel::SessionEventConsumerIdentity identity_;
    std::uint32_t state_block_handle_ = 0U;
    std::uint32_t candidate_slot_handle_ = 0U;
    std::uint32_t writer_token_handle_ = 0U;
    yyz::ActivationChildDefinition definition_;
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
        const auto found = event_consumers_.find(handle);
        return found == event_consumers_.end() ? nullptr
                                               : found->second.get();
    }

    RuntimeControl control;
    std::uint32_t relationship_state_handle = 0U;
    std::uint32_t parent_state_handle = 0U;
    std::uint32_t child_state_handle = 0U;
    std::uint32_t child_output_slot_handle = 0U;
    std::uint32_t influence_output_slot_handle = 0U;
    std::uint32_t child_entity_handle = 0U;

  private:
    using MaterializerMap = std::unordered_map<
        std::uint32_t, std::unique_ptr<kernel::SessionObjectMaterializer>>;

    [[nodiscard]] static const kernel::SessionObjectMaterializer* find(
        const MaterializerMap& values,
        std::uint32_t handle) noexcept {
        const auto found = values.find(handle);
        return found == values.end() ? nullptr : found->second.get();
    }

    template <typename Value>
    void add_slot(const contracts::PlanImageSlot& slot, Value value) {
        slots_.emplace(
            slot.handle,
            std::make_unique<TypedMaterializer<Value>>(
                kernel::SessionMaterializerIdentity{
                    slot.handle, kernel::SessionObjectRole::CycleFrameValue,
                    0U, slot.codec_entry_handle},
                slot.layout_id, std::move(value), &control));
    }

    template <typename State>
    void add_initial(const contracts::PlanImageInitialBinding& binding,
                     const contracts::PlanImageStateBlock& block,
                     State state) {
        initial_states_.emplace(
            binding.handle,
            std::make_unique<TypedMaterializer<State>>(
                kernel::SessionMaterializerIdentity{
                    binding.handle,
                    kernel::SessionObjectRole::InitialStateValue,
                    binding.builder_entry_handle, block.codec_entry_handle},
                block.layout_id, std::move(state), &control));
    }

    void build(const contracts::ExecutionPlanImage& image) {
        require(image.revision() == 5U && image.entities().size() == 2U &&
                    image.known_activations().size() == 1U &&
                    image.runtime_components().size() == 4U &&
                    image.state_blocks().size() == 3U &&
                    image.command_routes().size() == 1U &&
                    image.event_deliveries().size() == 2U,
                "inactive-child Image shape differs from fixture contract");
        const auto* relationship = occurrence_for_model(
            image, yyz::kActivationRelationshipModelId);
        const auto* parent =
            occurrence_for_model(image, yyz::kActivationParentModelId);
        const auto* child =
            occurrence_for_model(image, yyz::kActivationChildModelId);
        const auto* consumer = occurrence_for_model(
            image, yyz::kActivationChildConsumerModelId);
        require(relationship != nullptr && parent != nullptr &&
                    child != nullptr && consumer != nullptr,
                "inactive-child occurrence set is incomplete");

        relationship_definition_ = require_numerical(
            yyz::build_activation_relationship_definition(
                canonical_configuration(
                    relationship->canonical_configuration)),
            "relationship Image configuration was rejected");
        parent_definition_ = require_numerical(
            yyz::build_activation_parent_definition(canonical_configuration(
                parent->canonical_configuration)),
            "parent Image configuration was rejected");
        child_definition_ = require_numerical(
            yyz::build_activation_child_definition(canonical_configuration(
                child->canonical_configuration)),
            "child Image configuration was rejected");
        consumer_definition_ = require_numerical(
            yyz::build_activation_child_consumer_definition(
                canonical_configuration(consumer->canonical_configuration)),
            "consumer Image configuration was rejected");

        for (const auto& entity : image.entities()) {
            if (entity.entity_id == kChildEntity) {
                child_entity_handle = entity.handle;
            }
        }
        for (const auto& component : image.runtime_components()) {
            runtime_components_.emplace(
                component.handle,
                std::make_unique<TypedMaterializer<RuntimeMarker>>(
                    kernel::SessionMaterializerIdentity{
                        component.handle,
                        kernel::SessionObjectRole::RuntimeCell,
                        component.runtime_cell_factory_entry_handle, 0U},
                    "gnc.layout.fixture.yyz.activation-runtime-marker@1",
                    RuntimeMarker{component.occurrence_handle}, &control));
        }
        build_slots(image);
        build_initial_states(image);
        install_projection(image, *relationship,
                           relationship_state_handle,
                           yyz::kActivationRelationshipObservationContractId);
        install_projection(image, *parent, parent_state_handle,
                           yyz::kActivationParentObservationContractId);
        install_projection(image, *child, child_state_handle,
                           yyz::kActivationChildObservationContractId);
        install_consumer(image, *consumer);
        install_command_path(image, *relationship, *parent, *child);
    }

    void build_slots(const contracts::ExecutionPlanImage& image) {
        for (const auto& slot : image.slots()) {
            if (slot.kind == contracts::PlanImageSlotKind::CommittedState ||
                slot.kind == contracts::PlanImageSlotKind::CandidateState) {
                continue;
            }
            if (slot.layout_id ==
                yyz::kActivationRelationshipObservationLayoutId) {
                add_slot(slot, yyz::ActivationRelationshipObservation{});
            } else if (slot.layout_id ==
                       yyz::kActivationParentObservationLayoutId) {
                add_slot(slot, yyz::ActivationParentObservation{});
            } else if (slot.layout_id ==
                       yyz::kActivationChildObservationLayoutId) {
                child_output_slot_handle = slot.handle;
                add_slot(slot, yyz::ActivationChildObservation{});
            } else if (slot.layout_id ==
                       yyz::kActivationChildInfluenceLayoutId) {
                influence_output_slot_handle = slot.handle;
                add_slot(slot, yyz::ActivationChildInfluence{});
            } else {
                throw std::runtime_error(
                    "unsupported inactive-child slot layout");
            }
        }
    }

    void build_initial_states(const contracts::ExecutionPlanImage& image) {
        for (const auto& block : image.state_blocks()) {
            const auto binding = std::find_if(
                image.initial_bindings().begin(),
                image.initial_bindings().end(), [&](const auto& value) {
                    return value.committed_state_slot_handle ==
                           block.committed_slot_handle;
                });
            require(binding != image.initial_bindings().end(),
                    "inactive-child initial binding is absent");
            if (block.layout_id ==
                yyz::kActivationRelationshipStateLayoutId) {
                relationship_state_handle = block.handle;
                add_initial(
                    *binding, block,
                    require_numerical(
                        yyz::build_activation_relationship_initial_state(
                            relationship_definition_, {0, 0.0, 0}),
                        "relationship initial state was rejected"));
            } else if (block.layout_id ==
                       yyz::kActivationParentStateLayoutId) {
                parent_state_handle = block.handle;
                add_initial(
                    *binding, block,
                    require_numerical(
                        yyz::build_activation_parent_initial_state(
                            parent_definition_, {100.0, 0.0, 0}),
                        "parent initial state was rejected"));
            } else if (block.layout_id ==
                       yyz::kActivationChildStateLayoutId) {
                child_state_handle = block.handle;
                add_initial(
                    *binding, block,
                    require_numerical(
                        yyz::build_activation_child_initial_state(
                            child_definition_, {0, 0.0, 0.0, 0.0, 0.0, 0}),
                        "child initial state was rejected"));
            } else {
                throw std::runtime_error(
                    "unsupported inactive-child state layout");
            }
        }
    }

    void install_projection(
        const contracts::ExecutionPlanImage& image,
        const contracts::PlanImageOccurrence& occurrence,
        std::uint32_t state_handle, std::string_view contract_id) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* publish = component == nullptr
                                  ? nullptr
                                  : callsite_for(image, occurrence.handle,
                                                 "PublishProjection");
        require(component != nullptr && publish != nullptr,
                "inactive-child projection callsite is absent");
        const auto output = slot_with_contract(
            image, publish->output_slot_handles, contract_id);
        const auto writer = writer_for_slot(*publish, output);
        const auto identity = kernel::SessionInvocationIdentity{
            publish->handle, component->handle, publish->entry_handle};
        if (contract_id ==
            yyz::kActivationRelationshipObservationContractId) {
            invocations_.emplace(
                publish->handle,
                std::make_unique<LambdaInvocation>(
                    identity, [this, state_handle, output, writer](
                                  const auto& context) {
                        const yyz::ActivationRelationshipState* state = nullptr;
                        auto status = read_typed(context.committed(),
                                                 state_handle, state);
                        if (!status) return status;
                        return write_output(
                            context.outputs(), output, writer,
                            yyz::project_activation_relationship(
                                *state, context.tick()));
                    }));
        } else if (contract_id ==
                   yyz::kActivationParentObservationContractId) {
            invocations_.emplace(
                publish->handle,
                std::make_unique<LambdaInvocation>(
                    identity, [this, state_handle, output, writer](
                                  const auto& context) {
                        const yyz::ActivationParentState* state = nullptr;
                        auto status = read_typed(context.committed(),
                                                 state_handle, state);
                        if (!status) return status;
                        return write_output(
                            context.outputs(), output, writer,
                            yyz::project_activation_parent(*state,
                                                           context.tick()));
                    }));
        } else {
            invocations_.emplace(
                publish->handle,
                std::make_unique<LambdaInvocation>(
                    identity, [this, state_handle, output, writer](
                                  const auto& context) {
                        ++control.child_projections;
                        const yyz::ActivationChildState* state = nullptr;
                        auto status = read_typed(context.committed(),
                                                 state_handle, state);
                        if (!status) return status;
                        return write_output(
                            context.outputs(), output, writer,
                            yyz::project_activation_child(*state,
                                                          context.tick()));
                    }));
        }
    }

    void install_consumer(
        const contracts::ExecutionPlanImage& image,
        const contracts::PlanImageOccurrence& occurrence) {
        const auto* component =
            component_for_occurrence(image, occurrence.handle);
        const auto* evaluate = component == nullptr
                                   ? nullptr
                                   : callsite_for(image, occurrence.handle,
                                                  "BoundaryEvaluation");
        require(component != nullptr && evaluate != nullptr,
                "inactive-child consumer callsite is absent");
        const auto input = slot_with_contract(
            image, evaluate->input_slot_handles,
            yyz::kActivationChildObservationContractId);
        const auto output = slot_with_contract(
            image, evaluate->output_slot_handles,
            yyz::kActivationChildInfluenceContractId);
        const auto writer = writer_for_slot(*evaluate, output);
        invocations_.emplace(
            evaluate->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    evaluate->handle, component->handle,
                    evaluate->entry_handle},
                [this, input, output, writer](const auto& context) {
                    ++control.child_consumptions;
                    if (control.child_consumer_failures_remaining != 0U) {
                        --control.child_consumer_failures_remaining;
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "activation child consumer injected failure"};
                    }
                    const yyz::ActivationChildObservation* observation =
                        nullptr;
                    auto status = read_typed(context.inputs(), input,
                                             observation);
                    if (!status) return status;
                    const auto value =
                        yyz::evaluate_activation_child_consumer(
                            consumer_definition_, *observation);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed, output,
                            "activation child consumer rejected observation"};
                    }
                    return write_output(context.outputs(), output, writer,
                                        value.value());
                }));
    }

    void install_command_path(
        const contracts::ExecutionPlanImage& image,
        const contracts::PlanImageOccurrence& relationship,
        const contracts::PlanImageOccurrence& parent,
        const contracts::PlanImageOccurrence& child) {
        const auto& route = image.command_routes().front();
        require(route.event_delivery_handles.size() == 2U,
                "activation route is not exactly two-hop");
        const auto* relationship_component =
            component_for_occurrence(image, relationship.handle);
        const auto* parent_component =
            component_for_occurrence(image, parent.handle);
        const auto* child_component =
            component_for_occurrence(image, child.handle);
        const auto* reduction = callsite_for(
            image, relationship.handle, "CommandReduction");
        const auto* parent_mapping = callsite_for(
            image, parent.handle, "EventConsumption");
        const auto* child_mapping = callsite_for(
            image, child.handle, "EventConsumption");
        require(relationship_component != nullptr &&
                    parent_component != nullptr && child_component != nullptr &&
                    reduction != nullptr && parent_mapping != nullptr &&
                    child_mapping != nullptr,
                "activation command callsite set is incomplete");
        const auto relationship_candidate =
            candidate_slot(image, reduction->output_slot_handles);
        const auto parent_candidate =
            candidate_slot(image, parent_mapping->output_slot_handles);
        const auto child_candidate =
            candidate_slot(image, child_mapping->output_slot_handles);
        reducer_ = std::make_unique<ActivationReducer>(
            kernel::SessionCommandReducerIdentity{
                route.handle, reduction->handle,
                relationship_component->handle, reduction->entry_handle,
                &typeid(yyz::ActivateChildCommand),
                &typeid(yyz::ActivationRequested)},
            relationship_state_handle, relationship_candidate,
            writer_for_slot(*reduction, relationship_candidate),
            relationship_definition_, control);

        const auto first_delivery = std::find_if(
            image.event_deliveries().begin(),
            image.event_deliveries().end(), [&](const auto& delivery) {
                return delivery.handle == route.event_delivery_handles[0];
            });
        const auto second_delivery = std::find_if(
            image.event_deliveries().begin(),
            image.event_deliveries().end(), [&](const auto& delivery) {
                return delivery.handle == route.event_delivery_handles[1];
            });
        require(first_delivery != image.event_deliveries().end() &&
                    second_delivery != image.event_deliveries().end(),
                "activation event delivery is absent");
        event_consumers_.emplace(
            first_delivery->handle,
            std::make_unique<ParentMapper>(
                kernel::SessionEventConsumerIdentity{
                    first_delivery->handle, parent_mapping->handle,
                    parent_component->handle, parent_mapping->entry_handle,
                    &typeid(yyz::ActivationRequested),
                    &typeid(yyz::ActivationParentMapped)},
                parent_state_handle, parent_candidate,
                writer_for_slot(*parent_mapping, parent_candidate),
                parent_definition_, control));
        event_consumers_.emplace(
            second_delivery->handle,
            std::make_unique<ChildMapper>(
                kernel::SessionEventConsumerIdentity{
                    second_delivery->handle, child_mapping->handle,
                    child_component->handle, child_mapping->entry_handle,
                    &typeid(yyz::ActivationParentMapped),
                    &typeid(yyz::ActivationCompleted)},
                child_state_handle, child_candidate,
                writer_for_slot(*child_mapping, child_candidate),
                child_definition_, control));
    }

    MaterializerMap runtime_components_;
    MaterializerMap slots_;
    MaterializerMap initial_states_;
    std::unordered_map<std::uint32_t,
                       std::unique_ptr<kernel::SessionInvocationEntry>>
        invocations_;
    std::unique_ptr<ActivationReducer> reducer_;
    std::unordered_map<std::uint32_t,
                       std::unique_ptr<kernel::SessionEventConsumerEntry>>
        event_consumers_;
    yyz::ActivationRelationshipDefinition relationship_definition_;
    yyz::ActivationParentDefinition parent_definition_;
    yyz::ActivationChildDefinition child_definition_;
    yyz::ActivationChildConsumerDefinition consumer_definition_;
};

struct LiveSession {
    std::shared_ptr<const contracts::ExecutionPlanImage> image;
    std::shared_ptr<FixtureProvider> provider;
    std::unique_ptr<kernel::Session> session;
    kernel::RunId run_id;
};

[[nodiscard]] LiveSession initialize_session(
    const contracts::ExecutionPlanImage& image, std::string run_id,
    std::shared_ptr<FixtureProvider> provider = nullptr,
    std::shared_ptr<const contracts::ExecutionPlanImage> shared_image =
        nullptr) {
    if (shared_image == nullptr) {
        shared_image =
            std::make_shared<const contracts::ExecutionPlanImage>(image);
    }
    if (provider == nullptr) {
        provider = std::make_shared<FixtureProvider>(*shared_image);
    }
    auto creation = kernel::create_session(shared_image, provider);
    require(static_cast<bool>(creation),
            "inactive-child Session creation failed");
    auto session = std::move(creation.session);
    kernel::RunId id(std::move(run_id));
    const auto initialized = session->initialize(
        {id, kernel::exact_run_binding(*shared_image)});
    require(static_cast<bool>(initialized),
            std::string("inactive-child Session initialization failed: ") +
                std::to_string(static_cast<unsigned>(
                    initialized.result.error)) +
                " " + std::string(initialized.result.detail));
    return {std::move(shared_image), std::move(provider),
            std::move(session), std::move(id)};
}

[[nodiscard]] kernel::CommandRequest activation_command(
    const LiveSession& live, std::string id,
    std::int64_t effective_tick = 0) {
    const auto& route = live.image->command_routes().front();
    return {kernel::CommandId(std::move(id)), live.run_id, route.handle,
            route.target_runtime_component_handle, route.payload_schema_id,
            route.decision_authority, effective_tick, std::nullopt,
            "activate-child",
            kernel::InProcessOwnedValue::make(
                yyz::ActivateChildCommand{kTransferMass})};
}

template <typename Value>
[[nodiscard]] Value read_committed_state(
    const LiveSession& live, std::uint32_t state_block_handle) {
    kernel::SessionObjectIdentityView view;
    const auto status = kernel::qualification::SessionAccess::read_committed(
        *live.session, state_block_handle, view);
    require(status && view.type_identity == &typeid(Value),
            "inactive-child committed state is unavailable");
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
            "inactive-child committed output is unavailable");
    return *static_cast<const Value*>(view.address);
}

[[nodiscard]] bool has_output(const LiveSession& live,
                              std::uint32_t slot_handle) noexcept {
    const auto outputs = live.session->committed_outputs();
    return std::any_of(
        outputs.begin(), outputs.end(),
        [slot_handle](const auto& output) {
            return output.slot_handle == slot_handle;
        });
}

void require_dormant(const LiveSession& live) {
    const auto relationship =
        read_committed_state<yyz::ActivationRelationshipState>(
            live, live.provider->relationship_state_handle);
    const auto parent = read_committed_state<yyz::ActivationParentState>(
        live, live.provider->parent_state_handle);
    const auto child = read_committed_state<yyz::ActivationChildState>(
        live, live.provider->child_state_handle);
    const auto active =
        live.session->entity_active(live.provider->child_entity_handle);
    require(active.has_value() && !*active &&
                live.session->topology_revision() == 0U &&
                !relationship.child_active &&
                relationship.relationship_revision == 0U &&
                near(parent.available_mass, 100.0) && parent.revision == 0U &&
                !child.initialized && child.revision == 0U,
            "inactive-child dormant boundary is inconsistent");
}

void require_activated(const LiveSession& live) {
    const auto relationship =
        read_committed_state<yyz::ActivationRelationshipState>(
            live, live.provider->relationship_state_handle);
    const auto parent = read_committed_state<yyz::ActivationParentState>(
        live, live.provider->parent_state_handle);
    const auto child = read_committed_state<yyz::ActivationChildState>(
        live, live.provider->child_state_handle);
    const auto active =
        live.session->entity_active(live.provider->child_entity_handle);
    require(active.has_value() && *active &&
                live.session->topology_revision() == 1U &&
                relationship.child_active &&
                near(relationship.transferred_mass, kTransferMass) &&
                relationship.relationship_revision == 1U &&
                near(parent.available_mass, 100.0 - kTransferMass) &&
                near(parent.transferred_mass, kTransferMass) &&
                parent.revision == 1U && child.initialized &&
                near(child.position_meters, kChildPosition) &&
                near(child.velocity_meters_per_second, kChildVelocity) &&
                near(child.mass, kTransferMass) &&
                near(child.inertia, kTransferMass * kInertiaPerMass) &&
                child.revision == 1U,
            "inactive-child activation boundary is inconsistent");
}

void verify_compiler_and_image_contract(const CompiledFixture& fixture) {
    require(fixture.package.models.size() == 4U &&
                fixture.implementation.state_layouts.size() == 3U &&
                fixture.implementation.value_layouts.size() == 4U &&
                fixture.base.plan.entities.size() == 2U &&
                fixture.base.plan.known_activations.empty() &&
                fixture.extended.plan.entities.size() == 2U &&
                fixture.extended.plan.known_activations.size() == 1U &&
                fixture.extended.plan.command_routes.size() == 1U &&
                fixture.extended.plan.event_deliveries.size() == 2U &&
                fixture.image.revision() == 5U &&
                fixture.image.entities().size() == 2U &&
                fixture.image.known_activations().size() == 1U &&
                fixture.image.runtime_components().size() == 4U &&
                fixture.image.state_blocks().size() == 3U &&
                fixture.image.command_routes().front()
                        .event_delivery_handles.size() == 2U,
            "inactive-child compile/Image cardinality differs");
    const auto& activation = fixture.image.known_activations().front();
    require(activation.mapping_event_delivery_handles.size() == 2U &&
                activation.required_candidate_slot_handles.size() == 3U &&
                activation.gated_callsite_handles.size() == 2U &&
                activation.gated_output_slot_handles.size() == 2U &&
                activation.topology_revision_delta == 1U &&
                fixture.image.event_deliveries()[0].delivery ==
                    contracts::EventDeliveryPoint::LaterPhaseSameTick &&
                fixture.image.event_deliveries()[1].delivery ==
                    contracts::EventDeliveryPoint::
                        OrderedSamePhaseSameTick &&
                fixture.image.event_deliveries()[1]
                        .predecessor_event_delivery_handle ==
                    fixture.image.event_deliveries()[0].handle,
            "inactive-child numeric activation mapping differs");
    require(fixture.extended.proofs.proof_index_hash ==
                compiler::complete_plan_detail::proof_hash(
                    fixture.extended.proofs) &&
                fixture.extended.plan.descriptor_semantic_hash !=
                    fixture.base.plan.descriptor_semantic_hash,
            "inactive-child proof/hash extension is absent");
    for (const auto& entity : fixture.extended.plan.entities) {
        const auto proof_id = "proof/entity-lifecycle/" + entity.entity_id;
        const auto proof = std::find_if(
            fixture.extended.proofs.records.begin(),
            fixture.extended.proofs.records.end(), [&](const auto& record) {
                return record.proof_id == proof_id;
            });
        require(proof != fixture.extended.proofs.records.end() &&
                    proof->source_refs.size() == 2U,
                "entity identity/lifecycle provenance is incomplete");
    }
    const auto require_conformance = [&](std::string_view element,
                                         std::uint32_t handle) {
        const auto found = std::find_if(
            fixture.image.conformance().begin(),
            fixture.image.conformance().end(), [&](const auto& value) {
                return value.plan_element_id == element;
            });
        require(found != fixture.image.conformance().end() &&
                    found->source_refs.size() == 1U &&
                    found->proof_ids.size() == 1U &&
                    found->image_handles == std::vector<std::uint32_t>{handle},
                "command/event source-proof-Image conformance is incomplete");
    };
    require_conformance(
        fixture.extended.plan.command_routes.front().plan_element_id,
        fixture.image.command_routes().front().handle);
    for (std::size_t index = 0U;
         index < fixture.extended.plan.event_deliveries.size(); ++index) {
        require_conformance(
            fixture.extended.plan.event_deliveries[index].plan_element_id,
            fixture.image.event_deliveries()[index].handle);
    }
}

void verify_source_negatives(const CompiledFixture& fixture) {
    const auto expect_route_failure = [&](compiler::CompleteStaticCompilation base,
                                          compiler::CommandRouteSpec route,
                                          std::string_view message) {
        const auto outcome = compiler::compile_command_event_routes(
            std::move(base), {std::move(route)});
        require(!outcome.succeeded(), message);
    };

    const auto incomplete_link = compiler::link_complete_execution_plan(
        fixture.base.plan, fixture.base.proofs, {fixture.implementation});
    require(!incomplete_link.succeeded() &&
                !incomplete_link.diagnostics.empty(),
            "inactive entity plan linked without an activation route");

    auto active_child_source = fixture.source;
    active_child_source.entities[1].lifecycle =
        compiler::EntityLifecycle::ActiveAtInitialize;
    const auto active_child_base = compiler::compile_complete_execution_plan(
        active_child_source, {fixture.package});
    require(active_child_base.succeeded(),
            "active-child negative base unexpectedly failed");
    expect_route_failure(*active_child_base.value, activation_route(),
                         "activation accepted an initially active child");

    auto no_activation = activation_route();
    no_activation.known_activation.reset();
    expect_route_failure(fixture.base, std::move(no_activation),
                         "two-hop route without activation mapping passed");

    auto wrong_schema = activation_route();
    wrong_schema.chained_event_consumers.front().event_schema_id =
        std::string(yyz::kActivationRequestedEventSchemaId);
    expect_route_failure(fixture.base, std::move(wrong_schema),
                         "activation accepted a broken typed mapping edge");

    auto missing_owner_source = fixture.source;
    missing_owner_source.transactions.front().owner_occurrence_ids.pop_back();
    const auto missing_owner_base =
        compiler::compile_complete_execution_plan(missing_owner_source,
                                                  {fixture.package});
    if (missing_owner_base.succeeded()) {
        expect_route_failure(*missing_owner_base.value, activation_route(),
                             "activation accepted a transaction missing child owner");
    } else {
        require(!missing_owner_base.diagnostics.empty(),
                "missing-owner negative failed without a diagnostic");
    }

    auto wrong_parent = activation_route();
    wrong_parent.known_activation->parent_owner_occurrence_id =
        std::string(kRelationshipOccurrence);
    expect_route_failure(fixture.base, std::move(wrong_parent),
                         "activation accepted the wrong parent owner");

    auto extra_entity_source = fixture.source;
    auto extra_entity = extra_entity_source.entities.front();
    extra_entity.entity_id = "entity.activation.extra";
    extra_entity.identity_source =
        source_ref("/entities/activation-extra/identity");
    extra_entity.lifecycle_source =
        source_ref("/entities/activation-extra/lifecycle");
    extra_entity_source.entities.push_back(std::move(extra_entity));
    const auto extra_entity_base = compiler::compile_complete_execution_plan(
        extra_entity_source, {fixture.package});
    require(extra_entity_base.succeeded(),
            "extra-entity negative base unexpectedly failed");
    expect_route_failure(*extra_entity_base.value, activation_route(),
                         "the exact two-entity activation slice accepted an extra entity");
}

void require_image_initialization_failure(
    contracts::ExecutionPlanImageData data,
    std::string_view message) {
    data.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(data);
    auto image = std::make_shared<const contracts::ExecutionPlanImage>(
        contracts::ExecutionPlanImage::freeze(std::move(data)));
    auto provider = std::make_shared<FixtureProvider>(*image);
    auto creation = kernel::create_session(image, provider);
    require(static_cast<bool>(creation),
            "mutated activation Image did not reach Session validation");
    const auto initialized = creation.session->initialize(
        {kernel::RunId("run.activation.invalid-image"),
         kernel::exact_run_binding(*image)});
    require(!initialized &&
                initialized.result.error ==
                    kernel::SessionError::InvalidImageStructure,
            message);
}

void verify_image_negatives(const CompiledFixture& fixture) {
    auto lifecycle = fixture.image.data();
    const auto child_entity = std::find_if(
        lifecycle.entities.begin(), lifecycle.entities.end(),
        [](const auto& entity) { return entity.entity_id == kChildEntity; });
    require(child_entity != lifecycle.entities.end(),
            "activation Image omitted the child entity");
    child_entity->active_at_initialize = true;
    require_image_initialization_failure(
        std::move(lifecycle),
        "activation Image accepted an initially active child");

    auto candidates = fixture.image.data();
    candidates.known_activations.front()
        .required_candidate_slot_handles.pop_back();
    require_image_initialization_failure(
        std::move(candidates),
        "activation Image accepted an incomplete candidate set");

    auto deliveries = fixture.image.data();
    std::reverse(deliveries.known_activations.front()
                     .mapping_event_delivery_handles.begin(),
                 deliveries.known_activations.front()
                     .mapping_event_delivery_handles.end());
    require_image_initialization_failure(
        std::move(deliveries),
        "activation Image accepted reversed mapping deliveries");

    auto route_order = fixture.image.data();
    std::reverse(route_order.command_routes.front()
                     .event_delivery_handles.begin(),
                 route_order.command_routes.front()
                     .event_delivery_handles.end());
    require_image_initialization_failure(
        std::move(route_order),
        "activation Image accepted reversed route delivery order");

    auto predecessor = fixture.image.data();
    predecessor.event_deliveries[1].predecessor_event_delivery_handle = 0U;
    require_image_initialization_failure(
        std::move(predecessor),
        "activation Image accepted a missing delivery predecessor");

    auto gated_callsite = fixture.image.data();
    gated_callsite.known_activations.front().gated_callsite_handles.pop_back();
    require_image_initialization_failure(
        std::move(gated_callsite),
        "activation Image accepted an incomplete callsite gate");

    auto gated_output = fixture.image.data();
    gated_output.known_activations.front().gated_output_slot_handles.pop_back();
    require_image_initialization_failure(
        std::move(gated_output),
        "activation Image accepted an incomplete output gate");

    auto revision = fixture.image.data();
    revision.known_activations.front().topology_revision_delta = 2U;
    require_image_initialization_failure(
        std::move(revision),
        "activation Image accepted a non-unit topology revision");
}

void verify_happy_path(const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.activation.happy");
    const auto runtime_count = live.session->runtime_cell_count();
    require(runtime_count == image.runtime_components().size() &&
                live.provider->child_entity_handle != 0U &&
                !live.session->entity_active(
                     (std::numeric_limits<std::uint32_t>::max)())
                     .has_value(),
            "inactive child was not fully materialized or unknown entity was silent");
    require_dormant(live);
    require(live.provider->control.child_projections == 0U &&
                live.provider->control.child_consumptions == 0U &&
                !has_output(live, live.provider->child_output_slot_handle) &&
                !has_output(live, live.provider->influence_output_slot_handle),
            "inactive child participated in opening Publish");

    require(static_cast<bool>(live.session->submit_command(
                activation_command(live, "activate"))),
            "activation command was not queued");
    const auto activated = live.session->execute_step();
    require(activated.status == kernel::StepStatus::Committed &&
                activated.tick_before == 0 && activated.tick_after == 1 &&
                live.session->runtime_cell_count() == runtime_count &&
                live.provider->control.relationship_reductions == 1U &&
                live.provider->control.parent_mappings == 1U &&
                live.provider->control.child_mappings == 1U &&
                live.provider->control.child_projections == 0U &&
                live.provider->control.child_consumptions == 0U &&
                live.session->command_application_receipts().size() == 1U &&
                live.session->committed_events().size() == 2U &&
                !has_output(live, live.provider->child_output_slot_handle) &&
                !has_output(live, live.provider->influence_output_slot_handle),
            "activation tick did not commit one atomic two-hop event chain");
    require_activated(live);

    const auto duplicate = live.session->submit_command(
        activation_command(live, "activate-again", 1));
    require(static_cast<bool>(duplicate),
            "repeat activation command was not admitted for owner decision");
    const auto next = live.session->execute_step();
    require(next.status == kernel::StepStatus::Committed &&
                next.tick_before == 1 && next.tick_after == 2 &&
                live.provider->control.child_projections == 1U &&
                live.provider->control.child_consumptions == 1U &&
                has_output(live, live.provider->child_output_slot_handle) &&
                has_output(live, live.provider->influence_output_slot_handle) &&
                live.session->runtime_cell_count() == runtime_count &&
                live.session->topology_revision() == 1U &&
                live.session->committed_events().size() == 2U &&
                live.session->command_application_receipts().size() == 2U &&
                live.session->command_application_receipts().back().decision ==
                    kernel::CommandApplicationDecision::Rejected,
            "active child did not start on the next frozen Publish cadence");
    const auto influence =
        read_committed_output<yyz::ActivationChildInfluence>(
            live, live.provider->influence_output_slot_handle);
    require(near(influence.weighted_momentum,
                 kInfluenceGain * kTransferMass * kChildVelocity) &&
                influence.child_revision == 1U,
            "real child consumer did not observe activated state");

    const auto terminal = live.session->execute_step();
    require(terminal.status == kernel::StepStatus::Terminated &&
                live.session->state() == kernel::SessionState::Completed &&
                live.session->topology_revision() == 1U &&
                live.session->committed_events().size() == 2U &&
                live.session->command_application_receipts().size() == 2U &&
                live.session->command_application_receipts().back().decision ==
                    kernel::CommandApplicationDecision::Rejected,
            "repeat activation changed topology or escaped owner rejection");

    const auto reset = live.session->reset(
        {kernel::RunId("run.activation.reset"),
         kernel::exact_run_binding(*live.image)});
    require(reset && live.session->state() == kernel::SessionState::Initialized &&
                live.session->runtime_cell_count() == runtime_count &&
                live.session->committed_events().empty() &&
                live.session->command_application_receipts().empty() &&
                live.provider->control.child_projections == 2U,
            "activation reset did not preserve materialization and clear evidence");
    require_dormant(live);
    require(!has_output(live, live.provider->child_output_slot_handle) &&
                !has_output(live, live.provider->influence_output_slot_handle),
            "reset published inactive child output");
}

template <typename Arm>
void verify_retry_case(const contracts::ExecutionPlanImage& image,
                       std::string run_id, Arm arm,
                       std::string_view message) {
    auto live = initialize_session(image, std::move(run_id));
    require(static_cast<bool>(live.session->submit_command(
                activation_command(live, "activate"))),
            "retry fixture command was not queued");
    arm(live.provider->control);
    const auto failed = live.session->execute_step();
    require(failed.status == kernel::StepStatus::Failed &&
                failed.primary_diagnostic.has_value() &&
                failed.primary_diagnostic->validity_effect ==
                    contracts::EvidenceValidity::Unknown &&
                failed.primary_diagnostic->disposition ==
                    kernel::RuntimeFailureDisposition::RetryStep &&
                live.session->state() == kernel::SessionState::Initialized &&
                live.session->committed_epoch() == 0U &&
                live.session->committed_tick() == 0 &&
                live.session->pending_command_count() == 1U &&
                live.session->command_application_receipts().empty() &&
                live.session->committed_events().empty(),
            message);
    require_dormant(live);
    const auto retry = live.session->execute_step();
    require(retry.status == kernel::StepStatus::Committed &&
                live.session->pending_command_count() == 0U &&
                live.session->command_application_receipts().size() == 1U &&
                live.session->committed_events().size() == 2U,
            "legal activation retry did not commit exactly once");
    require_activated(live);
}

void verify_retry_and_fatal_boundaries(
    const contracts::ExecutionPlanImage& image) {
    verify_retry_case(
        image, "run.activation.retry.reducer",
        [](RuntimeControl& control) {
            control.reducer_failures_remaining = 1U;
        },
        "activation reducer failure leaked staged state or became fatal");
    verify_retry_case(
        image, "run.activation.retry.parent",
        [](RuntimeControl& control) {
            control.parent_mapper_failures_remaining = 1U;
        },
        "activation parent mapper failure leaked staged state or became fatal");
    verify_retry_case(
        image, "run.activation.retry.child",
        [](RuntimeControl& control) {
            control.child_mapper_failures_remaining = 1U;
        },
        "activation child mapper failure leaked staged state or became fatal");
    verify_retry_case(
        image, "run.activation.retry.seal",
        [](RuntimeControl& control) {
            control.seal_clone_failures_remaining = 1U;
        },
        "activation seal failure leaked staged state or became fatal");

    {
        auto live = initialize_session(image, "run.activation.precommit");
        require(static_cast<bool>(live.session->submit_command(
                    activation_command(live, "activate"))),
                "precommit-failure command was not queued");
        kernel::qualification::SessionAccess::fail_activation_precommit(
            *live.session);
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    live.session->state() == kernel::SessionState::Failed &&
                    failed.result.error ==
                        kernel::SessionError::TransactionPrecommitFailed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::Precommit &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    live.session->committed_epoch() == 0U &&
                    live.session->committed_tick() == 0 &&
                    live.session->topology_revision() == 0U &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty(),
                "activation precommit failure partially committed or retried");
        require_dormant(live);
    }

    {
        auto live = initialize_session(image, "run.activation.invalid-child");
        require(static_cast<bool>(live.session->submit_command(
                    activation_command(live, "activate"))),
                "invalid-candidate command was not queued");
        live.provider->control.invalid_child_candidates_remaining = 1U;
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    live.session->state() == kernel::SessionState::Failed &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid &&
                    live.session->committed_epoch() == 0U &&
                    live.session->committed_tick() == 0 &&
                    live.session->command_application_receipts().empty() &&
                    live.session->committed_events().empty(),
                "invalid child candidate was retryable or partially committed");
        require_dormant(live);
    }

    {
        auto live = initialize_session(image, "run.activation.consumer-fatal");
        require(static_cast<bool>(live.session->submit_command(
                    activation_command(live, "activate"))) &&
                    live.session->execute_step().status ==
                        kernel::StepStatus::Committed,
                "consumer-fatal fixture did not activate");
        live.provider->control.child_consumer_failures_remaining = 1U;
        const auto failed = live.session->execute_step();
        require(failed.status == kernel::StepStatus::Failed &&
                    live.session->state() == kernel::SessionState::Failed &&
                    live.session->topology_revision() == 1U &&
                    failed.primary_diagnostic.has_value() &&
                    failed.primary_diagnostic->stage ==
                        kernel::RuntimeDiagnosticStage::BoundaryInvocation &&
                    failed.primary_diagnostic->validity_effect ==
                        contracts::EvidenceValidity::Invalid,
                "ordinary active-child consumer failure remained retryable");
        require_activated(live);
    }
}

void verify_isolation_determinism_and_checkpoint(
    const contracts::ExecutionPlanImage& image) {
    auto shared_image =
        std::make_shared<const contracts::ExecutionPlanImage>(image);
    auto provider = std::make_shared<FixtureProvider>(*shared_image);
    auto first = initialize_session(image, "run.activation.shared.first",
                                    provider, shared_image);
    auto second = initialize_session(image, "run.activation.shared.second",
                                     provider, shared_image);
    const auto runtime_count = first.session->runtime_cell_count();
    require(static_cast<bool>(first.session->submit_command(
                activation_command(first, "activate-first"))) &&
                first.session->execute_step().status ==
                    kernel::StepStatus::Committed,
            "first shared-provider activation failed");
    require_activated(first);
    require_dormant(second);
    require(first.session->runtime_cell_count() == runtime_count &&
                second.session->runtime_cell_count() == runtime_count,
            "activation changed or shared runtime materialization");
    require(static_cast<bool>(second.session->submit_command(
                activation_command(second, "activate-second"))) &&
                second.session->execute_step().status ==
                    kernel::StepStatus::Committed,
            "second shared-provider activation failed");
    require_activated(second);
    const auto first_child =
        read_committed_state<yyz::ActivationChildState>(
            first, provider->child_state_handle);
    const auto second_child =
        read_committed_state<yyz::ActivationChildState>(
            second, provider->child_state_handle);
    require(first_child.initialized == second_child.initialized &&
                near(first_child.position_meters,
                     second_child.position_meters) &&
                near(first_child.mass, second_child.mass) &&
                first.session->committed_epoch() ==
                    second.session->committed_epoch(),
            "independent activation Sessions are nondeterministic");

    const auto checkpoint = first.session->checkpoint();
    require(!checkpoint &&
                checkpoint.result.error ==
                    kernel::SessionError::UnsupportedCheckpointCapability &&
                first.session->state() == kernel::SessionState::Initialized &&
                first.session->topology_revision() == 1U,
            "activation checkpoint did not fail closed without mutation");
    require_activated(first);

    auto created = kernel::create_session(shared_image, provider);
    require(static_cast<bool>(created),
            "activation restore target creation failed");
    const auto restore = created.session->restore(
        {kernel::RunId("run.activation.restore"),
         kernel::exact_run_binding(*shared_image), nullptr});
    require(!restore &&
                restore.result.error ==
                    kernel::SessionError::UnsupportedCheckpointCapability &&
                created.session->state() == kernel::SessionState::Created &&
                created.session->runtime_cell_count() == 0U,
            "activation restore did not fail closed before materialization");
}

void verify_product_contracts() {
    const auto bad_relationship =
        yyz::build_activation_relationship_definition(config(
            std::string(yyz::kActivationRelationshipConfigSchemaId),
            {{"exact_transfer_mass",
              (std::numeric_limits<double>::quiet_NaN)()}}));
    require(!bad_relationship.succeeded(),
            "non-finite activation relationship configuration was accepted");
    const auto bad_child = yyz::map_activation_child(
        {kInertiaPerMass}, {},
        {kChildPosition, kChildVelocity, kTransferMass, 1.0, 1U, 1U});
    require(!bad_child.succeeded(),
            "child accepted incomplete parent mapping facts");
}

void run() {
    const auto fixture = compile_fixture();
    verify_compiler_and_image_contract(fixture);
    verify_source_negatives(fixture);
    verify_image_negatives(fixture);
    verify_product_contracts();
    verify_happy_path(fixture.image);
    verify_retry_and_fatal_boundaries(fixture.image);
    verify_isolation_determinism_and_checkpoint(fixture.image);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_inactive_child_activation_probe "
                     "--self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 inactive-child atomic activation: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 inactive-child atomic activation: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
