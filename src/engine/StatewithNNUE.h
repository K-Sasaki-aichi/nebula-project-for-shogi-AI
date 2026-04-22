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


class StatewithNNUE {
private:
    nshogi::core::State state;
    // アキュムレータ (256次元 x 2手番)
    alignas(32) int16_t acc[nshogi::core::NumColors][256];

    // インデックスを入れておく配列
    // こうしておくことでCPU内部レベルでは早くなる
    // レジスタ枯渇やキャッシュ効率を高めるため
    int32_t indices[nshogi::core::NumColors][64];

    Square king_sq[nshogi::core::NumColors];

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

    // Sqをnnueように変換する関数
    template<nshogi::core::Color Us>
    inline constexpr int8_t SquareToSqId(const Square Sq){
        if constexpr(Us== nshogi::core::White){
            return 80 - SqIdTable[Sq];
        }
        return SqIdTable[Sq];
    }

    // 盤面にあるコマのインデックス
    template<nshogi::core::Color Us>
    inline constexpr int32_t getSqIndex(const PieceTypeKind type, const Square Sq) {
        int sqId = SquareToSqId<Us>(Sq);

        return pieceIdtable[type] + sqId + enemyOffset;
    }

    inline PieceTypeKind promote(const PieceTypeKind type){
        return promoteType[type];
    }

    inline PieceTypeKind rePromote(){

    }

    // 持ち駒のインデックス
    template<bool isOwn>
    inline constexpr int32_t getCapturedIndex(const PieceTypeKind type, const int count){
        return (isOwn ? ownTable[type] : otherTable[type]) + count;
    }

    // 自分の持ち駒、あるいは盤面のインデックス
    // Usは盤面を見ている手番、Cは今見ているコマの色
    template<nshogi::core::Color Us, nshogi::core::Color C>
    inline constexpr int32_t getIndex(const PieceTypeKind type, const Square Sq, const int count, const bool isStand) {
        int sqId = SquareToSqId<Us>(Sq);

        int base = typeIdtable[Us != C][NumPieceType * isStand + type];

        int mask = -static_cast<int>(isStand);
        int add  = (sqId & ~mask) | (count & mask);

        return base + add;
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

            // 4. 全ての特徴量を足し終わったら、メモリに書き戻す（ここでストアするのも1回だけ！）
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j]), a0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j + 16]), a1);
        }

        // for(int i = 0; i < num_feature; i++){
        //     const int16_t* ptr_weight = weight::w_input.weight[indices[C][i]];

        //     for(int j = 0; j < weight::NumAcc; j += 16){
        //         __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(&acc[C][j]));
        //         __m256i w = _mm256_load_si256(reinterpret_cast<const __m256i*>(&ptr_weight[j]));

        //         _mm256_store_si256(reinterpret_cast<__m256i*>(&acc[C][j]), _mm256_add_epi16(a, w));
        //     }
        // }
    }


    template<nshogi::core::Color C>
    void add_acc(const int index) {
        const int16_t* __restrict ptr_weight = weight::w_input.weight[index];
        int16_t* __restrict a_ptr = acc[C];

        // 2回分のループ展開: 1回のイテレーションで32要素 (512ビット) を処理
        for(int j = 0; j < weight::NumAcc; j += 32) {
            // 最初の16要素をロード
            __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a_ptr + j));
            __m256i w0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr_weight + j));
            
            // 最初のデータがロードされている間に、次の16要素のロードを開始
            __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a_ptr + j + 16));
            __m256i w1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr_weight + j + 16));

            // 加算とストア
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(a_ptr + j), _mm256_add_epi16(a0, w0));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(a_ptr + j + 16), _mm256_add_epi16(a1, w1));
        }
    }

    template<nshogi::core::Color C>
    void sub_acc(const int index) {
        const int16_t* __restrict ptr_weight = weight::w_input.weight[index];
        int16_t* __restrict a_ptr = acc[C];

        // 2回分のループ展開: 1回のイテレーションで32要素 (512ビット) を処理
        for(int j = 0; j < weight::NumAcc; j += 32) {
            // 最初の16要素をロード
            __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a_ptr + j));
            __m256i w0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr_weight + j));
            
            // 最初のデータがロードされている間に、次の16要素のロードを開始
            __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a_ptr + j + 16));
            __m256i w1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr_weight + j + 16));

            // 加算とストア
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(a_ptr + j), _mm256_sub_epi16(a0, w0));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(a_ptr + j + 16), _mm256_sub_epi16(a1, w1));
        }
    }


    template <nshogi::core::Color Us>
    void updateIncremental(const nshogi::core::Move32& M){
        using namespace nshogi::core;

        internal::ImmutableStateAdapter adapter(state);
        const int ownKingSqId = SquareToSqId<Us>(adapter->getKingSquare<Us>()) * 1548;
        const int oppoKingSq = SquareToSqId<static_cast<Color>(Us^1)>(adapter->getKingSquare<Us>()) * 1548;


        struct DirtyPiece ownDirty;
        struct DirtyPiece oppoDirty;

        PieceTypeKind type = M.pieceType();
       

        const bool isDrop = M.drop();
        const Square sq_from = M.from();
        const int count = adapter->getStandCount<C>(type);

        int num = 0;

        /* 動かしたコマの差分のインデックスを保存 */
        ownDirty.subIndex[num] = ownKingSqId + getIndex<Us, Us>(type, sq_from, count, isDrop);
        oppoDirty.subIndex[num] = oppoKingSq + getIndex<static_cast<Color>(Us^1), Us>(type, sq_from, count, isDrop);

        const Square sq_to = M.promote() ? promote(type) : type;
        ownDirty.addIndex[num++] = ownKingSqId + getIndex<Us, Us>(type, sq_to, 0, false);
        oppoDirty.addIndex[num++] = oppoKingSq + getIndex<static_cast<Color>(Us^1), Us>(type, sq_to, 0, false);

        /* 取られたコマの差分のインデックスを保存 */
        /* 取られたコマが無ければdirty_numを足さない */
        PieceTypeKind capturedType = M.capturePieceType();

        ownDirty.subIndex[num] = ownKingSqId + getIndex<Us, static_cast<Color>(Us^1)>(capturedType, sq_to, 0, false);
        oppoDirty.subIndex[num] = oppoKingSq + getIndex<static_cast<Color>(Us^1), static_cast<Color>(Us^1)>(capturedType, sq_to, 0, false);
        
        capturedType = rePromote(capturedType);
        const int capturedCount = adapter->getStandCount<C>(capturedType) + 1;
        ownDirty.addIndex[num] = ownKingSqId + getIndex<Us, Us>(capturedType, 0, capturedCount, true);
        oppoDirty.addIndex[num] = oppoKingSq + getIndex<static_cast<Color>(Us^1), Us>(capturedType, 0, capturedCount, true);
        
        num += (capturedType == PTK_Empty) ? 0 : 1;


        // 差分更新を行う.
        for(int i = 0; i < num; i++){
            sub_acc<Us>(ownDirty.subIndex[i]);
            sub_acc<static_cast<Color>(Us^1)>(oppoDirty.subIndex[i]);

            add_acc<Us>(ownDirty.addIndex[i]);
            add_acc<static_cast<Color>(Us^1)>(oppoDirty.addIndex[i]);
        }
    }

    // template <nshogi::core::Color Us>
    // void updateAcc(const nshogi::core::Move32& M){
    //     using namespace nshogi::core;
        
    //     if(M.pieceType() != PTK_King){
    //         updateIncremental<Us>(M);
    //         return;
    //     }
    // }

    void setAcc(const int32_t index);

    inline nshogi::core::State& getState(){return state;}

    inline auto& getAcc() const { return acc; }

    nshogi::core::Color getSideToMove() const;

};

}//nnue