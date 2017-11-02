#include "ReflDataProcessorPresenter.h"
#include "MantidAPI/AlgorithmManager.h"
#include "MantidAPI/IEventWorkspace.h"
#include "MantidAPI/MatrixWorkspace.h"
#include "MantidAPI/Run.h"
#include "MantidQtWidgets/Common/DataProcessorUI/DataProcessorView.h"
#include "MantidQtWidgets/Common/DataProcessorUI/TreeManager.h"
#include "MantidQtWidgets/Common/ParseKeyValueString.h"
#include "MantidQtWidgets/Common/ParseNumerics.h"
#include "MantidQtWidgets/Common/ProgressPresenter.h"
#include "ReflFromStdStringMap.h"
#include <algorithm>

using namespace MantidQt::MantidWidgets::DataProcessor;
using namespace MantidQt::MantidWidgets;
using namespace Mantid::API;

namespace MantidQt {
namespace CustomInterfaces {

/**
* Constructor
* @param whitelist : The set of properties we want to show as columns
* @param preprocessMap : A map containing instructions for pre-processing
* @param processor : A ProcessingAlgorithm
* @param postprocessor : A PostprocessingAlgorithm
* workspaces
* @param postprocessMap : A map containing instructions for post-processing.
* This map links column name to properties of the post-processing algorithm
* @param loader : The algorithm responsible for loading data
*/
ReflDataProcessorPresenter::ReflDataProcessorPresenter(
    const WhiteList &whitelist,
    const std::map<QString, PreprocessingAlgorithm> &preprocessMap,
    const ProcessingAlgorithm &processor,
    const PostprocessingAlgorithm &postprocessor,
    const std::map<QString, QString> &postprocessMap, const QString &loader)
    : GenericDataProcessorPresenter(whitelist, preprocessMap, processor,
                                    postprocessor, postprocessMap, loader) {}

/**
* Destructor
*/
ReflDataProcessorPresenter::~ReflDataProcessorPresenter() {}

/**
 Process selected data
*/
void ReflDataProcessorPresenter::process() {

  // Get selected runs
  const auto newSelected = m_manager->selectedData(true);

  // Don't continue if there are no items to process
  if (newSelected.empty())
    return;

  // If uniform slicing is empty process normally, delegating to
  // GenericDataProcessorPresenter
  auto timeSlicingValues = m_mainPresenter->getTimeSlicingValues();
  if (timeSlicingValues.isEmpty()) {
    // Check if any input event workspaces still exist in ADS
    if (proceedIfWSTypeInADS(newSelected, true)) {
      setPromptUser(false); // Prevent prompting user twice
      GenericDataProcessorPresenter::process();
    }
    return;
  }

  m_selectedData = newSelected;

  // Check if any input non-event workspaces exist in ADS
  if (!proceedIfWSTypeInADS(m_selectedData, false))
    return;

  // Get global settings
  m_preprocessingOptions = m_mainPresenter->getPreprocessingOptionsAsString();
  m_processingOptions = m_mainPresenter->getProcessingOptions();
  m_postprocessingOptions = m_mainPresenter->getPostprocessingOptions();

  // Get time slicing type
  auto timeSlicingType = m_mainPresenter->getTimeSlicingType();

  // Progress report
  int progress = 0;
  int maxProgress = static_cast<int>(m_selectedData.size());
  ProgressPresenter progressReporter(progress, maxProgress, maxProgress,
                                     m_progressView);

  // True if all groups were processed as event workspaces
  bool allGroupsWereEvent = true;
  // True if errors where encountered when reducing table
  bool errors = false;

  // Loop in groups
  for (const auto &item : m_selectedData) {

    // Group of runs
    GroupData group = item.second;

    try {
      // First load the runs.
      bool allEventWS = loadGroup(group);

      if (allEventWS) {
        // Process the group
        if (processGroupAsEventWS(item.first, group, timeSlicingType,
                                  timeSlicingValues))
          errors = true;

        // Notebook not implemented yet
        if (m_view->getEnableNotebook()) {
          GenericDataProcessorPresenter::giveUserWarning(
              "Notebook not implemented for sliced data yet",
              "Notebook will not be generated");
        }

      } else {
        // Process the group
        if (processGroupAsNonEventWS(item.first, group))
          errors = true;
        // Notebook
      }

      if (!allEventWS)
        allGroupsWereEvent = false;

    } catch (...) {
      errors = true;
    }
    progressReporter.report();
  }

  if (!allGroupsWereEvent)
    m_view->giveUserWarning(
        "Some groups could not be processed as event workspaces", "Warning");
  if (errors)
    m_view->giveUserWarning("Some errors were encountered when "
                            "reducing table. Some groups may not have "
                            "been fully processed.",
                            "Warning");

  progressReporter.clear();
}

/** Loads a group of runs. Tries loading runs as event workspaces. If any of the
* workspaces in the group is not an event workspace, stops loading and re-loads
* all of them as non-event workspaces. We need the workspaces to be of the same
* type to process them together.
*
* @param group :: the group of runs
* @return :: true if all runs were loaded as event workspaces. False otherwise
*/
bool ReflDataProcessorPresenter::loadGroup(const GroupData &group) {

  // Set of runs loaded successfully
  std::set<QString> loadedRuns;

  for (const auto &row : group) {

    // The run number
    auto runNo = row.second.at(0);
    // Try loading as event workspace
    bool eventWS = loadEventRun(runNo);
    if (!eventWS) {
      // This run could not be loaded as event workspace. We need to load and
      // process the whole group as non-event data.
      for (const auto &rowNew : group) {
        // The run number
        auto runNo = rowNew.second.at(0);
        // Load as non-event workspace
        loadNonEventRun(runNo);
      }
      // Remove monitors which were loaded as separate workspaces
      for (const auto &run : loadedRuns) {
        AnalysisDataService::Instance().remove(
            ("TOF_" + run + "_monitors").toStdString());
      }
      return false;
    }
    loadedRuns.insert(runNo);
  }
  return true;
}

auto ReflDataProcessorPresenter::parseTimeSlicingType(QString const &type)
    -> TimeSlicingType {
  if (type == "Custom")
    return TimeSlicingType::Custom;
  else if (type == "LogValue")
    return TimeSlicingType::LogValue;
  else if (type == "Uniform")
    return TimeSlicingType::Uniform;
  else if (type == "UniformEven")
    return TimeSlicingType::UniformEven;
  else
    throw std::runtime_error("Unknown time slicing type " + type.toStdString() +
                             ".");
}

bool ReflDataProcessorPresenter::isUniform(TimeSlicingType type) const {
  return type == TimeSlicingType::Uniform ||
         type == TimeSlicingType::UniformEven;
}

/** Processes a group of runs
*
* @param groupID :: An integer number indicating the id of this group
* @param group :: the group of event workspaces
* @param timeSlicingType :: The type of time slicing being used
* @param timeSlicingValues :: The string of values to perform time slicing with
* @return :: true if errors were encountered
*/
bool ReflDataProcessorPresenter::processGroupAsEventWS(
    int groupID, const GroupData &group, const QString &typeInput,
    const QString &timeSlicingValues) {

  auto type = parseTimeSlicingType(typeInput);

  bool errors = false;
  bool multiRow = group.size() > 1;
  size_t numGroupSlices = INT_MAX;

  std::vector<double> startTimes, stopTimes;
  QString logFilter; // Set if we are slicing by log value

  // For custom/log value slicing the start/stop times are the same for all rows
  if (type == TimeSlicingType::Custom)
    std::tie(startTimes, stopTimes) = parseCustom(timeSlicingValues);
  else if (type == TimeSlicingType::LogValue)
    std::tie(startTimes, stopTimes, logFilter) =
        parseLogValue(timeSlicingValues);

  for (const auto &row : group) {

    const auto rowID = row.first;  // Integer ID of this row
    const auto data = row.second;  // Vector containing data for this row
    auto runNo = row.second.at(0); // The run number

    if (isUniform(type)) {
      const auto runName = "TOF_" + runNo;
      std::tie(startTimes, stopTimes) =
          parseUniform(type, timeSlicingValues, runName);
    }

    size_t numSlices = startTimes.size();
    addNumSlicesEntry(groupID, rowID, std::make_pair(startTimes, stopTimes));

    for (size_t i = 0; i < numSlices; i++) {
      try {
        RowData slice(data);
        QString wsName =
            takeSlice(runNo, i, startTimes[i], stopTimes[i], logFilter);
        slice[0] = wsName;
        reduceRow(&slice);
        slice[0] = data[0];
        m_manager->update(groupID, rowID, slice);
      } catch (...) {
        return true;
      }
    }

    // For uniform slicing with multiple rows only the minimum number of slices
    // are common to each row
    if (multiRow && type == TimeSlicingType::Uniform)
      numGroupSlices = std::min(numGroupSlices, numSlices);
  }

  // Post-process (if needed)
  if (multiRow) {

    // All slices are common for uniform even, custom and log value slicing
    if (type != TimeSlicingType::Uniform)
      numGroupSlices = startTimes.size();

    addNumGroupSlicesEntry(groupID, std::make_pair(startTimes, stopTimes));

    for (size_t i = 0; i < numGroupSlices; i++) {
      GroupData groupNew;
      QStringList data;
      for (const auto &row : group) {
        data = row.second;
        data[0] = row.second[0] + sliceSuffix(startTimes[i], stopTimes[i]);
        groupNew[row.first] = data;
      }
      try {
        postProcessGroup(groupNew, i);
      } catch (...) {
        errors = true;
      }
    }
  }

  return errors;
}

/** Processes a group of non-event workspaces
*
* @param groupID :: An integer number indicating the id of this group
* @param group :: the group of event workspaces
* @return :: true if errors were encountered
*/
bool ReflDataProcessorPresenter::processGroupAsNonEventWS(int groupID,
                                                          GroupData &group) {

  bool errors = false;

  for (auto &row : group) {

    // Reduce this row
    reduceRow(&row.second);
    // Update the tree
    m_manager->update(groupID, row.first, row.second);
  }

  // Post-process (if needed)
  if (group.size() > 1) {
    try {
      postProcessGroup(group, groupID);
    } catch (...) {
      errors = true;
    }
  }

  return errors;
}

Mantid::API::IEventWorkspace_sptr
ReflDataProcessorPresenter::retrieveWorkspace(QString const &name) const {
  return AnalysisDataService::Instance().retrieveWS<IEventWorkspace>(
      name.toStdString());
}

/** Retrieves a workspace from the AnalysisDataService based on it's name.
 *
 * @param name :: The name of the workspace to retrieve.
 * @return A pointer to the retrieved workspace or null if the workspace does
 *not exist or
 * is not an event workspace.
 */
Mantid::API::IEventWorkspace_sptr
ReflDataProcessorPresenter::retrieveWorkspaceOrCritical(
    QString const &name) const {
  IEventWorkspace_sptr mws;
  if (workspaceExists(name)) {
    auto mws = retrieveWorkspace(name);
    if (mws == nullptr) {
      m_view->giveUserCritical("Workspace to slice " + name +
                                   " is not an event workspace!",
                               "Time slicing error");
      return nullptr;
    } else {
      return mws;
    }
  } else {
    m_view->giveUserCritical("Workspace to slice not found: " + name,
                             "Time slicing error");
    return nullptr;
  }
}

QString ReflDataProcessorPresenter::sliceSuffix(double startTime,
                                                double endTime) const {
  return "_" + QString::number(startTime) + "_to_" + QString::number(endTime);
}

std::pair<int, double> ReflDataProcessorPresenter::numberOfSlicesAndDuration(
    const QString &timeSlicing, TimeSlicingType slicingType,
    double totalDurationInSeconds) {
  switch (slicingType) {
  case TimeSlicingType::UniformEven: {
    auto numberOfSlices = parseDenaryInteger(timeSlicing);
    auto sliceDuration = totalDurationInSeconds / numberOfSlices;
    return std::make_pair(numberOfSlices, sliceDuration);
  }
  case TimeSlicingType::Uniform: {
    auto sliceDuration = parseDouble(timeSlicing);
    auto numberOfSlices =
        static_cast<int>(ceil(totalDurationInSeconds / sliceDuration));
    return std::make_pair(numberOfSlices, sliceDuration);
  }
  default:
    throw std::runtime_error("Cannot calculate number of slices and slice "
                             "duration for non uniform slicing type.");
  }
}

/** Parses a string to extract uniform time slicing
 *
 * @param timeSlicing :: The string to parse
 * @param slicingType :: The type of uniform slicing being used
 * @param wsName :: The name of the workspace to be sliced
 * @param startTimes :: Start times for the set of slices
 * @param stopTimes :: Stop times for the set of slices
 */
auto ReflDataProcessorPresenter::parseUniform(TimeSlicingType slicingType,
                                              const QString &timeSlicing,
                                              const QString &wsName)
    -> std::tuple<std::vector<double>, std::vector<double>> {
  IEventWorkspace_sptr mws = retrieveWorkspaceOrCritical(wsName);
  if (mws != nullptr) {
    const auto run = mws->run();
    const auto totalDuration = run.endTime() - run.startTime();
    double sliceDuration = .0;
    int numSlices = 0;

    std::tie(numSlices, sliceDuration) = numberOfSlicesAndDuration(
        timeSlicing, slicingType, totalDuration.total_seconds());

    // Add the start/stop times
    auto startTimes = std::vector<double>(numSlices);
    auto stopTimes = std::vector<double>(numSlices);

    for (int i = 0; i < numSlices; i++) {
      startTimes[i] = sliceDuration * i;
      stopTimes[i] = sliceDuration * (i + 1);
    }
    return std::make_tuple(startTimes, stopTimes);
  } else {
    return std::make_tuple(std::vector<double>(), std::vector<double>());
  }
}

/** Parses a string to extract custom time slicing
 *
 * @param timeSlicing :: The string to parse
 * @param startTimes :: Start times for the set of slices
 * @param stopTimes :: Stop times for the set of slices
 */
auto ReflDataProcessorPresenter::parseCustom(const QString &timeSlicing)
    -> std::tuple<std::vector<double>, std::vector<double>> {
  auto timeStr = timeSlicing.split(",");
  std::vector<double> times;
  std::transform(timeStr.begin(), timeStr.end(), std::back_inserter(times),
                 [](const QString &astr) { return parseDouble(astr); });

  size_t numSlices = times.size() > 1 ? times.size() - 1 : 1;

  // Add the start/stop times
  auto startTimes = std::vector<double>(numSlices);
  auto stopTimes = std::vector<double>(numSlices);

  if (times.size() == 1) {
    startTimes[0] = 0;
    stopTimes[0] = times[0];
  } else {
    for (size_t i = 0; i < numSlices; i++) {
      startTimes[i] = times[i];
      stopTimes[i] = times[i + 1];
    }
  }

  return std::make_tuple(std::move(startTimes), std::move(stopTimes));
}

/** Parses a string to extract log value filter and time slicing
 *
 * @param inputStr :: The string to parse
 * @param logFilter :: The log filter to use
 * @param startTimes :: Start times for the set of slices
 * @param stopTimes :: Stop times for the set of slices
 */
std::tuple<std::vector<double>, std::vector<double>, QString>
ReflDataProcessorPresenter::parseLogValue(const QString &inputStr) {
  auto strMap = fromStdStringMap(parseKeyValueString(inputStr.toStdString()));
  QString timeSlicing = strMap.at("Slicing");
  std::vector<double> startTimes, stopTimes;
  std::tie(startTimes, stopTimes) = parseCustom(timeSlicing);
  return std::make_tuple(std::move(startTimes), std::move(stopTimes),
                         strMap.at("LogFilter"));
}

bool ReflDataProcessorPresenter::workspaceExists(
    QString const &workspaceName) const {
  return AnalysisDataService::Instance().doesExist(workspaceName.toStdString());
}

/** Loads an event workspace and puts it into the ADS
*
* @param runNo :: The run number as a string
* @return :: True if algorithm was executed. False otherwise
*/
bool ReflDataProcessorPresenter::loadEventRun(const QString &runNo) {

  bool runFound;
  QString outName;
  QString prefix = "TOF_";
  QString instrument = m_view->getProcessInstrument();

  outName = findRunInADS(runNo, prefix, runFound);
  if (!runFound || !workspaceExists(outName + "_monitors") ||
      retrieveWorkspace(outName) == nullptr) {
    // Monitors must be loaded first and workspace must be an event workspace
    loadRun(runNo, instrument, prefix, "LoadEventNexus", runFound);
  }

  return runFound;
}

/** Loads a non-event workspace and puts it into the ADS
*
* @param runNo :: The run number as a string
*/
void ReflDataProcessorPresenter::loadNonEventRun(const QString &runNo) {

  bool runFound; // unused but required
  auto prefix = QString("TOF_");
  auto instrument = m_view->getProcessInstrument();

  findRunInADS(runNo, prefix, runFound);
  if (!runFound)
    loadRun(runNo, instrument, prefix, m_loader, runFound);
}

/** Tries loading a run from disk
 *
 * @param run : The name of the run
 * @param instrument : The instrument the run belongs to
 * @param prefix : The prefix to be prepended to the run number
 * @param loader : The algorithm used for loading runs
 * @param runFound : Whether or not the run was actually found
 * @returns string name of the run
 */
QString ReflDataProcessorPresenter::loadRun(const QString &run,
                                            const QString &instrument,
                                            const QString &prefix,
                                            const QString &loader,
                                            bool &runFound) {

  runFound = true;
  auto const fileName = instrument + run;
  auto const outputName = prefix + run;

  IAlgorithm_sptr algLoadRun =
      AlgorithmManager::Instance().create(loader.toStdString());
  algLoadRun->initialize();
  algLoadRun->setProperty("Filename", fileName.toStdString());
  algLoadRun->setProperty("OutputWorkspace", outputName.toStdString());
  if (loader == "LoadEventNexus")
    algLoadRun->setProperty("LoadMonitors", true);
  algLoadRun->execute();
  if (!algLoadRun->isExecuted()) {
    // Run not loaded from disk
    runFound = false;
    return "";
  }

  return outputName;
}

/** Takes a slice from a run and puts the 'sliced' workspace into the ADS
*
* @param runNo :: The run number as a string
* @param startTime :: Start time
* @param stopTime :: Stop time
* @param logFilter :: The log filter to use if slicing by log value
* @return :: the name of the sliced workspace (without prefix 'TOF_')
*/
QString ReflDataProcessorPresenter::takeSlice(const QString &runNo,
                                              size_t sliceIndex,
                                              double startTime, double stopTime,
                                              const QString &logFilter) {

  QString runName = "TOF_" + runNo;
  QString sliceName = runName + sliceSuffix(startTime, stopTime);
  QString monName = runName + "_monitors";
  QString filterAlg = logFilter.isEmpty() ? "FilterByTime" : "FilterByLogValue";

  // Filter the run using the appropriate filter algorithm
  IAlgorithm_sptr filter =
      AlgorithmManager::Instance().create(filterAlg.toStdString());
  filter->initialize();
  filter->setProperty("InputWorkspace", runName.toStdString());
  filter->setProperty("OutputWorkspace", sliceName.toStdString());
  if (filterAlg == "FilterByTime") {
    filter->setProperty("StartTime", startTime);
    filter->setProperty("StopTime", stopTime);
  } else { // FilterByLogValue
    filter->setProperty("MinimumValue", startTime);
    filter->setProperty("MaximumValue", stopTime);
    filter->setProperty("TimeTolerance", 1.0);
    filter->setProperty("LogName", logFilter.toStdString());
  }

  filter->execute();

  // Obtain the normalization constant for this slice
  IEventWorkspace_sptr mws = retrieveWorkspace(runName);
  double total = mws->run().getProtonCharge();
  mws = retrieveWorkspace(sliceName);
  double slice = mws->run().getProtonCharge();
  double scaleFactor = slice / total;

  IAlgorithm_sptr scale = AlgorithmManager::Instance().create("Scale");
  scale->initialize();
  scale->setProperty("InputWorkspace", monName.toStdString());
  scale->setProperty("Factor", scaleFactor);
  scale->setProperty("OutputWorkspace", "__" + monName.toStdString() + "_temp");
  scale->execute();

  IAlgorithm_sptr rebinDet =
      AlgorithmManager::Instance().create("RebinToWorkspace");
  rebinDet->initialize();
  rebinDet->setProperty("WorkspaceToRebin", sliceName.toStdString());
  rebinDet->setProperty("WorkspaceToMatch",
                        "__" + monName.toStdString() + "_temp");
  rebinDet->setProperty("OutputWorkspace", sliceName.toStdString());
  rebinDet->setProperty("PreserveEvents", false);
  rebinDet->execute();

  IAlgorithm_sptr append = AlgorithmManager::Instance().create("AppendSpectra");
  append->initialize();
  append->setProperty("InputWorkspace1",
                      "__" + monName.toStdString() + "_temp");
  append->setProperty("InputWorkspace2", sliceName.toStdString());
  append->setProperty("OutputWorkspace", sliceName.toStdString());
  append->setProperty("MergeLogs", true);
  append->execute();

  // Remove temporary monitor ws
  AnalysisDataService::Instance().remove("__" + monName.toStdString() +
                                         "_temp");

  return sliceName.mid(4);
}

/** Plots any currently selected rows */
void ReflDataProcessorPresenter::plotRow() {

  const auto items = m_manager->selectedData();
  if (items.size() == 0)
    return;

  // If slicing values are empty plot normally
  auto timeSlicingValues =
      m_mainPresenter->getTimeSlicingValues().toStdString();
  if (timeSlicingValues.empty()) {
    GenericDataProcessorPresenter::plotRow();
    return;
  }

  // Set of workspaces to plot
  QOrderedSet<QString> workspaces;
  // Set of workspaces not found in the ADS
  QSet<QString> notFound;

  for (const auto &item : items) {
    for (const auto &run : item.second) {
      auto &times = m_numSlicesMap.at(item.first).at(run.first);
      const auto &startTimes = times.first;
      const auto &endTimes = times.second;
      const auto wsName = getReducedWorkspaceName(run.second, "", "IvsQ_");

      assert(startTimes.size() == endTimes.size());
      for (size_t slice = 0; slice < startTimes.size(); slice++) {
        const auto sliceName =
            wsName + sliceSuffix(startTimes[slice], endTimes[slice]);
        if (workspaceExists(sliceName))
          workspaces.insert(sliceName, nullptr);
        else
          notFound.insert(sliceName);
      }
    }
  }

  if (!notFound.isEmpty())
    issueNotFoundWarning("rows", notFound);

  plotWorkspaces(workspaces);
}

/** This method returns, for a given set of rows, i.e. a group of runs, the name
* of the output (post-processed) workspace
*
* @param groupData : The data in a given group
* @param prefix : A prefix to be appended to the generated ws name
* @param index : The index of the slice
* @returns : The name of the workspace
*/
QString ReflDataProcessorPresenter::getPostprocessedWorkspaceName(
    const GroupData &groupData, size_t groupID, const QString &prefix, const QString& suffix) {
  QStringList outputNames;
  for (auto it = groupData.cbegin(); it != groupData.cend(); ++it) {
    auto& times = m_numSlicesMap[groupID][index];
    auto& endTimes = times.first;
    auto& startTimes = times.second;
    outputNames.append(getReducedWorkspaceName((*it).second) +
                       sliceSuffix(startTimes[(*it).first], endTimes[(*it).first]));
  }
  return prefix + outputNames.join("_");
}

/** Plots any currently selected groups */
void ReflDataProcessorPresenter::plotGroup() {

  const auto selectedGroups = m_manager->selectedData();
  if (selectedGroups.size() == 0)
    return;

  // If slicing values are empty plot normally
  auto timeSlicingValues = m_mainPresenter->getTimeSlicingValues();
  if (timeSlicingValues.isEmpty()) {
    GenericDataProcessorPresenter::plotGroup();
    return;
  }

  // Set of workspaces to plot
  QOrderedSet<QString> workspaces;
  // Set of workspaces not found in the ADS
  QSet<QString> notFound;

  for (const auto &groups : selectedGroups) {
    if (groups.second.size() > 1) {
      for (auto slice = 0ul; slice < groups.second.size(); ++slice) {
        const auto wsName = getPostprocessedWorkspaceName(
            groups.second, groups.first, "", "_IvsQ");

        if (workspaceExists(wsName))
          workspaces.insert(wsName, nullptr);
        else
          notFound.insert(wsName);
      }
    }
  }

  if (!notFound.isEmpty())
    issueNotFoundWarning("groups", notFound);

  plotWorkspaces(workspaces);
}

/** Asks user if they wish to proceed if the AnalysisDataService contains input
 * workspaces of a specific type
 *
 * @param data :: The data selected in the table
 * @param findEventWS :: Whether or not we are searching for event workspaces
 * @return :: Boolean - true if user wishes to proceed, false if not
 */
bool ReflDataProcessorPresenter::proceedIfWSTypeInADS(const TreeData &data,
                                                      const bool findEventWS) {

  QStringList foundInputWorkspaces;

  for (const auto &item : data) {
    const auto group = item.second;

    for (const auto &row : group) {
      bool runFound = false;
      auto runNo = row.second.at(0);
      auto outName = findRunInADS(runNo, "TOF_", runFound);

      if (runFound) {
        bool isEventWS = retrieveWorkspace(outName) != nullptr;
        if (findEventWS == isEventWS) {
          foundInputWorkspaces.append(outName);
        } else if (isEventWS) { // monitors must be loaded
          auto monName = outName + "_monitors";
          if (!workspaceExists(monName))
            foundInputWorkspaces.append(outName);
        }
      }
    }
  }

  if (foundInputWorkspaces.size() > 0) {
    // Input workspaces of type found, ask user if they wish to process
    auto foundStr = foundInputWorkspaces.join("\n");

    bool process = m_view->askUserYesNo(
        "Processing selected rows will replace the following workspaces:\n\n" +
            foundStr + "\n\nDo you wish to continue?",
        "Process selected rows?");

    if (process) {
      // Remove all found workspaces
      for (auto &wsName : foundInputWorkspaces) {
        AnalysisDataService::Instance().remove(wsName.toStdString());
      }
    }

    return process;
  }

  // No input workspaces of type found, proceed with reduction automatically
  return true;
}

/** Add entry for the number of slices for a row in a group
*
* @param groupID :: The ID of the group
* @param rowID :: The ID of the row in group
* @param numSlices :: Number of slices
*/
void ReflDataProcessorPresenter::addNumSlicesEntry(
    int groupID, int rowID,
    std::pair<std::vector<double>, std::vector<double>> numSlices) {
  m_numSlicesMap[groupID][rowID] = std::move(numSlices);
}

/** Add entry for the number of slices for all rows in a group
*
* @param groupID :: The ID of the group
* @param numSlices :: Number of slices
*/
void ReflDataProcessorPresenter::addNumGroupSlicesEntry(
    int groupID,
    std::pair<std::vector<double>, std::vector<double>> sliceTimes) {
  m_numGroupSlicesMap[groupID] = std::move(sliceTimes);
}
}
}
