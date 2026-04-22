#pragma once

#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../model/weights.h"
#include "../nshogi/src/core/internal/stateadapter.h"
#include "nn/nnue_nn.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cstring>
#include <immintrin.h>

using nshogi::core::PieceTypeKind;
using nshogi::core::Square;
using nshogi::core::Color;

constexpr int MAX_PLY = 512;

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
    NULL_Id = 100,
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

constexpr uint16_t typeIdtable[2][nshogi::core::NumPieceType+8] = {
    {// 盤面のコマ用
    Error,     // 使わない
    Pawn, Lance, Knight, Silver, Bishop, Rook, Gold,
    Error,    
    Gold, Gold, Gold, Gold, ProBishop, ProRook,

    // 持ち駒用
    NULL_Id, 
    capPawn, capLance, capKnight, capSilver, capBishop, 
    capRook, capGold,
    },

    {    // 盤面のコマ用
    Error,     // 使わない
    Pawn+81, Lance+81, Knight, Silver, Bishop, Rook, Gold,
    Error,    
    Gold, Gold, Gold, Gold, ProBishop, ProRook,

    // 持ち駒用
    NULL_Id, 
    capPawn+19, capLance+5, capKnight+5, capSilver+5, capBishop+3, 
    capRook+3, capGold+5,
    }
};

constexpr PieceTypeKind promoteType[] = {
    nshogi::core::PTK_Empty,
    nshogi::core::PTK_ProPawn, nshogi::core::PTK_ProLance, nshogi::core::PTK_ProKnight,
    nshogi::core::PTK_ProSilver, nshogi::core::PTK_ProBishop, nshogi::core::PTK_ProRook,

};

constexpr PieceTypeKind rePromoteType[] = {
    nshogi::core::PTK_Empty,
    nshogi::core::PTK_Pawn, nshogi::core::PTK_Lance, nshogi::core::PTK_Knight, nshogi::core::PTK_Silver,
    nshogi::core::PTK_Bishop, nshogi::core::PTK_Rook, nshogi::core::PTK_Gold, nshogi::core::PTK_King,
    nshogi::core::PTK_Pawn, nshogi::core::PTK_Lance, nshogi::core::PTK_Knight,
    nshogi::core::PTK_Silver, nshogi::core::PTK_Bishop, nshogi::core::PTK_Rook,
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

struct DirtyPiece
{
    int dirty_num = 0;
    uint16_t subIndex[2];
    uint16_t addIndex[2];
};

struct alignas(64) StateInfo{
    alignas(32) int16_t acc[nshogi::core::NumColors][256];

    uint64_t zobristKey;
};

class StatewithNNUE {
private:
    nshogi::core::State state;

    // インデックスを入れておく配列
    // こうしておくことでCPU内部レベルでは早くなる
    // レジスタ枯渇やキャッシュ効率を高めるため
    int32_t indices[nshogi::core::NumColors][64];

    std::vector<StateInfo> stateStack;
    StateInfo* st;

public:
    void init(){
        stateStack.resize(256);
        st = &stateStack[0];

        refresh_acc<nshogi::core::White>();
        refresh_acc<nshogi::core::Black>();
    }

    StatewithNNUE()
        : state(nshogi::core::StateBuilder::getInitialState()) {}

    StatewithNNUE(nshogi::core::State&& s)
        : state(std::move(s)) {}

    template <nshogi::core::Color Us>
    void doMove(const nshogi::core::Move32& M){
        using namespace nshogi::core;
        
        StateInfo* prev_info = st;
        st++;

        *st = *prev_info;

        state.doMove(M);

        if(M.pieceType() != PTK_King){
            updateIncremental<Us>(M);
            return;
        }

        refresh_acc<Us>();
        PieceTypeKind capturedType = M.capturePieceType();
        if(capturedType != PTK_Empty){
            updateIncrementalCaptureOnly<Us>(M, capturedType);
        }

        return;
    }

    void undoMove(){
        st--;
        state.undoMove();
    }

    inline constexpr PieceId PieceTypeKindToPieceId(const PieceTypeKind type){
        return pieceIdtable[type];
    }

    // Sqをnnueように変換する関数
    template<Color Us>
    inline constexpr int8_t SquareToSqId(const Square Sq){
        if constexpr(Us== nshogi::core::White){
            return 80 - SqIdTable[Sq];
        }
        return SqIdTable[Sq];
    }

    // 成りコマに変換する関数
    inline PieceTypeKind promote(const PieceTypeKind type){
        return promoteType[type];
    }

    // 成りコマなら戻す。そうじゃないならもとのまま
    inline PieceTypeKind rePromote(const PieceTypeKind type){
        return rePromoteType[type];
    }

    // 盤面にあるコマのインデックス
    template<Color Us, Color C>
    constexpr int32_t getSqIndex(const PieceTypeKind type, const Square Sq) {
        int sqId = SquareToSqId<Us>(Sq);
        
        // 味方なら 0、敵なら 81(盤面の升の数だけずらす)
        constexpr int enemyOffset = (Us == C) ? 0 : 81;

        return pieceIdtable[type] + sqId + enemyOffset;
    }

    // 持ち駒のインデックス
    template<bool isOwn>
    inline constexpr int32_t getCapturedIndex(const PieceTypeKind type, const int count){
        return (isOwn ? ownTable[type] : otherTable[type]) + count;
    }

    // 自分の持ち駒、あるいは盤面のインデックス
    // Usは盤面を見ている手番、Cは今見ているコマの色
    template<Color Us, Color C>
    inline constexpr int32_t getIndex(const PieceTypeKind type, const Square Sq, const int count, const bool isStand) {
        const int sqId = SquareToSqId<Us>(Sq);

        const int base = typeIdtable[Us != C][nshogi::core::NumPieceType * isStand + type];

        const int mask = -static_cast<int>(isStand);
        const int add  = (sqId & ~mask) | (count & mask);

        // 味方なら 0、敵なら 81(盤面の升の数だけずらす)
        constexpr int enemyOffset = (Us == C) ? 0 : 81;

        return base + add + enemyOffset;
    }

    // アキュムレータの初期化
    // biasで初期化
    inline void initAcc(){
        auto& acc = st->acc;
        std::memcpy(acc[nshogi::core::Black], weight::w_input.bias, sizeof(acc[nshogi::core::Black]));
        std::memcpy(acc[nshogi::core::White], weight::w_input.bias, sizeof(acc[nshogi::core::White]));
    }
    inline void initAcc(Color C){
        auto& acc = st->acc;
        std::memcpy(acc[C], weight::w_input.bias, sizeof(acc[C]));
    }

    template <Color C>
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

    template <Color C>
    void refresh_acc(){
        using namespace nshogi::core;
        using namespace nnue;

        auto& acc = st->acc;

        initAcc(C);

        int num_feature = 0;
        extract_features<C>(num_feature);

        for(int j = 0; j < weight::NumAcc; j += 32) {
            // アキュムレータから初期値をロード
            __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&acc[C][j]));
            __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&acc[C][j + 16]));

            // 全ての特徴量の重みを「レジスタ上で」ひたすら足し込む
            for(int i = 0; i < num_feature; i++){
                int feature_idx = indices[C][i];
                const int16_t* ptr_weight = weight::w_input.weight[feature_idx];

                __m256i w0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&ptr_weight[j]));
                __m256i w1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&ptr_weight[j + 16]));

                a0 = _mm256_add_epi16(a0, w0);
                a1 = _mm256_add_epi16(a1, w1);
            }

            // 全ての特徴量を足し終わったら、メモリに書き戻す
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j]), a0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j + 16]), a1);
        }
    }


    // moveの後に行う
    template <Color Us>
    void updateIncremental(const nshogi::core::Move32& M){
        using namespace nshogi::core;

        auto& acc = st->acc;

        internal::ImmutableStateAdapter adapter(state);
        constexpr Color Oppo = static_cast<Color>(Us ^ 1);
        const int ownKingSqId = SquareToSqId<Us>(adapter->getKingSquare<Us>()) * 1548;
        const int oppoKingSqId = SquareToSqId<Oppo>(adapter->getKingSquare<Oppo>()) * 1548;

        const Square sq_zero = static_cast<Square>(0);

        struct DirtyPiece ownDirty;
        struct DirtyPiece oppoDirty;

        PieceTypeKind type = M.pieceType();

        const bool isDrop = M.drop();
        const Square sq_from = M.from();
        const Square sq_to = M.to();
        const int count = adapter->getStandCount<Us>(type) + 1;

        int num = 0;

        // 差分更新を行うインデックスを計算する

        /* 動かしたコマの差分のインデックスを保存 */
        ownDirty.subIndex[num] = ownKingSqId + getIndex<Us, Us>(type, sq_from, count, isDrop);
        oppoDirty.subIndex[num] = oppoKingSqId + getIndex<Oppo, Us>(type, sq_from, count, isDrop);

        const PieceTypeKind next_type = M.promote() ? promote(type) : type;
        ownDirty.addIndex[num++] = ownKingSqId + getIndex<Us, Us>(next_type, sq_to, 0, false);
        oppoDirty.addIndex[num++] = oppoKingSqId + getIndex<Oppo, Us>(next_type, sq_to, 0, false);

        /* 取られたコマの差分のインデックスを保存 */
        /* 取られたコマが無ければdirty_numを足さない */
        PieceTypeKind capturedType = M.capturePieceType();

        ownDirty.subIndex[num] = ownKingSqId + getIndex<Us, static_cast<Color>(Oppo)>(capturedType, sq_to, 0, false);
        oppoDirty.subIndex[num] = oppoKingSqId + getIndex<Oppo, static_cast<Color>(Oppo)>(capturedType, sq_to, 0, false);
        
        capturedType = rePromote(capturedType);
        const int capturedCount = adapter->getStandCount<Us>(capturedType);
        ownDirty.addIndex[num] = ownKingSqId + getIndex<Us, Us>(capturedType, sq_zero, capturedCount, true);
        oppoDirty.addIndex[num] = oppoKingSqId + getIndex<Oppo, Us>(capturedType, sq_zero, capturedCount, true);
        
        num += (capturedType == PTK_Empty) ? 0 : 1;


        // 上で計算したインデックスをもとに差分更新を行う
        int16_t* __restrict a_ptr_own = acc[Us];
        int16_t* __restrict a_ptr_oppo = acc[Oppo];
        for(int i = 0; i < weight::NumAcc; i += 32) {
            // アキュムレータをロード.
            __m256i a0_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr_own + i));
            __m256i a1_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr_own + i + 16));
            __m256i a0_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr_oppo + i));
            __m256i a1_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr_oppo + i + 16));
        
            for(int j = 0; j < num; j++){
                // 重みのロード
                const int16_t* ptr_sub_weight_own = weight::w_input.weight[ownDirty.subIndex[j]];
                const int16_t* ptr_add_weight_own = weight::w_input.weight[ownDirty.addIndex[j]];

                const int16_t* ptr_sub_weight_oppo = weight::w_input.weight[oppoDirty.subIndex[j]];
                const int16_t* ptr_add_weight_oppo = weight::w_input.weight[oppoDirty.addIndex[j]];


                __m256i w0_sub_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight_own + i));
                __m256i w1_sub_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight_own + i + 16));
                __m256i w0_add_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight_own + i));
                __m256i w1_add_own = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight_own + i + 16));

                __m256i w0_sub_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight_oppo + i));
                __m256i w1_sub_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight_oppo + i + 16));
                __m256i w0_add_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight_oppo + i));
                __m256i w1_add_oppo = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight_oppo + i + 16));


                a0_own = _mm256_sub_epi16(a0_own, w0_sub_own);
                a1_own = _mm256_sub_epi16(a1_own, w1_sub_own);
                a0_own = _mm256_add_epi16(a0_own, w0_add_own);
                a1_own = _mm256_add_epi16(a1_own, w1_add_own);

                a0_oppo = _mm256_sub_epi16(a0_oppo, w0_sub_oppo);
                a1_oppo = _mm256_sub_epi16(a1_oppo, w1_sub_oppo);
                a0_oppo = _mm256_add_epi16(a0_oppo, w0_add_oppo);
                a1_oppo = _mm256_add_epi16(a1_oppo, w1_add_oppo);
            }

            // 計算結果をストア
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Us][i]), a0_own);
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Us][i + 16]), a1_own);

            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Oppo][i]), a0_oppo);
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Oppo][i + 16]), a1_oppo);
        }
    
    }

    template <Color Us>
    void updateIncrementalCaptureOnly(const nshogi::core::Move32& M, PieceTypeKind capturedType){
        auto& acc = st->acc;

        const Square sq_zero = static_cast<Square>(0);

        nshogi::core::internal::ImmutableStateAdapter adapter(state);
        constexpr Color Oppo = static_cast<Color>(Us ^ 1);
        const int oppoKingSqId = SquareToSqId<Oppo>(adapter->getKingSquare<Oppo>()) * 1548;

        const Square sq_to = M.to();
        int sub_index = oppoKingSqId + getIndex<Oppo, Oppo>(capturedType, sq_to, 0, false);

        capturedType = rePromote(capturedType);
        const int capturedCount = adapter->getStandCount<Us>(capturedType);
        int add_index = oppoKingSqId + getIndex<Oppo, Us>(capturedType, sq_zero, capturedCount, true);

        int16_t* __restrict a_ptr = acc[Oppo];
        const int16_t* ptr_sub_weight = weight::w_input.weight[sub_index];
        const int16_t* ptr_add_weight = weight::w_input.weight[add_index];
        for(int i = 0; i < weight::NumAcc; i += 32) {
            __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr + i));
            __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(a_ptr + i + 16));

            __m256i w0_sub = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight + i));
            __m256i w1_sub = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_sub_weight + i + 16));
            
            __m256i w0_add = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight + i));
            __m256i w1_add = _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr_add_weight + i + 16));
        
            a0 = _mm256_sub_epi16(a0, w0_sub);
            a1 = _mm256_sub_epi16(a1, w1_sub);
            a0 = _mm256_add_epi16(a0, w0_add);
            a1 = _mm256_add_epi16(a1, w1_add);

            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Oppo][i]), a0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[Oppo][i + 16]), a1);
        }
    }

    template <nshogi::core::Color C>
    inline int32_t eval(StatewithNNUE& statewithNNUE){
        return (nnue::NN::calNN<C>(st->acc) >> 4);
    }

    const auto& getAcc(){ return st->acc; }

    void setAcc(const int32_t index);

    inline nshogi::core::State& getState(){return state;}

    nshogi::core::Color getSideToMove() const;

};

}//nnue