#pragma once

#include "../nshogi/src/core/state.h"

namespace engine
{

    // Very small prototype search for testing:
    // - 1-ply (evaluate after one move)
    // - Uses NNUE evaluation (requires weight::load() beforehand)
    // - Returns MoveNone if no legal moves.
    //
    // Score is from the root side-to-move's perspective.
    [[nodiscard]] nshogi::core::Move32 searchOnePlyNNUE(const nshogi::core::State &root);

} // namespace engine
