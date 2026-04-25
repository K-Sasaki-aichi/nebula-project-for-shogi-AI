#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"

#include <cstdint>

namespace engine
{
    // 引数を StatewithNNUE の参照に変更します
    SearchResult searchNNUE(nnue::StatewithNNUE &st)
    {
        using nshogi::core::Black;
        using nshogi::core::White;

        const int INF = 30000;
        SearchResult result;

        // st.getState() で内部の盤面状態を取得して合法手を生成
        const auto rootMoves = nshogi::core::MoveGenerator::generateLegalMoves(st.getState());
        const int size = rootMoves.size();
        if (size == 0) return result;

        int32_t alpha = -INF - 1;
        nshogi::core::Move32 bestMove = rootMoves[0];

        const auto side = st.getSideToMove();

        for (const auto mv : rootMoves)
        {
            int32_t v;

            if (side == Black){
                st.doMove<Black>(mv); // 差分更新！
                v = -negamax<White>(st, result.deepth, -INF, -alpha);
                st.undoMove();
            } else {
                st.doMove<White>(mv); // 差分更新！
                v = -negamax<Black>(st, result.deepth, -INF, -alpha);
                st.undoMove();
            }

            if (v > alpha) {
                alpha = v;
                result.bestMove = mv;
                result.score = v;
            }
        }

        return result;
    }
} // namespace engine
