#include <immintrin.h>
#include <cstdint>
#include <algorithm>

// もっと速くできるがとりあえずこの関数。いずれ直します。

namespace NNUE{
	namespace NN{

		inline int32_t hsum_epi32(__m256i v) {
		    __m128i x128 = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
		    x128 = _mm_hadd_epi32(x128, x128);
		    x128 = _mm_hadd_epi32(x128, x128);
		    return _mm_cvtsi128_si32(x128);
		}


		void compute_layert_32x1(
			const __restrict uint8_t* in_value, 
			int* out_value, 
			const int8_t weights[32],
			const int32_t biase
		){
			__m256i v_in = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in_value));
			
			// 加算用のすべて1の16bitレジスタ
			__m256i v_ones = _mm256_set1_epi16(1);

			__m256i v_w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights));

			// v_in（ノード）と v_w（重み）	をかけ合わせてと隣で足す
			// 8bit -> 16bit
			__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w);

			// すべて1のレジスタとかけて足し合わせることで水平加算の手助け
			__m256i tmp_32 = _mm256_madd_epi16(tmp_16, v_ones);

			// 水平加算
			*out_value = hsum_epi32(tmp_32) + biase;
		}

		
		void compute_layert_32x32(
            const __restrict uint8_t* in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][32],
            const int32_t biases[32]
        ){
            __m256i v_in = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in_value));

            __m256i v_ones = _mm256_set1_epi16(1);
            
            // 【修正1・2】後半のSSE命令に合わせて __m128i で宣言し、8ビット単位で127をセットする
            __m128i v_zeros = _mm_setzero_si128();
            __m128i v_max = _mm_set1_epi8(127); 

            for(int i=0; i < 32; i += 4){
                __m256i row0 = _mm256_madd_epi16(v_ones, _mm256_maddubs_epi16(v_in, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights[i]))));
                __m256i row1 = _mm256_madd_epi16(v_ones, _mm256_maddubs_epi16(v_in, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights[i+1]))));
                __m256i row2 = _mm256_madd_epi16(v_ones, _mm256_maddubs_epi16(v_in, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights[i+2]))));
                __m256i row3 = _mm256_madd_epi16(v_ones, _mm256_maddubs_epi16(v_in, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights[i+3]))));

                // 水平加算
                // 半分だけ加算
                __m256i tmp = _mm256_hadd_epi32(_mm256_hadd_epi32(row0, row1), _mm256_hadd_epi32(row2, row3));
                
                // ここでlo+hiで完全に加算 (256bit -> 128bit の縮小)
                __m128i sum_lo = _mm256_castsi256_si128(tmp);
                __m128i sum_hi = _mm256_extracti128_si256(tmp, 1);
                __m128i sum_vec = _mm_add_epi32(sum_lo, sum_hi);

                // Biaseの加算
                sum_vec = _mm_add_epi32(sum_vec, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&biases[i])));

                // スケール(sum >> 6)
                // 【修正3】 _mm_ を追加
                sum_vec = _mm_srai_epi32(sum_vec, 6);

                // Cliped ReLU(0-127)
                // 32 -> 16 -> 8 bit
                // ※v_zerosが__m128iになったことでエラー解消
                __m128i res = _mm_packus_epi16(_mm_packs_epi32(sum_vec, v_zeros), v_zeros);
            
                // ※v_maxが__m128iかつepi8になったことでエラー解消、正しい比較が可能に
                res = _mm_min_epu8(res, v_max);

                *reinterpret_cast<int32_t*>(&out_value[i]) = _mm_cvtsi128_si32(res);
            }
        }


		void compute_layert_512x32(
	    const __restrict uint8_t* in_value, 
	    uint8_t* out_value, 
	    const int8_t weights[32][512],
	    const int32_t biases[32]
		) {
			const __m256i v_ones = _mm256_set1_epi16(1);

			for (int i = 0; i < 32; i += 4) {
				__m256i v_sum0 = _mm256_setzero_si256();
				__m256i v_sum1 = _mm256_setzero_si256();
				__m256i v_sum2 = _mm256_setzero_si256();
				__m256i v_sum3 = _mm256_setzero_si256();

				for (int j = 0; j < 512; j += 32) {
					__m256i v_in = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_value[j]));

					// 重みロードと積和演算 (タイポ修正済み)
					auto step = [&](const int8_t* w_ptr, __m256i& v_sum) {
						__m256i v_w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w_ptr));
						__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w);
						v_sum = _mm256_add_epi32(v_sum, _mm256_madd_epi16(tmp_16, v_ones));
					};

					step(&weights[i + 0][j], v_sum0);
					step(&weights[i + 1][j], v_sum1);
					step(&weights[i + 2][j], v_sum2);
					step(&weights[i + 3][j], v_sum3);
				}

				// 垂直に足してから最後に水平加算する等の工夫も可能ですが、まずは基本の修正
				int32_t sums[4] = {
					hsum_epi32(v_sum0) + biases[i + 0],
					hsum_epi32(v_sum1) + biases[i + 1],
					hsum_epi32(v_sum2) + biases[i + 2],
					hsum_epi32(v_sum3) + biases[i + 3]
				};

				for (int k = 0; k < 4; ++k) {
					out_value[i + k] = static_cast<uint8_t>(std::clamp(sums[k] >> 6, 0, 127));
				}
	    	}
		}

	} // namespace NN
} // namespace NNUE
