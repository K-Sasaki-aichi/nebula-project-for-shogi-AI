#include "../src/nshogi/src/io/csa.h"
#include "../src/nshogi/src/io/sfen.h"
#include "../src/nshogi/src/core/types.h"
#include "../src/nshogi/src/core/state.h"
#include "../src/nshogi/src/core/position.h"
#include <fstream>
#include <string>
#include <iostream>
#include <filesystem>
#include <vector>
#include <utility>
#include <algorithm>
#include <tuple>

namespace fs = std::filesystem;
using Move32 = nshogi::core::Move32;
int PTK_Empty = nshogi::core::PTK_Empty;

std::vector<std::tuple<uint64_t, Move32, uint32_t>> book;
const nshogi::core::State base_hirate_state = nshogi::core::StateBuilder::getInitialState();
int max_over_count = 0;

/* 設定 */
const double min_rate = 2800; // レートの下限
const int ply_set = 40;       // 何手までの定石か
const int min_count = 4;      // その局面が出現するべき最小値

// 【改善点1】高速版：レーティング制限関数（正規表現を使わない）
bool isHighQualityGame(const std::string& csa_data, double threshold = 2800.0) {
    auto extractRate = [](const std::string& data, const std::string& key) -> double {
        size_t pos = data.find(key);
        if (pos == std::string::npos) return 0.0;
        
        pos += key.length();
        size_t nl_pos = data.find('\n', pos); // 行末を探す
        if (nl_pos == std::string::npos) nl_pos = data.length();
        
        std::string line = data.substr(pos, nl_pos - pos);
        
        // 最後の ':' 以降の数値を抽出する
        size_t last_colon = line.find_last_of(':');
        try {
            if (last_colon != std::string::npos) {
                return std::stod(line.substr(last_colon + 1));
            } else {
                return std::stod(line);
            }
        } catch (...) {
            return 0.0;
        }
    };

    double black_rate = extractRate(csa_data, "'black_rate:");
    // 先手が基準を満たしていなければ後手を調べる前に弾く（高速化）
    if (black_rate < threshold) return false;

    double white_rate = extractRate(csa_data, "'white_rate:");
    return white_rate >= threshold;
}

// 定跡追加関数
void addBookBuf(const nshogi::core::State& full_state, std::vector<std::pair<uint64_t, Move32>>& bookBuf) {
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

        if(max_count >= min_count) optimized_book.push_back({current_hash, best_move, max_count});
        
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

// 【改善点2】高速版：ファイル読み込み（stringstreamを廃止し、バイナリで一括読み込み）
std::string readFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary); 
    if (!ifs) throw std::runtime_error("File not found: " + path);
    
    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();
    if (size == 0) return "";
    
    std::string buffer(size, '\0');
    ifs.seekg(0, std::ios::beg);
    ifs.read(&buffer[0], size);
    
    return buffer;
}

void bookgenerator(const std::string& dir){
    nshogi::core::Position hirate_pos = nshogi::core::PositionBuilder::getInitialPosition();
    std::string hirate_csa_str = nshogi::io::csa::positionToCSA(hirate_pos);
    int processed_count = 0;
    int file_count = 0;

    std::vector<std::pair<uint64_t, Move32>> bookBuf;
    
    // 【改善点3】メモリの事前確保（再確保によるオーバーヘッドを削減）
    bookBuf.reserve(500000); 

    for (const auto& entry : fs::directory_iterator(dir)) {
        try {
            // 進捗表示 (1000局ごと)
            if (file_count % 1000 == 0) {
                std::cout << "file :  " << file_count << std::endl;
                std::cout << "Processed " << processed_count << " high-quality games." << std::endl;
            }

            if (!fs::is_regular_file(entry)) continue;

            std::string path = entry.path().string();
            std::string csa_data = readFile(path);

            // R2800以上かチェック
            if (!isHighQualityGame(csa_data, min_rate)) {
                file_count++;
                continue;
            }

            // Stateの構築 
            nshogi::core::State state = nshogi::io::csa::StateBuilder::newState(csa_data);

            // 平手かどうかのチェック
            if (nshogi::io::csa::positionToCSA(state.getInitialPosition()) != hirate_csa_str) {
                file_count++;
                continue;
            }

            // 全ての条件をクリアしたら定跡として追加
            addBookBuf(state, bookBuf);
            processed_count++;
            file_count++;

            // 10000局ごとにbookと合わせる
            if (processed_count > 0 && processed_count % 10000 == 0) {
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
    
    // 残ったものを定跡に入れる
    if (!bookBuf.empty()) {
        std::sort(bookBuf.begin(), bookBuf.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        addBook(bookBuf);
    }

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