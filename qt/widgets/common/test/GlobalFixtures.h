#ifndef WIDGETSCOMMON_GLOBALFIXTURES_H
#define WIDGETSCOMMON_GLOBALFIXTURES_H

#include <QApplication>

#include <cxxtest/GlobalFixture.h>

/**
 * QApplicationInit
 *
 * Uses setUpWorld/tearDownWorld to initialize & finalize
 * QApplication object
 */
class QApplicationHolder : CxxTest::GlobalFixture {
public:
  bool setUpWorld() override {
    int argc(0);
    char **argv = {};
    m_app = new QApplication(argc, argv);
    return true;
  }

  bool tearDownWorld() override {
    delete m_app;
    return true;
  }

private:
  QApplication *m_app;
};

//------------------------------------------------------------------------------
// Static definitions
//
// We rely on cxxtest only including this file once so that the following
// statements do not cause multiple-definition errors.
//------------------------------------------------------------------------------
static QApplicationHolder MAIN_QAPPLICATION;

#endif // WIDGETSCOMMON_GLOBALFIXTURES_H
