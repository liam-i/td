//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/bits.h"
#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/MapNode.h"
#include "td/utils/Random.h"
#include "td/utils/SetNode.h"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace td {

template <class NodeT, class HashT, class EqT>
class FlatHashTable {
  static constexpr uint32 INVALID_BUCKET = 0xFFFFFFFF;

  void allocate_nodes(uint32 size) {
    DCHECK(size >= 8);
    DCHECK((size & (size - 1)) == 0);
    CHECK(size <= min(static_cast<uint32>(1) << 29, static_cast<uint32>(0x7FFFFFFF / sizeof(NodeT))));
    nodes_ = new NodeT[size];
    // used_node_count_ = 0;
    bucket_count_mask_ = size - 1;
    bucket_count_ = size;
    begin_bucket_ = INVALID_BUCKET;
  }

  static void clear_nodes(NodeT *nodes) {
    delete[] nodes;
  }

 public:
  using KeyT = typename NodeT::public_key_type;
  using key_type = typename NodeT::public_key_type;
  using value_type = typename NodeT::public_type;

  struct Iterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = FlatHashTable::value_type;
    using pointer = value_type *;
    using reference = value_type &;

    Iterator &operator++() {
      DCHECK(it_ != nullptr);
      do {
        if (unlikely(++it_ == end_)) {
          it_ = begin_;
        }
        if (unlikely(it_ == start_)) {
          it_ = nullptr;
          break;
        }
      } while (it_->empty());
      return *this;
    }
    reference operator*() {
      return it_->get_public();
    }
    pointer operator->() {
      return &*it_;
    }
    NodeT *get() {
      return it_;
    }

    bool operator==(const Iterator &other) const {
      DCHECK(begin_ == other.begin_);
      DCHECK(end_ == other.end_);
      return it_ == other.it_;
    }
    bool operator!=(const Iterator &other) const {
      DCHECK(begin_ == other.begin_);
      DCHECK(end_ == other.end_);
      return it_ != other.it_;
    }

    Iterator() = default;
    Iterator(NodeT *it, NodeT *begin, NodeT *end) : it_(it), begin_(begin), start_(it), end_(end) {
    }

   private:
    NodeT *it_ = nullptr;
    NodeT *begin_ = nullptr;
    NodeT *start_ = nullptr;
    NodeT *end_ = nullptr;
  };

  struct ConstIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = FlatHashTable::value_type;
    using pointer = const value_type *;
    using reference = const value_type &;

    ConstIterator &operator++() {
      ++it_;
      return *this;
    }
    reference operator*() {
      return *it_;
    }
    pointer operator->() {
      return &*it_;
    }
    bool operator==(const ConstIterator &other) const {
      return it_ == other.it_;
    }
    bool operator!=(const ConstIterator &other) const {
      return it_ != other.it_;
    }

    ConstIterator() = default;
    ConstIterator(Iterator it) : it_(std::move(it)) {
    }

   private:
    Iterator it_;
  };
  using iterator = Iterator;
  using const_iterator = ConstIterator;

  FlatHashTable() = default;
  FlatHashTable(const FlatHashTable &other) {
    assign(other);
  }
  void operator=(const FlatHashTable &other) {
    clear();
    assign(other);
  }

  FlatHashTable(std::initializer_list<NodeT> nodes) {
    if (nodes.size() == 0) {
      return;
    }
    reserve(nodes.size());
    uint32 used_nodes = 0;
    for (auto &new_node : nodes) {
      CHECK(!new_node.empty());
      auto bucket = calc_bucket(new_node.key());
      while (true) {
        auto &node = nodes_[bucket];
        if (node.empty()) {
          node.copy_from(new_node);
          used_nodes++;
          break;
        }
        if (EqT()(node.key(), new_node.key())) {
          break;
        }
        next_bucket(bucket);
      }
    }
    used_node_count_ = used_nodes;
  }

  FlatHashTable(FlatHashTable &&other) noexcept
      : nodes_(other.nodes_)
      , used_node_count_(other.used_node_count_)
      , bucket_count_mask_(other.bucket_count_mask_)
      , bucket_count_(other.bucket_count_)
      , begin_bucket_(other.begin_bucket_) {
    other.drop();
  }
  void operator=(FlatHashTable &&other) noexcept {
    clear();
    nodes_ = other.nodes_;
    used_node_count_ = other.used_node_count_;
    bucket_count_mask_ = other.bucket_count_mask_;
    bucket_count_ = other.bucket_count_;
    begin_bucket_ = other.begin_bucket_;
    other.drop();
  }
  void swap(FlatHashTable &other) noexcept {
    std::swap(nodes_, other.nodes_);
    std::swap(used_node_count_, other.used_node_count_);
    std::swap(bucket_count_mask_, other.bucket_count_mask_);
    std::swap(bucket_count_, other.bucket_count_);
    std::swap(begin_bucket_, other.begin_bucket_);
  }
  ~FlatHashTable() {
    if (nodes_ != nullptr) {
      clear_nodes(nodes_);
    }
  }

  uint32 bucket_count() const {
    return bucket_count_;
  }

  Iterator find(const KeyT &key) {
    if (unlikely(nodes_ == nullptr) || is_key_empty(key)) {
      return end();
    }
    auto bucket = calc_bucket(key);
    while (true) {
      auto &node = nodes_[bucket];
      if (EqT()(node.key(), key)) {
        return create_iterator(&node);
      }
      if (node.empty()) {
        return end();
      }
      next_bucket(bucket);
    }
  }

  ConstIterator find(const KeyT &key) const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->find(key));
  }

  size_t size() const {
    return used_node_count_;
  }

  bool empty() const {
    return used_node_count_ == 0;
  }

  Iterator begin() {
    if (empty()) {
      return end();
    }
    if (begin_bucket_ == INVALID_BUCKET) {
      begin_bucket_ = Random::fast_uint32() & bucket_count_mask_;
      while (nodes_[begin_bucket_].empty()) {
        next_bucket(begin_bucket_);
      }
    }
    return create_iterator(nodes_ + begin_bucket_);
  }
  Iterator end() {
    return create_iterator(nullptr);
  }

  ConstIterator begin() const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->begin());
  }
  ConstIterator end() const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->end());
  }

  void reserve(size_t size) {
    if (size == 0) {
      return;
    }
    CHECK(size <= (1u << 29));
    uint32 want_size = normalize(static_cast<uint32>(size) * 5 / 3 + 1);
    if (want_size > bucket_count()) {
      resize(want_size);
    }
  }

  template <class... ArgsT>
  std::pair<Iterator, bool> emplace(KeyT key, ArgsT &&...args) {
    try_grow();
    CHECK(!is_key_empty(key));
    auto bucket = calc_bucket(key);
    while (true) {
      auto &node = nodes_[bucket];
      if (EqT()(node.key(), key)) {
        return {create_iterator(&node), false};
      }
      if (node.empty()) {
        node.emplace(std::move(key), std::forward<ArgsT>(args)...);
        used_node_count_++;
        return {create_iterator(&node), true};
      }
      next_bucket(bucket);
    }
  }

  std::pair<Iterator, bool> insert(KeyT key) {
    return emplace(std::move(key));
  }

  template <class ItT>
  void insert(ItT begin, ItT end) {
    for (; begin != end; ++begin) {
      emplace(*begin);
    }
  }

  template <class T = typename NodeT::second_type>
  T &operator[](const KeyT &key) {
    return emplace(key).first->second;
  }

  size_t erase(const KeyT &key) {
    auto it = find(key);
    if (it == end()) {
      return 0;
    }
    erase(it);
    return 1;
  }

  size_t count(const KeyT &key) const {
    return find(key) != end();
  }

  void clear() {
    if (nodes_ != nullptr) {
      clear_nodes(nodes_);
      drop();
    }
  }

  void erase(Iterator it) {
    DCHECK(it != end());
    DCHECK(!it.get()->empty());
    erase_node(it.get());
    try_shrink();
  }

  template <class F>
  void remove_if(F &&f) {
    if (empty()) {
      return;
    }

    auto it = begin().get();
    auto end = nodes_ + bucket_count();
    while (it != end && !it->empty()) {
      ++it;
    }
    if (it == end) {
      do {
        --it;
      } while (!it->empty());
    }
    auto first_empty = it;
    while (it != end) {
      if (!it->empty() && f(it->get_public())) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    for (it = nodes_; it != first_empty;) {
      if (!it->empty() && f(it->get_public())) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    try_shrink();
  }

 private:
  NodeT *nodes_ = nullptr;
  uint32 used_node_count_ = 0;
  uint32 bucket_count_mask_ = 0;
  uint32 bucket_count_ = 0;
  uint32 begin_bucket_ = 0;

  void drop() {
    nodes_ = nullptr;
    used_node_count_ = 0;
    bucket_count_mask_ = 0;
    bucket_count_ = 0;
    begin_bucket_ = 0;
  }

  void assign(const FlatHashTable &other) {
    if (other.size() == 0) {
      return;
    }
    resize(other.bucket_count());
    for (const auto &new_node : other) {
      auto bucket = calc_bucket(new_node.key());
      while (true) {
        auto &node = nodes_[bucket];
        if (node.empty()) {
          node.copy_from(new_node);
          break;
        }
        next_bucket(bucket);
      }
    }
    used_node_count_ = other.used_node_count_;
  }

  void try_grow() {
    if (unlikely(nodes_ == nullptr)) {
      resize(8);
    } else if (unlikely(used_node_count_ * 5 > bucket_count_mask_ * 3)) {
      resize(2 * bucket_count_mask_ + 2);
    }
    invalidate_iterators();
  }

  void try_shrink() {
    DCHECK(nodes_ != nullptr);
    if (unlikely(used_node_count_ * 10 < bucket_count_mask_ && bucket_count_mask_ > 7)) {
      resize(normalize((used_node_count_ + 1) * 5 / 3 + 1));
    }
    invalidate_iterators();
  }

  static uint32 normalize(uint32 size) {
    return td::max(static_cast<uint32>(1) << (32 - count_leading_zeroes32(size)), static_cast<uint32>(8));
  }

  uint32 calc_bucket(const KeyT &key) const {
    return randomize_hash(HashT()(key)) & bucket_count_mask_;
  }

  inline void next_bucket(uint32 &bucket) const {
    bucket = (bucket + 1) & bucket_count_mask_;
  }

  void resize(uint32 new_size) {
    if (unlikely(nodes_ == nullptr)) {
      allocate_nodes(new_size);
      used_node_count_ = 0;
      return;
    }

    auto old_nodes = nodes_;
    uint32 old_size = used_node_count_;
    uint32 old_bucket_count = bucket_count_;
    allocate_nodes(new_size);
    used_node_count_ = old_size;

    auto old_nodes_end = old_nodes + old_bucket_count;
    for (NodeT *old_node = old_nodes; old_node != old_nodes_end; ++old_node) {
      if (old_node->empty()) {
        continue;
      }
      auto bucket = calc_bucket(old_node->key());
      while (!nodes_[bucket].empty()) {
        next_bucket(bucket);
      }
      nodes_[bucket] = std::move(*old_node);
    }
    clear_nodes(old_nodes);
  }

  void erase_node(NodeT *it) {
    DCHECK(nodes_ <= it && static_cast<size_t>(it - nodes_) < bucket_count());
    it->clear();
    used_node_count_--;

    const auto bucket_count = bucket_count_;
    const auto *end = nodes_ + bucket_count;
    for (auto *test_node = it + 1; test_node != end; test_node++) {
      if (likely(test_node->empty())) {
        return;
      }

      auto want_node = nodes_ + calc_bucket(test_node->key());
      if (want_node <= it || want_node > test_node) {
        *it = std::move(*test_node);
        it = test_node;
      }
    }

    auto empty_i = static_cast<uint32>(it - nodes_);
    auto empty_bucket = empty_i;
    for (uint32 test_i = bucket_count;; test_i++) {
      auto test_bucket = test_i - bucket_count_;
      if (nodes_[test_bucket].empty()) {
        return;
      }

      auto want_i = calc_bucket(nodes_[test_bucket].key());
      if (want_i < empty_i) {
        want_i += bucket_count;
      }

      if (want_i <= empty_i || want_i > test_i) {
        nodes_[empty_bucket] = std::move(nodes_[test_bucket]);
        empty_i = test_i;
        empty_bucket = test_bucket;
      }
    }
  }

  Iterator create_iterator(NodeT *node) {
    return Iterator(node, nodes_, nodes_ + bucket_count());
  }

  void invalidate_iterators() {
    begin_bucket_ = INVALID_BUCKET;
  }
};

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashMapImpl = FlatHashTable<MapNode<KeyT, ValueT>, HashT, EqT>;

template <class KeyT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashSetImpl = FlatHashTable<SetNode<KeyT>, HashT, EqT>;

}  // namespace td
