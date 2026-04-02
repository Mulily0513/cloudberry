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
 * pax_simple8b_rle_test.cc
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_simple8b_rle_test.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_simple8b_rle.h"

#include <random>
#include <vector>

#include "comm/gtest_wrappers.h"
#include "pax_gtest_helper.h"

namespace pax {

class Simple8bRleTest : public ::testing::Test {
 protected:
  std::vector<uint64_t> CompressAndDecompress(
      const std::vector<uint64_t> &input) {
    Simple8bRleCompressor compressor;
    for (auto v : input) {
      compressor.Append(v);
    }

    auto [data, len] = compressor.Finish();
    EXPECT_GT(len, 0u);

    Simple8bRleDecompressor decompressor;
    decompressor.Init(data, len);
    EXPECT_EQ(decompressor.NumElements(), input.size());

    std::vector<uint64_t> output;
    while (decompressor.HasNext()) {
      output.push_back(decompressor.Next());
    }
    return output;
  }
};

TEST_F(Simple8bRleTest, AllZeros) {
  std::vector<uint64_t> input(1000, 0);
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, SingleValue) {
  std::vector<uint64_t> input = {42};
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, TwoValues) {
  std::vector<uint64_t> input = {100, 200};
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, SmallValues1Bit) {
  /* All 0s and 1s: selector 1, 64 values per block */
  std::vector<uint64_t> input;
  for (int i = 0; i < 200; i++) {
    input.push_back(i % 2);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, SmallValues4Bit) {
  /* Values 0-15: selector 4, 16 values per block */
  std::vector<uint64_t> input;
  for (int i = 0; i < 500; i++) {
    input.push_back(i % 16);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, MediumValues16Bit) {
  /* Values up to 65535: selector 11 */
  std::vector<uint64_t> input;
  for (int i = 0; i < 500; i++) {
    input.push_back(i * 100);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, LargeValues32Bit) {
  std::vector<uint64_t> input;
  for (uint64_t i = 0; i < 100; i++) {
    input.push_back(i * 100000000ULL);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, MaxValue64Bit) {
  /* Single 64-bit value per block: selector 14 */
  std::vector<uint64_t> input;
  for (int i = 0; i < 50; i++) {
    input.push_back(UINT64_MAX - i);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, RLERunSameValues) {
  /* Long run of same value should trigger RLE (selector 15) */
  std::vector<uint64_t> input(10000, 42);
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, MixedSmallAndLargeValues) {
  std::vector<uint64_t> input;
  /* Some small values */
  for (int i = 0; i < 100; i++) input.push_back(i);
  /* Some large values */
  for (int i = 0; i < 50; i++) input.push_back(UINT64_MAX / 2 + i);
  /* Back to small */
  for (int i = 0; i < 100; i++) input.push_back(i * 3);

  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, RandomValues) {
  std::mt19937_64 gen(12345);
  std::uniform_int_distribution<uint64_t> dis(0, 1000000);

  std::vector<uint64_t> input;
  for (int i = 0; i < 5000; i++) {
    input.push_back(dis(gen));
  }

  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, SequentialIntegers) {
  /* 0, 1, 2, ..., 9999 — typical after zigzag encoding of constant deltas */
  std::vector<uint64_t> input;
  for (uint64_t i = 0; i < 10000; i++) {
    input.push_back(i);
  }
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P0: Large values that exceed RLE's 28-bit value limit
 * When value > 2^28-1, RLE (selector 15) cannot be used even for
 * identical runs. Must fall back to selector 14 (1 elem, 64-bit).
 * ================================================================ */

TEST_F(Simple8bRleTest, LargeValueRLEFallback) {
  /* 100 identical UINT64_MAX values: too large for RLE (28-bit value limit)
   * Should use selector 14 (64-bit per element) instead of selector 15 (RLE) */
  std::vector<uint64_t> input(100, UINT64_MAX);
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, LargeValueRunAboveRLELimit) {
  /* Value that fits in 32 bits but exceeds 28-bit RLE limit (2^28 = 268435456) */
  uint64_t large_val = (1ULL << 28);  /* exactly one above RLE value limit */
  std::vector<uint64_t> input(200, large_val);
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, MixedLargeAndSmallRuns) {
  /* Alternating runs of small (RLE-eligible) and large (non-RLE) values */
  std::vector<uint64_t> input;
  for (int i = 0; i < 100; i++) input.push_back(42);         /* RLE eligible */
  for (int i = 0; i < 100; i++) input.push_back(UINT64_MAX); /* selector 14 */
  for (int i = 0; i < 100; i++) input.push_back(7);          /* RLE eligible */
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * P1: Empty input
 * ================================================================ */

TEST_F(Simple8bRleTest, EmptyInput) {
  /* Finish() with 0 elements should produce valid but minimal output */
  Simple8bRleCompressor compressor;
  auto [data, len] = compressor.Finish();

  /* Should produce at least the header (num_elements=0, num_blocks=0) */
  EXPECT_GE(len, sizeof(uint32_t) * 2);

  Simple8bRleDecompressor decompressor;
  decompressor.Init(data, len);
  EXPECT_EQ(decompressor.NumElements(), 0u);
  EXPECT_FALSE(decompressor.HasNext());
}

/* ================================================================
 * P2: Selector boundary precision tests
 * ================================================================ */

TEST_F(Simple8bRleTest, ExactSelectorBoundaries) {
  /* Values at exact selector boundaries:
   * sel 1: max 1-bit = 1
   * sel 2: max 2-bit = 3
   * sel 4: max 4-bit = 15
   * sel 8: max 8-bit = 255  */
  std::vector<uint64_t> input = {1, 3, 15, 255, 65535, (1ULL << 21) - 1,
                                  (1ULL << 32) - 1, UINT64_MAX};
  auto output = CompressAndDecompress(input);
  EXPECT_EQ(input, output);
}

TEST_F(Simple8bRleTest, CompressionRatioAllZeros) {
  /* All zeros should compress extremely well (selector 0) */
  Simple8bRleCompressor compressor;
  std::vector<uint64_t> input(100000, 0);
  for (auto v : input) {
    compressor.Append(v);
  }
  auto [data, len] = compressor.Finish();

  size_t raw_size = input.size() * sizeof(uint64_t);
  double ratio = static_cast<double>(raw_size) / len;
  EXPECT_GT(ratio, 50.0)
      << "Expected >50x compression for all-zeros, got " << ratio << "x";
}

}  /* namespace pax */
