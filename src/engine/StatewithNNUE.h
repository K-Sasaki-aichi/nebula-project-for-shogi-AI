#pragma once

#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../model/weights.h"
#include "../nshogi/src/core/internal/stateadapter.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cstring>
#include <immintrin.h>

using nshogi::core::PieceTypeKind;
using nshogi::core::Square;


namespace nnue {

enum PieceId : uint16_t {
    Error = 0,
    Pawn = 90,
    Lance = 252,
    Knight = 414,
    Silver = 576,
    Gold = 738,
    Bishop = 900,
    ProBishop = 1062,
    Rook = 1224,
    ProRook = 1386,
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

enum standPiecedId : int8_t{
    NULL_Id = -1,
    capPawn = 0,
    capLance = 38,
    capKnight = 48,
    capSilver = 58,
    capGold = 68,
    capBishop = 78,
    capRook = 84,
};

// enum class otherStandPiecedId : int8_t{
//     NULL_Id = -1,
//     capPawn = 19,
//     capLance = 43,
//     capKnight = 53,
//     capSilver = 63,
//     capGold = 73,
//     capBishop = 81,
//     capRook = 87,
// };

constexpr int8_t ownTable[8] = {
    NULL_Id,
    capPawn,
    capLance,
    capKnight,
    capSilver,
    capBishop,
    capRook,
    capGold
};

constexpr int8_t otherTable[8] = {
    NULL_Id,
    capPawn+19,
    capLance+5,
    capKnight+5,
    capSilver+5,
    capBishop+3,
    capRook+3,
    capGold+5
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

class StatewithNNUE {
private:
    nshogi::core::State state;
    // アキュムレータ (256次元 x 2手番)
    alignas(32) int16_t acc[nshogi::core::NumColors][256];

    // インデックスを入れておく配列
    // こうしておくことでCPU内部レベルでは早くなる
    // レジスタ枯渇やキャッシュ効率を高めるため
    int32_t indices[nshogi::core::NumColors][64];

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

    template<nshogi::core::Color Us>
    inline constexpr int8_t SquareToSqId(const Square Sq){
        if constexpr(Us== nshogi::core::White){
            return 80 - SqIdTable[Sq];
        }
        return SqIdTable[Sq];
    }

    template<nshogi::core::Color Us, nshogi::core::Color C>
    constexpr int32_t getSqIndex(const PieceTypeKind type, const Square Sq) {
        int sqId = SquareToSqId<Us>(Sq);
        
        // 味方なら 0、敵なら 1
        constexpr int enemyOffset = (Us == C) ? 0 : 81;

        return pieceIdtable[type] + sqId + enemyOffset;
    }

    template<bool isOwn>
    inline constexpr int32_t getCapturedIndex(const PieceTypeKind type, const int count){
        return (isOwn ? ownTable[type] : otherTable[type]) + count;
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

    template <nshogi::core::Color C>
    void extract_features(int& num_feature){
        using namespace nshogi::core;
        using namespace nnue;

        //const Position& Pos = state.getPosition();
        internal::ImmutableStateAdapter adapter(state);
        Square king_sq = adapter->getKingSquare<C>();
        
        const int KingSqId = SquareToSqId<C>(king_sq) * 1548;

        internal::bitboard::Bitboard bitboard;
        int8_t count;

        #pragma unroll
        for(int pt = PTK_Pawn; pt < NumPieceType; pt++){
            if (pt == PTK_King) {
                continue;
            }

            PieceTypeKind type = static_cast<PieceTypeKind>(pt);

            bitboard = adapter->getBitboard<Black>(type);
            while (!bitboard.isZero()) {
                Square sq = bitboard.popOne();
                indices[C][num_feature++] = KingSqId + getSqIndex<C, Black>(type, sq);
            }

            bitboard = adapter->getBitboard<White>(type);
            while (!bitboard.isZero()) {
                Square sq = bitboard.popOne();
                indices[C][num_feature++] = KingSqId + getSqIndex<C, White>(type, sq);
            }

            if(pt < PTK_King){
                count = adapter->getStandCount<Black>(type);
                for (int i = 1; i <= count; ++i) { // 1枚目からcount枚目まで全て足す
                    indices[C][num_feature++] = KingSqId + getCapturedIndex<C==Black>(type, i);
                }

                // 敵の持ち駒
                count = adapter->getStandCount<White>(type);
                for (int i = 1; i <= count; ++i) {
                    indices[C][num_feature++] = KingSqId + getCapturedIndex<C==White>(type, i);
                }
            }
        }
    }

    template <nshogi::core::Color C>
    void refresh_acc(){
        using namespace nshogi::core;
        using namespace nnue;

        initAcc(C);

        int num_feature = 0;
        extract_features<C>(num_feature);

        for(int i = 0; i < num_feature; i++){
            const int16_t* ptr_weight = weight::w_input.weight[indices[C][i]];

            for(int j = 0; j < weight::NumAcc; j += 16){
                __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&acc[C][j]));
                __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&ptr_weight[j]));

                _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j]), _mm256_add_epi16(a, w));
            }
        }
    }


    void setAcc(const int32_t index);

    inline nshogi::core::State& getState(){return state;}

    inline auto& getAcc() const { return acc; }

    nshogi::core::Color getSideToMove() const;

};

}//nnue