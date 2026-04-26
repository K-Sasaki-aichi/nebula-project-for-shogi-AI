#pragma once

#include "../../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "TT.h"
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

    // オーダリング用の指し手とスコアのペア
    struct ScoredMove {
        nshogi::core::Move32 move;
        int score;
    };

    constexpr int PieceValueTable[nshogi::core::NumPieceType] = {
        0,      // PTK_Empty   (0)
        100,    // PTK_Pawn    (1)
        300,    // PTK_Lance   (2)
        300,    // PTK_Knight  (3)
        500,    // PTK_Silver  (4)
        800,    // PTK_Bishop  (5)
        1000,   // PTK_Rook    (6)
        600,    // PTK_Gold    (7)
        10000,  // PTK_King    (8)
        600,    // PTK_ProPawn   (9)
        600,    // PTK_ProLance  (10)
        600,    // PTK_ProKnight (11)
        600,    // PTK_ProSilver (12)
        1050,   // PTK_ProBishop (13)
        1250    // PTK_ProRook   (14)
    };

    // 指し手にスコアを付ける関数（MVV-LVA + 置換表の手 + 成り）
    inline int scoreMove(const nnue::StatewithNNUE& st, nshogi::core::Move32 mv, nshogi::core::Move32 ttMove = nshogi::core::Move32::MoveNone()) {
        if (mv == ttMove) {
            return 1000000; // 置換表の最善手を最優先
        }

        int score = 0;

        const int capPieceValue = PieceValueTable[mv.capturePieceType()];

        score += (capPieceValue != 0) * (100000 + capPieceValue - PieceValueTable[mv.pieceType()]);

        score += (mv.promote()) * 50000;

        return score;
    }

    inline std::vector<ScoredMove> sortMoves(const nnue::StatewithNNUE& st, const auto& moves, nshogi::core::Move32 ttMove = nshogi::core::Move32::MoveNone()) {
        std::vector<ScoredMove> scoredMoves;
        scoredMoves.reserve(moves.size());

        for (const auto mv : moves) {
            scoredMoves.push_back({mv, scoreMove(st, mv, ttMove)});
        }

        std::sort(scoredMoves.begin(), scoredMoves.end(), [](const ScoredMove& a, const ScoredMove& b) {
            return a.score > b.score;
        });

        return scoredMoves;
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

        if (depth == 0) {
            return alpha;
        }

        const auto Moves = nshogi::core::MoveGenerator::generateLegalCaptureMoves(st.getState());
        const auto orderedMoves = sortMoves(st, Moves);

        for(const auto& [mv, orderingScore] : orderedMoves){
            st.doMove<C>(mv);
            int16_t score = -qsearch<~C>(st, depth-1, -beta, -alpha);
            st.undoMove();

            if(score >= beta){
                return score;
            }
            if(score > alpha){
                alpha = score;
            }
        }

        return alpha;
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
            return qsearch<C>(st, 1, alpha, beta);
        }

        // オーバーフローを防ぐため、安全な値をINFとする
        constexpr int INF = 30000;
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

        // TTから前回の最善手を取得
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();
        if (TT.isHit(entry, hash)) { 
            tt_move = entry->move;
        }

        const auto Moves = nshogi::core::MoveGenerator::generateLegalMoves(state);
        const auto orderedMoves = sortMoves(st, Moves, tt_move);

        for (const auto& [mv, orderingScore] : orderedMoves){
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