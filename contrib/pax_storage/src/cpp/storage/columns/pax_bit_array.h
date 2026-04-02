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
 * pax_bit_array.h
 *
 *   Variable-width bit array for Gorilla XOR-based float compression.
 *   Supports appending and reading arbitrary-width bit sequences.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_bit_array.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pax {

/*
 * Serialization format:
 *   [8 bytes: num_bits (uint64)]
 *   [ceil(num_bits/64) * 8 bytes: bucket data]
 */

class BitArray {
 public:
  void AppendBits(uint64_t value, uint8_t num_bits) {
    if (num_bits == 0) return;

    /* mask off any high bits beyond num_bits to prevent data leakage */
    if (num_bits < 64) {
      value &= ((1ULL << num_bits) - 1);
    }

    uint64_t bucket_idx = num_bits_ / 64;
    uint8_t bit_offset = static_cast<uint8_t>(num_bits_ % 64);

    if (bucket_idx >= buckets_.size()) {
      buckets_.push_back(0);
    }

    /* write into current bucket */
    buckets_[bucket_idx] |= (value << bit_offset);

    /* handle overflow into next bucket */
    if (bit_offset + num_bits > 64) {
      if (bucket_idx + 1 >= buckets_.size()) {
        buckets_.push_back(0);
      }
      buckets_[bucket_idx + 1] |= (value >> (64 - bit_offset));
    }

    num_bits_ += num_bits;
  }

  uint64_t NumBits() const { return num_bits_; }

  /* total serialized size including the num_bits header */
  size_t DataSize() const {
    return sizeof(uint64_t) + buckets_.size() * sizeof(uint64_t);
  }

  /* serialize to output buffer, returns bytes written */
  size_t Serialize(char *out) const {
    char *ptr = out;
    memcpy(ptr, &num_bits_, sizeof(uint64_t));
    ptr += sizeof(uint64_t);
    if (!buckets_.empty()) {
      memcpy(ptr, buckets_.data(), buckets_.size() * sizeof(uint64_t));
      ptr += buckets_.size() * sizeof(uint64_t);
    }
    return static_cast<size_t>(ptr - out);
  }

  void Reset() {
    buckets_.clear();
    num_bits_ = 0;
  }

 private:
  std::vector<uint64_t> buckets_;
  uint64_t num_bits_ = 0;
};

class BitArrayIterator {
 public:
  BitArrayIterator() : buckets_(nullptr), num_bits_(0), current_bit_(0) {}

  BitArrayIterator(const char *data, size_t data_size) {
    assert(data_size >= sizeof(uint64_t));
    memcpy(&num_bits_, data, sizeof(uint64_t));
    /* store as char* to avoid alignment requirements — the data may
     * follow a 4-byte length prefix and not be 8-byte aligned */
    buckets_ = data + sizeof(uint64_t);
    current_bit_ = 0;
  }

  uint64_t ReadBits(uint8_t num_bits) {
    if (num_bits == 0) return 0;
    assert(current_bit_ + num_bits <= num_bits_);

    uint64_t bucket_idx = current_bit_ / 64;
    uint8_t bit_offset = static_cast<uint8_t>(current_bit_ % 64);

    /* use memcpy to safely read potentially unaligned uint64_t */
    uint64_t bucket_val;
    memcpy(&bucket_val, buckets_ + bucket_idx * sizeof(uint64_t),
           sizeof(uint64_t));

    uint64_t result;
    if (num_bits == 64) {
      result = bucket_val >> bit_offset;
      if (bit_offset > 0) {
        uint64_t next_val;
        memcpy(&next_val, buckets_ + (bucket_idx + 1) * sizeof(uint64_t),
               sizeof(uint64_t));
        result |= next_val << (64 - bit_offset);
      }
    } else {
      uint64_t mask = (1ULL << num_bits) - 1;
      result = (bucket_val >> bit_offset) & mask;
      if (bit_offset + num_bits > 64) {
        uint64_t next_val;
        memcpy(&next_val, buckets_ + (bucket_idx + 1) * sizeof(uint64_t),
               sizeof(uint64_t));
        uint8_t remaining = static_cast<uint8_t>(bit_offset + num_bits - 64);
        uint64_t high_bits = next_val & ((1ULL << remaining) - 1);
        result |= high_bits << (num_bits - remaining);
      }
    }

    current_bit_ += num_bits;
    return result;
  }

  bool HasNext() const { return current_bit_ < num_bits_; }

  /* total serialized size (for advancing past this BitArray in a buffer) */
  size_t SerializedSize() const {
    uint64_t num_buckets = (num_bits_ + 63) / 64;
    return sizeof(uint64_t) + num_buckets * sizeof(uint64_t);
  }

 private:
  const char *buckets_;
  uint64_t num_bits_;
  uint64_t current_bit_;
};

}  // namespace pax
