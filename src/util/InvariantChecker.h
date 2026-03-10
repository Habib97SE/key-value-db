#pragma once

#include "storage/Pager.h"

#include <cstdint>
#include <string>

namespace sdb {

class InvariantChecker {
public:
    static void validateTree(Pager& pager, std::uint32_t rootPageId);
};

}  // namespace sdb
