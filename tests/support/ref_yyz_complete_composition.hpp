#pragma once

#include <gnc/compiler/complete_execution_plan.hpp>

namespace gnc::tests::ref_yyz {

// One programmatic REF-YYZ composition root shared by the R2 qualification
// and the first R3 Image consumer. It is fixture support, not a frontend or a
// runtime registry.
[[nodiscard]] gnc::compiler::CompleteStaticCompositionSource
make_complete_source(
    const gnc::model_sdk::StaticPackageDescriptor& package);

// Full fixture facade for Kernel tests. Its implementation owns the package
// dependency, leaving the consumer translation unit free of YYZ concrete
// headers, identifiers, and catalog calls.
[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_complete_image();

} // namespace gnc::tests::ref_yyz
