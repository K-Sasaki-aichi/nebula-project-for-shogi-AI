#include "../nshogi/src/core/initializer.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../nshogi/src/io/sfen.h"
#include "../core/squareiterator.h"
#include "../core/types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <iostream>

void print(){

    using namespace core;

    std::stringstream SStream;

    SquareIterator<IterateOrder::NWSE> SquareIt;

    int SequentialEmptyCount = 0;
    for (auto It = SquareIt.begin(); It != SquareIt.end(); ++It) {
        const Square Sq = *It;

        if (Pos.pieceOn(Sq) == PK_Empty) {
            SequentialEmptyCount++;
        } else {
            if (SequentialEmptyCount > 0) {
                SStream << SequentialEmptyCount;
                SequentialEmptyCount = 0;
            }

            SStream << pieceToSfen(Pos.pieceOn(Sq));
        }

        if (squareToFile(Sq) == File1) {
            if (SequentialEmptyCount > 0) {
                SStream << SequentialEmptyCount;
                SequentialEmptyCount = 0;
            }

            if (squareToRank(Sq) != RankI) {
                SStream << '/';
            }
        }
    }

    SStream << ((Pos.sideToMove() == Black) ? " b " : " w ");

    bool StandExists = false;

    for (Color C : Colors) {
        for (PieceTypeKind Pt : StandPieceTypes) {
            const uint8_t StandCount = getStandCount(Pos.getStand(C), Pt);

            if (StandCount > 0) {
                StandExists = true;
                if (StandCount > 1) {
                    SStream << (int)StandCount;
                }
                SStream << pieceToSfen(makePiece(C, Pt));
            }
        }
    }

    if (!StandExists) {
        SStream << "-";
    }

    SStream << " " << Pos.getPlyOffset() + Ply + 1;

    return SStream.str();

}

int main() {
    using namespace nshogi;

    // Initialize the library.
    core::initializer::initializeAll();

    // Set up the initial state.
    auto state = core::StateBuilder::getInitialState();

    // Generate legal moves.
    auto moves = core::MoveGenerator::generateLegalMoves(state);

    // Print all moves in sfen format.
    std::cout << "moves.size(): " << moves.size() << std::endl;
    for (const auto& move : moves) {
        std::cout << io::sfen::move32ToSfen(move) << std::endl;
    }

    return 0;
}