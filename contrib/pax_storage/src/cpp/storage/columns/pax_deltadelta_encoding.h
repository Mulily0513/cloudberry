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
 * pax_deltadelta_encoding.h
 *
 *   Double-delta integer compression for PAX columnar storage.
 *   Algorithm: delta-of-deltas + zigzag encoding + Simple8b-RLE.
 *
 *   Compression format:
 *     [4 bytes: num_elements (uint32)]
 *     [1 byte:  element_size (sizeof(T): 1/2/4/8)]
 *     [3 bytes: padding]
 *     [8 bytes: first_value (int64)]
 *     [variable: Simple8bRle serialized delta-of-deltas]
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_deltadelta_encoding.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <vector>

#include "storage/columns/pax_decoding.h"
#include "storage/columns/pax_encoding.h"

namespace pax {

struct DeltaDeltaHeader {
  uint32_t num_elements;
  uint8_t element_size;
  uint8_t padding[3];
  int64_t first_value;
};

class PaxDeltaDeltaEncoder : public PaxEncoder {
 public:
  explicit PaxDeltaDeltaEncoder(const EncodingOption &encoder_options);

  void Append(char *data, size_t size) override;
  bool SupportAppendNull() const override;
  void Flush() override;
  size_t GetBoundSize(size_t src_len) const override;

 private:
  static int64_t ReadSignExtended(const char *data, size_t elem_size);

  uint8_t element_size_ = 0;
  std::vector<int64_t> values_;
};

template <typename T>
class PaxDeltaDeltaDecoder : public PaxDecoder {
 public:
  explicit PaxDeltaDeltaDecoder(const PaxDecoder::DecodingOption &decoder_options);

  PaxDecoder *SetSrcBuffer(char *data, size_t data_len) override;
  PaxDecoder *SetDataBuffer(
      std::shared_ptr<DataBuffer<char>> result_buffer) override;
  size_t Next(const char *not_null) override;
  size_t Decoding() override;
  size_t Decoding(const char *not_null, size_t not_null_len) override;
  const char *GetBuffer() const override;
  size_t GetBufferSize() const override;

 private:
  std::shared_ptr<DataBuffer<char>> data_buffer_;
  std::shared_ptr<DataBuffer<char>> result_buffer_;
};

}  // namespace pax
