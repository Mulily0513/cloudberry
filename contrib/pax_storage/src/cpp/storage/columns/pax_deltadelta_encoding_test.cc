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
 * pax_deltadelta_encoding_test.cc
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_deltadelta_encoding_test.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_deltadelta_encoding.h"

#include <cstring>
#include <random>
#include <vector>

#include "comm/gtest_wrappers.h"
#include "pax_gtest_helper.h"

namespace pax {

class PaxDeltaDeltaEncodingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    encoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_DELTA_DELTA;
    encoding_options_.is_sign = true;

    decoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_DELTA_DELTA;
    decoding_options_.is_sign = true;
  }

  /* Encode per-element (mimics PaxEncodingColumn behavior) then decode */
  template <typename T>
  std::vector<T> EncodeAndDecode(const std::vector<T> &input) {
    PaxDeltaDeltaEncoder encoder(encoding_options_);

    size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(T));
    encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

    /* Append per-element, matching PaxEncodingColumn::Append pattern */
    for (size_t i = 0; i < input.size(); i++) {
      T val = input[i];
      encoder.Append(reinterpret_cast<char *>(&val), sizeof(T));
    }
    encoder.Flush();

    const char *encoded_data = encoder.GetBuffer();
    size_t encoded_size = encoder.GetBufferSize();
    EXPECT_GT(encoded_size, 0u);

    /* Decode */
    PaxDeltaDeltaDecoder<T> decoder(decoding_options_);
    decoder.SetSrcBuffer(const_cast<char *>(encoded_data), encoded_size);

    auto result_buffer =
        std::make_shared<DataBuffer<char>>(input.size() * sizeof(T));
    decoder.SetDataBuffer(result_buffer);

    size_t decoded_size = decoder.Decoding();
    EXPECT_EQ(decoded_size, input.size() * sizeof(T));

    const T *decoded_data = reinterpret_cast<const T *>(decoder.GetBuffer());
    size_t count = decoded_size / sizeof(T);

    return std::vector<T>(decoded_data, decoded_data + count);
  }

  PaxEncoder::EncodingOption encoding_options_;
  PaxDecoder::DecodingOption decoding_options_;
};

/* ---- int64 tests ---- */

TEST_F(PaxDeltaDeltaEncodingTest, Int64BasicSequence) {
  std::vector<long> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64SingleValue) {
  std::vector<long> input = {42};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64TwoValues) {
  std::vector<long> input = {100, 200};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64ConstantDelta) {
  /* Simulates equi-spaced timestamps: delta-of-delta = 0 */
  std::vector<long> input;
  for (long i = 0; i < 1000; i++) {
    input.push_back(1000000 + i * 10);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64AllSameValues) {
  std::vector<long> input(500, 42);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64NegativeValues) {
  std::vector<long> input = {-100, -50, 0, 50, 100};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64DecreasingSequence) {
  std::vector<long> input = {1000, 900, 800, 700, 600, 500};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64LargeValues) {
  /* Timestamp-like microsecond values */
  std::vector<long> input;
  long base = 1451606400000000L; /* 2016-01-01 00:00:00 in microseconds */
  for (long i = 0; i < 500; i++) {
    input.push_back(base + i * 10000000L); /* 10-second intervals */
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64Random) {
  std::mt19937 gen(12345);
  std::uniform_int_distribution<long> dis(-1000000, 1000000);

  std::vector<long> input;
  for (int i = 0; i < 1000; i++) {
    input.push_back(dis(gen));
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64MixedPattern) {
  std::vector<long> input = {10, 20, 15, 25, 5, 30, 1, 35, -10, 100};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- int32 tests ---- */

TEST_F(PaxDeltaDeltaEncodingTest, Int32BasicSequence) {
  std::vector<int> input = {1, 2, 3, 4, 5, 6, 7, 8};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int32ConstantDelta) {
  std::vector<int> input;
  for (int i = 0; i < 500; i++) {
    input.push_back(100 + i * 7);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int32NegativeValues) {
  std::vector<int> input = {-100, -50, -10, 0, 10, 50, 100};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- int16 tests ---- */

TEST_F(PaxDeltaDeltaEncodingTest, Int16BasicSequence) {
  std::vector<short> input = {10, 20, 30, 40, 50};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int16NegativeValues) {
  std::vector<short> input = {-100, -50, 0, 50, 100};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- Compression ratio check ---- */

TEST_F(PaxDeltaDeltaEncodingTest, CompressionRatioConstantDelta) {
  /* Equi-spaced sequence: delta-of-delta = 0, should compress extremely well */
  PaxDeltaDeltaEncoder encoder(encoding_options_);
  std::vector<long> input;
  for (long i = 0; i < 10000; i++) {
    input.push_back(1000000 + i * 10);
  }

  size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(long));
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  for (size_t i = 0; i < input.size(); i++) {
    long val = input[i];
    encoder.Append(reinterpret_cast<char *>(&val), sizeof(long));
  }
  encoder.Flush();

  size_t raw_size = input.size() * sizeof(long);
  size_t encoded_size = encoder.GetBufferSize();

  /* Constant-delta sequence should compress to < 5% of original */
  double ratio = static_cast<double>(raw_size) / encoded_size;
  EXPECT_GT(ratio, 20.0)
      << "Expected >20x compression for constant-delta, got " << ratio << "x"
      << " (raw=" << raw_size << ", encoded=" << encoded_size << ")";
}

/* ================================================================
 * Tests inspired by TimescaleDB compression_unit_test.c
 * (test_delta2, test_delta3, test_delta4 patterns)
 * ================================================================ */

/* test_delta2 equivalent: alternating delta prevents full RLE */
TEST_F(PaxDeltaDeltaEncodingTest, Int64AlternatingDelta) {
  std::vector<long> input;
  for (long i = 0; i < 1015; i++) {
    if (i % 2 != 0)
      input.push_back(2 * i);
    else
      input.push_back(i);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* test_delta3 equivalent: pseudo-random with equal stretches */
static uint64_t test_hash64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

TEST_F(PaxDeltaDeltaEncodingTest, Int64RandomWithEqualStretches) {
  /* Hash-based pseudo-random with intentional equal-value stretches
   * at i%37<4 and i%53<2 positions */
  std::vector<long> input;
  for (int i = 0; i < 1015; i++) {
    int base = i;
    if (i % 37 < 4)
      base = 1;
    else if (i % 53 < 2)
      base = 2;
    input.push_back(static_cast<long>(test_hash64(base)));
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* test_delta4 equivalent: big deltas causing zigzag overflow */
TEST_F(PaxDeltaDeltaEncodingTest, Int32BigDeltas) {
  /* Values with very large deltas that test zigzag encoding limits */
  std::vector<int> input = {-603979776, 1462059044};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int32RepeatingPatternBigDeltas) {
  /* Pattern from TSDB test_delta4_case2: repeating hex patterns
   * with large delta-of-deltas between groups */
  std::vector<int> input = {
      0x7979fd07,
      0x79797979, 0x79797979, 0x79797979, 0x79797979,
      0x79797979, 0x79797979, 0x79797979, 0x79797979,
      0x79797979, 0x79797979, 0x79797979, 0x79797979,
      0x79797979, 0x79797979,
      0x50505050,
      static_cast<int>(0xc4c4c4c4), static_cast<int>(0xc4c4c4c4),
      0x50505050, 0x50505050,
      static_cast<int>(0xc4c4c4c4),
  };
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* Extreme boundary values — TSDB compression_algos.sql "really big deltas" */
TEST_F(PaxDeltaDeltaEncodingTest, Int64ExtremeDeltas) {
  long big_max = INT64_MAX;
  long big_min = INT64_MIN;
  std::vector<long> input = {
      /* big deltas */
      0, big_max, big_min, big_max, big_min,
      0, big_min, 32, 5, big_min, -52, big_max,
      1000,
      /* big delta_deltas */
      0, big_max, big_max, big_min, big_min, big_max, big_max,
      0, big_max - 1, big_max - 1, big_min, big_min, big_max - 1, big_max - 1,
  };
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* int8 (signed char) type test — smallest supported integer width */
TEST_F(PaxDeltaDeltaEncodingTest, Int8BasicSequence) {
  std::vector<signed char> input;
  for (signed char i = -100; i < 100; i++) {
    input.push_back(i);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxDeltaDeltaEncodingTest, Int8BoundaryValues) {
  std::vector<signed char> input = {-128, -127, 0, 126, 127, -128, 127};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* 1015 elements — same count as TSDB TEST_ELEMENTS */
TEST_F(PaxDeltaDeltaEncodingTest, Int64Sequence1015) {
  std::vector<long> input;
  for (long i = 0; i < 1015; i++) {
    input.push_back(i);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* Large random dataset */
TEST_F(PaxDeltaDeltaEncodingTest, Int64RandomLarge) {
  std::mt19937_64 gen(42);
  std::uniform_int_distribution<long> dis(INT64_MIN, INT64_MAX);

  std::vector<long> input;
  for (int i = 0; i < 10000; i++) {
    input.push_back(dis(gen));
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P0: Double-Flush — verify values_.clear() after Flush()
 * After Flush(), calling Flush() again should be a no-op because
 * values_ was cleared. The output buffer should not grow.
 * ================================================================ */

TEST_F(PaxDeltaDeltaEncodingTest, DoubleFlushIsNoop) {
  PaxDeltaDeltaEncoder encoder(encoding_options_);

  std::vector<long> input = {1, 2, 3, 4, 5};
  size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(long));
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  for (auto &v : input) {
    encoder.Append(reinterpret_cast<char *>(&v), sizeof(long));
  }
  encoder.Flush();
  size_t size_after_first_flush = encoder.GetBufferSize();
  EXPECT_GT(size_after_first_flush, 0u);

  /* Second Flush with no new data — should be a no-op */
  encoder.Flush();
  size_t size_after_second_flush = encoder.GetBufferSize();
  EXPECT_EQ(size_after_first_flush, size_after_second_flush);

  /* Verify data is still correct */
  PaxDeltaDeltaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(const_cast<char *>(encoder.GetBuffer()),
                       size_after_first_flush);
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(input.size() * sizeof(long));
  decoder.SetDataBuffer(result_buffer);
  decoder.Decoding();
  const long *output = reinterpret_cast<const long *>(decoder.GetBuffer());
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Mismatch at index " << i;
  }
}

/* ================================================================
 * P1: Bulk Append — multi-element Append call after element_size is set.
 * First Append must be per-element to establish element_size_.
 * Subsequent calls can pass multiple elements at once.
 * ================================================================ */

TEST_F(PaxDeltaDeltaEncodingTest, BulkAppend) {
  std::vector<long> input = {10, 20, 30, 40, 50, 60, 70, 80};

  PaxDeltaDeltaEncoder encoder(encoding_options_);
  size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(long));
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  /* First element sets element_size_ */
  long first = input[0];
  encoder.Append(reinterpret_cast<char *>(&first), sizeof(long));

  /* Remaining elements in one bulk call */
  encoder.Append(reinterpret_cast<char *>(input.data() + 1),
                 (input.size() - 1) * sizeof(long));
  encoder.Flush();

  const char *encoded_data = encoder.GetBuffer();
  size_t encoded_size = encoder.GetBufferSize();

  PaxDeltaDeltaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(const_cast<char *>(encoded_data), encoded_size);
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(input.size() * sizeof(long));
  decoder.SetDataBuffer(result_buffer);
  decoder.Decoding();

  const long *output = reinterpret_cast<const long *>(decoder.GetBuffer());
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Mismatch at index " << i;
  }
}

/* ================================================================
 * P1: Empty input and edge case tests
 * ================================================================ */

TEST_F(PaxDeltaDeltaEncodingTest, EmptyEncoderFlush) {
  /* Encoder with no Append: Flush is a no-op, buffer stays empty */
  PaxDeltaDeltaEncoder encoder(encoding_options_);
  size_t bound_size = encoder.GetBoundSize(0);
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));
  encoder.Flush();
  EXPECT_EQ(encoder.GetBufferSize(), 0u);
}

TEST_F(PaxDeltaDeltaEncodingTest, DecoderNullSrc) {
  /* Decoder with no src buffer: Decoding returns 0 */
  PaxDeltaDeltaDecoder<long> decoder(decoding_options_);
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

TEST_F(PaxDeltaDeltaEncodingTest, DecoderZeroElements) {
  /* Manually crafted buffer with num_elements=0: decoder returns 0 */
  DeltaDeltaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 0;
  header.element_size = sizeof(long);
  header.first_value = 0;

  PaxDeltaDeltaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

TEST_F(PaxDeltaDeltaEncodingTest, HeaderOnlyOneElement) {
  /* Manually craft a minimal buffer: header with num_elements=1.
   * Decoder only reads first_value from header, no Simple8b data needed. */
  DeltaDeltaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 1;
  header.element_size = sizeof(long);
  header.first_value = 12345;

  PaxDeltaDeltaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(sizeof(long));
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, sizeof(long));

  const long *output = reinterpret_cast<const long *>(decoder.GetBuffer());
  EXPECT_EQ(output[0], 12345L);
}

TEST_F(PaxDeltaDeltaEncodingTest, HeaderOnlyOneElementInt32) {
  /* Same as above but for int32 — verifies int64-to-int32 narrowing */
  DeltaDeltaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 1;
  header.element_size = sizeof(int);
  header.first_value = -42;

  PaxDeltaDeltaDecoder<int> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(sizeof(int));
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, sizeof(int));

  const int *output = reinterpret_cast<const int *>(decoder.GetBuffer());
  EXPECT_EQ(output[0], -42);
}

}  /* namespace pax */
