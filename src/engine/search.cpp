#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"
#include "../book/book.h"
#include "../nshogi/src/io/sfen.h"
#include <fstream>

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

    static void logSearch(const std::string &msg)
    {
        std::ofstream f("nebula_search_log.txt", std::ios::app);
        f << msg << "\n";
    }

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

            if (!local_best_move.isNone())
            {
                // sharedBestMove.store(local_best_move, std::memory_order_relaxed);
                TT.store(hash, local_best_move, alpha, 0, i, BOUND_EXACT, age);
            }
        }
    }

    // 引数を StatewithNNUE の参照に変更します
    SearchResult searchNNUE(nnue::StatewithNNUE &st, int think_time, int PLY)
    {
        initKillers();
        using nshogi::core::Black;
        using nshogi::core::White;

        auto now = std::chrono::steady_clock::now().time_since_epoch();
        long long start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        limitTimeMs.store(start_ms + think_time, std::memory_order_relaxed);

        logSearch("---- go ---- think_time=" + std::to_string(think_time) +
                " PLY=" + std::to_string(PLY));   // ← 追加

        const int depth = 40;
        const int INF = 30000;
        const int NUM_THREADS = 2;
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

            // add: この深さ(i)での最善手を一時的に保持する変数
            nshogi::core::Move32 current_best_move = nshogi::core::Move32::MoveNone();
            int16_t current_best_score = -INF;

            nshogi::core::Move32 tt_move = result.bestMove;

            TTEntry entry;

            if (tt_move == nshogi::core::Move32::MoveNone() && TT.read(hash, entry))
            {
                tt_move = entry.move;
            }

            MovePicker2<false> moves(st.getState(), tt_move);

            auto mv = moves.next();

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
                    int16_t score = -negamax<White>(st, i, -INF, -alpha, age, 0, main_td);

                    //######
// std::cout
//     << "depth=" << i
//     << " move=" << static_cast<uint32_t>(mv.value())
//     << " score=" << score
//     << '\n';
                    //######
                    st.undoMove();

                    if (isStop.load(std::memory_order_relaxed))
                    {
                        break;
                    }

                    if (score > alpha)
                    {
                        alpha = score;
                        current_best_move = mv;
                        current_best_score = score;
                        // result.bestMove = mv;
                        // result.score = score;
                    }

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
                    int16_t score = -negamax<Black>(st, i, -INF, -alpha, age, 0, main_td);

                    //######
// std::cout
//     << "depth=" << i
//     << " move=" << static_cast<uint32_t>(mv.value())
//     << " score=" << score
//     << '\n';
                    //######

                    st.undoMove();
                    if (isStop.load(std::memory_order_relaxed))
                    {
                        break;
                    }

                    if (score > alpha)
                    {
                        alpha = score;
                        current_best_move = mv;
                        current_best_score = score;
                        // result.bestMove = mv;
                        // result.score = score;
                    }

                    mv = moves.next();
                }

                break;
            }

            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }

            result.bestMove = current_best_move;
            result.score = current_best_score;
            result.depth = i;

            logSearch("[d=" + std::to_string(i) +
                      "] move=" + nshogi::io::sfen::move32ToSfen(result.bestMove) +
                      " score=" + std::to_string(result.score) +
                      " nodes=" + std::to_string(main_td.nodes));

            // 反復深化の1つの深さ(i)の探索が終わった直後
            TT.store(hash, result.bestMove, result.score, 0, i, BOUND_EXACT, age);
            // sharedBestMove.store(result.bestMove, std::memory_order_relaxed);

            // 終了条件
            if (prev_best_move == result.bestMove)
                stable_count++;
            else
                stable_count = 0;

            if (age < 55 && i >= 11 && stable_count >= 3)
                break;

            if(age >= 55 && i >= 13 && stable_count >= 4)
                break;
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

        logSearch("[final] move=" + nshogi::io::sfen::move32ToSfen(result.bestMove) +
                  " score=" + std::to_string(result.score) +
                  " depth=" + std::to_string(result.depth));

        std::cout << "nodes:" << main_td.nodes << std::endl;
        // think_time が 0 の場合に備える
        if (think_time > 0)
        {
            double nps = (double)main_td.nodes / ((double)think_time / 1000.0);
            std::cout << "nps:" << (long long)nps << std::endl;
        }
        else
        {
            // 1ms未満の場合は測定不能として出すか、前回の値を出す
            std::cout << "nps:0 (too fast)" << std::endl;
        }

        return result;
    }
} // namespace engine
