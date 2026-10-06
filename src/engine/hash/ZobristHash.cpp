#include "ZobristHash.hpp"

#include <cstdint>

// Simplea xorshift64 PRNG for table initialization.
uint64_t ZobristHash::xorshift64()
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

ZobristHash::ZobristHash()
{
    // initialize seed with an arbitrary constant
    seed = 0x9e3779b97f4a7c15ULL;

    // fill board table
    for (int sq = 0; sq < 81; ++sq)
    {
        for (int pt = 0; pt < NumPieceType; ++pt)
        {
            for (int c = 0; c < NumColors; ++c)
            {
                uint64_t v = xorshift64();
                board_table[sq][pt][c] = v;
            }
        }
    }

    // turn hash
    turn_hash = xorshift64();

    current_hash = 0ULL;
}

uint64_t ZobristHash::update_move(uint64_t current, Square from_sq, Square to_sq, PieceTypeKind pt,
                                  Color c)
{
    if (pt == (PieceTypeKind)0)
        return current;
    const int ipt = static_cast<int>(pt);
    const int ic = static_cast<int>(c);
    const int from_i = static_cast<int>(from_sq);
    const int to_i = static_cast<int>(to_sq);
    current ^= board_table[from_i][ipt][ic];
    current ^= board_table[to_i][ipt][ic];
    return current;
}

uint64_t ZobristHash::update_capture(uint64_t current, Square from_sq, Square to_sq,
                                     PieceTypeKind pt, PieceTypeKind captured_pt, Color c)
{
    const int from_i = static_cast<int>(from_sq);
    const int to_i = static_cast<int>(to_sq);

    if (pt != (PieceTypeKind)0)
    {
        const int ipt = static_cast<int>(pt);
        const int ic = static_cast<int>(c);
        current ^= board_table[from_i][ipt][ic];
        current ^= board_table[to_i][ipt][ic];
    }

    if (captured_pt != (PieceTypeKind)0)
    {
        const int icap = static_cast<int>(captured_pt);
        const int oc = static_cast<int>(c) ^ 1;
        current ^= board_table[to_i][icap][oc];
    }

    return current;
}

uint64_t ZobristHash::update_drop(uint64_t current, Square to_sq, PieceTypeKind pt, Color c)
{
    if (pt == (PieceTypeKind)0)
        return current;
    const int ipt = static_cast<int>(pt);
    const int ic = static_cast<int>(c);
    const int to_i = static_cast<int>(to_sq);
    current ^= board_table[to_i][ipt][ic];
    return current;
}

uint64_t ZobristHash::update_turn(uint64_t current)
{
    return current ^ turn_hash;
}