#include "../nshogi/src/core/initializer.h"
#include "../nshogi/src/core/movegenerator.h"
#include "../nshogi/src/core/state.h"
#include "../nshogi/src/core/statebuilder.h"
#include "../nshogi/src/io/sfen.h"
#include "StatewithNNUE.h"
#include "../model/weights.h"
#include "../book/book.h"

#include "search.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <atomic>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{

    std::mutex g_usi_io_mutex;

    int PLY;

    void writeLine(const std::string &line)
    {
        std::lock_guard<std::mutex> lock(g_usi_io_mutex);
        std::cout << line << std::endl;
    }

#if defined(_WIN32)
    void setCwdToExecutableDir()
    {
        wchar_t buf[MAX_PATH] = {0};
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
        {
            return;
        }
        std::wstring path(buf, n);
        const std::size_t pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
        {
            return;
        }
        path.resize(pos);
        SetCurrentDirectoryW(path.c_str());
    }
#else
    void setCwdToExecutableDir() {}
#endif

    std::vector<std::string> splitTokens(const std::string &line)
    {
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        for (std::string t; iss >> t;)
        {
            tokens.push_back(t);
        }
        return tokens;
    }

    void sendInfoString(const std::string &msg)
    {
        writeLine("info string " + msg);
    }

    struct SearchController
    {
        std::thread worker;
        std::atomic<bool> pondering{false};
        std::atomic<bool> finished{false};
        std::atomic<bool> bestmove_sent{false};

        // guarded by g_usi_io_mutex
        std::string bestmove = "resign";
    };

    SearchController g_search;

    void stopAndJoinSearchThreadIfAny()
    {
        engine::isStop.store(true, std::memory_order_relaxed);
        if (g_search.worker.joinable())
        {
            g_search.worker.join();
        }
    }

    void sendBestmoveOnceFromStored()
    {
        bool expected = false;
        if (!g_search.bestmove_sent.compare_exchange_strong(expected, true))
        {
            return;
        }

        std::string best;
        {
            std::lock_guard<std::mutex> lock(g_usi_io_mutex);
            best = g_search.bestmove;
        }
        writeLine("bestmove " + best);
    }

    void tryPublishBestmove()
    {
        if (engine::isStop.load(std::memory_order_relaxed))
        {
            return;
        }

        if (g_search.pondering.load(std::memory_order_relaxed))
        {
            return;
        }

        if (!g_search.finished.load(std::memory_order_relaxed))
        {
            return;
        }

        sendBestmoveOnceFromStored();
    }

    struct EngineContext
    {
        // nshogi::core::State から nnue::StatewithNNUE に変更
        std::optional<nnue::StatewithNNUE> state;
        bool nnue_weights_loaded = false;
        bool book_loaded = false;

        EngineContext()
        {
            // デフォルトコンストラクタで初期盤面＆アキュムレータ計算が走る
            state.emplace();
        }
    };

    void ensureNNUEWeightsLoaded(EngineContext &ctx)
    {
        if (ctx.nnue_weights_loaded)
        {
            return;
        }
        // USIプロトコル上の標準出力には出さず、診断用の標準エラー出力を使う
        ctx.nnue_weights_loaded = weight::load();
        if (!ctx.nnue_weights_loaded)
        {
            std::cerr << "[usi] NNUE weight load failed (model/nn.bin). Fallback to non-eval move." << std::endl;
        }
        else
        {
            std::cerr << "[usi] NNUE weight loaded." << std::endl;
        }
    }

    void ensureBookLoaded(EngineContext &ctx)
    {
        if (ctx.book_loaded)
        {
            return;
        }

        // book.h で定義した loadBook を呼び出す
        // ファイルが見つからない等のエラー処理は loadBook 内で行う想定
        try
        {
            loadBook("book/book2.bin");
            ctx.book_loaded = true;
            std::cerr << "[usi] Book loaded." << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[usi] Book load failed: " << e.what() << std::endl;
            // 失敗しても探索はできるので、フラグだけ立てて何度も読みに行かないようにする
            ctx.book_loaded = true;
        }
    }

    void ensureState(EngineContext &ctx)
    {
        if (!ctx.state.has_value())
        {
            ctx.state.emplace();
        }
    }

    void applyMoveTokens(EngineContext &ctx, const std::vector<std::string> &tokens, std::size_t fromIndex)
    {
        ensureState(ctx);
        PLY = tokens.size();
        for (std::size_t i = fromIndex; i < tokens.size(); ++i)
        {
            // sfenToMove32はPositionを要求するため、getPosition()を渡す
            const auto mv = nshogi::io::sfen::sfenToMove32(ctx.state->getPosition(), tokens[i]);
            if (mv.isNone() || mv.isWin())
            {
                continue;
            }
            // StatewithNNUE の doMove が呼ばれ、NNUEの差分更新が実行される
            ctx.state->doMove(mv);
        }
    }

    void handlePosition(EngineContext &ctx, const std::vector<std::string> &tokens)
    {
        if (tokens.size() < 2)
        {
            return;
        }

        std::size_t i = 1;
        if (tokens[i] == "startpos")
        {
            ctx.state.emplace(); // StatewithNNUE() 初期化（フル計算）
            ++i;
        }
        else if (tokens[i] == "sfen")
        {
            if (i + 4 >= tokens.size())
            {
                throw std::runtime_error("position sfen: insufficient tokens");
            }
            const std::string sfen = tokens[i + 1] + " " + tokens[i + 2] + " " + tokens[i + 3] + " " + tokens[i + 4];

            // nshogi::core::State を生成し、StatewithNNUE(nshogi::core::State&&) コンストラクタにムーブして渡す
            ctx.state.emplace(nshogi::io::sfen::StateBuilder::newState(sfen));
            i += 5;
        }
        else
        {
            return;
        }

        if (i < tokens.size() && tokens[i] == "moves")
        {
            ++i;
            applyMoveTokens(ctx, tokens, i);
        }
    }

    std::string pickBestmoveSfen(EngineContext &ctx)
    {
        ensureState(ctx);
        // MoveGenerator は nshogi::core::State を要求するため、getState() で参照を渡す
        const auto moves = nshogi::core::MoveGenerator::generateLegalMoves(ctx.state->getState());
        if (moves.size() == 0)
        {
            return "resign";
        }
        return nshogi::io::sfen::move32ToSfen(moves[0]);
    }

    void handleGo(EngineContext &ctx, const std::vector<std::string> &tokens)
    {
        int btime = 0;
        int wtime = 0;
        int binc = 0;
        int winc = 0;
        int byoyomi = 0;
        int movetime = -1;

        for (std::size_t i = 1; i < tokens.size();)
        {
            const auto &key = tokens[i];
            if (i + 1 >= tokens.size())
                break;
            const auto &val = tokens[i + 1];

            if (key == "btime")
                btime = std::stoi(val);
            else if (key == "wtime")
                wtime = std::stoi(val);
            else if (key == "binc")
                binc = std::stoi(val);
            else if (key == "winc")
                winc = std::stoi(val);
            else if (key == "byoyomi")
                byoyomi = std::stoi(val);
            else if (key == "movetime")
                movetime = std::stoi(val);

            i += 2;
        }

        ensureState(ctx);
        const auto stm = ctx.state->getSideToMove();
        const bool isBlack = (stm == nshogi::core::Black);

        const int my_time = isBlack ? btime : wtime;
        const int my_inc = isBlack ? binc : winc;

        int think_time = 0;
        if (movetime >= 0)
        {
            think_time = movetime;
        }
        else
        {
            int divisor = (PLY < 30) ? 40 : 23;

            // 基本の計算
            think_time = (my_time / divisor) + my_inc;

            // 3. 序盤の最低思考時間を短めに、中盤以降を長めにする調整
            // 序盤は 500ms、中盤以降は 2000ms を保証するなど
            int min_think_time = (PLY < 30) ? 1000 : 2000;
            think_time = std::max(think_time, min_think_time);

            // 4. 残り時間による絶対制限（時間切れ防止）
            int absolute_limit = my_time * 0.85;
            think_time = std::min(think_time, absolute_limit);
        }

        sendInfoString("think_time(ms)=" + std::to_string(think_time));

        ensureNNUEWeightsLoaded(ctx);

        // 二重起動防止: 既存探索があれば止めて待つ
        stopAndJoinSearchThreadIfAny();

        // フラグ初期化
        engine::isStop.store(false, std::memory_order_relaxed);
        g_search.finished.store(false, std::memory_order_relaxed);
        g_search.bestmove_sent.store(false, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(g_usi_io_mutex);
            g_search.bestmove = "resign";
        }

        bool ponder = false;
        for (const auto &t : tokens)
        {
            if (t == "ponder")
            {
                ponder = true;
                break;
            }
        }
        g_search.pondering.store(ponder, std::memory_order_relaxed);

        ensureState(ctx);
        const std::string sfen_snapshot = nshogi::io::sfen::stateToSfen(ctx.state->getState());
        const bool weights_loaded = ctx.nnue_weights_loaded;

        g_search.worker = std::thread([sfen_snapshot, weights_loaded, think_time]()
                                      {
            std::string best = "resign";
            int depth = 0;
            int score = 0;

            if (weights_loaded)
            {
                nnue::StatewithNNUE local_state(nshogi::io::sfen::StateBuilder::newState(sfen_snapshot));
                const auto result = engine::searchNNUE(local_state, think_time, PLY);
                if (!result.bestMove.isNone())
                {
                    best = nshogi::io::sfen::move32ToSfen(result.bestMove);
                    depth = result.depth;
                    score = result.score;
                }
            }
            else
            {
                const auto st = nshogi::io::sfen::StateBuilder::newState(sfen_snapshot);
                const auto moves = nshogi::core::MoveGenerator::generateLegalMoves(st);
                if (moves.size() != 0)
                {
                    best = nshogi::io::sfen::move32ToSfen(moves[0]);
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_usi_io_mutex);
                g_search.bestmove = best;
            }
            g_search.finished.store(true, std::memory_order_relaxed);

            // ponder中は出力せず保持。通常goならここでbestmoveを出す(1回だけ)。
            if (!g_search.pondering.load(std::memory_order_relaxed))
            {
                std::cout << "info depth " << depth << " score cp " << score << " pv " << best << std::endl;
                sendBestmoveOnceFromStored();
            } });
    }

} // namespace

int main()
{
    setCwdToExecutableDir();

    nshogi::core::initializer::initializeAll();

    EngineContext ctx;

    for (std::string line; std::getline(std::cin, line);)
    {
        const auto tokens = splitTokens(line);
        if (tokens.empty())
            continue;

        const auto &cmd = tokens[0];

        if (cmd == "usi")
        {
            writeLine("id name nebula_debug");
            writeLine("id author Sasaki, Horiuchi");
            writeLine("usiok");
            continue;
        }

        if (cmd == "isready")
        {
            ensureNNUEWeightsLoaded(ctx);
            ensureBookLoaded(ctx);
            writeLine("readyok");
            continue;
        }

        if (cmd == "setoption")
            continue;

        if (cmd == "usinewgame")
        {
            stopAndJoinSearchThreadIfAny();
            ctx.state.emplace(); // 初期局面でアキュムレータを初期化
            continue;
        }

        if (cmd == "position")
        {
            try
            {
                handlePosition(ctx, tokens);
            }
            catch (const std::exception &e)
            {
                sendInfoString(std::string("position parse error: ") + e.what());
            }
            continue;
        }

        if (cmd == "go")
        {
            try
            {
                handleGo(ctx, tokens);
            }
            catch (const std::exception &e)
            {
                sendInfoString(std::string("go error: ") + e.what());
                writeLine("bestmove resign");
            }
            continue;
        }

        if (cmd == "stop")
        {
            stopAndJoinSearchThreadIfAny();
            sendBestmoveOnceFromStored();
            continue;
        }

        if (cmd == "ponderhit")
        {
            g_search.pondering.store(false, std::memory_order_relaxed);
            tryPublishBestmove();
            continue;
        }

        if (cmd == "quit")
        {
            stopAndJoinSearchThreadIfAny();
            break;
        }
    }

    return 0;
}