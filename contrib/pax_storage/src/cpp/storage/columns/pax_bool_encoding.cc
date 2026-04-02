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
 * pax_bool_encoding.cc
 *
 *   Boolean compression implementation.
 *   Uses Simple8b-RLE to pack boolean 0/1 values.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_bool_encoding.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_bool_encoding.h"

#include <cstring>

#include "storage/columns/pax_simple8b_rle.h"

namespace pax {

/* ---- Encoder ---- */

PaxBoolEncoder::PaxBoolEncoder(const EncodingOption &encoder_options)
    : PaxEncoder(encoder_options) {}

void PaxBoolEncoder::Append(char *data, size_t size) {
  /* PostgreSQL bool is 1 byte: 0 = false, non-zero = true.
   * Stream directly to compressor to avoid intermediate vector. */
  for (size_t i = 0; i < size; i++) {
    uint8_t byte_val;
    memcpy(&byte_val, data + i, 1);
    compressor_.Append(byte_val ? 1 : 0);
    num_elements_++;
  }
}

bool PaxBoolEncoder::SupportAppendNull() const { return false; }

void PaxBoolEncoder::Flush() {
  if (num_elements_ == 0) return;

  /* Guard against double-Flush: a second Flush would append data to the
   * buffer, but the decoder only reads one encoded block from the start,
   * silently losing the second batch. */
  Assert(result_buffer_->Used() == 0);

  auto [s8b_data, s8b_size] = compressor_.Finish();

  size_t total_size = sizeof(uint32_t) + s8b_size;

  if (result_buffer_->Capacity() < total_size) {
    result_buffer_->ReSize(total_size);
  }

  /* write header: just num_elements as uint32 (no struct padding risk) */
  result_buffer_->Write(reinterpret_cast<char *>(&num_elements_),
                        sizeof(uint32_t));
  result_buffer_->Brush(sizeof(uint32_t));

  /* write simple8b-rle data */
  if (s8b_size > 0) {
    result_buffer_->Write(const_cast<char *>(s8b_data), s8b_size);
    result_buffer_->Brush(s8b_size);
  }

  num_elements_ = 0;
}

size_t PaxBoolEncoder::GetBoundSize(size_t src_len) const {
  /* worst case: header + one 64-bit word per 64 bools + overhead */
  /* header is uint32 (4 bytes) + simple8b-rle data */
  return sizeof(uint32_t) + (src_len / 64 + 2) * 8 + 256;
}

/* ---- Decoder ---- */

PaxBoolDecoder::PaxBoolDecoder(
    const PaxDecoder::DecodingOption &decoder_options)
    : PaxDecoder(decoder_options),
      data_buffer_(nullptr),
      result_buffer_(nullptr) {}

PaxDecoder *PaxBoolDecoder::SetSrcBuffer(char *data, size_t data_len) {
  if (data) {
    data_buffer_ =
        std::make_shared<DataBuffer<char>>(data, data_len, false, false);
    data_buffer_->Brush(data_len);
  }
  return this;
}

PaxDecoder *PaxBoolDecoder::SetDataBuffer(
    std::shared_ptr<DataBuffer<char>> result_buffer) {
  result_buffer_ = std::move(result_buffer);
  return this;
}

const char *PaxBoolDecoder::GetBuffer() const {
  return result_buffer_ ? result_buffer_->GetBuffer() : nullptr;
}

size_t PaxBoolDecoder::GetBufferSize() const {
  return result_buffer_ ? result_buffer_->Used() : 0;
}

size_t PaxBoolDecoder::Next(const char * /*not_null*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

size_t PaxBoolDecoder::Decoding() {
  if (!data_buffer_) return 0;
  Assert(result_buffer_);

  size_t data_len = data_buffer_->Used();
  if (data_len < sizeof(uint32_t)) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               fmt("Bool: data too short for header (got %zu, need %zu)",
                   data_len, sizeof(uint32_t)));
  }

  const char *ptr = data_buffer_->GetBuffer();

  /* read header: num_elements as raw uint32 (no struct padding risk) */
  uint32_t num_elements;
  memcpy(&num_elements, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  if (num_elements == 0) return 0;

  /* output: 1 byte per bool */
  size_t output_size = num_elements;
  if (result_buffer_->Capacity() < output_size) {
    result_buffer_->ReSize(output_size);
  }

  /* decompress simple8b-rle packed booleans */
  size_t s8b_size = data_buffer_->Used() - sizeof(uint32_t);
  if (s8b_size < sizeof(uint32_t) * 2) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               fmt("Bool: Simple8b payload too short (got %zu)", s8b_size));
  }
  Simple8bRleDecompressor decompressor;
  decompressor.Init(ptr, s8b_size);

  for (uint32_t i = 0; i < num_elements; i++) {
    uint64_t val = decompressor.Next();
    uint8_t bool_val = val ? 1 : 0;
    result_buffer_->Write(reinterpret_cast<char *>(&bool_val), 1);
    result_buffer_->Brush(1);
  }

  Assert(result_buffer_->Used() == output_size);
  return result_buffer_->Used();
}

size_t PaxBoolDecoder::Decoding(const char * /*not_null*/,
                                size_t /*not_null_len*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

}  // namespace pax
