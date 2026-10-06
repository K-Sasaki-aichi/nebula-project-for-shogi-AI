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

        void compute_layer_512x32(
            const uint8_t* __restrict in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][512],
            const int32_t biases[32]
		);


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
