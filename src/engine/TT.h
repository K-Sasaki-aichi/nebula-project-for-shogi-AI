#pragma once
#include <vector>
#include "TTentry.h"

namespace engine{
class TranspositionTable{
    private:
        std::vector<TTEntry> table;
        uint64_t mask;

    public:
        TranspositionTable() {
            resize(1024*4); // デフォルトで1GB確保
        }
        void resize(size_t size_mb) {
            // 16バイト(sizeof(TTEntry)) で割って、何個のエントリが入るか計算
            size_t num_entries = (size_mb * 1024 * 1024) / sizeof(TTEntry);
            
            // ハッシュのインデックス計算を高速化（& mask）するため、
            size_t p2 = 1;
            while (p2 * 2 <= num_entries) p2 *= 2;

            // std::vector の assign を使うと、古いメモリが解放され、
            table.assign(p2, TTEntry{});
            mask = p2 - 1;
        }

        inline TTEntry* probe(uint64_t hashVal) {
            return &table[hashVal & mask];
        }

        inline bool isHit(const TTEntry* entry, uint64_t hash) const {
            return entry->key == static_cast<uint32_t>(hash >> 32);
        }

        inline bool hasUseHash(const TTEntry* entry, uint64_t hash, int depth, int16_t alpha, int16_t beta, int16_t* tt_score) const {
            if(!isHit(entry, hash)) return false;

            if (entry->depth < depth) return false;

            *tt_score = entry->score;
            Bound bound = entry->getBound();

            return (bound == BOUND_EXACT)
                    || (bound == BOUND_LOWER && *tt_score >= beta)
                    || (bound == BOUND_UPPER && *tt_score <= alpha);

        }

        inline void store(uint64_t hash, nshogi::core::Move32 move,
                        int16_t score, int16_t eval, int8_t depth,
                        Bound bound, uint8_t age)
        {
            TTEntry* entry = probe(hash);

            // 64ビットのハッシュから、上位32ビットを衝突確認用キーとして抽出
            uint32_t key = static_cast<uint32_t>(hash >> 32); 

            if (entry->key == key && move.isNone()) {
                move = entry->move;
            }

            // 世代 (Age) が古いエントリは無条件で上書き
            // 世代が同じでも、今回の探索深さ (Depth) の方が深ければ上書き
            if (entry->getAge() != age || entry->depth <= depth) {
                // entryポインタ経由でsaveを呼び出す
                entry->save(key, move, score, eval, depth, bound, age);
            }
            
        }
};

extern TranspositionTable TT;

} //namespace engine