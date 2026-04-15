#ifndef ZOBRIST_HASH_HPP
#define ZOBRIST_HASH_HPP

#include <cstdint>

// 駒の種類や色のルール
enum Color { BLACK = 0, WHITE = 1, COLOR_NB = 2 };

enum PieceType {
    EMPTY = -1,
    PAWN = 0,
    LANCE,
    KNIGHT,
    SILVER,
    BISHOP,
    ROOK,
    GOLD,
    KING,
    P_PAWN,
    P_LANCE,
    P_KNIGHT,
    P_SILVER,
    HORSE,
    DRAGON,
    PIECE_TYPE_NB = 14
};

class ZobristHash {
 public:
    ZobristHash(); // 初期化

    // 各種アクションのハッシュ更新関数
    uint64_t update_move(uint64_t current, int from_sq, int to_sq, PieceType pt,
                         Color c);
    uint64_t update_capture(uint64_t current, int from_sq, int to_sq,
                            PieceType pt, PieceType captured_pt, Color c);
    uint64_t update_drop(uint64_t current, int to_sq, PieceType pt, Color c);
    uint64_t update_turn(uint64_t current);

 private:
    uint64_t board_table[81][PIECE_TYPE_NB][COLOR_NB];
    uint64_t turn_hash;
    uint64_t seed;
    uint64_t current_hash;

    uint64_t xorshift64(); // 乱数生成機
};

#endif