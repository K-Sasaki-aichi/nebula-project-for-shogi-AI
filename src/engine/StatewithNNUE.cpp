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


// StatewithNNUE()
//     : state(nshogi::core::StateBuilder::getInitialState()) {}

// StatewithNNUE(nshogi::core::State&& s)
//     : state(std::move(s)) {}

// アキュムレータの初期化
void StatewithNNUE::initAcc(){

}

// indexを受け取りアキュムレータを計算する関数
void StatewithNNUE::setAcc(const int32_t index){

}


template <nshogi::core::Color C>
void StatewithNNUE::calFullAcc(){
    using namespace nshogi::core;

    const Position& Pos = state.getPosition();
    const int KingSqId = SquareToSqId(king_sq[C]) * 1548;

    SquareIterator<IterateOrder::NWSE> SquareIt;

    
    for (auto It = SquareIt.begin(); It != SquareIt.end(); ++It){
        const Square Sq = *It;
        
        

    }
}


void StatewithNNUE::doMove(nshogi::core::Move32 move){
    state.doMove(move);
    std::cout << "NNUE" << std::endl;
}

void StatewithNNUE::undoMove(){
    state.undoMove();
}
