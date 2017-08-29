#ifndef MANTIDQTWIDGETS_CATALOGSELECTOR_H_
#define MANTIDQTWIDGETS_CATALOGSELECTOR_H_

#include "ui_CatalogSelector.h"
#include "WidgetDllOption.h"

namespace MantidQt {
namespace MantidWidgets {
class EXPORT_OPT_MANTIDQT_MANTIDWIDGETS CatalogSelector : public QWidget {
  Q_OBJECT

public:
  /// Default constructor
  CatalogSelector(QWidget *parent = 0);
  /// Obtain the session information for the facilities selected.
  std::vector<std::string> getSelectedCatalogSessions();
  /// Populate the ListWidget with the facilities of the catalogs the user is
  /// logged in to.
  void populateFacilitySelection();

private:
  /// Initialise the layout
  virtual void initLayout();

private slots:
  /// Checks the checkbox of the list item selected.
  void checkSelectedFacility(QListWidgetItem *item);

protected:
  /// The form generated by QT Designer.
  Ui::CatalogSelector m_uiForm;
};
} // namespace MantidWidgets
} // namespace MantidQt

#endif // MANTIDQTWIDGETS_CATALOGSELECTOR_H_