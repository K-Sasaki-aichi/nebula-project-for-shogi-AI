#include "../nshogi/src/core/initializer.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/position.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../nshogi/src/io/sfen.h"
#include "../nshogi/src/core/squareiterator.h"
#include "../nshogi/src/core/types.h"
#include "../nshogi/src/core/internal/stateadapter.h"
#include "StatewithNNUE.h"
#include <string>
#include <iostream>

using nshogi::core::Position;


std::string PieceKindtoString(const nshogi::core::PieceKind PK){
    using namespace nshogi::core;

    switch (PK)
    {
    case PK_Empty: return ".";

    // 先手
    case PK_BlackPawn: return "P";
    case PK_BlackLance: return "L";
    case PK_BlackKnight: return "N";
    case PK_BlackSilver: return "S";
    case PK_BlackBishop: return "B";
    case PK_BlackRook: return "R";
    case PK_BlackGold: return "G";
    case PK_BlackKing: return "K";

    case PK_BlackProPawn: return "+P";
    case PK_BlackProLance: return "+L";
    case PK_BlackProKnight: return "+N";
    case PK_BlackProSilver: return "+S";
    case PK_BlackProBishop: return "+B";
    case PK_BlackProRook: return "+R";

    // 後手
    case PK_WhitePawn: return "p";
    case PK_WhiteLance: return "l";
    case PK_WhiteKnight: return "n";
    case PK_WhiteSilver: return "s";
    case PK_WhiteBishop: return "b";
    case PK_WhiteRook: return "r";
    case PK_WhiteGold: return "g";
    case PK_WhiteKing: return "k";

    case PK_WhiteProPawn: return "+p";
    case PK_WhiteProLance: return "+l";
    case PK_WhiteProKnight: return "+n";
    case PK_WhiteProSilver: return "+s";
    case PK_WhiteProBishop: return "+b";
    case PK_WhiteProRook: return "+r";

    default:
        return "?";
    }
}

void print(const Position& Pos) {
    using namespace nshogi::core;

    SquareIterator<IterateOrder::NWSE> SquareIt;
    
    int i = 0;
    for (auto It = SquareIt.begin(); It != SquareIt.end(); ++It) {
        const Square Sq = *It;

        std::cout << nshogi::io::sfen::squareToSfen(Sq);

        if((++i % 9) == 0) std::cout << std::endl;
    }

    std::cout << std::endl << std::endl;
}

int main() {
    using namespace nshogi::core;
    using namespace nnue;

    // Initialize the library.
    initializer::initializeAll();

    // Set up the initial state.
    //auto state = core::StateBuilder::getInitialState();

    StatewithNNUE stateWithNNUE;

    auto& state = stateWithNNUE.getState();

    // Generate legal moves.
    auto moves = MoveGenerator::generateLegalMoves(state);

    internal::ImmutableStateAdapter adapter(state);
    internal::bitboard::Bitboard black_pawns = adapter->getBitboard<Black, PTK_Pawn>();
    // Bitboardが空になる（isZero() が true になる）までループ
    while (!black_pawns.isZero()) {
        // popOne()は、一番下位の駒の位置を取得しつつ、その駒をBitboardから「消去」します
        Square pawn_sq = black_pawns.popOne();
        std::cout << nshogi::io::sfen::squareToSfen(pawn_sq) << std::endl;
    }


    print(state.getPosition());

    stateWithNNUE.doMove(moves[2]);

    print(state.getPosition());

    state.undoMove();

    print(state.getPosition());


    // // Print all moves in sfen format.
    // std::cout << "moves.size(): " << moves.size() << std::endl;
    // for (const auto& move : moves) {
    // std::cout << io::sfen::move32ToSfen(move) << std::endl;
    // }

    return 0;
}