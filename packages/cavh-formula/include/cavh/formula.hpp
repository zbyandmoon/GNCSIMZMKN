#pragma once

#include "gnc/contracts/sample_context.hpp"
#include "gnc/foundation/numerical_outcome.hpp"
#include "gnc/foundation/numerical_policy.hpp"
#include "gnc/model_sdk/algorithm_evaluation.hpp"
#include "gnc/model_sdk/model_metadata.hpp"
#include "gnc/model_sdk/static_descriptor.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gnc::packages::cavh {

inline constexpr std::string_view kCavhFormulaContractIdentity =
    "gnc.package.cavh.formula.contract.experimental@1";
inline constexpr std::string_view kCavhFormulaPackageIdentity =
    "gnc.package.cavh-formula.experimental@1";
inline constexpr std::string_view kCavhFormulaPackageVersion = "0.1.0";
inline constexpr std::string_view kCavhFormulaModelIdentity =
    "gnc.package.cavh.formula.legacy-transcribed.experimental@1";
inline constexpr std::string_view kGlideEnvelopeModelIdentity =
    "gnc.package.cavh.glide-envelope.parabolic.experimental@1";
inline constexpr std::string_view kGlideEnvelopeModelVersion = "0.1.0";
inline constexpr std::string_view kGlideEnvelopeConfigSchemaIdentity =
    "gnc.package.cavh.glide-envelope.config@1";
inline constexpr std::uint32_t kGlideEnvelopeConfigSchemaVersion = 1U;
inline constexpr std::string_view kGlideEnvelopeOutputContractIdentity =
    "gnc.contract.cavh.glide-envelope-query-output@1";
inline constexpr std::string_view kGlideEnvelopeRequestContractIdentity =
    "gnc.contract.cavh.glide-envelope-query-input@1";
inline constexpr std::string_view kGlideEnvelopePreparationCallShapeIdentity =
    "gnc.call-shape.cavh.glide-envelope.prepare@1";
inline constexpr std::string_view kGlideEnvelopeQueryCallShapeIdentity =
    "gnc.call-shape.cavh.glide-envelope.query@1";
inline constexpr gnc::foundation::AlgorithmIdentity
    kCavhFormulaPreparationIdentity{
        "gnc.package.cavh.formula.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kGlideEnvelopePreparationIdentity{
        "gnc.package.cavh.glide-envelope.prepare@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kGlideEnvelopeQueryIdentity{
        "gnc.package.cavh.glide-envelope.query@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCavhGammaReferenceIdentity{
        "gnc.package.cavh.formula.gamma-reference@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity kCavhTdctIdentity{
    "gnc.package.cavh.formula.tdct@1", "0.1.0"};
inline constexpr gnc::foundation::AlgorithmIdentity
    kCavhFormulaKernelIdentity{
        "gnc.package.cavh.formula.composite@1", "0.1.0"};

enum class GammaReferenceEquation : std::uint8_t {
    Eq17MachDependent,
    Eq18MachIndependent,
};

[[nodiscard]] constexpr std::string_view to_string(
    GammaReferenceEquation equation) noexcept {
    switch (equation) {
    case GammaReferenceEquation::Eq17MachDependent:
        return "eq17";
    case GammaReferenceEquation::Eq18MachIndependent:
        return "eq18";
    }
    return "unknown";
}

enum class TdctSaturation : std::uint8_t {
    None,
    Lower,
    Upper,
};

[[nodiscard]] constexpr std::string_view to_string(
    TdctSaturation saturation) noexcept {
    switch (saturation) {
    case TdctSaturation::None:
        return "none";
    case TdctSaturation::Lower:
        return "lower";
    case TdctSaturation::Upper:
        return "upper";
    }
    return "unknown";
}

struct ParabolicEnvelopeDefinition {
    double cl_intercept = 0.0;
    double cl_slope_per_radian = 0.0;
    double cd0_base = 0.0;
    double cd0_slope_per_mach = 0.0;
    double induced_drag_factor = 0.0;
    double alpha_min_radians = 0.0;
    double alpha_max_radians = 0.0;
};

struct GlideEnvelopeDefinition {
    gnc::model_sdk::ModelDefinitionMetadata metadata;
    ParabolicEnvelopeDefinition polar;
};

[[nodiscard]] gnc::model_sdk::CanonicalConfigBlock
canonical_glide_envelope_config(
    const GlideEnvelopeDefinition& definition);

[[nodiscard]] gnc::foundation::NumericalOutcome<GlideEnvelopeDefinition>
build_glide_envelope_definition(
    const gnc::model_sdk::CanonicalConfigBlock& configuration);

class GlideEnvelopePreparedModel {
  public:
    GlideEnvelopePreparedModel(const GlideEnvelopePreparedModel&) = default;
    GlideEnvelopePreparedModel(GlideEnvelopePreparedModel&&) noexcept =
        default;
    GlideEnvelopePreparedModel& operator=(
        const GlideEnvelopePreparedModel&) = default;
    GlideEnvelopePreparedModel& operator=(
        GlideEnvelopePreparedModel&&) noexcept = default;

    [[nodiscard]] const GlideEnvelopeDefinition& definition() const noexcept;
    [[nodiscard]] const gnc::model_sdk::PreparedModelMetadata& metadata()
        const noexcept;

  private:
    explicit GlideEnvelopePreparedModel(
        std::shared_ptr<const GlideEnvelopeDefinition> definition,
        gnc::model_sdk::PreparedModelMetadata metadata) noexcept;

    std::shared_ptr<const GlideEnvelopeDefinition> definition_;
    gnc::model_sdk::PreparedModelMetadata metadata_;

    friend gnc::foundation::NumericalOutcome<GlideEnvelopePreparedModel>
    prepare_glide_envelope_model(GlideEnvelopeDefinition definition);
};

[[nodiscard]] gnc::foundation::NumericalOutcome<GlideEnvelopePreparedModel>
prepare_glide_envelope_model(GlideEnvelopeDefinition definition);

struct GlideEnvelopeQueryInput {
    double mach = 0.0;
};

struct GlideEnvelopeQueryOutput {
    double cd0 = 0.0;
    double cl_star = 0.0;
    double cd_star = 0.0;
    double lift_to_drag_maximum = 0.0;
    double alpha_star_radians = 0.0;
    double dcl_star_dmach = 0.0;
};

struct GlideEnvelopeQueryTelemetry {};

using GlideEnvelopeQueryEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<GlideEnvelopeQueryOutput,
                                        GlideEnvelopeQueryTelemetry>;

class GlideEnvelopeQueryKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        GlideEnvelopeQueryEvaluation>
    evaluate(const GlideEnvelopePreparedModel& model,
             const GlideEnvelopeQueryInput& input);
};

struct CavhFormulaAlgorithmDefinition {
    GammaReferenceEquation equation =
        GammaReferenceEquation::Eq18MachIndependent;
    double denominator_minimum_absolute = 0.0;
    double derivative_minimum_absolute_seconds_per_meter = 0.0;
    gnc::foundation::NumericalPolicy numerical_policy;
};

struct TdctFormulaDefinition {
    double gain = 0.0;
    double alpha_min_radians = 0.0;
    double alpha_max_radians = 0.0;
};

struct CavhFormulaDefinition {
    gnc::contracts::FrameIdentity navigation_frame;
    gnc::contracts::ClockDomainIdentity clock_domain;
    std::int64_t configuration_revision = -1;
    GlideEnvelopeDefinition envelope;
    CavhFormulaAlgorithmDefinition algorithm;
    TdctFormulaDefinition tdct;
};

[[nodiscard]] inline gnc::model_sdk::StaticPackageDescriptor
describe_cavh_formula_package() {
    gnc::model_sdk::StaticModelDescriptor envelope;
    envelope.definition = {
        std::string(kGlideEnvelopeModelIdentity),
        std::string(kGlideEnvelopeModelVersion),
        gnc::model_sdk::ModelExecutionForm::PureQuery};
    envelope.placement =
        gnc::model_sdk::ModelPlacement::VehicleOutput;
    envelope.preparation_algorithm_id =
        std::string(kGlideEnvelopePreparationIdentity.id);
    envelope.preparation_algorithm_version =
        std::string(kGlideEnvelopePreparationIdentity.version);
    envelope.preparation_call_shape_id =
        std::string(kGlideEnvelopePreparationCallShapeIdentity);
    envelope.configuration.schema_id =
        std::string(kGlideEnvelopeConfigSchemaIdentity);
    envelope.configuration.schema_version =
        kGlideEnvelopeConfigSchemaVersion;
    envelope.configuration.fields = {
        {"alpha_max_radians",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"alpha_min_radians",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"cd0_base", gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"cd0_slope_per_mach",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"cl_intercept",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"cl_slope_per_radian",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
        {"induced_drag_factor",
         gnc::model_sdk::CanonicalConfigValueKind::Float64},
    };
    envelope.pure_query =
        gnc::model_sdk::StaticPureQueryDescriptor{
            std::string(kGlideEnvelopeQueryIdentity.id),
            std::string(kGlideEnvelopeQueryIdentity.version),
            gnc::model_sdk::StaticWorkspaceRequirement::None,
            std::string(kGlideEnvelopeRequestContractIdentity),
            std::string(kGlideEnvelopeQueryCallShapeIdentity)};
    envelope.ports.push_back(
        {"envelope", std::string(kGlideEnvelopeOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Output,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::OneOrMore,
         gnc::model_sdk::TemporalRelation::NotApplicable});

    gnc::model_sdk::StaticAlgorithmDescriptor formula;
    formula.algorithm_id = std::string(kCavhFormulaKernelIdentity.id);
    formula.algorithm_version =
        std::string(kCavhFormulaKernelIdentity.version);
    formula.ports.push_back(
        {"glide-envelope",
         std::string(kGlideEnvelopeOutputContractIdentity),
         gnc::model_sdk::StaticPortDirection::Input,
         gnc::model_sdk::BindingKind::PureQuery,
         gnc::model_sdk::PortCardinality::ExactlyOne,
         gnc::model_sdk::TemporalRelation::NotApplicable});

    gnc::model_sdk::StaticPackageDescriptor package;
    package.package_id = std::string(kCavhFormulaPackageIdentity);
    package.package_version = std::string(kCavhFormulaPackageVersion);
    package.models.push_back(std::move(envelope));
    package.algorithms.push_back(std::move(formula));
    return package;
}

class PreparedCavhFormulaModel {
  public:
    PreparedCavhFormulaModel(const PreparedCavhFormulaModel&) = default;
    PreparedCavhFormulaModel(PreparedCavhFormulaModel&&) noexcept = default;
    PreparedCavhFormulaModel& operator=(
        const PreparedCavhFormulaModel&) = default;
    PreparedCavhFormulaModel& operator=(
        PreparedCavhFormulaModel&&) noexcept = default;

    [[nodiscard]] const CavhFormulaDefinition& definition() const noexcept;
    [[nodiscard]] const GlideEnvelopePreparedModel& glide_envelope_model()
        const noexcept;

  private:
    explicit PreparedCavhFormulaModel(
        std::shared_ptr<const CavhFormulaDefinition> definition,
        GlideEnvelopePreparedModel glide_envelope_model) noexcept;

    std::shared_ptr<const CavhFormulaDefinition> definition_;
    GlideEnvelopePreparedModel glide_envelope_model_;

    friend gnc::foundation::NumericalOutcome<PreparedCavhFormulaModel>
    prepare_cavh_formula_model(CavhFormulaDefinition definition);
};

[[nodiscard]] gnc::foundation::NumericalOutcome<PreparedCavhFormulaModel>
prepare_cavh_formula_model(CavhFormulaDefinition definition);

// Atmosphere and Mach derivatives are supplied products. This package owns
// the accepted CAVH envelope and formula evaluation only.
struct CavhOperatingPointInput {
    double density_kilograms_per_cubic_meter = 0.0;
    double density_altitude_gradient_kilograms_per_cubic_meter_per_meter =
        0.0;
    double altitude_meters = 0.0;
    double reference_radius_meters = 0.0;
    double speed_meters_per_second = 0.0;
    double gravity_meters_per_second_squared = 0.0;
    double mass_kilograms = 0.0;
    double reference_area_square_meters = 0.0;
    double mach = 0.0;
    double mach_speed_partial_seconds_per_meter = 0.0;
    double mach_altitude_partial_per_meter = 0.0;
    double bank_angle_radians = 0.0;
};

struct CavhFormulaInput {
    gnc::contracts::SampleContext context;
    CavhOperatingPointInput operating_point;
    double gamma_measured_radians = 0.0;
};

struct GammaReferenceInput {
    CavhFormulaInput formula;
    GlideEnvelopeQueryOutput envelope;
};

using CavhEnvelopeOutput = GlideEnvelopeQueryOutput;

struct GammaReferenceIntermediates {
    double density_kilograms_per_cubic_meter = 0.0;
    double density_altitude_gradient_kilograms_per_cubic_meter_per_meter =
        0.0;
    double radius_meters = 0.0;
    double dynamic_pressure_pascals = 0.0;
    double drag_force_newtons = 0.0;
    double cl_vertical = 0.0;
    double dcl_vertical_dmach = 0.0;
    double mach_speed_partial_seconds_per_meter = 0.0;
    double mach_altitude_partial_per_meter = 0.0;
    double dcl_vertical_dspeed_seconds_per_meter = 0.0;
    std::optional<double> b1;
    double b2 = 0.0;
    double b3 = 0.0;
};

struct GammaReferenceOutput {
    gnc::contracts::SampleContext context;
    GammaReferenceEquation equation =
        GammaReferenceEquation::Eq18MachIndependent;
    double alpha_star_radians = 0.0;
    double gamma_reference_radians = 0.0;
};

struct GammaReferenceTelemetry {
    GlideEnvelopeQueryOutput envelope;
    GammaReferenceIntermediates intermediates;
};

using GammaReferenceEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<GammaReferenceOutput,
                                        GammaReferenceTelemetry>;

struct TdctFormulaInput {
    gnc::contracts::SampleContext context;
    double alpha_star_radians = 0.0;
    double gamma_reference_radians = 0.0;
    double gamma_measured_radians = 0.0;
};

struct TdctFormulaOutput {
    gnc::contracts::SampleContext context;
    double alpha_limited_radians = 0.0;
};

struct TdctFormulaTelemetry {
    double error_radians = 0.0;
    double correction_radians = 0.0;
    double alpha_raw_radians = 0.0;
    TdctSaturation saturation = TdctSaturation::None;
};

using TdctFormulaEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<TdctFormulaOutput,
                                        TdctFormulaTelemetry>;

struct CavhFormulaOutput {
    GammaReferenceOutput gamma_reference;
    TdctFormulaOutput tdct;
};

struct CavhFormulaTelemetry {
    GammaReferenceTelemetry gamma_reference;
    TdctFormulaTelemetry tdct;
};

using CavhFormulaEvaluation =
    gnc::model_sdk::AlgorithmEvaluation<CavhFormulaOutput,
                                        CavhFormulaTelemetry>;

class CavhFormulaKernel {
  public:
    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        GammaReferenceEvaluation>
    evaluate_gamma_reference(const PreparedCavhFormulaModel& model,
                             const GammaReferenceInput& input);

    [[nodiscard]] static gnc::foundation::NumericalOutcome<
        TdctFormulaEvaluation>
    evaluate_tdct(const PreparedCavhFormulaModel& model,
                  const TdctFormulaInput& input);

    [[nodiscard]] static
        gnc::foundation::NumericalOutcome<CavhFormulaEvaluation>
    evaluate(const PreparedCavhFormulaModel& model,
             const CavhFormulaInput& input);
};

} // namespace gnc::packages::cavh
