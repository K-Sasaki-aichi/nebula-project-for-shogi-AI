#include "weights.h"
#include <fstream>
#include <iostream>
#include <string>

namespace weight {
    constexpr char* filePath = "nn.bin";
    #include <fstream>
#include <iostream>
#include <string>

// 先ほどのヘッダーファイル（例: weight.h）をインクルードしていると仮定します
// #include "weight.h"

namespace weight {

    // extern 宣言された変数の実体を定義
    W_input w_input;
    W_Acc w_acc;
    W_layert w_layer;
    W_output w_output;

    bool load() {
        std::ifstream ifs("nn.bin", std::ios::binary);
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

        // ==========================================
        // 2. 特徴変換層 (HalfKP) の読み込み
        // ==========================================
        uint32_t ft_hash;
        ifs.read(reinterpret_cast<char*>(&ft_hash), sizeof(ft_hash));

        // 構造体の並び順が bias -> weight となったため、直感的に読み込めます。
        // sizeof() を使うことでサイズ指定も安全になりました。
        ifs.read(reinterpret_cast<char*>(w_input.bias), sizeof(w_input.bias));
        ifs.read(reinterpret_cast<char*>(w_input.weight), sizeof(w_input.weight));

        // ==========================================
        // 3. ネットワーク層 (AffineTransform) の読み込み
        // ==========================================
        uint32_t net_hash;
        ifs.read(reinterpret_cast<char*>(&net_hash), sizeof(net_hash));

        // --- 第1隠れ層 (AffineTransform[32<-512]) ---
        ifs.read(reinterpret_cast<char*>(w_acc.bias), sizeof(w_acc.bias));
        
        // 【注意】ファイルは [出力32][入力512]、構造体はSIMD向けに [入力512][出力32] となっています。
        // メモリ上の並びが異なるため、一時配列に読み込んでから「転置（Transpose）」して代入します。
        int8_t acc_wt_file[Numlayer][2 * NumAcc];
        ifs.read(reinterpret_cast<char*>(acc_wt_file), sizeof(acc_wt_file));
        for (int o = 0; o < Numlayer; ++o) {
            for (int i = 0; i < 2 * NumAcc; ++i) {
                w_acc.weight[i][o] = acc_wt_file[o][i];
            }
        }

        // --- 第2隠れ層 (AffineTransform[32<-32]) ---
        ifs.read(reinterpret_cast<char*>(w_layer.bias), sizeof(w_layer.bias));
        
        // ファイルは [出力32][入力32]、構造体は [入力32][出力32] のため転置
        int8_t layer_wt_file[Numlayer][Numlayer];
        ifs.read(reinterpret_cast<char*>(layer_wt_file), sizeof(layer_wt_file));
        for (int o = 0; o < Numlayer; ++o) {
            for (int i = 0; i < Numlayer; ++i) {
                w_layer.weight[i][o] = layer_wt_file[o][i];
            }
        }

        // --- 出力層 (AffineTransform[1<-32]) ---
        ifs.read(reinterpret_cast<char*>(&w_output.bias), sizeof(w_output.bias));
        
        // ファイルは [出力1][入力32]、構造体は [入力32][出力1] のため転置
        int8_t out_wt_file[1][Numlayer];
        ifs.read(reinterpret_cast<char*>(out_wt_file), sizeof(out_wt_file));
        for (int o = 0; o < 1; ++o) {
            for (int i = 0; i < Numlayer; ++i) {
                w_output.weight[i][o] = out_wt_file[o][i];
            }
        }

        return ifs.good();
    }
}

}