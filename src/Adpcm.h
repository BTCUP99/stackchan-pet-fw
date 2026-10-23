#pragma once

#include <Arduino.h>

// IMA ADPCM, 4:1, for audio over the network link (USB keeps raw PCM).
// Each block is self-contained: int16 predictor, uint8 step index, uint8 reserved,
// then one nibble per sample, low nibble first. Mirrors src/adpcm.py on the Mac.
namespace Adpcm {

constexpr size_t kHeaderBytes = 4;

inline size_t encodedBytes(size_t samples) {
  return kHeaderBytes + (samples + 1) / 2;
}

inline const int16_t* stepTable() {
  static const int16_t kSteps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};
  return kSteps;
}

inline int8_t indexStep(uint8_t code) {
  static const int8_t kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
  return kIndex[code & 0x0F];
}

inline int32_t decodeNibble(uint8_t code, int32_t& predictor, int32_t& index) {
  const int32_t step = stepTable()[index];
  int32_t diff = step >> 3;
  if (code & 4) diff += step;
  if (code & 2) diff += step >> 1;
  if (code & 1) diff += step >> 2;
  predictor += (code & 8) ? -diff : diff;
  predictor = constrain(predictor, -32768, 32767);
  index = constrain(index + indexStep(code), 0, 88);
  return predictor;
}

// samples -> out (encodedBytes(count) bytes). Returns bytes written.
inline size_t encode(const int16_t* samples, size_t count, uint8_t* out) {
  int32_t predictor = count > 0 ? samples[0] : 0;
  int32_t index = 0;
  out[0] = static_cast<uint8_t>(predictor & 0xFF);
  out[1] = static_cast<uint8_t>((predictor >> 8) & 0xFF);
  out[2] = 0;
  out[3] = 0;
  uint8_t* data = out + kHeaderBytes;
  for (size_t i = 0; i < count; ++i) {
    const int32_t step = stepTable()[index];
    int32_t diff = samples[i] - predictor;
    uint8_t code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    if (diff >= step) { code |= 4; diff -= step; }
    if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; }
    if (diff >= (step >> 2)) { code |= 1; }
    decodeNibble(code, predictor, index);
    if (i & 1) {
      data[i / 2] |= static_cast<uint8_t>(code << 4);
    } else {
      data[i / 2] = code;
    }
  }
  return encodedBytes(count);
}

// block -> out samples. Returns samples written (at most maxSamples).
inline size_t decode(const uint8_t* block, size_t length, int16_t* out, size_t maxSamples) {
  if (length < kHeaderBytes) return 0;
  int32_t predictor = static_cast<int16_t>(block[0] | (block[1] << 8));
  int32_t index = constrain(static_cast<int32_t>(block[2]), 0, 88);
  const size_t samples = min((length - kHeaderBytes) * 2, maxSamples);
  for (size_t i = 0; i < samples; ++i) {
    const uint8_t byte = block[kHeaderBytes + i / 2];
    out[i] = static_cast<int16_t>(decodeNibble((i & 1) ? byte >> 4 : byte & 0x0F, predictor, index));
  }
  return samples;
}

}  // namespace Adpcm
