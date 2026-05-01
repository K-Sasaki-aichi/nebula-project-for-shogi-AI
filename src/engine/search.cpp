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

    void helperThreadWorker(nnue::StatewithNNUE root_st, int thread_id, int target_depth){
        using nshogi::core::Black;
        using nshogi::core::White;

        const int depth = target_depth+3;
        const int INF = 30000;

        const auto side = root_st.getSideToMove();
        const int age = root_st.getPly();

        // 探索の多様化: スレッドごとに開始深さを変える (Lazy SMP の定石)
        int start_depth = 1 + (thread_id % 4);

        for(int i = start_depth; i < depth; i++){
            if(side == Black){
                negamax<Black>(root_st, i, -INF, INF, age, 0);
            } else {
                negamax<White>(root_st, i, -INF, INF, age, 0);
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
        const int NUM_THREADS= 6;
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

        for(int i = 0; i < NUM_THREADS; i++){
            threads.push_back(std::thread([st, i, depth]() {
                helperThreadWorker(st, i, depth);
            }));
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

            // 反復深化の1つの深さ(i)の探索が終わった直後
            TT.store(hash, result.bestMove, result.score, 0, i, BOUND_EXACT, age);
        }

        isStop.store(true);
        for(auto& t : threads) {
            if(t.joinable()) t.join();
        }

        return result;
    }
} // namespace engine
