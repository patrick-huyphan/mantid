#ifndef MANTID_MANTIDWIDGETS_DATAPROCESSORPROCESSINGALGORITHMTEST_H
#define MANTID_MANTIDWIDGETS_DATAPROCESSORPROCESSINGALGORITHMTEST_H

#include <cxxtest/TestSuite.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <QStringList>

#include "MantidAPI/FrameworkManager.h"
#include "MantidQtWidgets/Common/DataProcessorUI/ProcessingAlgorithm.h"

using namespace MantidQt::MantidWidgets;
using namespace MantidQt::MantidWidgets::DataProcessor;
using namespace Mantid::API;
using namespace testing;

//=====================================================================================
// Functional tests
//=====================================================================================
class ProcessingAlgorithmTest : public CxxTest::TestSuite {

private:
public:
  // This pair of boilerplate methods prevent the suite being created statically
  // This means the constructor isn't called when running other tests
  static ProcessingAlgorithmTest *createSuite() {
    return new ProcessingAlgorithmTest();
  }
  static void destroySuite(ProcessingAlgorithmTest *suite) { delete suite; }
  ProcessingAlgorithmTest() { FrameworkManager::Instance(); };

  void test_valid_algorithms() {
    // Any algorithm with at least one input ws property and one output ws
    // property is valid
    // Currently ws must be either MatrixWorkspace or Workspace but this can be
    // changed
    std::vector<QString> prefixes = {"run_"};
    std::vector<QString> suffixes = {""};
    TS_ASSERT_THROWS_NOTHING(ProcessingAlgorithm("Rebin", prefixes, suffixes));
    TS_ASSERT_THROWS_NOTHING(ProcessingAlgorithm("ExtractSpectra", prefixes, suffixes));
    TS_ASSERT_THROWS_NOTHING(ProcessingAlgorithm("ConvertUnits", prefixes, suffixes));
  }

  void test_invalid_algorithms() {
    std::vector<QString> prefixes = {"IvsQ_"};
    std::vector<QString> suffixes = {""};

    // Algorithms with no input workspace properties
    TS_ASSERT_THROWS(ProcessingAlgorithm("Stitch1DMany", prefixes, suffixes),
                     std::invalid_argument);
    // Algorithms with no output workspace properties
    TS_ASSERT_THROWS(ProcessingAlgorithm("SaveAscii", prefixes, suffixes),
                     std::invalid_argument);
  }
  void test_ReflectometryReductionOneAuto() {

    QString algName = "ReflectometryReductionOneAuto";

    // ReflectometryReductionOneAuto has three output ws properties
    // We should provide three prefixes and suffixes, one for each ws
    std::vector<QString> prefixes = {"IvsQ_binned_"};
    std::vector<QString> suffixes = {"_binned"};
    TS_ASSERT_THROWS(
        ProcessingAlgorithm(algName, prefixes, suffixes, std::set<QString>()),
        std::invalid_argument);

    prefixes.push_back("IvsQ_");
    suffixes.push_back("_test");
    // This should also throw
    TS_ASSERT_THROWS(
        ProcessingAlgorithm(algName, prefixes, suffixes, std::set<QString>()),
        std::invalid_argument);
    // But this should be OK
    prefixes.push_back("IvsLam_");
    suffixes.push_back("_suffix");
    TS_ASSERT_THROWS_NOTHING(
        ProcessingAlgorithm(algName, prefixes, suffixes, std::set<QString>()));

    auto alg = ProcessingAlgorithm(algName, prefixes, suffixes, std::set<QString>());
    TS_ASSERT_EQUALS(alg.name(), "ReflectometryReductionOneAuto");
    TS_ASSERT_EQUALS(alg.numberOfOutputProperties(), 3);
    TS_ASSERT_EQUALS(alg.prefix(0), "IvsQ_binned_");
    TS_ASSERT_EQUALS(alg.prefix(1), "IvsQ_");
    TS_ASSERT_EQUALS(alg.prefix(2), "IvsLam_");
    TS_ASSERT_EQUALS(alg.suffix(0), "_binned");
    TS_ASSERT_EQUALS(alg.suffix(1), "_test");
    TS_ASSERT_EQUALS(alg.suffix(2), "_suffix");
    TS_ASSERT_EQUALS(alg.inputPropertyName(0), "InputWorkspace");
    TS_ASSERT_EQUALS(alg.inputPropertyName(1), "FirstTransmissionRun");
    TS_ASSERT_EQUALS(alg.inputPropertyName(2), "SecondTransmissionRun");
    TS_ASSERT_EQUALS(alg.outputPropertyName(0), "OutputWorkspaceBinned");
    TS_ASSERT_EQUALS(alg.outputPropertyName(1), "OutputWorkspace");
    TS_ASSERT_EQUALS(alg.outputPropertyName(2), "OutputWorkspaceWavelength");
  }

  // Add more tests for specific algorithms here
};
#endif /* MANTID_MANTIDWIDGETS_DATAPROCESSORPROCESSINGALGORITHMTEST_H */
