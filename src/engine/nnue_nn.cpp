#include <immintrin.h>
#include <cstdint>
#include <algorithm>


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

		void compute_layert_32x32(
			const __restrict uint8_t* in_value, 
			uint8_t* out_value, 
			const int8_t weights[32][32],
			const int32_t biases[32]
		){
			__m256i v_in = _mm256_load_si256(reinterpret_cast<const __m256i*>(in_ping));
			
			// 加算用のすべて1の16bitレジスタ
			__m256i v_ones = _mm256_set1_epi16(1);

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

	} // NN
} // NNUE
