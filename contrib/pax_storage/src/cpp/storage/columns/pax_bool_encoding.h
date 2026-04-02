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
 * pax_bool_encoding.h
 *
 *   Boolean compression for PAX columnar storage.
 *   Uses Simple8b-RLE to pack boolean values (0/1) efficiently.
 *   Since booleans are single-bit values, Simple8b-RLE packs up to
 *   64 booleans per 64-bit word (selector 1, 1-bit per element).
 *
 *   Compression format:
 *     [4 bytes: num_elements (uint32)]
 *     [variable: Simple8bRle serialized 0/1 values]
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_bool_encoding.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "storage/columns/pax_decoding.h"
#include "storage/columns/pax_encoding.h"
#include "storage/columns/pax_simple8b_rle.h"

namespace pax {

class PaxBoolEncoder : public PaxEncoder {
 public:
  explicit PaxBoolEncoder(const EncodingOption &encoder_options);

  void Append(char *data, size_t size) override;
  bool SupportAppendNull() const override;
  void Flush() override;
  size_t GetBoundSize(size_t src_len) const override;

 private:
  Simple8bRleCompressor compressor_;
  uint32_t num_elements_ = 0;
};

class PaxBoolDecoder : public PaxDecoder {
 public:
  explicit PaxBoolDecoder(const PaxDecoder::DecodingOption &decoder_options);

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
