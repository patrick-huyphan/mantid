#ifndef MANTID_MANTIDWIDGETS_GENERICDATAPROCESSORPRESENTERTEST_H
#define MANTID_MANTIDWIDGETS_GENERICDATAPROCESSORPRESENTERTEST_H

#include <cxxtest/TestSuite.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MantidAPI/FrameworkManager.h"
#include "MantidAPI/TableRow.h"
#include "MantidAPI/WorkspaceGroup.h"
#include "MantidGeometry/Instrument.h"
#include "MantidQtWidgets/Common/DataProcessorUI/DataProcessorMockObjects.h"
#include "MantidQtWidgets/Common/DataProcessorUI/GenericDataProcessorPresenter.h"
#include "MantidQtWidgets/Common/DataProcessorUI/GenericDataProcessorTreeManagerFactory.h"
#include "MantidQtWidgets/Common/DataProcessorUI/ProgressableViewMockObject.h"
#include "MantidQtWidgets/Common/WidgetDllOption.h"
#include "MantidTestHelpers/WorkspaceCreationHelper.h"

using namespace MantidQt::MantidWidgets;
using namespace Mantid::API;
using namespace Mantid::Kernel;
using namespace testing;

//=====================================================================================
// Functional tests
//=====================================================================================

// Use this mocked presenter for tests that will start the reducing row/group
// workers/threads. This overrides the async methods to be non-async, allowing
// them to be tested.
class GenericDataProcessorPresenterNoThread
    : public GenericDataProcessorPresenter {
public:
  // Standard constructor
  GenericDataProcessorPresenterNoThread(
      const DataProcessorWhiteList &whitelist,
      const std::map<QString, DataProcessorPreprocessingAlgorithm>
          &preprocessMap,
      const DataProcessorProcessingAlgorithm &processor,
      const DataProcessorPostprocessingAlgorithm &postprocessor,
      const std::map<QString, QString> &postprocessMap =
          std::map<QString, QString>(),
      const QString &loader = "Load")
      : GenericDataProcessorPresenter(whitelist, preprocessMap, processor,
                                      postprocessor, postprocessMap, loader) {}

  // Delegating constructor (no pre-processing required)
  GenericDataProcessorPresenterNoThread(
      const DataProcessorWhiteList &whitelist,
      const DataProcessorProcessingAlgorithm &processor,
      const DataProcessorPostprocessingAlgorithm &postprocessor)
      : GenericDataProcessorPresenter(
            whitelist, std::map<QString, DataProcessorPreprocessingAlgorithm>(),
            processor, postprocessor) {}

  // Destructor
  ~GenericDataProcessorPresenterNoThread() override {}

private:
  // non-async row reduce
  void startAsyncRowReduceThread(RowItem *rowItem, int groupIndex) override {
    try {
      reduceRow(&rowItem->second);
      m_manager->update(groupIndex, rowItem->first, rowItem->second);
      m_manager->setProcessed(true, rowItem->first, groupIndex);
    } catch (std::exception &ex) {
      reductionError(QString(ex.what()));
      threadFinished(1);
    }
    threadFinished(0);
  }

  // non-async group reduce
  void startAsyncGroupReduceThread(GroupData &groupData,
                                   int groupIndex) override {
    try {
      postProcessGroup(groupData);
      if (m_manager->rowCount(groupIndex) == static_cast<int>(groupData.size()))
        m_manager->setProcessed(true, groupIndex);
    } catch (std::exception &ex) {
      reductionError(QString(ex.what()));
      threadFinished(1);
    }
    threadFinished(0);
  }

  // Overriden non-async methods have same implementation as parent class
  void process() override { GenericDataProcessorPresenter::process(); }
  void plotRow() override { GenericDataProcessorPresenter::plotRow(); }
  void plotGroup() override { GenericDataProcessorPresenter::process(); }
};

class GenericDataProcessorPresenterTest : public CxxTest::TestSuite {

private:
  DataProcessorWhiteList createReflectometryWhiteList() {

    DataProcessorWhiteList whitelist;
    whitelist.addElement("Run(s)", "InputWorkspace", "", true, "TOF_");
    whitelist.addElement("Angle", "ThetaIn", "");
    whitelist.addElement("Transmission Run(s)", "FirstTransmissionRun", "",
                         true, "TRANS_");
    whitelist.addElement("Q min", "MomentumTransferMin", "");
    whitelist.addElement("Q max", "MomentumTransferMax", "");
    whitelist.addElement("dQ/Q", "MomentumTransferStep", "");
    whitelist.addElement("Scale", "ScaleFactor", "");
    return whitelist;
  }

  std::map<QString, DataProcessorPreprocessingAlgorithm>
  createReflectometryPreprocessMap() {

    return std::map<QString, DataProcessorPreprocessingAlgorithm>{
        {"Run(s)",
         DataProcessorPreprocessingAlgorithm(
             "Plus", "TOF_", std::set<QString>{"LHSWorkspace", "RHSWorkspace",
                                               "OutputWorkspace"})},
        {"Transmission Run(s)",
         DataProcessorPreprocessingAlgorithm(
             "CreateTransmissionWorkspaceAuto", "TRANS_",
             std::set<QString>{"FirstTransmissionRun", "SecondTransmissionRun",
                               "OutputWorkspace"})}};
  }

  DataProcessorProcessingAlgorithm createReflectometryProcessor() {

    return DataProcessorProcessingAlgorithm(
        "ReflectometryReductionOneAuto",
        std::vector<QString>{"IvsQ_binned_", "IvsQ_", "IvsLam_"},
        std::set<QString>{"ThetaIn", "ThetaOut", "InputWorkspace",
                          "OutputWorkspace", "OutputWorkspaceWavelength",
                          "FirstTransmissionRun", "SecondTransmissionRun"});
  }

  DataProcessorPostprocessingAlgorithm createReflectometryPostprocessor() {

    return DataProcessorPostprocessingAlgorithm(
        "Stitch1DMany", "IvsQ_",
        std::set<QString>{"InputWorkspaces", "OutputWorkspace"});
  }

  ITableWorkspace_sptr createWorkspace(const QString &wsName) {
    return createWorkspace(wsName, m_presenter->getWhiteList());
  }

  ITableWorkspace_sptr
  createWorkspace(const QString &wsName,
                  const DataProcessorWhiteList &whitelist) {
    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();

    const int ncols = static_cast<int>(whitelist.size());

    auto colGroup = ws->addColumn("str", "Group");
    colGroup->setPlotType(0);

    for (int col = 0; col < ncols; col++) {
      auto column = ws->addColumn(
          "str", whitelist.colNameFromColIndex(col).toStdString());
      column->setPlotType(0);
    }

    if (wsName.length() > 0)
      AnalysisDataService::Instance().addOrReplace(wsName.toStdString(), ws);

    return ws;
  }

  void createTOFWorkspace(const QString &wsName,
                          const QString &runNumber = "") {
    auto tinyWS =
        WorkspaceCreationHelper::create2DWorkspaceWithReflectometryInstrument();
    auto inst = tinyWS->getInstrument();

    inst->getParameterMap()->addDouble(inst.get(), "I0MonitorIndex", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "PointDetectorStart", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "PointDetectorStop", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "LambdaMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "LambdaMax", 10.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorBackgroundMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorBackgroundMax",
                                       10.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorIntegralMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorIntegralMax", 10.0);

    tinyWS->mutableRun().addLogData(
        new PropertyWithValue<double>("Theta", 0.12345));
    if (!runNumber.isEmpty())
      tinyWS->mutableRun().addLogData(new PropertyWithValue<std::string>(
          "run_number", runNumber.toStdString()));

    AnalysisDataService::Instance().addOrReplace(wsName.toStdString(), tinyWS);
  }

  void createMultiPeriodTOFWorkspace(const QString &wsName,
                                     const QString &runNumber = "") {

    createTOFWorkspace(wsName + "_1", runNumber);
    createTOFWorkspace(wsName + "_2", runNumber);

    auto stdWorkspaceName = wsName.toStdString();

    WorkspaceGroup_sptr group = boost::make_shared<WorkspaceGroup>();
    group->addWorkspace(
        AnalysisDataService::Instance().retrieve(stdWorkspaceName + "_1"));
    group->addWorkspace(
        AnalysisDataService::Instance().retrieve(stdWorkspaceName + "_2"));

    AnalysisDataService::Instance().addOrReplace(stdWorkspaceName, group);
  }

  ITableWorkspace_sptr
  createPrefilledWorkspace(const QString &wsName,
                           const DataProcessorWhiteList &whitelist) {
    auto ws = createWorkspace(wsName, whitelist);
    TableRow row = ws->appendRow();
    row << "0"
        << "12345"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";
    row = ws->appendRow();
    row << "0"
        << "12346"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";
    row = ws->appendRow();
    row << "1"
        << "24681"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"

        << "";
    row = ws->appendRow();
    row << "1"
        << "24682"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"

        << "";
    return ws;
  }

  ITableWorkspace_sptr createPrefilledWorkspace(const QString &wsName) {
    auto ws = createWorkspace(wsName);
    TableRow row = ws->appendRow();
    row << "0"
        << "12345"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";
    row = ws->appendRow();
    row << "0"
        << "12346"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";
    row = ws->appendRow();
    row << "1"
        << "24681"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"

        << "";
    row = ws->appendRow();
    row << "1"
        << "24682"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"

        << "";
    return ws;
  }

  ITableWorkspace_sptr
  createPrefilledWorkspaceThreeGroups(const QString &wsName) {
    auto ws = createWorkspace(wsName);
    TableRow row = ws->appendRow();
    row << "0"
        << "12345"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "";
    row = ws->appendRow();
    row << "0"
        << "12346"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "";
    row = ws->appendRow();
    row << "1"
        << "24681"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "";
    row = ws->appendRow();
    row << "1"
        << "24682"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "";
    row = ws->appendRow();
    row << "2"
        << "30000"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "";
    row = ws->appendRow();
    row << "2"
        << "30001"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "";
    return ws;
  }

  ITableWorkspace_sptr
  createPrefilledWorkspaceWithTrans(const QString &wsName) {
    auto ws = createWorkspace(wsName);
    TableRow row = ws->appendRow();
    row << "0"
        << "12345"
        << "0.5"
        << "11115"
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"

        << "";
    row = ws->appendRow();
    row << "0"
        << "12346"
        << "1.5"
        << "11116"
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"

        << "";
    row = ws->appendRow();
    row << "1"
        << "24681"
        << "0.5"
        << "22221"
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"

        << "";
    row = ws->appendRow();
    row << "1"
        << "24682"
        << "1.5"
        << "22222"
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"

        << "";
    return ws;
  }

public:
  // This pair of boilerplate methods prevent the suite being created
  // statically
  // This means the constructor isn't called when running other tests
  static GenericDataProcessorPresenterTest *createSuite() {
    return new GenericDataProcessorPresenterTest();
  }
  static void destroySuite(GenericDataProcessorPresenterTest *suite) {
    delete suite;
  }

  NiceMock<MockDataProcessorView> m_mockDataProcessorView;
  NiceMock<MockProgressableView> m_mockProgress;
  std::unique_ptr<GenericDataProcessorPresenter> m_presenter;

  void setUp() override {
    setUpDefaultPresenterWithMockViews();
    DefaultValue<QString>::Set(QString());
  }

    void setUpDefaultPresenterWithMockViews() {
      setUpDefaultPresenter();
      injectViews(m_mockDataProcessorView, m_mockProgress);
    }

    void injectViews(DataProcessorView &dataProcessorView,
                     ProgressableView &progressView) {
      m_presenter->acceptViews(&dataProcessorView, &progressView);
    }

    void setUpDefaultPresenter() { m_presenter = makeUniqueDefaultPresenter(); }

  std::unique_ptr<GenericDataProcessorPresenter> makeUniqueDefaultPresenter() {
    return std::make_unique<GenericDataProcessorPresenter>(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
  }

  void tearDown() override {
    DefaultValue<QString>::Clear();
    TS_ASSERT(Mock::VerifyAndClearExpectations(&m_mockDataProcessorView));
    TS_ASSERT(Mock::VerifyAndClearExpectations(&m_mockProgress));
  }

  void setUpPresenterWithCommandProvider(
      std::unique_ptr<DataProcessorTreeManagerFactory> treeManagerFactory,
      std::unique_ptr<CommandProviderFactory> commandProviderFactory) {
    m_presenter = std::make_unique<GenericDataProcessorPresenter>(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor(),
        std::move(treeManagerFactory), std::move(commandProviderFactory));
    injectViews(m_mockDataProcessorView, m_mockProgress);
  }



  void injectParentPresenter(MockMainPresenter &mainPresenter) {
    m_presenter->accept(&mainPresenter);
  }

  void notifyPresenter(DataProcessorPresenter::Flag flag) {
    m_presenter->notify(flag);
  }





  GenericDataProcessorPresenterTest() { FrameworkManager::Instance(); }

  bool workspaceExists(const char *workspaceName) {
    return AnalysisDataService::Instance().doesExist(workspaceName);
  }

  void removeWorkspace(const char *workspaceName) {
    AnalysisDataService::Instance().remove(workspaceName);
  }

  void testConstructor() {
    // We don't the view we will handle yet, so none of the methods below should
    // be called
    EXPECT_CALL(m_mockDataProcessorView, setTableList(_)).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, setOptionsHintStrategy(_, _)).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, addEditActionsProxy()).Times(0);
    // Constructor
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());

    // Verify expectations

    // Check that the presenter updates the whitelist adding columns 'Group' and
    // 'Options'
    auto whitelist = presenter.getWhiteList();
    TS_ASSERT_EQUALS(whitelist.size(), 9);
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(0), "Run(s)");
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(7), "Options");
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(8), "HiddenOptions");
  }

  void testPresenterAcceptsViews() {
    setUpDefaultPresenter();
    // When the presenter accepts the views, expect the following:
    // Expect that the list of actions is published
    EXPECT_CALL(m_mockDataProcessorView, addEditActionsProxy())
        .Times(Exactly(1));
    // Expect that the list of settings is populated
    EXPECT_CALL(m_mockDataProcessorView, loadSettings(_)).Times(Exactly(1));
    // Expect that the list of tables is populated
    EXPECT_CALL(m_mockDataProcessorView, setTableList(_)).Times(Exactly(1));
    // Expect that the layout containing pre-processing, processing and
    // post-processing options is created
    std::vector<QString> stages = {"Pre-process", "Pre-process", "Process",
                                   "Post-process"};
    std::vector<QString> algorithms = {
        "Plus", "CreateTransmissionWorkspaceAuto",
        "ReflectometryReductionOneAuto", "Stitch1DMany"};

    // Expect that the autocompletion hints are populated
    EXPECT_CALL(m_mockDataProcessorView, setOptionsHintStrategy(_, 7))
        .Times(Exactly(1));
    // Now accept the views

    MockProgressableView mockProgress;
    injectViews(m_mockDataProcessorView, mockProgress);
  }

  void testSaveNew() {
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    TS_ASSERT(workspaceExists("TestWorkspace"));
    removeWorkspace("TestWorkspace");
  }

  void testSaveExisting() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(0);
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    removeWorkspace("TestWorkspace");
  }

  void testSaveAs() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // The user hits "save as" but cancels when choosing a name
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return(""));
    notifyPresenter(DataProcessorPresenter::SaveAsFlag);

    // The user hits "save as" and and enters "Workspace" for a name
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("Workspace"));
    notifyPresenter(DataProcessorPresenter::SaveAsFlag);

    TS_ASSERT(workspaceExists("Workspace"));
    ITableWorkspace_sptr ws =
        AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
            "Workspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    TS_ASSERT_EQUALS(ws->columnCount(), 10);

    removeWorkspace("TestWorkspace");
    removeWorkspace("Workspace");
  }

  void testAppendRow() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" twice with no rows selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(5, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(5, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testAppendRowSpecify() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" twice, with the second row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(5, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testAppendRowSpecifyPlural() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);
    rowlist[1].insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" once, with the second, third, and fourth row
    // selected.
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testAppendRowSpecifyGroup() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" once, with the first group selected.
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testAppendGroup() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" once, with the first group selected.
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::AppendGroupFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testAppendGroupSpecifyPlural() {
    createPrefilledWorkspaceThreeGroups("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    std::set<int> grouplist;
    grouplist.insert(0);
    grouplist.insert(1);

    // The user hits "append group" once, with the first and second groups
    // selected.
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    notifyPresenter(DataProcessorPresenter::AppendGroupFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 7);
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "");
    TS_ASSERT_EQUALS(ws->String(5, GroupCol), "2");
    TS_ASSERT_EQUALS(ws->String(6, GroupCol), "2");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testDeleteRowNone() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with no rows selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents()).Times(0);
    notifyPresenter(DataProcessorPresenter::DeleteRowFlag);

    // The user hits save
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table has not lost any rows
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testDeleteRowSingle() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with the second row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents()).Times(0);
    notifyPresenter(DataProcessorPresenter::DeleteRowFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 3);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24682");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testDeleteRowPlural() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);
    rowlist[1].insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with the first three rows selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::DeleteRowFlag);

    // The user hits save
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check the rows were deleted as expected
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 1);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "24682");
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testDeleteGroup() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete group" with no groups selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::DeleteGroupFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testDeleteGroupPlural() {
    createPrefilledWorkspaceThreeGroups("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);
    grouplist.insert(1);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with the second row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    notifyPresenter(DataProcessorPresenter::DeleteGroupFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 2);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "30000");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "30001");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "2");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "2");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testProcess() {
    auto mockCommandProvider =
        std::make_unique<MockDataProcessorCommandProvider>();
    auto constexpr PAUSE_ACTION_INDEX = 12;
    auto constexpr PROCESS_ACTION_INDEX = 23;
    auto constexpr MODIFICATION_ACTION_INDEX_0 = 16;
    auto constexpr MODIFICATION_ACTION_INDEX_1 = 15;

    using CommandIndices =
        typename MockDataProcessorCommandProvider::CommandIndices;
    using CommandVector =
        typename MockDataProcessorCommandProvider::CommandVector;
    
    auto const editCommands = CommandVector();
    auto const tableCommands = CommandVector();

    EXPECT_CALL(*mockCommandProvider, getEditCommands())
        .WillOnce(ReturnRef(editCommands));
    EXPECT_CALL(*mockCommandProvider, getTableCommands())
        .WillOnce(ReturnRef(tableCommands));
    EXPECT_CALL(*mockCommandProvider, getPausingEditCommands())
        .WillRepeatedly(Return(CommandIndices(PAUSE_ACTION_INDEX)));

    EXPECT_CALL(*mockCommandProvider, getProcessingEditCommands())
        .WillRepeatedly(Return(CommandIndices(PROCESS_ACTION_INDEX)));

    EXPECT_CALL(*mockCommandProvider, getModifyingEditCommands())
        .WillRepeatedly(Return(CommandIndices(MODIFICATION_ACTION_INDEX_0,
                                              MODIFICATION_ACTION_INDEX_1)));

    auto mockCommandProviderFactory =
        std::make_unique<MockDataProcessorCommandProviderFactory>();
    ON_CALL(*mockCommandProviderFactory, fromPostprocessorName(_, _))
        .WillByDefault(::testing::Invoke(
            [&mockCommandProvider](QString const &,
                                   GenericDataProcessorPresenter &)
                -> std::unique_ptr<DataProcessorCommandProvider> {
              return std::move(mockCommandProvider);
            }));

    auto treeManagerFactory =
        std::make_unique<GenericDataProcessorTreeManagerFactory>();

    setUpPresenterWithCommandProvider(std::move(treeManagerFactory),
                                      std::move(mockCommandProviderFactory));
    std::array<int, 10> x{};
    for(auto y : x) {
      std::cout << y << std::endl;
    }

    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected

    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .WillOnce(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .WillOnce(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .WillOnce(Return(QString("Params = \"0.1\"")));

    // Expect disables modification controls, enables pause and disables
    // processing.
    EXPECT_CALL(m_mockDataProcessorView, enableAction(PAUSE_ACTION_INDEX));

    EXPECT_CALL(m_mockDataProcessorView, disableProcessButton());
    EXPECT_CALL(m_mockDataProcessorView, disableSelectionAndEditing());
    EXPECT_CALL(m_mockDataProcessorView, disableAction(PROCESS_ACTION_INDEX));
    EXPECT_CALL(m_mockDataProcessorView,
                disableAction(MODIFICATION_ACTION_INDEX_0));
    EXPECT_CALL(m_mockDataProcessorView,
                disableAction(MODIFICATION_ACTION_INDEX_1));

    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .WillOnce(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    notifyPresenter(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12345"));
    TS_ASSERT(workspaceExists("TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12346"));
    TS_ASSERT(workspaceExists("TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("TOF_12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("TOF_12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockCommandProvider));
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockCommandProviderFactory));
  }

  void testTreeUpdatedAfterProcess() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    auto ws = createPrefilledWorkspace("TestWorkspace");
    ws->String(0, ThetaCol) = "";
    ws->String(1, ScaleCol) = "";
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    notifyPresenter(DataProcessorPresenter::ProcessFlag);
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT(ws->String(0, ThetaCol) != "");
    TS_ASSERT(ws->String(1, ScaleCol) != "");

    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12345"));
    TS_ASSERT(workspaceExists("TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12346"));
    TS_ASSERT(workspaceExists("TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("TOF_12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("TOF_12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testTreeUpdatedAfterProcessMultiPeriod() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    auto ws = createPrefilledWorkspace("TestWorkspace");
    ws->String(0, ThetaCol) = "";
    ws->String(0, ScaleCol) = "";
    ws->String(1, ThetaCol) = "";
    ws->String(1, ScaleCol) = "";
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    createMultiPeriodTOFWorkspace("TOF_12345", "12345");
    createMultiPeriodTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    notifyPresenter(DataProcessorPresenter::ProcessFlag);
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(0, ThetaCol), "22.5");
    TS_ASSERT_EQUALS(ws->String(0, ScaleCol), "1");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "22.5");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "1");

    // Check output workspaces were created as expected
    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12345"));
    TS_ASSERT(workspaceExists("TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12346"));
    TS_ASSERT(workspaceExists("TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    AnalysisDataService::Instance().clear();

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testProcessOnlyRowsSelected() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first two rows
    // selected
    // This means we will process the selected rows but we will not
    // post-process them
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    //  EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    //  EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    notifyPresenter(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12345"));
    TS_ASSERT(workspaceExists("TOF_12345"));
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12346"));
    TS_ASSERT(workspaceExists("TOF_12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("TOF_12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("TOF_12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testProcessWithNotebook() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillRepeatedly(Return("Params = \"0.1\""));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(1);
    notifyPresenter(DataProcessorPresenter::ProcessFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("TOF_12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("TOF_12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testExpandAllGroups() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockMainPresenter, giveUserCritical(_, _)).Times(0);

    // The user hits the 'Expand All' button
    EXPECT_CALL(m_mockDataProcessorView, expandAll()).Times(1);

    notifyPresenter(DataProcessorPresenter::ExpandAllGroupsFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testCollapseAllGroups() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockMainPresenter, giveUserCritical(_, _)).Times(0);

    // The user hits the 'Expand All' button
    EXPECT_CALL(m_mockDataProcessorView, collapseAll()).Times(1);

    notifyPresenter(DataProcessorPresenter::CollapseAllGroupsFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testSelectAll() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockMainPresenter, giveUserCritical(_, _)).Times(0);

    // Select all rows / groups
    EXPECT_CALL(m_mockDataProcessorView, selectAll()).Times(1);

    notifyPresenter(DataProcessorPresenter::SelectAllFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  /*
  * Test processing workspaces with non-standard names, with
  * and without run_number information in the sample log.
  */
  void testProcessCustomNames() {
    setUpDefaultPresenterWithMockViews();
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    auto ws = createWorkspace("TestWorkspace");
    TableRow row = ws->appendRow();
    row << "1"
        << "dataA"
        << "0.7"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";
    row = ws->appendRow();
    row << "1"
        << "dataB"
        << "2.3"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1"
        << "ProcessingInstructions='0'";

    createTOFWorkspace("dataA");
    createTOFWorkspace("dataB");

    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);

    notifyPresenter(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_dataA"));
    TS_ASSERT(workspaceExists("IvsQ_binned_TOF_dataB"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_dataA"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_dataB"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_dataA"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_dataB"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_dataA_TOF_dataB"));

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("dataA");
    removeWorkspace("dataB");
    removeWorkspace("IvsQ_binned_TOF_dataA");
    removeWorkspace("IvsQ_binned_TOF_dataB");
    removeWorkspace("IvsQ_TOF_dataA");
    removeWorkspace("IvsQ_TOF_dataB");
    removeWorkspace("IvsLam_TOF_dataA");
    removeWorkspace("IvsLam_TOF_dataB");
    removeWorkspace("IvsQ_TOF_dataA_TOF_dataB");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testBadWorkspaceType() {
    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();

    // Wrong types
    ws->addColumn("int", "StitchGroup");
    ws->addColumn("str", "Run(s)");
    ws->addColumn("str", "ThetaIn");
    ws->addColumn("str", "TransRun(s)");
    ws->addColumn("str", "Qmin");
    ws->addColumn("str", "Qmax");
    ws->addColumn("str", "dq/q");
    ws->addColumn("str", "Scale");
    ws->addColumn("str", "Options");

    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    // We should receive an error
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(1);

    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    removeWorkspace("TestWorkspace");
  }

  void testBadWorkspaceLength() {
    // Because we to open twice, get an error twice
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(2);
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(2)
        .WillRepeatedly(Return("TestWorkspace"));

    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();
    ws->addColumn("str", "StitchGroup");
    ws->addColumn("str", "Run(s)");
    ws->addColumn("str", "ThetaIn");
    ws->addColumn("str", "TransRun(s)");
    ws->addColumn("str", "Qmin");
    ws->addColumn("str", "Qmax");
    ws->addColumn("str", "dq/q");
    ws->addColumn("str", "Scale");
    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    // Try to open with too few columns
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    ws->addColumn("str", "OptionsA");
    ws->addColumn("str", "OptionsB");
    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    // Try to open with too many columns
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    removeWorkspace("TestWorkspace");
  }

  void testPromptSaveAfterAppendRow() {
    // User hits "append row"
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user will decide not to discard their changes
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    // The user saves
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("Workspace"));
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    removeWorkspace("Workspace");
  }

  void testPromptSaveAfterAppendGroup() {
    // User hits "append group"
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendGroupFlag);

    // The user will decide not to discard their changes
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    // The user saves
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("Workspace"));
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    removeWorkspace("Workspace");
  }

  void testPromptSaveAfterDeleteRow() {
    // User hits "append row" a couple of times
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // The user saves
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("Workspace"));
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    //...then deletes the 2nd row
    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::DeleteRowFlag);

    // The user will decide not to discard their changes when asked
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    // The user saves
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    removeWorkspace("Workspace");
  }

  void testPromptSaveAfterDeleteGroup() {
    // User hits "append group" a couple of times
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendGroupFlag);
    notifyPresenter(DataProcessorPresenter::AppendGroupFlag);

    // The user saves
    EXPECT_CALL(m_mockDataProcessorView,
                askUserString(_, _, QString("Workspace")))
        .Times(1)
        .WillOnce(Return("Workspace"));
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    //...then deletes the 2nd row
    std::set<int> grouplist;
    grouplist.insert(1);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::DeleteGroupFlag);

    // The user will decide not to discard their changes when asked
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    // The user saves
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    removeWorkspace("Workspace");
  }

  void testPromptSaveAndDiscard() {
    // User hits "append row" a couple of times
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // Then hits "new table", and decides to discard
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(true));
    notifyPresenter(DataProcessorPresenter::NewTableFlag);

    // These next two times they don't get prompted - they have a new table
    notifyPresenter(DataProcessorPresenter::NewTableFlag);
    notifyPresenter(DataProcessorPresenter::NewTableFlag);
  }

  void testPromptSaveOnOpen() {
    createPrefilledWorkspace("TestWorkspace");

    // User hits "append row"
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::TableUpdatedFlag);
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);

    // and tries to open a workspace, but gets prompted and decides not to
    // discard
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // the user does it again, but discards
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // the user does it one more time, and is not prompted
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    EXPECT_CALL(m_mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);
  }

  void testExpandSelection() {
    auto ws = createWorkspace("TestWorkspace");
    TableRow row = ws->appendRow();
    row << "0"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 0
    row = ws->appendRow();
    row << "1"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 1
    row = ws->appendRow();
    row << "1"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 2
    row = ws->appendRow();
    row << "2"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 3
    row = ws->appendRow();
    row << "2"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 4
    row = ws->appendRow();
    row << "2"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 5
    row = ws->appendRow();
    row << "3"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 6
    row = ws->appendRow();
    row << "4"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 7
    row = ws->appendRow();
    row << "4"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 8
    row = ws->appendRow();
    row << "5"
        << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"

        << ""; // Row 9

    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    std::map<int, std::set<int>> selection;
    std::set<int> expected;

    selection[0].insert(0);
    expected.insert(0);

    // With row 0 selected, we shouldn't expand at all
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::ExpandSelectionFlag);

    // With 0,1 selected, we should finish with groups 0,1 selected
    selection.clear();
    selection[0].insert(0);
    selection[1].insert(0);

    expected.clear();
    expected.insert(0);
    expected.insert(1);

    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::ExpandSelectionFlag);

    // With 1,6 selected, we should finish with groups 1,3 selected
    selection.clear();
    selection[1].insert(0);
    selection[3].insert(0);

    expected.clear();
    expected.insert(1);
    expected.insert(3);

    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::ExpandSelectionFlag);

    // With 4,8 selected, we should finish with groups 2,4 selected
    selection.clear();
    selection[2].insert(1);
    selection[4].insert(2);

    expected.clear();
    expected.insert(2);
    expected.insert(4);

    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::ExpandSelectionFlag);

    // With nothing selected, we should finish with nothing selected
    selection.clear();
    expected.clear();

    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, setSelection(_)).Times(0);
    notifyPresenter(DataProcessorPresenter::ExpandSelectionFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testGroupRows() {
    auto ws = createWorkspace("TestWorkspace");
    TableRow row = ws->appendRow();
    row << "0"
        << "0"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 0
    row = ws->appendRow();
    row << "0"
        << "1"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 1
    row = ws->appendRow();
    row << "0"
        << "2"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 2
    row = ws->appendRow();
    row << "0"
        << "3"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 3

    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> selection;
    selection[0].insert(0);
    selection[0].insert(1);

    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::GroupRowsFlag);
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "");
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "2");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "3");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "0");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "1");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testGroupRowsNothingSelected() {
    auto ws = createWorkspace("TestWorkspace");
    TableRow row = ws->appendRow();
    row << "0"
        << "0"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 0
    row = ws->appendRow();
    row << "0"
        << "1"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 1
    row = ws->appendRow();
    row << "0"
        << "2"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 2
    row = ws->appendRow();
    row << "0"
        << "3"
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1"
        << ""; // Row 3

    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents()).Times(0);
    notifyPresenter(DataProcessorPresenter::GroupRowsFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testClearRows() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);
    rowlist[1].insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "clear selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::ClearSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the group ids have been set correctly
    TS_ASSERT_EQUALS(ws->String(0, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(3, GroupCol), "1");

    // Make sure the selected rows are clear
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "");
    TS_ASSERT_EQUALS(ws->String(2, ThetaCol), "");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "");
    TS_ASSERT_EQUALS(ws->String(2, TransCol), "");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "");
    TS_ASSERT_EQUALS(ws->String(2, QMinCol), "");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "");
    TS_ASSERT_EQUALS(ws->String(2, QMaxCol), "");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "");
    TS_ASSERT_EQUALS(ws->String(2, DQQCol), "");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "");
    TS_ASSERT_EQUALS(ws->String(2, ScaleCol), "");

    // Tidy up
    removeWorkspace("TestWorkspace");
  }

  void testCopyRow() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);

    const auto expected = QString(
        "0\t12346\t1.5\t\t1.4\t2.9\t0.04\t1\tProcessingInstructions='0'\t");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::CopySelectedFlag);
  }

  void testCopyEmptySelection() {
    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, setClipboard(QString())).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    notifyPresenter(DataProcessorPresenter::CopySelectedFlag);
  }

  void testCopyRows() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);
    rowlist[1].insert(0);
    rowlist[1].insert(1);

    const auto expected = QString(
        "0\t12345\t0.5\t\t0.1\t1.6\t0.04\t1\tProcessingInstructions='0'\t\n"
        "0\t12346\t1.5\t\t1.4\t2.9\t0.04\t1\tProcessingInstructions='0'\t\n"
        "1\t24681\t0.5\t\t0.1\t1.6\t0.04\t1\t\t\n"
        "1\t24682\t1.5\t\t1.4\t2.9\t0.04\t1\t\t");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::CopySelectedFlag);
  }

  void testCutRow() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);

    const auto expected = QString(
        "0\t12346\t1.5\t\t1.4\t2.9\t0.04\t1\tProcessingInstructions='0'\t");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::CutSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 3);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24682");
  }

  void testCutRows() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);
    rowlist[1].insert(0);

    const auto expected = QString(
        "0\t12345\t0.5\t\t0.1\t1.6\t0.04\t1\tProcessingInstructions='0'\t\n"
        "0\t12346\t1.5\t\t1.4\t2.9\t0.04\t1\tProcessingInstructions='0'\t\n"
        "1\t24681\t0.5\t\t0.1\t1.6\t0.04\t1\t\t");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::CutSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 1);
    // Check the only unselected row is left behind
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "24682");
  }

  void testPasteRow() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);

    const auto clipboard =
        QString("6\t123\t0.5\t456\t1.2\t3.4\t3.14\t5\tabc\tdef");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->String(1, OptionsCol), "abc");
    TS_ASSERT_EQUALS(ws->String(1, HiddenOptionsCol), "def");

    // Row is going to be pasted into the group where row in clipboard
    // belongs, i.e. group 0
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
  }

  void testPasteNewRow() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    const auto clipboard =
        QString("1\t123\t0.5\t456\t1.2\t3.4\t3.14\t5\tabc\tdef");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    notifyPresenter(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(4, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(4, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(4, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(4, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(4, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(4, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, OptionsCol), "abc");
    TS_ASSERT_EQUALS(ws->String(4, HiddenOptionsCol), "def");
  }

  void testPasteRows() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(1);
    rowlist[1].insert(0);

    const auto clipboard =
        QString("6\t123\t0.5\t456\t1.2\t3.4\t3.14\t5\tabc\tdef\n"
                "2\t345\t2.7\t123\t2.1\t4.3\t2.17\t3\tdef\tabc");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    notifyPresenter(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->String(1, GroupCol), "0");
    TS_ASSERT_EQUALS(ws->String(1, OptionsCol), "abc");
    TS_ASSERT_EQUALS(ws->String(1, HiddenOptionsCol), "def");

    TS_ASSERT_EQUALS(ws->String(2, RunCol), "345");
    TS_ASSERT_EQUALS(ws->String(2, ThetaCol), "2.7");
    TS_ASSERT_EQUALS(ws->String(2, TransCol), "123");
    TS_ASSERT_EQUALS(ws->String(2, QMinCol), "2.1");
    TS_ASSERT_EQUALS(ws->String(2, QMaxCol), "4.3");
    TS_ASSERT_EQUALS(ws->String(2, DQQCol), "2.17");
    TS_ASSERT_EQUALS(ws->String(2, ScaleCol), "3");
    TS_ASSERT_EQUALS(ws->String(2, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(2, OptionsCol), "def");
    TS_ASSERT_EQUALS(ws->String(2, HiddenOptionsCol), "abc");
  }

  void testPasteNewRows() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    const auto clipboard =
        QString("1\t123\t0.5\t456\t1.2\t3.4\t3.14\t5\tabc\tzzz\n"
                "1\t345\t2.7\t123\t2.1\t4.3\t2.17\t3\tdef\tyyy");

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    notifyPresenter(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    notifyPresenter(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(4, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(4, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(4, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(4, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(4, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(4, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->String(4, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(4, OptionsCol), "abc");
    TS_ASSERT_EQUALS(ws->String(4, HiddenOptionsCol), "zzz");

    TS_ASSERT_EQUALS(ws->String(5, RunCol), "345");
    TS_ASSERT_EQUALS(ws->String(5, ThetaCol), "2.7");
    TS_ASSERT_EQUALS(ws->String(5, TransCol), "123");
    TS_ASSERT_EQUALS(ws->String(5, QMinCol), "2.1");
    TS_ASSERT_EQUALS(ws->String(5, QMaxCol), "4.3");
    TS_ASSERT_EQUALS(ws->String(5, DQQCol), "2.17");
    TS_ASSERT_EQUALS(ws->String(5, ScaleCol), "3");
    TS_ASSERT_EQUALS(ws->String(5, GroupCol), "1");
    TS_ASSERT_EQUALS(ws->String(5, OptionsCol), "def");
    TS_ASSERT_EQUALS(ws->String(5, HiddenOptionsCol), "yyy");
  }

  void testPasteEmptyClipboard() {
    // Empty clipboard
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren()).Times(0);
    notifyPresenter(DataProcessorPresenter::PasteSelectedFlag);
  }

  void testPasteToNonexistentGroup() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    // Empty clipboard
    EXPECT_CALL(m_mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return("1\t123\t0.5\t456\t1.2\t3.4\t3.14\t5\tabc\t"));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillOnce(Return(std::map<int, std::set<int>>()));
    TS_ASSERT_THROWS_NOTHING(
        notifyPresenter(DataProcessorPresenter::PasteSelectedFlag));
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testImportTable() {
    EXPECT_CALL(
        m_mockDataProcessorView,
        runPythonAlgorithm(QString("try:\n  algm = LoadTBLDialog()\n  print("
                                   "algm.getPropertyValue(\"OutputWorkspace\"))"
                                   "\nexcept:\n  pass\n")));
    notifyPresenter(DataProcessorPresenter::ImportTableFlag);
  }

  void testExportTable() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);
    EXPECT_CALL(m_mockDataProcessorView,
                runPythonAlgorithm(QString(
                    "try:\n  algm = SaveTBLDialog()\nexcept:\n  pass\n")));
    notifyPresenter(DataProcessorPresenter::ExportTableFlag);
  }

  void testPlotRowWarn() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    createPrefilledWorkspace("TestWorkspace");
    createTOFWorkspace("TOF_12345", "12345");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));

    // We should be warned
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);

    // We should be warned
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _));
    // The user hits "plot rows" with the first row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    notifyPresenter(DataProcessorPresenter::PlotRowFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("TOF_12345");
  }

  void testPlotEmptyRow() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _));
    // Append an empty row to our table
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    // Attempt to plot the empty row (should result in critical warning)
    notifyPresenter(DataProcessorPresenter::PlotRowFlag);
  }

  void testPlotGroupWithEmptyRow() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    createPrefilledWorkspace("TestWorkspace");
    createTOFWorkspace("TOF_12345", "12345");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);
    std::set<int> grouplist;
    grouplist.insert(0);
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(2)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _));
    // Open up our table with one row
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);
    // Append an empty row to the table
    notifyPresenter(DataProcessorPresenter::AppendRowFlag);
    // Attempt to plot the group (should result in critical warning)
    notifyPresenter(DataProcessorPresenter::PlotGroupFlag);
    removeWorkspace("TestWorkspace");
    removeWorkspace("TOF_12345");
  }

  void testPlotGroupWarn() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    createPrefilledWorkspace("TestWorkspace");
    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    // We should be warned
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _));
    // The user hits "plot groups" with the first row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    notifyPresenter(DataProcessorPresenter::PlotGroupFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("TOF_12345");
    removeWorkspace("TOF_12346");
  }

  void testWorkspaceNamesNoTrans() {
    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");

    QStringList row0 = {"12345", "0.5", "", "0.1", "0.3", "0.04", "1", "", ""};
    QStringList row1 = {"12346", "0.5", "", "0.1", "0.3", "0.04", "1", "", ""};
    std::map<int, QStringList> group = {{0, row0}, {1, row1}};

    // Test the names of the reduced workspaces
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row0, "prefix_1_"),
                     "prefix_1_TOF_12345");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row1, "prefix_2_"),
                     "prefix_2_TOF_12346");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row0), "TOF_12345");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row1), "TOF_12346");
    // Test the names of the post-processed ws
    TS_ASSERT_EQUALS(
        m_presenter->getPostprocessedWorkspaceName(group, "new_prefix_"),
        "new_prefix_TOF_12345_TOF_12346");
    TS_ASSERT_EQUALS(m_presenter->getPostprocessedWorkspaceName(group),
                     "TOF_12345_TOF_12346");
  }

  void testWorkspaceNamesWithTrans() {
    createPrefilledWorkspaceWithTrans("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");

    QStringList row0 = {"12345", "0.5", "11115", "0.1", "0.3",
                        "0.04",  "1",   "",      ""};
    QStringList row1 = {"12346", "0.5", "11116", "0.1", "0.3",
                        "0.04",  "1",   "",      ""};
    std::map<int, QStringList> group = {{0, row0}, {1, row1}};

    // Test the names of the reduced workspaces
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row0, "prefix_1_"),
                     "prefix_1_TOF_12345_TRANS_11115");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row1, "prefix_2_"),
                     "prefix_2_TOF_12346_TRANS_11116");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row0),
                     "TOF_12345_TRANS_11115");
    TS_ASSERT_EQUALS(m_presenter->getReducedWorkspaceName(row1),
                     "TOF_12346_TRANS_11116");
    // Test the names of the post-processed ws
    TS_ASSERT_EQUALS(
        m_presenter->getPostprocessedWorkspaceName(group, "new_prefix_"),
        "new_prefix_TOF_12345_TRANS_11115_TOF_12346_TRANS_11116");
    TS_ASSERT_EQUALS(m_presenter->getPostprocessedWorkspaceName(group),
                     "TOF_12345_TRANS_11115_TOF_12346_TRANS_11116");
  }

  void testWorkspaceNameWrongData() {
    createPrefilledWorkspaceWithTrans("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");

    QStringList row0 = {"12345", "0.5"};
    QStringList row1 = {"12346", "0.5"};
    std::map<int, QStringList> group = {{0, row0}, {1, row1}};

    // Test the names of the reduced workspaces
    TS_ASSERT_THROWS_ANYTHING(m_presenter->getReducedWorkspaceName(row0));
    TS_ASSERT_THROWS_ANYTHING(
        m_presenter->getPostprocessedWorkspaceName(group));
  }

  /// Tests the reduction when no pre-processing algorithms are given

  void testProcessNoPreProcessing() {
    NiceMock<MockMainPresenter> mockMainPresenter;

    // We don't know the view we will handle yet, so none of the methods below
    // should be called
    EXPECT_CALL(m_mockDataProcessorView, setTableList(_)).Times(0);
    EXPECT_CALL(m_mockDataProcessorView, setOptionsHintStrategy(_, _)).Times(0);
    // Constructor (no pre-processing)
    GenericDataProcessorPresenterNoThread presenter(
        createReflectometryWhiteList(), createReflectometryProcessor(),
        createReflectometryPostprocessor());
    // Verify expectations
    TS_ASSERT(Mock::VerifyAndClearExpectations(&m_mockDataProcessorView));

    // Check that the presenter has updated the whitelist adding columns 'Group'
    // and 'Options'
    auto whitelist = presenter.getWhiteList();
    TS_ASSERT_EQUALS(whitelist.size(), 9);
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(0), "Run(s)");
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(7), "Options");

    // When the presenter accepts the views, expect the following:
    // Expect that the list of settings is populated
    EXPECT_CALL(m_mockDataProcessorView, loadSettings(_)).Times(Exactly(1));
    // Expect that the list of tables is populated
    EXPECT_CALL(m_mockDataProcessorView, setTableList(_)).Times(Exactly(1));
    // Expect that the autocompletion hints are populated
    EXPECT_CALL(m_mockDataProcessorView, setOptionsHintStrategy(_, 7))
        .Times(Exactly(1));
    // Now accept the views
    presenter.acceptViews(&m_mockDataProcessorView, &m_mockProgress);
    presenter.accept(&mockMainPresenter);

    // Verify expectations
    TS_ASSERT(Mock::VerifyAndClearExpectations(&m_mockDataProcessorView));

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> grouplist;
    grouplist.insert(0);

    createTOFWorkspace("12345", "12345");
    createTOFWorkspace("12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(""));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    presenter.notify(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12345"));
    TS_ASSERT(workspaceExists("12345"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12346"));
    TS_ASSERT(workspaceExists("IvsLam_TOF_12346"));
    TS_ASSERT(workspaceExists("12346"));
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("12345");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testPlotRowPythonCode() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);
    createTOFWorkspace("IvsQ_binned_TOF_12345", "12345");
    createTOFWorkspace("IvsQ_binned_TOF_12346", "12346");

    std::map<int, std::set<int>> rowlist;
    rowlist[0].insert(0);
    rowlist[0].insert(1);

    // We should be warned
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _)).Times(0);
    // The user hits "plot rows" with the first row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));

    auto pythonCode = QString(
        "base_graph = None\nbase_graph = "
        "plotSpectrum(\"IvsQ_binned_TOF_12345\", 0, True, window = "
        "base_graph)\nbase_graph = plotSpectrum(\"IvsQ_binned_TOF_12346\", 0, "
        "True, window = base_graph)\nbase_graph.activeLayer().logLogAxes()\n");

    EXPECT_CALL(m_mockDataProcessorView, runPythonAlgorithm(pythonCode))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::PlotRowFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
  }

  void testPlotGroupPythonCode() {
    MockProgressableView mockProgress;
    setUpDefaultPresenter();
    injectViews(m_mockDataProcessorView, mockProgress);

    createPrefilledWorkspace("TestWorkspace");
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    notifyPresenter(DataProcessorPresenter::OpenTableFlag);
    createTOFWorkspace("IvsQ_TOF_12345_TOF_12346");

    std::set<int> group = {0};

    // We should be warned
    EXPECT_CALL(m_mockDataProcessorView, giveUserWarning(_, _)).Times(0);
    // The user hits "plot rows" with the first row selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(group));

    auto pythonCode =
        QString("base_graph = None\nbase_graph = "
                "plotSpectrum(\"IvsQ_TOF_12345_TOF_12346\", 0, True, window = "
                "base_graph)\nbase_graph.activeLayer().logLogAxes()\n");

    EXPECT_CALL(m_mockDataProcessorView, runPythonAlgorithm(pythonCode))
        .Times(1);
    notifyPresenter(DataProcessorPresenter::PlotGroupFlag);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");
  }

  void testNoPostProcessing() {
    // Test very basic functionality of the presenter when no post-processing
    // algorithm is defined

    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(createReflectometryWhiteList(),
                                            createReflectometryProcessor());
    presenter.acceptViews(&m_mockDataProcessorView, &mockProgress);

    // Calls that should throw
    TS_ASSERT_THROWS_ANYTHING(
        presenter.notify(DataProcessorPresenter::AppendGroupFlag));
    TS_ASSERT_THROWS_ANYTHING(
        presenter.notify(DataProcessorPresenter::DeleteGroupFlag));
    TS_ASSERT_THROWS_ANYTHING(
        presenter.notify(DataProcessorPresenter::GroupRowsFlag));
    TS_ASSERT_THROWS_ANYTHING(
        presenter.notify(DataProcessorPresenter::ExpandSelectionFlag));
    TS_ASSERT_THROWS_ANYTHING(
        presenter.notify(DataProcessorPresenter::PlotGroupFlag));
    TS_ASSERT(presenter.getPostprocessedWorkspaceName(
                  std::map<int, QStringList>()) == "");
  }

  void testPostprocessMap() {
    NiceMock<MockMainPresenter> mockMainPresenter;

    std::map<QString, QString> postprocesssMap = {{"dQ/Q", "Params"}};
    GenericDataProcessorPresenterNoThread presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor(),
        postprocesssMap);
    presenter.acceptViews(&m_mockDataProcessorView, &m_mockProgress);
    presenter.accept(&mockMainPresenter);

    // Open a table
    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(m_mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    createTOFWorkspace("12345", "12345");
    createTOFWorkspace("12346", "12346");

    std::set<int> grouplist;
    grouplist.insert(0);

    // We should not receive any errors
    EXPECT_CALL(m_mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first group selected
    EXPECT_CALL(m_mockDataProcessorView, getSelectedChildren())
        .Times(1)
        .WillRepeatedly(Return(std::map<int, std::set<int>>()));
    EXPECT_CALL(m_mockDataProcessorView, getSelectedParents())
        .Times(1)
        .WillRepeatedly(Return(grouplist));
    EXPECT_CALL(mockMainPresenter, getPreprocessingOptionsAsString())
        .Times(1)
        .WillOnce(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getPreprocessingProperties())
        .Times(2)
        .WillRepeatedly(Return(QString()));
    EXPECT_CALL(mockMainPresenter, getProcessingOptions())
        .Times(1)
        .WillOnce(Return(QString("")));
    EXPECT_CALL(mockMainPresenter, getPostprocessingOptions())
        .Times(1)
        .WillOnce(Return("Params='-0.10'"));
    // EXPECT_CALL(m_mockDataProcessorView, resumed()).Times(1);
    // EXPECT_CALL(mockMainPresenter, resume()).Times(1);
    EXPECT_CALL(m_mockDataProcessorView, isNotebookEnabled())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(m_mockDataProcessorView, requestNotebookPath()).Times(0);

    presenter.notify(DataProcessorPresenter::ProcessFlag);

    // Check output workspace was stitched with params = '-0.04'
    TS_ASSERT(workspaceExists("IvsQ_TOF_12345_TOF_12346"));

    MatrixWorkspace_sptr out =
        AnalysisDataService::Instance().retrieveWS<MatrixWorkspace>(
            "IvsQ_TOF_12345_TOF_12346");
    TSM_ASSERT_DELTA(
        "Logarithmic rebinning should have been applied, with param 0.04",
        out->x(0)[0], 0.100, 1e-5);
    TSM_ASSERT_DELTA(
        "Logarithmic rebinning should have been applied, with param 0.04",
        out->x(0)[1], 0.104, 1e-5);
    TSM_ASSERT_DELTA(
        "Logarithmic rebinning should have been applied, with param 0.04",
        out->x(0)[2], 0.10816, 1e-5);
    TSM_ASSERT_DELTA(
        "Logarithmic rebinning should have been applied, with param 0.04",
        out->x(0)[3], 0.11248, 1e-5);

    // Tidy up
    removeWorkspace("TestWorkspace");
    removeWorkspace("IvsQ_binned_TOF_12345");
    removeWorkspace("IvsQ_TOF_12345");
    removeWorkspace("IvsLam_TOF_12345");
    removeWorkspace("12345");
    removeWorkspace("IvsQ_binned_TOF_12346");
    removeWorkspace("IvsQ_TOF_12346");
    removeWorkspace("IvsLam_TOF_12346");
    removeWorkspace("12346");
    removeWorkspace("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testPauseReduction() {
    NiceMock<MockMainPresenter> mockMainPresenter;
    injectParentPresenter(mockMainPresenter);

    // User hits the 'pause' button
    EXPECT_CALL(mockMainPresenter, pause()).Times(1);
    // EXPECT_CALL(m_mockDataProcessorView, disableAction(1));
    // Expect the pause buttons are disabled.

    notifyPresenter(DataProcessorPresenter::PauseFlag);

    // When processing first group, it should confirm reduction has been paused
    EXPECT_CALL(mockMainPresenter, confirmReductionPaused()).Times(1);
    // EXPECT_CALL(m_mockDataProcessorView, reductionPaused()).Times(1);

    // Expect pause button re-enabled, process button enabled and
    // table modification re-enabled.

    m_presenter->callNextGroup();

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockMainPresenter));
  }

  void testInstrumentList() {
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(createReflectometryWhiteList(),
                                            createReflectometryProcessor());
    presenter.acceptViews(&m_mockDataProcessorView, &mockProgress);

    EXPECT_CALL(m_mockDataProcessorView,
                setInstrumentList(
                    QString::fromStdString("INTER,SURF,POLREF,OFFSPEC,CRISP"),
                    QString::fromStdString("INTER")))
        .Times(1);
    presenter.setInstrumentList(
        QStringList{"INTER", "SURF", "POLREF", "OFFSPEC", "CRISP"}, "INTER");
  }
};

#endif /* MANTID_MANTIDWIDGETS_GENERICDATAPROCESSORPRESENTERTEST_H */
