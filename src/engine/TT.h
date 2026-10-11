#pragma once
#include <atomic>
#include <memory>
#include <cstdint>
#include <vector>
#include <cstring>
#include "TTentry.h"

#include <mutex>

namespace engine
{

    // 共有テーブルに保存するスレッドセーフなエントリ
    struct alignas(16) TTSlot
    {
        std::atomic<uint32_t> key{0};
        std::atomic<uint32_t> move{0};
        std::atomic<int16_t> score{0};
        std::atomic<int16_t> eval{0};
        std::atomic<uint8_t> depth{0};
        std::atomic<uint8_t> age{0};
        std::atomic<uint8_t> bound{BOUND_NONE};
    };
    class TranspositionTable
    {
    private:
        std::unique_ptr<TTSlot[]> table;
        uint64_t mask = 0;

    public:
        TranspositionTable()
        {
            resize(1024 * 4);
        }

        void resize(size_t size_mb)
        {
            size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTSlot);

            size_t p2 = 1;
            while (p2 * 2 <= num_entries)
                p2 <<= 1;

            table = std::make_unique<TTSlot[]>(p2);
            mask = p2 - 1;
        }

        // 多分使ってないのでコメントアウトしてもよいはず。
        //ダメだったら教えてください。
        // inline TTEntry *probe(uint64_t hash)
        // {
        //     return &table[hash & mask];
        // }

        inline bool read(uint64_t hash, TTEntry &out) const
        {
            const TTSlot &entry = table[hash & mask];

            uint32_t key = static_cast<uint32_t>(hash >> 32);

            uint32_t k1 = entry.key.load(std::memory_order_acquire);

            if (k1 != key)
                return false;

            // 各フィールドをatomicで読み込む
            out.key = k1;
            out.move = nshogi::core::Move32::fromValue(
                entry.move.load(std::memory_order_relaxed));
            out.score =
                entry.score.load(std::memory_order_relaxed);
            out.eval =
                entry.eval.load(std::memory_order_relaxed);
            out.depth =
                entry.depth.load(std::memory_order_relaxed);
            out.age =
                entry.age.load(std::memory_order_relaxed);
            out.bound =
                entry.bound.load(std::memory_order_relaxed);

            // 安全のためのダブルチェック
            const uint32_t k2 =
                entry.key.load(std::memory_order_acquire);

            return k1 == k2 && k2 == key;
        }

        inline void store(uint64_t hash,
                          nshogi::core::Move32 move,
                          int16_t score,
                          int16_t eval,
                          int8_t depth,
                          Bound bound,
                          uint8_t age)
        {
            TTSlot &entry = table[hash & mask];
            uint32_t key = static_cast<uint32_t>(hash >> 32);

            bool replace = false;

            const uint32_t old_key =
                entry.key.load(std::memory_order_relaxed);
            const uint8_t old_depth =
                entry.depth.load(std::memory_order_relaxed);
            const uint8_t old_age =
                entry.age.load(std::memory_order_relaxed);

            if (old_key == key)
            {
                // 【同じ局面の場合】
                // 今の探索の方が深い（または同じ）、あるいは同じ深さでもより正確なBoundなら上書き
                if (depth > old_depth || (depth == old_depth && bound == BOUND_EXACT))
                {
                    replace = true;
                }
            }
            else
            {
                // 【違う局面（ハッシュ衝突）の場合】
                // 1. TTのデータが「前の手番（古いAge）」のものなら、価値が低いので上書き
                if (old_age != age){
                    replace = true;
                }
                // 2. 同じ手番の探索中なら、深さを比較して「今の探索の方が深い（価値が高い）」場合のみ上書き
                else if (depth >= old_depth){
                    replace = true;
                }
            }

            if(!replace) return;

        // 書き込み途中であることを示すキー
            entry.key.store(
                key ^ 1u, std::memory_order_relaxed);

            entry.move.store(
                move.value(), std::memory_order_relaxed);
            entry.score.store(
                score, std::memory_order_relaxed);
            entry.eval.store(
                eval, std::memory_order_relaxed);
            entry.depth.store(
                static_cast<uint8_t>(depth),
                std::memory_order_relaxed);
            entry.age.store(
                age, std::memory_order_relaxed);
            entry.bound.store(
                static_cast<uint8_t>(bound),
                std::memory_order_relaxed);
                
            // 最後にキーを公開する
            entry.key.store(
                key, std::memory_order_release);
        }
    };

    extern TranspositionTable TT;

} // namespace engine
