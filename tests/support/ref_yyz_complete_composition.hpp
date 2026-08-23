#pragma once

#include <gnc/compiler/complete_execution_plan.hpp>

namespace gnc::tests::ref_yyz {

// One programmatic REF-YYZ composition root shared by the R2 qualification
// and the first R3 Image consumer. It is fixture support, not a frontend or a
// runtime registry.
[[nodiscard]] gnc::compiler::CompleteStaticCompositionSource
make_complete_source(
    const gnc::model_sdk::StaticPackageDescriptor& package);

// Uses the same stable package contribution while assigning a distinct
// qualification identity and programmatic source-owned schedule/temporal
// overrides to the multi-rate HeldLatest edge.
[[nodiscard]] gnc::compiler::CompleteStaticCompositionSource
make_multirate_held_output_qualification_source(
    const gnc::model_sdk::StaticPackageDescriptor& package);

// 00A target-rate composition: 100 Hz truth navigation, 20 Hz guidance,
// 50 Hz control, 100 Hz actuation, and a source-owned 25 Hz observation fact
// on a 0.01 s base clock. Science and difference verdicts remain external to
// this executable conformance fixture.
[[nodiscard]] gnc::compiler::CompleteStaticCompositionSource
make_00a_target_rate_source(
    const gnc::model_sdk::StaticPackageDescriptor& package,
    std::int64_t terminal_tick);

// Canonical 00A abstract-engineering profile. The package-owned mapping
// supplies the 31.2304/121.4737 launch-local ENU initial condition, 680 kg
// mass, 30 s duration and 1/5/2/1/4 cadence to the ordinary source chain.
[[nodiscard]] gnc::compiler::CompleteStaticCompositionSource
make_00a_canonical_source(
    const gnc::model_sdk::StaticPackageDescriptor& package);

// Full fixture facade for Kernel tests. Its implementation owns the package
// dependency, leaving the consumer translation unit free of YYZ concrete
// headers, identifiers, and catalog calls.
[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_complete_image();

[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_multirate_held_output_qualification_image();

[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_00a_target_rate_image(std::int64_t terminal_tick);

[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_00a_canonical_image();

// Compiles the same executable composition through the ordinary Catalog and
// linker path while one Runtime Cell deliberately omits the optional
// Resettable capability. The returned Image is legal and remains executable;
// only completed-run reset is unavailable for that Session.
[[nodiscard]] gnc::compiler::CompleteOutcome<
    gnc::contracts::ExecutionPlanImage>
compile_complete_image_without_reset_capability();

} // namespace gnc::tests::ref_yyz
