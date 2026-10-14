#pragma once

#include "../nshogi/src/core/types.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/movegenerator.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "TT.h"
#include "../nshogi/src/io/sfen.h"
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

    inline constexpr int MaxPly = 256;

    // 探索結果を格納する構造体
    struct SearchResult
    {
        nshogi::core::Move32 bestMove = nshogi::core::Move32::MoveNone();
        nshogi::core::Move32 ponderMove = nshogi::core::Move32::MoveNone();
        int32_t score = 0;
        int32_t depth = 0;
    };

    struct ThreadData
    {
        uint64_t nodes = 0;
        bool isMain = false;

        nshogi::core::Move32 pvTable[MaxPly][MaxPly];
        int pvLength[MaxPly];

        // ヒストリーヒューリスティック用の配列
        // [Piece][To]
        int history_table[32][81] = {{0}};

        void update_history(nshogi::core::Move32 move, int bonus, nshogi::core::Color C){
            int pt = static_cast<int>(move.pieceType());
            int color_offset = (C == nshogi::core::Color::White) ? 16 : 0;
            int piece_idx = pt + color_offset;
            int to_idx = static_cast<int>(move.to());

            constexpr int HISTORY_MAX = 16384;
            int& score = history_table[piece_idx][to_idx];

            score -= score * std::abs(bonus) / HISTORY_MAX;
            score += bonus;
        }
    };

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

    inline void print_pv(int depth, int score, uint64_t nodes, const ThreadData& td)
    {
        // ここでハッシュ使用率を取得（パーミル単位: 0〜1000）
        int hashfull = TT.hashFull();

        std::cout << "info depth " << depth << " nodes " << nodes << " hashfull " << hashfull << " score cp " << score << " pv";
        for (int j = 0; j < td.pvLength[0] && j < MaxPly; ++j)
        {
            std::cout << " " << nshogi::io::sfen::move32ToSfen(td.pvTable[0][j]);
        }
        std::cout << std::endl;
    }

    template <nshogi::core::Color C>
    int16_t qsearch(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta, int ply, ThreadData &td)
    {
        td.nodes++;
        bool in_check = st.isInCheck();

        int16_t best = -INF - 1;
        int16_t stand_pat = -INF - 1;

        // 王手されていない時のみ stand_pat の評価とカットを行う
        if (!in_check)
        {
            stand_pat = st.eval<C>(); // または nnue::eval::eval<C>(st)
            if (stand_pat >= beta)
            {
                return stand_pat;
            }
            alpha = std::max(alpha, stand_pat);
            best = stand_pat;
        }

        // depth == 0 でも、王手がかかっている場合はすぐに打ち切らずに回避手を生成させる
        if (depth <= 0 && !in_check)
        {
            return best;
        }

        auto &state = st.getState();
        
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();
        const uint64_t hash = st.getHash();
        TTEntry entry;
        if (TT.read(hash, entry))
        {
            tt_move = entry.move;
        }

        MovePicker2<true> moves(state, tt_move);

        int legal_moves_played = 0;
        auto mv = moves.next();
        while (mv != nshogi::core::Move32::MoveNone())
        {
            // isStop = true なら探索を終了
            // if (isStop.load(std::memory_order_relaxed))
            // {
            //     return alpha;
            // }

            if (!in_check){
                const auto cap = mv.capturePieceType();
                if (cap != nshogi::core::PTK_Empty)
                {
                    // 「現在の評価値 + 取る駒の価値 + (マージン)」
                    int16_t futility_value = stand_pat + pieceValue(cap) + 400;

                    // それでも alpha 以下にしかならないなら、絶対に alpha を更新できないためスキップ
                    if (futility_value <= alpha)
                    {
                        mv = moves.next();
                        continue; // doMoveせずに次の手へ！
                    }
                }
            }

            st.doMove<C>(mv);
            int16_t score = -qsearch<~C>(st, depth - 1, -beta, -alpha, ply + 1, td);

            st.undoMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return 0; // スコアを比較せず、すぐに抜ける
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
            return -INF + ply;
        }

        // alpha ではなく、実際に見つけたベストスコアを返す
        return best;
    }

    template <nshogi::core::Color C, bool allow_null = true>
    int16_t negamax(nnue::StatewithNNUE &st, int depth, int16_t alpha, int16_t beta, int age, int ply, ThreadData &td)
    {
        td.nodes++;

        if (td.isMain && (td.nodes & 2047) == 0)
        {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            if (now_ms >= engine::limitTimeMs.load(std::memory_order_relaxed))
            {
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
            return INF - ply; // 王手連続の千日手勝ち
        case nshogi::core::RepetitionStatus::LossRepetition:
            return -INF + ply; // 王手連続の千日手負け
        case nshogi::core::RepetitionStatus::Repetition:
            return 0; // 通常の千日手
        case nshogi::core::RepetitionStatus::SuperiorRepetition:
            return 20000;
        case nshogi::core::RepetitionStatus::InferiorRepetition:
            return -20000;
        default:
            break;
        }

        // 宣言勝ち
        if (state.canDeclare())
        {
            return INF - ply;
        }

        if (isStop.load(std::memory_order_relaxed))
        {
            return 0;
        }

        if (depth == 0)
        {
            td.pvLength[ply] = 0;
            return qsearch<C>(st, 50, alpha, beta, ply, td);
        }

        nshogi::core::Move32 best_move = nshogi::core::Move32::MoveNone();

        constexpr nshogi::core::Color Oppo = ~C;

        // 【修正】INT_MINではなく安全な負の無限大を使用
        int16_t best = -INF - 1;

        int16_t tt_score;
        const uint64_t hash = st.getHash();
        TTEntry entry;
        nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();

        //一度実験の為に消す
        //なんでネストになってるの？
        if (TT.read(hash, entry))
        {
            tt_move = entry.move;

            if (entry.depth >= depth)
            {
                int16_t tt_score = entry.score;
                Bound bound = entry.getBound();

                // ハッシュで取るときに詰みの場合を考える
                if(tt_score > 29000) tt_score -= ply;
                else if(tt_score < -29000) tt_score += ply;

                bool cutoff = false;
                if (bound == BOUND_EXACT)
                    cutoff = true;

                if (bound == BOUND_LOWER && tt_score >= beta)
                    cutoff = true;

                if (bound == BOUND_UPPER && tt_score <= alpha)
                    cutoff = true;

                if(cutoff){
                    // TTカットオフ時: tt_move が存在すれば PV にセットする
                    if (!tt_move.isNone())
                    {
                        td.pvTable[ply][0] = tt_move;
                        td.pvLength[ply] = 1;
                    }
                    else
                    {
                        td.pvLength[ply] = 0;
                    }
                    return tt_score;
                }
            }
        }

        // 途中で終了してもいいように
        td.pvLength[ply] = 0;

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
        if (ply > 0 && !st.isInCheck() && depth > R_adaptive && allow_null && static_eval >= beta)
        {
            st.doNullMove();
            int16_t score = -negamax<Oppo, false>(st, depth - 1 - R_adaptive, -beta, -beta + 1, age, ply + 1, td);

            st.undoNullMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return 0; // スコアを比較せず、すぐに抜ける
            }

            if (score >= beta)
            {
                int16_t return_score = score >= 20000 ? beta : score;
                TT.store(hash, nshogi::core::Move32::MoveNone(), return_score, 0, depth - 1 - R_adaptive, BOUND_LOWER, age);
                return return_score;
            }
        }

        engine::OrderingInfo info{};
        info.hashMove = tt_move;
        info.history = td.history_table;

        if (0 <= ply && ply < engine::MaxPly)
        {
            info.killer1 = engine::killers[ply][0];
            info.killer2 = engine::killers[ply][1];
        }

        MovePicker2<false> moves(state, info);

        int legal_moves_played = 0;

        const bool is_in_check = st.isInCheck();

        nshogi::core::Move32 quiet_moves_tried[64];
        int quiet_count = 0;

        auto mv = moves.next();
        while (mv != nshogi::core::Move32::MoveNone())
        {
            const bool is_capture = (mv.capturePieceType() != nshogi::core::PTK_Empty);
            const bool is_promotion = mv.promote();
            const bool is_good_capture = is_capture && isGoodCapture(state, mv);

            const bool is_quiet = !is_capture && !is_promotion;
            if(is_quiet && quiet_count < 64){
                quiet_moves_tried[quiet_count++] = mv;
            }
            
            st.doMove<C>(mv);

            const bool gives_check = st.isInCheck();

            int16_t score;

            // PVS
            if(legal_moves_played == 0){
                // １手目はフルウィンドウ
                score = -negamax<Oppo>(st, depth - 1, -beta, -alpha, age, ply + 1, td);
            } else {
                // 簡易LMR
                if (ply > 0 && depth >= 3 && 
                    legal_moves_played >= 3 && 
                    !is_good_capture && 
                    !is_promotion && 
                    !is_in_check && 
                    !gives_check)
                {
                    int reduction = 1;

                    if (legal_moves_played >= 6)
                        reduction = 2;

                    // 浅く探索する (depth - 1 - reduction)
                    score = -negamax<Oppo>(st, depth - 1 - reduction, -alpha - 1, -alpha, age, ply + 1, td);

                    // もし浅く読んだ結果が Alpha を超えた場合はフル計算
                    if (!isStop.load(std::memory_order_relaxed) && score > alpha)
                    {
                        score = -negamax<Oppo>(st, depth - 1, -alpha - 1, -alpha, age, ply + 1, td);
                    }
                }
                else
                {
                    score = -negamax<Oppo>(st, depth - 1, -alpha - 1, -alpha, age, ply + 1, td);
                }

                // 2. Null Window探索の結果、1手目より良いスコア(score > alpha)が出た場合
                // かつ、ベータカットの基準値(beta)には届いていない場合、フルウィンドウで再探索して正確な値を測る
                if (score > alpha && score < beta && !isStop.load(std::memory_order_relaxed))
                {
                    score = -negamax<Oppo>(st, depth - 1, -beta, -alpha, age, ply + 1, td);
                }
            }

            st.undoMove();

            if (isStop.load(std::memory_order_relaxed))
            {
                return 0; // スコアを比較せず、すぐに抜ける
            }

            legal_moves_played++;

            if (score > best)
            {
                best = score;
                best_move = mv;
            }

            //alpha = std::max(alpha, best);
            if (best >= beta)
            {
                // キラーの更新
                if (0 <= ply && ply < engine::MaxPly)
                {
                    const bool is_capture = (best_move.capturePieceType() != nshogi::core::PTK_Empty);
                    if (!is_capture && best_move != engine::killers[ply][0])
                    {
                        engine::killers[ply][1] = engine::killers[ply][0];
                        engine::killers[ply][0] = best_move;
                    }
                }

                if(is_quiet){
                    // あまり大きくなり過ぎないようにキャップ
                    int bonus = std::min(depth * depth, 400);

                    // ヒストリーの更新
                    td.update_history(best_move, bonus, C);

                    for(int i = 0; i < quiet_count-1; i++){
                        td.update_history(quiet_moves_tried[i], -bonus, C);
                    }
                }

                break;
            }

            if(score > alpha){
                alpha = score;

                td.pvTable[ply][0] = mv;
                int child_len = td.pvLength[ply + 1];
                for(int j = 0; j < child_len; j++){
                    td.pvTable[ply][j + 1] = td.pvTable[ply + 1][j];
                }
                td.pvLength[ply] = child_len + 1;
            }

            mv = moves.next();
        }

        // 探索ループを抜けた後
        if (legal_moves_played == 0)
        {
            // 1手も指せなかった ＝ 詰まされている（またはステールメイト）
            const auto lastMove = state.getLastMove();
            if (lastMove.drop() && lastMove.pieceType() == nshogi::core::PTK_Pawn)
            {
                return INF - ply;
            }

            return -INF + ply;
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

        int16_t store_score = best;
        if (store_score > 29000) store_score += ply;
        else if (store_score < -29000) store_score -= ply;

        // 静的評価値(eval)は今度考える.
        if constexpr(allow_null){
            TT.store(hash, best_move, store_score, 0, depth, bound, age);
        }

        return best;
    }

    [[nodiscard]] SearchResult searchNNUE(nnue::StatewithNNUE &st, int think_time, int PLY);

} // namespace engine
