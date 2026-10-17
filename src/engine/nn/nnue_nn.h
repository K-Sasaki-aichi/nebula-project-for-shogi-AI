#pragma once
#include <immintrin.h>
#include <cstdint>
#include <algorithm>
#include "../../model/weights.h"
#include "../../nshogi/src/core/types.h"

namespace nnue
{
    namespace NN{
        inline int32_t hsum_epi32(__m256i v) {
		    __m128i x128 = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
		    x128 = _mm_hadd_epi32(x128, x128);
		    x128 = _mm_hadd_epi32(x128, x128);
		    return _mm_cvtsi128_si32(x128);
		}

        void accToInput(const int16_t acc0[256],const int16_t acc1[256],  uint8_t out[512]);

        void compute_layer_32x1(
			const uint8_t* __restrict in_value, 
			int32_t& out_value, 
			const int8_t weights[32],
			const int32_t bias
		);

        void compute_layer_32x32(
            const uint8_t* __restrict in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][32],
            const int32_t biases[32]
        );

        // void compute_layer_512x32(
        //     const uint8_t* __restrict in_value, 
        //     uint8_t* out_value, 
        //     const int8_t weights[32][512],
        //     const int32_t biases[32]
		// );

        inline void compute_layer_512x32(
			const uint8_t* __restrict in_value, 
			uint8_t* out_value, 
			const int8_t weights[32][512],
			const int32_t biases[32]
		) {
			const __m256i v_ones = _mm256_set1_epi16(1);

			// 8行ごとの処理に変更
			for (int i = 0; i < 32; i += 8) {
				__m256i v_sum0 = _mm256_setzero_si256();
				__m256i v_sum1 = _mm256_setzero_si256();
				__m256i v_sum2 = _mm256_setzero_si256();
				__m256i v_sum3 = _mm256_setzero_si256();
				__m256i v_sum4 = _mm256_setzero_si256();
				__m256i v_sum5 = _mm256_setzero_si256();
				__m256i v_sum6 = _mm256_setzero_si256();
				__m256i v_sum7 = _mm256_setzero_si256();

				for (int j = 0; j < 512; j += 32) {
					// in_value は alignas(32) なので高速な load_si256 が可能
					__m256i v_in = _mm256_load_si256(reinterpret_cast<const __m256i*>(&in_value[j]));

					// weights もメモリレイアウト上 32バイト境界なので load_si256 が可能
					#define ACCUMULATE(sum, w_ptr) \
						do { \
							__m256i v_w = _mm256_load_si256(reinterpret_cast<const __m256i*>(w_ptr)); \
							__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w); \
							sum = _mm256_add_epi32(sum, _mm256_madd_epi16(tmp_16, v_ones)); \
						} while(0)

					ACCUMULATE(v_sum0, &weights[i + 0][j]);
					ACCUMULATE(v_sum1, &weights[i + 1][j]);
					ACCUMULATE(v_sum2, &weights[i + 2][j]);
					ACCUMULATE(v_sum3, &weights[i + 3][j]);
					ACCUMULATE(v_sum4, &weights[i + 4][j]);
					ACCUMULATE(v_sum5, &weights[i + 5][j]);
					ACCUMULATE(v_sum6, &weights[i + 6][j]);
					ACCUMULATE(v_sum7, &weights[i + 7][j]);

					#undef ACCUMULATE
				}

				int32_t sums[8] = {
					hsum_epi32(v_sum0) + biases[i + 0],
					hsum_epi32(v_sum1) + biases[i + 1],
					hsum_epi32(v_sum2) + biases[i + 2],
					hsum_epi32(v_sum3) + biases[i + 3],
					hsum_epi32(v_sum4) + biases[i + 4],
					hsum_epi32(v_sum5) + biases[i + 5],
					hsum_epi32(v_sum6) + biases[i + 6],
					hsum_epi32(v_sum7) + biases[i + 7]
				};

				for (int k = 0; k < 8; ++k) {
					out_value[i + k] = static_cast<uint8_t>(std::clamp(sums[k] >> 6, 0, 127));
				}
			}
		}


        template<nshogi::core::Color Us>
        int32_t calNN(const int16_t acc[2][256]){
			using namespace weight;

			alignas(32) uint8_t clipped_acc[512];
			alignas(32) uint8_t h1_out[32];
			alignas(32) uint8_t h2_out[32];
			int32_t score = 0;

            if constexpr(Us == nshogi::core::Black){
                accToInput(acc[0], acc[1], clipped_acc);
            } else {
                accToInput(acc[1], acc[0], clipped_acc);
            }
			
			compute_layer_512x32(clipped_acc, h1_out, w_acc.weight, w_acc.bias);
			compute_layer_32x32(h1_out, h2_out, w_layer.weight, w_layer.bias);
			compute_layer_32x1(h2_out, score, w_output.weight, w_output.bias);

			return score;
		}
    
    } // namespace NN
} // namespace nnue
