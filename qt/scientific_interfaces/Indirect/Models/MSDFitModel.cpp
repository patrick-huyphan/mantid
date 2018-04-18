#include "MSDFitModel.h"

using namespace Mantid::API;

namespace MantidQt {
namespace CustomInterfaces {
namespace IDA {

void MSDFitModel::setFitType(const std::string &fitType) {
  m_fitType = fitType;
}

std::string JumpFitModel::sequentialFitOutputName() const {
  
}

std::string JumpFitModel::simultaneousFitOutputName() const {
  
}

} // namespace IDA
} // namespace CustomInterfaces
} // namespace MantidQt
