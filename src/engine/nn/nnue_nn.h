#pragma once
#include <immintrin.h>
#include <cstdint>
#include <algorithm>
#include "../model/weights.h"

namespace nnue
{
    namespace NN{
        inline int32_t hsum_epi32(__m256i v) {
		    __m128i x128 = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
		    x128 = _mm_hadd_epi32(x128, x128);
		    x128 = _mm_hadd_epi32(x128, x128);
		    return _mm_cvtsi128_si32(x128);
		}

        void accToInput(const int16_t acc[2][256], uint8_t out[512]);

        void compute_layer_32x1(
			const __restrict uint8_t* in_value, 
			int32_t& out_value, 
			const int8_t weights[32],
			const int32_t bias
		);

        void compute_layer_32x32(
            const __restrict uint8_t* in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][32],
            const int32_t biases[32]
        );

        void compute_layer_512x32(
            const __restrict uint8_t* in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][512],
            const int32_t biases[32]
		);

        int32_t evaluation(const int16_t acc[2][256]);
    
    } // namespace NN
} // namespace nnue
