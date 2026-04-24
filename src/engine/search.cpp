#include "search.h"
#include "StatewithNNUE.h"
#include "eval.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../../nshogi/src/core/types.h"

#include <cstdint>

namespace engine
{
    nshogi::core::Move32 searchOnePlyNNUE(const nshogi::core::State &root)
    {
        using nshogi::core::Black;
        using nshogi::core::White;

        const int INF = 30000;

        const auto rootMoves = nshogi::core::MoveGenerator::generateLegalMoves(root);
        if (rootMoves.size() == 0)
        {
            return nshogi::core::Move32::MoveNone();
        }

        nnue::StatewithNNUE st{root.clone()};
        st.init();

        int32_t alpha = -INF - 1;
        nshogi::core::Move32 bestMove = rootMoves[0];

        for (const auto mv : rootMoves)
        {
            const auto side = st.getSideToMove();
            int32_t v;

            if (side == Black){
                st.doMove<Black>(mv);
                v = -negamax<White>(st, 3, -INF, alpha);
                st.undoMove();
            }else{
                st.doMove<White>(mv);
                v = -negamax<Black>(st, 3, -INF, alpha);
                st.undoMove();
            }

            if (v > alpha)
            {
                alpha = v;
                bestMove = mv;
            }
        }

        return bestMove;
    }
} // namespace engine
