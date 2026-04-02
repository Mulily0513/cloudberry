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
 * pax_bit_array_test.cc
 *
 *   Unit tests for BitArray — variable-width bit array used by
 *   Gorilla XOR compression. Tests cover cross-bucket boundaries,
 *   64-bit full-width operations, empty arrays, and serialization.
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_bit_array_test.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_bit_array.h"

#include <cstring>
#include <random>
#include <vector>

#include "comm/gtest_wrappers.h"
#include "pax_gtest_helper.h"

namespace pax {

class BitArrayTest : public ::testing::Test {
 protected:
  /* Serialize a BitArray and read it back via BitArrayIterator */
  void SerializeAndVerify(BitArray &arr,
                          const std::vector<std::pair<uint64_t, uint8_t>> &entries) {
    size_t ser_size = arr.DataSize();
    std::vector<char> buf(ser_size);
    arr.Serialize(buf.data());

    BitArrayIterator iter(buf.data(), ser_size);

    for (size_t i = 0; i < entries.size(); i++) {
      ASSERT_TRUE(iter.HasNext()) << "Iterator exhausted at index " << i;
      uint64_t val = iter.ReadBits(entries[i].second);
      EXPECT_EQ(entries[i].first, val)
          << "Mismatch at index " << i
          << " (num_bits=" << static_cast<int>(entries[i].second) << ")";
    }
  }
};

/* ================================================================
 * P0: Basic functionality
 * ================================================================ */

TEST_F(BitArrayTest, SingleBitAppendAndRead) {
  BitArray arr;
  arr.AppendBits(1, 1);
  arr.AppendBits(0, 1);
  arr.AppendBits(1, 1);

  EXPECT_EQ(arr.NumBits(), 3u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {
      {1, 1}, {0, 1}, {1, 1}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, VariousWidths) {
  BitArray arr;
  /* 3-bit value: 5 (101) */
  arr.AppendBits(5, 3);
  /* 8-bit value: 200 */
  arr.AppendBits(200, 8);
  /* 16-bit value: 50000 */
  arr.AppendBits(50000, 16);
  /* 1-bit value: 1 */
  arr.AppendBits(1, 1);

  EXPECT_EQ(arr.NumBits(), 3u + 8u + 16u + 1u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {
      {5, 3}, {200, 8}, {50000, 16}, {1, 1}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, ZeroBitsAppend) {
  /* AppendBits with num_bits=0 should be a no-op */
  BitArray arr;
  arr.AppendBits(0xDEAD, 0);
  EXPECT_EQ(arr.NumBits(), 0u);

  arr.AppendBits(42, 6);
  arr.AppendBits(0, 0);  /* no-op */
  EXPECT_EQ(arr.NumBits(), 6u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {{42, 6}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, ReadBitsZero) {
  /* ReadBits(0) should return 0 without advancing */
  BitArray arr;
  arr.AppendBits(7, 3);

  size_t ser_size = arr.DataSize();
  std::vector<char> buf(ser_size);
  arr.Serialize(buf.data());

  BitArrayIterator iter(buf.data(), ser_size);
  EXPECT_EQ(iter.ReadBits(0), 0u);
  EXPECT_TRUE(iter.HasNext());
  EXPECT_EQ(iter.ReadBits(3), 7u);
}

/* ================================================================
 * P0: Cross-bucket boundary tests (64-bit boundary)
 * ================================================================ */

TEST_F(BitArrayTest, CrossBucketBoundary) {
  /* Write 60 bits, then 10 bits — the 10-bit value crosses the 64-bit boundary */
  BitArray arr;
  arr.AppendBits(0xFFFFFFFFFFFFFFFULL, 60);  /* 60 bits of 1s */
  arr.AppendBits(0x3FF, 10);                   /* 10-bit value crossing boundary */

  EXPECT_EQ(arr.NumBits(), 70u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {
      {0xFFFFFFFFFFFFFFFULL, 60}, {0x3FF, 10}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, ExactBucketBoundary) {
  /* Write exactly 64 bits, then another value */
  BitArray arr;
  arr.AppendBits(UINT64_MAX, 64);
  arr.AppendBits(42, 6);

  EXPECT_EQ(arr.NumBits(), 70u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {
      {UINT64_MAX, 64}, {42, 6}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, Full64BitAtNonZeroOffset) {
  /* Write 7 bits, then a full 64-bit value — the 64-bit read must cross bucket */
  BitArray arr;
  arr.AppendBits(0x55, 7);  /* 7 bits: 1010101 */
  arr.AppendBits(0xDEADBEEFCAFEBABEULL, 64);

  EXPECT_EQ(arr.NumBits(), 71u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {
      {0x55, 7}, {0xDEADBEEFCAFEBABEULL, 64}};
  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, MultipleBucketCrossings) {
  /* Write many small values that collectively cross multiple bucket boundaries */
  BitArray arr;
  std::vector<std::pair<uint64_t, uint8_t>> entries;

  for (int i = 0; i < 200; i++) {
    uint8_t width = static_cast<uint8_t>((i % 6) + 1);  /* 1-6 bits */
    uint64_t val = static_cast<uint64_t>(i) & ((1ULL << width) - 1);
    arr.AppendBits(val, width);
    entries.push_back({val, width});
  }

  SerializeAndVerify(arr, entries);
}

/* ================================================================
 * P0: Many 1-bit appends (stress test for bucket overflow)
 * ================================================================ */

TEST_F(BitArrayTest, Many1BitAppends) {
  /* 1000 single-bit appends crossing many buckets */
  BitArray arr;
  std::vector<std::pair<uint64_t, uint8_t>> entries;

  for (int i = 0; i < 1000; i++) {
    uint64_t val = i % 2;
    arr.AppendBits(val, 1);
    entries.push_back({val, 1});
  }

  EXPECT_EQ(arr.NumBits(), 1000u);
  SerializeAndVerify(arr, entries);
}

/* ================================================================
 * P0: Empty BitArray
 * ================================================================ */

TEST_F(BitArrayTest, EmptyArray) {
  BitArray arr;
  EXPECT_EQ(arr.NumBits(), 0u);
  EXPECT_EQ(arr.DataSize(), sizeof(uint64_t));  /* just the header */

  std::vector<char> buf(arr.DataSize());
  arr.Serialize(buf.data());

  BitArrayIterator iter(buf.data(), buf.size());
  EXPECT_FALSE(iter.HasNext());
}

/* ================================================================
 * P0: 6-bit values (Gorilla leading_zeros format)
 * ================================================================ */

TEST_F(BitArrayTest, Gorilla6BitLeadingZeros) {
  /* Gorilla stores leading_zeros as 6-bit values (0-63).
   * Test all possible 6-bit values in sequence. */
  BitArray arr;
  std::vector<std::pair<uint64_t, uint8_t>> entries;

  for (uint64_t v = 0; v < 64; v++) {
    arr.AppendBits(v, 6);
    entries.push_back({v, 6});
  }

  EXPECT_EQ(arr.NumBits(), 64u * 6u);
  SerializeAndVerify(arr, entries);
}

/* ================================================================
 * P0: Reset and reuse
 * ================================================================ */

TEST_F(BitArrayTest, ResetAndReuse) {
  BitArray arr;
  arr.AppendBits(0xFF, 8);
  arr.AppendBits(0xAB, 8);
  EXPECT_EQ(arr.NumBits(), 16u);

  arr.Reset();
  EXPECT_EQ(arr.NumBits(), 0u);

  /* Reuse after reset */
  arr.AppendBits(42, 6);
  EXPECT_EQ(arr.NumBits(), 6u);

  std::vector<std::pair<uint64_t, uint8_t>> entries = {{42, 6}};
  SerializeAndVerify(arr, entries);
}

/* ================================================================
 * P1: Random widths and values
 * ================================================================ */

TEST_F(BitArrayTest, RandomWidthsAndValues) {
  std::mt19937_64 gen(42);
  BitArray arr;
  std::vector<std::pair<uint64_t, uint8_t>> entries;

  for (int i = 0; i < 500; i++) {
    uint8_t width = static_cast<uint8_t>((gen() % 64) + 1);  /* 1-64 */
    uint64_t val = gen();
    if (width < 64) {
      val &= ((1ULL << width) - 1);
    }
    arr.AppendBits(val, width);
    entries.push_back({val, width});
  }

  SerializeAndVerify(arr, entries);
}

TEST_F(BitArrayTest, AllMaxValues) {
  /* All 64-bit max values in sequence */
  BitArray arr;
  std::vector<std::pair<uint64_t, uint8_t>> entries;

  for (int i = 0; i < 100; i++) {
    arr.AppendBits(UINT64_MAX, 64);
    entries.push_back({UINT64_MAX, 64});
  }

  EXPECT_EQ(arr.NumBits(), 6400u);
  SerializeAndVerify(arr, entries);
}

}  /* namespace pax */
