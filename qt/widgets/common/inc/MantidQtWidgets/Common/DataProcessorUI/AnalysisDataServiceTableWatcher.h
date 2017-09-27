#include "MantidAPI/AnalysisDataService.h"
namespace MantidQt {
namespace MantidWidgets {
namespace DataProcessor {
class AnalysisDataServiceTableWatcher
    : public MantidQt::API::WorkspaceObserver {
public:
  using TableWorkspaceSet = QSet<QString>;
  using Subscriber = std::function<void(const TableWorkspaceSet& tables)>;
  AnalysisDataServiceTableWatcher(AnalysisDataServiceImpl &ads, Subscriber onListUpdated);

private:
  void onListUpdated();
  void handlePreDelete(const std::string &name,
                       Mantid::API::Workspace_sptr workspace);
  void handlePostDelete(const std::string &name);
  void handleAdd(const std::string &name,
                 Mantid::API::Workspace_sptr workspace);
  void handleAfterReplace(const std::string &name,
                          Mantid::API::Workspace_sptr workspace);
  void handleRename(const std::string &oldName, const std::string &newName);
  void handleClearADS();
  TableWorkspaceSet m_tables;
  AnalysisDataServiceImpl &m_ads;
Subscriber m_onListUpdated;
};
}
}
}
