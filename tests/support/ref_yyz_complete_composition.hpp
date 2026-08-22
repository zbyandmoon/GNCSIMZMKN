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

// Compiles the same executable composition through the ordinary Catalog and
// linker path while one Runtime Cell deliberately omits the optional
// Resettable capability. The returned Image is legal and remains executable;
// only completed-run reset is unavailable for that Session.
[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_complete_image_without_reset_capability();

} // namespace gnc::tests::ref_yyz
