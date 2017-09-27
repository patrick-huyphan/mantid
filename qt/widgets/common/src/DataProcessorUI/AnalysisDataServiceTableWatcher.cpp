#include "MantidQtWidgets/Common/DataProcessorUI/AnalysisDataServiceTableWatcher.h"
#include <QString>
namespace MantidQt {
namespace MantidWidgets {
namespace DataProcessor {
AnalysisDataServiceTableWatcher::AnalysisDataServiceTableWatcher(
    Mantid::API::AnalysisDataServiceImpl &ads, Subscriber onListUpdated)
    : m_ads{ads}, m_onListUpdated{onListUpdated} {}

void AnalysisDataServiceTableWatcher::onListUpdated() {
  m_onListUpdated(m_tables);
}

void AnalysisDataServiceTableWatcher::handleAdd(const std::string &name,
                                                Mantid::API::Workspace_sptr workspace) {
  if (Mantid::API::AnalysisDataService::Instance().isHiddenDataServiceObject(
          name))
    return;

  if (!m_manager->isValidModel(workspace, m_whitelist.size()))
    return;

  m_tables.insert(QString::fromStdString(name));
  onListUpdated();
}

void AnalysisDataServiceTableWatcher::handlePreDelete(
    const std::string &name, Mantid::API::Workspace_sptr workspace) {}

void AnalysisDataServiceTableWatcher::handlePostDelete(
    const std::string &name) {
  m_tables.remove(QString::fromStdString(name));
  onListUpdated();
}

void AnalysisDataServiceTableWatcher::handleAfterReplace(
    const std::string &name, Mantid::API::Workspace_sptr workspace) {}

void AnalysisDataServiceTableWatcher::handleRename(const std::string &oldName,
                                                   const std::string &newName) {
  // if a workspace with oldName exists then replace it for the same workspace
  // with newName
  auto qOldName = QString::fromStdString(oldName);
  auto qNewName = QString::fromStdString(newName);
  if (m_tables.contains(qOldName)) {
    m_tables.remove(qOldName);
    m_tables.insert(qNewName);
    onListUpdated();
  }
}

void AnalysisDataServiceTableWatcher::handleClearADS() {
  m_tables.clear();
  onListUpdated();
}
}
}
}
