#ifndef MANTID_ALGORITHMS_GETALLEI_H_
#define MANTID_ALGORITHMS_GETALLEI_H_

#include "MantidKernel/System.h"
#include "MantidKernel/cow_ptr.h"
#include "MantidAPI/Algorithm.h"
#include "MantidAPI/MatrixWorkspace.h"
//#include "MantidAPI/IAlgorithm.h"


namespace Mantid {

namespace Algorithms {

/** Estimate all incident energies, used by chopper instrument.

    Copyright &copy; 2008-9 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
    National Laboratory & European Spallation Source

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
class DLLExport GetAllEi : public API::Algorithm {
public:
  GetAllEi();
  virtual ~GetAllEi(){};

  /// Algorithms name for identification. @see Algorithm::name
  virtual const std::string name() const { return "GetAllEi"; };
  /// Algorithm's summary for use in the GUI and help. @see Algorithm::summary
  virtual const std::string summary() const{
    return "Analyze the chopper logs and identify energies to use as incident energies\n"
           "in an inelastic experiment from the signal registered by the monitors.";
  }
  /// Algorithm's version for identification. @see Algorithm::version
  virtual int version() const{ return 1; } ;
  /// Algorithm's category for identification. @see Algorithm::category
  virtual const std::string category()const { return "Direct\\Inelastic"; };
  /// Cross-check properties with each other @see IAlgorithm::validateInputs
  virtual std::map<std::string, std::string> validateInputs();
private:
  // Implement abstract Algorithm methods
  void init();
  void exec();
protected: // for testing, private otherwise.
  // prepare working workspace with appropriate monitor spectra for fitting 
 API::MatrixWorkspace_sptr 
    GetAllEi::buildWorkspaceToFit(const API::MatrixWorkspace_sptr &inputWS,
    size_t &wsIndex0);

   /**Return average time series log value for the appropriately filtered log*/
   double getAvrgLogValue(const API::MatrixWorkspace_sptr &inputWS, const std::string &propertyName,
          std::vector<Kernel::SplittingInterval> &splitter);
  /**process logs and retrieve chopper speed and chopper delay*/
  void findChopSpeedAndDelay(const API::MatrixWorkspace_sptr &inputWS,
       double &chop_speed,double &chop_delay);
  void findGuessOpeningTimes(const std::pair<double,double> &TOF_range,
      double ChopDelay,double Period,std::vector<double > & guess_opening_times);
  /**Get energy of monitor peak if one is present*/
  bool findMonitorPeak(const API::MatrixWorkspace_sptr &inputWS,
       double Ei,const std::vector<size_t> & monsRangeMin,
      const std::vector<size_t> & monsRangeMax,
      double & energy,double & height,double &width);
  /**Find indexes of each expected peak intervals */
  void findBinRanges(const MantidVec & eBins,const MantidVec & signal,
      const std::vector<double> & guess_energies,
      double Eresolution,std::vector<size_t> & irangeMin,
      std::vector<size_t> & irangeMax, std::vector<bool> &guessValid);

  size_t calcDerivativeAndCountZeros(const std::vector<double> &bins,const std::vector<double> &signal,
    std::vector<double> &deriv,std::vector<double> &zeros);

  /// if log, which identifies that instrument is running is available on workspace.
  /// The log should be positive when instrument is running and negative or 0 otherwise.
  bool m_useFilterLog;
  /// if true, take derivate of the filter log to identify interval when instrument is running.
  bool m_FilterWithDerivative;
  /// maximal relative peak width to consider acceptable. Defined by minimal instrument resolution
  /// and does not exceed 0.08
  double m_min_Eresolution;
  // set as half max LET resolution at 20mev at 5e-4
  double m_max_Eresolution;
  double m_peakEnergyRatio2reject;
  // the value of constant phase shift on the chopper used to calculate
  // tof at chopper from recorded delay.
  double m_phase;
};

} // namespace Algorithms
} // namespace Mantid

#endif /* MANTID_ALGORITHMS_GETALLEI_H_ */
