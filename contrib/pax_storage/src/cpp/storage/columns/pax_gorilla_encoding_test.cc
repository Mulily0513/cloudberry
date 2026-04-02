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
 * pax_gorilla_encoding_test.cc
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_gorilla_encoding_test.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_gorilla_encoding.h"

#include <cmath>
#include <cstring>
#include <random>
#include <vector>

#include "comm/gtest_wrappers.h"
#include "pax_gtest_helper.h"

namespace pax {

class PaxGorillaEncodingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    encoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_GORILLA;
    encoding_options_.is_sign = false;

    decoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_GORILLA;
    decoding_options_.is_sign = false;
  }

  /* Encode per-element then decode, returns decoded values */
  template <typename T>
  std::vector<T> EncodeAndDecode(const std::vector<T> &input) {
    PaxGorillaEncoder encoder(encoding_options_);

    size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(T));
    encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

    for (size_t i = 0; i < input.size(); i++) {
      T val = input[i];
      encoder.Append(reinterpret_cast<char *>(&val), sizeof(T));
    }
    encoder.Flush();

    const char *encoded_data = encoder.GetBuffer();
    size_t encoded_size = encoder.GetBufferSize();
    EXPECT_GT(encoded_size, 0u);

    PaxGorillaDecoder<T> decoder(decoding_options_);
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

/* ---- float8 (double / long) tests ---- */
/* Note: Gorilla operates on bit patterns, so we use long to hold
   the IEEE 754 representation and do bitwise comparison */

TEST_F(PaxGorillaEncodingTest, Float8BasicValues) {
  /* Store double bit patterns as long */
  std::vector<double> doubles = {55.2, 56.1, 55.8, 56.3, 55.9};
  std::vector<long> input(doubles.size());
  for (size_t i = 0; i < doubles.size(); i++) {
    std::memcpy(&input[i], &doubles[i], sizeof(double));
  }

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(input.size(), output.size());

  /* Verify bit-exact roundtrip */
  for (size_t i = 0; i < input.size(); i++) {
    double decoded;
    std::memcpy(&decoded, &output[i], sizeof(double));
    EXPECT_EQ(doubles[i], decoded) << "Mismatch at index " << i;
  }
}

TEST_F(PaxGorillaEncodingTest, Float8SingleValue) {
  double d = 3.14159265358979;
  long val;
  std::memcpy(&val, &d, sizeof(double));
  std::vector<long> input = {val};

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(1u, output.size());

  double decoded;
  std::memcpy(&decoded, &output[0], sizeof(double));
  EXPECT_EQ(d, decoded);
}

TEST_F(PaxGorillaEncodingTest, Float8TwoValues) {
  double d1 = 100.5, d2 = 100.6;
  long v1, v2;
  std::memcpy(&v1, &d1, sizeof(double));
  std::memcpy(&v2, &d2, sizeof(double));
  std::vector<long> input = {v1, v2};

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(2u, output.size());

  double dec1, dec2;
  std::memcpy(&dec1, &output[0], sizeof(double));
  std::memcpy(&dec2, &output[1], sizeof(double));
  EXPECT_EQ(d1, dec1);
  EXPECT_EQ(d2, dec2);
}

TEST_F(PaxGorillaEncodingTest, Float8AllSameValues) {
  double d = 42.0;
  long val;
  std::memcpy(&val, &d, sizeof(double));

  std::vector<long> input(500, val);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float8AllZeros) {
  double d = 0.0;
  long val;
  std::memcpy(&val, &d, sizeof(double));

  std::vector<long> input(100, val);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float8SpecialValues) {
  double inf = std::numeric_limits<double>::infinity();
  double neg_inf = -std::numeric_limits<double>::infinity();
  double max_val = std::numeric_limits<double>::max();
  double min_val = std::numeric_limits<double>::min();

  std::vector<double> doubles = {0.0, inf, neg_inf, max_val, min_val, -0.0};
  std::vector<long> input(doubles.size());
  for (size_t i = 0; i < doubles.size(); i++) {
    std::memcpy(&input[i], &doubles[i], sizeof(double));
  }

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(input.size(), output.size());

  for (size_t i = 0; i < input.size(); i++) {
    /* Bit-exact comparison (handles -0.0 vs 0.0 correctly) */
    EXPECT_EQ(input[i], output[i]) << "Bit mismatch at index " << i;
  }
}

TEST_F(PaxGorillaEncodingTest, Float8SimulatedCpuMetrics) {
  /* Simulates CPU usage metrics: values oscillate around ~55% */
  std::mt19937 gen(42);
  std::normal_distribution<double> dis(55.0, 2.0);

  std::vector<long> input;
  for (int i = 0; i < 1000; i++) {
    double d = dis(gen);
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }

  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float8Random) {
  std::mt19937 gen(12345);
  std::uniform_real_distribution<double> dis(-1e6, 1e6);

  std::vector<long> input;
  for (int i = 0; i < 1000; i++) {
    double d = dis(gen);
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }

  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- float4 (int / 32-bit) tests ---- */

TEST_F(PaxGorillaEncodingTest, Float4BasicValues) {
  std::vector<float> floats = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f};
  std::vector<int> input(floats.size());
  for (size_t i = 0; i < floats.size(); i++) {
    std::memcpy(&input[i], &floats[i], sizeof(float));
  }

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(input.size(), output.size());

  for (size_t i = 0; i < input.size(); i++) {
    float decoded;
    std::memcpy(&decoded, &output[i], sizeof(float));
    EXPECT_EQ(floats[i], decoded) << "Mismatch at index " << i;
  }
}

TEST_F(PaxGorillaEncodingTest, Float4AllSame) {
  float f = 3.14f;
  int val;
  std::memcpy(&val, &f, sizeof(float));

  std::vector<int> input(200, val);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- Compression ratio check ---- */

TEST_F(PaxGorillaEncodingTest, CompressionRatioSimilarValues) {
  /* Similar adjacent values should compress well with Gorilla */
  PaxGorillaEncoder encoder(encoding_options_);

  std::mt19937 gen(42);
  std::normal_distribution<double> dis(100.0, 0.5);

  std::vector<long> input;
  for (int i = 0; i < 10000; i++) {
    double d = dis(gen);
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
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

  /* Similar float values should achieve meaningful compression.
   * Gorilla works best when adjacent values are similar (small XOR).
   * With std::normal_distribution(100.0, 0.5), adjacent values share
   * many bits, so we expect at least 1.2x compression. */
  double ratio = static_cast<double>(raw_size) / encoded_size;
  EXPECT_GT(ratio, 1.2)
      << "Expected >1.2x compression for similar floats, got " << ratio << "x"
      << " (raw=" << raw_size << ", encoded=" << encoded_size << ")";
}

/* ================================================================
 * Tests inspired by TimescaleDB compression_unit_test.c
 * (test_gorilla_int, test_gorilla_float, test_gorilla_double patterns)
 * ================================================================ */

/* test_gorilla_int equivalent: sequential integers stored as gorilla */
TEST_F(PaxGorillaEncodingTest, IntegerAsGorilla) {
  /* Gorilla is typically for floats, but can encode any bit pattern.
   * TSDB test_gorilla_int uses sequential integers 0..1014. */
  std::vector<long> input;
  for (long i = 0; i < 1015; i++) {
    input.push_back(i);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* test_gorilla_float equivalent: sequential floats cast from ints */
TEST_F(PaxGorillaEncodingTest, Float4Sequential) {
  std::vector<int> input;
  for (int i = 0; i < 1015; i++) {
    float f = static_cast<float>(i);
    int val;
    std::memcpy(&val, &f, sizeof(float));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* test_gorilla_double equivalent: sequential doubles */
TEST_F(PaxGorillaEncodingTest, Float8Sequential1015) {
  std::vector<long> input;
  for (long i = 0; i < 1015; i++) {
    double d = static_cast<double>(i);
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* test_gorilla_double(have_random=true) equivalent:
 * hash-based pseudo-random with equal stretches */
static uint64_t gorilla_hash64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

TEST_F(PaxGorillaEncodingTest, Float8RandomWithEqualStretches) {
  /* Hash-based pseudo-random doubles with intentional equal-value stretches
   * at i%37<4 and i%53<2 positions (same as TSDB test pattern) */
  std::vector<long> input;
  for (int i = 0; i < 1015; i++) {
    int base = i;
    if (i % 37 < 4)
      base = 1;
    else if (i % 53 < 2)
      base = 2;
    double d = static_cast<double>(gorilla_hash64(base));
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * Special IEEE 754 values (from TSDB compression_algos.sql)
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, Float8NaN) {
  /* NaN bit patterns must be preserved exactly */
  double nan = std::numeric_limits<double>::quiet_NaN();
  long nan_bits;
  std::memcpy(&nan_bits, &nan, sizeof(double));

  /* Mix NaN with normal values */
  double d1 = 1.0, d2 = 2.0;
  long v1, v2;
  std::memcpy(&v1, &d1, sizeof(double));
  std::memcpy(&v2, &d2, sizeof(double));

  std::vector<long> input = {v1, nan_bits, v2, nan_bits, nan_bits};
  auto output = EncodeAndDecode(input);
  ASSERT_EQ(input.size(), output.size());

  /* NaN bit pattern comparison (NaN != NaN with ==, so compare bits) */
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Bit mismatch at index " << i;
  }
}

TEST_F(PaxGorillaEncodingTest, Float8NegativeZero) {
  /* -0.0 and +0.0 have different bit patterns; both must be preserved */
  double pos_zero = 0.0;
  double neg_zero = -0.0;
  long pz, nz;
  std::memcpy(&pz, &pos_zero, sizeof(double));
  std::memcpy(&nz, &neg_zero, sizeof(double));
  ASSERT_NE(pz, nz); /* Sanity check: different bit patterns */

  std::vector<long> input = {pz, nz, pz, nz, nz, pz};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float4SpecialValues) {
  /* TSDB compression_algos.sql: FLOAT4 special values */
  float inf = std::numeric_limits<float>::infinity();
  float neg_inf = -std::numeric_limits<float>::infinity();
  float nan = std::numeric_limits<float>::quiet_NaN();
  float max_val = std::numeric_limits<float>::max();
  float min_val = std::numeric_limits<float>::min();
  float neg_zero = -0.0f;

  std::vector<float> floats = {0.0f, inf, neg_inf, nan, max_val,
                               min_val, neg_zero, 1.0f, -1.0f};
  std::vector<int> input(floats.size());
  for (size_t i = 0; i < floats.size(); i++) {
    std::memcpy(&input[i], &floats[i], sizeof(float));
  }

  auto output = EncodeAndDecode(input);
  ASSERT_EQ(input.size(), output.size());

  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Bit mismatch at index " << i;
  }
}

/* ================================================================
 * XOR path coverage tests
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, Float8SmallDifferences) {
  /* Values with very small XOR — tests the "reuse previous
   * leading zeros and meaningful bits" path in Gorilla encoding */
  std::vector<long> input;
  double base = 100.0;
  for (int i = 0; i < 200; i++) {
    /* Tiny increments → small XOR → reuse previous block info */
    double d = base + i * 0.001;
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float8LargeXorJumps) {
  /* Alternating large and small values → large XOR each time
   * → always writes new leading_zeros/meaningful_bits */
  std::vector<long> input;
  for (int i = 0; i < 500; i++) {
    double d = (i % 2 == 0) ? 1e-300 : 1e+300;
    long val;
    std::memcpy(&val, &d, sizeof(double));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float8RandomLarge) {
  /* 10000 fully random doubles across the full range */
  std::mt19937_64 gen(42);
  std::vector<long> input;
  for (int i = 0; i < 10000; i++) {
    long val = static_cast<long>(gen());
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Float4Random) {
  /* Random float4 values */
  std::mt19937 gen(12345);
  std::uniform_real_distribution<float> dis(-1e6f, 1e6f);

  std::vector<int> input;
  for (int i = 0; i < 1000; i++) {
    float f = dis(gen);
    int val;
    std::memcpy(&val, &f, sizeof(float));
    input.push_back(val);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ---- Compression ratio check ---- */

TEST_F(PaxGorillaEncodingTest, CompressionRatioAllSame) {
  /* All identical values: XOR = 0, should compress extremely well */
  PaxGorillaEncoder encoder(encoding_options_);

  double d = 42.0;
  long val;
  std::memcpy(&val, &d, sizeof(double));

  std::vector<long> input(10000, val);
  size_t bound_size = encoder.GetBoundSize(input.size() * sizeof(long));
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  for (size_t i = 0; i < input.size(); i++) {
    long v = input[i];
    encoder.Append(reinterpret_cast<char *>(&v), sizeof(long));
  }
  encoder.Flush();

  size_t raw_size = input.size() * sizeof(long);
  size_t encoded_size = encoder.GetBufferSize();

  /* All-same values: XOR all zero, should get >20x compression */
  double ratio = static_cast<double>(raw_size) / encoded_size;
  EXPECT_GT(ratio, 20.0)
      << "Expected >20x compression for all-same values, got " << ratio << "x";
}

/* ================================================================
 * P0: Double-Flush — verify values_.clear() after Flush()
 * After Flush(), calling Flush() again should be a no-op.
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, DoubleFlushIsNoop) {
  PaxGorillaEncoder encoder(encoding_options_);

  double d1 = 1.0, d2 = 2.0, d3 = 3.0;
  std::vector<long> input(3);
  std::memcpy(&input[0], &d1, sizeof(double));
  std::memcpy(&input[1], &d2, sizeof(double));
  std::memcpy(&input[2], &d3, sizeof(double));

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
  EXPECT_EQ(size_after_first_flush, encoder.GetBufferSize());

  /* Verify data is still correct */
  PaxGorillaDecoder<long> decoder(decoding_options_);
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
 * P1: Gorilla int16 (short) type test
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, Int16AsGorilla) {
  std::vector<short> input;
  for (short i = 0; i < 500; i++) {
    input.push_back(i);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxGorillaEncodingTest, Int16SpecialPatterns) {
  std::vector<short> input = {0, 1, -1, 32767, -32768, 0, 32767, -32768};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P1: Gorilla int8 (signed char) type test
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, Int8AsGorilla) {
  std::vector<signed char> input;
  for (int i = -128; i <= 127; i++) {
    input.push_back(static_cast<signed char>(i));
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P1: Bulk Append — single call with all data
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, BulkAppend) {
  std::vector<double> doubles = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7};
  std::vector<long> input(doubles.size());
  for (size_t i = 0; i < doubles.size(); i++) {
    std::memcpy(&input[i], &doubles[i], sizeof(double));
  }

  PaxGorillaEncoder encoder(encoding_options_);
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

  PaxGorillaDecoder<long> decoder(decoding_options_);
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
 * P2: Three-path alternation stress test
 * Forces frequent transitions between all 3 Gorilla XOR paths:
 *   path 0 (XOR=0), path 1 (reuse window), path 2 (new window)
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, ThreePathAlternation) {
  std::vector<long> input;

  double base = 100.0;
  long base_bits;
  std::memcpy(&base_bits, &base, sizeof(double));

  for (int i = 0; i < 1000; i++) {
    if (i % 3 == 0) {
      /* Same value → XOR = 0 → path 0 */
      input.push_back(base_bits);
    } else if (i % 3 == 1) {
      /* Tiny change → small XOR → path 1 (reuse previous window) */
      double d = base + i * 0.0001;
      long val;
      std::memcpy(&val, &d, sizeof(double));
      input.push_back(val);
    } else {
      /* Huge jump → large XOR → path 2 (new window) */
      double d = (i % 2 == 0) ? 1e+100 : 1e-100;
      long val;
      std::memcpy(&val, &d, sizeof(double));
      input.push_back(val);
    }
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P1: Empty input and edge case tests
 * ================================================================ */

TEST_F(PaxGorillaEncodingTest, EmptyEncoderFlush) {
  /* Encoder with no Append: Flush is a no-op, buffer stays empty */
  PaxGorillaEncoder encoder(encoding_options_);
  size_t bound_size = encoder.GetBoundSize(0);
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));
  encoder.Flush();
  EXPECT_EQ(encoder.GetBufferSize(), 0u);
}

TEST_F(PaxGorillaEncodingTest, DecoderNullSrc) {
  /* Decoder with no src buffer: Decoding returns 0 */
  PaxGorillaDecoder<long> decoder(decoding_options_);
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

TEST_F(PaxGorillaEncodingTest, DecoderZeroElements) {
  /* Manually crafted buffer with num_elements=0: decoder returns 0 */
  GorillaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 0;
  header.element_size = sizeof(long);
  header.first_value = 0;

  PaxGorillaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

TEST_F(PaxGorillaEncodingTest, HeaderOnlyOneElement) {
  /* Manually craft a minimal buffer: header with num_elements=1.
   * Decoder only reads first_value from header, no XOR sections needed. */
  GorillaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 1;
  header.element_size = sizeof(long);

  double d = 3.14;
  uint64_t bits;
  std::memcpy(&bits, &d, sizeof(double));
  header.first_value = bits;

  PaxGorillaDecoder<long> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(sizeof(long));
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, sizeof(long));

  /* Verify bit-exact roundtrip of the float value */
  double decoded_val;
  std::memcpy(&decoded_val, decoder.GetBuffer(), sizeof(double));
  EXPECT_EQ(d, decoded_val);
}

TEST_F(PaxGorillaEncodingTest, HeaderOnlyOneElementInt32) {
  /* 1-element int32 Gorilla: verifies uint64-to-int32 truncation */
  GorillaHeader header;
  memset(&header, 0, sizeof(header));
  header.num_elements = 1;
  header.element_size = sizeof(int);

  float f = -2.5f;
  int fbits;
  std::memcpy(&fbits, &f, sizeof(float));
  /* zero-extend to uint64 (same as encoder) */
  header.first_value = 0;
  memcpy(&header.first_value, &fbits, sizeof(int));

  PaxGorillaDecoder<int> decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&header), sizeof(header));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(sizeof(int));
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, sizeof(int));

  float decoded_val;
  std::memcpy(&decoded_val, decoder.GetBuffer(), sizeof(float));
  EXPECT_EQ(f, decoded_val);
}

}  /* namespace pax */
