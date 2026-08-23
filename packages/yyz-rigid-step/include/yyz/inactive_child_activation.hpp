#pragma once

#include <gnc/model_sdk/in_process_codec.hpp>
#include <gnc/model_sdk/runtime_cell_factory.hpp>
#include <gnc/model_sdk/static_implementation.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace gnc::packages::yyz {

inline constexpr std::string_view kInactiveChildActivationPackageId =
    "gnc.package.yyz.inactive-child-activation-qualification@1";
inline constexpr std::string_view kInactiveChildActivationPackageVersion =
    "0.1.0";
inline constexpr std::string_view kInactiveChildActivationBuildFingerprint =
    "build.yyz.inactive-child-activation-qualification.release";
inline constexpr std::string_view kInactiveChildActivationModelVersion =
    "0.1.0";

inline constexpr std::string_view kActivationRelationshipModelId =
    "gnc.package.yyz.activation-relationship.qualification@1";
inline constexpr std::string_view kActivationParentModelId =
    "gnc.package.yyz.activation-parent.qualification@1";
inline constexpr std::string_view kActivationChildModelId =
    "gnc.package.yyz.activation-child.qualification@1";
inline constexpr std::string_view kActivationChildConsumerModelId =
    "gnc.package.yyz.activation-child-consumer.qualification@1";

inline constexpr std::string_view kActivationRelationshipConfigSchemaId =
    "gnc.config.yyz.activation-relationship@1";
inline constexpr std::string_view kActivationParentConfigSchemaId =
    "gnc.config.yyz.activation-parent@1";
inline constexpr std::string_view kActivationChildConfigSchemaId =
    "gnc.config.yyz.activation-child@1";
inline constexpr std::string_view kActivationChildConsumerConfigSchemaId =
    "gnc.config.yyz.activation-child-consumer@1";

inline constexpr std::string_view kActivationRelationshipInitialSchemaId =
    "gnc.initial.yyz.activation-relationship@1";
inline constexpr std::string_view kActivationParentInitialSchemaId =
    "gnc.initial.yyz.activation-parent@1";
inline constexpr std::string_view kActivationChildInitialSchemaId =
    "gnc.initial.yyz.activation-child@1";

inline constexpr std::string_view kActivationRelationshipStateSchemaId =
    "gnc.state-schema.yyz.activation-relationship@1";
inline constexpr std::string_view kActivationParentStateSchemaId =
    "gnc.state-schema.yyz.activation-parent@1";
inline constexpr std::string_view kActivationChildStateSchemaId =
    "gnc.state-schema.yyz.activation-child@1";
inline constexpr std::string_view kActivationRelationshipStateLayoutId =
    "gnc.layout.yyz.activation-relationship-state@1";
inline constexpr std::string_view kActivationParentStateLayoutId =
    "gnc.layout.yyz.activation-parent-state@1";
inline constexpr std::string_view kActivationChildStateLayoutId =
    "gnc.layout.yyz.activation-child-state@1";

inline constexpr std::string_view kActivationRelationshipObservationContractId =
    "gnc.contract.yyz.activation-relationship-observation@1";
inline constexpr std::string_view kActivationParentObservationContractId =
    "gnc.contract.yyz.activation-parent-observation@1";
inline constexpr std::string_view kActivationChildObservationContractId =
    "gnc.contract.yyz.activation-child-observation@1";
inline constexpr std::string_view kActivationChildInfluenceContractId =
    "gnc.contract.yyz.activation-child-influence@1";
inline constexpr std::string_view kActivateChildCommandSchemaId =
    "gnc.command.yyz.activate-child@1";
inline constexpr std::string_view kActivationRequestedEventSchemaId =
    "gnc.event.yyz.activation-requested@1";
inline constexpr std::string_view kActivationParentMappedEventSchemaId =
    "gnc.event.yyz.activation-parent-mapped@1";
inline constexpr std::string_view kActivationCompletedEventSchemaId =
    "gnc.event.yyz.activation-completed@1";
inline constexpr std::string_view kActivationAuditEventSchemaId =
    "gnc.event.yyz.activation-audit@1";
inline constexpr std::string_view kActivationAuditConsumedSchemaId =
    "gnc.event.yyz.activation-audit-consumed@1";

inline constexpr std::string_view kActivationRelationshipObservationLayoutId =
    "gnc.layout.yyz.activation-relationship-observation@1";
inline constexpr std::string_view kActivationParentObservationLayoutId =
    "gnc.layout.yyz.activation-parent-observation@1";
inline constexpr std::string_view kActivationChildObservationLayoutId =
    "gnc.layout.yyz.activation-child-observation@1";
inline constexpr std::string_view kActivationChildInfluenceLayoutId =
    "gnc.layout.yyz.activation-child-influence@1";

struct ActivationRelationshipDefinition {
    double exact_transfer_mass = 0.0;
};

struct ActivationParentDefinition {
    double child_position_meters = 0.0;
    double child_velocity_meters_per_second = 0.0;
    double child_inertia_per_mass = 0.0;
};

struct ActivationChildDefinition {
    double expected_inertia_per_mass = 0.0;
};

struct ActivationChildConsumerDefinition {
    double influence_gain = 0.0;
};

struct ActivationRelationshipState {
    bool child_active = false;
    double transferred_mass = 0.0;
    std::uint64_t relationship_revision = 0U;
};

struct ActivationParentState {
    double available_mass = 0.0;
    double transferred_mass = 0.0;
    std::uint64_t revision = 0U;
};

struct ActivationChildState {
    bool initialized = false;
    double position_meters = 0.0;
    double velocity_meters_per_second = 0.0;
    double mass = 0.0;
    double inertia = 0.0;
    std::uint64_t revision = 0U;
};

struct ActivationRelationshipObservation {
    bool child_active = false;
    double transferred_mass = 0.0;
    std::uint64_t relationship_revision = 0U;
};

struct ActivationParentObservation {
    double available_mass = 0.0;
    double transferred_mass = 0.0;
    std::uint64_t revision = 0U;
};

struct ActivationChildObservation {
    bool initialized = false;
    double position_meters = 0.0;
    double velocity_meters_per_second = 0.0;
    double mass = 0.0;
    double inertia = 0.0;
    std::uint64_t revision = 0U;
};

struct ActivationChildInfluence {
    double weighted_momentum = 0.0;
    std::uint64_t child_revision = 0U;
};

struct ActivateChildCommand {
    double transfer_mass = 0.0;
};

struct ActivationRequested {
    double transfer_mass = 0.0;
    std::uint64_t relationship_revision = 0U;
};

struct ActivationParentMapped {
    double child_position_meters = 0.0;
    double child_velocity_meters_per_second = 0.0;
    double child_mass = 0.0;
    double child_inertia = 0.0;
    std::uint64_t parent_revision = 0U;
    std::uint64_t relationship_revision = 0U;
};

struct ActivationCompleted {
    std::uint64_t child_revision = 0U;
    std::uint64_t parent_revision = 0U;
    std::uint64_t relationship_revision = 0U;
};

struct ActivationAuditEvent {
    std::uint64_t relationship_revision = 0U;
};

struct ActivationAuditConsumed {
    std::uint64_t relationship_revision = 0U;
};

struct ActivationRelationshipInitialInput {
    std::int64_t child_active = 0;
    double transferred_mass = 0.0;
    std::int64_t relationship_revision = 0;
};

struct ActivationParentInitialInput {
    double available_mass = 0.0;
    double transferred_mass = 0.0;
    std::int64_t revision = 0;
};

struct ActivationChildInitialInput {
    std::int64_t initialized = 0;
    double position_meters = 0.0;
    double velocity_meters_per_second = 0.0;
    double mass = 0.0;
    double inertia = 0.0;
    std::int64_t revision = 0;
};

struct ActivationRelationshipReduction {
    ActivationRelationshipState candidate;
    ActivationRequested event;
};

struct ActivationParentMapping {
    ActivationParentState candidate;
    ActivationParentMapped event;
};

struct ActivationChildMapping {
    ActivationChildState candidate;
    ActivationCompleted event;
};

template <typename Definition>
struct ActivationRuntimeCell {
    const Definition definition;
    const gnc::model_sdk::RuntimeCellFactoryContext context;
};

struct ActivationRuntimeCellBindings {};

using ActivationRelationshipRuntimeCell =
    ActivationRuntimeCell<ActivationRelationshipDefinition>;
using ActivationParentRuntimeCell =
    ActivationRuntimeCell<ActivationParentDefinition>;
using ActivationChildRuntimeCell =
    ActivationRuntimeCell<ActivationChildDefinition>;
using ActivationChildConsumerRuntimeCell =
    ActivationRuntimeCell<ActivationChildConsumerDefinition>;

using ActivationRelationshipDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ActivationRelationshipDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using ActivationParentDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ActivationParentDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using ActivationChildDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ActivationChildDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using ActivationChildConsumerDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<ActivationChildConsumerDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);

using ActivationRelationshipRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ActivationRelationshipRuntimeCell, ActivationRelationshipDefinition,
        ActivationRuntimeCellBindings>;
using ActivationParentRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ActivationParentRuntimeCell, ActivationParentDefinition,
        ActivationRuntimeCellBindings>;
using ActivationChildRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ActivationChildRuntimeCell, ActivationChildDefinition,
        ActivationRuntimeCellBindings>;
using ActivationChildConsumerRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        ActivationChildConsumerRuntimeCell,
        ActivationChildConsumerDefinition, ActivationRuntimeCellBindings>;

using ActivationRelationshipProjectionCall =
    ActivationRelationshipObservation (*)(
        const ActivationRelationshipState&, std::int64_t);
using ActivationParentProjectionCall = ActivationParentObservation (*)(
    const ActivationParentState&, std::int64_t);
using ActivationChildProjectionCall = ActivationChildObservation (*)(
    const ActivationChildState&, std::int64_t);
using ActivationRelationshipReductionCall =
    gnc::foundation::NumericalOutcome<ActivationRelationshipReduction> (*)(
        const ActivationRelationshipDefinition&,
        const ActivationRelationshipState&, const ActivateChildCommand&);
using ActivationRelationshipEventConsumptionCall =
    gnc::foundation::NumericalOutcome<ActivationAuditConsumed> (*)(
        const ActivationRelationshipDefinition&,
        const ActivationRelationshipState&, const ActivationAuditEvent&);
using ActivationParentMappingCall =
    gnc::foundation::NumericalOutcome<ActivationParentMapping> (*)(
        const ActivationParentDefinition&, const ActivationParentState&,
        const ActivationRequested&);
using ActivationChildMappingCall =
    gnc::foundation::NumericalOutcome<ActivationChildMapping> (*)(
        const ActivationChildDefinition&, const ActivationChildState&,
        const ActivationParentMapped&);
using ActivationChildConsumerCall =
    gnc::foundation::NumericalOutcome<ActivationChildInfluence> (*)(
        const ActivationChildConsumerDefinition&,
        const ActivationChildObservation&);

using ActivationRelationshipInitialStateCall =
    gnc::foundation::NumericalOutcome<ActivationRelationshipState> (*)(
        const ActivationRelationshipDefinition&,
        const ActivationRelationshipInitialInput&);
using ActivationParentInitialStateCall =
    gnc::foundation::NumericalOutcome<ActivationParentState> (*)(
        const ActivationParentDefinition&,
        const ActivationParentInitialInput&);
using ActivationChildInitialStateCall =
    gnc::foundation::NumericalOutcome<ActivationChildState> (*)(
        const ActivationChildDefinition&,
        const ActivationChildInitialInput&);

template <typename State>
using ActivationStateCloneCall = State (*)(const State&);
template <typename State>
using ActivationStateValidateCall = bool (*)(const State&) noexcept;
template <typename State>
using ActivationStateSwapCall = void (*)(State&, State&) noexcept;
template <typename State>
using ActivationStateCodec = gnc::model_sdk::InProcessStateCodec<
    ActivationStateCloneCall<State>, ActivationStateValidateCall<State>,
    ActivationStateValidateCall<State>, ActivationStateValidateCall<State>,
    ActivationStateSwapCall<State>, ActivationStateCloneCall<State>>;

using ActivationRelationshipStateCodec =
    ActivationStateCodec<ActivationRelationshipState>;
using ActivationParentStateCodec = ActivationStateCodec<ActivationParentState>;
using ActivationChildStateCodec = ActivationStateCodec<ActivationChildState>;
using ActivationRelationshipStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<ActivationRelationshipStateCodec>;
using ActivationParentStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<ActivationParentStateCodec>;
using ActivationChildStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<ActivationChildStateCodec>;

[[nodiscard]] gnc::model_sdk::StaticPackageDescriptor
describe_inactive_child_activation_package();
[[nodiscard]] gnc::model_sdk::StaticPackageImplementation
describe_inactive_child_activation_implementation(
    std::string build_fingerprint =
        std::string(kInactiveChildActivationBuildFingerprint));

[[nodiscard]] gnc::foundation::NumericalOutcome<
    ActivationRelationshipDefinition>
build_activation_relationship_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationParentDefinition>
build_activation_parent_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationChildDefinition>
build_activation_child_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<
    ActivationChildConsumerDefinition>
build_activation_child_consumer_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

[[nodiscard]] gnc::foundation::NumericalOutcome<
    ActivationRelationshipRuntimeCell>
create_activation_relationship_runtime_cell(
    const ActivationRelationshipDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationParentRuntimeCell>
create_activation_parent_runtime_cell(
    const ActivationParentDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationChildRuntimeCell>
create_activation_child_runtime_cell(
    const ActivationChildDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<
    ActivationChildConsumerRuntimeCell>
create_activation_child_consumer_runtime_cell(
    const ActivationChildConsumerDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const ActivationRuntimeCellBindings& bindings);

[[nodiscard]] ActivationRelationshipObservation
project_activation_relationship(const ActivationRelationshipState& state,
                                std::int64_t tick);
[[nodiscard]] ActivationParentObservation project_activation_parent(
    const ActivationParentState& state, std::int64_t tick);
[[nodiscard]] ActivationChildObservation project_activation_child(
    const ActivationChildState& state, std::int64_t tick);
[[nodiscard]] gnc::foundation::NumericalOutcome<
    ActivationRelationshipReduction>
reduce_activate_child(const ActivationRelationshipDefinition& definition,
                      const ActivationRelationshipState& prior,
                      const ActivateChildCommand& command);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationAuditConsumed>
consume_activation_audit(
    const ActivationRelationshipDefinition& definition,
    const ActivationRelationshipState& state,
    const ActivationAuditEvent& event);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationParentMapping>
map_activation_parent(const ActivationParentDefinition& definition,
                      const ActivationParentState& prior,
                      const ActivationRequested& request);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationChildMapping>
map_activation_child(const ActivationChildDefinition& definition,
                     const ActivationChildState& prior,
                     const ActivationParentMapped& mapped);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationChildInfluence>
evaluate_activation_child_consumer(
    const ActivationChildConsumerDefinition& definition,
    const ActivationChildObservation& observation);

[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationRelationshipState>
build_activation_relationship_initial_state(
    const ActivationRelationshipDefinition& definition,
    const ActivationRelationshipInitialInput& input);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationParentState>
build_activation_parent_initial_state(
    const ActivationParentDefinition& definition,
    const ActivationParentInitialInput& input);
[[nodiscard]] gnc::foundation::NumericalOutcome<ActivationChildState>
build_activation_child_initial_state(
    const ActivationChildDefinition& definition,
    const ActivationChildInitialInput& input);

[[nodiscard]] ActivationRelationshipState
clone_activation_relationship_state(
    const ActivationRelationshipState& state);
[[nodiscard]] bool validate_activation_relationship_state(
    const ActivationRelationshipState& state) noexcept;
void swap_activation_relationship_state(ActivationRelationshipState& lhs,
                                        ActivationRelationshipState& rhs)
    noexcept;
[[nodiscard]] const ActivationRelationshipStateCodec&
activation_relationship_state_codec() noexcept;

[[nodiscard]] ActivationParentState clone_activation_parent_state(
    const ActivationParentState& state);
[[nodiscard]] bool validate_activation_parent_state(
    const ActivationParentState& state) noexcept;
void swap_activation_parent_state(ActivationParentState& lhs,
                                  ActivationParentState& rhs) noexcept;
[[nodiscard]] const ActivationParentStateCodec&
activation_parent_state_codec() noexcept;

[[nodiscard]] ActivationChildState clone_activation_child_state(
    const ActivationChildState& state);
[[nodiscard]] bool validate_activation_child_state(
    const ActivationChildState& state) noexcept;
void swap_activation_child_state(ActivationChildState& lhs,
                                 ActivationChildState& rhs) noexcept;
[[nodiscard]] const ActivationChildStateCodec&
activation_child_state_codec() noexcept;

} // namespace gnc::packages::yyz
