#include "../src/nshogi/src/io/csa.h"
#include "../src/nshogi/src/io/sfen.h"
#include "../src/nshogi/src/core/types.h"
#include "../src/nshogi/src/core/state.h"
#include "../src/nshogi/src/core/position.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <filesystem>
#include <vector>
#include <utility>
#include <algorithm>
#include <tuple>
#include <regex>

namespace fs = std::filesystem;
using Move32 = nshogi::core::Move32;
int PTK_Empty = nshogi::core::PTK_Empty;

std::vector<std::tuple<uint64_t, Move32, uint32_t>> book;
const nshogi::core::State base_hirate_state = nshogi::core::StateBuilder::getInitialState();
int max_over_count = 0;

/* 設定 */
const double min_rate = 3000;
const int ply_set = 35;


// レーティング制限関数（正規表現を用いた堅牢版）
bool isHighQualityGame(const std::string& csa_data, double threshold = 2800.0) {
    double black_rate = 0.0;
    double white_rate = 0.0;

    // 正規表現パターン: 
    // ^'black_rate: (任意の文字列) : (数値) を抽出する
    // 最後のキャプチャグループ ([-+]?[0-9]*\.?[0-9]+) で数値を捉える
    std::regex black_regex(R"('black_rate:.*:([-+]?[0-9]*\.?[0-9]+))");
    std::regex white_regex(R"('white_rate:.*:([-+]?[0-9]*\.?[0-9]+))");
    
    std::smatch match;

    // 先手レートの抽出
    if (std::regex_search(csa_data, match, black_regex)) {
        try {
            black_rate = std::stod(match[1].str());
        } catch (...) {
            black_rate = 0.0;
        }
    }

    // 後手レートの抽出
    if (std::regex_search(csa_data, match, white_regex)) {
        try {
            white_rate = std::stod(match[1].str());
        } catch (...) {
            white_rate = 0.0;
        }
    }

    // デバッグ用出力（正常に抽出できているか確認するため。確認後は消してOK）
    /*
    if (black_rate > 0.0 || white_rate > 0.0) {
        std::cout << "[Parse] Black: " << black_rate << ", White: " << white_rate << std::endl;
    }
    */

    return (black_rate >= threshold && white_rate >= threshold);
}

//  定跡追加関数
void addBookBuf(const nshogi::core::State& full_state, std::vector<std::pair<uint64_t, Move32>>& bookBuf) {
    /* ---- 平手に関わらず定跡を作りたいならこっち ---------- */
    // nshogi::core::State current_state = full_state.clone();
    // int total_ply = current_state.getPly();

    // // 2. 0手目（初期局面）まで指し手をすべて巻き戻す
    // while (current_state.getPly() > 0) {
    //     current_state.undoMove();
    // }
    /* --------------------------------------------------- */

    // 平手専用
    nshogi::core::State current_state = base_hirate_state.clone();

    // 抽出する手数の上限
    int max_ply = full_state.getPly();
    int limit = std::min(max_ply, ply_set);

    int over_count = 0;
    for(int ply = 0; ply < limit; ply++ ){
        uint64_t hash = current_state.getHash();
        nshogi::core::Move32 move = full_state.getHistoryMove(ply);

        bookBuf.push_back({hash, move});

        current_state.doMove(move);

        if(ply == limit - 1 && limit < max_ply) {
            // 王手がかかっている、または次の手が駒を取る手なら、抽出上限を1手伸ばす
            if(current_state.isInCheck() || full_state.getHistoryMove(ply + 1).capturePieceType() != PTK_Empty) {
                limit++;
                over_count++;
            }
        }
    }

    if(over_count > max_over_count) max_over_count = over_count;
}

void addBook(std::vector<std::pair<uint64_t, Move32>>& bookBuf){
    auto current = bookBuf.begin();
    while(current != bookBuf.end()){
        uint64_t current_hash = current->first;
        auto next_hash_it = current;
        while(next_hash_it != bookBuf.end() && next_hash_it->first == current_hash) next_hash_it++;

        std::vector<std::pair<Move32, uint32_t>> move_counts;
        for (auto it = current; it != next_hash_it; ++it) {
            bool found = false;
            for (auto& mc : move_counts) {
                if (mc.first == it->second) {
                    mc.second++;
                    found = true;
                    break;
                }
            }
            if (!found) {
                move_counts.push_back({it->second, 1});
            }
        }

        nshogi::core::Move32 best_move;
        uint32_t max_count = 0;
        for (const auto& mc : move_counts) {
            if (mc.second > max_count) {
                max_count = mc.second;
                best_move = mc.first;
            }
        }

        book.push_back({current_hash, best_move, max_count});
        current = next_hash_it;
    }
}

void saveBook(){
    if (book.empty()) return;

    std::sort(book.begin(), book.end(), [](const auto& a, const auto& b) {
        return std::get<0>(a) < std::get<0>(b);
    });

    std::vector<std::tuple<uint64_t, Move32, uint32_t>> optimized_book;

    auto current = book.begin();
    while(current != book.end()){
        uint64_t current_hash = std::get<0>(*current);
        auto next_hash_it = current;
        while(next_hash_it != book.end() && std::get<0>(*next_hash_it) == current_hash) next_hash_it++;

        std::vector<std::pair<Move32, uint32_t>> move_counts;
        for (auto it = current; it != next_hash_it; ++it) {
            bool found = false;
            for (auto& mc : move_counts) {
                if (mc.first == std::get<1>(*it)) {
                    mc.second += std::get<2>(*it);
                    found = true;
                    break;
                }
            }
            if (!found) {
                move_counts.push_back({std::get<1>(*it), std::get<2>(*it)});
            }
        }

        nshogi::core::Move32 best_move;
        uint32_t max_count = 0;
        for (const auto& mc : move_counts) {
            if (mc.second > max_count) {
                max_count = mc.second;
                best_move = mc.first;
            }
        }

        optimized_book.push_back({current_hash, best_move, max_count});
        current = next_hash_it;
    }

    std::ofstream ofs("book.bin", std::ios::binary);
    
    for (const auto& entry : optimized_book) {
        uint64_t hash = std::get<0>(entry);
        Move32 move = std::get<1>(entry);
        uint32_t count = std::get<2>(entry);

        ofs.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        ofs.write(reinterpret_cast<const char*>(&move), sizeof(move));
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
    }

    book = std::move(optimized_book);
    std::cout << "Complete!" << std::endl <<  "book.bin generated successfully." << std::endl;
}

std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("File not found: " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void bookgenerator(const std::string& dir){
    nshogi::core::Position hirate_pos = nshogi::core::PositionBuilder::getInitialPosition();
    std::string hirate_csa_str = nshogi::io::csa::positionToCSA(hirate_pos);
    int processed_count = 0;
    int file_count = 0;

    std::vector<std::pair<uint64_t, Move32>> bookBuf;

    for (const auto& entry : fs::directory_iterator(dir)) {
        try {
            // 進捗表示 (100局ごと)
            if (file_count % 1000 == 0) {
                std::cout << "file :  " << file_count << std::endl;
                std::cout << "Processed " << processed_count << " high-quality games." << std::endl;
            }

            if (!fs::is_regular_file(entry)) continue;

            std::string path = entry.path().string();
            std::string csa_data = readFile(path);

            // R3000以上かチェック
            if (!isHighQualityGame(csa_data, min_rate)) {
                file_count++;
                continue;
            }

            // Stateの構築 
            nshogi::core::State state = nshogi::io::csa::StateBuilder::newState(csa_data);

            // 平手かどうかのチェック
            if (nshogi::io::csa::positionToCSA(state.getInitialPosition()) != hirate_csa_str) {
                continue;
            }

            // 全ての条件をクリアしたら定跡として追加
            addBookBuf(state, bookBuf);
            processed_count++;
            file_count++;

            // 10000局ごとにbookと合わせる.
            if (processed_count % 10000 == 0) {
                std::sort(bookBuf.begin(), bookBuf.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });
                addBook(bookBuf);

                bookBuf.clear();
            }

        } catch (const std::exception& e) {
            std::cerr << "Error in " << entry.path() << " : " << e.what() << std::endl;
        }
    }
    
    // 残ったものを定石に入れる.
    std::sort(bookBuf.begin(), bookBuf.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    addBook(bookBuf);

    std::cout << "Total processed games: " << processed_count << std::endl;
    std::cout << "Total book entries: " << book.size() << std::endl;
}

int main() {
    // 棋譜の入ったフォルダ
    std::string target_dir = "C:/Users/ksasa/source/repos/shogi_ai/kifu";
    
    std::cout << "Starting book generation..." << std::endl;
    bookgenerator(target_dir);
    
    saveBook();

    std::cout << "over : " << max_over_count << std::endl; 

    return 0;
}