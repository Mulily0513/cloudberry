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
 * pax_bool_encoding_test.cc
 *
 *   Unit tests for Bool compression encoding.
 *   Test design inspired by TimescaleDB compression_unit_test.c
 *   (test_bool, test_bool_rle patterns).
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/columns/pax_bool_encoding_test.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/columns/pax_bool_encoding.h"

#include <cstring>
#include <random>
#include <vector>

#include "comm/gtest_wrappers.h"
#include "pax_gtest_helper.h"

namespace pax {

class PaxBoolEncodingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    encoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_BOOL_COMPRESS;
    encoding_options_.is_sign = false;

    decoding_options_.column_encode_type =
        ColumnEncoding_Kind::ColumnEncoding_Kind_BOOL_COMPRESS;
    decoding_options_.is_sign = false;
  }

  /* Encode boolean values (as uint8: 0 or 1) then decode */
  std::vector<uint8_t> EncodeAndDecode(const std::vector<uint8_t> &input) {
    PaxBoolEncoder encoder(encoding_options_);

    size_t bound_size = encoder.GetBoundSize(input.size());
    encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

    /* Append per-element, matching PaxEncodingColumn::Append pattern */
    for (size_t i = 0; i < input.size(); i++) {
      uint8_t val = input[i];
      encoder.Append(reinterpret_cast<char *>(&val), 1);
    }
    encoder.Flush();

    const char *encoded_data = encoder.GetBuffer();
    size_t encoded_size = encoder.GetBufferSize();
    EXPECT_GT(encoded_size, 0u);

    /* Decode */
    PaxBoolDecoder decoder(decoding_options_);
    decoder.SetSrcBuffer(const_cast<char *>(encoded_data), encoded_size);

    auto result_buffer = std::make_shared<DataBuffer<char>>(input.size());
    decoder.SetDataBuffer(result_buffer);

    size_t decoded_size = decoder.Decoding();
    EXPECT_EQ(decoded_size, input.size());

    const uint8_t *decoded_data =
        reinterpret_cast<const uint8_t *>(decoder.GetBuffer());
    return std::vector<uint8_t>(decoded_data, decoded_data + decoded_size);
  }

  /* Helper: generate bool pattern with run_length flipping */
  std::vector<uint8_t> GenerateRunPattern(int num_elements, int run_length) {
    std::vector<uint8_t> result;
    bool val = true;
    int rlen = run_length;
    for (int i = 0; i < num_elements; i++) {
      if (rlen == 0) {
        val = !val;
        rlen = run_length;
      }
      result.push_back(val ? 1 : 0);
      rlen--;
    }
    return result;
  }

  PaxEncoder::EncodingOption encoding_options_;
  PaxDecoder::DecodingOption decoding_options_;
};

/* ================================================================
 * Basic roundtrip tests
 * (inspired by TSDB test_bool_rle basic patterns)
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, BasicAlternating) {
  /* Alternating true/false pattern, run_length=1 */
  auto input = GenerateRunPattern(1015, 1);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, AllTrue) {
  std::vector<uint8_t> input(1000, 1);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, AllFalse) {
  std::vector<uint8_t> input(1000, 0);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, SingleTrue) {
  std::vector<uint8_t> input = {1};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, SingleFalse) {
  std::vector<uint8_t> input = {0};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, TwoValues) {
  std::vector<uint8_t> input = {1, 0};
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * RLE-friendly patterns with various run lengths
 * (inspired by TSDB test_bool_rle run_length=1,5,27,61,65,100,191,600)
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, RunLength5) {
  auto input = GenerateRunPattern(1015, 5);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength27) {
  auto input = GenerateRunPattern(1015, 27);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength61) {
  auto input = GenerateRunPattern(1015, 61);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength65) {
  /* 65 > 64: exceeds Simple8b selector-1 block capacity */
  auto input = GenerateRunPattern(1015, 65);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength100) {
  auto input = GenerateRunPattern(1015, 100);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength191) {
  auto input = GenerateRunPattern(1015, 191);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLength600) {
  auto input = GenerateRunPattern(1015, 600);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RunLengthExceedTotal) {
  /* run_length > num_elements: all values are the same */
  auto input = GenerateRunPattern(1015, 1016);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * Pattern tests
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, EveryThirdTrue) {
  /* Pattern: F F T F F T ... */
  std::vector<uint8_t> input;
  for (int i = 0; i < 900; i++) {
    input.push_back((i % 3 == 0) ? 1 : 0);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, LongTrueFollowedByFalse) {
  /* 500 true then 500 false */
  std::vector<uint8_t> input;
  for (int i = 0; i < 500; i++) input.push_back(1);
  for (int i = 0; i < 500; i++) input.push_back(0);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, RandomPattern) {
  /* Pseudo-random boolean pattern using hash function */
  std::mt19937 gen(12345);
  std::uniform_int_distribution<int> dis(0, 1);

  std::vector<uint8_t> input;
  for (int i = 0; i < 1015; i++) {
    input.push_back(dis(gen));
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * Large dataset test
 * (TSDB uses TEST_ELEMENTS=1015, we test larger too)
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, LargeDataset10000) {
  std::vector<uint8_t> input;
  for (int i = 0; i < 10000; i++) {
    input.push_back(i % 2);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

TEST_F(PaxBoolEncodingTest, LargeDatasetAllTrue100000) {
  std::vector<uint8_t> input(100000, 1);
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(input, output);
}

/* ================================================================
 * Compression ratio tests
 * (inspired by TSDB test_bool compressed size validation)
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, CompressionRatioAllSame) {
  /* All same values: RLE should compress extremely well */
  PaxBoolEncoder encoder(encoding_options_);

  std::vector<uint8_t> input(100000, 1);
  size_t bound_size = encoder.GetBoundSize(input.size());
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  for (size_t i = 0; i < input.size(); i++) {
    uint8_t val = input[i];
    encoder.Append(reinterpret_cast<char *>(&val), 1);
  }
  encoder.Flush();

  size_t raw_size = input.size();
  size_t encoded_size = encoder.GetBufferSize();

  /* All-same bools should compress to very small size (RLE) */
  double ratio = static_cast<double>(raw_size) / encoded_size;
  EXPECT_GT(ratio, 100.0)
      << "Expected >100x compression for all-same bools, got " << ratio << "x"
      << " (raw=" << raw_size << ", encoded=" << encoded_size << ")";
}

TEST_F(PaxBoolEncodingTest, CompressionRatioAlternating) {
  /* Alternating pattern: 64 bools per Simple8b block (selector 1) */
  PaxBoolEncoder encoder(encoding_options_);

  std::vector<uint8_t> input;
  for (int i = 0; i < 100000; i++) {
    input.push_back(i % 2);
  }
  size_t bound_size = encoder.GetBoundSize(input.size());
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  for (size_t i = 0; i < input.size(); i++) {
    uint8_t val = input[i];
    encoder.Append(reinterpret_cast<char *>(&val), 1);
  }
  encoder.Flush();

  size_t raw_size = input.size();
  size_t encoded_size = encoder.GetBufferSize();

  /* 1-bit values packed 64 per 8 bytes = 8x compression baseline */
  double ratio = static_cast<double>(raw_size) / encoded_size;
  EXPECT_GT(ratio, 3.0)
      << "Expected >3x compression for alternating bools, got " << ratio << "x"
      << " (raw=" << raw_size << ", encoded=" << encoded_size << ")";
}

TEST_F(PaxBoolEncodingTest, PackingEfficiency64Bools) {
  /* Verify that 64 alternating bools pack as efficiently as 1 constant bool
   * (both use selector 1: 64 values per block, vs selector 15: RLE)
   * Inspired by TSDB bool_compressed_size test */
  PaxBoolEncoder encoder1(encoding_options_);
  size_t bound1 = encoder1.GetBoundSize(1);
  encoder1.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound1));
  uint8_t v = 1;
  encoder1.Append(reinterpret_cast<char *>(&v), 1);
  encoder1.Flush();
  size_t size_single = encoder1.GetBufferSize();

  PaxBoolEncoder encoder64(encoding_options_);
  size_t bound64 = encoder64.GetBoundSize(64);
  encoder64.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound64));
  for (int i = 0; i < 64; i++) {
    uint8_t val = (i % 2) ? 1 : 0;
    encoder64.Append(reinterpret_cast<char *>(&val), 1);
  }
  encoder64.Flush();
  size_t size_64 = encoder64.GetBufferSize();

  /* Both should fit in roughly the same space (1 Simple8b block + header) */
  EXPECT_LE(size_64, size_single + 16)
      << "64 bools should pack into ~1 block, similar to 1 bool";
}

/* ================================================================
 * P0: Double-Flush — verify values_.clear() after Flush()
 * After Flush(), calling Flush() again should be a no-op.
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, DoubleFlushIsNoop) {
  PaxBoolEncoder encoder(encoding_options_);

  std::vector<uint8_t> input = {1, 0, 1, 0, 1};
  size_t bound_size = encoder.GetBoundSize(input.size());
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));
  for (auto &v : input) {
    encoder.Append(reinterpret_cast<char *>(&v), 1);
  }
  encoder.Flush();
  size_t size_after_first_flush = encoder.GetBufferSize();
  EXPECT_GT(size_after_first_flush, 0u);

  /* Second Flush with no new data — should be a no-op */
  encoder.Flush();
  EXPECT_EQ(size_after_first_flush, encoder.GetBufferSize());

  /* Verify data is still correct */
  PaxBoolDecoder decoder(decoding_options_);
  decoder.SetSrcBuffer(const_cast<char *>(encoder.GetBuffer()),
                       size_after_first_flush);
  auto result_buffer = std::make_shared<DataBuffer<char>>(input.size());
  decoder.SetDataBuffer(result_buffer);
  decoder.Decoding();
  const uint8_t *output =
      reinterpret_cast<const uint8_t *>(decoder.GetBuffer());
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Mismatch at index " << i;
  }
}

/* ================================================================
 * P1: Non-canonical boolean input values
 * PostgreSQL bool is 1 byte: 0 = false, any non-zero = true.
 * Encoder should normalize non-zero to 1.
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, NonCanonicalBoolValues) {
  /* Values 2, 128, 255 are all "true" in PostgreSQL boolean semantics */
  std::vector<uint8_t> input = {0, 2, 128, 255, 1, 0, 42};
  std::vector<uint8_t> expected = {0, 1, 1, 1, 1, 0, 1};  /* normalized */

  auto output = EncodeAndDecode(input);
  EXPECT_EQ(expected, output);
}

TEST_F(PaxBoolEncodingTest, NonCanonicalAllNonZero) {
  /* All non-zero bytes should decode as 1 */
  std::vector<uint8_t> input;
  std::vector<uint8_t> expected;
  for (int i = 1; i <= 255; i++) {
    input.push_back(static_cast<uint8_t>(i));
    expected.push_back(1);
  }
  auto output = EncodeAndDecode(input);
  EXPECT_EQ(expected, output);
}

/* ================================================================
 * P1: Bulk Append — multiple bytes in one Append call
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, BulkAppend) {
  std::vector<uint8_t> input = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1};

  PaxBoolEncoder encoder(encoding_options_);
  size_t bound_size = encoder.GetBoundSize(input.size());
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));

  /* Single Append call with all data */
  encoder.Append(reinterpret_cast<char *>(input.data()), input.size());
  encoder.Flush();

  const char *encoded_data = encoder.GetBuffer();
  size_t encoded_size = encoder.GetBufferSize();

  PaxBoolDecoder decoder(decoding_options_);
  decoder.SetSrcBuffer(const_cast<char *>(encoded_data), encoded_size);
  auto result_buffer = std::make_shared<DataBuffer<char>>(input.size());
  decoder.SetDataBuffer(result_buffer);
  decoder.Decoding();

  const uint8_t *output =
      reinterpret_cast<const uint8_t *>(decoder.GetBuffer());
  for (size_t i = 0; i < input.size(); i++) {
    EXPECT_EQ(input[i], output[i]) << "Mismatch at index " << i;
  }
}

/* ================================================================
 * P1: Empty input and edge case tests
 * ================================================================ */

TEST_F(PaxBoolEncodingTest, EmptyEncoderFlush) {
  /* Encoder with no Append: Flush is a no-op, buffer stays empty */
  PaxBoolEncoder encoder(encoding_options_);
  size_t bound_size = encoder.GetBoundSize(0);
  encoder.SetDataBuffer(std::make_shared<DataBuffer<char>>(bound_size));
  encoder.Flush();
  EXPECT_EQ(encoder.GetBufferSize(), 0u);
}

TEST_F(PaxBoolEncodingTest, DecoderNullSrc) {
  /* Decoder with no src buffer: Decoding returns 0 */
  PaxBoolDecoder decoder(decoding_options_);
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

TEST_F(PaxBoolEncodingTest, DecoderZeroElements) {
  /* Manually crafted buffer with num_elements=0: decoder returns 0 */
  uint32_t num_elements = 0;

  PaxBoolDecoder decoder(decoding_options_);
  decoder.SetSrcBuffer(reinterpret_cast<char *>(&num_elements),
                        sizeof(num_elements));
  auto result_buffer =
      std::make_shared<DataBuffer<char>>(64);
  decoder.SetDataBuffer(result_buffer);
  size_t decoded = decoder.Decoding();
  EXPECT_EQ(decoded, 0u);
}

}  /* namespace pax */
