#pragma once

#include "../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "TT.h"
// #include "movepicker.h"
#include "movepicker2.h"
#include <algorithm>
#include <vector>
#include <limits>

namespace engine
{
    // オーバーフローを防ぐための安全な無限大
    constexpr int16_t INF = 30000;

    // NMP いつまで行うか
    constexpr int R = 3;

    // 探索結果を格納する構造体
    struct SearchResult
    {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0;
        int32_t depth = 5;
    };

    inline constexpr int MaxPly = 128;

    using KillerMovePair = ::std::array<nshogi::core::Move32, 2>;
    using KillersTable = ::std::array<KillerMovePair, MaxPly>;

    inline thread_local KillersTable killers = []
    {
        KillersTable k{};
        for (auto &p : k)
        {
            p[0] = nshogi::core::Move32::MoveNone();
            p[1] = nshogi::core::Move32::MoveNone();
        }
        return k;
    }();

    inline void resetKillers() noexcept
    {
        for (auto &p : killers)
        {
            p[0] = nshogi::core::Move32::MoveNone();
            p[1] = nshogi::core::Move32::MoveNone();
        }
    }

    template <nshogi::core::Color C>
    int16_t qsearch(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta)
    {
        bool in_check = st.isInCheck();

        int16_t best = -INF - 1;

        // 王手されていない時のみ stand_pat の評価とカットを行う
        if (!in_check)
        {
            int16_t stand_pat = st.eval<C>(); // または nnue::eval::eval<C>(st)
            if (stand_pat >= beta)
            {
                return stand_pat;
            }
            alpha = std::max(alpha, stand_pat);
            best = stand_pat;
        }

        if (depth == 0)
        {
            return in_check ? alpha : best;
        }

        auto &state = st.getState();
        MovePicker2<true> moves(state, nshogi::core::Move32::MoveNone());

        int legal_moves_played = 0;
        auto mv = moves.next();
        while (mv != nshogi::core::Move32::MoveNone())
        {
            st.doMove<C>(mv);
            int16_t score = -qsearch<~C>(st, depth - 1, -beta, -alpha);
            st.undoMove();

            legal_moves_played++;

            // より良いスコアを見つけたら best を更新する（ここを追加！）
            if (score > best)
            {
                best = score;
            }

            // ベータカット (Fail-Soft)
            if (best >= beta)
            {
                return best;
            }

            alpha = std::max(alpha, best);

            mv = moves.next();
        }

        // 王手されていて、合法な回避手が1つもなかった場合は「詰み」
        if (in_check && legal_moves_played == 0)
        {
            return -INF; // 探索深さ(ply)の概念がqsearchにはないので固定の負けスコアを返す
        }

        // alpha ではなく、実際に見つけたベストスコアを返す
        return best;
    }

    template <nshogi::core::Color C, bool allow_null = true>
    int16_t negamax(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta, int age, int ply)
    {
        int16_t oriAlpha = alpha;
        auto &state = st.getState();
        const auto repetition = state.getRepetitionStatus();
        // 千日手の判定
        switch (repetition)
        {
        case nshogi::core::RepetitionStatus::WinRepetition:
            return 30000 - ply; // 王手連続の千日手勝ち
        case nshogi::core::RepetitionStatus::LossRepetition:
            return -30000 + ply; // 王手連続の千日手負け
        case nshogi::core::RepetitionStatus::Repetition:
            return 0; // 通常の千日手
        case nshogi::core::RepetitionStatus::SuperiorRepetition:
            return 30000 - ply;
        case nshogi::core::RepetitionStatus::InferiorRepetition:
            return -30000 + ply;
        default:
            break;
        }

        if (depth == 0)
        {
            return qsearch<C>(st, 50, alpha, beta);
        }

        nshogi::core::Move32 best_move = nshogi::core::Move32::MoveNone();

        constexpr nshogi::core::Color Oppo = ~C;

        // 【修正】INT_MINではなく安全な負の無限大を使用
        int16_t best = -INF - 1;

        int16_t tt_score;
        const uint64_t hash = st.getHash();
        TTEntry *entry = TT.probe(hash);
        if (TT.hasUseHash(entry, hash, depth, alpha, beta, &tt_score))
        {
            return tt_score;
        }

        // 1. NMPのための静的評価値チェック（TTにスコアがあればそれを使う実装に拡張も可能）
        int16_t static_eval = st.eval<C>();

        // NMP
        // if (!st.isInCheck() && depth > R && allow_null && static_eval >= beta){
        //     st.doNullMove();
        //     int16_t score = -negamax<Oppo, false>(st, depth-1-R, -beta, -beta+1, age, ply+1);
        //     st.undoNullMove();
        //     if(score >= beta) {
        //         TT.store(hash, nshogi::core::Move32::MoveNone(), score, 0, depth, BOUND_LOWER, age);
        //         return score;
        //     }
        // }

        // TTから前回の最善手を取得
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();
        if (TT.isHit(entry, hash))
        {
            tt_move = entry->move;
        }

        engine::OrderingInfo info{};
        info.hashMove = tt_move;
        if (0 <= ply && ply < engine::MaxPly)
        {
            info.killer1 = engine::killers[ply][0];
            info.killer2 = engine::killers[ply][1];
        }

        MovePicker2<false> moves(state, info);

        int legal_moves_played = 0;

        auto mv = moves.next();
        while (mv != nshogi::core::Move32::MoveNone())
        {
            st.doMove<C>(mv);
            int16_t score = -negamax<Oppo>(st, depth - 1, -beta, -alpha, age, ply + 1);
            st.undoMove();

            legal_moves_played++;

            if (score > best)
            {
                best = score;
                best_move = mv;
            }
            alpha = std::max(alpha, best);
            if (alpha >= beta)
            {
                if (0 <= ply && ply < engine::MaxPly)
                {
                    const bool is_capture = (best_move.capturePieceType() != nshogi::core::PTK_Empty);
                    if (!is_capture && best_move != engine::killers[ply][0])
                    {
                        engine::killers[ply][1] = engine::killers[ply][0];
                        engine::killers[ply][0] = best_move;
                    }
                }
                break;
            }

            mv = moves.next();
        }

        // 探索ループを抜けた後
        if (legal_moves_played == 0)
        {
            // 1手も指せなかった ＝ 詰まされている（またはステールメイト）
            return -(INF - ply);
        }

        uint32_t key = static_cast<uint32_t>(hash >> 32);
        Bound bound;
        if (best <= oriAlpha)
        {
            bound = BOUND_UPPER; // Fail-Low
        }
        else if (best >= beta)
        {
            bound = BOUND_LOWER; // Fail-High
        }
        else
        {
            bound = BOUND_EXACT; // PV Node
        }

        // 静的評価値(eval)は今度考える.
        TT.store(hash, best_move, best, 0, depth, bound, age);
        return best;
    }

    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st);

} // namespace engine