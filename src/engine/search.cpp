#include "search.h"

#include "StatewithNNUE.h"
#include "eval.h"

#include "../nshogi/src/core/movegenerator.h"

#include <cstdint>
#include <limits>

namespace engine
{

    nshogi::core::Move32 searchOnePlyNNUE(const nshogi::core::State &root)
    {
        using nshogi::core::Black;
        using nshogi::core::White;

        const auto rootMoves = nshogi::core::MoveGenerator::generateLegalMoves(root);
        if (rootMoves.size() == 0)
        {
            return nshogi::core::Move32::MoveNone();
        }

        // Clone state (nshogi::core::State is non-copyable).
        nnue::StatewithNNUE st{root.clone()};

        // IMPORTANT: StatewithNNUE uses an internal StateInfo pointer (st).
        // init() must be called before refresh_acc()/initAcc().
        st.init();

        auto &state = st.getState();

        int32_t bestScore = std::numeric_limits<int32_t>::min();
        nshogi::core::Move32 bestMove = rootMoves[0];

        for (const auto mv : rootMoves)
        {
            state.doMove(mv);

            // Prototype: refresh accumulator fully each leaf (slow but simple).
            st.refresh_acc<Black>();
            st.refresh_acc<White>();

            const auto childSide = state.getSideToMove();
            int32_t v = 0;
            if (childSide == Black)
            {
                v = nnue::eval::eval<Black>(st);
            }
            else
            {
                v = nnue::eval::eval<White>(st);
            }

            // Convert "side-to-move" score to "root mover" score.
            const int32_t score = -v;

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = mv;
            }

            state.undoMove();
        }

        return bestMove;
    }

} // namespace engine
