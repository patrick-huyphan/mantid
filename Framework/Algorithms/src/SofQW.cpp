#include <stdexcept>

#include "MantidAPI/BinEdgeAxis.h"
#include "MantidAPI/CommonBinsValidator.h"
#include "MantidAPI/HistogramValidator.h"
#include "MantidAPI/InstrumentValidator.h"
#include "MantidAPI/SpectraAxisValidator.h"
#include "MantidAPI/SpectrumDetectorMapping.h"
#include "MantidAPI/WorkspaceFactory.h"
#include "MantidAPI/WorkspaceUnitValidator.h"
#include "MantidAlgorithms/SofQW.h"
#include "MantidDataObjects/Histogram1D.h"
#include "MantidGeometry/Instrument/DetectorGroup.h"
#include "MantidKernel/ArrayProperty.h"
#include "MantidKernel/BoundedValidator.h"
#include "MantidKernel/CompositeValidator.h"
#include "MantidKernel/ListValidator.h"
#include "MantidKernel/PhysicalConstants.h"
#include "MantidKernel/RebinParamsValidator.h"
#include "MantidKernel/UnitFactory.h"
#include "MantidKernel/VectorHelper.h"

namespace Mantid {
namespace Algorithms {

// Register the algorithm into the AlgorithmFactory
DECLARE_ALGORITHM(SofQW)

/// Energy to K constant
double SofQW::energyToK() {
  static const double energyToK = 8.0 * M_PI * M_PI *
                                  PhysicalConstants::NeutronMass *
                                  PhysicalConstants::meV * 1e-20 /
                                  (PhysicalConstants::h * PhysicalConstants::h);
  return energyToK;
}

using namespace Kernel;
using namespace API;

/**
 * @return A summary of the algorithm
 */
const std::string SofQW::summary() const {
  return "Computes S(Q,w) using a either centre point or parallel-piped "
         "rebinning.\n"
         "The output from each method is:\n"
         "CentrePoint - centre-point rebin that takes no account of pixel "
         "curvature or area overlap\n\n"
         "Polygon - parallel-piped rebin, outputting a weighted-sum of "
         "overlapping polygons\n\n"
         "NormalisedPolygon - parallel-piped rebin, outputting a weighted-sum "
         "of "
         "overlapping polygons normalised by the fractional area of each "
         "overlap";
}

/**
 * Create the input properties
 */
void SofQW::init() {
  createCommonInputProperties(*this);

  // Add the Method property to control which algorithm is called
  const char *methodOptions[] = {"Centre", "Polygon", "NormalisedPolygon"};
  this->declareProperty(
      "Method", "Centre",
      boost::make_shared<StringListValidator>(
          std::vector<std::string>(methodOptions, methodOptions + 3)),
      "Defines the method used to compute the output.");
}

/**
 * Create the common set of input properties for the given algorithm
 * @param alg An algorithm object
 */
void SofQW::createCommonInputProperties(API::Algorithm &alg) {
  auto wsValidator = boost::make_shared<CompositeValidator>();
  wsValidator->add<WorkspaceUnitValidator>("DeltaE");
  wsValidator->add<SpectraAxisValidator>();
  wsValidator->add<CommonBinsValidator>();
  wsValidator->add<HistogramValidator>();
  wsValidator->add<InstrumentValidator>();
  alg.declareProperty(make_unique<WorkspaceProperty<>>(
                          "InputWorkspace", "", Direction::Input, wsValidator),
                      "Reduced data in units of energy transfer DeltaE.\nThe "
                      "workspace must contain histogram data and have common "
                      "bins across all spectra.");
  alg.declareProperty(make_unique<WorkspaceProperty<>>("OutputWorkspace", "",
                                                       Direction::Output),
                      "The name to use for the q-omega workspace.");
  alg.declareProperty(
      make_unique<ArrayProperty<double>>(
          "QAxisBinning", boost::make_shared<RebinParamsValidator>()),
      "The bin parameters to use for the q axis (in the format used by the "
      ":ref:`algm-Rebin` algorithm).");

  std::vector<std::string> propOptions{"Direct", "Indirect"};
  alg.declareProperty("EMode", "",
                      boost::make_shared<StringListValidator>(propOptions),
                      "The energy transfer analysis mode (Direct/Indirect)");
  auto mustBePositive = boost::make_shared<BoundedValidator<double>>();
  mustBePositive->setLower(0.0);
  alg.declareProperty("EFixed", 0.0, mustBePositive,
                      "The value of fixed energy: :math:`E_i` (EMode=Direct) "
                      "or :math:`E_f` (EMode=Indirect) (meV).\nMust be set "
                      "here if not available in the instrument definition.");
  alg.declareProperty("ReplaceNaNs", false,
                      "If true, all NaN values in the output workspace are "
                      "replaced using the ReplaceSpecialValues algorithm.",
                      Direction::Input);
  alg.declareProperty(
      make_unique<ArrayProperty<double>>(
          "EAxisBinning", boost::make_shared<RebinParamsValidator>(true)),
      "The bin parameters to use for the E axis (optional, in the format "
      "used by the :ref:`algm-Rebin` algorithm).");
}

void SofQW::exec() {
  // Find the approopriate algorithm
  std::string method = this->getProperty("Method");
  std::string child = "SofQW" + method;

  // Setup and run
  Algorithm_sptr childAlg = boost::dynamic_pointer_cast<Algorithm>(
      createChildAlgorithm(child, 0.0, 1.0));
  // This will add the Method property to the child algorithm but it will be
  // ignored anyway...
  childAlg->copyPropertiesFrom(*this);
  childAlg->execute();

  MatrixWorkspace_sptr outputWS = childAlg->getProperty("OutputWorkspace");

  this->setProperty("OutputWorkspace", outputWS);

  // Progress reports & cancellation
  MatrixWorkspace_const_sptr inputWorkspace = getProperty("InputWorkspace");
  const size_t nHistos = inputWorkspace->getNumberHistograms();
  auto m_progress = make_unique<Progress>(this, 0.0, 1.0, nHistos);
  m_progress->report("Creating output workspace");
}

/** Creates the output workspace, setting the axes according to the input
 * binning parameters
 *  @param[in]  inputWorkspace The input workspace
 *  @param[in]  qbinParams The q-bin parameters from the user
 *  @param[out] qAxis The 'vertical' (q) axis defined by the given parameters
 *  @param[out] ebinParams The 'horizontal' (energy) axis parameters (optional)
 *  @return A pointer to the newly-created workspace
 */
API::MatrixWorkspace_sptr SofQW::setUpOutputWorkspace(
    const API::MatrixWorkspace_const_sptr &inputWorkspace,
    const std::vector<double> &qbinParams, std::vector<double> &qAxis,
    const std::vector<double> &ebinParams) {
  // Create vector to hold the new X axis values
  HistogramData::BinEdges xAxis(0);
  int xLength;
  if (ebinParams.empty()) {
    xAxis = inputWorkspace->refX(0);
    xLength = static_cast<int>(xAxis.size());
  } else {
    xLength = static_cast<int>(VectorHelper::createAxisFromRebinParams(
        ebinParams, xAxis.mutableRawData()));
  }
  // Create a vector to temporarily hold the vertical ('y') axis and populate
  // that
  const int yLength = static_cast<int>(
      VectorHelper::createAxisFromRebinParams(qbinParams, qAxis));

  // Create the output workspace
  MatrixWorkspace_sptr outputWorkspace = WorkspaceFactory::Instance().create(
      inputWorkspace, yLength - 1, xLength, xLength - 1);
  // Create a numeric axis to replace the default vertical one
  Axis *const verticalAxis = new BinEdgeAxis(qAxis);
  outputWorkspace->replaceAxis(1, verticalAxis);

  // Now set the axis values
  for (int i = 0; i < yLength - 1; ++i) {
    outputWorkspace->setBinEdges(i, xAxis);
  }

  // Set the axis units
  verticalAxis->unit() = UnitFactory::Instance().create("MomentumTransfer");
  verticalAxis->title() = "|Q|";

  // Set the X axis title (for conversion to MD)
  outputWorkspace->getAxis(0)->title() = "Energy transfer";

  outputWorkspace->setYUnit("");
  outputWorkspace->setYUnitLabel("Intensity");

  return outputWorkspace;
}

} // namespace Algorithms
} // namespace Mantid
