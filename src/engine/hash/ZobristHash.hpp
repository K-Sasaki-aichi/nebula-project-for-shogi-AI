#ifndef ZOBRIST_HASH_HPP
#define ZOBRIST_HASH_HPP

#include <cstdint>

namespace nshogi
{
    namespace core
    {
        enum Square : int8_t;
    }
}
using nshogi::core::Square;
namespace nshogi
{
    namespace core
    {
        enum PieceTypeKind : uint8_t;
        enum Color : uint8_t;
        constexpr int NumPieceType = 15;
        constexpr int NumColors = 2;
    }
}
using nshogi::core::Color;
using nshogi::core::NumColors;
using nshogi::core::NumPieceType;
using nshogi::core::PieceTypeKind;

class ZobristHash
{
public:
    ZobristHash(); // 初期化

    // 各種アクションのハッシュ更新関数
    uint64_t update_move(uint64_t current, Square from_sq, Square to_sq, PieceTypeKind pt,
                         Color c);
    uint64_t update_capture(uint64_t current, Square from_sq, Square to_sq,
                            PieceTypeKind pt, PieceTypeKind captured_pt, Color c);
    uint64_t update_drop(uint64_t current, Square to_sq, PieceTypeKind pt, Color c);
    uint64_t update_turn(uint64_t current);

private:
    uint64_t board_table[81][NumPieceType][NumColors];
    uint64_t turn_hash;
    uint64_t seed;
    uint64_t current_hash;

    uint64_t xorshift64(); // 乱数生成機
};

#endif