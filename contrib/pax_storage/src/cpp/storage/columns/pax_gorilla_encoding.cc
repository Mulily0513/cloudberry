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
 * pax_gorilla_encoding.cc
 *
 *   Gorilla XOR-based float compression implementation.
 *   Based on: Pelkonen et al., "Gorilla: A Fast, Scalable, In-Memory
 *   Time Series Database", VLDB 2015, Section 4.1.2.
 *
 *   XOR arithmetic always uses 64-bit values. Elements are zero-extended
 *   to uint64 on encoding and truncated back to sizeof(T) on decoding.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_gorilla_encoding.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_gorilla_encoding.h"

#include <cstring>

#include "storage/columns/pax_bit_array.h"
#include "storage/columns/pax_simple8b_rle.h"

/*
 * Endian-aware helpers for zero-extending sub-64-bit values to uint64
 * and truncating back. On little-endian (x86), memcpy to/from low bytes
 * is correct. On big-endian, the low bytes of a uint64 are at the end,
 * so we need an offset.
 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PAX_IS_BIG_ENDIAN 1
#else
#define PAX_IS_BIG_ENDIAN 0
#endif

namespace pax {

/* count leading zeros, returning 64 for zero input */
static inline uint8_t CountLeadingZeros64(uint64_t v) {
  if (v == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return static_cast<uint8_t>(__builtin_clzll(v));
#else
  uint8_t n = 0;
  if ((v >> 32) == 0) { n += 32; v <<= 32; }
  if ((v >> 48) == 0) { n += 16; v <<= 16; }
  if ((v >> 56) == 0) { n += 8;  v <<= 8;  }
  if ((v >> 60) == 0) { n += 4;  v <<= 4;  }
  if ((v >> 62) == 0) { n += 2;  v <<= 2;  }
  if ((v >> 63) == 0) { n += 1; }
  return n;
#endif
}

/* count trailing zeros, returning 64 for zero input */
static inline uint8_t CountTrailingZeros64(uint64_t v) {
  if (v == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return static_cast<uint8_t>(__builtin_ctzll(v));
#else
  uint8_t n = 0;
  if ((v & 0xFFFFFFFF) == 0) { n += 32; v >>= 32; }
  if ((v & 0xFFFF) == 0)     { n += 16; v >>= 16; }
  if ((v & 0xFF) == 0)       { n += 8;  v >>= 8;  }
  if ((v & 0xF) == 0)        { n += 4;  v >>= 4;  }
  if ((v & 0x3) == 0)        { n += 2;  v >>= 2;  }
  if ((v & 0x1) == 0)        { n += 1; }
  return n;
#endif
}

/* helper: write a length-prefixed section to result_buffer_ */
static void WriteLengthPrefixedSection(
    std::shared_ptr<DataBuffer<char>> &buf, const char *data, size_t size) {
  uint32_t sz = static_cast<uint32_t>(size);
  buf->Write(reinterpret_cast<char *>(&sz), sizeof(uint32_t));
  buf->Brush(sizeof(uint32_t));
  if (size > 0) {
    buf->Write(const_cast<char *>(data), size);
    buf->Brush(size);
  }
}

/* helper: read a length-prefixed section, advance ptr with bounds check */
static std::pair<const char *, size_t> ReadLengthPrefixedSection(
    const char *&ptr, const char *end) {
  if (ptr + sizeof(uint32_t) > end) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               "Gorilla: truncated length prefix");
  }
  uint32_t sz;
  memcpy(&sz, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  if (ptr + sz > end) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               "Gorilla: section exceeds data boundary");
  }
  const char *data = ptr;
  ptr += sz;
  return {data, sz};
}

/* ---- Encoder ---- */

PaxGorillaEncoder::PaxGorillaEncoder(const EncodingOption &encoder_options)
    : PaxEncoder(encoder_options) {}

void PaxGorillaEncoder::Append(char *data, size_t size) {
  Assert(size > 0);

  if (element_size_ == 0) {
    /* first call: detect element size */
    element_size_ = static_cast<uint8_t>(size);
  }

  /* supports both per-element and bulk append */
  size_t count = size / element_size_;
  for (size_t i = 0; i < count; i++) {
    uint64_t bits = 0;
    /* on big-endian, copy into the low bytes (high address) of uint64 */
    size_t offset = PAX_IS_BIG_ENDIAN ? (sizeof(uint64_t) - element_size_) : 0;
    memcpy(reinterpret_cast<char *>(&bits) + offset,
           data + i * element_size_, element_size_);
    values_.push_back(bits);
  }
}

bool PaxGorillaEncoder::SupportAppendNull() const {
  return false;
}

void PaxGorillaEncoder::Flush() {
  if (values_.empty()) return;

  /* Guard against double-Flush: a second Flush would append data to the
   * buffer, but the decoder only reads one encoded block from the start,
   * silently losing the second batch. */
  Assert(result_buffer_->Used() == 0);

  Assert(values_.size() <= UINT32_MAX);
  uint32_t num_elements = static_cast<uint32_t>(values_.size());

  /*
   * Gorilla XOR encoding — always use 64-bit arithmetic.
   * For sub-64-bit types, the upper bits are always zero,
   * which means leading zeros will be >= (64 - type_bits).
   * This is handled correctly by the 64-bit XOR arithmetic.
   */
  Simple8bRleCompressor tag0_comp;
  Simple8bRleCompressor tag1_comp;
  BitArray leading_zeros_arr;
  Simple8bRleCompressor num_bits_comp;
  BitArray xor_arr;

  uint8_t prev_leading = 0;
  uint8_t prev_num_bits = 0;

  for (uint32_t i = 1; i < num_elements; i++) {
    uint64_t xor_val = values_[i] ^ values_[i - 1];

    if (xor_val == 0) {
      tag0_comp.Append(0);
    } else {
      tag0_comp.Append(1);

      uint8_t leading = CountLeadingZeros64(xor_val);
      uint8_t trailing = CountTrailingZeros64(xor_val);
      uint8_t meaningful_bits = 64 - leading - trailing;

      if (i > 1 && leading >= prev_leading &&
          (leading + meaningful_bits) <=
              (prev_leading + prev_num_bits)) {
        /* reuse previous window: tag1 = 0 */
        tag1_comp.Append(0);
        /* extract meaningful bits using prev window */
        uint8_t shift = 64 - prev_leading - prev_num_bits;
        uint64_t meaningful = (xor_val >> shift);
        if (prev_num_bits < 64) {
          meaningful &= ((1ULL << prev_num_bits) - 1);
        }
        xor_arr.AppendBits(meaningful, prev_num_bits);
      } else {
        /* new window: tag1 = 1 */
        tag1_comp.Append(1);
        /* leading stored in 6 bits (max 63); clamp since
         * xor_val != 0 means leading <= 63 */
        leading_zeros_arr.AppendBits(leading, 6);
        num_bits_comp.Append(meaningful_bits);

        /* extract meaningful bits */
        uint64_t meaningful = (xor_val >> trailing);
        if (meaningful_bits < 64) {
          meaningful &= ((1ULL << meaningful_bits) - 1);
        }
        xor_arr.AppendBits(meaningful, meaningful_bits);

        prev_leading = leading;
        prev_num_bits = meaningful_bits;
      }
    }
  }

  /* serialize all components */
  auto [tag0_data, tag0_size] = tag0_comp.Finish();
  auto [tag1_data, tag1_size] = tag1_comp.Finish();
  auto [nb_data, nb_size] = num_bits_comp.Finish();

  /* serialize BitArrays to temp buffers */
  size_t lz_ser_size = leading_zeros_arr.DataSize();
  size_t xor_ser_size = xor_arr.DataSize();
  std::vector<char> lz_buf(lz_ser_size);
  std::vector<char> xor_buf(xor_ser_size);
  leading_zeros_arr.Serialize(lz_buf.data());
  xor_arr.Serialize(xor_buf.data());

  /* compute total size */
  size_t total_size = sizeof(GorillaHeader) +
                      sizeof(uint32_t) + tag0_size +
                      sizeof(uint32_t) + tag1_size +
                      sizeof(uint32_t) + lz_ser_size +
                      sizeof(uint32_t) + nb_size +
                      sizeof(uint32_t) + xor_ser_size;

  if (result_buffer_->Capacity() < total_size) {
    result_buffer_->ReSize(total_size);
  }

  /* write header */
  GorillaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = num_elements;
  header.element_size = element_size_;
  header.first_value = values_[0];

  result_buffer_->Write(reinterpret_cast<char *>(&header), sizeof(header));
  result_buffer_->Brush(sizeof(header));

  /* write sections with length prefixes */
  WriteLengthPrefixedSection(result_buffer_, tag0_data, tag0_size);
  WriteLengthPrefixedSection(result_buffer_, tag1_data, tag1_size);
  WriteLengthPrefixedSection(result_buffer_, lz_buf.data(), lz_ser_size);
  WriteLengthPrefixedSection(result_buffer_, nb_data, nb_size);
  WriteLengthPrefixedSection(result_buffer_, xor_buf.data(), xor_ser_size);

  values_.clear();
}

size_t PaxGorillaEncoder::GetBoundSize(size_t src_len) const {
  /* worst case: header + 5 sections each with overhead */
  return sizeof(GorillaHeader) + src_len * 2 + 256;
}

/* ---- Decoder ---- */

template <typename T>
PaxGorillaDecoder<T>::PaxGorillaDecoder(
    const PaxDecoder::DecodingOption &decoder_options)
    : PaxDecoder(decoder_options),
      data_buffer_(nullptr),
      result_buffer_(nullptr) {}

template <typename T>
PaxDecoder *PaxGorillaDecoder<T>::SetSrcBuffer(char *data, size_t data_len) {
  if (data) {
    data_buffer_ =
        std::make_shared<DataBuffer<char>>(data, data_len, false, false);
    data_buffer_->Brush(data_len);
  }
  return this;
}

template <typename T>
PaxDecoder *PaxGorillaDecoder<T>::SetDataBuffer(
    std::shared_ptr<DataBuffer<char>> result_buffer) {
  result_buffer_ = std::move(result_buffer);
  return this;
}

template <typename T>
const char *PaxGorillaDecoder<T>::GetBuffer() const {
  return result_buffer_ ? result_buffer_->GetBuffer() : nullptr;
}

template <typename T>
size_t PaxGorillaDecoder<T>::GetBufferSize() const {
  return result_buffer_ ? result_buffer_->Used() : 0;
}

template <typename T>
size_t PaxGorillaDecoder<T>::Next(const char * /*not_null*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

template <typename T>
size_t PaxGorillaDecoder<T>::Decoding() {
  if (!data_buffer_) return 0;
  Assert(result_buffer_);

  size_t data_len = data_buffer_->Used();
  if (data_len < sizeof(GorillaHeader)) {
    CBDB_RAISE(cbdb::CException::kExTypeLogicError,
               fmt("Gorilla: data too short for header (got %zu, need %zu)",
                   data_len, sizeof(GorillaHeader)));
  }

  const char *ptr = data_buffer_->GetBuffer();
  const char *end = ptr + data_len;

  /* read header */
  GorillaHeader header;
  memcpy(&header, ptr, sizeof(header));
  ptr += sizeof(header);

  uint32_t num_elements = header.num_elements;
  if (num_elements == 0) return 0;

  size_t output_size = num_elements * sizeof(T);
  if (result_buffer_->Capacity() < output_size) {
    result_buffer_->ReSize(output_size);
  }

  /* write first value — truncate from uint64 to T */
  T first_val;
  /* on big-endian, the low bytes are at high address */
  size_t be_offset = PAX_IS_BIG_ENDIAN ? (sizeof(uint64_t) - sizeof(T)) : 0;
  memcpy(&first_val,
         reinterpret_cast<const char *>(&header.first_value) + be_offset,
         sizeof(T));
  result_buffer_->Write(reinterpret_cast<char *>(&first_val), sizeof(T));
  result_buffer_->Brush(sizeof(T));

  if (num_elements == 1) {
    return result_buffer_->Used();
  }

  /* read length-prefixed sections */
  auto [tag0_data, tag0_size] = ReadLengthPrefixedSection(ptr, end);
  auto [tag1_data, tag1_size] = ReadLengthPrefixedSection(ptr, end);
  auto [lz_data, lz_size] = ReadLengthPrefixedSection(ptr, end);
  auto [nb_data, nb_size] = ReadLengthPrefixedSection(ptr, end);
  auto [xor_data, xor_size] = ReadLengthPrefixedSection(ptr, end);

  /* init decompressors */
  Simple8bRleDecompressor tag0_decomp;
  tag0_decomp.Init(tag0_data, tag0_size);

  Simple8bRleDecompressor tag1_decomp;
  tag1_decomp.Init(tag1_data, tag1_size);

  BitArrayIterator lz_iter(lz_data, lz_size);

  Simple8bRleDecompressor nb_decomp;
  nb_decomp.Init(nb_data, nb_size);

  BitArrayIterator xor_iter(xor_data, xor_size);

  /*
   * Always use 64-bit XOR arithmetic for reconstruction,
   * matching the encoder which always uses 64-bit values.
   */
  uint64_t prev_bits = header.first_value;
  uint8_t prev_leading = 0;
  uint8_t prev_num_bits = 0;

  for (uint32_t i = 1; i < num_elements; i++) {
    uint64_t tag0 = tag0_decomp.Next();

    uint64_t current_bits;
    if (tag0 == 0) {
      /* XOR is 0, value is same as previous */
      current_bits = prev_bits;
    } else {
      uint64_t tag1 = tag1_decomp.Next();

      uint8_t leading, meaningful_bits;
      if (tag1 == 0) {
        /* reuse previous window */
        leading = prev_leading;
        meaningful_bits = prev_num_bits;
      } else {
        /* new window */
        leading = static_cast<uint8_t>(lz_iter.ReadBits(6));
        meaningful_bits = static_cast<uint8_t>(nb_decomp.Next());
        prev_leading = leading;
        prev_num_bits = meaningful_bits;
      }

      /* Validate bit layout — corrupted data could produce out-of-range
       * values that cause undefined shift behavior. Use CBDB_RAISE (not
       * Assert) so this check survives release builds. */
      if (leading + meaningful_bits > 64) {
        CBDB_RAISE(cbdb::CException::kExTypeLogicError,
                   fmt("Gorilla: leading(%u) + meaningful_bits(%u) > 64",
                       (unsigned)leading, (unsigned)meaningful_bits));
      }
      uint64_t meaningful = xor_iter.ReadBits(meaningful_bits);
      uint8_t shift = 64 - leading - meaningful_bits;
      uint64_t xor_val = meaningful << shift;
      current_bits = prev_bits ^ xor_val;
    }

    /* truncate from uint64 to T — on big-endian, low bytes at high address */
    T out_val;
    memcpy(&out_val,
           reinterpret_cast<const char *>(&current_bits) + be_offset,
           sizeof(T));
    result_buffer_->Write(reinterpret_cast<char *>(&out_val), sizeof(T));
    result_buffer_->Brush(sizeof(T));
    prev_bits = current_bits;
  }

  Assert(result_buffer_->Used() == output_size);
  return result_buffer_->Used();
}

template <typename T>
size_t PaxGorillaDecoder<T>::Decoding(const char * /*not_null*/,
                                       size_t /*not_null_len*/) {
  CBDB_RAISE(cbdb::CException::kExTypeUnImplements);
}

/* explicit template instantiations
 * Instantiate for both long and long long to ensure portability:
 * on Linux x86_64 int64_t = long, on macOS ARM64 int64_t = long long.
 * C++ treats long and long long as distinct types even when same size.
 */
template class PaxGorillaDecoder<long>;
template class PaxGorillaDecoder<long long>;
template class PaxGorillaDecoder<int>;
template class PaxGorillaDecoder<short>;
template class PaxGorillaDecoder<signed char>;

}  // namespace pax
