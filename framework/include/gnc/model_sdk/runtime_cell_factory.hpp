#pragma once

#include "gnc/foundation/numerical_outcome.hpp"

#include <cstdint>
#include <string_view>

namespace gnc::model_sdk {

// Stable plan-local identity for one future Session Runtime Cell. R2 assigns
// it and exact-links its package factory, but never invokes that factory.
struct RuntimeInstanceId {
    std::uint32_t value = 0U;
};

// Immutable resource-plan facts passed by the future package-owned R3
// composition boundary. A handle of zero or an empty layout is invalid; the
// current REF-YYZ slice links only the explicit no-workspace layout.
struct RuntimeCellResourcePlanView {
    std::uint32_t handle = 0U;
    std::string_view workspace_layout_id;
};

struct RuntimeCellFactoryContext {
    RuntimeInstanceId runtime_instance_id;
    std::uint32_t runtime_component_handle = 0U;
    RuntimeCellResourcePlanView resources;
};

template <typename RuntimeCell, typename Definition, typename Bindings>
using RuntimeCellFactoryCall =
    gnc::foundation::NumericalOutcome<RuntimeCell> (*)(
        const Definition&, const RuntimeCellFactoryContext&,
        const Bindings&);

struct OutputWriterTokenId {
    std::uint32_t value = 0U;
};

template <typename Value>
struct CompiledOutputWriter {
    std::uint32_t slot_handle = 0U;
    OutputWriterTokenId writer_token;
};

// R3 constructs these package-typed bound references only after loading the
// immutable Image. R2 freezes their numeric authorization/entry handles but
// never supplies PreparedModel objects or calls the entry.
template <typename PreparedModel, typename Callable>
struct BoundProviderHandle {
    std::uint32_t invocation_handle = 0U;
    std::uint32_t provider_plan_handle = 0U;
    std::uint32_t entry_handle = 0U;
    const PreparedModel* prepared_model = nullptr;
    Callable callable = nullptr;
};

template <typename PreparedModel, typename Callable>
using BoundQueryHandle = BoundProviderHandle<PreparedModel, Callable>;

template <typename PreparedModel, typename Callable>
using BoundClosureHandle = BoundProviderHandle<PreparedModel, Callable>;

} // namespace gnc::model_sdk
