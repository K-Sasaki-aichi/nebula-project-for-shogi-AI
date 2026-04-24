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
    template<nshogi::core::Color C>
    int32_t negamax(nnue::StatewithNNUE& st, int depth, int alpha, int beta){
        if(depth == 0){
            return nnue::eval::eval<C>(st);
        }

        // オーバーフローを防ぐため、安全な値をINFとする
        constexpr int INF = 30000;

        auto &state = st.getState();
        const auto Moves = nshogi::core::MoveGenerator::generateLegalMoves(state);
        
        if(Moves.size() == 0) return -INF - depth;

        // 【修正】enum class 対策として一度 int にキャストしてからビット反転する
        constexpr nshogi::core::Color Oppo = static_cast<nshogi::core::Color>(static_cast<int>(C) ^ 1);
        
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

    [[nodiscard]] nshogi::core::Move32 searchNNUE(nnue::StatewithNNUE &st);

} // namespace engine