//----------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------
#include <cmath>
#include <vector>

#include "MantidKernel/ArrayProperty.h"
#include "MantidAlgorithms/MuonAsymmetryCalc.h"

namespace Mantid
{
namespace Algorithms
{

using namespace Kernel;
using API::Progress;
using std::size_t;

// Register the class into the algorithm factory
DECLARE_ALGORITHM( MuonAsymmetryCalc)

/** Initialisation method. Declares properties to be used in algorithm.
 *
 */
void MuonAsymmetryCalc::init()
{
  declareProperty(new API::WorkspaceProperty<>("InputWorkspace", "",
      Direction::Input), "Name of the input workspace");
  declareProperty(new API::WorkspaceProperty<>("OutputWorkspace", "",
      Direction::Output), "The name of the workspace to be created as the output of the algorithm");

  declareProperty(new ArrayProperty<int> ("ForwardSpectra"),
      "The spectra numbers of the forward group");
  declareProperty(new ArrayProperty<int> ("BackwardSpectra"),
      "The spectra numbers of the backward group");
  declareProperty("Alpha", 1.0, "The balance parameter (default 1)", Direction::Input);
}

/** Executes the algorithm
 *
 */
void MuonAsymmetryCalc::exec()
{
  std::vector<int> forward_list = getProperty("ForwardSpectra");
  std::vector<int> backward_list = getProperty("BackwardSpectra");
  int forward = forward_list.size() ? forward_list[0] : 0;
  int backward = backward_list.size() ? backward_list[0] : 1;
  double alpha = getProperty("Alpha");

  //Get original workspace
  API::MatrixWorkspace_sptr inputWS = getProperty("InputWorkspace");

  API::MatrixWorkspace_sptr tmpWS;
  if (forward_list.size() > 1 || backward_list.size() > 1)
  {
    API::IAlgorithm_sptr group = createChildAlgorithm("GroupDetectors");
    group->setProperty("InputWorkspace", inputWS);
    group->setProperty("SpectraList", backward_list);
    group->setProperty("KeepUngroupedSpectra", true);
    group->execute();
    tmpWS = group->getProperty("OutputWorkspace");

    group = createChildAlgorithm("GroupDetectors");
    group->setProperty("InputWorkspace", tmpWS);
    group->setProperty("SpectraList", forward_list);
    group->setProperty("KeepUngroupedSpectra", true);
    group->execute();
    tmpWS = group->getProperty("OutputWorkspace");

    forward = 0;
    backward = 1;
  }
  else
    tmpWS = inputWS;

  //setProperty("OutputWorkspace", tmpWS); return;

  //Create a workspace with only one spectra for forward
  API::MatrixWorkspace_sptr outputWS = API::WorkspaceFactory::Instance().create(inputWS, 1,
                                                 inputWS->readX(0).size(),inputWS->blocksize());

  //Calculate asymmetry for each time bin
  //F-aB / F+aB
  Progress prog(this, 0.0, 1.0, tmpWS->blocksize());
  for (size_t j = 0; j < tmpWS->blocksize(); ++j)
  {
    // cal F-aB
    double numerator = tmpWS->dataY(forward)[j] - alpha * tmpWS->dataY(backward)[j];
    // cal F+aB
    double denominator = (tmpWS->dataY(forward)[j] + alpha * tmpWS->dataY(backward)[j]);

    // cal F-aB / F+aB
    outputWS->dataY(0)[j] = denominator ? numerator / denominator : 0.;

    // Work out the error (as in 1st attachment of ticket #4188)
    double error = 1.0;
    if(denominator) {
      // cal F + a2B
      double q1 =  tmpWS->dataY(forward)[j] + alpha * alpha * tmpWS->dataY(backward)[j];
      // cal 1 + ((f-aB)/(F+aB))2 
      double q2 = 1 + numerator*numerator/(denominator*denominator);
      // cal error
      error = sqrt(q1*q2)/denominator;
    } 
    outputWS->dataE(0)[j] = error;
     
    prog.report();
  }

  //Copy the imput time bins on to the output
  outputWS->dataX(0) = inputWS->readX(0);

  setProperty("OutputWorkspace", outputWS);
}

} // namespace Algorithm
} // namespace Mantid


