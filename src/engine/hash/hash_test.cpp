#include "../positionbuilder.h"
#include "hash.h"
#include <cassert>
#include <iostream>

using namespace nshogi::core;
using namespace nshogi::core::internal;

// テスト用に PositionBuilder を継承して protected メソッドにアクセスする
struct TestBuilder : public nshogi::core::PositionBuilder {
    TestBuilder() = default;
    TestBuilder(const nshogi::core::Position& P)
        : PositionBuilder(P) {
    }

    Position buildPublic() {
        return build();
    }
    void setPiecePublic(Square Sq, PieceKind Piece) {
        setPiece(Sq, Piece);
    }
    void setColorPublic(Color C) {
        setColor(C);
    }
};

int main() {
    std::cout << "Zobrist Test Start\n";

    Hash<uint64_t> zh;

    // --- テスト局面1 ---
    TestBuilder b;
    // 全て空にする
    for (int i = 0; i < 81; ++i) {
        b.setPiecePublic(static_cast<Square>(i), PK_Empty);
    }

    b.setPiecePublic(static_cast<Square>(0), PK_BlackPawn); // 適当な駒配置
    b.setPiecePublic(static_cast<Square>(10), PK_BlackSilver);
    b.setColorPublic(Black);

    Position pos1 = b.buildPublic();

    zh.refresh(pos1);
    uint64_t h1 = zh.getValue();

    std::cout << "Hash1: " << h1 << "\n";

    // --- 同じ局面を再生成 ---
    Position pos2 = nshogi::core::PositionBuilder::newPosition(pos1);

    zh.refresh(pos2);
    uint64_t h2 = zh.getValue();

    std::cout << "Hash2: " << h2 << "\n";

    assert(h1 == h2);

    // --- 手番違いチェック ---
    TestBuilder b2(pos2);
    b2.setColorPublic(White);
    pos2 = b2.buildPublic();

    zh.refresh(pos2);
    uint64_t h3 = zh.getValue();

    std::cout << "Hash3 (side changed): " << h3 << "\n";

    assert(h1 != h3);

    std::cout << "All tests passed!\n";

    return 0;
}