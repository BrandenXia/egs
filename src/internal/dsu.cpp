#include "egs/internal/dsu.hpp"

namespace egs::internal {

Id dsu::make_set() {
  Id id = {static_cast<uint32_t>(parent.size())};
  parent.push_back(id);
  rank.push_back(1);
  return id;
}

Id dsu::find(Id id) {
  if (parent[id.val] == id)
    return id;
  return parent[id.val] = find(parent[id.val]);
}

Id dsu::merge(Id a, Id b) {
  a = find(a), b = find(b);
  if (a == b)
    return a;
  if (rank[a.val] < rank[b.val])
    std::swap(a, b);
  parent[b.val] = a;
  rank[a.val] += rank[b.val];
  return a;
}

} // namespace egs::internal
