#pragma once
#include <vector>
#include <cstring>
#include "TTentry.h"

namespace engine {

class TranspositionTable {
private:
    std::vector<TTEntry> table;
    uint64_t mask;

public:
    TranspositionTable() {
        resize(1024 * 2);
    }

    void resize(size_t size_mb) {
        size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTEntry);

        size_t p2 = 1;
        while (p2 * 2 <= num_entries) p2 <<= 1;

        table.resize(p2);
        mask = p2 - 1;
    }

    inline TTEntry* probe(uint64_t hash) {
        return &table[hash & mask];
    }

    inline bool read(uint64_t hash, TTEntry& out) const {
        TTEntry* entry = const_cast<TTEntry*>(&table[hash & mask]);

        uint32_t key = static_cast<uint32_t>(hash >> 32);

        uint32_t k1 = entry->key;
        if (k1 != key) return false;

        // コピー
        std::memcpy(&out, entry, sizeof(TTEntry));

        uint32_t k2 = entry->key;

        // 安全のためにダブルチェック
        if (k1 != k2 || k1 != key)
            return false;

        return true;
    }

    inline void store(uint64_t hash,
                      nshogi::core::Move32 move,
                      int16_t score,
                      int16_t eval,
                      int8_t depth,
                      Bound bound,
                      uint8_t age)
    {
        TTEntry* entry = &table[hash & mask];
        uint32_t key = static_cast<uint32_t>(hash >> 32);

        // 上書き条件（Stockfish簡易版）
        if (entry->key == key) {
            if (entry->depth > depth)
                return;
        }

        entry->move = move;
        entry->score = score;
        entry->eval = eval;
        entry->depth = depth;
        entry->bound_age = (age << 2) | bound;
        entry->key = key;
    }
};

extern TranspositionTable TT;

} // namespace engine