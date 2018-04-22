#include "MantidQtWidgets/Common/Batch/ExtractSubtrees.h"
#include <tuple>
namespace MantidQt {
namespace MantidWidgets {
namespace Batch {

void ExtractSubtrees::nodeWasSubtreeRoot(RowLocation const &rowLocation) {
  previousWasRoot = true;
  previousNode = rowLocation;
}

void ExtractSubtrees::nodeWasNotSubtreeRoot(RowLocation const &rowLocation) {
  previousWasRoot = false;
  previousNode = rowLocation;
}

bool ExtractSubtrees::isChildOfPrevious(RowLocation const &location) const {
  return location.isChildOf(previousNode);
}

bool ExtractSubtrees::isSiblingOfPrevious(RowLocation const &location) const {
  return location.isSiblingOf(previousNode);
}

auto extractSubtreeRecursive(
    Subtree &subtree, RowLocation const &rootRelativeToTree,
    RowLocation parent, int minDepth,
    typename std::vector<RowLocation>::const_iterator currentRow,
    typename std::vector<RowLocation>::const_iterator endRow,
    typename std::vector<Row>::const_iterator currentRowData)
    -> boost::optional<
        std::tuple<typename std::vector<RowLocation>::const_iterator,
                   typename std::vector<Row>::const_iterator, bool>> {
  auto childCount = 0;
  for (; currentRow != endRow; ) {
    auto currentDepth = (*currentRow).depth();
    if (currentDepth > minDepth) {
      if (currentDepth == minDepth + 1) {
        auto nextPositions = extractSubtreeRecursive(
            subtree, rootRelativeToTree, subtree.back().first, minDepth + 1,
            currentRow, endRow, currentRowData);
        if (nextPositions.is_initialized()) {
          auto subtreeFinished = false;
          std::tie(currentRow, currentRowData, subtreeFinished) =
              nextPositions.value();
          if (subtreeFinished) {
            return nextPositions;
          }
        } else {
          return boost::none;
        }
      } else  {
        return boost::none;
      }
    } else if (currentDepth < minDepth) {
      if (currentDepth < rootRelativeToTree.depth())
        return std::make_tuple(currentRow, currentRowData, true);
      else
        return std::make_tuple(currentRow, currentRowData, false);
    } else {
      subtree.emplace_back(parent.child(childCount), *currentRowData);
      ++childCount;
      ++currentRow;
      ++currentRowData;
    }
  }
  return std::make_tuple(currentRow, currentRowData, true);
}

auto ExtractSubtrees::operator()(std::vector<RowLocation> region,
                                 std::vector<Row> regionData)
    -> boost::optional<std::vector<Subtree>> {
  assertOrThrow(
      region.size() == regionData.size(),
      "ExtractSubtrees: region must have a length identical to regionData");
  if (!region.empty()) {
    std::sort(region.begin(), region.end());
    auto rowIt = region.cbegin();
    auto rowDataIt = regionData.cbegin();
    auto done = false;
    auto subtrees = std::vector<Subtree>();

    while (rowIt != region.end() && !done) {
      auto subtree = Subtree({{RowLocation(), std::move(*rowDataIt)}});
      auto nextPositions =
          extractSubtreeRecursive(subtree, *rowIt, subtree[0].first, (*rowIt).depth() + 1, rowIt + 1,
                                  region.end(), rowDataIt + 1);
      if (nextPositions.is_initialized()) {
        std::tie(rowIt, rowDataIt, done) = nextPositions.value();
        subtrees.emplace_back(std::move(subtree));
      } else {
        return boost::none;
      }
    }
    return subtrees;
  } else
    return boost::none;
}
} // namespace Batch
} // namespace MantidWidgets
} // namespace MantidQt
