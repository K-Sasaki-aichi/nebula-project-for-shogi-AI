#include "ZobristHash.hpp"
#include <iostream>

int main() {
    std::cout << "Shogi AI Start!" << std::endl;
    ZobristHash zh;
    std::cout << "Initial Hash: " << zh.update_turn(0) << std::endl;
    return 0;
}