#ifndef MANTIDQTCUSTOMINTERFACESIDA_JUMPFITMODEL_H_
#define MANTIDQTCUSTOMINTERFACESIDA_JUMPFITMODEL_H_

#include "IndirectFittingModel.h"

namespace MantidQt {
namespace CustomInterfaces {
namespace IDA {

class DLLExport JumpFitModel : public IndirectFittingModel {
public:
  void addWorkspace(Mantid::API::MatrixWorkspace_sptr workspace,
                    const Spectra &spectra) override;
  void setFitType(const std::string &fitType);

  const std::vector<std::string> &getWidths() const;
  std::size_t getWidthSpectra(std::size_t widthIndex) const;

  std::string sequentialFitOutputName() const override;
  std::string simultaneousFitOutputName() const override;

private:
  void findWidths(Mantid::API::MatrixWorkspace_sptr workspace);

  std::string m_fitType;
  std::vector<std::string> m_widths;
  std::vector<std::size_t> m_widthSpectra;
};

} // namespace IDA
} // namespace CustomInterfaces
} // namespace MantidQt

#endif
