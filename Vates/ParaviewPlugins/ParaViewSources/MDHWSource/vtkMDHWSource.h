#ifndef _vtkMDHWSource_h 
#define _vtkMDHWSource_h

#include "MantidKernel/make_unique.h"
#include "MantidVatesAPI/Normalization.h"
#include "vtkStructuredGridAlgorithm.h"

#include <string>

namespace Mantid
{
  namespace VATES
  {
    class MDLoadingPresenter;
  }
}

/*  Source for fetching Multidimensional Workspace out of the Mantid Analysis Data Service
    and converting them into vtkDataSets as part of the pipeline source.

    @date 01/12/2011

    Copyright &copy; 2007-9 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge National Laboratory & European Spallation Source

    This file is part of Mantid.

    Mantid is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Mantid is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    File change history is stored at: <https://github.com/mantidproject/mantid>.
    Code Documentation is available at: <http://doxygen.mantidproject.org>

*/

// cppcheck-suppress class_X_Y
class VTK_EXPORT vtkMDHWSource : public vtkStructuredGridAlgorithm
{
public:
  static vtkMDHWSource *New();
  vtkTypeMacro(vtkMDHWSource, vtkStructuredGridAlgorithm) void PrintSelf(
      ostream &os, vtkIndent indent) override;

  void SetWsName(std::string wsName);

  //------- MDLoadingView methods ----------------
  virtual double getTime() const;
  virtual size_t getRecursionDepth() const;
  virtual bool getLoadInMemory() const;
  //----------------------------------------------

  /// Update the algorithm progress.
  void updateAlgorithmProgress(double, const std::string&);
  /// Getter for the input geometry xml
  const char* GetInputGeometryXML();
  /// Getter for the special coodinate value
  int GetSpecialCoordinates();
  /// Getter for the workspace name
  const char* GetWorkspaceName();
  /// Getter for the workspace type
  char* GetWorkspaceTypeName();
  /// Getter for the minimum value of the workspace data
  double GetMinValue();
  /// Getter for the maximum value of the workspace data
  double GetMaxValue();
  /// Getter for the maximum value of the workspace data
  const char* GetInstrument();
  /// Setter for the normalization
  void SetNormalization(int option);

protected:
  vtkMDHWSource();
  ~vtkMDHWSource() override;
  int RequestInformation(vtkInformation *, vtkInformationVector **,
                         vtkInformationVector *) override;
  int RequestData(vtkInformation *, vtkInformationVector **,
                  vtkInformationVector *) override;

private:
  
  /// Name of the workspace.
  std::string m_wsName;

  /// Time.
  double m_time;

  /// MVP presenter.
  std::unique_ptr<Mantid::VATES::MDLoadingPresenter> m_presenter;

  /// Cached typename.
  std::string typeName;

  /// Normalization Option
  Mantid::VATES::VisualNormalization m_normalizationOption;


  vtkMDHWSource(const vtkMDHWSource&);
  void operator = (const vtkMDHWSource&);
  void setTimeRange(vtkInformationVector* outputVector);
};
#endif
