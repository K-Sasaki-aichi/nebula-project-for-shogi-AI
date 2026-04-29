#pragma once

#include "../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "TT.h"
#include "movepicker.h"
#include <algorithm>
#include <vector>
#include <limits>

namespace engine
{
    // オーバーフローを防ぐための安全な無限大
    constexpr int16_t INF = 30000;

    // 探索結果を格納する構造体
    struct SearchResult {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0;
        int32_t depth = 5;
    };


    template<nshogi::core::Color C>
    int16_t qsearch(nnue::StatewithNNUE& st, int depth, int16_t alpha, int16_t beta){
        int16_t stand_pat = nnue::eval::eval<C>(st);
        if(stand_pat >= beta){
            return stand_pat;
        }
        if(stand_pat > alpha){
            alpha = stand_pat;
        }

        if (depth == 0) {
            return alpha;
        }

        auto &state = st.getState();
        MovePicker<true> moves(state, nshogi::core::Move32::MoveNone());

        auto mv = moves.next();
        while(mv != nshogi::core::Move32::MoveNone()){
            st.doMove<C>(mv);
            int16_t score = -qsearch<~C>(st, depth-1, -beta, -alpha);
            st.undoMove();
            
            if(score >= beta){
                return score;
            }
            if(score > alpha){
                alpha = score;
            }

            mv = moves.next();
        }

        return alpha;
    }

    template<nshogi::core::Color C, bool allow_null>
    int16_t negamax(nnue::StatewithNNUE& st, int depth, int16_t alpha, int16_t beta, int age, int ply){
        // オーバーフローを防ぐため、安全な値をINFとする
        constexpr int INF = 30000;

        int16_t oriAlpha = alpha;
        auto &state = st.getState();
        const auto repetition = state.getRepetitionStatus();
        // 千日手の判定
        switch (repetition) {
            case nshogi::core::RepetitionStatus::WinRepetition:      return 30000-ply;  // 王手連続の千日手勝ち
            case nshogi::core::RepetitionStatus::LossRepetition:     return -30000+ply; // 王手連続の千日手負け
            case nshogi::core::RepetitionStatus::Repetition:         return 0;      // 通常の千日手
            case nshogi::core::RepetitionStatus::SuperiorRepetition: return 30000-ply;
            case nshogi::core::RepetitionStatus::InferiorRepetition: return -30000+ply;
            default: break;
        }

        if(depth == 0){
            return qsearch<C>(st, 50, alpha, beta);
        }

        nshogi::core::Move32 best_move = nshogi::core::Move32::MoveNone();

        constexpr nshogi::core::Color Oppo = ~C;
        
        // 【修正】INT_MINではなく安全な負の無限大を使用
        int16_t best = -INF - 1;

        int16_t tt_score;
        const uint64_t hash = st.getHash();
        TTEntry* entry = TT.probe(hash);
        if(TT.hasUseHash(entry, hash, depth, alpha, beta, &tt_score)) {
            return tt_score;
        }


        // NMP
        if constexpr(allow_null){
            
        }


        // TTから前回の最善手を取得
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();
        if (TT.isHit(entry, hash)) { 
            tt_move = entry->move;
        }

        MovePicker<false> moves(state, tt_move);

        int legal_moves_played = 0;

        auto mv = moves.next();
        while(mv != nshogi::core::Move32::MoveNone()){
            st.doMove<C>(mv);    
            int16_t score = -negamax<Oppo>(st, depth-1, -beta, -alpha, age, ply+1);    
            st.undoMove();
            
            legal_moves_played++;

            if(score > best){
                best = score;
                best_move = mv;
            }
            alpha = std::max(alpha, best);
            if(alpha >= beta){
                break;
            }

            mv = moves.next();
        }

        // 探索ループを抜けた後
        if (legal_moves_played == 0) {
            // 1手も指せなかった ＝ 詰まされている（またはステールメイト）
            return -(INF - ply); 
        }

        uint32_t key = static_cast<uint32_t>(hash >> 32); 
        Bound bound;
        if (best <= oriAlpha) {
            bound = BOUND_UPPER; // Fail-Low
        } else if (best >= beta) {
            bound = BOUND_LOWER; // Fail-High
        } else {
            bound = BOUND_EXACT; // PV Node
        }
        
        // 静的評価値(eval)は今度考える.
        TT.store(hash, best_move, best, 0, depth, bound, age);
        return best;
    }


    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st);


} // namespace engine