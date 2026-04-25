#pragma once

#include "../../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h" // Movesに必要
#include "StatewithNNUE.h"
#include "eval.h"
#include <algorithm> // std::maxに必要
#include <limits>

namespace engine
{
    // 探索結果を格納する構造体
    struct SearchResult {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0; // 評価値
        int32_t deepth = 4;
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
    int32_t negamax(nnue::StatewithNNUE& st, int depth, int alpha, int beta){
        auto &state = st.getState();
        const auto repetition = state.getRepetitionStatus();
        switch (repetition)
        {
        case nshogi::core::RepetitionStatus::WinRepetition: // 連続王手の千日手（価値）
        case nshogi::core::RepetitionStatus::SuperiorRepetition: // 無意味なコマ捨ての連続
            /* code */
            break;
        case nshogi::core::RepetitionStatus::LossRepetition:
        case nshogi::core::RepetitionStatus::InferiorRepetition:
            break;

        case nshogi::core::RepetitionStatus::Repetition: // 千日手

        default:
            break;
        }

        if(depth == 0){
            return nnue::eval::eval<C>(st);
        }

        // オーバーフローを防ぐため、安全な値をINFとする
        constexpr int INF = 30000;

        
        
        const auto Moves = nshogi::core::MoveGenerator::generateLegalMoves(state);
        
        // if(Moves.size() == 0) return -INF - depth;

        constexpr nshogi::core::Color Oppo = ~C;
        
        // 【修正】INT_MINではなく安全な負の無限大を使用
        int32_t value = -INF - 1; 

        for (const auto mv : Moves){
            st.doMove<C>(mv);
            
            value = std::max(value, -negamax<Oppo>(st, depth-1, -beta, -alpha));
            
            st.undoMove();
            
            alpha = std::max(alpha, value);
            if(alpha >= beta){
                break;
            }
        }

        return value;
    }

    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st);

} // namespace engine