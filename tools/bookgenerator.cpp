#include "../src/nshogi/src/io/csa.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>

std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("File not found");
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

int main() {
    try {
        std::string csa_data = readFile("game.csa");

        nshogi::core::State state = nshogi::io::csa::StateBuilder::newState(csa_data);

        // stateを使って探索や評価を行う...
    } catch (const std::exception& e) {
        // charToRankなどで投げられるruntime_errorをここでキャッチ
        std::cerr << "Parse error: " << e.what() << std::endl;
    }
}