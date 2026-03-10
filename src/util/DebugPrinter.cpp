#include "util/DebugPrinter.h"

#include "storage/PageLayout.h"

#include <sstream>

namespace sdb {

std::string DebugPrinter::renderTree(Pager& pager, std::uint32_t rootPageId) {
    std::string output;
    renderNode(pager, rootPageId, 0, output);
    return output;
}

void DebugPrinter::renderNode(Pager& pager, std::uint32_t pageId, int depth, std::string& output) {
    const auto page = pager.readPage(pageId);
    const auto indent = std::string(static_cast<std::size_t>(depth) * 2, ' ');
    std::ostringstream line;

    if (layout::pageType(page) == PageType::Leaf) {
        line << indent << "Leaf(" << pageId << "): [";
        for (std::size_t i = 0; i < layout::keyCount(page); ++i) {
            if (i > 0) {
                line << ", ";
            }
            line << layout::leafKeyAt(page, i);
        }
        line << "]\n";
        output += line.str();
        return;
    }

    line << indent << "Internal(" << pageId << "): [";
    for (std::size_t i = 0; i < layout::keyCount(page); ++i) {
        if (i > 0) {
            line << ", ";
        }
        line << layout::internalKeyAt(page, i);
    }
    line << "]\n";
    output += line.str();

    for (std::size_t i = 0; i <= layout::keyCount(page); ++i) {
        renderNode(pager, layout::internalChildAt(page, i), depth + 1, output);
    }
}

}  // namespace sdb
