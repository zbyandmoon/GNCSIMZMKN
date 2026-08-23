#pragma once

#include <gnc/model_sdk/in_process_codec.hpp>
#include <gnc/model_sdk/runtime_cell_factory.hpp>
#include <gnc/model_sdk/static_implementation.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace gnc::packages::yyz {

inline constexpr std::string_view kTwoEntityCausalPackageId =
    "gnc.package.yyz.two-entity-causal-qualification@1";
inline constexpr std::string_view kTwoEntityCausalPackageVersion = "0.1.0";
inline constexpr std::string_view kTwoEntityCausalBuildFingerprint =
    "build.yyz.two-entity-causal-qualification.release";
inline constexpr std::string_view kTwoEntityCausalModelVersion = "0.1.0";

inline constexpr std::string_view kEntityATruthModelId =
    "gnc.package.yyz.entity-a-truth.qualification@1";
inline constexpr std::string_view kOneTickTruthLinkModelId =
    "gnc.package.yyz.one-tick-truth-link.qualification@1";
inline constexpr std::string_view kEntityBTruthModelId =
    "gnc.package.yyz.entity-b-truth.qualification@1";

inline constexpr std::string_view kEntityATruthConfigSchemaId =
    "gnc.config.yyz.entity-a-truth@1";
inline constexpr std::string_view kOneTickTruthLinkConfigSchemaId =
    "gnc.config.yyz.one-tick-truth-link@1";
inline constexpr std::string_view kEntityBTruthConfigSchemaId =
    "gnc.config.yyz.entity-b-truth@1";
inline constexpr std::string_view kEntityATruthInitialSchemaId =
    "gnc.initial.yyz.entity-a-truth@1";
inline constexpr std::string_view kOneTickTruthLinkInitialSchemaId =
    "gnc.initial.yyz.one-tick-truth-link@1";
inline constexpr std::string_view kEntityBTruthInitialSchemaId =
    "gnc.initial.yyz.entity-b-truth@1";

inline constexpr std::string_view kEntityATruthObservationContractId =
    "gnc.contract.yyz.entity-a-truth-observation@1";
inline constexpr std::string_view kOneTickTruthLinkObservationContractId =
    "gnc.contract.yyz.one-tick-truth-link-observation@1";
inline constexpr std::string_view kEntityBTruthObservationContractId =
    "gnc.contract.yyz.entity-b-truth-observation@1";

inline constexpr std::string_view kEntityATruthStateSchemaId =
    "gnc.state-schema.yyz.entity-a-truth@1";
inline constexpr std::string_view kOneTickTruthLinkStateSchemaId =
    "gnc.state-schema.yyz.one-tick-truth-link@1";
inline constexpr std::string_view kEntityBTruthStateSchemaId =
    "gnc.state-schema.yyz.entity-b-truth@1";
inline constexpr std::string_view kEntityATruthStateLayoutId =
    "gnc.layout.yyz.entity-a-truth-state@1";
inline constexpr std::string_view kOneTickTruthLinkStateLayoutId =
    "gnc.layout.yyz.one-tick-truth-link-state@1";
inline constexpr std::string_view kEntityBTruthStateLayoutId =
    "gnc.layout.yyz.entity-b-truth-state@1";
inline constexpr std::string_view kEntityATruthObservationLayoutId =
    "gnc.layout.yyz.entity-a-truth-observation@1";
inline constexpr std::string_view kOneTickTruthLinkObservationLayoutId =
    "gnc.layout.yyz.one-tick-truth-link-observation@1";
inline constexpr std::string_view kEntityBTruthObservationLayoutId =
    "gnc.layout.yyz.entity-b-truth-observation@1";

struct EntityATruthDefinition {
    double increment_per_interval = 0.0;
};

struct OneTickTruthLinkDefinition {
    double transfer_gain = 1.0;
};

struct EntityBTruthDefinition {
    double current_gain = 0.0;
    double delayed_gain = 0.0;
};

struct EntityATruthState {
    double position = 0.0;
    std::uint64_t revision = 0U;
};

struct OneTickTruthLinkState {
    bool valid = false;
    std::int64_t source_tick = -1;
    double source_position = 0.0;
    std::uint64_t revision = 0U;
};

struct EntityBTruthState {
    double position = 0.0;
    double last_current_a_position = 0.0;
    double last_delayed_a_position = 0.0;
    bool last_delayed_valid = false;
    std::int64_t last_delayed_source_tick = -1;
    std::uint64_t revision = 0U;
};

struct EntityATruthObservation {
    std::int64_t tick = 0;
    double position = 0.0;
    std::uint64_t revision = 0U;
};

struct OneTickTruthLinkObservation {
    std::int64_t tick = 0;
    bool valid = false;
    std::int64_t source_tick = -1;
    double source_position = 0.0;
    std::uint64_t revision = 0U;
};

struct EntityBTruthObservation {
    std::int64_t tick = 0;
    EntityBTruthState state;
};

struct EntityATruthInitialInput {
    double position = 0.0;
};

struct OneTickTruthLinkInitialInput {
    double dormant_position = 0.0;
};

struct EntityBTruthInitialInput {
    double position = 0.0;
};

template <typename Definition>
struct TwoEntityRuntimeCell {
    Definition definition;
    gnc::model_sdk::RuntimeCellFactoryContext context;
};

struct TwoEntityRuntimeCellBindings {};

using EntityATruthRuntimeCell = TwoEntityRuntimeCell<EntityATruthDefinition>;
using OneTickTruthLinkRuntimeCell =
    TwoEntityRuntimeCell<OneTickTruthLinkDefinition>;
using EntityBTruthRuntimeCell = TwoEntityRuntimeCell<EntityBTruthDefinition>;

using EntityATruthDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<EntityATruthDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using OneTickTruthLinkDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<OneTickTruthLinkDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);
using EntityBTruthDefinitionBuilderCall =
    gnc::foundation::NumericalOutcome<EntityBTruthDefinition> (*)(
        const gnc::model_sdk::CanonicalConfigBlock&);

using EntityATruthRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        EntityATruthRuntimeCell, EntityATruthDefinition,
        TwoEntityRuntimeCellBindings>;
using OneTickTruthLinkRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        OneTickTruthLinkRuntimeCell, OneTickTruthLinkDefinition,
        TwoEntityRuntimeCellBindings>;
using EntityBTruthRuntimeCellFactoryCall =
    gnc::model_sdk::RuntimeCellFactoryCall<
        EntityBTruthRuntimeCell, EntityBTruthDefinition,
        TwoEntityRuntimeCellBindings>;

using EntityATruthProjectionCall = EntityATruthObservation (*)(
    const EntityATruthState&, std::int64_t);
using EntityATruthEvolutionCall =
    gnc::foundation::NumericalOutcome<EntityATruthState> (*)(
        const EntityATruthDefinition&, const EntityATruthState&,
        const EntityATruthObservation&);
using OneTickTruthLinkProjectionCall = OneTickTruthLinkObservation (*)(
    const OneTickTruthLinkState&, std::int64_t);
using OneTickTruthLinkEvolutionCall =
    gnc::foundation::NumericalOutcome<OneTickTruthLinkState> (*)(
        const OneTickTruthLinkDefinition&, const OneTickTruthLinkState&,
        const EntityATruthObservation&);
using EntityBTruthProjectionCall = EntityBTruthObservation (*)(
    const EntityBTruthState&, std::int64_t);
using EntityBTruthEvolutionCall =
    gnc::foundation::NumericalOutcome<EntityBTruthState> (*)(
        const EntityBTruthDefinition&, const EntityBTruthState&,
        const EntityATruthObservation&,
        const OneTickTruthLinkObservation&);

using EntityATruthInitialStateCall =
    gnc::foundation::NumericalOutcome<EntityATruthState> (*)(
        const EntityATruthDefinition&, const EntityATruthInitialInput&);
using OneTickTruthLinkInitialStateCall =
    gnc::foundation::NumericalOutcome<OneTickTruthLinkState> (*)(
        const OneTickTruthLinkDefinition&,
        const OneTickTruthLinkInitialInput&);
using EntityBTruthInitialStateCall =
    gnc::foundation::NumericalOutcome<EntityBTruthState> (*)(
        const EntityBTruthDefinition&, const EntityBTruthInitialInput&);

using EntityATruthStateCloneCall =
    EntityATruthState (*)(const EntityATruthState&);
using EntityATruthStateValidateCall =
    bool (*)(const EntityATruthState&) noexcept;
using EntityATruthStateSwapCall =
    void (*)(EntityATruthState&, EntityATruthState&) noexcept;
using EntityATruthStateCodec = gnc::model_sdk::InProcessStateCodec<
    EntityATruthStateCloneCall, EntityATruthStateValidateCall,
    EntityATruthStateValidateCall, EntityATruthStateValidateCall,
    EntityATruthStateSwapCall, EntityATruthStateCloneCall>;
using EntityATruthStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<EntityATruthStateCodec>;

using OneTickTruthLinkStateCloneCall =
    OneTickTruthLinkState (*)(const OneTickTruthLinkState&);
using OneTickTruthLinkStateValidateCall =
    bool (*)(const OneTickTruthLinkState&) noexcept;
using OneTickTruthLinkStateSwapCall =
    void (*)(OneTickTruthLinkState&, OneTickTruthLinkState&) noexcept;
using OneTickTruthLinkStateCodec = gnc::model_sdk::InProcessStateCodec<
    OneTickTruthLinkStateCloneCall, OneTickTruthLinkStateValidateCall,
    OneTickTruthLinkStateValidateCall, OneTickTruthLinkStateValidateCall,
    OneTickTruthLinkStateSwapCall, OneTickTruthLinkStateCloneCall>;
using OneTickTruthLinkStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<OneTickTruthLinkStateCodec>;

using EntityBTruthStateCloneCall =
    EntityBTruthState (*)(const EntityBTruthState&);
using EntityBTruthStateValidateCall =
    bool (*)(const EntityBTruthState&) noexcept;
using EntityBTruthStateSwapCall =
    void (*)(EntityBTruthState&, EntityBTruthState&) noexcept;
using EntityBTruthStateCodec = gnc::model_sdk::InProcessStateCodec<
    EntityBTruthStateCloneCall, EntityBTruthStateValidateCall,
    EntityBTruthStateValidateCall, EntityBTruthStateValidateCall,
    EntityBTruthStateSwapCall, EntityBTruthStateCloneCall>;
using EntityBTruthStateCodecGetter =
    gnc::model_sdk::InProcessCodecGetter<EntityBTruthStateCodec>;

[[nodiscard]] gnc::model_sdk::StaticPackageDescriptor
describe_two_entity_causal_package();
[[nodiscard]] gnc::model_sdk::StaticPackageImplementation
describe_two_entity_causal_implementation(
    std::string build_fingerprint =
        std::string(kTwoEntityCausalBuildFingerprint));

[[nodiscard]] gnc::foundation::NumericalOutcome<EntityATruthDefinition>
build_entity_a_truth_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<OneTickTruthLinkDefinition>
build_one_tick_truth_link_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);
[[nodiscard]] gnc::foundation::NumericalOutcome<EntityBTruthDefinition>
build_entity_b_truth_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

[[nodiscard]] gnc::foundation::NumericalOutcome<EntityATruthRuntimeCell>
create_entity_a_truth_runtime_cell(
    const EntityATruthDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<OneTickTruthLinkRuntimeCell>
create_one_tick_truth_link_runtime_cell(
    const OneTickTruthLinkDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings& bindings);
[[nodiscard]] gnc::foundation::NumericalOutcome<EntityBTruthRuntimeCell>
create_entity_b_truth_runtime_cell(
    const EntityBTruthDefinition& definition,
    const gnc::model_sdk::RuntimeCellFactoryContext& context,
    const TwoEntityRuntimeCellBindings& bindings);

[[nodiscard]] EntityATruthObservation project_entity_a_truth(
    const EntityATruthState& state, std::int64_t tick);
[[nodiscard]] gnc::foundation::NumericalOutcome<EntityATruthState>
evolve_entity_a_truth(const EntityATruthDefinition& definition,
                      const EntityATruthState& prior,
                      const EntityATruthObservation& opening);
[[nodiscard]] OneTickTruthLinkObservation project_one_tick_truth_link(
    const OneTickTruthLinkState& state, std::int64_t tick);
[[nodiscard]] gnc::foundation::NumericalOutcome<OneTickTruthLinkState>
evolve_one_tick_truth_link(
    const OneTickTruthLinkDefinition& definition,
    const OneTickTruthLinkState& prior,
    const EntityATruthObservation& current_a);
[[nodiscard]] EntityBTruthObservation project_entity_b_truth(
    const EntityBTruthState& state, std::int64_t tick);
[[nodiscard]] gnc::foundation::NumericalOutcome<EntityBTruthState>
evolve_entity_b_truth(
    const EntityBTruthDefinition& definition,
    const EntityBTruthState& prior,
    const EntityATruthObservation& current_a,
    const OneTickTruthLinkObservation& delayed_a);

[[nodiscard]] gnc::foundation::NumericalOutcome<EntityATruthState>
build_entity_a_truth_initial_state(
    const EntityATruthDefinition& definition,
    const EntityATruthInitialInput& input);
[[nodiscard]] gnc::foundation::NumericalOutcome<OneTickTruthLinkState>
build_one_tick_truth_link_initial_state(
    const OneTickTruthLinkDefinition& definition,
    const OneTickTruthLinkInitialInput& input);
[[nodiscard]] gnc::foundation::NumericalOutcome<EntityBTruthState>
build_entity_b_truth_initial_state(
    const EntityBTruthDefinition& definition,
    const EntityBTruthInitialInput& input);

[[nodiscard]] EntityATruthState clone_entity_a_truth_state(
    const EntityATruthState& state);
[[nodiscard]] bool validate_entity_a_truth_state(
    const EntityATruthState& state) noexcept;
void swap_entity_a_truth_state(EntityATruthState& lhs,
                               EntityATruthState& rhs) noexcept;
[[nodiscard]] const EntityATruthStateCodec&
entity_a_truth_state_codec() noexcept;

[[nodiscard]] OneTickTruthLinkState clone_one_tick_truth_link_state(
    const OneTickTruthLinkState& state);
[[nodiscard]] bool validate_one_tick_truth_link_state(
    const OneTickTruthLinkState& state) noexcept;
void swap_one_tick_truth_link_state(OneTickTruthLinkState& lhs,
                                    OneTickTruthLinkState& rhs) noexcept;
[[nodiscard]] const OneTickTruthLinkStateCodec&
one_tick_truth_link_state_codec() noexcept;

[[nodiscard]] EntityBTruthState clone_entity_b_truth_state(
    const EntityBTruthState& state);
[[nodiscard]] bool validate_entity_b_truth_state(
    const EntityBTruthState& state) noexcept;
void swap_entity_b_truth_state(EntityBTruthState& lhs,
                               EntityBTruthState& rhs) noexcept;
[[nodiscard]] const EntityBTruthStateCodec&
entity_b_truth_state_codec() noexcept;

} // namespace gnc::packages::yyz
