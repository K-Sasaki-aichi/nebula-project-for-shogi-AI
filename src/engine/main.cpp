// #include <nshogi/core/initializer.h>
// #include <nshogi/core/state.h>
// #include <nshogi/core/statebuilder.h>
// #include <nshogi/core/movegenerator.h>
// #include <nshogi/io/sfen.h>
#include "../nshogi/src/core/initializer.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../nshogi/src/io/sfen.h"

#include <iostream>

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