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
 * pax_simple8b_rle.h
 *
 *   Simple8b-RLE integer compression.
 *
 *   Selector table (public standard, Vo Ngoc Anh & Moffat, SPE 2010):
 *   Sel:  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
 *   Num:  0  64  32  21  16  12  10   9   8   6   5   4   3   2   1  RLE
 *   Bpe:  0   1   2   3   4   5   6   7   8  10  12  16  21  32  64  --
 *
 *   Selector 15 is RLE: [36-bit count][28-bit value].
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_simple8b_rle.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace pax {

/*
 * Serialization format:
 *   [4 bytes: num_elements (uint32)]
 *   [4 bytes: num_blocks (uint32)]
 *   [ceil(num_blocks/16) * 8 bytes: selector slots, 4-bit per selector]
 *   [num_blocks * 8 bytes: data blocks]
 */

class Simple8bRleCompressor {
 public:
  void Append(uint64_t value);
  std::pair<const char *, size_t> Finish();
  void Reset();

 private:
  void FlushPending();

  std::vector<uint64_t> pending_;
  std::vector<uint8_t> selectors_;
  std::vector<uint64_t> data_blocks_;
  uint32_t num_elements_ = 0;
  std::vector<char> output_;
};

class Simple8bRleDecompressor {
 public:
  void Init(const char *data, size_t len);
  uint32_t NumElements() const;
  uint64_t Next();
  bool HasNext() const;

 private:
  void DecodeCurrentBlock();

  const char *raw_data_ = nullptr;
  size_t raw_len_ = 0;
  uint32_t num_elements_ = 0;
  uint32_t num_blocks_ = 0;
  const uint8_t *selector_bytes_ = nullptr;
  const uint64_t *blocks_ = nullptr;

  uint32_t block_idx_ = 0;
  uint32_t decoded_count_ = 0;

  /* decoded values from current block
   * count is uint64_t because RLE selector 15 uses a 36-bit count field
   * (max ~68 billion), which exceeds uint32_t range. */
  uint64_t block_values_[64];
  uint64_t block_values_count_ = 0;
  uint64_t block_values_pos_ = 0;
};

}  // namespace pax
