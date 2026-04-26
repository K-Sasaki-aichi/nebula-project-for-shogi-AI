#pragma once

#include "../../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h" // Movesに必要
#include "StatewithNNUE.h"
#include "eval.h"
#include "TT.h"
#include <algorithm>
#include <limits>

namespace engine
{
    // 探索結果を格納する構造体
    struct SearchResult {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0; // 評価値
        int32_t deepth = 5;
    };

    struct ScoredMove {
        nshogi::core::Move32 move;
        int score;
    };

    inline void sortMoves(auto& moves, int size){
        std::vector<ScoredMove> ScoredMoves;
        ScoredMoves.reserve(size);
    }

    template<nshogi::core::Color C>
    int16_t negamax(nnue::StatewithNNUE& st, int depth, int16_t alpha, int16_t beta, int age){
        int16_t oriAlpha = alpha;
        auto &state = st.getState();
        const auto repetition = state.getRepetitionStatus();
        // 千日手の判定
        switch (repetition) {
            case nshogi::core::RepetitionStatus::WinRepetition:      return 30000;  // 王手連続の千日手勝ち
            case nshogi::core::RepetitionStatus::LossRepetition:     return -30000; // 王手連続の千日手負け
            case nshogi::core::RepetitionStatus::Repetition:         return 0;      // 通常の千日手
            case nshogi::core::RepetitionStatus::SuperiorRepetition: return 30000;
            case nshogi::core::RepetitionStatus::InferiorRepetition: return -30000;
            default: break;
        }

        if(depth == 0){
            return nnue::eval::eval<C>(st);
        }

        // オーバーフローを防ぐため、安全な値をINFとする
        constexpr int INF = 30000;
        nshogi::core::Move32 best_move = nshogi::core::Move32::MoveNone();
        
        const auto Moves = nshogi::core::MoveGenerator::generateLegalMoves(state);

        constexpr nshogi::core::Color Oppo = ~C;
        
        // 【修正】INT_MINではなく安全な負の無限大を使用
        int16_t best = -INF - 1;

        int16_t tt_score;
        uint64_t hash = st.getHash();
        TTEntry* entry = TT.probe(hash);
        if(TT.hasUseHash(entry, hash, depth, alpha, beta, &tt_score)) return tt_score;

        for (const auto mv : Moves){
            st.doMove<C>(mv);
            
            int16_t score = -negamax<Oppo>(st, depth-1, -beta, -alpha, age);
            
            st.undoMove();
            
            if(score > best){
                best = score;
                best_move = mv;
            }
            alpha = std::max(alpha, best);
            if(alpha >= beta){
                break;
            }
        }

        uint32_t key = static_cast<uint32_t>(hash >> 32); 
        if(best <= oriAlpha)  entry->save(key, best_move, best, 0, depth, BOUND_UPPER, age);
        else if(best >= beta) entry->save(key, best_move, best, 0, depth, BOUND_LOWER, age);
        else                  entry->save(key, best_move, best, 0, depth, BOUND_EXACT, age);

        return best;
    }

    template<nshogi::core::Color C>
    int16_t qsearch(nnue::StatewithNNUE& st, int depth, int16_t alpha, int16_t beta){
        int16_t stand_pat = nnue::eval::eval<C>(st);
        if(stand_pat >= beta){
            return stand_pat;
        }
        if(stand_pat > alpha){
            alpha = stand_pat;
        }

        if()
    }

    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st);

} // namespace engine