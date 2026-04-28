#pragma once

#include "../nshogi/src/core/types.h"
#include <vector>
#include <string>
#include <cstdint>

/**
 * @brief 定跡データのエントリ構造体
 */
struct BookEntry {
    uint64_t hash;          // 局面のハッシュ値
    nshogi::core::Move32 move; // 指し手
    uint32_t count;         // 出現回数
};

// 他のファイルからアクセスできるように宣言
extern std::vector<BookEntry> loaded_book;

/**
 * @brief 定跡ファイルをバイナリ形式で読み込みます
 * @param filename 読み込むファイルパス
 */
void loadBook(const std::string& filename);

/**
 * @brief 現在のハッシュ値に合致する指し手を探します
 * @param current_hash 局面のハッシュ値
 * @return 合致する指し手。見つからない場合は Move32() (isNone() == true) を返します
 */
nshogi::core::Move32 findBookMove(uint64_t current_hash);