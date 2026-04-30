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

            MovePicker<false> moves(st.getState(), tt_move);

            auto mv = moves.next();

            switch (side)
            {
            case Black:
                while(mv != nshogi::core::Move32::MoveNone()){
                    st.doMove<Black>(mv);    
                    int16_t score = -negamax<White>(st, i, -INF, -alpha, age, 0);    
                    st.undoMove();
                    
                    if (score > alpha) {
                        alpha = score;
                        result.bestMove = mv;
                        result.score = score;
                    }

                    mv = moves.next();
                }
            
                break;
            
            default:
                while(mv != nshogi::core::Move32::MoveNone()){
                    st.doMove<White>(mv);    
                    int16_t score = -negamax<Black>(st, i, -INF, -alpha, age, 0);    
                    st.undoMove();
                    
                    if (score > alpha) {
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

        return result;
    }
} // namespace engine
