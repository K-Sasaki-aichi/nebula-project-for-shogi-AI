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

template <nshogi::core::Color C>
void StatewithNNUE::extract_features(int& num_feature){
    using namespace nshogi::core;
    using namespace nnue;

    //const Position& Pos = state.getPosition();
    internal::ImmutableStateAdapter adapter(state);
    Square black_king_sq = adapter->getKingSquare<C>();
    
    const int KingSqId = SquareToSqId(king_sq) * 1548;

    internal::bitboard::Bitboard bitboard;
    int8_t count;
    for(int pt = PTK_Pawn; i < NumPieceType; i++){
        PieceTypeKind type = static_cast<PieceTypeKind>(pt);

        bitboard = adapter->getBitboard(Black, type);
        while (!bitboard.isZero()) {
            Square sq = bitboard.popOne();
            indices[C][num_features++] = KingSqId + getSqIndex<C, Black>(type, sq);
        }

        bitboard = adapter->getBitboard(White, type);
        while (!bitboard.isZero()) {
            Square sq = bitboard.popOne();
            indices[C][num_features++] = KingSqId + getSqIndex<C, White>(type, sq);
        }

        count = adapter->getStandCount<Black, type>();
        indices[C][num_features++] = KingSqId + getCapturedIndex(type, count);
    
        count = adapter->getStandCount<White, type>();
        indices[C][num_features++] = KingSqId + getCapturedIndex(type, count);
    }
}

template <nshogi::core::Color C>
void refresh_acc(){
    using namespace nshogi::core;
    using namespace nnue;

    int num_features = 0;
    extract_features<C>(num_features);

    for(int i = 0; i < num_features; i++){
        constexper int16_t* weight = weight::w_nput.weight[indices[C][i]];

        for(int j = 0; j < weight::NumAcc; j += 16){
            __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(&acc[j]));
            __m256i w = _mm256_load_si256(reinterpret_cast<const __m256i*>(&weights[j]));

            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[i]), _mm256_add_epi16(a, w));
        }
    }
}

void StatewithNNUE::doMove(nshogi::core::Move32 move){
    state.doMove(move);
    std::cout << "NNUE" << std::endl;
}

void StatewithNNUE::undoMove(){
    state.undoMove();
}

}