#ifndef MANTID_ALGORITHMS_CALCULATEPOLYNOMIALBACKGROUNDTEST_H_
#define MANTID_ALGORITHMS_CALCULATEPOLYNOMIALBACKGROUNDTEST_H_

#include <cxxtest/TestSuite.h>

#include "MantidAlgorithms/CalculatePolynomialBackground.h"

#include "MantidAPI/FrameworkManager.h"
#include "MantidDataObjects/Workspace2D.h"
#include "MantidDataObjects/WorkspaceCreation.h"
#include "MantidHistogramData/Histogram.h"
#include "MantidTestHelpers/WorkspaceCreationHelper.h"

using namespace Mantid;

class CalculatePolynomialBackgroundTest : public CxxTest::TestSuite {
public:
  // This pair of boilerplate methods prevent the suite being created statically
  // This means the constructor isn't called when running other tests
  static CalculatePolynomialBackgroundTest *createSuite() { return new CalculatePolynomialBackgroundTest(); }
  static void destroySuite( CalculatePolynomialBackgroundTest *suite ) { delete suite; }


  CalculatePolynomialBackgroundTest() { API::FrameworkManager::Instance(); }

  void test_Init() {
    Algorithms::CalculatePolynomialBackground alg;
    alg.setRethrows(true);
    TS_ASSERT_THROWS_NOTHING(alg.initialize())
    TS_ASSERT(alg.isInitialized())
  }

  void test_successfulExecutionWithDefaultParameters() {
    using namespace WorkspaceCreationHelper;
    const auto nHist = 2;
    const auto nBin = 2;
    auto ws = create2DWorkspaceWhereYIsWorkspaceIndex(nHist, nBin + 1);
    auto alg = makeAlgorithm();
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("InputWorkspace", ws))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("OutputWorkspace", "outputWS"))
    TS_ASSERT_THROWS_NOTHING(alg->execute())
    TS_ASSERT(alg->isExecuted())
  }

  void test_constantBackground() {
    using namespace WorkspaceCreationHelper;
    const size_t nHist{2};
    const size_t nBin{3};
    auto ws = create2DWorkspaceWhereYIsWorkspaceIndex(nHist, nBin);
    for (size_t histI = 0; histI < nHist; ++histI) {
      ws->setCountVariances(histI, nBin, static_cast<double>(histI + 1));
    }
    auto alg = makeAlgorithm();
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("InputWorkspace", ws))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("OutputWorkspace", "outputWS"))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("Degree", 0))
    TS_ASSERT_THROWS_NOTHING(alg->execute())
    TS_ASSERT(alg->isExecuted())
    API::MatrixWorkspace_sptr outWS = alg->getProperty("OutputWorkspace");
    TS_ASSERT(outWS)
    for (size_t histI = 0; histI < nHist; ++histI) {
      const auto &ys = ws->y(histI);
      const auto &es = ws->e(histI);
      const auto &xs = ws->x(histI);
      const auto &bkgYs = outWS->y(histI);
      const auto &bkgEs = outWS->e(histI);
      const auto &bkgXs = outWS->x(histI);
      for (size_t binI = 0; binI < nBin; ++binI) {
        TS_ASSERT_DELTA(bkgYs[binI], ys[binI], 1e-12)
        TS_ASSERT_DELTA(bkgEs[binI], es[binI] / std::sqrt(static_cast<double>(nBin)), 1e-12)
        TS_ASSERT_EQUALS(bkgXs[binI], xs[binI])
      }
    }
  }

  void test_linearBackground() {
    using namespace WorkspaceCreationHelper;
    const size_t nHist{2};
    const size_t nBin{3};
    auto ws = create2DWorkspaceWhereYIsWorkspaceIndex(nHist, nBin);
    for (size_t histI = 0; histI < nHist; ++histI) {
      ws->setCountVariances(histI, nBin, static_cast<double>(histI + 1));
    }
    auto alg = makeAlgorithm();
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("InputWorkspace", ws))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("OutputWorkspace", "outputWS"))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("Degree", 1))
    TS_ASSERT_THROWS_NOTHING(alg->execute())
    TS_ASSERT(alg->isExecuted())
    API::MatrixWorkspace_sptr outWS = alg->getProperty("OutputWorkspace");
    TS_ASSERT(outWS)
    for (size_t histI = 0; histI < nHist; ++histI) {
      const auto &ys = ws->y(histI);
      const auto &xs = ws->x(histI);
      const auto &bkgYs = outWS->y(histI);
      const auto &bkgEs = outWS->e(histI);
      const auto &bkgXs = outWS->x(histI);
      for (size_t binI = 0; binI < nBin; ++binI) {
        TS_ASSERT_DELTA(bkgYs[binI], ys[binI], 1e-12)
        TS_ASSERT_LESS_THAN(0, bkgEs[binI])
        TS_ASSERT_EQUALS(bkgXs[binI], xs[binI])
      }
    }
  }

  void test_rangesWithGap() {
    using namespace WorkspaceCreationHelper;
    const size_t nHist{1};
    const HistogramData::BinEdges edges{0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5};
    const auto nBin = edges.size() - 1;
    const HistogramData::Counts counts{1.0, 2.0, 0.0, 0.0, 5.0, 6.0};
    const HistogramData::Histogram h{edges, counts};
    auto ws = API::MatrixWorkspace_sptr(DataObjects::create<DataObjects::Workspace2D>(nHist, h));
    auto alg = makeAlgorithm();
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("InputWorkspace", ws))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("OutputWorkspace", "outputWS"))
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("Degree", 1))
    const std::vector<double> ranges{0.0, 2.5, 4.5, 7.0};
    TS_ASSERT_THROWS_NOTHING(alg->setProperty("XRanges", ranges))
    TS_ASSERT_THROWS_NOTHING(alg->execute())
    TS_ASSERT(alg->isExecuted())
    API::MatrixWorkspace_sptr outWS = alg->getProperty("OutputWorkspace");
    TS_ASSERT(outWS)
    const auto &xs = ws->x(0);
    const auto &bkgYs = outWS->y(0);
    const auto &bkgEs = outWS->e(0);
    const auto &bkgXs = outWS->x(0);
    const std::vector<double> expected{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    for (size_t binI = 0; binI < nBin; ++binI) {
      TS_ASSERT_DELTA(bkgYs[binI], expected[binI], 1e-12)
      TS_ASSERT_LESS_THAN(0, bkgEs[binI])
      TS_ASSERT_EQUALS(bkgXs[binI], xs[binI])
    }
  }

private:
  static boost::shared_ptr<Algorithms::CalculatePolynomialBackground> makeAlgorithm() {
    auto a = boost::make_shared<Algorithms::CalculatePolynomialBackground>();
    a->initialize();
    a->setChild(true);
    a->setRethrows(true);
    return a;
  }
};


#endif /* MANTID_ALGORITHMS_CALCULATEPOLYNOMIALBACKGROUNDTEST_H_ */