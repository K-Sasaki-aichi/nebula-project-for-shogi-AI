#pragma once

// #include "../nshogi/src/core/state.h"

// class StatewithNNUE {
// private:
//     nshogi::core::State state;

// public:
//     StatewithNNUE();
//     StatewithNNUE(nshogi::core::State&& s);

//     void doMove(nshogi::core::Move32 move);
//     void undoMove();

//     inline nshogi::core::State& getState() {return state;}
// };

#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "StatewithNNUE.h"
#include <string>
#include <iostream>

class StatewithNNUE {
private:
    nshogi::core::State state;

public:
    StatewithNNUE()
        : state(nshogi::core::StateBuilder::getInitialState()) {}

    StatewithNNUE(nshogi::core::State&& s)
        : state(std::move(s)) {}

    void doMove(nshogi::core::Move32 move);

    void undoMove(){
        state.undoMove();
    }

    nshogi::core::State& getState(){return state;}
};