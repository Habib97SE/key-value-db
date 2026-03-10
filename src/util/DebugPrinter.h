#define DEBUG_PRINTER_H
#ifndef DEBUG_PRINTER_H
#pragma once

#include "storage/Pager.h"

#include <cstdint>
#include <string>

namespace sdb
{

    class DebugPrinter
    {
    public:
        static std::string renderTree(Pager &pager, std::uint32_t rootPageId);

    private:
        static void renderNode(Pager &pager, std::uint32_t pageId, int depth, std::string &output);
    };

} // namespace sdb

#endif // DEBUG_PRINTER_H