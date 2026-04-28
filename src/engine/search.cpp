#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"
#include "../book/book.h"

#include <cstdint>

namespace engine
{
    TranspositionTable TT; 

    // 引数を StatewithNNUE の参照に変更します
    SearchResult searchNNUE(nnue::StatewithNNUE &st)
    {
        using nshogi::core::Black;
        using nshogi::core::White;

        const int depth = 6;
        const int INF = 30000;
        SearchResult result;

        const uint64_t hash = st.getHash();
        const nshogi::core::Move32 b_move = findBookMove(hash);
        if (!b_move.isNone()) {
            std::cout << "info string book hit" << std::endl;
            result.bestMove = b_move;
            return result;
        }

        // st.getState() で内部の盤面状態を取得して合法手を生成
        const auto rootMoves = nshogi::core::MoveGenerator::generateLegalMoves(st.getState());
        const int size = rootMoves.size();
        if (size == 0) return result;

        nshogi::core::Move32 bestMove = rootMoves[0];

        const auto side = st.getSideToMove();
        const int age = st.getPly();
        int16_t tt_score;

        for(int i = 0; i < depth; i++){
            int16_t alpha = -INF - 1;

            nshogi::core::Move32 tt_move = result.bestMove; 
            TTEntry* entry = TT.probe(hash);
            if (tt_move == nshogi::core::Move32::MoveNone() && TT.isHit(entry, hash)) {
                tt_move = entry->move;
            }

            const auto orderedMoves = sortMoves(st, rootMoves, tt_move);

            switch (side)
            {
            case Black:
                for(const auto& [mv, score] : orderedMoves) {
                    int16_t v;
                    st.doMove<Black>(mv);
                    v = -negamax<White>(st, i, -INF, -alpha, age);
                    st.undoMove();

                    if (v > alpha) {
                        alpha = v;
                        result.bestMove = mv;
                        result.score = v;
                    }
                }
                break;
            
            default:
                for(const auto& [mv, score] : orderedMoves) {
                    int16_t v;
                    st.doMove<White>(mv);
                    v = -negamax<Black>(st, i, -INF, -alpha, age);
                    st.undoMove();

                    if (v > alpha) {
                        alpha = v;
                        result.bestMove = mv;
                        result.score = v;
                    }
                }
                break;
            }

            // 反復深化の1つの深さ(i)の探索が終わった直後
            TT.store(hash, result.bestMove, result.score, 0, i, BOUND_EXACT, age);

        }

        return result;
    }
} // namespace engine
