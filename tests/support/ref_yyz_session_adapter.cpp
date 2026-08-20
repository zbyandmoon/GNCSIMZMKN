#include "ref_yyz_session_adapter.hpp"

#include "gnc/model_sdk/in_process_codec.hpp"

#include <yyz/mass_commit.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <functional>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace gnc::tests::ref_yyz {
namespace {

namespace yyz = gnc::packages::yyz;

using gnc::contracts::ExecutionPlanImage;
using gnc::contracts::PlanImageCallsite;
using gnc::contracts::PlanImageEntry;
using gnc::contracts::PlanImageInitialBinding;
using gnc::contracts::PlanImageInvocation;
using gnc::contracts::PlanImageOccurrence;
using gnc::contracts::PlanImageRuntimeComponent;
using gnc::contracts::PlanImageSlot;
using gnc::kernel::InProcessObjectLayout;
using gnc::kernel::InProcessObjectOperations;
using gnc::kernel::SessionMaterializationProvider;
using gnc::kernel::SessionObjectAccess;
using gnc::kernel::SessionObjectMaterializer;
using gnc::model_sdk::CanonicalConfigBlock;
using gnc::model_sdk::CanonicalConfigField;
using gnc::model_sdk::CanonicalEnumValue;

template <typename Value>
[[nodiscard]] const Value* find_handle(const std::vector<Value>& values,
                                       std::uint32_t handle) noexcept;
[[nodiscard]] const PlanImageOccurrence* find_occurrence(
    const ExecutionPlanImage& image, std::uint32_t handle) noexcept;
template <typename Call>
[[nodiscard]] Call exact_call(const ExecutionPlanImage& image,
                              std::uint32_t entry_handle);
template <typename Call>
[[nodiscard]] bool entry_is(const ExecutionPlanImage& image,
                            std::uint32_t entry_handle) noexcept;
[[nodiscard]] CanonicalConfigBlock canonical_configuration(
    const PlanImageOccurrence::ConfigBlock& image_configuration);
void record(const std::shared_ptr<MaterializationTrace>& trace,
            TraceAction action, TraceObjectKind kind,
            std::uint32_t handle);
[[nodiscard]] bool injects_failure(const AdapterOptions& options,
                                   FailurePhase phase,
                                   std::size_t ordinal) noexcept;
[[nodiscard]] yyz::AerodynamicTableAsset aerodynamic_asset();

class CompiledProvider final : public SessionMaterializationProvider {
  public:
    std::unordered_map<std::uint32_t,
                       std::shared_ptr<const SessionObjectMaterializer>>
        preparations;
    std::unordered_map<std::uint32_t,
                       std::shared_ptr<const SessionObjectMaterializer>>
        runtime_components;
    std::unordered_map<std::uint32_t,
                       std::shared_ptr<const SessionObjectMaterializer>>
        slots;
    std::unordered_map<std::uint32_t,
                       std::shared_ptr<const SessionObjectMaterializer>>
        initial_states;

    [[nodiscard]] const SessionObjectMaterializer* preparation(
        std::uint32_t handle) const noexcept override {
        return find(preparations, handle);
    }
    [[nodiscard]] const SessionObjectMaterializer* runtime_component(
        std::uint32_t handle) const noexcept override {
        return find(runtime_components, handle);
    }
    [[nodiscard]] const SessionObjectMaterializer* slot(
        std::uint32_t handle) const noexcept override {
        return find(slots, handle);
    }
    [[nodiscard]] const SessionObjectMaterializer* initial_state(
        std::uint32_t handle) const noexcept override {
        return find(initial_states, handle);
    }

  private:
    template <typename Map>
    [[nodiscard]] static const SessionObjectMaterializer* find(
        const Map& values, std::uint32_t handle) noexcept {
        const auto found = values.find(handle);
        return found == values.end() ? nullptr : found->second.get();
    }
};

template <typename Value, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_basic_materializer(
    std::string layout_identity, TraceObjectKind kind,
    std::uint32_t handle, std::shared_ptr<MaterializationTrace> trace,
    Construct construct);

template <typename Value, typename Codec, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_state_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const Codec& codec, std::uint32_t initial_binding_handle,
    std::shared_ptr<MaterializationTrace> trace, Construct construct);

template <typename Value>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_default_slot_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const gnc::model_sdk::TypedInProcessSlotCodec<Value>& codec,
    std::uint32_t slot_handle,
    std::shared_ptr<MaterializationTrace> trace);

[[nodiscard]] double initial_float(
    const PlanImageInitialBinding& binding, std::string_view field_id);
[[nodiscard]] std::int64_t initial_integer(
    const PlanImageInitialBinding& binding, std::string_view field_id);
[[nodiscard]] const std::string& initial_string(
    const PlanImageInitialBinding& binding, std::string_view field_id);
[[nodiscard]] gnc::contracts::DataQuality data_quality(
    std::string_view token);

template <typename Outcome>
[[nodiscard]] auto require_outcome(Outcome outcome, std::string_view detail)
    -> typename std::decay<decltype(outcome.value())>::type;

void build_preparations(
    const ExecutionPlanImage& image, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    for (std::size_t ordinal = 0U;
         ordinal < image.lifecycle().preparation_handles.size(); ++ordinal) {
        const auto handle = image.lifecycle().preparation_handles[ordinal];
        const auto* preparation = find_handle(image.preparations(), handle);
        if (preparation == nullptr) {
            throw std::runtime_error("preparation lifecycle handle is invalid");
        }
        const auto* occurrence =
            find_occurrence(image, preparation->occurrence_handle);
        if (occurrence == nullptr) {
            throw std::runtime_error("preparation occurrence is missing");
        }
        const auto configuration =
            canonical_configuration(occurrence->canonical_configuration);
        const bool fail = injects_failure(
            options, FailurePhase::Preparation, ordinal);

        if (entry_is<yyz::UniformEnvironmentPreparationCall>(
                image, preparation->prepare_entry_handle)) {
            auto definition = require_outcome(
                yyz::build_uniform_environment_definition(configuration),
                "uniform-environment definition rejected Image config");
            const auto prepare = exact_call<
                yyz::UniformEnvironmentPreparationCall>(
                image, preparation->prepare_entry_handle);
            provider.preparations.emplace(
                handle,
                make_basic_materializer<yyz::PreparedUniformEnvironmentModel>(
                    typeid(yyz::PreparedUniformEnvironmentModel).name(),
                    TraceObjectKind::Preparation, handle, trace,
                    [definition = std::move(definition), prepare, fail,
                     trace, handle](const SessionObjectAccess&,
                                    void* destination) mutable noexcept {
                        if (fail) {
                            record(trace, TraceAction::InjectedFailure,
                                   TraceObjectKind::Preparation, handle);
                            return false;
                        }
                        try {
                            auto outcome = prepare(definition);
                            if (!outcome.succeeded() || !outcome.has_value()) {
                                return false;
                            }
                            new (destination)
                                yyz::PreparedUniformEnvironmentModel(
                                    std::move(outcome.value()));
                            return true;
                        } catch (...) {
                            return false;
                        }
                    }));
            continue;
        }

        if (entry_is<yyz::AerodynamicTablePreparationCall>(
                image, preparation->prepare_entry_handle)) {
            auto table = aerodynamic_asset();
            if (!occurrence->asset_bindings.empty()) {
                table.asset_schema_id =
                    occurrence->asset_bindings.front().asset_schema_id;
                table.asset_id = occurrence->asset_bindings.front().asset_id;
            }
            auto definition = require_outcome(
                yyz::build_aerodynamic_table_definition(configuration,
                                                         table.asset_id),
                "aerodynamic definition rejected Image config");
            const auto prepare = exact_call<
                yyz::AerodynamicTablePreparationCall>(
                image, preparation->prepare_entry_handle);
            provider.preparations.emplace(
                handle,
                make_basic_materializer<yyz::PreparedAerodynamicTableModel>(
                    typeid(yyz::PreparedAerodynamicTableModel).name(),
                    TraceObjectKind::Preparation, handle, trace,
                    [definition = std::move(definition),
                     table = std::move(table), prepare, fail, trace,
                     handle](const SessionObjectAccess&,
                             void* destination) mutable noexcept {
                        if (fail) {
                            record(trace, TraceAction::InjectedFailure,
                                   TraceObjectKind::Preparation, handle);
                            return false;
                        }
                        try {
                            auto outcome = prepare(definition, table);
                            if (!outcome.succeeded() || !outcome.has_value()) {
                                return false;
                            }
                            new (destination)
                                yyz::PreparedAerodynamicTableModel(
                                    std::move(outcome.value()));
                            return true;
                        } catch (...) {
                            return false;
                        }
                    }));
            continue;
        }

        if (entry_is<yyz::ForceMomentClosurePreparationCall>(
                image, preparation->prepare_entry_handle)) {
            auto definition = require_outcome(
                yyz::build_force_moment_closure_definition(configuration),
                "closure definition rejected Image config");
            const auto prepare = exact_call<
                yyz::ForceMomentClosurePreparationCall>(
                image, preparation->prepare_entry_handle);
            provider.preparations.emplace(
                handle,
                make_basic_materializer<yyz::PreparedForceMomentClosureModel>(
                    typeid(yyz::PreparedForceMomentClosureModel).name(),
                    TraceObjectKind::Preparation, handle, trace,
                    [definition = std::move(definition), prepare, fail,
                     trace, handle](const SessionObjectAccess&,
                                    void* destination) mutable noexcept {
                        if (fail) {
                            record(trace, TraceAction::InjectedFailure,
                                   TraceObjectKind::Preparation, handle);
                            return false;
                        }
                        try {
                            auto outcome = prepare(definition);
                            if (!outcome.succeeded() || !outcome.has_value()) {
                                return false;
                            }
                            new (destination)
                                yyz::PreparedForceMomentClosureModel(
                                    std::move(outcome.value()));
                            return true;
                        } catch (...) {
                            return false;
                        }
                    }));
            continue;
        }
        throw std::runtime_error("unsupported REF-YYZ preparation entry");
    }
}

template <typename Value>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
slot_materializer_if(
    const ExecutionPlanImage& image, const PlanImageSlot& slot,
    std::string_view layout_identity,
    const std::shared_ptr<MaterializationTrace>& trace) {
    using Codec = gnc::model_sdk::TypedInProcessSlotCodec<Value>;
    using Getter = gnc::model_sdk::InProcessCodecGetter<Codec>;
    if (!entry_is<Getter>(image, slot.codec_entry_handle)) {
        return nullptr;
    }
    const auto getter = exact_call<Getter>(image, slot.codec_entry_handle);
    return make_default_slot_materializer<Value>(
        std::string(layout_identity), slot.codec_entry_handle, getter(),
        slot.handle, trace);
}

void build_slots(const ExecutionPlanImage& image,
                 const AdapterOptions& options,
                 const std::shared_ptr<MaterializationTrace>& trace,
                 CompiledProvider& provider,
                 RefYyzSessionAdapter& adapter) {
    bool first_non_state_seen = false;
    for (const auto& slot : image.slots()) {
        if (slot.kind == gnc::contracts::PlanImageSlotKind::CommittedState ||
            slot.kind == gnc::contracts::PlanImageSlotKind::CandidateState) {
            continue;
        }
        if (!first_non_state_seen) {
            adapter.first_non_state_slot_handle = slot.handle;
            first_non_state_seen = true;
            if (options.omit_first_slot_materializer) {
                continue;
            }
        }

        std::shared_ptr<const SessionObjectMaterializer> materializer;
        materializer = slot_materializer_if<yyz::CommittedRigidObservation>(
            image, slot, yyz::kRigidObservationLayoutIdentity, trace);
        if (materializer == nullptr) {
            materializer = slot_materializer_if<yyz::RigidFormInput>(
                image, slot, yyz::kRigidFormInputLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::ControlledRigidBoundaryPreparationOutput>(
                image, slot,
                yyz::kControlledRigidBoundaryPreparationLayoutIdentity,
                trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<yyz::MassPropertiesInput>(
                image, slot, yyz::kMassPropertiesLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::AltitudePitchGuidanceOutput>(
                image, slot, yyz::kGuidanceOutputLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::PitchMomentControllerOutput>(
                image, slot, yyz::kControllerOutputLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::IdealBodyMomentActuatorOutput>(
                image, slot, yyz::kActuatorOutputLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::SuppliedPropulsionBodyWrench>(
                image, slot, yyz::kPropulsionWrenchLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<yyz::MassFlowIntervalInput>(
                image, slot, yyz::kMassFlowLayoutIdentity, trace);
        }
        if (materializer == nullptr) {
            materializer = slot_materializer_if<
                yyz::CommittedMissionResultOutput>(
                image, slot, yyz::kMissionResultLayoutIdentity, trace);
            if (materializer != nullptr) {
                adapter.mission_result_slot_handle = slot.handle;
            }
        }
        if (materializer == nullptr) {
            throw std::runtime_error("unsupported REF-YYZ slot codec entry");
        }
        provider.slots.emplace(slot.handle, std::move(materializer));
    }
}

[[nodiscard]] yyz::RigidInitialStateInput rigid_initial_input(
    const PlanImageInitialBinding& binding) {
    yyz::RigidState state;
    state.position.value = gnc::foundation::Vec3{
        initial_float(binding, "position.x_meters"),
        initial_float(binding, "position.y_meters"),
        initial_float(binding, "position.z_meters")};
    state.velocity.value = gnc::foundation::Vec3{
        initial_float(binding, "velocity.x_meters_per_second"),
        initial_float(binding, "velocity.y_meters_per_second"),
        initial_float(binding, "velocity.z_meters_per_second")};
    state.attitude.value = gnc::foundation::quaternion_from_wxyz(
        initial_float(binding, "attitude.w"),
        initial_float(binding, "attitude.x"),
        initial_float(binding, "attitude.y"),
        initial_float(binding, "attitude.z"));
    state.angular_rate.value = gnc::foundation::Vec3{
        initial_float(binding, "angular_rate.x_radians_per_second"),
        initial_float(binding, "angular_rate.y_radians_per_second"),
        initial_float(binding, "angular_rate.z_radians_per_second")};
    return {std::move(state)};
}

[[nodiscard]] gnc::contracts::DataQuality data_quality(
    std::string_view token) {
    if (token == "Valid" || token == "valid") {
        return gnc::contracts::DataQuality::Valid;
    }
    if (token == "Degraded" || token == "degraded") {
        return gnc::contracts::DataQuality::Degraded;
    }
    if (token == "Invalid" || token == "invalid") {
        return gnc::contracts::DataQuality::Invalid;
    }
    throw std::runtime_error("unknown initial-state data quality");
}

[[nodiscard]] yyz::MassInitialStateInput mass_initial_input(
    const PlanImageInitialBinding& binding) {
    yyz::MassState state;
    state.context.frame.id = initial_string(binding, "context.frame_id");
    state.context.clock_domain.id =
        initial_string(binding, "context.clock_domain_id");
    state.context.sample_time.tick =
        initial_integer(binding, "context.sample_time.tick");
    state.context.sample_time.seconds =
        initial_float(binding, "context.sample_time.seconds");
    state.context.configuration_revision =
        initial_integer(binding, "context.configuration_revision");
    state.context.quality =
        data_quality(initial_string(binding, "context.quality"));
    state.mass_state_id = initial_string(binding, "mass_state_id");
    state.mass_kilograms = initial_float(binding, "mass_kilograms");
    state.body_origin_to_center_of_mass.value = gnc::foundation::Vec3{
        initial_float(binding,
                      "body_origin_to_center_of_mass.x_meters"),
        initial_float(binding,
                      "body_origin_to_center_of_mass.y_meters"),
        initial_float(binding,
                      "body_origin_to_center_of_mass.z_meters")};
    auto& inertia = state.inertia_about_center_of_mass.value;
    inertia(0, 0) = initial_float(
        binding,
        "inertia_about_center_of_mass.xx_kilogram_meters_squared");
    inertia(0, 1) = initial_float(
        binding,
        "inertia_about_center_of_mass.xy_kilogram_meters_squared");
    inertia(0, 2) = initial_float(
        binding,
        "inertia_about_center_of_mass.xz_kilogram_meters_squared");
    inertia(1, 0) = initial_float(
        binding,
        "inertia_about_center_of_mass.yx_kilogram_meters_squared");
    inertia(1, 1) = initial_float(
        binding,
        "inertia_about_center_of_mass.yy_kilogram_meters_squared");
    inertia(1, 2) = initial_float(
        binding,
        "inertia_about_center_of_mass.yz_kilogram_meters_squared");
    inertia(2, 0) = initial_float(
        binding,
        "inertia_about_center_of_mass.zx_kilogram_meters_squared");
    inertia(2, 1) = initial_float(
        binding,
        "inertia_about_center_of_mass.zy_kilogram_meters_squared");
    inertia(2, 2) = initial_float(
        binding,
        "inertia_about_center_of_mass.zz_kilogram_meters_squared");
    return {std::move(state)};
}

void build_initial_states(
    const ExecutionPlanImage& image, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider, RefYyzSessionAdapter& adapter) {
    for (std::size_t ordinal = 0U;
         ordinal < image.lifecycle().initial_binding_handles.size();
         ++ordinal) {
        const auto handle = image.lifecycle().initial_binding_handles[ordinal];
        const auto* binding = find_handle(image.initial_bindings(), handle);
        if (binding == nullptr) {
            throw std::runtime_error("initial binding handle is invalid");
        }
        const auto block_found = std::find_if(
            image.state_blocks().begin(), image.state_blocks().end(),
            [binding](const auto& block) {
                return block.committed_slot_handle ==
                       binding->committed_state_slot_handle;
            });
        if (block_found == image.state_blocks().end()) {
            throw std::runtime_error("initial binding has no state block");
        }
        const auto& block = *block_found;
        const auto component_found = std::find_if(
            image.runtime_components().begin(),
            image.runtime_components().end(), [&block](const auto& component) {
                return std::find(component.state_block_handles.begin(),
                                 component.state_block_handles.end(),
                                 block.handle) !=
                       component.state_block_handles.end();
            });
        const auto* occurrence =
            find_occurrence(image, binding->owner_occurrence_handle);
        if (component_found == image.runtime_components().end() ||
            occurrence == nullptr) {
            throw std::runtime_error("state owner component is missing");
        }
        const auto configuration =
            canonical_configuration(occurrence->canonical_configuration);
        const bool fail =
            injects_failure(options, FailurePhase::InitialState, ordinal);

        if (entry_is<yyz::RigidInitialStateCall>(
                image, binding->builder_entry_handle)) {
            const auto builder = exact_call<
                yyz::ControlledRigidDefinitionBuilderCall>(
                image, component_found->definition_builder_entry_handle);
            auto definition = require_outcome(
                builder(configuration),
                "rigid definition rejected Image configuration");
            auto input = rigid_initial_input(*binding);
            const auto initial = exact_call<yyz::RigidInitialStateCall>(
                image, binding->builder_entry_handle);
            const auto codec_getter = exact_call<yyz::RigidStateCodecGetter>(
                image, block.codec_entry_handle);
            provider.initial_states.emplace(
                handle,
                make_state_materializer<yyz::RigidState>(
                    std::string(yyz::kRigidStateLayoutIdentity),
                    block.codec_entry_handle, codec_getter(), handle, trace,
                    [algorithm = definition.rigid.algorithm,
                     input = std::move(input), initial, fail, trace,
                     handle](const SessionObjectAccess&,
                             void* destination) mutable noexcept {
                        if (fail) {
                            record(trace, TraceAction::InjectedFailure,
                                   TraceObjectKind::State, handle);
                            return false;
                        }
                        try {
                            auto outcome = initial(algorithm, input);
                            if (!outcome.succeeded() || !outcome.has_value()) {
                                return false;
                            }
                            new (destination)
                                yyz::RigidState(std::move(outcome.value()));
                            return true;
                        } catch (...) {
                            return false;
                        }
                    }));
            continue;
        }

        if (entry_is<yyz::MassInitialStateCall>(
                image, binding->builder_entry_handle)) {
            const auto builder = exact_call<
                yyz::ScalarBurnMassDefinitionBuilderCall>(
                image, component_found->definition_builder_entry_handle);
            auto definition = require_outcome(
                builder(configuration),
                "mass definition rejected Image configuration");
            auto input = mass_initial_input(*binding);
            const auto initial = exact_call<yyz::MassInitialStateCall>(
                image, binding->builder_entry_handle);
            const auto codec_getter = exact_call<yyz::MassStateCodecGetter>(
                image, block.codec_entry_handle);
            adapter.mass_state_block_handle = block.handle;
            provider.initial_states.emplace(
                handle,
                make_state_materializer<yyz::MassState>(
                    std::string(yyz::kMassStateLayoutIdentity),
                    block.codec_entry_handle, codec_getter(), handle, trace,
                    [definition = std::move(definition),
                     input = std::move(input), initial, fail, trace,
                     handle](const SessionObjectAccess&,
                             void* destination) mutable noexcept {
                        if (fail) {
                            record(trace, TraceAction::InjectedFailure,
                                   TraceObjectKind::State, handle);
                            return false;
                        }
                        try {
                            auto outcome = initial(definition, input);
                            if (!outcome.succeeded() || !outcome.has_value()) {
                                return false;
                            }
                            new (destination)
                                yyz::MassState(std::move(outcome.value()));
                            return true;
                        } catch (...) {
                            return false;
                        }
                    }));
            continue;
        }
        throw std::runtime_error("unsupported REF-YYZ initial-state entry");
    }
}

template <typename Value>
[[nodiscard]] const Value* find_handle(const std::vector<Value>& values,
                                       std::uint32_t handle) noexcept {
    const auto found = std::find_if(
        values.begin(), values.end(), [handle](const auto& value) {
            return value.handle == handle;
        });
    return found == values.end() ? nullptr : &*found;
}

[[nodiscard]] const PlanImageEntry* find_entry(
    const ExecutionPlanImage& image, std::uint32_t handle) noexcept {
    return find_handle(image.entries(), handle);
}

[[nodiscard]] const PlanImageOccurrence* find_occurrence(
    const ExecutionPlanImage& image, std::uint32_t handle) noexcept {
    return find_handle(image.occurrences(), handle);
}

template <typename Call>
[[nodiscard]] Call exact_call(const ExecutionPlanImage& image,
                              std::uint32_t entry_handle) {
    const auto* entry = find_entry(image, entry_handle);
    if (entry == nullptr) {
        throw std::runtime_error("missing exact Image entry");
    }
    const auto* typed = std::any_cast<Call>(&entry->typed_entry);
    if (typed == nullptr || *typed == nullptr) {
        throw std::runtime_error("wrong exact Image entry type");
    }
    return *typed;
}

template <typename Call>
[[nodiscard]] bool entry_is(const ExecutionPlanImage& image,
                            std::uint32_t entry_handle) noexcept {
    const auto* entry = find_entry(image, entry_handle);
    return entry != nullptr &&
           std::any_cast<Call>(&entry->typed_entry) != nullptr;
}

template <typename Call>
[[nodiscard]] const PlanImageCallsite& component_callsite(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component) {
    const auto found = std::find_if(
        component.callsite_handles.begin(), component.callsite_handles.end(),
        [&](std::uint32_t handle) {
            const auto* callsite = find_handle(image.callsites(), handle);
            return callsite != nullptr &&
                   entry_is<Call>(image, callsite->entry_handle);
        });
    if (found == component.callsite_handles.end()) {
        throw std::runtime_error("component lacks exact typed callsite");
    }
    return *find_handle(image.callsites(), *found);
}

template <typename Call>
[[nodiscard]] const PlanImageInvocation& component_invocation(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component) {
    const auto found = std::find_if(
        component.invocation_handles.begin(),
        component.invocation_handles.end(), [&](std::uint32_t handle) {
            const auto* invocation = find_handle(image.invocations(), handle);
            return invocation != nullptr &&
                   entry_is<Call>(image, invocation->entry_handle);
        });
    if (found == component.invocation_handles.end()) {
        throw std::runtime_error("component lacks exact typed invocation");
    }
    return *find_handle(image.invocations(), *found);
}

[[nodiscard]] CanonicalConfigBlock canonical_configuration(
    const PlanImageOccurrence::ConfigBlock& image_configuration) {
    CanonicalConfigBlock result;
    result.schema_id = image_configuration.schema_id;
    result.schema_version = image_configuration.schema_version;
    result.fields.reserve(image_configuration.fields.size());
    for (const auto& field : image_configuration.fields) {
        CanonicalConfigField converted;
        converted.field_id = field.field_id;
        switch (field.kind) {
        case gnc::contracts::PlanImageValueKind::String:
            converted.value = field.string_value;
            break;
        case gnc::contracts::PlanImageValueKind::Enum:
            converted.value = CanonicalEnumValue{field.string_value};
            break;
        case gnc::contracts::PlanImageValueKind::Integer:
            converted.value = field.integer_value;
            break;
        case gnc::contracts::PlanImageValueKind::Float64:
            converted.value = field.float64_value;
            break;
        }
        result.fields.push_back(std::move(converted));
    }
    return result;
}

[[nodiscard]] const gnc::contracts::PlanImageValue& initial_value(
    const PlanImageInitialBinding& binding, std::string_view field_id) {
    const auto found = std::find_if(
        binding.values.begin(), binding.values.end(),
        [field_id](const auto& value) { return value.field_id == field_id; });
    if (found == binding.values.end()) {
        throw std::runtime_error("initial-state field is missing");
    }
    return *found;
}

[[nodiscard]] double initial_float(const PlanImageInitialBinding& binding,
                                   std::string_view field_id) {
    const auto& value = initial_value(binding, field_id);
    if (value.kind != gnc::contracts::PlanImageValueKind::Float64) {
        throw std::runtime_error("initial-state field is not float64");
    }
    return value.float64_value;
}

[[nodiscard]] std::int64_t initial_integer(
    const PlanImageInitialBinding& binding, std::string_view field_id) {
    const auto& value = initial_value(binding, field_id);
    if (value.kind != gnc::contracts::PlanImageValueKind::Integer) {
        throw std::runtime_error("initial-state field is not integer");
    }
    return value.integer_value;
}

[[nodiscard]] const std::string& initial_string(
    const PlanImageInitialBinding& binding, std::string_view field_id) {
    const auto& value = initial_value(binding, field_id);
    if (value.kind != gnc::contracts::PlanImageValueKind::String &&
        value.kind != gnc::contracts::PlanImageValueKind::Enum) {
        throw std::runtime_error("initial-state field is not text");
    }
    return value.string_value;
}

void record(const std::shared_ptr<MaterializationTrace>& trace,
            TraceAction action, TraceObjectKind kind,
            std::uint32_t handle) {
    trace->events.push_back({action, kind, handle});
}

[[nodiscard]] bool injects_failure(const AdapterOptions& options,
                                   FailurePhase phase,
                                   std::size_t ordinal) noexcept {
    return options.failure.phase == phase &&
           options.failure.ordinal == ordinal;
}

template <typename Value>
class BasicOperations final : public InProcessObjectOperations {
  public:
    BasicOperations(std::string layout_identity,
                    std::uint32_t codec_entry_handle,
                    TraceObjectKind kind, std::uint32_t handle,
                    std::shared_ptr<MaterializationTrace> trace)
        : layout_identity_(std::move(layout_identity)),
          codec_entry_handle_(codec_entry_handle), kind_(kind),
          handle_(handle), trace_(std::move(trace)) {}

    [[nodiscard]] InProcessObjectLayout layout() const noexcept override {
        return {sizeof(Value), alignof(Value), layout_identity_,
                codec_entry_handle_, &typeid(Value),
                std::is_trivially_copyable<Value>::value,
                std::is_trivially_destructible<Value>::value};
    }

    [[nodiscard]] bool copy_construct(
        const void* source, void* destination) const noexcept override {
        try {
            new (destination) Value(*static_cast<const Value*>(source));
            record(trace_, TraceAction::CopyConstruct, kind_, handle_);
            record(trace_, TraceAction::Construct, kind_, handle_);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool replace(void*, const void*) const noexcept override {
        return false;
    }

    [[nodiscard]] bool validate(const void*) const noexcept override {
        return true;
    }

    void destroy(void* object) const noexcept override {
        static_cast<Value*>(object)->~Value();
        record(trace_, TraceAction::Destroy, kind_, handle_);
    }

    void constructed() const {
        record(trace_, TraceAction::Construct, kind_, handle_);
    }

  private:
    std::string layout_identity_;
    std::uint32_t codec_entry_handle_;
    TraceObjectKind kind_;
    std::uint32_t handle_;
    std::shared_ptr<MaterializationTrace> trace_;
};

template <typename Value>
class SlotOperations final : public InProcessObjectOperations {
  public:
    using Codec = gnc::model_sdk::TypedInProcessSlotCodec<Value>;

    SlotOperations(std::string layout_identity,
                   std::uint32_t codec_entry_handle, const Codec& codec,
                   std::uint32_t handle,
                   std::shared_ptr<MaterializationTrace> trace)
        : layout_identity_(std::move(layout_identity)),
          codec_entry_handle_(codec_entry_handle), codec_(codec),
          handle_(handle), trace_(std::move(trace)) {}

    [[nodiscard]] InProcessObjectLayout layout() const noexcept override {
        return {sizeof(Value), alignof(Value), layout_identity_,
                codec_entry_handle_, &typeid(Value),
                std::is_trivially_copyable<Value>::value,
                std::is_trivially_destructible<Value>::value};
    }

    [[nodiscard]] bool copy_construct(
        const void* source, void* destination) const noexcept override {
        try {
            new (destination)
                Value(codec_.copy(*static_cast<const Value*>(source)));
            record(trace_, TraceAction::CopyConstruct,
                   TraceObjectKind::Slot, handle_);
            record(trace_, TraceAction::Construct,
                   TraceObjectKind::Slot, handle_);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool replace(
        void* destination, const void* source) const noexcept override {
        try {
            Value replacement =
                codec_.copy(*static_cast<const Value*>(source));
            using std::swap;
            swap(*static_cast<Value*>(destination), replacement);
            record(trace_, TraceAction::Replace,
                   TraceObjectKind::Slot, handle_);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool validate(const void* object) const noexcept override {
        return codec_.validate != nullptr &&
               codec_.validate(*static_cast<const Value*>(object));
    }

    void destroy(void* object) const noexcept override {
        static_cast<Value*>(object)->~Value();
        record(trace_, TraceAction::Destroy, TraceObjectKind::Slot, handle_);
    }

    void constructed() const {
        record(trace_, TraceAction::Construct, TraceObjectKind::Slot, handle_);
    }

  private:
    std::string layout_identity_;
    std::uint32_t codec_entry_handle_;
    Codec codec_;
    std::uint32_t handle_;
    std::shared_ptr<MaterializationTrace> trace_;
};

template <typename Value, typename Codec>
class StateOperations final : public InProcessObjectOperations {
  public:
    StateOperations(std::string layout_identity,
                    std::uint32_t codec_entry_handle, const Codec& codec,
                    std::uint32_t handle,
                    std::shared_ptr<MaterializationTrace> trace)
        : layout_identity_(std::move(layout_identity)),
          codec_entry_handle_(codec_entry_handle), codec_(codec),
          handle_(handle), trace_(std::move(trace)) {}

    [[nodiscard]] InProcessObjectLayout layout() const noexcept override {
        return {sizeof(Value), alignof(Value), layout_identity_,
                codec_entry_handle_, &typeid(Value),
                std::is_trivially_copyable<Value>::value,
                std::is_trivially_destructible<Value>::value};
    }

    [[nodiscard]] bool copy_construct(
        const void* source, void* destination) const noexcept override {
        try {
            new (destination)
                Value(codec_.clone(*static_cast<const Value*>(source)));
            record(trace_, TraceAction::CopyConstruct,
                   TraceObjectKind::State, handle_);
            record(trace_, TraceAction::Construct,
                   TraceObjectKind::State, handle_);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool replace(
        void* destination, const void* source) const noexcept override {
        try {
            Value replacement =
                codec_.clone(*static_cast<const Value*>(source));
            codec_.noexcept_swap(*static_cast<Value*>(destination),
                                 replacement);
            record(trace_, TraceAction::Replace,
                   TraceObjectKind::State, handle_);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool validate(const void* object) const noexcept override {
        const auto& value = *static_cast<const Value*>(object);
        return codec_.validate != nullptr && codec_.validate_finite != nullptr &&
               codec_.validate_invariants != nullptr &&
               codec_.validate(value) && codec_.validate_finite(value) &&
               codec_.validate_invariants(value);
    }

    void destroy(void* object) const noexcept override {
        static_cast<Value*>(object)->~Value();
        record(trace_, TraceAction::Destroy, TraceObjectKind::State, handle_);
    }

    void constructed() const {
        record(trace_, TraceAction::Construct, TraceObjectKind::State, handle_);
    }

  private:
    std::string layout_identity_;
    std::uint32_t codec_entry_handle_;
    Codec codec_;
    std::uint32_t handle_;
    std::shared_ptr<MaterializationTrace> trace_;
};

class FixedMaterializer final : public SessionObjectMaterializer {
  public:
    using Construct =
        std::function<bool(const SessionObjectAccess&, void*)>;

    FixedMaterializer(std::shared_ptr<const InProcessObjectOperations> operations,
                      Construct construct)
        : operations_(std::move(operations)),
          construct_(std::move(construct)) {}

    [[nodiscard]] const InProcessObjectOperations& operations()
        const noexcept override {
        return *operations_;
    }

    [[nodiscard]] bool construct(const SessionObjectAccess& objects,
                                 void* destination) const noexcept override {
        try {
            return construct_(objects, destination);
        } catch (...) {
            return false;
        }
    }

  private:
    std::shared_ptr<const InProcessObjectOperations> operations_;
    Construct construct_;
};

template <typename Value, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_basic_materializer(
    std::string layout_identity, TraceObjectKind kind,
    std::uint32_t handle, std::shared_ptr<MaterializationTrace> trace,
    Construct construct) {
    auto operations = std::make_shared<BasicOperations<Value>>(
        std::move(layout_identity), 0U, kind, handle, trace);
    return std::make_shared<FixedMaterializer>(
        operations,
        [operations, construct = std::move(construct)](
            const SessionObjectAccess& objects,
            void* destination) mutable noexcept {
            if (!construct(objects, destination)) {
                return false;
            }
            operations->constructed();
            return true;
        });
}

template <typename Value>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_default_slot_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const gnc::model_sdk::TypedInProcessSlotCodec<Value>& codec,
    std::uint32_t slot_handle,
    std::shared_ptr<MaterializationTrace> trace) {
    auto operations = std::make_shared<SlotOperations<Value>>(
        std::move(layout_identity), codec_entry_handle, codec, slot_handle,
        trace);
    return std::make_shared<FixedMaterializer>(
        operations,
        [operations](const SessionObjectAccess&, void* destination) noexcept {
            try {
                new (destination) Value{};
                operations->constructed();
                return true;
            } catch (...) {
                return false;
            }
        });
}

template <typename Value, typename Codec, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_state_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const Codec& codec, std::uint32_t initial_binding_handle,
    std::shared_ptr<MaterializationTrace> trace, Construct construct) {
    auto operations = std::make_shared<StateOperations<Value, Codec>>(
        std::move(layout_identity), codec_entry_handle, codec,
        initial_binding_handle, trace);
    return std::make_shared<FixedMaterializer>(
        operations,
        [operations, construct = std::move(construct)](
            const SessionObjectAccess& objects,
            void* destination) mutable noexcept {
            if (!construct(objects, destination)) {
                return false;
            }
            operations->constructed();
            return true;
        });
}

[[nodiscard]] gnc::model_sdk::RuntimeCellFactoryContext factory_context(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component) {
    const auto* resources =
        find_handle(image.resource_plans(), component.resource_plan_handle);
    if (resources == nullptr) {
        throw std::runtime_error("runtime component lacks resource plan");
    }
    return {{component.runtime_instance_id}, component.handle,
            {resources->handle, resources->workspace_layout_id}};
}

[[nodiscard]] yyz::AerodynamicTableAsset aerodynamic_asset() {
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

template <typename Outcome>
[[nodiscard]] auto require_outcome(Outcome outcome, std::string_view detail)
    -> typename std::decay<decltype(outcome.value())>::type {
    if (!outcome.succeeded() || !outcome.has_value()) {
        throw std::runtime_error(std::string(detail));
    }
    return std::move(outcome.value());
}

} // namespace

std::size_t MaterializationTrace::live_object_count() const noexcept {
    std::size_t constructed = 0U;
    std::size_t destroyed = 0U;
    for (const auto& event : events) {
        if (event.action == TraceAction::Construct) {
            ++constructed;
        } else if (event.action == TraceAction::Destroy) {
            ++destroyed;
        }
    }
    return constructed >= destroyed ? constructed - destroyed : 0U;
}

std::vector<std::uint32_t> MaterializationTrace::constructed_handles(
    TraceObjectKind kind) const {
    std::vector<std::uint32_t> result;
    for (const auto& event : events) {
        if (event.kind == kind && event.action == TraceAction::Construct) {
            result.push_back(event.handle);
        }
    }
    return result;
}

std::vector<std::uint32_t> MaterializationTrace::destroyed_handles(
    TraceObjectKind kind) const {
    std::vector<std::uint32_t> result;
    for (const auto& event : events) {
        if (event.kind == kind && event.action == TraceAction::Destroy) {
            result.push_back(event.handle);
        }
    }
    return result;
}

namespace {

template <typename Value>
[[nodiscard]] gnc::model_sdk::CompiledOutputWriter<Value> output_writer(
    const PlanImageCallsite& callsite, std::size_t ordinal) {
    if (ordinal >= callsite.output_slot_handles.size() ||
        ordinal >= callsite.output_writer_token_handles.size()) {
        throw std::runtime_error("callsite lacks compiled output writer");
    }
    return {callsite.output_slot_handles[ordinal],
            {callsite.output_writer_token_handles[ordinal]}};
}

template <typename Value>
[[nodiscard]] std::uint32_t typed_input_slot(
    const ExecutionPlanImage& image,
    const PlanImageCallsite& callsite) {
    using Codec = gnc::model_sdk::TypedInProcessSlotCodec<Value>;
    using Getter = gnc::model_sdk::InProcessCodecGetter<Codec>;
    std::uint32_t result = 0U;
    for (const auto handle : callsite.input_slot_handles) {
        const auto* slot = find_handle(image.slots(), handle);
        if (slot != nullptr && entry_is<Getter>(image,
                                               slot->codec_entry_handle)) {
            if (result != 0U) {
                throw std::runtime_error("typed input slot is ambiguous");
            }
            result = handle;
        }
    }
    if (result == 0U) {
        throw std::runtime_error("typed input slot is missing");
    }
    return result;
}

[[nodiscard]] const PlanImageOccurrence& component_occurrence(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component) {
    const auto* occurrence =
        find_occurrence(image, component.occurrence_handle);
    if (occurrence == nullptr) {
        throw std::runtime_error("runtime occurrence is missing");
    }
    return *occurrence;
}

template <typename Cell, typename Factory, typename Definition,
          typename Bindings>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
fixed_runtime_materializer(
    const PlanImageRuntimeComponent& component,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    Factory factory, Definition definition, Bindings bindings, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace) {
    const auto handle = component.handle;
    return make_basic_materializer<Cell>(
        typeid(Cell).name(), TraceObjectKind::RuntimeCell, handle, trace,
        [context, factory, definition = std::move(definition),
         bindings = std::move(bindings), fail, trace,
         handle](const SessionObjectAccess&,
                 void* destination) mutable noexcept {
            if (fail) {
                record(trace, TraceAction::InjectedFailure,
                       TraceObjectKind::RuntimeCell, handle);
                return false;
            }
            try {
                auto outcome = factory(definition, context, bindings);
                if (!outcome.succeeded() || !outcome.has_value()) {
                    return false;
                }
                new (destination) Cell(std::move(outcome.value()));
                return true;
            } catch (...) {
                return false;
            }
        });
}

void build_guidance_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::AltitudePitchGuidanceDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "guidance definition rejected Image config");
    const auto factory = exact_call<
        yyz::AltitudePitchGuidanceRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& callsite =
        component_callsite<yyz::AltitudePitchGuidanceCall>(image, component);
    if (callsite.input_slot_handles.size() != 1U) {
        throw std::runtime_error("guidance callsite input shape changed");
    }
    yyz::AltitudePitchGuidanceRuntimeCellBindings bindings;
    bindings.boundary_evaluation_callsite_handle = callsite.handle;
    bindings.observation_input_slot_handle = callsite.input_slot_handles[0U];
    bindings.guidance_output =
        output_writer<yyz::AltitudePitchGuidanceOutput>(callsite, 0U);
    bindings.boundary_evaluation =
        exact_call<yyz::AltitudePitchGuidanceCall>(
            image, callsite.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<yyz::AltitudePitchGuidanceRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_controller_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::PitchMomentControllerDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "controller definition rejected Image config");
    const auto factory = exact_call<
        yyz::PitchMomentControllerRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& callsite = component_callsite<yyz::PitchMomentControllerCall>(
        image, component);
    if (callsite.input_slot_handles.size() != 1U) {
        throw std::runtime_error("controller callsite input shape changed");
    }
    yyz::PitchMomentControllerRuntimeCellBindings bindings;
    bindings.boundary_evaluation_callsite_handle = callsite.handle;
    bindings.guidance_input_slot_handle = callsite.input_slot_handles[0U];
    bindings.controller_output =
        output_writer<yyz::PitchMomentControllerOutput>(callsite, 0U);
    bindings.boundary_evaluation = exact_call<yyz::PitchMomentControllerCall>(
        image, callsite.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<yyz::PitchMomentControllerRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_actuator_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::IdealBodyMomentActuatorDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "actuator definition rejected Image config");
    const auto factory = exact_call<
        yyz::IdealBodyMomentActuatorRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& callsite =
        component_callsite<yyz::IdealBodyMomentActuatorCall>(image,
                                                             component);
    if (callsite.input_slot_handles.size() != 1U) {
        throw std::runtime_error("actuator callsite input shape changed");
    }
    yyz::IdealBodyMomentActuatorRuntimeCellBindings bindings;
    bindings.boundary_evaluation_callsite_handle = callsite.handle;
    bindings.controller_input_slot_handle = callsite.input_slot_handles[0U];
    bindings.actuator_output =
        output_writer<yyz::IdealBodyMomentActuatorOutput>(callsite, 0U);
    bindings.boundary_evaluation =
        exact_call<yyz::IdealBodyMomentActuatorCall>(
            image, callsite.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<
            yyz::IdealBodyMomentActuatorRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_propulsion_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::FixedSuppliedPropulsionDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "propulsion definition rejected Image config");
    const auto factory = exact_call<
        yyz::FixedSuppliedPropulsionRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& callsite =
        component_callsite<yyz::FixedSuppliedPropulsionCall>(image,
                                                             component);
    yyz::FixedSuppliedPropulsionRuntimeCellBindings bindings;
    bindings.boundary_evaluation_callsite_handle = callsite.handle;
    bindings.propulsion_wrench_output =
        output_writer<yyz::SuppliedPropulsionBodyWrench>(callsite, 0U);
    bindings.mass_flow_output =
        output_writer<yyz::MassFlowIntervalInput>(callsite, 1U);
    bindings.boundary_evaluation = exact_call<yyz::FixedSuppliedPropulsionCall>(
        image, callsite.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<
            yyz::FixedSuppliedPropulsionRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_evaluator_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::CommittedMissionResultDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "evaluator definition rejected Image config");
    const auto factory = exact_call<
        yyz::CommittedMissionResultRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& callsite = component_callsite<
        yyz::CommittedMissionHistoryEvaluationCall>(image, component);
    if (component.evaluator_history_handles.size() != 1U) {
        throw std::runtime_error("evaluator history shape changed");
    }
    yyz::CommittedMissionResultRuntimeCellBindings bindings;
    bindings.boundary_evaluation_callsite_handle = callsite.handle;
    bindings.committed_history_handle =
        component.evaluator_history_handles[0U];
    bindings.mission_result_output =
        output_writer<yyz::CommittedMissionResultOutput>(callsite, 0U);
    bindings.boundary_evaluation = exact_call<
        yyz::CommittedMissionHistoryEvaluationCall>(image,
                                                     callsite.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<
            yyz::CommittedMissionResultRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_mass_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    if (component.state_block_handles.size() != 1U ||
        component.transaction_handles.size() != 1U) {
        throw std::runtime_error("mass state/transaction shape changed");
    }
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::ScalarBurnMassDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "mass definition rejected Image config");
    const auto factory = exact_call<
        yyz::ScalarBurnMassRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& publish = component_callsite<yyz::MassPublishProjectionCall>(
        image, component);
    const auto& evolve = component_callsite<yyz::MassIntervalEvolutionCall>(
        image, component);
    const auto* transaction =
        find_handle(image.transactions(), component.transaction_handles[0U]);
    if (transaction == nullptr) {
        throw std::runtime_error("mass transaction is missing");
    }
    const auto candidate = std::find_if(
        transaction->candidates.begin(), transaction->candidates.end(),
        [&component](const auto& member) {
            return member.owner_occurrence_handle ==
                   component.occurrence_handle;
        });
    if (candidate == transaction->candidates.end()) {
        throw std::runtime_error("mass candidate writer is missing");
    }

    yyz::ScalarBurnMassRuntimeCellBindings bindings;
    bindings.state_block_handle = component.state_block_handles[0U];
    bindings.transaction_handle = component.transaction_handles[0U];
    bindings.publish_projection_callsite_handle = publish.handle;
    bindings.interval_evolution_callsite_handle = evolve.handle;
    bindings.mass_properties_output =
        output_writer<yyz::MassPropertiesInput>(publish, 0U);
    bindings.candidate_state_writer = {candidate->writer_token_handle};
    bindings.mass_flow_input_slot_handle =
        typed_input_slot<yyz::MassFlowIntervalInput>(image, evolve);
    bindings.publish_projection = exact_call<yyz::MassPublishProjectionCall>(
        image, publish.entry_handle);
    bindings.interval_evolution = exact_call<yyz::MassIntervalEvolutionCall>(
        image, evolve.entry_handle);
    provider.runtime_components.emplace(
        component.handle,
        fixed_runtime_materializer<yyz::ScalarBurnMassRuntimeCell>(
            component, factory_context(image, component), factory,
            std::move(definition), std::move(bindings), fail, trace));
}

void build_controlled_rigid_runtime(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component, bool fail,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    if (component.state_block_handles.size() != 1U ||
        component.integration_scope_handles.size() != 1U ||
        component.transaction_handles.size() != 1U ||
        component.interval_model_slot_handles.size() != 1U) {
        throw std::runtime_error("controlled rigid dependency shape changed");
    }
    const auto config = canonical_configuration(
        component_occurrence(image, component).canonical_configuration);
    const auto builder = exact_call<
        yyz::ControlledRigidDefinitionBuilderCall>(
        image, component.definition_builder_entry_handle);
    auto definition = require_outcome(
        builder(config), "controlled rigid definition rejected Image config");
    const auto factory = exact_call<
        yyz::ControlledRigidRuntimeCellFactoryCall>(
        image, component.runtime_cell_factory_entry_handle);
    const auto& publish = component_callsite<yyz::RigidPublishProjectionCall>(
        image, component);
    const auto& boundary = component_callsite<
        yyz::ControlledRigidBoundaryRuntimeCall>(image, component);
    const auto& derivative = component_callsite<yyz::RigidDerivativeCall>(
        image, component);
    const auto& environment = component_invocation<
        yyz::UniformEnvironmentQueryCall>(image, component);
    const auto& aerodynamic = component_invocation<
        yyz::AerodynamicTableQueryCall>(image, component);
    const auto& closure = component_invocation<yyz::ForceMomentClosureCall>(
        image, component);

    yyz::ControlledRigidRuntimeCellBindings bindings;
    bindings.state_block_handle = component.state_block_handles[0U];
    bindings.integration_scope_handle =
        component.integration_scope_handles[0U];
    bindings.transaction_handle = component.transaction_handles[0U];
    bindings.publish_projection_callsite_handle = publish.handle;
    bindings.boundary_evaluation_callsite_handle = boundary.handle;
    bindings.derivative_evaluation_callsite_handle = derivative.handle;
    bindings.observation_output =
        output_writer<yyz::CommittedRigidObservation>(publish, 0U);
    bindings.boundary_preparation_output = output_writer<
        yyz::ControlledRigidBoundaryPreparationOutput>(boundary, 0U);
    bindings.mass_properties_input_slot_handle =
        typed_input_slot<yyz::MassPropertiesInput>(image, boundary);
    bindings.propulsion_body_wrench_input_slot_handle =
        typed_input_slot<yyz::SuppliedPropulsionBodyWrench>(image, boundary);
    bindings.actuator_output_input_slot_handle =
        typed_input_slot<yyz::IdealBodyMomentActuatorOutput>(image, boundary);
    bindings.held_form_result_slot_handle =
        component.interval_model_slot_handles[0U];
    bindings.publish_projection = exact_call<yyz::RigidPublishProjectionCall>(
        image, publish.entry_handle);
    bindings.boundary_evaluation = exact_call<
        yyz::ControlledRigidBoundaryRuntimeCall>(image,
                                                 boundary.entry_handle);
    bindings.derivative_evaluation = exact_call<yyz::RigidDerivativeCall>(
        image, derivative.entry_handle);
    bindings.bound_invocations.environment = {
        environment.handle, environment.provider_plan_handle,
        environment.entry_handle, nullptr,
        exact_call<yyz::UniformEnvironmentQueryCall>(
            image, environment.entry_handle)};
    bindings.bound_invocations.frozen_form.aerodynamic = {
        aerodynamic.handle, aerodynamic.provider_plan_handle,
        aerodynamic.entry_handle, nullptr,
        exact_call<yyz::AerodynamicTableQueryCall>(
            image, aerodynamic.entry_handle)};
    bindings.bound_invocations.frozen_form.closure = {
        closure.handle, closure.provider_plan_handle, closure.entry_handle,
        nullptr,
        exact_call<yyz::ForceMomentClosureCall>(image,
                                                closure.entry_handle)};
    const auto context = factory_context(image, component);
    const auto handle = component.handle;
    const auto environment_preparation =
        environment.provider_preparation_handle;
    const auto aerodynamic_preparation =
        aerodynamic.provider_preparation_handle;
    const auto closure_preparation = closure.provider_preparation_handle;
    provider.runtime_components.emplace(
        handle,
        make_basic_materializer<yyz::ControlledRigidRuntimeCell>(
            typeid(yyz::ControlledRigidRuntimeCell).name(),
            TraceObjectKind::RuntimeCell, handle, trace,
            [context, factory, definition = std::move(definition),
             bindings = std::move(bindings), fail, trace, handle,
             environment_preparation, aerodynamic_preparation,
             closure_preparation](const SessionObjectAccess& objects,
                                  void* destination) mutable noexcept {
                if (fail) {
                    record(trace, TraceAction::InjectedFailure,
                           TraceObjectKind::RuntimeCell, handle);
                    return false;
                }
                auto compiled = bindings;
                compiled.bound_invocations.environment.prepared_model =
                    static_cast<const yyz::PreparedUniformEnvironmentModel*>(
                        objects.prepared_object(environment_preparation));
                compiled.bound_invocations.frozen_form.aerodynamic
                    .prepared_model =
                    static_cast<const yyz::PreparedAerodynamicTableModel*>(
                        objects.prepared_object(aerodynamic_preparation));
                compiled.bound_invocations.frozen_form.closure
                    .prepared_model =
                    static_cast<const yyz::PreparedForceMomentClosureModel*>(
                        objects.prepared_object(closure_preparation));
                try {
                    auto outcome =
                        factory(definition, context, compiled);
                    if (!outcome.succeeded() || !outcome.has_value()) {
                        return false;
                    }
                    new (destination) yyz::ControlledRigidRuntimeCell(
                        std::move(outcome.value()));
                    return true;
                } catch (...) {
                    return false;
                }
            }));
}

void build_runtime_components(
    const ExecutionPlanImage& image, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    CompiledProvider& provider) {
    for (std::size_t ordinal = 0U;
         ordinal < image.lifecycle().runtime_component_handles.size();
         ++ordinal) {
        const auto handle =
            image.lifecycle().runtime_component_handles[ordinal];
        const auto* component =
            find_handle(image.runtime_components(), handle);
        if (component == nullptr) {
            throw std::runtime_error("runtime lifecycle handle is invalid");
        }
        const bool fail =
            injects_failure(options, FailurePhase::RuntimeCell, ordinal);
        const auto entry_handle =
            component->runtime_cell_factory_entry_handle;
        if (entry_is<yyz::ControlledRigidRuntimeCellFactoryCall>(
                image, entry_handle)) {
            build_controlled_rigid_runtime(image, *component, fail, trace,
                                           provider);
        } else if (entry_is<yyz::ScalarBurnMassRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_mass_runtime(image, *component, fail, trace, provider);
        } else if (entry_is<
                       yyz::AltitudePitchGuidanceRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_guidance_runtime(image, *component, fail, trace, provider);
        } else if (entry_is<
                       yyz::PitchMomentControllerRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_controller_runtime(image, *component, fail, trace, provider);
        } else if (entry_is<
                       yyz::IdealBodyMomentActuatorRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_actuator_runtime(image, *component, fail, trace, provider);
        } else if (entry_is<
                       yyz::FixedSuppliedPropulsionRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_propulsion_runtime(image, *component, fail, trace, provider);
        } else if (entry_is<
                       yyz::CommittedMissionResultRuntimeCellFactoryCall>(
                       image, entry_handle)) {
            build_evaluator_runtime(image, *component, fail, trace, provider);
        } else {
            throw std::runtime_error("unsupported REF-YYZ Runtime Cell entry");
        }
    }
}

} // namespace

RefYyzSessionAdapter make_session_adapter(
    const ExecutionPlanImage& image, AdapterOptions options) {
    RefYyzSessionAdapter result;
    result.trace = std::make_shared<MaterializationTrace>();
    try {
        auto provider = std::make_shared<CompiledProvider>();
        build_preparations(image, options, result.trace, *provider);
        build_runtime_components(image, options, result.trace, *provider);
        build_slots(image, options, result.trace, *provider, result);
        build_initial_states(image, options, result.trace, *provider, result);
        result.provider = std::move(provider);
    } catch (const std::exception& error) {
        result.error = error.what();
        result.provider.reset();
    } catch (...) {
        result.error = "unknown REF-YYZ adapter construction failure";
        result.provider.reset();
    }
    return result;
}

NonTrivialObjectProbe exercise_non_trivial_objects(
    kernel::Session& session, const RefYyzSessionAdapter& adapter) {
    NonTrivialObjectProbe result;
    result.state_is_non_trivial =
        !std::is_trivially_copyable<yyz::MassState>::value &&
        !std::is_trivially_destructible<yyz::MassState>::value;
    result.output_is_non_trivial =
        !std::is_trivially_copyable<yyz::CommittedMissionResultOutput>::value &&
        !std::is_trivially_destructible<
            yyz::CommittedMissionResultOutput>::value;

    const auto* committed = static_cast<const yyz::MassState*>(
        session.committed_state_object(adapter.mass_state_block_handle));
    auto* candidate = const_cast<yyz::MassState*>(
        static_cast<const yyz::MassState*>(
            session.candidate_state_object(adapter.mass_state_block_handle)));
    if (committed != nullptr && candidate != nullptr) {
        result.initial_mass_string_present =
            !committed->mass_state_id.empty() &&
            candidate->mass_state_id == committed->mass_state_id;
        candidate->mass_state_id = "temporary.session-local.mass-state";
        const auto clone = session.clone_committed_to_candidate(
            adapter.mass_state_block_handle);
        const auto* restored = static_cast<const yyz::MassState*>(
            session.candidate_state_object(adapter.mass_state_block_handle));
        result.state_clone_restored_string =
            static_cast<bool>(clone) && restored != nullptr &&
            restored->mass_state_id == committed->mass_state_id;
    }

    yyz::CommittedMissionResultOutput first;
    first.termination.reason_code = "first-session-result";
    first.terminal_predicates[0U].predicate_id = "predicate.first";
    first.terminal_predicates[0U].reason_code = "reason.first";
    const auto first_replace = session.replace_slot(
        adapter.mission_result_slot_handle,
        {&first, sizeof(first), alignof(decltype(first)), &typeid(first)});
    result.first_output_replace_succeeded =
        static_cast<bool>(first_replace);

    yyz::CommittedMissionResultOutput second;
    second.termination.reason_code = "replacement-session-result";
    second.terminal_predicates[0U].predicate_id = "predicate.replacement";
    second.terminal_predicates[0U].reason_code = "reason.replacement";
    const auto second_replace = session.replace_slot(
        adapter.mission_result_slot_handle,
        {&second, sizeof(second), alignof(decltype(second)), &typeid(second)});
    result.second_output_replace_succeeded =
        static_cast<bool>(second_replace);
    const auto* stored = static_cast<const yyz::CommittedMissionResultOutput*>(
        session.slot_object(adapter.mission_result_slot_handle));
    result.output_string_replaced =
        stored != nullptr &&
        stored->termination.reason_code == "replacement-session-result" &&
        stored->terminal_predicates[0U].predicate_id ==
            "predicate.replacement" &&
        stored->terminal_predicates[0U].reason_code ==
            "reason.replacement";
    return result;
}

} // namespace gnc::tests::ref_yyz
