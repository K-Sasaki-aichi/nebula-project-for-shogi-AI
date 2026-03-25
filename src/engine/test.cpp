#include <immintrin.h>
#include <cstdint>
#include <iostream>

// メモリの32バイト境界アライメントはAVX2のロード(_mm256_load_si256)に必須
alignas(32) const int INPUT_SIZE = 256; 
alignas(32) const int OUTPUT_SIZE = 32;

struct LinearLayer {
    // 重み: [OUTPUT_SIZE][INPUT_SIZE] (int8_t)
    alignas(32) int8_t weights[OUTPUT_SIZE][INPUT_SIZE];
    // バイアス: [OUTPUT_SIZE] (int32_t)
    alignas(32) int32_t biases[OUTPUT_SIZE];
};

// AVX2を用いた水平加算（256bitレジスタ内の8つのint32_tをすべて足し合わせる）
inline int32_t hsum_epi32_avx(__m256i x) {
    __m128i hi64 = _mm256_extracti128_si256(x, 1);
    __m128i lo64 = _mm256_castsi256_si128(x);
    __m128i sum64 = _mm_add_epi32(hi64, lo64);
    __m128i hi32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(1, 0, 3, 2));
    __m128i sum32 = _mm_add_epi32(sum64, hi32);
    __m128i hi16 = _mm_shuffle_epi32(sum32, _MM_SHUFFLE(2, 3, 0, 1));
    __m128i sum16 = _mm_add_epi32(sum32, hi16);
    return _mm_cvtsi128_si32(sum16);
}

// 全結合層のフォワードパス
void forward_dense(const uint8_t* input, const LinearLayer& layer, int32_t* output) {
    // int16_tの加算用に全要素が1のベクトルを用意
    const __m256i ones = _mm256_set1_epi16(1);

    for (int i = 0; i < OUTPUT_SIZE; ++i) {
        __m256i sum_vec = _mm256_setzero_si256();

        // 32要素(256bit)ずつ処理
        for (int j = 0; j < INPUT_SIZE; j += 32) {
            // 1. 入力(uint8)と重み(int8)をロード
            __m256i in_vec = _mm256_load_si256((__m256i*)&input[j]);
            __m256i w_vec  = _mm256_load_si256((__m256i*)&layer.weights[i][j]);

            // 2. uint8 * int8 -> int16 の積和演算
            // 32個の8bit同士を掛け、隣り合う2つを足して16個の16bitにする
            __m256i dot16 = _mm256_maddubs_epi16(in_vec, w_vec);

            // 3. int16 -> int32 への変換と加算
            // dot16に1を掛けて隣り合う2つを足すことで、8個の32bit整数の和にする
            __m256i dot32 = _mm256_madd_epi16(dot16, ones);

            // 4. アキュムレータに足しこむ
            sum_vec = _mm256_add_epi32(sum_vec, dot32);
        }

        // 5. レジスタ内の総和を求め、バイアスを足して出力へ
        output[i] = hsum_epi32_avx(sum_vec) + layer.biases[i];
    }
}


/////////////////////////////////////////////////////////

void forward_dense_optimized(const uint8_t* input, const LinearLayer& layer, int32_t* output) {
    const __m256i ones = _mm256_set1_epi16(1);

    // 外側のループを 2 ずつ進める（2つの出力ニューロンを同時に処理）
    for (int i = 0; i < OUTPUT_SIZE; i += 2) {
        // 2つの出力それぞれのためのアキュムレータ
        __m256i sum0 = _mm256_setzero_si256();
        __m256i sum1 = _mm256_setzero_si256();

        for (int j = 0; j < INPUT_SIZE; j += 32) {
            // 1. 入力データは1回だけロードする（レジスタにバッファリング！）
            __m256i in_vec = _mm256_load_si256((__m256i*)&input[j]);

            // 2. 重みは出力ニューロン2つ分（i行目と i+1行目）をロード
            __m256i w0 = _mm256_load_si256((__m256i*)&layer.weights[i][j]);
            __m256i w1 = _mm256_load_si256((__m256i*)&layer.weights[i + 1][j]);

            // 3. i行目の計算（in_vecを使用）
            __m256i dot16_0 = _mm256_maddubs_epi16(in_vec, w0);
            __m256i dot32_0 = _mm256_madd_epi16(dot16_0, ones);
            sum0 = _mm256_add_epi32(sum0, dot32_0);

            // 4. i+1行目の計算（同じin_vecを使い回す！）
            __m256i dot16_1 = _mm256_maddubs_epi16(in_vec, w1);
            __m256i dot32_1 = _mm256_madd_epi16(dot16_1, ones);
            sum1 = _mm256_add_epi32(sum1, dot32_1);
        }

        // 5. 最後にまとめて水平加算とバイアス加算
        output[i]     = hsum_epi32_avx(sum0) + layer.biases[i];
        output[i + 1] = hsum_epi32_avx(sum1) + layer.biases[i + 1];
    }
}