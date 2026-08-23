#include "gnc/compiler/complete_execution_plan.hpp"
#include "gnc/kernel/session.hpp"
#include "support/session_qualification_access.hpp"

#include <yyz/two_entity_causal.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
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

constexpr std::string_view kEntityA = "entity.fixture.yyz.causal.a@1";
constexpr std::string_view kEntityB = "entity.fixture.yyz.causal.b@1";
constexpr std::string_view kClock = "clock.fixture.yyz.causal.10hz@1";
constexpr std::string_view kAOccurrence = "a.entity-a-truth";
constexpr std::string_view kLinkOccurrence = "b.one-tick-link";
constexpr std::string_view kBOccurrence = "c.entity-b-truth";
constexpr std::string_view kTransaction = "transaction.fixture.yyz.causal";
constexpr std::int64_t kTerminalTick = 3;
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
    return {"fixture://yyz/two-entity-causal", std::move(path)};
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
    sdk::CanonicalConfigBlock configuration,
    std::string entity_id, const compiler::ScopeKey& scope) {
    compiler::CompleteSourceOccurrence result;
    result.occurrence_id = std::move(occurrence_id);
    result.model_id = std::move(model_id);
    result.model_version = std::string(yyz::kTwoEntityCausalModelVersion);
    result.source = source_ref("/occurrences/" + result.occurrence_id);
    result.subject_entity_id = std::move(entity_id);
    result.subject_source =
        source_ref("/occurrences/" + result.occurrence_id + "/subject");
    result.scope = scope;
    result.scope_source =
        source_ref("/occurrences/" + result.occurrence_id + "/scope");
    result.placement = sdk::ModelPlacement::VehicleOutput;
    result.placement_source =
        source_ref("/occurrences/" + result.occurrence_id + "/placement");
    result.configuration = std::move(configuration);
    result.configuration_source = source_ref(
        "/occurrences/" + result.occurrence_id + "/configuration");
    append_field_sources(result);
    return result;
}

[[nodiscard]] compiler::CompleteSourceBinding binding(
    std::string id, std::string provider_occurrence,
    std::string provider_port, std::string consumer_occurrence,
    std::string consumer_port) {
    compiler::CompleteSourceBinding result;
    result.binding_id = std::move(id);
    result.provider_occurrence_id = std::move(provider_occurrence);
    result.provider_port_id = std::move(provider_port);
    result.consumer_occurrence_id = std::move(consumer_occurrence);
    result.consumer_port_id = std::move(consumer_port);
    result.source = source_ref("/bindings/" + result.binding_id);
    return result;
}

[[nodiscard]] compiler::CompleteStaticCompositionSource fixture_source() {
    compiler::CompleteStaticCompositionSource source;
    source.source_version =
        std::string(compiler::kCompleteStaticCompositionSourceVersion);
    source.mission_id = "mission.fixture.yyz.two-entity-causal@1";
    source.plan_id = "plan.fixture.yyz.two-entity-causal";
    source.mission_source = source_ref("/mission");
    source.clock = {std::string(kClock), 0.1, 0, kTerminalTick,
                    source_ref("/clock")};
    source.entities = {
        {std::string(kEntityA), compiler::EntityLifecycle::ActiveAtInitialize,
         source_ref("/entities/a/id"),
         source_ref("/entities/a/lifecycle")},
        {std::string(kEntityB), compiler::EntityLifecycle::ActiveAtInitialize,
         source_ref("/entities/b/id"),
         source_ref("/entities/b/lifecycle")}};
    const compiler::ScopeKey scope_a{compiler::ScopeKind::Vehicle,
                                     std::string(kEntityA)};
    const compiler::ScopeKey scope_b{compiler::ScopeKind::Vehicle,
                                     std::string(kEntityB)};
    source.scopes = {{scope_a, source_ref("/scopes/a")},
                     {scope_b, source_ref("/scopes/b")}};

    source.occurrences.push_back(occurrence(
        std::string(kAOccurrence), std::string(yyz::kEntityATruthModelId),
        config(std::string(yyz::kEntityATruthConfigSchemaId),
               {{"increment_per_interval", 1.0}}),
        std::string(kEntityA), scope_a));
    source.occurrences.push_back(occurrence(
        std::string(kLinkOccurrence),
        std::string(yyz::kOneTickTruthLinkModelId),
        config(std::string(yyz::kOneTickTruthLinkConfigSchemaId),
               {{"transfer_gain", 1.0}}),
        std::string(kEntityB), scope_b));
    source.occurrences.push_back(occurrence(
        std::string(kBOccurrence), std::string(yyz::kEntityBTruthModelId),
        config(std::string(yyz::kEntityBTruthConfigSchemaId),
               {{"current_gain", 1.0}, {"delayed_gain", 0.5}}),
        std::string(kEntityB), scope_b));

    compiler::CompleteSourceInitialBinding initial_a;
    initial_a.owner_occurrence_id = std::string(kAOccurrence);
    initial_a.builder_inputs =
        config(std::string(yyz::kEntityATruthInitialSchemaId),
               {{"position", 1.0}});
    initial_a.field_sources = {
        {"position", source_ref("/initial/a/position")}};
    initial_a.source = source_ref("/initial/a");
    source.initial_bindings.push_back(std::move(initial_a));

    compiler::CompleteSourceInitialBinding initial_link;
    initial_link.owner_occurrence_id = std::string(kLinkOccurrence);
    initial_link.builder_inputs =
        config(std::string(yyz::kOneTickTruthLinkInitialSchemaId),
               {{"dormant_position", 0.0}});
    initial_link.field_sources = {
        {"dormant_position", source_ref("/initial/link/dormant-position")}};
    initial_link.source = source_ref("/initial/link");
    source.initial_bindings.push_back(std::move(initial_link));

    compiler::CompleteSourceInitialBinding initial_b;
    initial_b.owner_occurrence_id = std::string(kBOccurrence);
    initial_b.builder_inputs =
        config(std::string(yyz::kEntityBTruthInitialSchemaId),
               {{"position", 10.0}});
    initial_b.field_sources = {
        {"position", source_ref("/initial/b/position")}};
    initial_b.source = source_ref("/initial/b");
    source.initial_bindings.push_back(std::move(initial_b));

    auto a_to_link = binding(
        "binding.causal.a-to-link", std::string(kAOccurrence),
        "truth-observation", std::string(kLinkOccurrence), "current-a");
    a_to_link.entity_selector =
        compiler::CompleteSourceBinding::EntitySelector{
            "selector.causal.a-to-link", std::string(kEntityA),
            source_ref("/selectors/a-to-link")};
    source.bindings.push_back(std::move(a_to_link));

    auto a_to_b = binding(
        "binding.causal.a-to-b", std::string(kAOccurrence),
        "truth-observation", std::string(kBOccurrence), "current-a");
    a_to_b.entity_selector = compiler::CompleteSourceBinding::EntitySelector{
        "selector.causal.a-to-b", std::string(kEntityA),
        source_ref("/selectors/a-to-b")};
    source.bindings.push_back(std::move(a_to_b));
    source.bindings.push_back(binding(
        "binding.causal.link-to-b", std::string(kLinkOccurrence),
        "delayed-a", std::string(kBOccurrence), "delayed-a"));
    source.bindings.push_back(binding(
        "binding.causal.a-opening", std::string(kAOccurrence),
        "truth-observation", std::string(kAOccurrence), "opening-truth"));

    compiler::CompleteSourceTransaction transaction;
    transaction.transaction_id = std::string(kTransaction);
    transaction.scope = scope_a;
    transaction.owner_occurrence_ids = {
        std::string(kAOccurrence), std::string(kLinkOccurrence),
        std::string(kBOccurrence)};
    transaction.source = source_ref("/transactions/causal");
    transaction.member_scopes = {scope_a, scope_b};
    source.transactions.push_back(std::move(transaction));
    source.package_build_locks.push_back(
        {std::string(yyz::kTwoEntityCausalPackageId),
         std::string(yyz::kTwoEntityCausalPackageVersion),
         std::string(yyz::kTwoEntityCausalBuildFingerprint),
         source_ref("/packages/two-entity")});
    return source;
}

struct CompiledFixture {
    sdk::StaticPackageDescriptor package;
    sdk::StaticPackageImplementation implementation;
    compiler::CompleteStaticCompositionSource source;
    compiler::CompleteStaticCompilation compilation;
    contracts::ExecutionPlanImage image;
};

[[nodiscard]] CompiledFixture compile_fixture() {
    auto package = yyz::describe_two_entity_causal_package();
    auto implementation = yyz::describe_two_entity_causal_implementation();
    auto source = fixture_source();
    const auto compilation =
        compiler::compile_complete_execution_plan(source, {package});
    if (!compilation.succeeded()) {
        throw std::runtime_error(
            "two-entity plan compilation failed: " +
            diagnostic_text(compilation));
    }
    const auto linked = compiler::link_complete_execution_plan(
        compilation.value->plan, compilation.value->proofs,
        {implementation});
    if (!linked.succeeded()) {
        throw std::runtime_error(
            "two-entity Image link failed: " + diagnostic_text(linked));
    }
    return {std::move(package), std::move(implementation),
            std::move(source), *compilation.value, *linked.value};
}

struct RuntimeControl {
    std::size_t b_evolution_failures_remaining = 0U;
    std::vector<std::string> candidate_trace;
};

template <typename Value>
class TypedOperations final : public kernel::InProcessObjectOperations {
  public:
    TypedOperations(std::string layout_id, std::uint32_t codec_handle)
        : layout_id_(std::move(layout_id)), codec_handle_(codec_handle) {}

    [[nodiscard]] kernel::InProcessObjectLayout layout()
        const noexcept override {
        return {sizeof(Value), alignof(Value), layout_id_, codec_handle_,
                &typeid(Value), std::is_trivially_copyable_v<Value>,
                std::is_trivially_destructible_v<Value>};
    }

    [[nodiscard]] bool copy_construct(
        const void* source, void* destination) const noexcept override {
        if (source == nullptr || destination == nullptr) return false;
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
        if constexpr (std::is_same_v<Value, yyz::EntityATruthState>) {
            return yyz::validate_entity_a_truth_state(
                *static_cast<const Value*>(object));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::OneTickTruthLinkState>) {
            return yyz::validate_one_tick_truth_link_state(
                *static_cast<const Value*>(object));
        } else if constexpr (std::is_same_v<Value, yyz::EntityBTruthState>) {
            return yyz::validate_entity_b_truth_state(
                *static_cast<const Value*>(object));
        } else {
            return true;
        }
    }

    [[nodiscard]] bool supports_nofail_swap() const noexcept override {
        return std::is_same_v<Value, yyz::EntityATruthState> ||
               std::is_same_v<Value, yyz::OneTickTruthLinkState> ||
               std::is_same_v<Value, yyz::EntityBTruthState>;
    }

    void nofail_swap(void* lhs, void* rhs) const noexcept override {
        if constexpr (std::is_same_v<Value, yyz::EntityATruthState>) {
            yyz::swap_entity_a_truth_state(*static_cast<Value*>(lhs),
                                           *static_cast<Value*>(rhs));
        } else if constexpr (std::is_same_v<
                                 Value, yyz::OneTickTruthLinkState>) {
            yyz::swap_one_tick_truth_link_state(*static_cast<Value*>(lhs),
                                                *static_cast<Value*>(rhs));
        } else if constexpr (std::is_same_v<Value, yyz::EntityBTruthState>) {
            yyz::swap_entity_b_truth_state(*static_cast<Value*>(lhs),
                                           *static_cast<Value*>(rhs));
        }
    }

    void destroy(void* object) const noexcept override {
        if (object != nullptr) static_cast<Value*>(object)->~Value();
    }

  private:
    std::string layout_id_;
    std::uint32_t codec_handle_ = 0U;
};

template <typename Value>
class TypedMaterializer final : public kernel::SessionObjectMaterializer {
  public:
    TypedMaterializer(kernel::SessionMaterializerIdentity identity,
                      std::string layout_id, Value value)
        : identity_(identity),
          operations_(std::move(layout_id), identity.codec_entry_handle),
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
                    "two-entity invocation threw"};
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
    throw std::runtime_error("typed two-entity slot is absent");
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
    throw std::runtime_error("two-entity candidate state slot is absent");
}

[[nodiscard]] std::uint32_t writer_for_slot(
    const contracts::PlanImageCallsite& callsite,
    std::uint32_t slot_handle) {
    require(callsite.output_slot_handles.size() ==
                callsite.output_writer_token_handles.size(),
            "two-entity output/writer shape differs");
    for (std::size_t index = 0U;
         index < callsite.output_slot_handles.size(); ++index) {
        if (callsite.output_slot_handles[index] == slot_handle) {
            return callsite.output_writer_token_handles[index];
        }
    }
    throw std::runtime_error("two-entity writer token is absent");
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
                "two-entity typed input differs from Image layout"};
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
        std::uint32_t) const noexcept override {
        return nullptr;
    }
    [[nodiscard]] const kernel::SessionEventConsumerEntry* event_consumer(
        std::uint32_t) const noexcept override {
        return nullptr;
    }

    RuntimeControl control;
    std::uint32_t a_state_block_handle = 0U;
    std::uint32_t link_state_block_handle = 0U;
    std::uint32_t b_state_block_handle = 0U;
    std::uint32_t a_output_slot_handle = 0U;
    std::uint32_t link_output_slot_handle = 0U;
    std::uint32_t b_output_slot_handle = 0U;

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
                    slot.handle, kernel::SessionObjectRole::CycleFrameValue,
                    0U, slot.codec_entry_handle},
                slot.layout_id, std::move(value)));
    }

    void build_slots(const contracts::ExecutionPlanImage& image) {
        for (const auto& slot : image.slots()) {
            if (slot.kind == contracts::PlanImageSlotKind::CommittedState ||
                slot.kind == contracts::PlanImageSlotKind::CandidateState) {
                continue;
            }
            if (slot.layout_id == yyz::kEntityATruthObservationLayoutId) {
                a_output_slot_handle = slot.handle;
                add_slot(slot, yyz::EntityATruthObservation{});
            } else if (slot.layout_id ==
                       yyz::kOneTickTruthLinkObservationLayoutId) {
                link_output_slot_handle = slot.handle;
                add_slot(slot, yyz::OneTickTruthLinkObservation{});
            } else if (slot.layout_id ==
                       yyz::kEntityBTruthObservationLayoutId) {
                b_output_slot_handle = slot.handle;
                add_slot(slot, yyz::EntityBTruthObservation{});
            } else {
                throw std::runtime_error(
                    "unsupported two-entity slot layout");
            }
        }
    }

    void build(const contracts::ExecutionPlanImage& image) {
        require(image.revision() == 4U && image.entity_selectors().size() == 2U &&
                    image.state_blocks().size() == 3U &&
                    image.runtime_components().size() == 3U,
                "two-entity Image shape differs from fixture contract");
        const auto* occurrence_a =
            occurrence_for_model(image, yyz::kEntityATruthModelId);
        const auto* occurrence_link =
            occurrence_for_model(image, yyz::kOneTickTruthLinkModelId);
        const auto* occurrence_b =
            occurrence_for_model(image, yyz::kEntityBTruthModelId);
        require(occurrence_a != nullptr && occurrence_link != nullptr &&
                    occurrence_b != nullptr,
                "two-entity Image occurrence is absent");

        definition_a_ = require_numerical(
            yyz::build_entity_a_truth_definition(canonical_configuration(
                occurrence_a->canonical_configuration)),
            "entity A Image configuration was rejected");
        definition_link_ = require_numerical(
            yyz::build_one_tick_truth_link_definition(canonical_configuration(
                occurrence_link->canonical_configuration)),
            "link Image configuration was rejected");
        definition_b_ = require_numerical(
            yyz::build_entity_b_truth_definition(canonical_configuration(
                occurrence_b->canonical_configuration)),
            "entity B Image configuration was rejected");

        for (const auto& component : image.runtime_components()) {
            runtime_components_.emplace(
                component.handle,
                std::make_unique<TypedMaterializer<RuntimeMarker>>(
                    kernel::SessionMaterializerIdentity{
                        component.handle, kernel::SessionObjectRole::RuntimeCell,
                        component.runtime_cell_factory_entry_handle, 0U},
                    "gnc.layout.fixture.yyz.two-entity-runtime-marker@1",
                    RuntimeMarker{component.occurrence_handle}));
        }
        build_slots(image);
        build_initial_states(image);
        install_entity_a(image, *occurrence_a);
        install_link(image, *occurrence_link);
        install_entity_b(image, *occurrence_b);
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
                    "two-entity initial binding is absent");
            if (block.layout_id == yyz::kEntityATruthStateLayoutId) {
                a_state_block_handle = block.handle;
                const auto state = require_numerical(
                    yyz::build_entity_a_truth_initial_state(definition_a_,
                                                            {1.0}),
                    "entity A initial state was rejected");
                initial_states_.emplace(
                    binding->handle,
                    std::make_unique<TypedMaterializer<yyz::EntityATruthState>>(
                        kernel::SessionMaterializerIdentity{
                            binding->handle,
                            kernel::SessionObjectRole::InitialStateValue,
                            binding->builder_entry_handle,
                            block.codec_entry_handle},
                        block.layout_id, state));
            } else if (block.layout_id ==
                       yyz::kOneTickTruthLinkStateLayoutId) {
                link_state_block_handle = block.handle;
                const auto state = require_numerical(
                    yyz::build_one_tick_truth_link_initial_state(
                        definition_link_, {0.0}),
                    "link initial state was rejected");
                initial_states_.emplace(
                    binding->handle,
                    std::make_unique<TypedMaterializer<
                        yyz::OneTickTruthLinkState>>(
                        kernel::SessionMaterializerIdentity{
                            binding->handle,
                            kernel::SessionObjectRole::InitialStateValue,
                            binding->builder_entry_handle,
                            block.codec_entry_handle},
                        block.layout_id, state));
            } else if (block.layout_id == yyz::kEntityBTruthStateLayoutId) {
                b_state_block_handle = block.handle;
                const auto state = require_numerical(
                    yyz::build_entity_b_truth_initial_state(definition_b_,
                                                            {10.0}),
                    "entity B initial state was rejected");
                initial_states_.emplace(
                    binding->handle,
                    std::make_unique<TypedMaterializer<yyz::EntityBTruthState>>(
                        kernel::SessionMaterializerIdentity{
                            binding->handle,
                            kernel::SessionObjectRole::InitialStateValue,
                            binding->builder_entry_handle,
                            block.codec_entry_handle},
                        block.layout_id, state));
            } else {
                throw std::runtime_error(
                    "unsupported two-entity state layout");
            }
        }
    }

    void install_entity_a(
        const contracts::ExecutionPlanImage& image,
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
                "entity A callsite set is incomplete");
        const auto observation = slot_with_contract(
            image, publish->output_slot_handles,
            yyz::kEntityATruthObservationContractId);
        const auto observation_writer = writer_for_slot(*publish, observation);
        invocations_.emplace(
            publish->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    publish->handle, component->handle, publish->entry_handle},
                [this, observation,
                 observation_writer](const auto& context) {
                    const yyz::EntityATruthState* state = nullptr;
                    auto status = read_typed(context.committed(),
                                             a_state_block_handle, state);
                    if (!status) return status;
                    const auto value =
                        yyz::project_entity_a_truth(*state, context.tick());
                    return write_typed(context.outputs(), observation,
                                       observation_writer, value);
                }));

        const auto next_state =
            candidate_slot(image, evolution->output_slot_handles);
        const auto opening = slot_with_contract(
            image, evolution->input_slot_handles,
            yyz::kEntityATruthObservationContractId);
        const auto next_writer = writer_for_slot(*evolution, next_state);
        invocations_.emplace(
            evolution->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    evolution->handle, component->handle,
                    evolution->entry_handle},
                [this, opening, next_state,
                 next_writer](const auto& context) {
                    control.candidate_trace.push_back("A");
                    const yyz::EntityATruthState* prior = nullptr;
                    const yyz::EntityATruthObservation* observation = nullptr;
                    auto status = read_typed(context.committed(),
                                             a_state_block_handle, prior);
                    if (!status) return status;
                    status = read_typed(context.inputs(), opening,
                                        observation);
                    if (!status) return status;
                    const auto value = yyz::evolve_entity_a_truth(
                        definition_a_, *prior, *observation);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            next_state, "entity A evolution failed"};
                    }
                    return write_candidate(context.candidates(), next_state,
                                           next_writer, value.value());
                }));
    }

    void install_link(
        const contracts::ExecutionPlanImage& image,
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
                "link callsite set is incomplete");
        const auto observation = slot_with_contract(
            image, publish->output_slot_handles,
            yyz::kOneTickTruthLinkObservationContractId);
        const auto observation_writer = writer_for_slot(*publish, observation);
        invocations_.emplace(
            publish->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    publish->handle, component->handle, publish->entry_handle},
                [this, observation,
                 observation_writer](const auto& context) {
                    const yyz::OneTickTruthLinkState* state = nullptr;
                    auto status = read_typed(context.committed(),
                                             link_state_block_handle, state);
                    if (!status) return status;
                    const auto value = yyz::project_one_tick_truth_link(
                        *state, context.tick());
                    return write_typed(context.outputs(), observation,
                                       observation_writer, value);
                }));

        const auto current_a = slot_with_contract(
            image, evolution->input_slot_handles,
            yyz::kEntityATruthObservationContractId);
        const auto next_state =
            candidate_slot(image, evolution->output_slot_handles);
        const auto next_writer = writer_for_slot(*evolution, next_state);
        invocations_.emplace(
            evolution->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    evolution->handle, component->handle,
                    evolution->entry_handle},
                [this, current_a, next_state,
                 next_writer](const auto& context) {
                    control.candidate_trace.push_back("Link");
                    const yyz::OneTickTruthLinkState* prior = nullptr;
                    const yyz::EntityATruthObservation* a = nullptr;
                    auto status = read_typed(context.committed(),
                                             link_state_block_handle, prior);
                    if (!status) return status;
                    status = read_typed(context.inputs(), current_a, a);
                    if (!status) return status;
                    const auto value = yyz::evolve_one_tick_truth_link(
                        definition_link_, *prior, *a);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            next_state, "one-tick link evolution failed"};
                    }
                    return write_candidate(context.candidates(), next_state,
                                           next_writer, value.value());
                }));
    }

    void install_entity_b(
        const contracts::ExecutionPlanImage& image,
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
                "entity B callsite set is incomplete");
        const auto observation = slot_with_contract(
            image, publish->output_slot_handles,
            yyz::kEntityBTruthObservationContractId);
        const auto observation_writer = writer_for_slot(*publish, observation);
        invocations_.emplace(
            publish->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    publish->handle, component->handle, publish->entry_handle},
                [this, observation,
                 observation_writer](const auto& context) {
                    const yyz::EntityBTruthState* state = nullptr;
                    auto status = read_typed(context.committed(),
                                             b_state_block_handle, state);
                    if (!status) return status;
                    const auto value =
                        yyz::project_entity_b_truth(*state, context.tick());
                    return write_typed(context.outputs(), observation,
                                       observation_writer, value);
                }));

        const auto current_a = slot_with_contract(
            image, evolution->input_slot_handles,
            yyz::kEntityATruthObservationContractId);
        const auto delayed_a = slot_with_contract(
            image, evolution->input_slot_handles,
            yyz::kOneTickTruthLinkObservationContractId);
        const auto next_state =
            candidate_slot(image, evolution->output_slot_handles);
        const auto next_writer = writer_for_slot(*evolution, next_state);
        invocations_.emplace(
            evolution->handle,
            std::make_unique<LambdaInvocation>(
                kernel::SessionInvocationIdentity{
                    evolution->handle, component->handle,
                    evolution->entry_handle},
                [this, current_a, delayed_a, next_state,
                 next_writer](const auto& context) {
                    control.candidate_trace.push_back("B");
                    if (control.b_evolution_failures_remaining != 0U) {
                        --control.b_evolution_failures_remaining;
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            next_state,
                            "entity B injected candidate failure"};
                    }
                    const yyz::EntityBTruthState* prior = nullptr;
                    const yyz::EntityATruthObservation* a = nullptr;
                    const yyz::OneTickTruthLinkObservation* delayed = nullptr;
                    auto status = read_typed(context.committed(),
                                             b_state_block_handle, prior);
                    if (!status) return status;
                    status = read_typed(context.inputs(), current_a, a);
                    if (!status) return status;
                    status = read_typed(context.inputs(), delayed_a, delayed);
                    if (!status) return status;
                    const auto value = yyz::evolve_entity_b_truth(
                        definition_b_, *prior, *a, *delayed);
                    if (!value.succeeded()) {
                        return kernel::SessionResult{
                            kernel::SessionError::InvocationFailed,
                            next_state, "entity B evolution failed"};
                    }
                    return write_candidate(context.candidates(), next_state,
                                           next_writer, value.value());
                }));
    }

    MaterializerMap runtime_components_;
    MaterializerMap slots_;
    MaterializerMap initial_states_;
    std::unordered_map<std::uint32_t,
                       std::unique_ptr<kernel::SessionInvocationEntry>>
        invocations_;
    yyz::EntityATruthDefinition definition_a_;
    yyz::OneTickTruthLinkDefinition definition_link_;
    yyz::EntityBTruthDefinition definition_b_;
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
            "two-entity Session creation failed");
    auto session = std::move(creation.session);
    kernel::RunId id(std::move(run_id));
    const auto initialized = session->initialize(
        {id, kernel::exact_run_binding(*shared_image)});
    require(static_cast<bool>(initialized),
            "two-entity Session initialization failed");
    return {std::move(shared_image), std::move(provider),
            std::move(session), std::move(id)};
}

template <typename Value>
[[nodiscard]] Value read_committed_state(
    const LiveSession& live, std::uint32_t state_block_handle) {
    kernel::SessionObjectIdentityView view;
    const auto status =
        kernel::qualification::SessionAccess::read_committed(
            *live.session, state_block_handle, view);
    require(status && view.type_identity == &typeid(Value),
            "two-entity committed state is unavailable");
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
            "two-entity committed output is unavailable");
    return *static_cast<const Value*>(view.address);
}

[[nodiscard]] bool has_diagnostic(
    const std::vector<compiler::CompleteDiagnostic>& diagnostics,
    compiler::CompleteDiagnosticCode code) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto& diagnostic) {
                           return diagnostic.code == code;
                       });
}

void verify_static_and_compiled_contract(const CompiledFixture& fixture) {
    require(fixture.package.models.size() == 3U &&
                fixture.implementation.state_layouts.size() == 3U &&
                fixture.implementation.value_layouts.size() == 3U &&
                fixture.image.revision() == 4U &&
                fixture.image.occurrences().size() == 3U &&
                fixture.image.runtime_components().size() == 3U &&
                fixture.image.state_blocks().size() == 3U &&
                fixture.image.entity_selectors().size() == 2U &&
                fixture.image.transactions().size() == 1U,
            "two-entity static or Image shape is incomplete");
    const auto factory_count = static_cast<std::size_t>(std::count_if(
        fixture.implementation.entries.begin(),
        fixture.implementation.entries.end(), [](const auto& entry) {
            return entry.kind == sdk::StaticEntryKind::RuntimeCellFactory;
        }));
    require(factory_count == 3U,
            "two-entity package did not exact-link three factories");

    const auto selector_proof_count =
        static_cast<std::size_t>(std::count_if(
            fixture.compilation.proofs.records.begin(),
            fixture.compilation.proofs.records.end(), [](const auto& proof) {
                return proof.kind ==
                       compiler::PlanProofKind::EntitySelectorAuthorization;
            }));
    require(selector_proof_count == 2U &&
                fixture.compilation.plan.entity_selectors.size() == 2U,
            "cross-entity selector proofs are incomplete");
    const auto scope_proof = std::find_if(
        fixture.compilation.proofs.records.begin(),
        fixture.compilation.proofs.records.end(), [](const auto& proof) {
            return proof.proof_id ==
                   "proof/scope/binding.causal.a-to-b";
        });
    require(
        scope_proof != fixture.compilation.proofs.records.end() &&
            std::find(scope_proof->premises.begin(),
                      scope_proof->premises.end(),
                      "authorization=entity-selector") !=
                scope_proof->premises.end() &&
            std::find(scope_proof->premises.begin(),
                      scope_proof->premises.end(),
                      "selector=selector.causal.a-to-b") !=
                scope_proof->premises.end() &&
            std::find(scope_proof->covered_plan_elements.begin(),
                      scope_proof->covered_plan_elements.end(),
                      "entity-selector/selector.causal.a-to-b") !=
                scope_proof->covered_plan_elements.end() &&
            std::find(scope_proof->premises.begin(),
                      scope_proof->premises.end(),
                      "compatible=true") == scope_proof->premises.end(),
        "cross-entity scope proof does not depend on selector authorization");

    const auto& transaction = fixture.image.transactions().front();
    require(transaction.candidates.size() == 3U &&
                transaction.member_occurrence_handles.size() == 3U &&
                std::is_sorted(transaction.member_occurrence_handles.begin(),
                               transaction.member_occurrence_handles.end()) &&
                std::adjacent_find(
                    transaction.member_occurrence_handles.begin(),
                    transaction.member_occurrence_handles.end()) ==
                    transaction.member_occurrence_handles.end(),
            "multi-scope transaction membership is not exact");

    const auto* occurrence_a =
        occurrence_for_model(fixture.image, yyz::kEntityATruthModelId);
    require(occurrence_a != nullptr,
            "entity A occurrence is absent from Image");
    for (const auto& selector : fixture.image.entity_selectors()) {
        require(selector.selected_entity_id == kEntityA &&
                    selector.provider_occurrence_handle ==
                        occurrence_a->handle &&
                    !selector.consumer_callsite_handles.empty(),
                "selector authority does not name the A provider");
    }
    const auto distinct_state_owners = [&fixture] {
        std::vector<std::uint32_t> owners;
        for (const auto& block : fixture.image.state_blocks()) {
            owners.push_back(block.owner_occurrence_handle);
        }
        std::sort(owners.begin(), owners.end());
        return std::adjacent_find(owners.begin(), owners.end()) == owners.end();
    }();
    require(distinct_state_owners,
            "two-entity mutable state has a duplicated owner");

    const auto repeated = compiler::compile_complete_execution_plan(
        fixture.source, {fixture.package});
    require(repeated.succeeded(),
            "repeated two-entity compilation failed");
    const auto repeated_image = compiler::link_complete_execution_plan(
        repeated.value->plan, repeated.value->proofs,
        {fixture.implementation});
    require(repeated_image.succeeded() &&
                repeated.value->plan.source_semantic_hash ==
                    fixture.compilation.plan.source_semantic_hash &&
                repeated.value->plan.descriptor_semantic_hash ==
                    fixture.compilation.plan.descriptor_semantic_hash &&
                repeated_image.value->fingerprint() ==
                    fixture.image.fingerprint(),
            "two-entity compile/link is not deterministic");
}

struct ScenarioResult {
    yyz::EntityATruthState entity_a;
    yyz::OneTickTruthLinkState link;
    yyz::EntityBTruthState entity_b;
    kernel::RunOutcome outcome;
};

[[nodiscard]] ScenarioResult run_scenario(
    const contracts::ExecutionPlanImage& image, std::string run_id) {
    auto live = initialize_session(image, std::move(run_id));

    const auto step_zero = live.session->execute_step();
    require(step_zero.status == kernel::StepStatus::Committed &&
                step_zero.tick_before == 0 && step_zero.tick_after == 1 &&
                step_zero.committed_epoch == 1U,
            "two-entity tick zero did not commit");
    const auto a_zero =
        read_committed_output<yyz::EntityATruthObservation>(
            live, live.provider->a_output_slot_handle);
    const auto link_zero =
        read_committed_output<yyz::OneTickTruthLinkObservation>(
            live, live.provider->link_output_slot_handle);
    const auto b_after_zero = read_committed_state<yyz::EntityBTruthState>(
        live, live.provider->b_state_block_handle);
    require(a_zero.tick == 0 && near(a_zero.position, 1.0) &&
                link_zero.tick == 0 && !link_zero.valid &&
                near(b_after_zero.position, 11.0) &&
                near(b_after_zero.last_current_a_position, 1.0) &&
                !b_after_zero.last_delayed_valid,
            "tick zero did not preserve current/absent-delayed semantics");

    const auto step_one = live.session->execute_step();
    require(step_one.status == kernel::StepStatus::Committed &&
                step_one.tick_before == 1 && step_one.tick_after == 2,
            "two-entity tick one did not commit");
    const auto a_one =
        read_committed_output<yyz::EntityATruthObservation>(
            live, live.provider->a_output_slot_handle);
    const auto link_one =
        read_committed_output<yyz::OneTickTruthLinkObservation>(
            live, live.provider->link_output_slot_handle);
    const auto b_after_one = read_committed_state<yyz::EntityBTruthState>(
        live, live.provider->b_state_block_handle);
    require(a_one.tick == 1 && near(a_one.position, 2.0) &&
                link_one.tick == 1 && link_one.valid &&
                link_one.source_tick == 0 &&
                near(link_one.source_position, 1.0) &&
                near(b_after_one.position, 13.5) &&
                near(b_after_one.last_current_a_position, 2.0) &&
                b_after_one.last_delayed_valid &&
                b_after_one.last_delayed_source_tick == 0 &&
                near(b_after_one.last_delayed_a_position, 1.0),
            "tick one did not combine same-tick A and previous committed A");

    const auto step_two = live.session->execute_step();
    require(step_two.status == kernel::StepStatus::Committed &&
                step_two.tick_before == 2 && step_two.tick_after == 3,
            "two-entity tick two did not commit");
    const auto terminal = live.session->execute_step();
    require(terminal.status == kernel::StepStatus::Terminated &&
                terminal.tick_before == kTerminalTick &&
                terminal.tick_after == kTerminalTick &&
                live.session->state() == kernel::SessionState::Completed,
            "two-entity clock terminal boundary is inconsistent");

    ScenarioResult result;
    result.entity_a = read_committed_state<yyz::EntityATruthState>(
        live, live.provider->a_state_block_handle);
    result.link = read_committed_state<yyz::OneTickTruthLinkState>(
        live, live.provider->link_state_block_handle);
    result.entity_b = read_committed_state<yyz::EntityBTruthState>(
        live, live.provider->b_state_block_handle);
    const auto* outcome = live.session->run_outcome();
    require(outcome != nullptr,
            "two-entity completed run outcome is absent");
    result.outcome = *outcome;
    require(result.entity_a.revision == 3U &&
                near(result.entity_a.position, 4.0) &&
                result.link.revision == 3U && result.link.source_tick == 2 &&
                near(result.link.source_position, 3.0) &&
                result.entity_b.revision == 3U &&
                near(result.entity_b.position, 17.5) &&
                result.entity_b.last_delayed_source_tick == 1 &&
                near(result.entity_b.last_delayed_a_position, 2.0),
            "two-entity final causal state is incorrect");
    return result;
}

void verify_atomic_failure(
    const contracts::ExecutionPlanImage& image) {
    auto live = initialize_session(image, "run.two-entity.rollback");
    const auto a_before = read_committed_state<yyz::EntityATruthState>(
        live, live.provider->a_state_block_handle);
    const auto link_before =
        read_committed_state<yyz::OneTickTruthLinkState>(
            live, live.provider->link_state_block_handle);
    const auto b_before = read_committed_state<yyz::EntityBTruthState>(
        live, live.provider->b_state_block_handle);
    live.provider->control.b_evolution_failures_remaining = 1U;
    live.provider->control.candidate_trace.clear();
    const auto failed = live.session->execute_step();
    const auto a_after = read_committed_state<yyz::EntityATruthState>(
        live, live.provider->a_state_block_handle);
    const auto link_after =
        read_committed_state<yyz::OneTickTruthLinkState>(
            live, live.provider->link_state_block_handle);
    const auto b_after = read_committed_state<yyz::EntityBTruthState>(
        live, live.provider->b_state_block_handle);
    require(failed.status == kernel::StepStatus::Failed &&
                failed.committed_epoch == 0U &&
                failed.primary_diagnostic.has_value(),
            "multi-owner candidate failure did not return one frozen failure");
    const auto& diagnostic = *failed.primary_diagnostic;
    require(diagnostic.stage ==
                    kernel::RuntimeDiagnosticStage::CandidateProduction &&
                diagnostic.cause_code ==
                    kernel::SessionError::InvocationFailed &&
                diagnostic.validity_effect ==
                    contracts::EvidenceValidity::Invalid &&
                diagnostic.disposition ==
                    kernel::RuntimeFailureDisposition::FailOperation &&
                diagnostic.tick == 0 && diagnostic.base_epoch == 0U,
            "multi-owner candidate failure diagnostic changed");
    require(live.session->state() == kernel::SessionState::Failed &&
                live.session->committed_tick() == 0 &&
                live.session->committed_epoch() == 0U &&
                live.session->committed_outputs().empty(),
            "multi-owner candidate failure advanced Session authority");
    require(live.provider->control.candidate_trace ==
                std::vector<std::string>({"A", "Link", "B"}),
            "multi-owner candidate failure changed deterministic production order");
    require(near(a_before.position, a_after.position) &&
                a_before.revision == a_after.revision &&
                link_before.valid == link_after.valid &&
                link_before.revision == link_after.revision &&
                near(b_before.position, b_after.position) &&
                b_before.revision == b_after.revision,
            "failure after A candidate leaked a partial multi-owner commit");
    const auto rejected = live.session->execute_step();
    require(rejected.status == kernel::StepStatus::Failed &&
                rejected.result.error ==
                    kernel::SessionError::InvalidLifecycleTransition,
            "fatal two-entity physics failure was retryable");

    auto recovery = initialize_session(image, "run.two-entity.recovery");
    require(recovery.session->execute_step().status ==
                kernel::StepStatus::Committed,
            "fresh Session could not recover the same immutable Image");
}

void verify_shared_provider_isolation(
    const contracts::ExecutionPlanImage& image) {
    auto shared_image =
        std::make_shared<const contracts::ExecutionPlanImage>(image);
    auto provider = std::make_shared<FixtureProvider>(*shared_image);
    auto first = initialize_session(image, "run.two-entity.shared.first",
                                    provider, shared_image);
    auto second = initialize_session(image, "run.two-entity.shared.second",
                                     provider, shared_image);
    require(first.session->execute_step().status ==
                kernel::StepStatus::Committed,
            "first shared-provider Session did not commit");
    const auto first_a = read_committed_state<yyz::EntityATruthState>(
        first, provider->a_state_block_handle);
    const auto second_a_before = read_committed_state<yyz::EntityATruthState>(
        second, provider->a_state_block_handle);
    require(first_a.revision == 1U && second_a_before.revision == 0U,
            "shared provider leaked Session state");
    require(second.session->execute_step().status ==
                kernel::StepStatus::Committed,
            "second shared-provider Session did not commit");
    const auto second_a_after = read_committed_state<yyz::EntityATruthState>(
        second, provider->a_state_block_handle);
    const auto first_b = read_committed_state<yyz::EntityBTruthState>(
        first, provider->b_state_block_handle);
    const auto second_b = read_committed_state<yyz::EntityBTruthState>(
        second, provider->b_state_block_handle);
    require(second_a_after.revision == first_a.revision &&
                near(second_a_after.position, first_a.position) &&
                second_b.revision == first_b.revision &&
                near(second_b.position, first_b.position),
            "shared-provider Sessions diverged under identical steps");
}

void verify_source_and_link_negatives(const CompiledFixture& fixture) {
    const auto expect_compile_failure =
        [&](compiler::CompleteStaticCompositionSource source,
            compiler::CompleteDiagnosticCode code,
            std::string_view message) {
            const auto outcome = compiler::compile_complete_execution_plan(
                source, {fixture.package});
            require(!outcome.succeeded() &&
                        has_diagnostic(outcome.diagnostics, code),
                    message);
        };

    auto missing_selector = fixture.source;
    missing_selector.bindings.front().entity_selector.reset();
    expect_compile_failure(
        std::move(missing_selector),
        compiler::CompleteDiagnosticCode::ScopeMismatch,
        "cross-entity binding without selector did not fail closed");

    auto wrong_entity = fixture.source;
    wrong_entity.bindings.front().entity_selector->selected_entity_id =
        std::string(kEntityB);
    expect_compile_failure(
        std::move(wrong_entity),
        compiler::CompleteDiagnosticCode::InvalidEntitySelector,
        "selector naming the consumer entity did not fail closed");

    auto duplicate_selector = fixture.source;
    duplicate_selector.bindings[1].entity_selector->selector_id =
        duplicate_selector.bindings[0].entity_selector->selector_id;
    expect_compile_failure(
        std::move(duplicate_selector),
        compiler::CompleteDiagnosticCode::InvalidEntitySelector,
        "duplicate selector identity did not fail closed");

    auto same_scope_selector = fixture.source;
    same_scope_selector.bindings.back().entity_selector =
        compiler::CompleteSourceBinding::EntitySelector{
            "selector.causal.same-scope", std::string(kEntityB),
            source_ref("/selectors/same-scope")};
    expect_compile_failure(
        std::move(same_scope_selector),
        compiler::CompleteDiagnosticCode::InvalidEntitySelector,
        "same-scope selector did not fail closed");

    auto missing_member_scope = fixture.source;
    missing_member_scope.transactions.front().member_scopes.resize(1U);
    expect_compile_failure(
        std::move(missing_member_scope),
        compiler::CompleteDiagnosticCode::InvalidTransaction,
        "transaction omitting entity B scope did not fail closed");

    auto mutated_plan = fixture.compilation.plan;
    mutated_plan.entity_selectors.front().selected_entity_id =
        std::string(kEntityB);
    mutated_plan.descriptor_semantic_hash =
        compiler::complete_plan_detail::descriptor_hash(mutated_plan);
    const auto mutated_proofs =
        compiler::complete_plan_detail::derive_proofs(mutated_plan);
    const auto mutated_link = compiler::link_complete_execution_plan(
        mutated_plan, mutated_proofs, {fixture.implementation});
    require(!mutated_link.succeeded() &&
                has_diagnostic(
                    mutated_link.diagnostics,
                    compiler::CompleteDiagnosticCode::
                        SourceImageConformanceFailure),
            "coherently rehashed selector mutation passed exact linker checks");

    auto mutated_transaction = fixture.compilation.plan;
    mutated_transaction.transactions.front().member_scopes.pop_back();
    mutated_transaction.descriptor_semantic_hash =
        compiler::complete_plan_detail::descriptor_hash(mutated_transaction);
    const auto transaction_proofs =
        compiler::complete_plan_detail::derive_proofs(mutated_transaction);
    const auto transaction_link = compiler::link_complete_execution_plan(
        mutated_transaction, transaction_proofs, {fixture.implementation});
    require(!transaction_link.succeeded() &&
                has_diagnostic(
                    transaction_link.diagnostics,
                    compiler::CompleteDiagnosticCode::
                        SourceImageConformanceFailure),
            "coherently rehashed transaction scope mutation passed linker checks");
}

void require_image_initialization_failure(
    contracts::ExecutionPlanImageData data, std::string_view message) {
    data.image_fingerprint =
        compiler::complete_plan_detail::image_fingerprint(data);
    auto image = std::make_shared<const contracts::ExecutionPlanImage>(
        contracts::ExecutionPlanImage::freeze(std::move(data)));
    auto provider = std::make_shared<FixtureProvider>(*image);
    auto creation = kernel::create_session(image, provider);
    require(static_cast<bool>(creation),
            "mutated two-entity Image did not reach Session validation");
    const auto initialized = creation.session->initialize(
        {kernel::RunId("run.two-entity.invalid-image"),
         kernel::exact_run_binding(*image)});
    require(!initialized &&
                initialized.result.error ==
                    kernel::SessionError::InvalidImageStructure,
            message);
}

void verify_image_negatives(const CompiledFixture& fixture) {
    auto selector = fixture.image.data();
    require(!selector.entity_selectors.empty(),
            "selector Image mutation fixture is empty");
    selector.entity_selectors.front().provider_slot_handle =
        std::find_if(selector.slots.begin(), selector.slots.end(),
                     [](const auto& slot) {
                         return slot.contract_id ==
                                yyz::kEntityBTruthObservationContractId;
                     })
            ->handle;
    require_image_initialization_failure(
        std::move(selector),
        "selector numeric slot mutation passed Session validation");

    auto selector_reader_subset = fixture.image.data();
    auto& selected = selector_reader_subset.entity_selectors.front();
    const auto additional_reader = std::find_if(
        selector_reader_subset.callsites.begin(),
        selector_reader_subset.callsites.end(), [&](const auto& callsite) {
            return callsite.occurrence_handle ==
                       selected.consumer_occurrence_handle &&
                   std::find(selected.consumer_callsite_handles.begin(),
                             selected.consumer_callsite_handles.end(),
                             callsite.handle) ==
                       selected.consumer_callsite_handles.end();
        });
    const auto selected_slot = std::find_if(
        selector_reader_subset.slots.begin(),
        selector_reader_subset.slots.end(), [&](const auto& slot) {
            return slot.handle == selected.provider_slot_handle;
        });
    require(additional_reader != selector_reader_subset.callsites.end() &&
                selected_slot != selector_reader_subset.slots.end(),
            "selector reader-subset mutation fixture is incomplete");
    additional_reader->input_slot_handles.push_back(
        selected.provider_slot_handle);
    std::sort(additional_reader->input_slot_handles.begin(),
              additional_reader->input_slot_handles.end());
    require_image_initialization_failure(
        std::move(selector_reader_subset),
        "selector accepted an unlisted callsite input reader");

    auto transaction = fixture.image.data();
    require(!transaction.transactions.empty() &&
                transaction.transactions.front()
                        .member_occurrence_handles.size() == 3U,
            "transaction Image mutation fixture is incomplete");
    transaction.transactions.front().member_occurrence_handles.pop_back();
    require_image_initialization_failure(
        std::move(transaction),
        "multi-scope member occurrence mutation passed Session validation");

    auto missing_scope_membership = fixture.image.data();
    missing_scope_membership.transactions.front()
        .member_occurrence_handles.clear();
    require_image_initialization_failure(
        std::move(missing_scope_membership),
        "multi-scope transaction accepted absent scope membership");

    auto candidate_subset = fixture.image.data();
    auto& atomic = candidate_subset.transactions.front();
    const auto b_occurrence = std::find_if(
        candidate_subset.occurrences.begin(),
        candidate_subset.occurrences.end(), [](const auto& occurrence) {
            return occurrence.occurrence_id == kBOccurrence;
        });
    require(b_occurrence != candidate_subset.occurrences.end(),
            "multi-scope candidate-subset mutation has no entity B");
    const auto removed = std::find_if(
        atomic.candidates.begin(), atomic.candidates.end(),
        [&](const auto& candidate) {
            return candidate.owner_occurrence_handle == b_occurrence->handle;
        });
    require(removed != atomic.candidates.end(),
            "multi-scope candidate-subset mutation has no entity B candidate");
    const auto removed_slot = removed->candidate_state_slot_handle;
    const auto removed_producer = removed->producer_handle;
    atomic.candidates.erase(removed);
    const auto erase_handle = [](auto& handles, std::uint32_t handle) {
        handles.erase(std::remove(handles.begin(), handles.end(), handle),
                      handles.end());
    };
    for (auto& branch : atomic.branches) {
        erase_handle(branch.committed_candidate_slot_handles, removed_slot);
        erase_handle(branch.discarded_candidate_slot_handles, removed_slot);
    }
    candidate_subset.cancellation_policy.safe_points.erase(
        std::remove_if(
            candidate_subset.cancellation_policy.safe_points.begin(),
            candidate_subset.cancellation_policy.safe_points.end(),
            [&](const auto& point) {
                return point.kind ==
                           contracts::PlanImageCancellationSafePointKind::
                               AfterCandidateProducer &&
                       point.subject_handle == removed_producer;
            }),
        candidate_subset.cancellation_policy.safe_points.end());
    require_image_initialization_failure(
        std::move(candidate_subset),
        "multi-scope transaction accepted a coherent candidate-owner subset");
}

void verify_product_failures() {
    const auto invalid_definition = yyz::build_entity_a_truth_definition(
        config(std::string(yyz::kEntityATruthConfigSchemaId),
               {{"increment_per_interval",
                 (std::numeric_limits<double>::quiet_NaN)()}}));
    require(!invalid_definition.succeeded(),
            "non-finite entity A definition was accepted");
    const auto invalid_causal = yyz::evolve_entity_b_truth(
        {1.0, 0.5}, {}, {1, 2.0, 1U}, {1, true, 1, 2.0, 1U});
    require(!invalid_causal.succeeded(),
            "same-tick link payload was accepted as previous committed data");
}

void run() {
    const auto fixture = compile_fixture();
    verify_static_and_compiled_contract(fixture);
    verify_source_and_link_negatives(fixture);
    verify_image_negatives(fixture);
    verify_product_failures();
    verify_atomic_failure(fixture.image);
    verify_shared_provider_isolation(fixture.image);
    const auto first = run_scenario(
        fixture.image, "run.two-entity.determinism.first");
    const auto second = run_scenario(
        fixture.image, "run.two-entity.determinism.second");
    require(near(first.entity_a.position, second.entity_a.position) &&
                first.entity_a.revision == second.entity_a.revision &&
                first.link.valid == second.link.valid &&
                first.link.source_tick == second.link.source_tick &&
                near(first.link.source_position,
                     second.link.source_position) &&
                near(first.entity_b.position, second.entity_b.position) &&
                first.entity_b.revision == second.entity_b.revision &&
                first.outcome.final_status == second.outcome.final_status &&
                first.outcome.final_tick == second.outcome.final_tick &&
                first.outcome.final_committed_epoch ==
                    second.outcome.final_committed_epoch &&
                first.outcome.run_id != second.outcome.run_id,
            "independent two-entity runs are not deterministic apart from RunId");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--self-check") {
        std::cerr << "usage: gnc_kernel_two_entity_causal_probe "
                     "--self-check\n";
        return 2;
    }
    try {
        run();
        std::cout << "R3 two-entity causal transaction: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "R3 two-entity causal transaction: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
