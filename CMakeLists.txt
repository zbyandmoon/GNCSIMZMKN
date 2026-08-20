cmake_minimum_required(VERSION 3.20)

project(
    GNCZMKNNext
    VERSION 0.0.0
    DESCRIPTION "Greenfield implementation skeleton for the GNCZMKN target architecture"
    LANGUAGES CXX)

option(GNC_BUILD_TESTS "Build repository smoke tests" ON)
option(GNC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/GncWarnings.cmake)

find_package(Eigen3 3.4.0 EXACT CONFIG REQUIRED)

add_library(gnc_build_options INTERFACE)
target_compile_features(gnc_build_options INTERFACE cxx_std_17)
target_include_directories(
    gnc_build_options
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/framework/include>)

function(gnc_add_interface_module target)
    add_library(${target} INTERFACE)
    target_link_libraries(${target} INTERFACE gnc_build_options)
    add_library(gnc::${target} ALIAS ${target})
endfunction()

gnc_add_interface_module(foundation)
target_link_libraries(foundation INTERFACE Eigen3::Eigen)
gnc_add_interface_module(contracts)
target_link_libraries(contracts INTERFACE foundation)

gnc_add_interface_module(model_sdk)
target_link_libraries(model_sdk INTERFACE foundation contracts)

gnc_add_interface_module(compiler)
target_link_libraries(compiler INTERFACE foundation contracts model_sdk)

gnc_add_interface_module(kernel)
target_link_libraries(kernel INTERFACE foundation contracts)

gnc_add_interface_module(evidence)
target_link_libraries(evidence INTERFACE foundation contracts)

gnc_add_interface_module(workflow)
target_link_libraries(workflow INTERFACE foundation contracts evidence)

gnc_add_interface_module(application)
target_link_libraries(
    application
    INTERFACE foundation contracts compiler kernel evidence workflow)

gnc_add_interface_module(adapters)
target_link_libraries(adapters INTERFACE foundation contracts application)

add_library(
    yyz_rigid_step
    STATIC
        packages/yyz-rigid-step/src/rigid_step.cpp
        packages/yyz-rigid-step/src/mass_commit.cpp)
target_include_directories(
    yyz_rigid_step
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/packages/yyz-rigid-step/include>)
target_link_libraries(yyz_rigid_step PUBLIC foundation contracts model_sdk)
gnc_enable_warnings(yyz_rigid_step)
add_library(gnc::yyz_rigid_step ALIAS yyz_rigid_step)

add_library(
    cavh_formula
    STATIC
        packages/cavh-formula/src/formula.cpp)
target_include_directories(
    cavh_formula
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/packages/cavh-formula/include>)
target_link_libraries(cavh_formula PUBLIC foundation contracts model_sdk)
gnc_enable_warnings(cavh_formula)
add_library(gnc::cavh_formula ALIAS cavh_formula)

add_executable(gnc_sim apps/cli/main.cpp)
target_link_libraries(gnc_sim PRIVATE adapters)
gnc_enable_warnings(gnc_sim)

if(GNC_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(gnc_skeleton_smoke tests/skeleton_smoke.cpp)
    target_link_libraries(gnc_skeleton_smoke PRIVATE foundation)
    gnc_enable_warnings(gnc_skeleton_smoke)

    # R1 Foundation product slice: typed outcomes, RK4, interpolation and roots.
    add_executable(
        gnc_foundation_numerics_probe
        tests/foundation_numerics.cpp)
    target_link_libraries(gnc_foundation_numerics_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_numerics_probe)

    add_executable(
        gnc_foundation_trilinear_probe
        tests/foundation_trilinear.cpp)
    target_link_libraries(gnc_foundation_trilinear_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_trilinear_probe)

    add_executable(
        gnc_foundation_root_probe
        tests/foundation_root.cpp)
    target_link_libraries(gnc_foundation_root_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_root_probe)

    add_executable(
        gnc_foundation_local_root_probe
        tests/foundation_local_root.cpp)
    target_link_libraries(gnc_foundation_local_root_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_local_root_probe)

    add_executable(
        gnc_foundation_differentiation_probe
        tests/foundation_differentiation.cpp)
    target_link_libraries(
        gnc_foundation_differentiation_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_differentiation_probe)

    add_executable(
        gnc_foundation_one_sided_differentiation_probe
        tests/foundation_one_sided_differentiation.cpp)
    target_link_libraries(
        gnc_foundation_one_sided_differentiation_probe PRIVATE foundation)
    gnc_enable_warnings(
        gnc_foundation_one_sided_differentiation_probe)

    add_executable(
        gnc_foundation_spd_solve_probe
        tests/foundation_spd_solve.cpp)
    target_link_libraries(gnc_foundation_spd_solve_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_spd_solve_probe)

    add_executable(
        gnc_foundation_quaternion_probe
        tests/foundation_quaternion.cpp)
    target_link_libraries(gnc_foundation_quaternion_probe PRIVATE foundation)
    gnc_enable_warnings(gnc_foundation_quaternion_probe)

    # R1 YYZ product slice: prepared aero query through rigid RK4 candidate.
    add_executable(
        gnc_yyz_rigid_step_product_probe
        tests/yyz_product_rigid_step.cpp)
    target_link_libraries(
        gnc_yyz_rigid_step_product_probe PRIVATE yyz_rigid_step)
    gnc_enable_warnings(gnc_yyz_rigid_step_product_probe)

    # R1 YYZ product slice: scalar burn and two typed atomic boundaries.
    add_executable(
        gnc_yyz_two_interval_mass_commit_product_probe
        tests/yyz_product_two_interval_mass_commit.cpp)
    target_link_libraries(
        gnc_yyz_two_interval_mass_commit_product_probe
        PRIVATE yyz_rigid_step)
    gnc_enable_warnings(
        gnc_yyz_two_interval_mass_commit_product_probe)

    # R1 CAVH product slice: typed envelope, Eq17/Eq18 and TDCT formulas.
    add_executable(
        gnc_cavh_formula_product_probe
        tests/cavh_formula_product.cpp)
    target_link_libraries(
        gnc_cavh_formula_product_probe PRIVATE cavh_formula)
    gnc_enable_warnings(gnc_cavh_formula_product_probe)

    # R2 first compiler slice: typed source through a static YYZ/CAVH plan.
    add_executable(
        gnc_compiler_static_plan_probe
        tests/compiler_static_plan.cpp)
    target_link_libraries(
        gnc_compiler_static_plan_probe
        PRIVATE compiler yyz_rigid_step cavh_formula)
    gnc_enable_warnings(gnc_compiler_static_plan_probe)

    # R2 Catalog slice: first package-owned RuntimeComponent descriptor.
    add_executable(
        gnc_compiler_runtime_component_catalog_probe
        tests/compiler_runtime_component_catalog.cpp)
    target_link_libraries(
        gnc_compiler_runtime_component_catalog_probe
        PRIVATE compiler yyz_rigid_step cavh_formula)
    gnc_enable_warnings(gnc_compiler_runtime_component_catalog_probe)

    # R2 package contracts: real state owners and exact linked YYZ entries.
    add_executable(
        gnc_yyz_static_product_contracts_probe
        tests/yyz_static_product_contracts.cpp)
    target_link_libraries(
        gnc_yyz_static_product_contracts_probe
        PRIVATE compiler yyz_rigid_step)
    gnc_enable_warnings(gnc_yyz_static_product_contracts_probe)

    # R2 vertical review: canonical REF-YYZ planning/proof/exact-entry link.
    add_executable(
        gnc_compiler_complete_yyz_plan_probe
        tests/compiler_complete_yyz_plan.cpp
        tests/support/ref_yyz_complete_composition.cpp)
    target_link_libraries(
        gnc_compiler_complete_yyz_plan_probe
        PRIVATE compiler yyz_rigid_step)
    if(MSVC)
        target_compile_options(
            gnc_compiler_complete_yyz_plan_probe PRIVATE /bigobj)
    endif()
    gnc_enable_warnings(gnc_compiler_complete_yyz_plan_probe)

    # R0 validation spike: intentionally isolated from product modules and Legacy.
    add_executable(
        gnc_scientific_conventions
        tests/scientific_conventions.cpp)
    target_compile_features(gnc_scientific_conventions PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_scientific_conventions)

    # R0 scientific oracle probe: fixture-local and isolated from product code.
    add_executable(
        gnc_minimal_3dof_probe
        tests/minimal_3dof.cpp)
    target_compile_features(gnc_minimal_3dof_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_minimal_3dof_probe)

    # R0 observation-only benchmark: isolated from product modules and Legacy.
    add_executable(
        gnc_minimal_3dof_benchmark
        benchmarks/r0/minimal_3dof_benchmark.cpp)
    target_compile_features(gnc_minimal_3dof_benchmark PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_minimal_3dof_benchmark)

    # R0 CAVH scientific oracle: independent formula/guidance qualification.
    add_executable(
        gnc_cavh_formula_probe
        tests/cavh_formula.cpp)
    target_compile_features(gnc_cavh_formula_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_cavh_formula_probe)

    # R0 YYZ scientific oracle: accepted fixture-local rigid-body core.
    add_executable(
        gnc_yyz_6dof_core_probe
        tests/yyz_6dof_core.cpp)
    target_compile_features(gnc_yyz_6dof_core_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_6dof_core_probe)

    # R0 YYZ scientific oracle: fixture-local force/moment closure.
    add_executable(
        gnc_yyz_force_moment_closure_probe
        tests/yyz_force_moment_closure.cpp)
    target_compile_features(
        gnc_yyz_force_moment_closure_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_force_moment_closure_probe)

    # R0 YYZ scientific oracle: fixture-local supplied air-data kinematics.
    add_executable(
        gnc_yyz_air_data_kinematics_probe
        tests/yyz_air_data_kinematics.cpp)
    target_compile_features(
        gnc_yyz_air_data_kinematics_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_air_data_kinematics_probe)

    # R0 YYZ scientific oracle: fixture-local aero dimensionalization.
    add_executable(
        gnc_yyz_aero_dimensionalization_probe
        tests/yyz_aero_dimensionalization.cpp)
    target_compile_features(
        gnc_yyz_aero_dimensionalization_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_aero_dimensionalization_probe)

    # R0 YYZ scientific oracle: fixture-local trilinear aero lookup.
    add_executable(
        gnc_yyz_aero_lookup_probe
        tests/yyz_aero_lookup.cpp)
    target_compile_features(
        gnc_yyz_aero_lookup_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_aero_lookup_probe)

    # R0 YYZ scientific oracle: fixture-local supplied uniform environment.
    add_executable(
        gnc_yyz_uniform_environment_probe
        tests/yyz_uniform_environment.cpp)
    target_compile_features(
        gnc_yyz_uniform_environment_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_uniform_environment_probe)

    # R0 YYZ scientific oracle: fixture-local supplied propulsion response.
    add_executable(
        gnc_yyz_propulsion_response_probe
        tests/yyz_propulsion_response.cpp)
    target_compile_features(
        gnc_yyz_propulsion_response_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_propulsion_response_probe)

    # R0 YYZ scientific oracle: committed MassProperties projection.
    add_executable(
        gnc_yyz_mass_properties_probe
        tests/yyz_mass_properties.cpp)
    target_compile_features(
        gnc_yyz_mass_properties_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_mass_properties_probe)

    # R0 YYZ scientific oracle: sampled scalar-burn MassState evolution.
    add_executable(
        gnc_yyz_scalar_burn_mass_probe
        tests/yyz_scalar_burn_mass.cpp)
    target_compile_features(
        gnc_yyz_scalar_burn_mass_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_scalar_burn_mass_probe)

    # R0 YYZ scientific oracle: one composed FrozenInterval trajectory.
    add_executable(
        gnc_yyz_frozen_interval_probe
        tests/yyz_frozen_interval.cpp)
    target_compile_features(
        gnc_yyz_frozen_interval_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_frozen_interval_probe)

    # R0 YYZ scientific oracle: mass commit across two FrozenIntervals.
    add_executable(
        gnc_yyz_two_interval_mass_commit_probe
        tests/yyz_two_interval_mass_commit.cpp)
    target_compile_features(
        gnc_yyz_two_interval_mass_commit_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_two_interval_mass_commit_probe)

    # R0 YYZ scientific oracle: committed metrics and AtGrid result.
    add_executable(
        gnc_yyz_run_evaluation_probe
        tests/yyz_run_evaluation.cpp)
    target_compile_features(
        gnc_yyz_run_evaluation_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_run_evaluation_probe)

    # R0 YYZ scientific oracle: fixture-local mission composition.
    add_executable(
        gnc_yyz_mission_composition_probe
        tests/yyz_mission_composition.cpp)
    target_compile_features(
        gnc_yyz_mission_composition_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_yyz_mission_composition_probe)

    # R0 Legacy behavior oracle: independent journal, no Legacy linkage.
    add_executable(
        gnc_legacy_sync_commit_probe
        tests/legacy_sync_commit.cpp)
    target_compile_features(gnc_legacy_sync_commit_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_sync_commit_probe)

    # R0 Legacy publish oracle: independent projection, no Legacy linkage.
    add_executable(
        gnc_legacy_publish_probe
        tests/legacy_publish.cpp)
    target_compile_features(gnc_legacy_publish_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_publish_probe)

    # R0 Legacy phase oracle: independent trace model, no Legacy linkage.
    add_executable(
        gnc_legacy_phase_probe
        tests/legacy_phase.cpp)
    target_compile_features(gnc_legacy_phase_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_phase_probe)

    # R0 Legacy group oracle: independent joint RK4, no Legacy linkage.
    add_executable(
        gnc_legacy_continuous_group_probe
        tests/legacy_continuous_group.cpp)
    target_compile_features(
        gnc_legacy_continuous_group_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_continuous_group_probe)

    # R0 Legacy CSV oracle: semantic header mapping, no Legacy linkage.
    add_executable(
        gnc_legacy_csv_probe
        tests/legacy_csv.cpp)
    target_compile_features(gnc_legacy_csv_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_csv_probe)

    # R0 Legacy STOP oracle: independent semantic timeline, no Legacy linkage.
    add_executable(
        gnc_legacy_stop_probe
        tests/legacy_stop.cpp)
    target_compile_features(gnc_legacy_stop_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_stop_probe)

    # R0 Legacy SimFlow oracle: independent materialization/replay model.
    add_executable(
        gnc_legacy_simflow_probe
        tests/legacy_simflow.cpp)
    target_compile_features(gnc_legacy_simflow_probe PRIVATE cxx_std_17)
    gnc_enable_warnings(gnc_legacy_simflow_probe)

    add_test(NAME skeleton.library COMMAND gnc_skeleton_smoke)
    add_test(NAME skeleton.cli COMMAND gnc_sim --self-check)
    add_test(
        NAME r1.foundation-numerics.probe
        COMMAND gnc_foundation_numerics_probe --self-check)
    add_test(
        NAME r1.foundation-trilinear.probe
        COMMAND gnc_foundation_trilinear_probe --self-check)
    add_test(
        NAME r1.foundation-root.probe
        COMMAND gnc_foundation_root_probe --self-check)
    add_test(
        NAME r1.foundation-local-root.probe
        COMMAND gnc_foundation_local_root_probe --self-check)
    add_test(
        NAME r1.foundation-differentiation.probe
        COMMAND gnc_foundation_differentiation_probe --self-check)
    add_test(
        NAME r1.foundation-one-sided-differentiation.probe
        COMMAND gnc_foundation_one_sided_differentiation_probe
                --self-check)
    add_test(
        NAME r1.foundation-spd-solve.probe
        COMMAND gnc_foundation_spd_solve_probe --self-check)
    add_test(
        NAME r1.foundation-quaternion.probe
        COMMAND gnc_foundation_quaternion_probe --self-check)
    add_test(
        NAME r1.yyz-rigid-step.probe
        COMMAND gnc_yyz_rigid_step_product_probe --self-check)
    add_test(
        NAME r1.yyz-two-interval-mass-commit.probe
        COMMAND gnc_yyz_two_interval_mass_commit_product_probe --self-check)
    add_test(
        NAME r1.cavh-formula.probe
        COMMAND gnc_cavh_formula_product_probe --self-check)
    add_test(
        NAME r2.compiler-static-plan.probe
        COMMAND gnc_compiler_static_plan_probe --self-check)
    add_test(
        NAME r2.compiler-runtime-component-catalog.probe
        COMMAND gnc_compiler_runtime_component_catalog_probe --self-check)
    add_test(
        NAME r2.yyz-static-product-contracts.probe
        COMMAND gnc_yyz_static_product_contracts_probe --self-check)
    add_test(
        NAME r2.compiler-complete-yyz-plan.probe
        COMMAND gnc_compiler_complete_yyz_plan_probe --self-check)
    add_test(
        NAME r0.scientific-conventions.properties
        COMMAND gnc_scientific_conventions --self-check)
    add_test(
        NAME r0.minimal-3dof.probe
        COMMAND gnc_minimal_3dof_probe --self-check)
    add_test(
        NAME r0.performance-minimal-3dof.probe
        COMMAND gnc_minimal_3dof_benchmark --self-check)
    add_test(
        NAME r0.cavh-formula.probe
        COMMAND gnc_cavh_formula_probe --self-check)
    add_test(
        NAME r0.yyz-6dof-core.probe
        COMMAND gnc_yyz_6dof_core_probe --self-check)
    add_test(
        NAME r0.yyz-force-moment-closure.probe
        COMMAND gnc_yyz_force_moment_closure_probe --self-check)
    add_test(
        NAME r0.yyz-air-data-kinematics.probe
        COMMAND gnc_yyz_air_data_kinematics_probe --self-check)
    add_test(
        NAME r0.yyz-aero-dimensionalization.probe
        COMMAND gnc_yyz_aero_dimensionalization_probe --self-check)
    add_test(
        NAME r0.yyz-aero-lookup.probe
        COMMAND gnc_yyz_aero_lookup_probe --self-check)
    add_test(
        NAME r0.yyz-uniform-environment.probe
        COMMAND gnc_yyz_uniform_environment_probe --self-check)
    add_test(
        NAME r0.yyz-propulsion-response.probe
        COMMAND gnc_yyz_propulsion_response_probe --self-check)
    add_test(
        NAME r0.yyz-mass-properties.probe
        COMMAND gnc_yyz_mass_properties_probe --self-check)
    add_test(
        NAME r0.yyz-scalar-burn-mass.probe
        COMMAND gnc_yyz_scalar_burn_mass_probe --self-check)
    add_test(
        NAME r0.yyz-frozen-interval.probe
        COMMAND gnc_yyz_frozen_interval_probe --self-check)
    add_test(
        NAME r0.yyz-two-interval-mass-commit.probe
        COMMAND gnc_yyz_two_interval_mass_commit_probe --self-check)
    add_test(
        NAME r0.yyz-run-evaluation.probe
        COMMAND gnc_yyz_run_evaluation_probe --self-check)
    add_test(
        NAME r0.yyz-mission-composition.probe
        COMMAND gnc_yyz_mission_composition_probe --self-check)
    add_test(
        NAME r0.legacy-sync-commit.probe
        COMMAND gnc_legacy_sync_commit_probe --self-check)
    add_test(
        NAME r0.legacy-publish.probe
        COMMAND gnc_legacy_publish_probe --self-check)
    add_test(
        NAME r0.legacy-phase.probe
        COMMAND gnc_legacy_phase_probe --self-check)
    add_test(
        NAME r0.legacy-continuous-group.probe
        COMMAND gnc_legacy_continuous_group_probe --self-check)
    add_test(
        NAME r0.legacy-csv.probe
        COMMAND gnc_legacy_csv_probe --self-check)
    add_test(
        NAME r0.legacy-stop.probe
        COMMAND gnc_legacy_stop_probe --self-check)
    add_test(
        NAME r0.legacy-simflow.probe
        COMMAND gnc_legacy_simflow_probe --self-check)

    find_program(GNC_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(NOT GNC_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required for R0 conformance tests.")
    endif()
    add_test(
        NAME r0.schema-contracts
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-r0-specs.ps1)
    add_test(
        NAME r0.architecture-baseline
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-architecture-baseline.ps1)
    add_test(
        NAME r0.source-boundaries
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-source-boundaries.ps1)
    add_test(
        NAME r0.legacy-reproduction-evidence
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-legacy-reproduction.ps1
            -Quiet)
    add_test(
        NAME r0.license-provenance
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-license-provenance.ps1
            -Quiet)
    add_test(
        NAME r0.gate-readiness
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-r0-gate-readiness.ps1
            -Quiet)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME r2.compiler-canonical-semantic-hash.reference
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/r2_canonical_ir_hash_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-r2-canonical-ir/cases.json
            --source
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-001/source.json
            --asset-index
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-001/asset-index.json
            --probe
            $<TARGET_FILE:gnc_compiler_static_plan_probe>)
    add_test(
        NAME r1.model-sdk-prepared-model.conformance
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-model-sdk-prepared-models.py
            --yyz-probe
            $<TARGET_FILE:gnc_yyz_rigid_step_product_probe>
            --cavh-probe
            $<TARGET_FILE:gnc_cavh_formula_product_probe>)
    add_test(
        NAME r1.model-sdk-algorithm-evaluation.conformance
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-model-sdk-algorithm-evaluations.py
            --yyz-probe
            $<TARGET_FILE:gnc_yyz_rigid_step_product_probe>
            --cavh-probe
            $<TARGET_FILE:gnc_cavh_formula_product_probe>)
    add_test(
        NAME r1.yyz-rigid-step.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-yyz-rigid-step-product.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-frozen-interval/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-frozen-interval/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_rigid_step_product_probe>)
    add_test(
        NAME r1.yyz-two-interval-mass-commit.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-yyz-two-interval-mass-commit-product.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-two-interval-mass-commit/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-two-interval-mass-commit/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_two_interval_mass_commit_product_probe>)
    add_test(
        NAME r1.cavh-formula.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-cavh-formula-product.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_cavh_formula_product_probe>)
    add_test(
        NAME r0.scientific-conventions.cross-tool
        COMMAND
            ${GNC_POWERSHELL_EXECUTABLE}
            -NoLogo
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-scientific-conventions.ps1
            -CppExecutable $<TARGET_FILE:gnc_scientific_conventions>
            -PythonExecutable ${Python3_EXECUTABLE}
            -Quiet)
    add_test(
        NAME r0.minimal-3dof.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/minimal_3dof_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-minimal-3dof/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-minimal-3dof/reference.json
            --probe
            $<TARGET_FILE:gnc_minimal_3dof_probe>)
    add_test(
        NAME r1.foundation-numerics.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-numerics.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-minimal-3dof/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-minimal-3dof/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_numerics_probe>)
    add_test(
        NAME r1.foundation-trilinear.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-trilinear.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-aero-lookup/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-aero-lookup/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_trilinear_probe>)
    add_test(
        NAME r1.foundation-root.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-root.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_root_probe>)
    add_test(
        NAME r1.foundation-local-root.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-local-root.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_local_root_probe>)
    add_test(
        NAME r1.foundation-differentiation.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-differentiation.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_differentiation_probe>)
    add_test(
        NAME r1.foundation-one-sided-differentiation.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-one-sided-differentiation.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_one_sided_differentiation_probe>)
    add_test(
        NAME r1.foundation-spd-solve.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-spd-solve.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-mass-properties/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-mass-properties/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_spd_solve_probe>)
    add_test(
        NAME r1.foundation-quaternion.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-foundation-quaternion.py
            --convention-cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-scientific-conventions/cases.json
            --convention-profile
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-scientific-conventions/conventions.json
            --yyz-cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-6dof-core/cases.json
            --yyz-oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-6dof-core/reference.json
            --probe
            $<TARGET_FILE:gnc_foundation_quaternion_probe>)
    add_test(
        NAME r0.performance-minimal-3dof.baseline
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/r0_performance_baseline.py
            --manifest
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/r0/minimal-3dof/workload-manifest.json
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-minimal-3dof/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-minimal-3dof/reference.json
            --hardware-profile
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/r0/minimal-3dof/hardware-profile-windows-intel-12700k.json
            --baseline
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/r0/minimal-3dof/baseline-windows-intel-12700k.json
            --executable
            $<TARGET_FILE:gnc_minimal_3dof_benchmark>
            --configuration
            $<IF:$<CONFIG:Debug>,debug,release>
            --verify)
    add_test(
        NAME r0.cavh-formula.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/cavh_formula_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-cavh-formula/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-cavh-formula/reference.json
            --probe
            $<TARGET_FILE:gnc_cavh_formula_probe>)
    add_test(
        NAME r0.yyz-6dof-core.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_6dof_core_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-6dof-core/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-6dof-core/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_6dof_core_probe>)
    add_test(
        NAME r0.yyz-force-moment-closure.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_force_moment_closure_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-force-moment-closure/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-force-moment-closure/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_force_moment_closure_probe>)
    add_test(
        NAME r0.yyz-air-data-kinematics.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_air_data_kinematics_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-air-data-kinematics/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-air-data-kinematics/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_air_data_kinematics_probe>)
    add_test(
        NAME r0.yyz-aero-dimensionalization.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_aero_dimensionalization_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-aero-dimensionalization/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-aero-dimensionalization/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_aero_dimensionalization_probe>)
    add_test(
        NAME r0.yyz-aero-lookup.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_aero_lookup_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-aero-lookup/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-aero-lookup/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_aero_lookup_probe>)
    add_test(
        NAME r0.yyz-uniform-environment.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_uniform_environment_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-uniform-environment/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-uniform-environment/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_uniform_environment_probe>)
    add_test(
        NAME r0.yyz-propulsion-response.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_propulsion_response_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-propulsion-response/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-propulsion-response/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_propulsion_response_probe>)
    add_test(
        NAME r0.yyz-mass-properties.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_mass_properties_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-mass-properties/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-mass-properties/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_mass_properties_probe>)
    add_test(
        NAME r0.yyz-scalar-burn-mass.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_scalar_burn_mass_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-scalar-burn-mass/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-scalar-burn-mass/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_scalar_burn_mass_probe>)
    add_test(
        NAME r0.yyz-frozen-interval.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_frozen_interval_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-frozen-interval/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-frozen-interval/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_frozen_interval_probe>)
    add_test(
        NAME r0.yyz-two-interval-mass-commit.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_two_interval_mass_commit_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-two-interval-mass-commit/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-two-interval-mass-commit/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_two_interval_mass_commit_probe>)
    add_test(
        NAME r0.yyz-run-evaluation.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_run_evaluation_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-run-evaluation/cases.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-run-evaluation/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_run_evaluation_probe>)
    add_test(
        NAME r0.yyz-mission-composition.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_mission_composition_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-mission-composition/cases.json
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR}
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-mission-composition/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_mission_composition_probe>)
    add_test(
        NAME r0.yyz-canonical-bundle.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/yyz_canonical_bundle_reference.py
            --cases
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-yyz-001/cases.json
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR}
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-yyz-001/reference.json
            --probe
            $<TARGET_FILE:gnc_yyz_mission_composition_probe>)
    add_test(
        NAME r0.legacy-sync-commit.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_sync_commit_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-sync-commit/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-sync-commit/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_sync_commit_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-publish.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_publish_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-publish/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-publish/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_publish_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-phase.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_phase_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-phase/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-phase/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_phase_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-continuous-group.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_continuous_group_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-continuous-group/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-continuous-group/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_continuous_group_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-csv.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_csv_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-csv/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-csv/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_csv_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-stop.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_stop_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-stop/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-stop/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_stop_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
        NAME r0.legacy-simflow.oracle
        COMMAND
            ${Python3_EXECUTABLE}
            -I
            -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy_simflow_reference.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ref-legacy-simflow/input.json
            --oracle
            ${CMAKE_CURRENT_SOURCE_DIR}/oracles/ref-legacy-simflow/reference.json
            --probe
            $<TARGET_FILE:gnc_legacy_simflow_probe>
            --repo-root
            ${CMAKE_CURRENT_SOURCE_DIR})
endif()
