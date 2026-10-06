#include "book.h"
#include "../nshogi/src/core/types.h"
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <iostream>


// ファイル内だけで使うグローバル変数
static std::vector<BookEntry> g_loaded_book;

// 起動時に1回だけ呼ぶ
void loadBook(const std::string& filename = "book/book2.bin") {
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
        // デバッグ用にファイルがない場合はメッセージを出す
        std::cout << "info string book file not found: " << filename << std::endl;
        return;
    }

    g_loaded_book.clear();
    BookEntry entry;
    while (ifs.read(reinterpret_cast<char*>(&entry.hash), sizeof(entry.hash))) {
        ifs.read(reinterpret_cast<char*>(&entry.move), sizeof(entry.move));
        ifs.read(reinterpret_cast<char*>(&entry.count), sizeof(entry.count));
        g_loaded_book.push_back(entry);
    }
    
    // 二分探索のためにソート
    std::sort(g_loaded_book.begin(), g_loaded_book.end(), [](const BookEntry& a, const BookEntry& b) {
        return a.hash < b.hash;
    });

    std::cout << "info string book loaded: " << g_loaded_book.size() << " entries." << std::endl;
}

// 局面のハッシュから指し手を探す
nshogi::core::Move32 findBookMove(uint64_t hash) {
    auto it = std::lower_bound(g_loaded_book.begin(), g_loaded_book.end(), hash,
        [](const BookEntry& e, uint64_t h) {
            return e.hash < h;
        });

    if (it != g_loaded_book.end() && it->hash == hash) {
        return it->move;
    }
    return nshogi::core::Move32();
}