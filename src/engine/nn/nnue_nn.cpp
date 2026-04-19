// #include <immintrin.h>
// #include <cstdint>
// #include <algorithm>
// #include "../model/weights.h"
#include "nnue_nn.h"

// もっと速くできるがとりあえずこの関数。いずれ直します。

namespace nnue{
	namespace NN{
		void accToInput(const int16_t acc[2][256], uint8_t out[512]){
			const __m256i zeros = _mm256_setzero_si256();

			// アキュムレータの２次元配列をフラットに
			const int16_t* acc_flat = reinterpret_cast<const int16_t*>(acc);

			#pragma unroll
			for (int i = 0; i < 512; i += 32) { 
				__m256i v0 = _mm256_load_si256((const __m256i*)&acc_flat[i]);
				__m256i v1 = _mm256_load_si256((const __m256i*)&acc_flat[i + 16]);

				// ０以上にクリップ
				v0 = _mm256_max_epi16(v0, zeros);
				v1 = _mm256_max_epi16(v1, zeros);

				// 8bitにパック
				// この時127以上は127にされる
				__m256i packed = _mm256_packs_epi16(v0, v1);

				// 順序の並び替えとメモリストア
				_mm256_store_si256((__m256i*)&out[i],
									_mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0)));

			}
		}

		void compute_layer_32x1(
			const __restrict uint8_t* in_value, 
			int32_t& out_value, 
			const int8_t weights[32],
			const int32_t bias
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
			out_value = hsum_epi32(tmp_32) + bias;
		}

		
		void compute_layer_32x32(
            const __restrict uint8_t* in_value, 
            uint8_t* out_value, 
            const int8_t weights[32][32],
            const int32_t biases[32]
        ){
            __m256i v_in = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in_value));

            __m256i v_ones = _mm256_set1_epi16(1);
            
            // 後半のSSE命令に合わせて __m128i で宣言し、8ビット単位で127をセットする
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


		void compute_layer_512x32(
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

					// 重みロードと積和演算
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

		int32_t evaluation(const int16_t acc[2][256]){
			using namespace weight;

			alignas(32) uint8_t clipped_acc[512];
			alignas(32) uint8_t h1_out[32];
			alignas(32) uint8_t h2_out[32];
			int32_t score = 0;

			// アキュムレータを512個の特徴量に変換（clipedReLU）
			accToInput(acc, clipped_acc);
			
			compute_layer_512x32(clipped_acc, h1_out, w_acc.weight, w_acc.bias);
			compute_layer_32x32(h1_out, h2_out, w_layer.weight, w_layer.bias);
			compute_layer_32x1(h2_out, score, w_output.weight, w_output.bias);

			return score;
		}

	} // namespace NN
} // namespace NNUE
