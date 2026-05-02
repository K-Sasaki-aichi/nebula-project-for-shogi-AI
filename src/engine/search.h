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
#include <atomic>
#include <vector>
#include <limits>
#include <chrono>

namespace engine
{
    // 探索の終了を知らせるフラグ
    extern std::atomic<bool> isStop;

    // 終了時間
    extern std::atomic<long long> limitTimeMs;

    // ベストの動きを共有する
    extern std::atomic<nshogi::core::Move32> sharedBestMove;

    // オーバーフローを防ぐための安全な無限大
    constexpr int16_t INF = 30000;

    // NMP いつまで行うか
    constexpr int R = 3;

    // 探索結果を格納する構造体
    struct SearchResult
    {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0;
        int32_t depth = 0;
    };

    struct ThreadData
    {
        uint64_t nodes = 0;
        bool isMain = false;
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

    inline void initKillers() noexcept
    {
        for (auto &p : killers)
        {
            p[0] = nshogi::core::Move32::MoveNone();
            p[1] = nshogi::core::Move32::MoveNone();
        }
    }

    template <nshogi::core::Color C>
    int16_t qsearch(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta, ThreadData &td)
    {
        td.nodes++;
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
            // isStop = true なら探索を終了
            // if (isStop.load(std::memory_order_relaxed))
            // {
            //     return alpha;
            // }

            st.doMove<C>(mv);
            int16_t score = -qsearch<~C>(st, depth - 1, -beta, -alpha, td);

            st.undoMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return alpha; // スコアを比較せず、すぐに抜ける
            }

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
    int16_t negamax(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta, int age, int ply, ThreadData &td)
    {
        td.nodes++;

        if (td.isMain && (td.nodes & 2047) == 0) {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            if (now_ms >= engine::limitTimeMs.load(std::memory_order_relaxed)) {
                engine::isStop.store(true, std::memory_order_relaxed);
            }
        }

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
        
        // 宣言勝ち
        if(state.canDeclare()){
            return INF - ply;
        }


        if (isStop.load(std::memory_order_relaxed))
        {
            return alpha;
        }

        if (depth == 0)
        {
            return qsearch<C>(st, 50, alpha, beta, td);
        }

        nshogi::core::Move32 best_move = nshogi::core::Move32::MoveNone();

        constexpr nshogi::core::Color Oppo = ~C;

        // 【修正】INT_MINではなく安全な負の無限大を使用
        int16_t best = -INF - 1;

        int16_t tt_score;
        const uint64_t hash = st.getHash();
        TTEntry entry;

        if (TT.read(hash, entry))
        {
            if (entry.depth >= depth)
            {
                int16_t tt_score = entry.score;
                Bound bound = entry.getBound();

                if (bound == BOUND_EXACT)
                    return tt_score;

                if (bound == BOUND_LOWER && tt_score >= beta)
                    return tt_score;

                if (bound == BOUND_UPPER && tt_score <= alpha)
                    return tt_score;
            }
        }

        // 1. NMPのための静的評価値チェック（TTにスコアがあればそれを使う実装に拡張も可能）
        int16_t static_eval = st.eval<C>();

        // ▼▼▼ RFP (Reverse Futility Pruning) ▼▼▼
        if (depth <= 2 && !st.isInCheck() && std::abs(static_eval) < 5000)
        {
            int margin = 200 * depth;
            if (static_eval - margin >= beta)
            {
                return static_eval;
            }
        }
        // ▲▲▲ RFP ▲▲▲

        // NMP
        int R_adaptive = 3 + depth / 6;
        if (!st.isInCheck() && depth > R_adaptive && allow_null && static_eval >= beta)
        {
            st.doNullMove();
            int16_t score = -negamax<Oppo, false>(st, depth - 1 - R, -beta, -beta + 1, age, ply + 1, td);

            st.undoNullMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return alpha; // スコアを比較せず、すぐに抜ける
            }

            if (score >= beta)
            {
                return score >= 20000 ? beta : score;
            }
        }

        // TTから前回の最善手を取得
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();
        if (TT.read(hash, entry))
        {
            tt_move = entry.move;
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
        bool is_in_check = st.isInCheck();

        auto mv = moves.next();
        while (mv != nshogi::core::Move32::MoveNone())
        {
            const bool is_capture = (mv.capturePieceType() != nshogi::core::PTK_Empty);
            const bool is_promotion = mv.promote();
            const bool is_good_capture = is_capture && isGoodCapture(state, mv);

            st.doMove<C>(mv);

            int16_t score;

            // 簡易LMR
            if (depth >= 3 && legal_moves_played >= 3 && !is_good_capture && !is_promotion && !is_in_check)
            {
                int reduction = 1;

                if (legal_moves_played >= 6)
                    reduction = 2;

                // 浅く探索する (depth - 1 - reduction)
                score = -negamax<Oppo>(st, depth - 1 - reduction, -beta, -alpha, age, ply + 1, td);

                // もし浅く読んだ結果が Alpha を超えた場合はフル計算
                if (!isStop.load(std::memory_order_relaxed) && score > alpha)
                {
                    score = -negamax<Oppo>(st, depth - 1, -beta, -alpha, age, ply + 1, td);
                }
            }
            else
            {
                score = -negamax<Oppo>(st, depth - 1, -beta, -alpha, age, ply + 1, td);
            }

            st.undoMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return alpha; // スコアを比較せず、すぐに抜ける
            }

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
            const auto lastMove = state.getLastMove();
            if(lastMove.drop() && lastMove.pieceType() == nshogi::core::PTK_Pawn){
                return INF - ply;
            }
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

    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st, int think_time);

} // namespace engine