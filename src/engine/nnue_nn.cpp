#include <immintrin.h>
#include <cstdint>
#include <algorithm>

// もっと速くできるがとりあえずこの関数。いずれ直します。

namespace NNUE{
	namespace NN{
		inline int32_t hsum_epi32(__m256i x){
			// 256bit を上位128bitと下位128bitに分けて足す
		    __m128i x128 = _mm_add_epi32(_mm256_castsi256_si128(x), _mm256_extracti128_si256(x, 1));
		    // 128bit 内でシャッフルして足す
		    x128 = _mm_add_epi32(x128, _mm_shuffle_epi32(x128, _MM_SHUFFLE(1, 0, 3, 2)));
		    x128 = _mm_add_epi32(x128, _mm_shuffle_epi32(x128, _MM_SHUFFLE(2, 3, 0, 1)));
		    return _mm_cvtsi128_si32(x128); // 最終的な合計値を通常のint32_tで返す
		}


		void compute_layert_32x31(
			const __restrict uint8_t* in_value, 
			uint8_t* out_value, 
			const int8_t weights[32],
			const int32_t biase
		){
			__m256i v_in = _mm256_load_si256(reinterpret_cast<const __m256i*>(in_value));
			
			// 加算用のすべて1の16bitレジスタ
			__m256i v_ones = _mm256_set1_epi16(1);

			__m256i v_w = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights));

			// v_in（ノード）と v_w（重み）	をかけ合わせてと隣で足す
			// 8bit -> 16bit
			__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w);

			// すべて1のレジスタとかけて足し合わせることで水平加算の手助け
			__m256i tmp_32 = __256_madd_epi16(tmp_16, v_ones);

			// 水平加算
			int32_t sum = hsum_epi32(tmp_32);



			for(int i=0; i < 32; i++){
				__m256i v_w = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights[i]));

				// v_in（ノード）と v_w（重み）	をかけ合わせてと隣で足す
				// 8bit -> 16bit
				__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w);

				// すべて1のレジスタとかけて足し合わせることで水平加算の手助け
				__m256i tmp_32 = __256_madd_epi16(tmp_16, v_ones);

				// 水平加算
				int32_t sum = hsum_epi32(tmp_32);

				sum += biases[i];

				// スケール　とりあえず 2^6 で割る。
				sum >>= 6;

				//Clipped ReLU
				out_value[i] = static_cast<uint8_t>(std::clamp(sum, 0, 127));
			}
		}

		void compute_layert_32x32(
			const __restrict uint8_t* in_value, 
			uint8_t* out_value, 
			const int8_t weights[32][32],
			const int32_t biases[32]
		){
			__m256i v_in = _mm256_load_si256(reinterpret_cast<const __m256i*>(in_value));
			
			// 加算用のすべて1の16bitレジスタ
			__m256i v_ones = _mm256_set1_epi16(1);

			for(int i=0; i < 32; i++){
				__m256i v_w = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights[i]));

				__m256i tmp = __256_madd_epi16(v_ones, _mm256_maddubs_epi16(v_in, _mm256_load_si256(reinterpret_cast<const __m256i*>(weights[i]))));

				// 水平加算
				int32_t sum = hsum_epi32(tmp);

				sum += biases[i];

				// スケール　とりあえず 2^6 で割る。
				sum >>= 6;

				//Clipped ReLU
				out_value[i] = static_cast<uint8_t>(std::clamp(sum, 0, 127));
			}
		}

		void compute_layert_512x32(
			const __restrict uint8_t* in_value, 
			uint8_t* out_value, 
			const int8_t weights[32][512],
			const int32_t biases[32]
		){
			__m256i v_ones = _mm256_set1_epi16(1);

			for(int i = 0; i < 32; i++){
				// 加算の保存用
				__m256i v_sum32 = _mm256_setzero_si256();

				for(int j = 0; j < 512; j += 32){
					__m256i v_w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&weights[i][j]));
					__m256i v_in = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_value[j]));

					// v_in（ノード）と v_w（重み）	をかけ合わせてと隣で足す
					// 8bit -> 16bit
					__m256i tmp_16 = _mm256_maddubs_epi16(v_in, v_w);

					// すべて1のレジスタとかけて足し合わせることで水平加算の手助け
					__m256i tmp_32 = __256_madd_epi16(tmp_16, v_ones);

					v_sum32 = __mm256_add_epi32(v_sum32, tmp_32);
				}

				int32_t sum = hsum_epi32(v_sum32);

				sum += biases[i];
				sum >>= 6;
				out_value[i] = static_cast<uint8_t>(std::clamp(sum, 0, 127));
			}
		}

		void compute_layert_512x32_fast(
		    const __restrict uint8_t* in_value, 
		    uint8_t* out_value, 
		    const int8_t weights[32][512],
		    const int32_t biases[32]
		) {
		    __m256i v_ones = _mm256_set1_epi16(1);

		    // 4行（4ニューロン）ずつまとめて処理する
		    for (int i = 0; i < 32; i += 4) {
		        __m256i v_sum0 = _mm256_setzero_si256();
		        __m256i v_sum1 = _mm256_setzero_si256();
		        __m256i v_sum2 = _mm256_setzero_si256();
		        __m256i v_sum3 = _mm256_setzero_si256();

		        for (int j = 0; j < 512; j += 32) {
		            // 入力は1回ロードして使い回す
		            __m256i v_in = _mm256_loadu_si256((const __m256i*)&in_value[j]);

		            // 各行の重みをロードして演算
		            // 一度にやることで待ち時間を失くす
		            v_sum0 = _mm256_add_epi32(v_sum0, _mm256_madd_epi16(_mm256_maddubs_epi16(v_in, _mm256_loadu_si256((const __m256i*)&weights[i + 0][j])), v_ones));
		            v_sum1 = _mm256_add_epi32(v_sum1, _mm256_madd_epi16(_mm256_maddubs_epi16(v_in, _mm256_loadu_si256((const __m256i*)&weights[i + 1][j])), v_ones));
		            v_sum2 = _mm256_add_epi32(v_sum2, _mm256_madd_epi16(_mm256_maddubs_epi16(v_in, _mm256_loadu_si256((const __m256i*)&weights[i + 2][j])), v_ones));
		            v_sum3 = _mm256_add_epi32(v_sum3, _mm256_madd_epi16(_mm256_maddubs_epi16(v_in, _mm256_loadu_si256((const __m256i*)&weights[i + 3][j])), v_ones));
		        }

		        // 水平加算と後処理
		        int32_t s0 = hsum_epi32(v_sum0) + biases[i + 0];
		        int32_t s1 = hsum_epi32(v_sum1) + biases[i + 1];
		        int32_t s2 = hsum_epi32(v_sum2) + biases[i + 2];
		        int32_t s3 = hsum_epi32(v_sum3) + biases[i + 3];

		        out_value[i + 0] = static_cast<uint8_t>(std::clamp(s0 >> 6, 0, 127));
		        out_value[i + 1] = static_cast<uint8_t>(std::clamp(s1 >> 6, 0, 127));
		        out_value[i + 2] = static_cast<uint8_t>(std::clamp(s2 >> 6, 0, 127));
		        out_value[i + 3] = static_cast<uint8_t>(std::clamp(s3 >> 6, 0, 127));
		    }
		}

	} // NN
} // NNUE
