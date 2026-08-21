#include "ref_yyz_session_adapter.hpp"
#include "session_qualification_access.hpp"

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
using gnc::kernel::SessionMaterializerIdentity;
using gnc::kernel::SessionInvocationContext;
using gnc::kernel::SessionInvocationEntry;
using gnc::kernel::SessionInvocationIdentity;
using gnc::kernel::SessionObjectAccess;
using gnc::kernel::SessionObjectMaterializer;
using gnc::kernel::SessionObjectRequirement;
using gnc::kernel::SessionObjectRole;
using gnc::kernel::SessionResult;
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
template <typename Value>
[[nodiscard]] const Value* checked_prepared(
    const SessionObjectAccess& objects, std::uint32_t preparation_handle,
    std::uint32_t prepare_entry_handle) noexcept;
[[nodiscard]] CanonicalConfigBlock canonical_configuration(
    const PlanImageOccurrence::ConfigBlock& image_configuration);
void record(const std::shared_ptr<MaterializationTrace>& trace,
            TraceAction action, TraceObjectKind kind,
            std::uint32_t handle) noexcept;
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
    std::unordered_map<std::uint32_t,
                       std::shared_ptr<const SessionInvocationEntry>>
        invocations;

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
    [[nodiscard]] const SessionInvocationEntry* invocation(
        std::uint32_t handle) const noexcept override {
        return find(invocations, handle);
    }

  private:
    template <typename Map>
    [[nodiscard]] static const SessionObjectMaterializer* find(
        const Map& values, std::uint32_t handle) noexcept {
        const auto found = values.find(handle);
        return found == values.end() ? nullptr : found->second.get();
    }

    [[nodiscard]] static const SessionInvocationEntry* find(
        const std::unordered_map<
            std::uint32_t,
            std::shared_ptr<const SessionInvocationEntry>>& values,
        std::uint32_t handle) noexcept {
        const auto found = values.find(handle);
        return found == values.end() ? nullptr : found->second.get();
    }
};

template <typename Value, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_basic_materializer(
    std::string layout_identity, TraceObjectKind kind,
    std::uint32_t handle, std::shared_ptr<MaterializationTrace> trace,
    SessionMaterializerIdentity identity, Construct construct,
    std::vector<SessionObjectRequirement> dependencies = {});

template <typename Value, typename Codec, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_state_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const Codec& codec, std::uint32_t initial_binding_handle,
    std::uint32_t builder_entry_handle,
    std::shared_ptr<MaterializationTrace> trace, Construct construct);

template <typename Value>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_default_slot_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const gnc::model_sdk::TypedInProcessSlotCodec<Value>& codec,
    std::uint32_t slot_handle, SessionObjectRole role,
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
                    {handle, SessionObjectRole::PreparedModel,
                     preparation->prepare_entry_handle, 0U},
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
                    {handle, SessionObjectRole::PreparedModel,
                     preparation->prepare_entry_handle, 0U},
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
                    {handle, SessionObjectRole::PreparedModel,
                     preparation->prepare_entry_handle, 0U},
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
    if (slot.layout_id != layout_identity ||
        !entry_is<Getter>(image, slot.codec_entry_handle)) {
        return nullptr;
    }
    const auto getter = exact_call<Getter>(image, slot.codec_entry_handle);
    const auto role =
        slot.kind == gnc::contracts::PlanImageSlotKind::HeldIntervalValue
            ? SessionObjectRole::HeldIntervalValue
        : slot.storage_class ==
                  gnc::contracts::SlotStorageClass::TerminalResult
            ? SessionObjectRole::TerminalOutputValue
            : SessionObjectRole::CycleFrameValue;
    return make_default_slot_materializer<Value>(
        std::string(layout_identity), slot.codec_entry_handle, getter(),
        slot.handle, role, trace);
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
                    block.codec_entry_handle, codec_getter(), handle,
                    binding->builder_entry_handle, trace,
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
                    block.codec_entry_handle, codec_getter(), handle,
                    binding->builder_entry_handle, trace,
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

template <typename Value>
[[nodiscard]] const Value* checked_prepared(
    const SessionObjectAccess& objects, std::uint32_t preparation_handle,
    std::uint32_t prepare_entry_handle) noexcept {
    const auto view = objects.prepared_object(preparation_handle);
    if (!view || view.image_object_handle != preparation_handle ||
        view.role != SessionObjectRole::PreparedModel ||
        view.linked_entry_handle != prepare_entry_handle ||
        view.codec_entry_handle != 0U ||
        view.size_bytes != sizeof(Value) ||
        view.alignment_bytes != alignof(Value) ||
        view.type_identity != &typeid(Value)) {
        return nullptr;
    }
    return static_cast<const Value*>(view.address);
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
            std::uint32_t handle) noexcept {
    try {
        trace->events.push_back({action, kind, handle});
    } catch (...) {
        // Trace evidence is observational and cannot compromise object cleanup.
    }
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

    FixedMaterializer(
        std::shared_ptr<const InProcessObjectOperations> operations,
        SessionMaterializerIdentity identity, Construct construct,
        std::vector<SessionObjectRequirement> dependencies)
        : operations_(std::move(operations)), identity_(identity),
          construct_(std::move(construct)),
          dependencies_(std::move(dependencies)) {}

    [[nodiscard]] SessionMaterializerIdentity identity()
        const noexcept override {
        return identity_;
    }

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

    [[nodiscard]] std::size_t dependency_count() const noexcept override {
        return dependencies_.size();
    }

    [[nodiscard]] SessionObjectRequirement dependency(
        std::size_t index) const noexcept override {
        return index < dependencies_.size() ? dependencies_[index]
                                            : SessionObjectRequirement{};
    }

  private:
    std::shared_ptr<const InProcessObjectOperations> operations_;
    SessionMaterializerIdentity identity_;
    Construct construct_;
    std::vector<SessionObjectRequirement> dependencies_;
};

class IdentityOverrideMaterializer final
    : public SessionObjectMaterializer {
  public:
    IdentityOverrideMaterializer(
        std::shared_ptr<const SessionObjectMaterializer> source,
        SessionMaterializerIdentity identity)
        : source_(std::move(source)), identity_(identity) {}

    [[nodiscard]] SessionMaterializerIdentity identity()
        const noexcept override {
        return identity_;
    }
    [[nodiscard]] const InProcessObjectOperations& operations()
        const noexcept override {
        return source_->operations();
    }
    [[nodiscard]] std::size_t dependency_count() const noexcept override {
        return source_->dependency_count();
    }
    [[nodiscard]] SessionObjectRequirement dependency(
        std::size_t index) const noexcept override {
        return source_->dependency(index);
    }
    [[nodiscard]] bool construct(const SessionObjectAccess& objects,
                                 void* destination) const noexcept override {
        return source_->construct(objects, destination);
    }

  private:
    std::shared_ptr<const SessionObjectMaterializer> source_;
    SessionMaterializerIdentity identity_;
};

class FixedInvocation final : public SessionInvocationEntry {
  public:
    using Invoke = std::function<SessionResult(
        const SessionInvocationContext&)>;

    FixedInvocation(SessionInvocationIdentity identity, Invoke invoke)
        : identity_(identity), invoke_(std::move(invoke)) {}

    [[nodiscard]] SessionInvocationIdentity identity()
        const noexcept override {
        return identity_;
    }

    [[nodiscard]] SessionResult invoke(
        const SessionInvocationContext& context) const noexcept override {
        try {
            return invoke_(context);
        } catch (...) {
            return {gnc::kernel::SessionError::InvocationFailed,
                    identity_.callsite_handle,
                    "typed callsite threw unexpectedly"};
        }
    }

  private:
    SessionInvocationIdentity identity_;
    Invoke invoke_;
};

template <typename Value>
[[nodiscard]] const Value* checked_runtime(
    const SessionInvocationContext& context,
    std::uint32_t factory_entry_handle) noexcept {
    const auto& view = context.runtime_cell();
    if (!view || view.role != SessionObjectRole::RuntimeCell ||
        view.image_object_handle != context.component_handle() ||
        view.linked_entry_handle != factory_entry_handle ||
        view.codec_entry_handle != 0U || view.size_bytes != sizeof(Value) ||
        view.alignment_bytes != alignof(Value) ||
        view.type_identity != &typeid(Value)) {
        return nullptr;
    }
    return static_cast<const Value*>(view.address);
}

template <typename Value>
[[nodiscard]] const Value* checked_committed(
    const SessionInvocationContext& context,
    std::uint32_t state_block_handle, SessionResult& result) noexcept {
    gnc::kernel::SessionObjectIdentityView view;
    result = context.committed().read(state_block_handle, view);
    if (!result) {
        return nullptr;
    }
    if (!view || view.role != SessionObjectRole::CommittedState ||
        view.image_object_handle != state_block_handle ||
        view.size_bytes != sizeof(Value) ||
        view.alignment_bytes != alignof(Value) ||
        view.type_identity != &typeid(Value)) {
        result = {gnc::kernel::SessionError::ObjectTypeMismatch,
                  state_block_handle,
                  "committed state type mismatch"};
        return nullptr;
    }
    return static_cast<const Value*>(view.address);
}

template <typename Value>
[[nodiscard]] const Value* checked_input(
    const SessionInvocationContext& context, std::uint32_t slot_handle,
    SessionResult& result) noexcept {
    gnc::kernel::SessionObjectIdentityView view;
    result = context.inputs().read(slot_handle, view);
    if (!result) {
        return nullptr;
    }
    if (!view || view.image_object_handle != slot_handle ||
        view.size_bytes != sizeof(Value) ||
        view.alignment_bytes != alignof(Value) ||
        view.type_identity != &typeid(Value)) {
        result = {gnc::kernel::SessionError::ObjectTypeMismatch, slot_handle,
                  "frame input type mismatch"};
        return nullptr;
    }
    return static_cast<const Value*>(view.address);
}

template <typename Value>
[[nodiscard]] SessionResult write_value(
    const SessionInvocationContext& context, std::uint32_t slot_handle,
    std::uint32_t writer_token_handle, const Value& value) noexcept {
    return context.outputs().write(
        slot_handle, writer_token_handle,
        {&value, sizeof(Value), alignof(Value), &typeid(Value)});
}

template <typename Vector>
[[nodiscard]] std::array<double, 3U> vector3(const Vector& value) noexcept {
    return {value(0), value(1), value(2)};
}

template <typename Matrix>
[[nodiscard]] std::array<double, 9U> matrix3(const Matrix& value) noexcept {
    return {value(0, 0), value(0, 1), value(0, 2),
            value(1, 0), value(1, 1), value(1, 2),
            value(2, 0), value(2, 1), value(2, 2)};
}

[[nodiscard]] BoundaryContextProbe context_probe(
    const gnc::contracts::SampleContext& sample, double interval_start,
    double interval_end) noexcept {
    return {sample.sample_time.tick,
            sample.sample_time.seconds,
            interval_start,
            interval_end,
            sample.configuration_revision,
            sample.quality == gnc::contracts::DataQuality::Valid};
}

[[nodiscard]] std::size_t boundary_ordinal(
    const std::shared_ptr<OpeningBoundaryProbe>& probe) noexcept {
    return probe->call_order.size();
}

[[nodiscard]] std::uint32_t selected_writer_token(
    const AdapterOptions& options, std::size_t ordinal,
    std::uint32_t expected) noexcept {
    return options.wrong_writer_token_boundary_ordinal == ordinal
               ? expected + 1U
               : expected;
}

[[nodiscard]] gnc::contracts::SampleContext sample_context(
    const SessionInvocationContext& context,
    const gnc::contracts::FrameIdentity& frame,
    const gnc::contracts::ClockDomainIdentity& clock_domain,
    std::int64_t configuration_revision) {
    return {frame,
            clock_domain,
            {context.tick(), context.boundary_time_seconds()},
            configuration_revision,
            context.quality()};
}

[[nodiscard]] gnc::contracts::IntervalSampleContext interval_context(
    const SessionInvocationContext& context,
    const gnc::contracts::FrameIdentity& frame,
    const gnc::contracts::ClockDomainIdentity& clock_domain,
    std::int64_t configuration_revision) {
    return {sample_context(context, frame, clock_domain,
                           configuration_revision),
            {{context.tick(), context.interval_start_seconds()},
             {context.tick() + 1, context.interval_end_seconds()}}};
}

template <typename Value, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_basic_materializer(
    std::string layout_identity, TraceObjectKind kind,
    std::uint32_t handle, std::shared_ptr<MaterializationTrace> trace,
    SessionMaterializerIdentity identity, Construct construct,
    std::vector<SessionObjectRequirement> dependencies) {
    auto operations = std::make_shared<BasicOperations<Value>>(
        std::move(layout_identity), 0U, kind, handle, trace);
    return std::make_shared<FixedMaterializer>(
        operations, identity,
        [operations, construct = std::move(construct)](
            const SessionObjectAccess& objects,
            void* destination) mutable noexcept {
            if (!construct(objects, destination)) {
                return false;
            }
            operations->constructed();
            return true;
        },
        std::move(dependencies));
}

template <typename Value>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_default_slot_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const gnc::model_sdk::TypedInProcessSlotCodec<Value>& codec,
    std::uint32_t slot_handle, SessionObjectRole role,
    std::shared_ptr<MaterializationTrace> trace) {
    auto operations = std::make_shared<SlotOperations<Value>>(
        std::move(layout_identity), codec_entry_handle, codec, slot_handle,
        trace);
    return std::make_shared<FixedMaterializer>(
        operations,
        SessionMaterializerIdentity{slot_handle, role, 0U,
                                    codec_entry_handle},
        [operations](const SessionObjectAccess&, void* destination) noexcept {
            try {
                new (destination) Value{};
                operations->constructed();
                return true;
            } catch (...) {
                return false;
            }
        },
        std::vector<SessionObjectRequirement>{});
}

template <typename Value, typename Codec, typename Construct>
[[nodiscard]] std::shared_ptr<const SessionObjectMaterializer>
make_state_materializer(
    std::string layout_identity, std::uint32_t codec_entry_handle,
    const Codec& codec, std::uint32_t initial_binding_handle,
    std::uint32_t builder_entry_handle,
    std::shared_ptr<MaterializationTrace> trace, Construct construct) {
    auto operations = std::make_shared<StateOperations<Value, Codec>>(
        std::move(layout_identity), codec_entry_handle, codec,
        initial_binding_handle, trace);
    return std::make_shared<FixedMaterializer>(
        operations,
        SessionMaterializerIdentity{
            initial_binding_handle, SessionObjectRole::InitialStateValue,
            builder_entry_handle, codec_entry_handle},
        [operations, construct = std::move(construct)](
            const SessionObjectAccess& objects,
            void* destination) mutable noexcept {
            if (!construct(objects, destination)) {
                return false;
            }
            operations->constructed();
            return true;
        },
        std::vector<SessionObjectRequirement>{});
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

template <typename Value>
[[nodiscard]] gnc::model_sdk::CompiledOutputWriter<Value>
typed_output_writer(const ExecutionPlanImage& image,
                    const PlanImageCallsite& callsite) {
    using Codec = gnc::model_sdk::TypedInProcessSlotCodec<Value>;
    using Getter = gnc::model_sdk::InProcessCodecGetter<Codec>;
    std::size_t result = callsite.output_slot_handles.size();
    for (std::size_t index = 0U;
         index < callsite.output_slot_handles.size(); ++index) {
        const auto* slot = find_handle(image.slots(),
                                       callsite.output_slot_handles[index]);
        if (slot != nullptr &&
            entry_is<Getter>(image, slot->codec_entry_handle)) {
            if (result != callsite.output_slot_handles.size()) {
                throw std::runtime_error("typed output writer is ambiguous");
            }
            result = index;
        }
    }
    if (result == callsite.output_slot_handles.size() ||
        result >= callsite.output_writer_token_handles.size()) {
        throw std::runtime_error("typed output writer is missing");
    }
    return {callsite.output_slot_handles[result],
            {callsite.output_writer_token_handles[result]}};
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
        {handle, SessionObjectRole::RuntimeCell,
         component.runtime_cell_factory_entry_handle, 0U},
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
        typed_output_writer<yyz::SuppliedPropulsionBodyWrench>(image,
                                                               callsite);
    bindings.mass_flow_output =
        typed_output_writer<yyz::MassFlowIntervalInput>(image, callsite);
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
    const auto* environment_preparation_plan =
        find_handle(image.preparations(), environment_preparation);
    const auto* aerodynamic_preparation_plan =
        find_handle(image.preparations(), aerodynamic_preparation);
    const auto* closure_preparation_plan =
        find_handle(image.preparations(), closure_preparation);
    if (environment_preparation_plan == nullptr ||
        aerodynamic_preparation_plan == nullptr ||
        closure_preparation_plan == nullptr) {
        throw std::runtime_error("controlled runtime preparation is missing");
    }
    const auto environment_prepare_entry =
        environment_preparation_plan->prepare_entry_handle;
    const auto aerodynamic_prepare_entry =
        aerodynamic_preparation_plan->prepare_entry_handle;
    const auto closure_prepare_entry =
        closure_preparation_plan->prepare_entry_handle;
    std::vector<SessionObjectRequirement> dependencies = {
        {environment_preparation, SessionObjectRole::PreparedModel,
         environment_prepare_entry, 0U,
         sizeof(yyz::PreparedUniformEnvironmentModel),
         alignof(yyz::PreparedUniformEnvironmentModel),
         &typeid(yyz::PreparedUniformEnvironmentModel)},
        {aerodynamic_preparation, SessionObjectRole::PreparedModel,
         aerodynamic_prepare_entry, 0U,
         sizeof(yyz::PreparedAerodynamicTableModel),
         alignof(yyz::PreparedAerodynamicTableModel),
         &typeid(yyz::PreparedAerodynamicTableModel)},
        {closure_preparation, SessionObjectRole::PreparedModel,
         closure_prepare_entry, 0U,
         sizeof(yyz::PreparedForceMomentClosureModel),
         alignof(yyz::PreparedForceMomentClosureModel),
         &typeid(yyz::PreparedForceMomentClosureModel)}};
    provider.runtime_components.emplace(
        handle,
        make_basic_materializer<yyz::ControlledRigidRuntimeCell>(
            typeid(yyz::ControlledRigidRuntimeCell).name(),
            TraceObjectKind::RuntimeCell, handle, trace,
            {handle, SessionObjectRole::RuntimeCell,
             component.runtime_cell_factory_entry_handle, 0U},
            [context, factory, definition = std::move(definition),
             bindings = std::move(bindings), fail, trace, handle,
             environment_preparation, aerodynamic_preparation,
             closure_preparation, environment_prepare_entry,
             aerodynamic_prepare_entry,
             closure_prepare_entry](const SessionObjectAccess& objects,
                                   void* destination) mutable noexcept {
                if (fail) {
                    record(trace, TraceAction::InjectedFailure,
                           TraceObjectKind::RuntimeCell, handle);
                    return false;
                }
                auto compiled = bindings;
                compiled.bound_invocations.environment.prepared_model =
                    checked_prepared<yyz::PreparedUniformEnvironmentModel>(
                        objects, environment_preparation,
                        environment_prepare_entry);
                compiled.bound_invocations.frozen_form.aerodynamic
                    .prepared_model =
                    checked_prepared<yyz::PreparedAerodynamicTableModel>(
                        objects, aerodynamic_preparation,
                        aerodynamic_prepare_entry);
                compiled.bound_invocations.frozen_form.closure
                    .prepared_model =
                    checked_prepared<yyz::PreparedForceMomentClosureModel>(
                        objects, closure_preparation,
                        closure_prepare_entry);
                if (compiled.bound_invocations.environment.prepared_model ==
                        nullptr ||
                    compiled.bound_invocations.frozen_form.aerodynamic
                            .prepared_model == nullptr ||
                    compiled.bound_invocations.frozen_form.closure
                            .prepared_model == nullptr) {
                    return false;
                }
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
            },
            std::move(dependencies)));
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

void apply_materializer_mutations(
    const ExecutionPlanImage& image, const AdapterOptions& options,
    CompiledProvider& provider) {
    if (options.swap_first_two_preparation_materializers ||
        options.disguise_second_preparation_as_first) {
        if (image.lifecycle().preparation_handles.size() < 2U) {
            throw std::runtime_error(
                "preparation mutation requires two materializers");
        }
        auto first_handle = image.lifecycle().preparation_handles[0U];
        auto second_handle = image.lifecycle().preparation_handles[1U];
        if (options.disguise_second_preparation_as_first) {
            bool found_pair = false;
            for (std::size_t first_index = 0U;
                 first_index < image.lifecycle().preparation_handles.size() &&
                 !found_pair;
                 ++first_index) {
                for (std::size_t second_index = first_index + 1U;
                     second_index <
                     image.lifecycle().preparation_handles.size();
                     ++second_index) {
                    const auto candidate_first =
                        image.lifecycle().preparation_handles[first_index];
                    const auto candidate_second =
                        image.lifecycle().preparation_handles[second_index];
                    const auto first_layout =
                        provider.preparations.at(candidate_first)
                            ->operations().layout();
                    const auto second_layout =
                        provider.preparations.at(candidate_second)
                            ->operations().layout();
                    if (first_layout.size_bytes == second_layout.size_bytes &&
                        first_layout.alignment_bytes ==
                            second_layout.alignment_bytes &&
                        first_layout.type_identity !=
                            second_layout.type_identity) {
                        first_handle = candidate_first;
                        second_handle = candidate_second;
                        found_pair = true;
                        break;
                    }
                }
            }
            if (!found_pair) {
                throw std::runtime_error(
                    "prepared-model disguise lacks equal physical layout");
            }
        }
        auto first = provider.preparations.at(first_handle);
        auto second = provider.preparations.at(second_handle);
        if (options.swap_first_two_preparation_materializers) {
            provider.preparations[first_handle] = second;
            provider.preparations[second_handle] = first;
        }
        if (options.disguise_second_preparation_as_first) {
            const auto* preparation = find_handle(
                image.preparations(), first_handle);
            provider.preparations[first_handle] =
                std::make_shared<IdentityOverrideMaterializer>(
                    std::move(second),
                    SessionMaterializerIdentity{
                        first_handle, SessionObjectRole::PreparedModel,
                        preparation->prepare_entry_handle, 0U});
        }
    }
    if (options.wrong_first_runtime_factory_identity) {
        const auto handle =
            image.lifecycle().runtime_component_handles.front();
        const auto* component = find_handle(image.runtime_components(),
                                            handle);
        auto source = provider.runtime_components.at(handle);
        provider.runtime_components[handle] =
            std::make_shared<IdentityOverrideMaterializer>(
                std::move(source),
                SessionMaterializerIdentity{
                    handle, SessionObjectRole::RuntimeCell,
                    component->runtime_cell_factory_entry_handle + 1U, 0U});
    }
}

[[nodiscard]] SessionResult begin_boundary_invocation(
    const SessionInvocationContext& context, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    std::size_t& ordinal) {
    ordinal = boundary_ordinal(probe);
    probe->call_order.push_back(context.callsite_handle());
    if (injects_failure(options, FailurePhase::Boundary, ordinal)) {
        record(trace, TraceAction::InjectedFailure,
               TraceObjectKind::RuntimeCell, context.component_handle());
        return {gnc::kernel::SessionError::InvocationFailed,
                context.callsite_handle(),
                "injected opening-boundary failure"};
    }
    return {};
}

template <typename Value>
void install_invocation(
    CompiledProvider& provider, const PlanImageRuntimeComponent& component,
    const PlanImageCallsite& callsite,
    std::function<SessionResult(const SessionInvocationContext&)> invoke) {
    static_cast<void>(sizeof(Value));
    provider.invocations.emplace(
        callsite.handle,
        std::make_shared<FixedInvocation>(
            SessionInvocationIdentity{
                callsite.handle, component.handle, callsite.entry_handle},
            std::move(invoke)));
}

void build_rigid_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& projection = component_callsite<yyz::RigidPublishProjectionCall>(
        image, component);
    const auto projection_factory =
        component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::ControlledRigidRuntimeCell>(
        provider, component, projection,
        [options, trace, probe, projection_factory](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::ControlledRigidRuntimeCell>(context,
                                                  projection_factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "rigid projection Runtime Cell type mismatch"};
            }
            const auto* state = checked_committed<yyz::RigidState>(
                context, cell->bindings.state_block_handle, result);
            if (state == nullptr) return result;
            const auto& closure_definition =
                cell->bindings.bound_invocations.frozen_form.closure
                    .prepared_model->definition();
            const auto sample = sample_context(
                context, cell->definition.rigid.inertial_frame,
                closure_definition.clock_domain,
                closure_definition.configuration_revision);
            const auto output = cell->bindings.publish_projection(sample,
                                                                   *state);
            probe->observation_position = vector3(output.state.position.value);
            probe->observation_velocity = vector3(output.state.velocity.value);
            probe->observation_attitude_wxyz =
                gnc::foundation::quaternion_to_wxyz(
                    output.state.attitude.value);
            probe->observation_angular_rate =
                vector3(output.state.angular_rate.value);
            probe->contexts.push_back(context_probe(
                output.context, context.interval_start_seconds(),
                context.interval_end_seconds()));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            return write_value(
                context, cell->bindings.observation_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.observation_output.writer_token.value),
                output);
        });

    const auto& boundary = component_callsite<
        yyz::ControlledRigidBoundaryRuntimeCall>(image, component);
    const auto& closure_invocation =
        component_invocation<yyz::ForceMomentClosureCall>(image, component);
    const auto boundary_factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::ControlledRigidRuntimeCell>(
        provider, component, boundary,
        [options, trace, probe, boundary_factory,
         held_writer_token = closure_invocation.result_writer_token_handle](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::ControlledRigidRuntimeCell>(context, boundary_factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "controlled boundary Runtime Cell type mismatch"};
            }
            const auto* state = checked_committed<yyz::RigidState>(
                context, cell->bindings.state_block_handle, result);
            if (state == nullptr) return result;
            const auto* mass = checked_input<yyz::MassPropertiesInput>(
                context, cell->bindings.mass_properties_input_slot_handle,
                result);
            if (mass == nullptr) return result;
            const auto* propulsion =
                checked_input<yyz::SuppliedPropulsionBodyWrench>(
                    context,
                    cell->bindings
                        .propulsion_body_wrench_input_slot_handle,
                    result);
            if (propulsion == nullptr) return result;
            const auto* actuator =
                checked_input<yyz::IdealBodyMomentActuatorOutput>(
                    context,
                    cell->bindings.actuator_output_input_slot_handle,
                    result);
            if (actuator == nullptr) return result;
            const auto& closure =
                cell->bindings.bound_invocations.frozen_form.closure;
            const auto& closure_definition =
                closure.prepared_model->definition();
            yyz::RigidStepContext step_context;
            step_context.inertial_frame =
                cell->definition.rigid.inertial_frame;
            step_context.body_frame = closure_definition.body_frame;
            step_context.clock_domain = closure_definition.clock_domain;
            step_context.interval_start =
                {context.tick(), context.interval_start_seconds()};
            step_context.interval_end =
                {context.tick() + 1, context.interval_end_seconds()};
            step_context.configuration_revision =
                closure_definition.configuration_revision;
            step_context.quality = context.quality();
            const auto prepared = cell->bindings.boundary_evaluation(
                cell->definition, cell->bindings.bound_invocations,
                {step_context, *state, *mass, *propulsion, *actuator});
            if (!prepared.succeeded() || !prepared.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "controlled boundary preparation failed"};
            }
            const auto& output = prepared.value().output;
            const auto& telemetry = prepared.value().telemetry;
            probe->gravity = vector3(output.environment_response.gravity.value);
            probe->wind = vector3(
                output.environment_response.velocity_airmass.value);
            probe->density =
                output.environment_response.density_kilograms_per_cubic_meter;
            probe->speed_of_sound = output.environment_response
                                        .speed_of_sound_meters_per_second;
            probe->aerodynamic_coefficients =
                output.aerodynamic_coefficients
                    .coefficients_ca_cy_cn_cl_cm_cn;
            probe->closure_contribution_count =
                output.closure_request.contributions.size();
            probe->airspeed = telemetry.frozen_form.air_data
                                  .airspeed_meters_per_second;
            probe->alpha = telemetry.frozen_form.air_data.alpha_radians;
            probe->beta = telemetry.frozen_form.air_data.beta_radians;
            probe->dynamic_pressure = telemetry.frozen_form.air_data
                                          .dynamic_pressure_pascals;
            probe->mach = telemetry.frozen_form.air_data.mach;
            probe->contexts.push_back(context_probe(
                output.environment_response.context,
                context.interval_start_seconds(),
                context.interval_end_seconds()));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            result = write_value(
                context,
                cell->bindings.boundary_preparation_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.boundary_preparation_output.writer_token
                        .value),
                output);
            if (!result) return result;
            probe->controlled_preparation_written = true;
            const auto held = closure.callable(*closure.prepared_model,
                                               output.closure_request);
            if (!held.succeeded() || !held.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "held closure invocation failed"};
            }
            const auto& form = held.value().output;
            result = write_value(
                context, cell->bindings.held_form_result_slot_handle,
                held_writer_token, form);
            if (!result) return result;
            probe->held_force = vector3(form.force_total.value);
            probe->held_moment =
                vector3(form.moment_total_about_center_of_mass.value);
            probe->held_form_written = true;
            if (options.fail_after_output_boundary_ordinal == ordinal) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "injected failure after held output write"};
            }
            return {};
        });
}

void build_mass_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& projection = component_callsite<yyz::MassPublishProjectionCall>(
        image, component);
    const auto factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::ScalarBurnMassRuntimeCell>(
        provider, component, projection,
        [options, trace, probe, factory](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::ScalarBurnMassRuntimeCell>(context, factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "mass projection Runtime Cell type mismatch"};
            }
            const auto* state = checked_committed<yyz::MassState>(
                context, cell->bindings.state_block_handle, result);
            if (state == nullptr) return result;
            const auto interval = interval_context(
                context, state->context.frame, state->context.clock_domain,
                state->context.configuration_revision);
            const auto output = cell->bindings.publish_projection(interval,
                                                                   *state);
            probe->mass_kilograms = output.mass_kilograms;
            probe->center_of_mass =
                vector3(output.body_origin_to_center_of_mass.value);
            probe->inertia =
                matrix3(output.inertia_about_center_of_mass.value);
            probe->contexts.push_back(context_probe(
                output.context.sample,
                output.context.validity.effective_from.seconds,
                output.context.validity.effective_until.seconds));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            return write_value(
                context, cell->bindings.mass_properties_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.mass_properties_output.writer_token.value),
                output);
        });
}

void build_guidance_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    const std::shared_ptr<CapturedFrameView>& captured,
    CompiledProvider& provider) {
    const auto& callsite =
        component_callsite<yyz::AltitudePitchGuidanceCall>(image, component);
    const auto factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::AltitudePitchGuidanceRuntimeCell>(
        provider, component, callsite,
        [options, trace, probe, captured, factory](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::AltitudePitchGuidanceRuntimeCell>(context, factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "guidance Runtime Cell type mismatch"};
            }
            captured->view =
                std::make_shared<gnc::kernel::SessionInputView>(
                    context.inputs());
            captured->slot_handle =
                cell->bindings.observation_input_slot_handle;
            const auto* observation =
                checked_input<yyz::CommittedRigidObservation>(
                    context, cell->bindings.observation_input_slot_handle,
                    result);
            if (observation == nullptr) return result;
            const auto output = cell->bindings.boundary_evaluation(
                cell->definition, *observation);
            if (!output.succeeded() || !output.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "guidance evaluation failed"};
            }
            const auto& value = output.value();
            probe->guidance_altitude_error = value.altitude_error_meters;
            probe->guidance_raw_command = value.raw_pitch_command_radians;
            probe->guidance_command = value.pitch_command_radians;
            probe->guidance_limit =
                cell->definition.pitch_command_limit_radians;
            probe->guidance_saturated = value.saturated;
            probe->contexts.push_back(context_probe(
                value.source_observation.context,
                context.interval_start_seconds(),
                context.interval_end_seconds()));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            return write_value(
                context, cell->bindings.guidance_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.guidance_output.writer_token.value),
                value);
        });
}

void build_controller_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& callsite =
        component_callsite<yyz::PitchMomentControllerCall>(image, component);
    const auto factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::PitchMomentControllerRuntimeCell>(
        provider, component, callsite,
        [options, trace, probe, factory](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::PitchMomentControllerRuntimeCell>(context, factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "controller Runtime Cell type mismatch"};
            }
            const auto* guidance =
                checked_input<yyz::AltitudePitchGuidanceOutput>(
                    context, cell->bindings.guidance_input_slot_handle,
                    result);
            if (guidance == nullptr) return result;
            const auto output = cell->bindings.boundary_evaluation(
                cell->definition, *guidance);
            if (!output.succeeded() || !output.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "controller evaluation failed"};
            }
            const auto& value = output.value();
            probe->controller_pitch_error = value.pitch_error_radians;
            probe->controller_raw_moment =
                value.raw_moment_command_newton_meters;
            probe->controller_moment = value.moment_command_newton_meters;
            probe->controller_limit =
                cell->definition.moment_command_limit_newton_meters;
            probe->controller_saturated = value.saturated;
            probe->contexts.push_back(context_probe(
                value.context, context.interval_start_seconds(),
                context.interval_end_seconds()));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            return write_value(
                context, cell->bindings.controller_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.controller_output.writer_token.value),
                value);
        });
}

void build_actuator_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& callsite =
        component_callsite<yyz::IdealBodyMomentActuatorCall>(image, component);
    const auto factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::IdealBodyMomentActuatorRuntimeCell>(
        provider, component, callsite,
        [options, trace, probe, factory](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::IdealBodyMomentActuatorRuntimeCell>(context, factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "actuator Runtime Cell type mismatch"};
            }
            const auto* controller =
                checked_input<yyz::PitchMomentControllerOutput>(
                    context, cell->bindings.controller_input_slot_handle,
                    result);
            if (controller == nullptr) return result;
            const auto interval = interval_context(
                context, cell->definition.body_frame,
                cell->definition.clock_domain,
                cell->definition.configuration_revision);
            const auto output = cell->bindings.boundary_evaluation(
                cell->definition, interval, *controller);
            if (!output.succeeded() || !output.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "actuator evaluation failed"};
            }
            const auto& value = output.value();
            probe->actuator_moment =
                vector3(value.moment_about_center_of_mass.value);
            probe->contexts.push_back(context_probe(
                value.context.sample,
                value.context.validity.effective_from.seconds,
                value.context.validity.effective_until.seconds));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            return write_value(
                context, cell->bindings.actuator_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.actuator_output.writer_token.value),
                value);
        });
}

void build_propulsion_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    std::int64_t configuration_revision, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& callsite =
        component_callsite<yyz::FixedSuppliedPropulsionCall>(image, component);
    const auto factory = component.runtime_cell_factory_entry_handle;
    install_invocation<yyz::FixedSuppliedPropulsionRuntimeCell>(
        provider, component, callsite,
        [options, trace, probe, factory, configuration_revision](
            const SessionInvocationContext& context) -> SessionResult {
            std::size_t ordinal = 0U;
            auto result = begin_boundary_invocation(
                context, options, trace, probe, ordinal);
            if (!result) return result;
            const auto* cell = checked_runtime<
                yyz::FixedSuppliedPropulsionRuntimeCell>(context, factory);
            if (cell == nullptr) {
                return {gnc::kernel::SessionError::ObjectTypeMismatch,
                        context.component_handle(),
                        "propulsion Runtime Cell type mismatch"};
            }
            const auto interval = interval_context(
                context, cell->definition.propulsion.body_frame,
                cell->definition.propulsion.clock_domain,
                configuration_revision);
            const auto output = cell->bindings.boundary_evaluation(
                cell->definition, interval);
            if (!output.succeeded() || !output.has_value()) {
                return {gnc::kernel::SessionError::InvocationFailed,
                        context.callsite_handle(),
                        "propulsion evaluation failed"};
            }
            const auto& value = output.value();
            probe->propulsion_force =
                vector3(value.supplied_body_wrench.force.value);
            probe->propulsion_application_from_com = vector3(
                value.supplied_body_wrench
                    .center_of_mass_to_application.value);
            probe->propulsion_intrinsic_moment = vector3(
                value.supplied_body_wrench
                    .intrinsic_moment_at_application.value);
            probe->mass_flow_rate =
                value.mass_flow.fuel_consumption_rate_kilograms_per_second;
            probe->contexts.push_back(context_probe(
                value.supplied_body_wrench.context.sample,
                value.supplied_body_wrench.context.validity.effective_from
                    .seconds,
                value.supplied_body_wrench.context.validity.effective_until
                    .seconds));
            if (options.omit_output_boundary_ordinal == ordinal) return {};
            result = write_value(
                context,
                cell->bindings.propulsion_wrench_output.slot_handle,
                selected_writer_token(
                    options, ordinal,
                    cell->bindings.propulsion_wrench_output.writer_token
                        .value),
                value.supplied_body_wrench);
            if (!result) return result;
            return write_value(
                context, cell->bindings.mass_flow_output.slot_handle,
                cell->bindings.mass_flow_output.writer_token.value,
                value.mass_flow);
        });
}

void build_evaluator_invocations(
    const ExecutionPlanImage& image,
    const PlanImageRuntimeComponent& component,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    CompiledProvider& provider) {
    const auto& callsite = component_callsite<
        yyz::CommittedMissionHistoryEvaluationCall>(image, component);
    install_invocation<yyz::CommittedMissionResultRuntimeCell>(
        provider, component, callsite,
        [probe](const SessionInvocationContext& context) -> SessionResult {
            probe->terminal_evaluator_called = true;
            return {gnc::kernel::SessionError::InvocationFailed,
                    context.callsite_handle(),
                    "terminal evaluator lacks required committed history"};
        });
}

[[nodiscard]] std::int64_t opening_configuration_revision(
    const ExecutionPlanImage& image) {
    for (const auto& binding : image.initial_bindings()) {
        if (entry_is<yyz::MassInitialStateCall>(
                image, binding.builder_entry_handle)) {
            return initial_integer(binding,
                                   "context.configuration_revision");
        }
    }
    throw std::runtime_error("opening configuration revision is missing");
}

void build_invocations(
    const ExecutionPlanImage& image, const AdapterOptions& options,
    const std::shared_ptr<MaterializationTrace>& trace,
    const std::shared_ptr<OpeningBoundaryProbe>& probe,
    const std::shared_ptr<CapturedFrameView>& captured,
    CompiledProvider& provider) {
    std::vector<const PlanImageRuntimeComponent*> components;
    components.reserve(image.runtime_components().size());
    for (const auto& component : image.runtime_components()) {
        components.push_back(&component);
    }
    if (options.reverse_invocation_registration) {
        std::reverse(components.begin(), components.end());
    }
    const auto configuration_revision =
        opening_configuration_revision(image);
    for (const auto* component : components) {
        const auto factory = component->runtime_cell_factory_entry_handle;
        if (entry_is<yyz::ControlledRigidRuntimeCellFactoryCall>(
                image, factory)) {
            build_rigid_invocations(image, *component, options, trace, probe,
                                    provider);
        } else if (entry_is<yyz::ScalarBurnMassRuntimeCellFactoryCall>(
                       image, factory)) {
            build_mass_invocations(image, *component, options, trace, probe,
                                   provider);
        } else if (entry_is<
                       yyz::AltitudePitchGuidanceRuntimeCellFactoryCall>(
                       image, factory)) {
            build_guidance_invocations(image, *component, options, trace,
                                       probe, captured, provider);
        } else if (entry_is<
                       yyz::PitchMomentControllerRuntimeCellFactoryCall>(
                       image, factory)) {
            build_controller_invocations(image, *component, options, trace,
                                         probe, provider);
        } else if (entry_is<
                       yyz::IdealBodyMomentActuatorRuntimeCellFactoryCall>(
                       image, factory)) {
            build_actuator_invocations(image, *component, options, trace,
                                       probe, provider);
        } else if (entry_is<
                       yyz::FixedSuppliedPropulsionRuntimeCellFactoryCall>(
                       image, factory)) {
            build_propulsion_invocations(
                image, *component, configuration_revision, options, trace,
                probe, provider);
        } else if (entry_is<
                       yyz::CommittedMissionResultRuntimeCellFactoryCall>(
                       image, factory)) {
            build_evaluator_invocations(image, *component, probe, provider);
        }
    }
}

} // namespace

RefYyzSessionAdapter make_session_adapter(
    const ExecutionPlanImage& image, AdapterOptions options) {
    RefYyzSessionAdapter result;
    result.trace = std::make_shared<MaterializationTrace>();
    result.opening_boundary = std::make_shared<OpeningBoundaryProbe>();
    result.captured_input = std::make_shared<CapturedFrameView>();
    try {
        auto provider = std::make_shared<CompiledProvider>();
        build_preparations(image, options, result.trace, *provider);
        build_runtime_components(image, options, result.trace, *provider);
        build_slots(image, options, result.trace, *provider, result);
        build_initial_states(image, options, result.trace, *provider, result);
        apply_materializer_mutations(image, options, *provider);
        build_invocations(image, options, result.trace,
                          result.opening_boundary, result.captured_input,
                          *provider);
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
    const kernel::Session& session, const RefYyzSessionAdapter& adapter) {
    NonTrivialObjectProbe result;
    result.state_is_non_trivial =
        !std::is_trivially_copyable<yyz::MassState>::value &&
        !std::is_trivially_destructible<yyz::MassState>::value;
    result.output_is_non_trivial =
        !std::is_trivially_copyable<yyz::CommittedMissionResultOutput>::value &&
        !std::is_trivially_destructible<
            yyz::CommittedMissionResultOutput>::value;
    const auto state_constructs = adapter.trace->constructed_handles(
        TraceObjectKind::State);
    result.state_store_cloned_twice =
        std::count(state_constructs.begin(), state_constructs.end(),
                   adapter.mass_state_block_handle) == 0 &&
        state_constructs.size() >= session.committed_state_count() * 3U;
    result.frame_values_deferred =
        std::none_of(adapter.trace->events.begin(),
                     adapter.trace->events.end(), [](const auto& event) {
                         return event.kind == TraceObjectKind::Slot &&
                                event.action == TraceAction::Construct;
                     });
    return result;
}

kernel::SessionResult read_captured_stale_input(
    const RefYyzSessionAdapter& adapter) noexcept {
    if (adapter.captured_input == nullptr ||
        adapter.captured_input->view == nullptr) {
        return {kernel::SessionError::InvalidLifecycleTransition, 0U,
                "no captured input view"};
    }
    kernel::SessionObjectIdentityView ignored;
    return adapter.captured_input->view->read(
        adapter.captured_input->slot_handle, ignored);
}

kernel::SessionResult read_mass_candidate_for_qualification(
    const kernel::Session& session, const RefYyzSessionAdapter& adapter,
    double& mass_kilograms) noexcept {
    mass_kilograms = 0.0;
    kernel::SessionObjectIdentityView view;
    auto result = kernel::qualification::SessionAccess::read_candidate(
        session, adapter.mass_state_block_handle, view);
    if (!result) return result;
    if (!view || view.role != kernel::SessionObjectRole::CandidateState ||
        view.image_object_handle != adapter.mass_state_block_handle ||
        view.linked_entry_handle == 0U || view.codec_entry_handle == 0U ||
        view.size_bytes != sizeof(yyz::MassState) ||
        view.alignment_bytes != alignof(yyz::MassState) ||
        view.type_identity != &typeid(yyz::MassState)) {
        return {kernel::SessionError::ObjectTypeMismatch,
                adapter.mass_state_block_handle,
                "mass candidate qualification type mismatch"};
    }
    mass_kilograms =
        static_cast<const yyz::MassState*>(view.address)->mass_kilograms;
    return {};
}

kernel::SessionResult replace_mass_candidate_for_qualification(
    kernel::Session& session, const RefYyzSessionAdapter& adapter,
    double mass_kilograms) noexcept {
    try {
        kernel::SessionObjectIdentityView view;
        auto result = kernel::qualification::SessionAccess::read_candidate(
            session, adapter.mass_state_block_handle, view);
        if (!result) return result;
        if (!view || view.role != kernel::SessionObjectRole::CandidateState ||
            view.image_object_handle != adapter.mass_state_block_handle ||
            view.linked_entry_handle == 0U ||
            view.codec_entry_handle == 0U ||
            view.size_bytes != sizeof(yyz::MassState) ||
            view.alignment_bytes != alignof(yyz::MassState) ||
            view.type_identity != &typeid(yyz::MassState)) {
            return {kernel::SessionError::ObjectTypeMismatch,
                    adapter.mass_state_block_handle,
                    "mass candidate qualification type mismatch"};
        }
        auto replacement = *static_cast<const yyz::MassState*>(view.address);
        replacement.mass_kilograms = mass_kilograms;
        return kernel::qualification::SessionAccess::replace_candidate(
            session, adapter.mass_state_block_handle,
            {&replacement, sizeof(replacement), alignof(decltype(replacement)),
             &typeid(decltype(replacement))});
    } catch (...) {
        return {kernel::SessionError::InternalFailure,
                adapter.mass_state_block_handle,
                "mass candidate qualification copy failed"};
    }
}

} // namespace gnc::tests::ref_yyz
