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
#include "eval.h"
#include "../model/weights.h"
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

        std::cout << PieceKindtoString(Pos.pieceOn(Sq));      

        if((++i % 9) == 0) std::cout << std::endl;
    }
}


int main() {
    using namespace nshogi::core;
    using namespace nnue;

    initializer::initializeAll();
    
    std::string customSfen = "lnsgkgsnl/1r5b1/ppppppppp/7K1/9/9/PPPPPPP1P/1B5R1/LNSG1GSNL b P 1";

    StatewithNNUE stateWithNNUE(std::move(nshogi::io::sfen::StateBuilder::newState(customSfen)));
    auto& state = stateWithNNUE.getState();
    auto moves = MoveGenerator::generateLegalMoves(state);

    if(!weight::load()){
        std::cout << "load faild" << std::endl;
        return 1;
    } else {
        std::cout << "load success\n" << std::endl;
    }
    int moveCount = moves.size();
    std::cout << "legal size = " << moveCount << std::endl;

    int count = 0;
    Move32 M = moves[0];

    // ==========================================
    // 1. 差分更新 (Incremental) で計算
    // ==========================================
    std::cout << "Incremental test" << std::endl;
    stateWithNNUE.init();
    stateWithNNUE.doMove<Black>(M);
    print(state.getPosition());

    // 両方の視点から評価値を取得
    int32_t inc_score_black = nnue::eval::eval<Black>(stateWithNNUE);
    int32_t inc_score_white = nnue::eval::eval<White>(stateWithNNUE);
    std::cout << "Black Eval: " << inc_score_black << std::endl;
    std::cout << "White Eval: " << inc_score_white << std::endl;

    auto moves1 = MoveGenerator::generateLegalMoves(state);
    M = moves1[0];
    stateWithNNUE.doMove<White>(M);
    print(state.getPosition());

    // 両方の視点から評価値を取得
    inc_score_black = nnue::eval::eval<Black>(stateWithNNUE);
    inc_score_white = nnue::eval::eval<White>(stateWithNNUE);
    std::cout << "Black Eval: " << inc_score_black << std::endl;
    std::cout << "White Eval: " << inc_score_white << std::endl;

    stateWithNNUE.undoMove();
    print(state.getPosition());

    inc_score_black = nnue::eval::eval<Black>(stateWithNNUE);
    inc_score_white = nnue::eval::eval<White>(stateWithNNUE);
    std::cout << "Black Eval: " << inc_score_black << std::endl;
    std::cout << "White Eval: " << inc_score_white << std::endl;

    // メモリ上のアキュムレータの状態をコピーして保存しておく
    int16_t saved_acc_black[256];
    int16_t saved_acc_white[256];
    std::memcpy(saved_acc_black, stateWithNNUE.getAcc()[Black], sizeof(saved_acc_black));
    std::memcpy(saved_acc_white, stateWithNNUE.getAcc()[White], sizeof(saved_acc_white));

    stateWithNNUE.undoMove();
    std::cout << "--------------------------------\n" << std::endl;

    // ==========================================
    // 2. 全計算 (Full Refresh) で計算
    // ==========================================
    std::cout << "full Refresh test" << std::endl;
    stateWithNNUE.init();
    state.doMove(M); // stateの盤面だけ進める
    
    // アキュムレータを1から再構築
    stateWithNNUE.refresh_acc<Black>();
    stateWithNNUE.refresh_acc<White>();
    print(state.getPosition());

    // 両方の視点から評価値を取得
    int32_t full_score_black = nnue::eval::eval<Black>(stateWithNNUE);
    int32_t full_score_white = nnue::eval::eval<White>(stateWithNNUE);
    std::cout << "Black Eval: " << full_score_black << std::endl;
    std::cout << "White Eval: " << full_score_white << std::endl;

    M = moves1[0];

    state.doMove(M);

    // アキュムレータを1から再構築
    stateWithNNUE.refresh_acc<Black>();
    stateWithNNUE.refresh_acc<White>();
    print(state.getPosition());

    // 両方の視点から評価値を取得
    full_score_black = nnue::eval::eval<Black>(stateWithNNUE);
    full_score_white = nnue::eval::eval<White>(stateWithNNUE);
    std::cout << "Black Eval: " << full_score_black << std::endl;
    std::cout << "White Eval: " << full_score_white << std::endl;

    std::cout << "--------------------------------\n" << std::endl;

    // ==========================================
    // 3. アキュムレータの完全一致検証
    // ==========================================
    std::cout << "result" << std::endl;
    
    bool isBlackMatch = (std::memcmp(saved_acc_black, stateWithNNUE.getAcc()[Black], sizeof(saved_acc_black)) == 0);
    bool isWhiteMatch = (std::memcmp(saved_acc_white, stateWithNNUE.getAcc()[White], sizeof(saved_acc_white)) == 0);

    if (isBlackMatch) {
        std::cout << "[OK] Black: match" << std::endl;
    } else {
        std::cout << "[NG] Black: not-match" << std::endl;
    }

    if (isWhiteMatch) {
        std::cout << "[OK] White: match" << std::endl;
    } else {
        std::cout << "[NG] White: not-match" << std::endl;
    }

    return 0;
}