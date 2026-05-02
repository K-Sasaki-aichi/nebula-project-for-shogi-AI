#pragma once
#include <vector>
#include <cstring>
#include "TTentry.h"

namespace engine
{

    class TranspositionTable
    {
    private:
        std::vector<TTEntry> table;
        uint64_t mask;

    public:
        TranspositionTable()
        {
            resize(1024 * 4);
        }

        void resize(size_t size_mb)
        {
            size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTEntry);

            size_t p2 = 1;
            while (p2 * 2 <= num_entries)
                p2 <<= 1;

            table.resize(p2);
            mask = p2 - 1;
        }

        inline TTEntry *probe(uint64_t hash)
        {
            return &table[hash & mask];
        }

        inline bool read(uint64_t hash, TTEntry &out) const
        {
            TTEntry *entry = const_cast<TTEntry *>(&table[hash & mask]);

            uint32_t key = static_cast<uint32_t>(hash >> 32);

            uint32_t k1 = entry->key;
            if (k1 != key)
                return false;

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
            TTEntry *entry = &table[hash & mask];
            uint32_t key = static_cast<uint32_t>(hash >> 32);

            bool replace = false;

            if (entry->key == key)
            {
                // 【同じ局面の場合】
                // 今の探索の方が深い（または同じ）、あるいは同じ深さでもより正確なBoundなら上書き
                // ※「depth >= entry->depth - 2」のように少し浅くても最新の情報を優先するテクニックもあります
                if (depth >= entry->depth)
                {
                    replace = true;
                }
            }
            else
            {
                // 【違う局面（ハッシュ衝突）の場合】
                // 1. TTのデータが「前の手番（古いAge）」のものなら、価値が低いので上書き
                if (entry->getAge() != age)
                {
                    replace = true;
                }
                // 2. 同じ手番の探索中なら、深さを比較して「今の探索の方が深い（価値が高い）」場合のみ上書き
                else if (depth >= entry->depth)
                {
                    replace = true;
                }
            }

            if (replace)
            {
                entry->key = key;
                entry->move = move;
                entry->score = score;
                entry->eval = eval;
                entry->depth = depth;
                entry->bound_age = (age << 2) | bound;
            }
        }
    };

    extern TranspositionTable TT;

} // namespace engine