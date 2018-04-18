#include "MSDFit.h"
#include "../General/UserInputValidator.h"

#include "MantidAPI/AnalysisDataService.h"
#include "MantidAPI/FunctionFactory.h"
#include "MantidAPI/WorkspaceGroup.h"

#include "MantidQtWidgets/Common/SignalBlocker.h"
#include "MantidQtWidgets/LegacyQwt/RangeSelector.h"

#include <QFileInfo>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>

using namespace Mantid::API;

namespace {
Mantid::Kernel::Logger g_log("MSDFit");
}

namespace MantidQt {
namespace CustomInterfaces {
namespace IDA {
MSDFit::MSDFit(QWidget *parent)
    : IndirectFitAnalysisTab(parent), m_uiForm(new Ui::MSDFit) {
  m_uiForm->setupUi(parent);
  IndirectFitAnalysisTab::addPropertyBrowserToUI(m_uiForm.get());
}

void MSDFit::setup() {
  auto fitRangeSelector = m_uiForm->ppPlotTop->addRangeSelector("MSDRange");
  connect(fitRangeSelector, SIGNAL(minValueChanged(double)), this,
          SLOT(xMinSelected(double)));
  connect(fitRangeSelector, SIGNAL(maxValueChanged(double)), this,
          SLOT(xMaxSelected(double)));

  auto gaussian = FunctionFactory::Instance().createFunction("MSDGauss");
  auto peters = FunctionFactory::Instance().createFunction("MSDPeters");
  auto yi = FunctionFactory::Instance().createFunction("MSDYi");
  addComboBoxFunctionGroup("Gaussian", {gaussian});
  addComboBoxFunctionGroup("Peters", {peters});
  addComboBoxFunctionGroup("Yi", {yi});

  disablePlotGuess();
  disablePlotPreview();

  connect(m_uiForm->dsSampleInput, SIGNAL(dataReady(const QString &)), this,
          SLOT(newDataLoaded(const QString &)));
  connect(m_uiForm->pbSingleFit, SIGNAL(clicked()), this, SLOT(singleFit()));

  // Update plot when fit type changes
  connect(m_uiForm->spPlotSpectrum, SIGNAL(valueChanged(int)), this,
          SLOT(setSelectedSpectrum(int)));
  connect(m_uiForm->spPlotSpectrum, SIGNAL(valueChanged(int)), this,
          SLOT(updatePreviewPlots()));

  connect(m_uiForm->spSpectraMin, SIGNAL(valueChanged(int)), this,
          SLOT(specMinChanged(int)));
  connect(m_uiForm->spSpectraMax, SIGNAL(valueChanged(int)), this,
          SLOT(specMaxChanged(int)));

  connect(m_uiForm->pbPlot, SIGNAL(clicked()), this, SLOT(plotClicked()));
  connect(m_uiForm->pbSave, SIGNAL(clicked()), this, SLOT(saveClicked()));
  connect(m_uiForm->pbPlotPreview, SIGNAL(clicked()), this,
          SLOT(plotCurrentPreview()));

  connect(m_uiForm->ckPlotGuess, SIGNAL(stateChanged(int)), this,
          SLOT(updatePlotGuess()));
}

bool MSDFit::doPlotGuess() const {
  return m_uiForm->ckPlotGuess->isEnabled() &&
         m_uiForm->ckPlotGuess->isChecked();
}

void MSDFit::singleFit() { executeSingleFit(); }

bool MSDFit::validate() {
  UserInputValidator uiv;

  uiv.checkDataSelectorIsValid("Sample Input", m_uiForm->dsSampleInput);

  auto range = std::make_pair(startX(), endX());
  uiv.checkValidRange("Fitting Range", range);

  int specMin = m_uiForm->spSpectraMin->value();
  int specMax = m_uiForm->spSpectraMax->value();
  auto specRange = std::make_pair(specMin, specMax + 1);
  uiv.checkValidRange("Spectrum Range", specRange);

  // In the future the MSDFit algorithm should be modified to allow this
  if (selectedFitType() == "None")
    uiv.addErrorMessage("No fit type has been selected");

  QString errors = uiv.generateErrorMessage();
  showMessageBox(errors);

  return errors.isEmpty();
}

void MSDFit::loadSettings(const QSettings &settings) {
  m_uiForm->dsSampleInput->readSettings(settings.group());
}

/**
 * Handles the completion of the MSDFit algorithm
 *
 * @param error If the algorithm chain failed
 */
void MSDFit::algorithmComplete(bool error) {
  if (error)
    return;

  IndirectFitAnalysisTab::fitAlgorithmComplete(outputWorkspaceName() +
                                               "_Parameters");
  // Enable plot and save
  m_uiForm->pbPlot->setEnabled(true);
  m_uiForm->pbSave->setEnabled(true);
}

void MSDFit::updatePreviewPlots() {
  IndirectFitAnalysisTab::updatePlots(m_uiForm->ppPlotTop,
                                      m_uiForm->ppPlotBottom);
}

void MSDFit::updatePlotRange() {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("MSDRange");
  if (m_uiForm->ppPlotTop->hasCurve("Sample")) {
    const auto range = m_uiForm->ppPlotTop->getCurveRange("Sample");
    rangeSelector->setRange(range.first, range.second);
  }
}

void MSDFit::disablePlotGuess() { m_uiForm->ckPlotGuess->setEnabled(false); }

void MSDFit::enablePlotGuess() { m_uiForm->ckPlotGuess->setEnabled(true); }

void MSDFit::updatePlotOptions() {}

void MSDFit::enablePlotResult() { m_uiForm->pbPlot->setEnabled(true); }

void MSDFit::disablePlotResult() { m_uiForm->pbPlot->setEnabled(false); }

void MSDFit::enableSaveResult() { m_uiForm->pbSave->setEnabled(true); }

void MSDFit::disableSaveResult() { m_uiForm->pbSave->setEnabled(false); }

void MSDFit::enablePlotPreview() { m_uiForm->pbPlotPreview->setEnabled(true); }

void MSDFit::disablePlotPreview() {
  m_uiForm->pbPlotPreview->setEnabled(false);
}

void MSDFit::addGuessPlot(Mantid::API::MatrixWorkspace_sptr workspace) {
  m_uiForm->ppPlotTop->addSpectrum("Guess", workspace, 0, Qt::green);
}

void MSDFit::removeGuessPlot() {
  m_uiForm->ppPlotTop->removeSpectrum("Guess");
  m_uiForm->ckPlotGuess->setChecked(false);
}

/**
 * Called when new data has been loaded by the data selector.
 *
 * Configures ranges for spin boxes before raw plot is done.
 *
 * @param wsName Name of new workspace loaded
 */
void MSDFit::newDataLoaded(const QString wsName) {
  IndirectFitAnalysisTab::newInputDataLoaded(wsName);
  auto const &workspace = inputWorkspace();
  int maxWsIndex = 0;

  if (workspace) {
    maxWsIndex = static_cast<int>(workspace->getNumberHistograms()) - 1;
  }

  m_uiForm->spPlotSpectrum->setMaximum(maxWsIndex);
  m_uiForm->spPlotSpectrum->setMinimum(0);
  m_uiForm->spPlotSpectrum->setValue(0);

  m_uiForm->spSpectraMin->setMaximum(maxWsIndex);
  m_uiForm->spSpectraMin->setMinimum(0);

  m_uiForm->spSpectraMax->setMaximum(maxWsIndex);
  m_uiForm->spSpectraMax->setMinimum(0);
  m_uiForm->spSpectraMax->setValue(maxWsIndex);
}

/**
 * Handles the user entering a new minimum spectrum index.
 *
 * Prevents the user entering an overlapping spectra range.
 *
 * @param value Minimum spectrum index
 */
void MSDFit::specMinChanged(int value) {
  m_uiForm->spSpectraMax->setMinimum(value);
}

/**
 * Handles the user entering a new maximum spectrum index.
 *
 * Prevents the user entering an overlapping spectra range.
 *
 * @param value Maximum spectrum index
 */
void MSDFit::specMaxChanged(int value) {
  m_uiForm->spSpectraMin->setMaximum(value);
}

void MSDFit::startXChanged(double startX) {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("MSDRange");
  MantidQt::API::SignalBlocker<QObject> blocker(rangeSelector);
  rangeSelector->setMinimum(startX);
}

void MSDFit::endXChanged(double endX) {
  auto rangeSelector = m_uiForm->ppPlotTop->getRangeSelector("MSDRange");
  MantidQt::API::SignalBlocker<QObject> blocker(rangeSelector);
  rangeSelector->setMaximum(endX);
}

/**
 * Handles saving of workspace
 */
void MSDFit::saveClicked() {
  IndirectFitAnalysisTab::saveResult(outputWorkspaceName());
}

/**
 * Handles mantid plotting
 */
void MSDFit::plotClicked() {
  IndirectFitAnalysisTab::plotResult(outputWorkspaceName(), "All");
}

} // namespace IDA
} // namespace CustomInterfaces
} // namespace MantidQt
