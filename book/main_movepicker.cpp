// main_movepicker.cpp
#include "engine/movepicker.h"

#include "nshogi/src/core/initializer.h"
#include "nshogi/src/core/state.h"
#include "nshogi/src/io/sfen.h"

#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    using namespace nshogi;

    core::initializer::initializeAll();

    const std::string startSfen =
        "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";

    core::State state = io::sfen::StateBuilder::newState(startSfen);

    std::cout << "=== Initial SFEN ===\n"
              << io::sfen::stateToSfen(state) << "\n\n";

    nebula::engine::OrderingInfo info{};
    nebula::engine::MovePicker picker(state, info);

    std::uint64_t count = 0;
    while (auto mv_opt = picker.next())
    {
        const auto mv = *mv_opt;
        std::cout << io::sfen::move32ToSfen(mv) << "\n";
        ++count;
    }

    std::cout << "\nTotal moves: " << count << "\n";
    return 0;
}
