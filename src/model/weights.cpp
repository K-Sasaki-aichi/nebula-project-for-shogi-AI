#include "weights.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace weight {

    W_input w_input;
    W_Acc w_acc;
    W_layert w_layer;
    W_output w_output;

    bool load() {
        std::ifstream ifs("model/nn.bin", std::ios::binary);
        if (!ifs) {
            std::cerr << "エラー: nn.bin を開けませんでした。" << std::endl;
            return false;
        }

        // ==========================================
        // 1. NNUEヘッダーの読み飛ばし
        // ==========================================
        uint32_t version, hash, desc_len;
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
        ifs.read(reinterpret_cast<char*>(&hash), sizeof(hash));
        ifs.read(reinterpret_cast<char*>(&desc_len), sizeof(desc_len));
        
        std::string desc(desc_len, '\0');
        ifs.read(&desc[0], desc_len);

        // // ==========================================
        // // 2. 特徴変換層 (HalfKP) の読み込み
        // // ==========================================
        // uint32_t ft_hash;
        // ifs.read(reinterpret_cast<char*>(&ft_hash), sizeof(ft_hash));

        // ifs.read(reinterpret_cast<char*>(w_input.bias), sizeof(w_input.bias));

        // // 【修正】ファイルは [256][125388]、構造体はSIMD向けに [125388][256] です。
        // // スタックオーバーフローを防ぐため、1つの出力(125388要素=約250KB)ずつヒープに読み込み転置します。
        // std::vector<int16_t> ft_buf(NumFeatures);
        // for (int i = 0; i < NumAcc; ++i) { // 0 〜 255
        //     ifs.read(reinterpret_cast<char*>(ft_buf.data()), NumFeatures * sizeof(int16_t));
        //     for (int j = 0; j < NumFeatures; ++j) { // 0 〜 125387
        //         w_input.weight[j][i] = ft_buf[j];
        //     }
        // }

        // ==========================================
        // 2. 特徴変換層 (HalfKP) の読み込み
        // ==========================================
        uint32_t ft_hash;
        ifs.read(reinterpret_cast<char*>(&ft_hash), sizeof(ft_hash));

        ifs.read(reinterpret_cast<char*>(w_input.bias), sizeof(w_input.bias));

        // ファイル上でも [125388][256] (Input x Output) の順で格納されているため、
        // 転置は不要。構造体へ一撃ロード（Zero-Copy I/O）でそのまま読み込みます。
        ifs.read(reinterpret_cast<char*>(w_input.weight), sizeof(w_input.weight));

        // ==========================================
        // 3. ネットワーク層 (AffineTransform) の読み込み
        // ==========================================
        uint32_t net_hash;
        ifs.read(reinterpret_cast<char*>(&net_hash), sizeof(net_hash));

        // Affine層はすべてファイルと構造体で [出力][入力] のレイアウトが完全一致しています。
        // したがって、これ以降は転置不要の「一撃ロード（Zero-Copy I/O）」が最速・大正解です。

        // --- 第1隠れ層 (AffineTransform[32<-512]) ---
        ifs.read(reinterpret_cast<char*>(w_acc.bias), sizeof(w_acc.bias));
        ifs.read(reinterpret_cast<char*>(w_acc.weight), sizeof(w_acc.weight));

        // --- 第2隠れ層 (AffineTransform[32<-32]) ---
        ifs.read(reinterpret_cast<char*>(w_layer.bias), sizeof(w_layer.bias));
        ifs.read(reinterpret_cast<char*>(w_layer.weight), sizeof(w_layer.weight));

        // --- 出力層 (AffineTransform[1<-32]) ---
        ifs.read(reinterpret_cast<char*>(&w_output.bias), sizeof(w_output.bias));
        ifs.read(reinterpret_cast<char*>(w_output.weight), sizeof(w_output.weight));

        return ifs.good();
    }
}