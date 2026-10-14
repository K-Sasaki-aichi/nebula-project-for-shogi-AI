#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"
#include "../book/book.h"

#include <cstdint>
#include <thread>

namespace engine
{
    TranspositionTable TT;

    std::atomic<bool> isStop(false);

    std::atomic<long long> limitTimeMs{0};
    // 各スレッドで共有するrootのbestmove
    // std::atomic<nshogi::core::Move32> sharedBestMove{
    //     nshogi::core::Move32::MoveNone()
    // };

    void helperThreadWorker(nshogi::core::State cloned_st, int thread_id, int target_depth, ThreadData &td, int PLY)
    {
        using nshogi::core::Black;
        using nshogi::core::White;

        nnue::StatewithNNUE st(std::move(cloned_st));

        const int depth = target_depth + 3;
        const int INF = 30000;

        const auto side = st.getSideToMove();
        const int age = PLY;
        const uint64_t hash = st.getHash();

        // 探索の多様化: スレッドごとに開始深さを変える
        int start_depth = 1 + (thread_id % 4);
        int step = 1 + (thread_id % 3);

        td.pvLength[0] = 0;

        for (int i = start_depth; i < depth; i += step)
        {
            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }
            int16_t alpha = -INF - 1;
            nshogi::core::Move32 local_best_move = nshogi::core::Move32::MoveNone();

            // auto shared = sharedBestMove.load(std::memory_order_relaxed);
            nshogi::core::Move32 tt_move = nshogi::core::Move32::MoveNone();

            TTEntry entry;
            if (TT.read(hash, entry))
            {
                tt_move = entry.move;
            }

            MovePicker2<false> moves(st.getState(), tt_move, thread_id + 1);

            auto mv = moves.next();

            switch (side)
            {
            case Black:
                while (mv != nshogi::core::Move32::MoveNone())
                {
                    if (isStop.load(std::memory_order_relaxed))
                        return;

                    st.doMove<Black>(mv);
                    int16_t score = -negamax<White>(st, i, -INF, -alpha, age, 0, td);
                    st.undoMove();
                    if (isStop.load(std::memory_order_relaxed))
                        return;

                    if (score > alpha)
                    {
                        alpha = score;
                        local_best_move = mv;
                    }

                    mv = moves.next();
                }
                break;
            default:
                while (mv != nshogi::core::Move32::MoveNone())
                {
                    if (isStop.load(std::memory_order_relaxed))
                        return;

                    st.doMove<White>(mv);
                    int16_t score = -negamax<Black>(st, i, -INF, -alpha, age, 0, td);
                    st.undoMove();

                    if (isStop.load(std::memory_order_relaxed))
                        return;

                    if (score > alpha)
                    {
                        alpha = score;
                        local_best_move = mv;
                    }

                    mv = moves.next();
                }
                break;
            }

            // if (!local_best_move.isNone())
            // {
            //     // sharedBestMove.store(local_best_move, std::memory_order_relaxed);
            //     TT.store(hash, local_best_move, alpha, 0, i, BOUND_EXACT, age);
            // }
        }
    }

    // 引数を StatewithNNUE の参照に変更します
    SearchResult searchNNUE(nnue::StatewithNNUE &st, int think_time, int PLY)
    {
        initKillers();
        using nshogi::core::Black;
        using nshogi::core::White;

        // if (limitTimeMs.load(std::memory_order_relaxed) != std::numeric_limits<long long>::max()){
        //     auto now = std::chrono::steady_clock::now().time_since_epoch();
        //     long long start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        //     limitTimeMs.store(start_ms + think_time, std::memory_order_relaxed);
        // }

        const int depth = 40;
        const int INF = 30000;
        const int NUM_THREADS = 4;
        std::vector<ThreadData> threadData(NUM_THREADS + 1);
        SearchResult result;

        // 定跡
        const uint64_t hash = st.getHash();

        if (PLY <= 50)
        {
            const nshogi::core::Move32 b_move = findBookMove(hash);
            if (!b_move.isNone())
            {
                result.bestMove = b_move;
                return result;
            }
        }

        isStop.store(false);
        std::vector<std::thread> threads;
        // sharedBestMove.store(nshogi::core::Move32::MoveNone(), std::memory_order_relaxed);

        for (int i = 0; i < NUM_THREADS; i++)
        {
            threads.push_back(std::thread(helperThreadWorker, st.getState().clone(), i, depth, std::ref(threadData[i]), PLY));
        }

        const auto side = st.getSideToMove();
        const int age = PLY;
        int16_t tt_score;
        ThreadData &main_td = threadData[NUM_THREADS];
        main_td.isMain = true;

        nshogi::core::Move32 prev_best_move = nshogi::core::Move32::MoveNone();
        int16_t prev_score = 0;
        int stable_count = 0;

        for (int i = 0; i < depth; i++)
        {
            main_td.pvLength[0] = 0;

            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }

            if (st.getState().canDeclare())
            {
                SearchResult res;
                res.bestMove = nshogi::core::Move32::MoveWin();
                res.score = INF;
                res.depth = 0;

                std::cout << "bestmove win" << std::endl;
                exit(0);
                // 探索せずに即座に終わる！
                return res;
            }

            int16_t alpha = -INF;
            int16_t beta = INF;

            int16_t window_alpha = -INF;
            int16_t window_beta = INF;
            int delta = 300;

            if (i >= 4) {
                window_alpha = std::max(static_cast<int>(-INF), prev_score - delta);
                window_beta = std::min(static_cast<int>(INF), prev_score + delta);
            }

            nshogi::core::Move32 current_best_move;
            int16_t current_best_score;

            // 窓が外れたとき用の再探索
            while(true){
                int16_t alpha = window_alpha;
                int16_t beta = window_beta;

                // add: この深さ(i)での最善手を一時的に保持する変数
                current_best_move = nshogi::core::Move32::MoveNone();
                current_best_score = -INF;

                nshogi::core::Move32 tt_move = result.bestMove;

                TTEntry entry;

                if (tt_move == nshogi::core::Move32::MoveNone() && TT.read(hash, entry))
                {
                    tt_move = entry.move;
                }

                MovePicker2<false> moves(st.getState(), tt_move);

                auto mv = moves.next();

                bool is_first_move = true;
            
                switch (side)
                {
                case Black:
                    while (mv != nshogi::core::Move32::MoveNone())
                    {
                        // if (isStop.load(std::memory_order_relaxed))
                        // {
                        //     break;
                        // }
                        st.doMove<Black>(mv);

                        int16_t score;

                        if(is_first_move){
                            // 1手目はフルウィンドウ
                            // アスピレーションで-betaから
                            score = -negamax<White>(st, i, -beta, -alpha, age, 1, main_td);
                            is_first_move = false;
                        } else {
                            // 2手目以降はNull Window
                            score = -negamax<White>(st, i, -alpha - 1, -alpha, age, 1, main_td);
                            // 予想が外れて良さそうな場合はフルウィンドウで再探索
                            if (score > alpha && score < beta){
                                //アスピレーションで-betaから
                                score = -negamax<White>(st, i, -beta, -alpha, age, 1, main_td);
                            }
                        }

                        st.undoMove();

                        if (isStop.load(std::memory_order_relaxed)) break;

                        if(score > current_best_score){
                            current_best_score = score;
                            current_best_move = mv;
                        }

                        if (score > alpha){
                            alpha = score;
                            
                            main_td.pvTable[0][0] = mv;

                            int child_len = main_td.pvLength[1];
                            for (int j = 0; j < child_len; ++j){
                                main_td.pvTable[0][j + 1] = main_td.pvTable[1][j];
                            }

                            main_td.pvLength[0] = child_len + 1;
                        }

                        if (alpha >= beta) break;

                        mv = moves.next();
                    }

                    break;

                default:
                    while (mv != nshogi::core::Move32::MoveNone())
                    {
                        // if (isStop.load(std::memory_order_relaxed))
                        // {
                        //     break;
                        // }
                        st.doMove<White>(mv);
                        
                        int16_t score;

                        if(is_first_move){
                            // 1手目はフルウィンドウ
                            score = -negamax<Black>(st, i, -beta, -alpha, age, 1, main_td);
                            is_first_move = false;
                        } else {
                            // 2手目以降はNull Window
                            score = -negamax<Black>(st, i, -alpha - 1, -alpha, age, 1, main_td);
                            // 予想が外れて良さそうな場合はフルウィンドウで再探索
                            if (score > alpha && score < beta){
                                score = -negamax<Black>(st, i, -beta, -alpha, age, 1, main_td);
                            }
                        }

                        st.undoMove();

                        if (isStop.load(std::memory_order_relaxed)) break;

                        if (score > current_best_score){
                            current_best_score = score;
                            current_best_move = mv;
                        }

                        if (score > alpha)
                        {
                            alpha = score;

                            main_td.pvTable[0][0] = mv;
                            int child_len = main_td.pvLength[1];
                            for (int j = 0; j < child_len; ++j){
                                main_td.pvTable[0][j + 1] = main_td.pvTable[1][j];
                            }

                            main_td.pvLength[0] = child_len + 1;
                        }

                        if(alpha >= beta) break;

                        mv = moves.next();
                    }

                    break;
                }

                if(isStop.load(std::memory_order_relaxed)) break;

                // ▼ アスピレーションウィンドウの再判定 ▼
                // 予想より悪かった（Fail-Low）→ 下限を広げて再探索
                if (current_best_score <= window_alpha && window_alpha > -INF) {
                    window_alpha = std::max(static_cast<int>(-INF), window_alpha - delta);
                    delta += delta / 2; // 次のために窓を大きくしておく
                    continue; 
                }
                // 予想より良かった（Fail-High）→ 上限を広げて再探索
                else if (current_best_score >= window_beta && window_beta < INF) {
                    window_beta = std::min(static_cast<int>(INF), window_beta + delta);
                    delta += delta / 2;
                    continue; 
                }

                // 窓の中にスコアが収まったら、この深さの最善手が確定！
                result.bestMove = current_best_move;
                result.score = current_best_score;
                result.depth = i + 1;
                break; // while(true) を抜ける
            }

            if (isStop.load(std::memory_order_relaxed)) break;

            // 反復深化の1つの深さ(i)の探索が終わった直後
            TT.store(hash, result.bestMove, result.score, 0, i, BOUND_EXACT, age);
            // sharedBestMove.store(result.bestMove, std::memory_order_relaxed);

            print_pv(i + 1, result.score, main_td.nodes, main_td);

            // 評価値の変動が 30cp 以内かつ手も安定している場合のみ打ち切る
            bool score_is_stable = std::abs(result.score - prev_score) <= 150;

            if (age < 55 && i >= 13 && stable_count >= 4 && score_is_stable)
                break;

            if (age >= 55 && i >= 15 && stable_count >= 5 && score_is_stable)
                break;

            // 終了条件
            // if (prev_best_move == result.bestMove)
            //     stable_count++;
            // else
            //     stable_count = 0;

            // if (age < 55 && i >= 13 && stable_count >= 6)
            //     break;

            // if(age >= 55 && i >= 15 && stable_count >= 8)
            //     break;
            prev_best_move = result.bestMove;
            prev_score = result.score;
        }

        isStop.store(true);
        for (auto &t : threads)
        {
            if (t.joinable())
                t.join();
        }

        isStop.store(false);

        // 全スレッドの探索ノード数を合算
        uint64_t total_nodes = 0;
        for (const auto& td : threadData)
        {
            total_nodes += td.nodes;
        }

        std::cout << "nodes:" << total_nodes << std::endl;

        if (think_time > 0)
        {
            double nps = static_cast<double>(total_nodes) / (static_cast<double>(think_time) / 1000.0);
            std::cout << "nps:" << static_cast<long long>(nps) << std::endl;
        }
        else
        {
            std::cout << "nps:0 (too fast)" << std::endl;
        }

        // std::cout << "nodes:" << main_td.nodes << std::endl;
        // // think_time が 0 の場合に備える
        // if (think_time > 0)
        // {
        //     double nps = (double)main_td.nodes / ((double)think_time / 1000.0);
        //     std::cout << "nps:" << (long long)nps << std::endl;
        // }
        // else
        // {
        //     // 1ms未満の場合は測定不能として出すか、前回の値を出す
        //     std::cout << "nps:0 (too fast)" << std::endl;
        // }

        // 探索終了直前にPVの2手目をponderMoveとして格納
        if (main_td.pvLength[0] >= 2)
        {
            result.ponderMove = main_td.pvTable[0][1];
        }

        return result;
    }
} // namespace engine
