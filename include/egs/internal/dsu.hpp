#ifndef EGS_INTERNAL_DSU_HPP
#define EGS_INTERNAL_DSU_HPP

#include "egs/internal/common.hpp"

namespace egs::internal {

struct dsu {
public:
  Id make_set();
  Id find(Id id);
  Id merge(Id a, Id b);

private:
  std::vector<Id> parent;
  std::vector<uint32_t> rank;
};

} // namespace egs::internal

#endif
