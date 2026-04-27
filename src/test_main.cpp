// test_main.cpp
#include "engine/movepicker.h"

#include "nshogi/src/core/initializer.h"
#include "nshogi/src/core/state.h"
#include "nshogi/src/core/types.h"
#include "nshogi/src/io/sfen.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace nebula::engine
{
    struct SearchResult
    {
        nshogi::core::Move32 bestMove;
        int score;
        uint64_t nodes;
    };

    // SearchResult searchRoot(nshogi::core::State& state, int depth, nshogi::core::Move32 ttMove);
} // namespace nebula::engine

static const char *b01(bool v) { return v ? "1" : "0"; }

int main()
{
    using namespace nshogi;

    core::initializer::initializeAll();

    const std::string sfen = "4k4/9/9/9/4p4/4R4/9/9/4K4 b - 1";
    core::State state = io::sfen::StateBuilder::newState(sfen);

    std::cout << "=== SFEN ===\n"
              << sfen << "\n\n";

    std::cout << "=== Test1: MovePicker order ===\n";
    {
        nebula::engine::MovePicker picker(state, core::Move32::MoveNone());

        int idx = 0;
        while (auto mvOpt = picker.next())
        {
            const auto mv = *mvOpt;

            const bool isCapture = (mv.capturePieceType() != core::PTK_Empty);
            const bool isPromote = mv.promote();

            const int attackerV = nebula::engine::pieceValue(mv.pieceType());
            const int victimV = nebula::engine::pieceValue(mv.capturePieceType());
            const int mvvLva = isCapture ? (victimV * 10 - attackerV) : 0;
            const int promoBonus = nebula::engine::highPromotionBonus(mv);

            std::cout << "[" << idx << "] "
                      << io::sfen::move32ToSfen(mv)
                      << "  cap=" << b01(isCapture)
                      << " prom=" << b01(isPromote)
                      << "  MVV/LVA=" << mvvLva
                      << "  promoBonus=" << promoBonus
                      << "\n";
            ++idx;
        }
    }
    std::cout << "\n";

    // std::cout << "=== Test2: searchRoot depth=3 ===\n";
    // {
    //     auto r = nebula::engine::searchRoot(state, 3, core::Move32::MoveNone());
    //     std::cout << "bestMove = " << io::sfen::move32ToSfen(r.bestMove) << "\n";
    //     std::cout << "score    = " << r.score << "\n";
    //     std::cout << "nodes    = " << r.nodes << "\n";
    // }

    return 0;
}