#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"
#include "../book/book.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace engine
{
    TranspositionTable TT;

    std::atomic<bool> isStop(false);

    // 各スレッドで共有するrootのbestmove
    std::atomic<nshogi::core::Move32> sharedBestMove{
        nshogi::core::Move32::MoveNone()
    };

    void helperThreadWorker(nshogi::core::State cloned_st, int thread_id, int target_depth){
        using nshogi::core::Black;
        using nshogi::core::White;

        nnue::StatewithNNUE st(std::move(cloned_st));

        const int depth = target_depth+3;
        const int INF = 30000;

        const auto side = st.getSideToMove();
        const int age = st.getPly();
        const uint64_t hash = st.getHash();

        // 探索の多様化: スレッドごとに開始深さを変える
        int start_depth = 1 + (thread_id % 4);
        int step = 1 + (thread_id % 3);

        for (int i = start_depth; i < depth; i+=step)
        {
            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }
            int16_t alpha = -INF - 1;
            nshogi::core::Move32 local_best_move = nshogi::core::Move32::MoveNone();

            auto shared = sharedBestMove.load(std::memory_order_relaxed);
            nshogi::core::Move32 tt_move = shared;

            TTEntry entry;
            if(tt_move == nshogi::core::Move32::MoveNone() && TT.read(hash, entry)){
                tt_move = entry.move;
            }

            MovePicker2<false> moves(st.getState(), tt_move, thread_id+1);

            auto mv = moves.next();

            switch (side)
            {
            case Black:
                while (mv != nshogi::core::Move32::MoveNone())
                {
                    if (isStop.load(std::memory_order_relaxed)) return;

                    st.doMove<Black>(mv);
                    int16_t score = -negamax<White>(st, i, -INF, -alpha, age, 0);
                    st.undoMove();
                    if (isStop.load(std::memory_order_relaxed)) return;

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
                    if (isStop.load(std::memory_order_relaxed)) return;

                    st.doMove<White>(mv);
                    int16_t score = -negamax<Black>(st, i, -INF, -alpha, age, 0);
                    st.undoMove();

                    if (isStop.load(std::memory_order_relaxed)) return;

                    if (score > alpha)
                    {
                        alpha = score;
                        local_best_move = mv;
                    }

                    mv = moves.next();
                }

                break;
            }

            // 反復深化の1つの深さ(i)の探索が終わった直後
            if (!local_best_move.isNone()) {
                sharedBestMove.store(local_best_move, std::memory_order_relaxed);
                TT.store(hash, local_best_move, alpha, 0, i, BOUND_EXACT, age);
            }
        }
    }


    // 引数を StatewithNNUE の参照に変更します
    SearchResult searchNNUE(nnue::StatewithNNUE &st)
    {
        initKillers();
        using nshogi::core::Black;
        using nshogi::core::White;

        const int depth = 7;
        const int INF = 30000;
        const int NUM_THREADS= 5;
        SearchResult result;

        const uint64_t hash = st.getHash();
        const nshogi::core::Move32 b_move = findBookMove(hash);
        if (!b_move.isNone())
        {
            result.bestMove = b_move;
            return result;
        }

        isStop.store(false);
        std::vector<std::thread> threads;
        sharedBestMove.store(nshogi::core::Move32::MoveNone(), std::memory_order_relaxed);

        for(int i = 0; i < NUM_THREADS; i++){
            threads.push_back(std::thread(helperThreadWorker, st.getState().clone(), i, depth));
        }

        const auto side = st.getSideToMove();
        const int age = st.getPly();
        int16_t tt_score;

        for (int i = 0; i < depth; i++)
        {
            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }
            int16_t alpha = -INF - 1;

            nshogi::core::Move32 tt_move = result.bestMove;

            TTEntry entry;

            if (tt_move == nshogi::core::Move32::MoveNone() && TT.read(hash, entry)) {
                tt_move = entry.move;
            }

            // 2. それでも tt_move が無い時（探索の極初期など）だけ、他スレッドの意見を聞く
            if (tt_move == nshogi::core::Move32::MoveNone()) {
                auto shared = sharedBestMove.load(std::memory_order_relaxed);
                if (!shared.isNone()) {
                    tt_move = shared;
                }
            }

            MovePicker2<false> moves(st.getState(), tt_move);

            auto mv = moves.next();

            switch (side)
            {
            case Black:
                while (mv != nshogi::core::Move32::MoveNone())
                {
                    if (isStop.load(std::memory_order_relaxed))
                    {
                        break;
                    }
                    st.doMove<Black>(mv);
                    int16_t score = -negamax<White>(st, i, -INF, -alpha, age, 0);
                    st.undoMove();

                    if (score > alpha)
                    {
                        alpha = score;
                        result.bestMove = mv;
                        result.score = score;
                    }

                    mv = moves.next();
                }

                break;

            default:
                while (mv != nshogi::core::Move32::MoveNone())
                {
                    if (isStop.load(std::memory_order_relaxed))
                    {
                        break;
                    }
                    st.doMove<White>(mv);
                    int16_t score = -negamax<Black>(st, i, -INF, -alpha, age, 0);
                    st.undoMove();

                    if (score > alpha)
                    {
                        alpha = score;
                        result.bestMove = mv;
                        result.score = score;
                    }

                    mv = moves.next();
                }

                break;
            }

            result.depth = i;

            if (isStop.load(std::memory_order_relaxed))
            {
                break;
            }

            // 反復深化の1つの深さ(i)の探索が終わった直後
            TT.store(hash, result.bestMove, result.score, 0, i, BOUND_EXACT, age);
            sharedBestMove.store(result.bestMove, std::memory_order_relaxed);
        }

        isStop.store(true);
        for(auto& t : threads) {
            if(t.joinable()) t.join();
        }

        isStop.store(false);

        return result;
    }
} // namespace engine
