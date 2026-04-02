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
 * pax_deltadelta_encoding.cc
 *
 *   Double-delta integer compression implementation.
 *   Algorithm: delta-of-deltas + zigzag encoding + Simple8b-RLE.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_deltadelta_encoding.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_deltadelta_encoding.h"

#include <cstring>

#include "storage/columns/pax_simple8b_rle.h"

namespace pax {

/* ZigZag encode: signed -> unsigned */
static inline uint64_t ZigZagEncode(int64_t v) {
  return static_cast<uint64_t>((v << 1) ^ (v >> 63));
}

/* ZigZag decode: unsigned -> signed */
static inline int64_t ZigZagDecode(uint64_t v) {
  return static_cast<int64_t>((v >> 1) ^ -(static_cast<int64_t>(v) & 1));
}

/* Read a value of elem_size bytes and sign-extend to int64 */
int64_t PaxDeltaDeltaEncoder::ReadSignExtended(const char *data,
                                                size_t elem_size) {
  switch (elem_size) {
    case 1: {
      int8_t v;
      memcpy(&v, data, 1);
      return static_cast<int64_t>(v);
    }
    case 2: {
      int16_t v;
      memcpy(&v, data, 2);
      return static_cast<int64_t>(v);
    }
    case 4: {
      int32_t v;
      memcpy(&v, data, 4);
      return static_cast<int64_t>(v);
    }
    case 8: {
      int64_t v;
      memcpy(&v, data, 8);
      return v;
    }
    default:
      CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError,
                 fmt("DeltaDelta: unsupported element size %zu", elem_size));
  }
}

/* ---- Encoder ---- */

PaxDeltaDeltaEncoder::PaxDeltaDeltaEncoder(
    const EncodingOption &encoder_options)
    : PaxEncoder(encoder_options) {}

void PaxDeltaDeltaEncoder::Append(char *data, size_t size) {
  Assert(size > 0);

  if (element_size_ == 0) {
    /* first call: detect element size */
    element_size_ = static_cast<uint8_t>(size);
  }

  /* supports both per-element and bulk append */
  size_t count = size / element_size_;
  for (size_t i = 0; i < count; i++) {
    values_.push_back(ReadSignExtended(data + i * element_size_, element_size_));
  }
}

bool PaxDeltaDeltaEncoder::SupportAppendNull() const {
  return false;
}

void PaxDeltaDeltaEncoder::Flush() {
  if (values_.empty()) return;

  /* Guard against double-Flush: a second Flush would append data to the
   * buffer, but the decoder only reads one encoded block from the start,
   * silently losing the second batch. */
  Assert(result_buffer_->Used() == 0);

  Assert(values_.size() <= UINT32_MAX);
  uint32_t num_elements = static_cast<uint32_t>(values_.size());

  /* compute delta-of-deltas and zigzag encode
   * Use unsigned arithmetic to avoid signed integer overflow UB.
   * The uint64_t subtraction produces the correct two's complement
   * bit pattern, which is then reinterpreted as int64_t for zigzag. */
  Simple8bRleCompressor compressor;
  if (num_elements > 1) {
    int64_t prev_delta = static_cast<int64_t>(
        static_cast<uint64_t>(values_[1]) - static_cast<uint64_t>(values_[0]));
    compressor.Append(ZigZagEncode(prev_delta));

    for (uint32_t i = 2; i < num_elements; i++) {
      int64_t delta = static_cast<int64_t>(
          static_cast<uint64_t>(values_[i]) -
          static_cast<uint64_t>(values_[i - 1]));
      int64_t delta_of_delta = static_cast<int64_t>(
          static_cast<uint64_t>(delta) - static_cast<uint64_t>(prev_delta));
      compressor.Append(ZigZagEncode(delta_of_delta));
      prev_delta = delta;
    }
  }
  auto [s8b_data, s8b_size] = compressor.Finish();

  /* compute header */
  DeltaDeltaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = num_elements;
  header.element_size = element_size_;
  header.first_value = values_[0];

  size_t total_size = sizeof(DeltaDeltaHeader) + s8b_size;

  if (result_buffer_->Capacity() < total_size) {
    result_buffer_->ReSize(total_size);
  }

  /* write header */
  result_buffer_->Write(reinterpret_cast<char *>(&header), sizeof(header));
  result_buffer_->Brush(sizeof(header));

  /* write simple8b-rle data */
  if (s8b_size > 0) {
    result_buffer_->Write(const_cast<char *>(s8b_data), s8b_size);
    result_buffer_->Brush(s8b_size);
  }

  values_.clear();
}

size_t PaxDeltaDeltaEncoder::GetBoundSize(size_t src_len) const {
  /* worst case: header + one 64-bit word per element */
  return sizeof(DeltaDeltaHeader) + src_len + 256;
}

/* ---- Decoder ---- */

template <typename T>
PaxDeltaDeltaDecoder<T>::PaxDeltaDeltaDecoder(
    const PaxDecoder::DecodingOption &decoder_options)
    : PaxDecoder(decoder_options),
      data_buffer_(nullptr),
      result_buffer_(nullptr) {}

template <typename T>
PaxDecoder *PaxDeltaDeltaDecoder<T>::SetSrcBuffer(char *data,
                                                    size_t data_len) {
  if (data) {
    data_buffer_ =
        std::make_shared<DataBuffer<char>>(data, data_len, false, false);
    data_buffer_->Brush(data_len);
  }
  return this;
}

template <typename T>
PaxDecoder *PaxDeltaDeltaDecoder<T>::SetDataBuffer(
    std::shared_ptr<DataBuffer<char>> result_buffer) {
  result_buffer_ = std::move(result_buffer);
  return this;
}

template <typename T>
const char *PaxDeltaDeltaDecoder<T>::GetBuffer() const {
  return result_buffer_ ? result_buffer_->GetBuffer() : nullptr;
}

template <typename T>
size_t PaxDeltaDeltaDecoder<T>::GetBufferSize() const {
  return result_buffer_ ? result_buffer_->Used() : 0;
}

template <typename T>
size_t PaxDeltaDeltaDecoder<T>::Next(const char * /*not_null*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

template <typename T>
size_t PaxDeltaDeltaDecoder<T>::Decoding() {
  if (!data_buffer_) return 0;
  Assert(result_buffer_);

  size_t data_len = data_buffer_->Used();
  if (data_len < sizeof(DeltaDeltaHeader)) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               fmt("DeltaDelta: data too short for header (got %zu, need %zu)",
                   data_len, sizeof(DeltaDeltaHeader)));
  }

  const char *ptr = data_buffer_->GetBuffer();

  /* read header */
  DeltaDeltaHeader header;
  memcpy(&header, ptr, sizeof(header));
  ptr += sizeof(header);

  uint32_t num_elements = header.num_elements;
  if (num_elements == 0) return 0;

  /* ensure output buffer is large enough */
  size_t output_size = num_elements * sizeof(T);
  if (result_buffer_->Capacity() < output_size) {
    result_buffer_->ReSize(output_size);
  }

  /* write first value — narrow from int64 to T */
  T first_val = static_cast<T>(header.first_value);
  result_buffer_->Write(reinterpret_cast<char *>(&first_val), sizeof(T));
  result_buffer_->Brush(sizeof(T));

  if (num_elements == 1) {
    return result_buffer_->Used();
  }

  /* decompress simple8b-rle delta-of-deltas */
  size_t s8b_size = data_buffer_->Used() - sizeof(DeltaDeltaHeader);
  if (s8b_size < sizeof(uint32_t) * 2) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               fmt("DeltaDelta: Simple8b payload too short (got %zu)", s8b_size));
  }
  Simple8bRleDecompressor decompressor;
  decompressor.Init(ptr, s8b_size);

  /* reconstruct: first delta is zigzag-decoded from first s8b value
   * Use unsigned arithmetic to avoid signed overflow UB. */
  int64_t prev_value = header.first_value;
  int64_t prev_delta = ZigZagDecode(decompressor.Next());
  int64_t current_value = static_cast<int64_t>(
      static_cast<uint64_t>(prev_value) + static_cast<uint64_t>(prev_delta));
  T out_val = static_cast<T>(current_value);
  result_buffer_->Write(reinterpret_cast<char *>(&out_val), sizeof(T));
  result_buffer_->Brush(sizeof(T));

  /* remaining values: delta_of_delta -> delta -> value */
  for (uint32_t i = 2; i < num_elements; i++) {
    int64_t delta_of_delta = ZigZagDecode(decompressor.Next());
    int64_t delta = static_cast<int64_t>(
        static_cast<uint64_t>(prev_delta) +
        static_cast<uint64_t>(delta_of_delta));
    current_value = static_cast<int64_t>(
        static_cast<uint64_t>(current_value) + static_cast<uint64_t>(delta));
    out_val = static_cast<T>(current_value);
    result_buffer_->Write(reinterpret_cast<char *>(&out_val), sizeof(T));
    result_buffer_->Brush(sizeof(T));
    prev_delta = delta;
  }

  Assert(result_buffer_->Used() == output_size);
  return result_buffer_->Used();
}

template <typename T>
size_t PaxDeltaDeltaDecoder<T>::Decoding(const char * /*not_null*/,
                                          size_t /*not_null_len*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

/* explicit template instantiations
 * Instantiate for both long and long long to ensure portability:
 * on Linux x86_64 int64_t = long, on macOS ARM64 int64_t = long long.
 * C++ treats long and long long as distinct types even when same size.
 */
template class PaxDeltaDeltaDecoder<long>;
template class PaxDeltaDeltaDecoder<long long>;
template class PaxDeltaDeltaDecoder<int>;
template class PaxDeltaDeltaDecoder<short>;
template class PaxDeltaDeltaDecoder<signed char>;

}  // namespace pax
