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
        std::string pondermove = "";
        int think_time = 0;
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
        std::string ponder;
        {
            std::lock_guard<std::mutex> lock(g_usi_io_mutex);
            best = g_search.bestmove;
            ponder = g_search.pondermove;
        }

        // ponder手が存在すれば "bestmove XXXX ponder YYYY" の形式で出力
        if (!ponder.empty() && ponder != "none" && ponder != "resign")
        {
            writeLine("bestmove " + best + " ponder " + ponder);
        }
        else
        {
            writeLine("bestmove " + best);
        }
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

        if (i < tokens.size() && tokens[i] == "moves") { ++i; applyMoveTokens(ctx, tokens, i); }
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
        bool ponder = false;
        bool infinite = false;

        // ★ パース処理の修正：単一フラグとキー・値ペアを正しく識別する
        for (std::size_t i = 1; i < tokens.size(); ++i)
        {
            if (tokens[i] == "ponder")
            {
                ponder = true;
                continue; // ponder単体で1トークン消費
            }

            if (i + 1 >= tokens.size())
                break;

            const auto &key = tokens[i];
            const auto &val = tokens[i + 1];

            if (key == "btime")
            {
                btime = std::stoi(val);
                ++i;
            }
            else if (key == "wtime")
            {
                wtime = std::stoi(val);
                ++i;
            }
            else if (key == "binc")
            {
                binc = std::stoi(val);
                ++i;
            }
            else if (key == "winc")
            {
                winc = std::stoi(val);
                ++i;
            }
            else if (key == "byoyomi")
            {
                byoyomi = std::stoi(val);
                ++i;
            }
            else if (key == "movetime")
            {
                movetime = std::stoi(val);
                ++i;
            }
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
            // 1. 通信遅延やエンジンの処理遅延を吸収するためのマージン (ms)
            // 持ち時間が0になって時間切れ負けするのを防ぎます。
            const int NETWORK_OVERHEAD = 50; 
            
            // 実際に使える安全な残り時間 (0未満にならないよう保護)
            int usable_time = std::max(0, my_time - NETWORK_OVERHEAD);

            // 2. 想定残り手数 (Moves To Go) の算出
            // 将棋の平均手数は110〜120手程度。現在のPLYからあと何手続くかを推測します。
            int expected_moves_left;
            if (PLY < 40) {
                expected_moves_left = 35; // 序盤: まだ手数が続く
            } else if (PLY < 80) {
                expected_moves_left = 25; // 中盤: 激しくなりやすい
            } else {
                expected_moves_left = 15; // 終盤: 決着が近い
            }

            // 3. 基本となる思考時間 (Base Time)
            // 残り時間を想定手数で割り、フィッシャー加算の80%を加える。
            // (加算を100%使わず少し貯金することで、難局で時間を使えるようにする)
            int base_time = (usable_time / expected_moves_left) + (my_inc * 8 / 10);

            // 4. 絶対に超えてはいけない上限 (Hard Limit)
            // 1手に残り時間の20%以上を使うと、その後の時間切れリスクが激増するため制限。
            int absolute_limit = (usable_time / 5) + my_inc; 

            // まず、基本時間を上限でクリップする
            think_time = std::min(base_time, absolute_limit);

            // 5. 最低限の思考時間の確保 (Min Limit)
            // どんなに早くても100msは考えて浅い探索による悪手を防ぐ
            int min_think_time = 100;
            think_time = std::max(think_time, min_think_time);

            // 6. 【最終安全装置】実際に残っている安全な時間を絶対に超えない
            think_time = std::min(think_time, usable_time);
        }

        sendInfoString("think_time(ms)=" + std::to_string(think_time));

        ensureNNUEWeightsLoaded(ctx);

        stopAndJoinSearchThreadIfAny();

        engine::isStop.store(false, std::memory_order_relaxed);
        g_search.finished.store(false, std::memory_order_relaxed);
        g_search.bestmove_sent.store(false, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(g_usi_io_mutex);
            g_search.bestmove = "resign";
        }

        g_search.pondering.store(ponder, std::memory_order_relaxed);
        g_search.think_time = think_time;

        if (ponder)
        {
            engine::limitTimeMs.store(std::numeric_limits<long long>::max(), std::memory_order_relaxed);
        } else {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            long long start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            engine::limitTimeMs.store(start_ms + think_time, std::memory_order_relaxed);
        }

        ensureState(ctx);
        const std::string sfen_snapshot = nshogi::io::sfen::stateToSfen(ctx.state->getState());
        const bool weights_loaded = ctx.nnue_weights_loaded;

        g_search.worker = std::thread([sfen_snapshot, weights_loaded, think_time]()
                                      {
            std::string best = "resign";
            std::string ponder_move = "";
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

                    if (!result.ponderMove.isNone())
                    {
                        ponder_move = nshogi::io::sfen::move32ToSfen(result.ponderMove);
                    }
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
                g_search.pondermove = ponder_move;
            }
            g_search.finished.store(true, std::memory_order_relaxed);

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
            writeLine("id name nebula_win_s_d50");
            writeLine("id author Sasaki, Horiuchi");
            writeLine("option name USI_Hash type spin default 4096 min 16 max 131072");
            writeLine("option name USI_Ponder type check default true");
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

        if (cmd == "setoption"){
            stopAndJoinSearchThreadIfAny();
            
            std::string name = "";
            std::string value = "";
            bool parsing_name = false;
            bool parsing_value = false;

            for (size_t i = 1; i < tokens.size(); ++i)
            {
                if (tokens[i] == "name")
                {
                    parsing_name = true;
                    parsing_value = false;
                }
                else if (tokens[i] == "value")
                {
                    parsing_name = false;
                    parsing_value = true;
                }
                else
                {
                    if (parsing_name)
                    {
                        if (!name.empty()) name += " ";
                        name += tokens[i];
                    }
                    else if (parsing_value)
                    {
                        if (!value.empty()) value += " ";
                        value += tokens[i];
                    }
                }
            }

            // USI標準は "USI_Hash" だが、念のためチェス由来の "Hash" にも対応
            if (name == "USI_Hash" || name == "Hash")
            {
                try
                {
                    int hashSizeMb = std::stoi(value);
                    engine::TT.resize(hashSizeMb);
                    // ログ出力（不要であれば削除してください）
                    sendInfoString("Hash size resized to " + std::to_string(hashSizeMb) + " MB");
                }
                catch (const std::exception &)
                {
                    // パース失敗時などは無視
                }
            }

            continue;
        }

        if (cmd == "usinewgame")
        {
            stopAndJoinSearchThreadIfAny();
            ctx.state.emplace(); // 初期局面でアキュムレータを初期化
            engine::TT.clear();
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

            // 予想が命中したので、ここから本来の思考時間のタイマーを始動する
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            long long start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            engine::limitTimeMs.store(start_ms + g_search.think_time, std::memory_order_relaxed);

            // もしponder中にすでに探索が完了していた場合は、即座に bestmove を返す
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