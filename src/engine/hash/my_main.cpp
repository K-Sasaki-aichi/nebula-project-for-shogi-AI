#include "ZobristHash.hpp"
#include <iostream>

int main()
{
    std::cout << "Shogi AI Start!" << std::endl;
    ZobristHash zh;

    uint64_t h = 0;
    std::cout << "Start        : " << h << std::endl;

    // 1回目：手番を変える
    h = zh.update_turn(h);
    std::cout << "After Turn 1 : " << h << " (Random number)" << std::endl;

    // 2回目：もう一度手番を変える（元に戻るはず！）
    h = zh.update_turn(h);
    std::cout << "After Turn 2 : " << h << " (Should be 0)" << std::endl;

    return 0;
}