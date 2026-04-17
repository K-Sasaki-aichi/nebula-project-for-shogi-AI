#pragma once

#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cstring>

using nshogi::core::PieceTypeKind;
using nshogi::core::Square;


enum PieceId : uint8_t {
    Pawn = 0,
    Lance = 1,
    Knight = 2,
    Silver = 3,
    Gold = 4,
    Bishop = 5,
    Rook = 6,
    ProBishop = 7,
    ProRook = 8,

    Error = 255,
};

constexpr PieceId pieceIdtable[nshogi::core::NumPieceType] = {
    Error,     // 使わない
    Pawn,
    Lance,
    Knight,
    Silver,
    Bishop,
    Rook,
    Gold,
    Error,      // 使わない
    Gold,
    Gold,
    Gold,
    Gold,
    ProBishop,
    ProRook
};

constexpr int8_t SqIdTable[81] = {
     8,  7,  6,  5,  4,  3,  2,  1,  0, // 元の0〜8 (Sq1I〜Sq1A) を NNUEの Sq1I〜Sq1A にマッピング
    17, 16, 15, 14, 13, 12, 11, 10,  9,
    26, 25, 24, 23, 22, 21, 20, 19, 18,
    35, 34, 33, 32, 31, 30, 29, 28, 27,
    44, 43, 42, 41, 40, 39, 38, 37, 36,
    53, 52, 51, 50, 49, 48, 47, 46, 45,
    62, 61, 60, 59, 58, 57, 56, 55, 54,
    71, 70, 69, 68, 67, 66, 65, 64, 63,
    80, 79, 78, 77, 76, 75, 74, 73, 72  // 元の72〜80 (Sq9I〜Sq9A) を NNUEの Sq9I〜Sq9A にマッピング
};

enum capturedPiecedId : int16_t{
    NULL_Id = 0,
    capPawn = 1458,
    capLance = 1477,
    capKnight = 1482,
    capSilver = 1487,
    capGold = 1492,
    capBishop = 1497,
    capRook = 1500,
};

constexpr int16_t capPieceIdTable[8] = {
    NULL_Id,
    capPawn, 
    capLance,
    capKnight,
    capSilver,
    capBishop,
    capRook,
    capGold,
};



class StatewithNNUE {
private:
    nshogi::core::State state;
    // アキュムレータ (256次元 x 2手番)
    alignas(32) int32_t acc[nshogi::core::NumColors][256];

    Square king_sq[nshogi::core::NumColors] = {nshogi::core::Sq5I, nshogi::core::Sq5A}; 

public:
    StatewithNNUE()
        : state(nshogi::core::StateBuilder::getInitialState()) {}

    StatewithNNUE(nshogi::core::State&& s)
        : state(std::move(s)) {}

    void doMove(nshogi::core::Move32 move);

    void undoMove();

    inline constexpr PieceId PieceTypeKindToPieceId(const PieceTypeKind type){
        return pieceIdtable[type];
    }

    inline constexpr int8_t SquareToSqId(const Square Sq){
        return SqIdTable[Sq];
    }

    // 相手のコマならisOpponent = 1.
    // 729 = 81 * 9
    template<int isOpponent>
    inline int32_t getSqIndex(const PieceTypeKind type, const Square Sq) {
        return pieceIdtable[type] * 81
            + SqIdTable[Sq]
            + (isOpponent * 729);
    }

    template <int isOpponent>
    inline constexpr int32_t getCapturedIndex(const PieceTypeKind type, const int count){
        return capPieceIdTable[type] 
            + count 
            + (isOpponent * 45);
    }

    // アキュムレータの初期化
    // biasで初期化
    inline void initAcc(){
        std::memcpy(acc[nshogi::core::Black], weight::w_input.bias, sizeof(acc[nshogi::core::Black]));
        std::memcpy(acc[nshogi::core::White], weight::w_input.bias, sizeof(acc[nshogi::core::White]));
    }
    inline void initAcc(nshogi::core::Color C){
        std::memcpy(acc[C], weight::w_input.bias, sizeof(acc[C]));
    }
    // inline void initAcc_ZERO(){
    //     memset(acc[0], 0, sizeof(acc[0]));
    //     memset(acc[1], 0, sizeof(acc[1]));
    // }

    // inline void initAcc_ZERO(nshogi::core::Color C){
    //     memset(acc[C], 0, sizeof(acc[C]));
    // }

    void setAcc(const int32_t index);

    template <nshogi::core::Color C>
    void calFullAcc();

    inline nshogi::core::State& getState(){return state;}

};