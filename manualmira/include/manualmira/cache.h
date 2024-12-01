#ifndef MANUALMIRA_MANUALMIRA_INCLUDE_MANUALMIRA_CACHE_H_
#define MANUALMIRA_MANUALMIRA_INCLUDE_MANUALMIRA_CACHE_H_

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace manualmira::cache {

namespace internal {

template <std::size_t Size>
struct line {
  // TODO: consider using bit-field
  std::uintptr_t tag = 0;

  std::array<std::uint8_t, Size> block;
};

template <std::size_t WayCount, std::size_t LineSize>
struct set {
  using line = internal::line<LineSize>;

  set() { lines.resize(WayCount); }

  std::vector<line> lines;
};

}  // namespace internal

template <std::size_t SetCount, std::size_t LineSize,
          std::size_t IndexWidth = std::bit_width(SetCount - 1),
          std::size_t OffsetWidth = std::bit_width(LineSize - 1),
          std::size_t TagWidth =
              8 * sizeof(std::uintptr_t) - IndexWidth - OffsetWidth>
struct entry {
  static_assert(std::has_single_bit(SetCount),
                "Cache set count should be a power of 2");
  static_assert(std::has_single_bit(LineSize),
                "Cache line size should be a power of 2");

  static entry from_ptr(void* ptr) {
    auto addr = reinterpret_cast<std::uintptr_t>(ptr);
    return {
        .tag = addr >> IndexWidth >> OffsetWidth,
        .index = addr >> OffsetWidth,
        .offset = addr,
    };
  }

  std::size_t tag_width() { return TagWidth; }
  std::size_t index_width() { return IndexWidth; }
  std::size_t offset_width() { return OffsetWidth; }

  void* as_ptr() {
    std::uintptr_t addr =
        tag << IndexWidth << OffsetWidth | index << OffsetWidth | offset;
    return reinterpret_cast<void*>(addr);
  }

  std::uintptr_t tag : TagWidth;
  std::uintptr_t index : IndexWidth;
  std::uintptr_t offset : OffsetWidth;
};

template <std::size_t SetCount, std::size_t WayCount, std::size_t LineSize>
class cache {
 public:
  using entry = manualmira::cache::entry<SetCount, LineSize>;

  void* get(void* ptr, std::size_t size) {
    auto e = entry::from_ptr(ptr);

    if (e.offset + size > LineSize) return nullptr;

    for (line& l : sets_[e.index].lines)
      if (l.tag == e.tag) return l.block.data() + e.offset;

    return nullptr;
  }

 private:
  using set = internal::set<WayCount, LineSize>;
  using line = internal::line<LineSize>;

  std::array<set, SetCount> sets_{};
};

template <std::size_t SetCount, std::size_t LineSize>
using direct_mapped_cache = cache<SetCount, 1, LineSize>;

}  // namespace manualmira::cache

#endif
