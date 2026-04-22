#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../nshogi/src/core/squareiterator.h"
#include "../nshogi/src/core/types.h"
#include "StatewithNNUE.h"
#include "../model/weights.h"
#include <string>
#include <iostream>
#include <immintrin.h>


namespace nnue {
void StatewithNNUE::doMove(nshogi::core::Move32 move){
    state.doMove(move);
}

void StatewithNNUE::undoMove(){
    state.undoMove();
}


nshogi::core::Color StatewithNNUE::getSideToMove() const { return state.getSideToMove(); }

}