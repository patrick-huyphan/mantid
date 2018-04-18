#include "IqtFit.h"

#include "../General/UserInputValidator.h"

#include "MantidQtWidgets/Common/SignalBlocker.h"
#include "MantidQtWidgets/LegacyQwt/RangeSelector.h"

#include "MantidAPI/AlgorithmManager.h"
#include "MantidAPI/AnalysisDataService.h"
#include "MantidAPI/FunctionFactory.h"
#include "MantidAPI/WorkspaceGroup.h"

#include <QMenu>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>

#include <boost/algorithm/string/predicate.hpp>

using namespace Mantid::API;

namespace {
Mantid::Kernel::Logger g_log("IqtFit");
}

namespace MantidQt {
namespace CustomInterfaces {
namespace IDA {

IqtFit::IqtFit(QWidget *parent)
    : IndirectFitAnalysisTab(new IqtFitModel, parent),
      m_uiForm(new Ui::IqtFit) {
  m_uiForm->setupUi(parent);
  m_iqtFittingModel = dynamic_cast<IqtFitModel *>(fittingModel());
  setSpectrumSelectionView(m_uiForm->svSpectrumView);
  addPropertyBrowserToUI(m_uiForm.get());
}

void IqtFit::setup() {
  setMinimumSpectrum(0);
  setMaximumSpectrum(0);

  m_uiForm->ckPlotGuess->setChecked(false);
  disablePlotGuess();
  disablePlotPreview();

  // Create custom function groups
  const auto exponential =
      FunctionFactory::Instance().createFunction("ExpDecay");
  const auto stretchedExponential =
      FunctionFactory::Instance().createFunction("StretchExp");
  addSpinnerFunctionGroup("Exponential", {exponential}, 0, 2);
  addCheckBoxFunctionGroup("Stretched Exponential", {stretchedExponential});

  // Add custom settings
  addBoolCustomSetting("ConstrainIntensities", "Constrain Intensities");
  addBoolCustomSetting("ConstrainBeta", "Make Beta Global");
  addBoolCustomSetting("ExtractMembers", "Extract Members");
  setCustomSettingEnabled("ConstrainBeta", false);
  setCustomSettingEnabled("ConstrainIntensities", false);

  // Set available background options
  setBackgroundOptions({"None", "FlatBackground"});

  auto fitRangeSelector = m_uiForm->ppPlotTop->addRangeSelector("IqtFitRange");
  connect(fitRangeSelector, SIGNAL(minValueChanged(double)), this,
          SLOT(xMinSelected(double)));
  connect(fitRangeSelector, SIGNAL(maxValueChanged(double)), this,
          SLOT(xMaxSelected(double)));

  auto backRangeSelector = m_uiForm->ppPlotTop->addRangeSelector(
      "IqtFitBackRange", MantidWidgets::RangeSelector::YSINGLE);
  backRangeSelector->setVisible(false);
  backRangeSelector->setColour(Qt::darkGreen);
  backRangeSelector->setRange(0.0, 1.0);
  connect(backRangeSelector, SIGNAL(minValueChanged(double)), this,
          SLOT(backgroundSelectorChanged(double)));

  // Signal/slot ui connections
  connect(m_uiForm->dsSampleInput, SIGNAL(dataReady(const QString &)), this,
          SLOT(newDataLoaded(const QString &)));
  connect(m_uiForm->pbSingle, SIGNAL(clicked()), this, SLOT(singleFit()));

  // Update plot when fit type changes
  connect(m_uiForm->spPlotSpectrum, SIGNAL(valueChanged(int)), this,
          SLOT(setSelectedSpectrum(int)));
  connect(m_uiForm->spPlotSpectrum, SIGNAL(valueChanged(int)), this,
          SLOT(updatePreviewPlots()));

  connect(m_uiForm->pbPlot, SIGNAL(clicked()), this, SLOT(plotWorkspace()));
  connect(m_uiForm->pbSave, SIGNAL(clicked()), this, SLOT(saveResult()));
  connect(m_uiForm->pbPlotPreview, SIGNAL(clicked()), this,
          SLOT(plotCurrentPreview()));

  connect(m_uiForm->ckPlotGuess, SIGNAL(stateChanged(int)), this,
          SLOT(updatePlotGuess()));

  connect(this, SIGNAL(parameterChanged(const Mantid::API::IFunction *)), this,
          SLOT(parameterUpdated(const Mantid::API::IFunction *)));
  connect(this, SIGNAL(functionChanged()), this, SLOT(fitFunctionChanged()));
  connect(this, SIGNAL(customBoolChanged(const QString &, bool)), this,
          SLOT(customBoolUpdated(const QString &, bool)));
}

void IqtFit::fitFunctionChanged() {
  auto backRangeSelector =
      m_uiForm->ppPlotTop->getRangeSelector("IqtFitBackRange");

  if (backgroundName() == "None")
    backRangeSelector->setVisible(false);
  else
    backRangeSelector->setVisible(true);

  if (numberOfCustomFunctions("StretchExp") > 0) {
    setCustomSettingEnabled("ConstrainBeta", true);
  } else {
    setCustomBoolSetting("ConstrainBeta", false);
    setCustomSettingEnabled("ConstrainBeta", false);
  }
  m_iqtFittingModel->setFitTypeString(fitTypeString());
  updateIntensityTie();
}

void IqtFit::customBoolUpdated(const QString &key, bool value) {
  if (key == "Constrain Intensities") {
    if (value)
      updateIntensityTie();
    else
      removeTie(m_tiedParameter);
  }
}

void IqtFit::updateIntensityTie() {
  if (m_iqtFittingModel->getFittingFunction()) {
    removeTie(m_tiedParameter);
    const auto tie =
        QString::fromStdString(m_iqtFittingModel->createIntensityTie());
    updateIntensityTie(tie);
  } else {
    setCustomBoolSetting("ConstrainIntensities", false);
    setCustomSettingEnabled("ConstrainIntensities", false);
  }
}

void IqtFit::updateIntensityTie(const QString &intensityTie) {

  if (intensityTie.isEmpty()) {
    setCustomBoolSetting("ConstrainIntensities", false);
    setCustomSettingEnabled("ConstrainIntensities", false);
  } else {
    setCustomSettingEnabled("ConstrainIntensities", true);

    if (boolSettingValue("ConstrainIntensities")) {
      m_tiedParameter = intensityTie.split("=")[0];
      addTie(intensityTie);
    }
  }
}

bool IqtFit::doPlotGuess() const {
  return m_uiForm->ckPlotGuess->isEnabled() &&
         m_uiForm->ckPlotGuess->isChecked();
}

std::string IqtFit::fitTypeString() const {
  const auto numberOfExponential = numberOfCustomFunctions("ExpDecay");
  const auto numberOfStretched = numberOfCustomFunctions("StretchExp");

  if (numberOfExponential > 0)
    return std::to_string(numberOfExponential) + "E";

  if (numberOfStretched > 0)
    return std::to_string(numberOfStretched) + "S";

  return "";
}

void IqtFit::updatePlotOptions() {
  IndirectFitAnalysisTab::updatePlotOptions(m_uiForm->cbPlotType);
}

void IqtFit::enablePlotResult() { m_uiForm->pbPlot->setEnabled(true); }

void IqtFit::disablePlotResult() { m_uiForm->pbPlot->setEnabled(false); }

void IqtFit::enableSaveResult() { m_uiForm->pbSave->setEnabled(true); }

void IqtFit::disableSaveResult() { m_uiForm->pbSave->setEnabled(false); }

void IqtFit::enablePlotPreview() { m_uiForm->pbPlotPreview->setEnabled(true); }

void IqtFit::disablePlotPreview() {
  m_uiForm->pbPlotPreview->setEnabled(false);
}

/**
 * Plot workspace based on user input
 */
void IqtFit::plotWorkspace() {
  IndirectFitAnalysisTab::plotResult(m_uiForm->cbPlotType->currentText());
}

/**
 * Save the result of the algorithm
 */
void IqtFit::saveResult() { m_iqtFittingModel->saveResult(); }

/**
 * Handles completion of the IqtFitMultiple algorithm.
 * @param error True if the algorithm was stopped due to error, false otherwise
 * @param outputWsName The name of the output workspace
 */
void IqtFit::algorithmComplete(bool error) {

  if (error) {
    QString msg =
        "There was an error executing the fitting algorithm. Please see the "
        "Results Log pane for more details.";
    showMessageBox(msg);
    return;
  }

  IndirectFitAnalysisTab::fitAlgorithmComplete();
  m_uiForm->pbPlot->setEnabled(true);
  m_uiForm->pbSave->setEnabled(true);
  m_uiForm->cbPlotType->setEnabled(true);
}

bool IqtFit::validate() {
  UserInputValidator uiv;

  uiv.checkDataSelectorIsValid("Sample Input", m_uiForm->dsSampleInput);

  if (isEmptyModel())
    uiv.addErrorMessage("No fit function has been selected");

  if (inputWorkspace()->getXMin() < 0) {
    uiv.addErrorMessage("Error in input workspace: All X data must be "
                        "greater than or equal to 0.");
  }

  auto error = uiv.generateErrorMessage();
  emit showMessageBox(error);
  return error.isEmpty();
}

void IqtFit::loadSettings(const QSettings &settings) {
  m_uiForm->dsSampleInput->readSettings(settings.group());
}

/**
 * Called when new data has been loaded by the data selector.
 *
 * Configures ranges for spin boxes before raw plot is done.
 *
 * @param wsName Name of new workspace loaded
 */
void IqtFit::newDataLoaded(const QString wsName) {
  IndirectFitAnalysisTab::newInputDataLoaded(wsName);

  const auto maxWsIndex =
      static_cast<int>(inputWorkspace()->getNumberHistograms()) - 1;

  m_uiForm->spPlotSpectrum->setMaximum(maxWsIndex);
  m_uiForm->spPlotSpectrum->setMinimum(0);
  m_uiForm->spPlotSpectrum->setValue(0);
}

void IqtFit::backgroundSelectorChanged(double val) {
  m_iqtFittingModel->setDefaultParameterValue("A0", val, 0);
  setParameterValue("LinearBackground", "A0", val);
  setParameterValue("FlatBackground", "A0", val);
}

void IqtFit::parameterUpdated(const Mantid::API::IFunction *function) {
  if (function == nullptr)
    return;

  if (background() && function->asString() == background()->asString()) {
    auto rangeSelector =
        m_uiForm->ppPlotTop->getRangeSelector("IqtFitBackRange");
    MantidQt::API::SignalBlocker<QObject> blocker(rangeSelector);
    rangeSelector->setMinimum(function->getParameter("A0"));
  }
}

void IqtFit::updatePreviewPlots() {
  IndirectFitAnalysisTab::updatePlots(m_uiForm->ppPlotTop,
                                      m_uiForm->ppPlotBottom);
}

void IqtFit::updatePlotRange() {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("IqtFitRange");
  if (m_uiForm->ppPlotTop->hasCurve("Sample")) {
    const auto range = m_uiForm->ppPlotTop->getCurveRange("Sample");
    rangeSelector->setRange(range.first, range.second);
  }
}

void IqtFit::startXChanged(double startX) {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("IqtFitRange");
  MantidQt::API::SignalBlocker<QObject> blocker(rangeSelector);
  rangeSelector->setMinimum(startX);
}

void IqtFit::endXChanged(double endX) {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("IqtFitRange");
  MantidQt::API::SignalBlocker<QObject> blocker(rangeSelector);
  rangeSelector->setMaximum(endX);
}

void IqtFit::singleFit() { executeSingleFit(); }

void IqtFit::disablePlotGuess() { m_uiForm->ckPlotGuess->setEnabled(false); }

void IqtFit::enablePlotGuess() { m_uiForm->ckPlotGuess->setEnabled(true); }

void IqtFit::addGuessPlot(MatrixWorkspace_sptr workspace) {
  m_uiForm->ppPlotTop->addSpectrum("Guess", workspace, 0, Qt::green);
}

void IqtFit::removeGuessPlot() {
  m_uiForm->ppPlotTop->removeSpectrum("Guess");
  m_uiForm->ckPlotGuess->setChecked(false);
}
} // namespace IDA
} // namespace CustomInterfaces
} // namespace MantidQt
