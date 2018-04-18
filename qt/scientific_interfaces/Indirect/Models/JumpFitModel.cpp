#include "JumpFitModel.h"

#include "MantidAPI/TextAxis.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/map.hpp>

#include <unordered_map>

using namespace Mantid::API;

namespace {
using namespace MantidQt::CustomInterfaces::IDA;

struct ContainsOneOrMore {
  ContainsOneOrMore(std::vector<std::string> &&substrings)
      : m_substrings(std::move(substrings)) {}

  bool operator()(const std::string &str) const {
    for (const auto &substring : m_substrings) {
      if (str.rfind(substring) != std::string::npos)
        return true;
    }
    return false;
  }

private:
  std::vector<std::string> m_substrings;
};

template <typename Predicate>
std::pair<std::vector<std::string>, std::vector<std::size_t>>
findAxisLabels(TextAxis *axis, Predicate const &predicate) {
  std::vector<std::string> labels;
  std::vector<std::size_t> spectra;

  for (auto i = 0u; i < axis->length(); ++i) {
    auto label = axis->label(i);
    if (predicate(label)) {
      labels.emplace_back(label);
      spectra.emplace_back(i);
    }
  }
  return labels;
}

template <typename Predicate>
std::pair<std::vector<std::string>, std::vector<std::size_t>>
findAxisLabels(MatrixWorkspace_const_sptr workspace,
               Predicate const &predicate) {
  auto axis = dynamic_cast<TextAxis *>(workspace->getAxis(1));
  if (axis)
    return findAxisLabels(axis, predicate);
  return std::make_pair({}, {});
}

Spectra createSpectra(std::size_t spectrum) {
  return std::make_pair(spectrum, spectrum);
}

} // namespace

namespace MantidQt {
namespace CustomInterfaces {
namespace IDA {

void JumpFitModel::addWorkspace(Mantid::API::MatrixWorkspace_sptr workspace,
                                const Spectra &) {
  findWidths(workspace);

  if (!m_widths.empty()) {
    IndirectFittingModel::clearWorkspaces();
    IndirectFittingModel::addWorkspace(workspace,
                                       createSpectra(m_widthSpectra[0]));
  }
}

void JumpFitModel::findWidths(MatrixWorkspace_sptr workspace) {
  auto found =
      findAxisLabels(workspace, ContainsOneOrMore({".Width", ".FWHM"}));
  m_widths = found.first;
  m_widthSpectra = found.second;
}

void JumpFitModel::setFitType(const std::string &fitType) {
  m_fitType = fitType;
}

const std::vector<std::string> &JumpFitModel::getWidths() const {
  return m_widths;
}

std::size_t JumpFitModel::getWidthSpectra(std::size_t widthIndex) const {
  return m_widthSpectra[widthIndex];
}

std::string JumpFitModel::sequentialFitOutputName() const {
  auto name = createOutputName("%1%_JumpFit", "", 0);
  auto position = name.rfind("_Result");
  if (position != std::string::npos)
    return name.substr(0, position) + name.substr(position + 7, name.size());
  return name;
}

std::string JumpFitModel::simultaneousFitOutputName() const {
  return sequentialFitOutputName();
}

} // namespace IDA
} // namespace CustomInterfaces
} // namespace MantidQt
