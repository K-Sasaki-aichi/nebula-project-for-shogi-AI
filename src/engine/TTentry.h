#pragma once
#include <cstdint>
#include "../nshogi/src/core/types.h"


    // バウンドの種類（2ビットで収まる）
    enum Bound : uint8_t {
        BOUND_NONE  = 0,
        BOUND_UPPER = 1, // α値以下（これ以上良くならない）
        BOUND_LOWER = 2, // β値以上（これより悪くならない）
        BOUND_EXACT = 3  // 正確なスコア
    };

    // 16バイトにアライメントされた置換表エントリ
    // CPUキャッシュに最適化するため、合計サイズが16の倍数になるように設計します
    struct alignas(16) TTEntry {
        uint32_t key;               // 4 bytes: Zobrist Keyの上位32ビット（衝突確認用）
        nshogi::core::Move32 move;  // 4 bytes: 最善手
        int16_t  score;             // 2 bytes: 探索で得られた評価値
        int16_t  eval;              // 2 bytes: NNUEの静的評価値（Static Evaluation）
        uint8_t  depth;             // 1 byte : 探索深さ (0~255)
        uint8_t age;                // 1 byte : age(世代)
        uint8_t  bound;             // 1 byte : Bound(評価値の種類)
        uint8_t padding;            // 1 bytes: 16バイトに揃えるためのパディング

        // バウンドの取得
        inline Bound getBound() const {
            return static_cast<Bound>(bound);
        }

        // 世代の取得
        inline uint8_t getAge() const {
            return age; // 上位6ビット
        }

        // 書き込み用
        inline void save(
          uint32_t k,
          nshogi::core::Move32 m,
          int16_t s,
          int16_t e,
          uint8_t d,
          Bound b,
          uint8_t a)
        {
            key = k;
            move = m;
            score = s;
            eval = e;
            depth = d;
            bound = static_cast<uint8_t>(b);
            age = a;
        }
        // inline void save(uint32_t k, nshogi::core::Move32 m, int16_t s, int16_t e, uint8_t d, Bound b, uint8_t age) {
        //     move = m;
        //     score = s;
        //     eval = e;
        //     depth = d;
        //     bound_age = (age << 2) | b;
        //     key = k;
        // }
    };