/*-------------------------------------------------------------------------
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * pax_simple8b_rle.cc
 *
 *   Simple8b-RLE integer compression implementation.
 *   Based on: Vo Ngoc Anh & Moffat, "Index compression using 64-bit words",
 *   Software: Practice and Experience, 2010.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_simple8b_rle.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_simple8b_rle.h"

#include <cstring>

#include "comm/cbdb_wrappers.h"
#include "comm/log.h"

namespace pax {

/*
 * Simple8b selector table.
 * selector 0: 0 elements, 0 bits each (unused/padding)
 * selector 15: RLE encoding — 36-bit count, 28-bit value
 */
static constexpr int kSelectorCount = 16;

static constexpr int kSelectorElements[kSelectorCount] = {
    0, 64, 32, 21, 16, 12, 10, 9, 8, 6, 5, 4, 3, 2, 1, 0};

static constexpr int kSelectorBitsPerElem[kSelectorCount] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 21, 32, 64, 0};

static constexpr uint64_t kMaxValueForBits[65] = {
    0ULL,
    1ULL,
    3ULL,
    7ULL,
    0xFULL,
    0x1FULL,
    0x3FULL,
    0x7FULL,
    0xFFULL,
    0x1FFULL,
    0x3FFULL,
    0x7FFULL,
    0xFFFULL,
    0x1FFFULL,
    0x3FFFULL,
    0x7FFFULL,
    0xFFFFULL,
    0x1FFFFULL,
    0x3FFFFULL,
    0x7FFFFULL,
    0xFFFFFULL,
    0x1FFFFFULL,
    0x3FFFFFULL,
    0x7FFFFFULL,
    0xFFFFFFULL,
    0x1FFFFFFULL,
    0x3FFFFFFULL,
    0x7FFFFFFULL,
    0xFFFFFFFULL,
    0x1FFFFFFFULL,
    0x3FFFFFFFULL,
    0x7FFFFFFFULL,
    0xFFFFFFFFULL,
    0x1FFFFFFFFULL,
    0x3FFFFFFFFULL,
    0x7FFFFFFFFULL,
    0xFFFFFFFFFULL,
    0x1FFFFFFFFFULL,
    0x3FFFFFFFFFULL,
    0x7FFFFFFFFFULL,
    0xFFFFFFFFFFULL,
    0x1FFFFFFFFFFULL,
    0x3FFFFFFFFFFULL,
    0x7FFFFFFFFFFULL,
    0xFFFFFFFFFFFULL,
    0x1FFFFFFFFFFFULL,
    0x3FFFFFFFFFFFULL,
    0x7FFFFFFFFFFFULL,
    0xFFFFFFFFFFFFULL,
    0x1FFFFFFFFFFFFULL,
    0x3FFFFFFFFFFFFULL,
    0x7FFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFULL,
    0x1FFFFFFFFFFFFFULL,
    0x3FFFFFFFFFFFFFULL,
    0x7FFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFULL,
    0x1FFFFFFFFFFFFFFULL,
    0x3FFFFFFFFFFFFFFULL,
    0x7FFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFULL,
    0x1FFFFFFFFFFFFFFFULL,
    0x3FFFFFFFFFFFFFFFULL,
    0x7FFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL,
};

void Simple8bRleCompressor::Append(uint64_t value) {
  pending_.push_back(value);
  num_elements_++;
}

void Simple8bRleCompressor::Reset() {
  pending_.clear();
  selectors_.clear();
  data_blocks_.clear();
  num_elements_ = 0;
  output_.clear();
}

void Simple8bRleCompressor::FlushPending() {
  size_t pos = 0;
  size_t total = pending_.size();

  while (pos < total) {
    /* try RLE for runs — only use RLE when run_len exceeds the capacity
     * of the best packed selector for this value. Otherwise, packed
     * selectors are more space-efficient (e.g. selector 1 packs 64
     * 1-bit values per block vs RLE using 1 block per run). */
    if (total - pos >= 2 && pending_[pos] <= kMaxValueForBits[28]) {
      uint64_t val = pending_[pos];
      size_t run_len = 1;
      while (pos + run_len < total && pending_[pos + run_len] == val) {
        run_len++;
      }
      /* find the best selector capacity for this value */
      int best_capacity = 1; /* selector 14: 1 element, always fits */
      for (int sel = 1; sel <= 13; sel++) {
        if (val <= kMaxValueForBits[kSelectorBitsPerElem[sel]]) {
          best_capacity = kSelectorElements[sel];
          break;
        }
      }
      if (run_len > static_cast<size_t>(best_capacity)) {
        uint64_t max_run = kMaxValueForBits[36];
        if (run_len > static_cast<size_t>(max_run))
          run_len = static_cast<size_t>(max_run);
        uint64_t block = (static_cast<uint64_t>(run_len) << 28) | val;
        selectors_.push_back(15);
        data_blocks_.push_back(block);
        pos += run_len;
        continue;
      }
    }

    /* try selectors from most compact (1) to least compact (14) */
    bool packed = false;
    for (int sel = 1; sel <= 14; sel++) {
      int num_elems = kSelectorElements[sel];
      int bits_per = kSelectorBitsPerElem[sel];

      if (num_elems == 0) continue;

      int remaining = static_cast<int>(total - pos);

      /* selector 14: 1 element, 64 bits — always fits */
      if (sel == 14) {
        selectors_.push_back(14);
        data_blocks_.push_back(pending_[pos]);
        pos += 1;
        packed = true;
        break;
      }

      /* Tail packing: when fewer values remain than num_elems, pack them
       * with zero-padding. The decoder uses num_elements_ to stop, so
       * the zero-padded slots are never consumed. */
      int pack_count = (remaining < num_elems) ? remaining : num_elems;

      uint64_t max_val = kMaxValueForBits[bits_per];
      bool fits = true;
      for (int i = 0; i < pack_count; i++) {
        if (pending_[pos + i] > max_val) {
          fits = false;
          break;
        }
      }
      if (!fits) continue;

      /* pack values into a single 64-bit block (zero-padded if tail) */
      uint64_t block = 0;
      for (int i = pack_count - 1; i >= 0; i--) {
        block = (block << bits_per) | pending_[pos + i];
      }
      selectors_.push_back(static_cast<uint8_t>(sel));
      data_blocks_.push_back(block);
      pos += pack_count;
      packed = true;
      break;
    }
    Assert(packed);
    (void)packed;
  }
}

std::pair<const char *, size_t> Simple8bRleCompressor::Finish() {
  FlushPending();

  uint32_t num_blocks = static_cast<uint32_t>(data_blocks_.size());
  /* each selector is 4 bits, 16 selectors per 8-byte slot */
  uint32_t num_selector_slots = (num_blocks + 15) / 16;

  size_t total_size = sizeof(uint32_t) * 2 +
                      num_selector_slots * sizeof(uint64_t) +
                      num_blocks * sizeof(uint64_t);

  output_.resize(total_size);
  char *ptr = output_.data();

  /* write header */
  memcpy(ptr, &num_elements_, sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  memcpy(ptr, &num_blocks, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  /* write selector slots (4-bit per selector, 16 per slot).
   * Accumulate selectors in a local variable and write once per slot
   * to avoid repeated read-modify-write to heap memory. */
  char *selector_ptr = ptr;
  uint64_t slot_val = 0;
  for (uint32_t i = 0; i < num_blocks; i++) {
    uint32_t pos_in_slot = i % 16;
    slot_val |= (static_cast<uint64_t>(selectors_[i]) << (pos_in_slot * 4));
    if (pos_in_slot == 15 || i == num_blocks - 1) {
      memcpy(selector_ptr, &slot_val, sizeof(uint64_t));
      selector_ptr += sizeof(uint64_t);
      slot_val = 0;
    }
  }
  ptr += num_selector_slots * sizeof(uint64_t);

  /* write data blocks */
  if (num_blocks > 0) {
    memcpy(ptr, data_blocks_.data(), num_blocks * sizeof(uint64_t));
  }

  return {output_.data(), total_size};
}

/* ---- Decompressor ---- */

void Simple8bRleDecompressor::Init(const char *data, size_t len) {
  Assert(len >= sizeof(uint32_t) * 2);
  raw_data_ = data;
  raw_len_ = len;

  const char *ptr = data;
  memcpy(&num_elements_, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  memcpy(&num_blocks_, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  selector_bytes_ = reinterpret_cast<const uint8_t *>(ptr);
  uint32_t num_selector_slots = (num_blocks_ + 15) / 16;
  blocks_ = reinterpret_cast<const uint64_t *>(
      ptr + num_selector_slots * sizeof(uint64_t));

  block_idx_ = 0;
  decoded_count_ = 0;
  block_values_count_ = 0;
  block_values_pos_ = 0;
}

uint32_t Simple8bRleDecompressor::NumElements() const {
  return num_elements_;
}

bool Simple8bRleDecompressor::HasNext() const {
  return decoded_count_ < num_elements_;
}

void Simple8bRleDecompressor::DecodeCurrentBlock() {
  Assert(block_idx_ < num_blocks_);

  /* read 4-bit selector using memcpy for alignment safety */
  uint32_t slot_idx = block_idx_ / 16;
  uint32_t bit_offset = (block_idx_ % 16) * 4;
  uint64_t slot_val;
  memcpy(&slot_val, selector_bytes_ + slot_idx * sizeof(uint64_t),
         sizeof(uint64_t));
  uint8_t sel = (slot_val >> bit_offset) & 0xF;

  /* Selector 0 is reserved/unused — reject corrupted data.
   * Use CBDB_RAISE (not Assert) so this check survives release builds,
   * since corrupted on-disk data can trigger sel==0 in production. */
  if (sel == 0) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               "Simple8b: selector 0 is invalid (corrupted data)");
  }

  /* Use memcpy for alignment safety — blocks_ may not be 8-byte aligned
   * when embedded in Gorilla's combined buffer (e.g. on ARM64). */
  uint64_t block;
  memcpy(&block,
         reinterpret_cast<const char *>(blocks_) +
             block_idx_ * sizeof(uint64_t),
         sizeof(uint64_t));
  block_idx_++;

  if (sel == 15) {
    /* RLE: [36-bit count][28-bit value] */
    uint64_t value = block & kMaxValueForBits[28];
    uint64_t count = block >> 28;
    block_values_count_ = count;
    block_values_pos_ = 0;
    /* fill the buffer with the RLE value */
    uint64_t fill = (block_values_count_ < 64) ? block_values_count_ : 64;
    for (uint64_t i = 0; i < fill; i++) {
      block_values_[i] = value;
    }
    return;
  }

  if (sel == 14) {
    /* 1 element, 64 bits */
    block_values_[0] = block;
    block_values_count_ = 1;
    block_values_pos_ = 0;
    return;
  }

  int num_elems = kSelectorElements[sel];
  int bits_per = kSelectorBitsPerElem[sel];
  uint64_t mask = kMaxValueForBits[bits_per];

  block_values_count_ = static_cast<uint64_t>(num_elems);
  block_values_pos_ = 0;

  for (int i = 0; i < num_elems; i++) {
    block_values_[i] = block & mask;
    block >>= bits_per;
  }
}

uint64_t Simple8bRleDecompressor::Next() {
  Assert(HasNext());

  if (block_values_pos_ >= block_values_count_) {
    DecodeCurrentBlock();
  }

  /* for large RLE runs that exceed the 64-element buffer */
  if (block_values_pos_ >= 64 && block_values_pos_ < block_values_count_) {
    decoded_count_++;
    block_values_pos_++;
    return block_values_[0];
  }

  uint64_t val = block_values_[block_values_pos_];
  block_values_pos_++;
  decoded_count_++;
  return val;
}

}  // namespace pax
