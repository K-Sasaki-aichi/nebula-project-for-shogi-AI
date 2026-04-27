#include "../src/nshogi/src/io/csa.h"
#include "../src/nshogi/src/io/sfen.h"
#include "../src/nshogi/src/core/types.h"
#include "../src/nshogi/src/core/state.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <filesystem>
#include <vector>
#include <utility>
#include <algorithm> // std::min用

std::vector<std::pair<uint64_t, nshogi::core::Move32>> book;
uint64_t init_hash = nshogi::core::StateBuilder::getInitialState().getHash(); // ※
namespace fs = std::filesystem;


// レーティング制限関数
bool isHighQualityGame(const std::string& csa_data, double threshold = 3000.0) {
    double black_rate = 0.0;
    double white_rate = 0.0;

    std::stringstream ss(csa_data);
    std::string line;

    while (std::getline(ss, line)) {
        // 先手のレート取得
        if (line.find("'black_rate:") == 0) {
            auto pos = line.find_last_of(':');
            if (pos != std::string::npos) {
                try { black_rate = std::stod(line.substr(pos + 1)); } catch (...) {}
            }
        }
        // 後手のレート取得
        else if (line.find("'white_rate:") == 0) {
            auto pos = line.find_last_of(':');
            if (pos != std::string::npos) {
                try { white_rate = std::stod(line.substr(pos + 1)); } catch (...) {}
            }
        }

        // 両方のレートが取得できたらこれ以上読む必要なし
        if (black_rate > 0.0 && white_rate > 0.0) break;

        // 盤面情報(P)や指し手(+)が始まったら、もうレート情報はないと見なして抜ける（高速化）
        if (!line.empty() && (line[0] == '+' || line[0] == '-' || line[0] == 'P')) {
            break;
        }
    }

    // 両方が閾値(3000.0)以上ならtrue
    return (black_rate >= threshold && white_rate >= threshold);
}

//  定跡追加関数
void addBook(const nshogi::core::State& full_state) {

}

std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("File not found: " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void bookgenerator(const std::string& dir){
    int processed_count = 0;

    for (const auto& entry : fs::directory_iterator(dir)) {
        try {
            if (!fs::is_regular_file(entry)) continue;

            std::string path = entry.path().string();
            std::string csa_data = readFile(path);

            // 【制限1】 R3000以上かチェック (重いパース処理の前に弾く！)
            if (!isHighQualityGame(csa_data, 3000.0)) {
                continue;
            }

            // Stateの構築 (ここでパース実行)
            nshogi::core::State state = nshogi::io::csa::StateBuilder::newState(csa_data);

            // 【制限2】 平手かどうかのチェック (初期ハッシュが一致するか)
            if (state.getInitialPosition().getHash() != init_hash) {
                continue;
            }

            // 全ての条件をクリアしたので定跡として追加
            addBook(state);
            processed_count++;

            // 進捗表示 (1000局ごと)
            if (processed_count % 1000 == 0) {
                std::cout << "Processed " << processed_count << " high-quality games." << std::endl;
            }

        } catch (const std::exception& e) {
            // パースエラー等の壊れた棋譜は無視して次へ
            // std::cerr << "Error in " << entry.path() << " : " << e.what() << std::endl;
        }
    }
    
    std::cout << "Total processed games: " << processed_count << std::endl;
    std::cout << "Total book entries: " << book.size() << std::endl;
}

int main() {
    std::string target_dir = "./csa_files"; // 実際のフォルダ名に変更してください
    
    std::cout << "Starting book generation..." << std::endl;
    bookgenerator(target_dir);
    
    // TODO: ここで book ベクタを集計し、最大ヒット数の手だけを残してファイル出力する

    return 0;
}