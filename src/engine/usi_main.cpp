// #include "../nshogi/src/core/initializer.h"
// #include "../nshogi/src/core/movegenerator.h"
// #include "../nshogi/src/core/state.h"
// #include "../nshogi/src/core/statebuilder.h"
// #include "../nshogi/src/io/sfen.h"
// #include "StatewithNNUE.h" 
// #include "../model/weights.h"

// #include "search.h"

// #include <algorithm>
// #include <cstdint>
// #include <exception>
// #include <iostream>
// #include <optional>
// #include <sstream>
// #include <stdexcept>
// #include <string>
// #include <vector>

// #if defined(_WIN32)
// #include <windows.h>
// #endif

// namespace
// {

// #if defined(_WIN32)
// 	void setCwdToExecutableDir()
// 	{
// 		wchar_t buf[MAX_PATH] = {0};
// 		const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
// 		if (n == 0 || n >= MAX_PATH)
// 		{
// 			return;
// 		}
// 		std::wstring path(buf, n);
// 		const std::size_t pos = path.find_last_of(L"\\/");
// 		if (pos == std::wstring::npos)
// 		{
// 			return;
// 		}
// 		path.resize(pos);
// 		SetCurrentDirectoryW(path.c_str());
// 	}
// #else
// 	void setCwdToExecutableDir() {}
// #endif

// 	std::vector<std::string> splitTokens(const std::string &line)
// 	{
// 		std::istringstream iss(line);
// 		std::vector<std::string> tokens;
// 		for (std::string t; iss >> t;)
// 		{
// 			tokens.push_back(t);
// 		}
// 		return tokens;
// 	}

// 	void sendInfoString(const std::string &msg)
// 	{
// 		std::cout << "info string " << msg << std::endl;
// 	}

// 	struct EngineContext
// 	{
// 		std::optional<nshogi::core::State> state;
// 		bool nnue_weights_loaded = false;

// 		EngineContext()
// 		{
// 			// 起動直後は startpos 扱いにしておく（GUIは通常 position を送るが保険）
// 			state.emplace(nshogi::core::StateBuilder::getInitialState());
// 		}
// 	};

// 	void ensureNNUEWeightsLoaded(EngineContext &ctx)
// 	{
// 		if (ctx.nnue_weights_loaded)
// 		{
// 			return;
// 		}
// 		// Don't write anything to stdout here (USI protocol). Use stderr for diagnostics.
// 		ctx.nnue_weights_loaded = weight::load();
// 		if (!ctx.nnue_weights_loaded)
// 		{
// 			std::cerr << "[usi] NNUE weight load failed (model/nn.bin). Fallback to non-eval move." << std::endl;
// 		}
// 		else
// 		{
// 			std::cerr << "[usi] NNUE weight loaded." << std::endl;
// 		}
// 	}

// 	void ensureState(EngineContext &ctx)
// 	{
// 		if (!ctx.state.has_value())
// 		{
// 			ctx.state.emplace(nshogi::core::StateBuilder::getInitialState());
// 		}
// 	}

// 	void applyMoveTokens(EngineContext &ctx, const std::vector<std::string> &tokens, std::size_t fromIndex)
// 	{
// 		ensureState(ctx);
// 		for (std::size_t i = fromIndex; i < tokens.size(); ++i)
// 		{
// 			const auto mv = nshogi::io::sfen::sfenToMove32(ctx.state->getPosition(), tokens[i]);
// 			if (mv.isNone() || mv.isWin())
// 			{
// 				// 通常 USI の moves 列には来ないが、念のため無視
// 				continue;
// 			}
// 			ctx.state->doMove(mv);
// 		}
// 	}

// 	void handlePosition(EngineContext &ctx, const std::vector<std::string> &tokens)
// 	{
// 		// position startpos [moves ...]
// 		// position sfen <board> <stm> <hands> <ply> [moves ...]
// 		if (tokens.size() < 2)
// 		{
// 			return;
// 		}

// 		std::size_t i = 1;
// 		if (tokens[i] == "startpos")
// 		{
// 			ctx.state.emplace(nshogi::core::StateBuilder::getInitialState());
// 			++i;
// 		}
// 		else if (tokens[i] == "sfen")
// 		{
// 			if (i + 4 >= tokens.size())
// 			{
// 				throw std::runtime_error("position sfen: insufficient tokens");
// 			}
// 			const std::string sfen = tokens[i + 1] + " " + tokens[i + 2] + " " + tokens[i + 3] + " " + tokens[i + 4];
// 			ctx.state.emplace(nshogi::io::sfen::StateBuilder::newState(sfen));
// 			i += 5;
// 		}
// 		else
// 		{
// 			// 未対応形式
// 			return;
// 		}

// 		if (i < tokens.size() && tokens[i] == "moves")
// 		{
// 			++i;
// 			applyMoveTokens(ctx, tokens, i);
// 		}
// 	}

// 	std::string pickBestmoveSfen(EngineContext &ctx)
// 	{
// 		ensureState(ctx);
// 		const auto moves = nshogi::core::MoveGenerator::generateLegalMoves(*ctx.state);
// 		if (moves.size() == 0)
// 		{
// 			return "resign";
// 		}
// 		return nshogi::io::sfen::move32ToSfen(moves[0]);
// 	}

// 	void handleGo(EngineContext &ctx, const std::vector<std::string> &tokens)
// 	{
// 		// go btime 0 wtime 0 binc 0 winc 0 byoyomi 0 ...
// 		int btime = 0;
// 		int wtime = 0;
// 		int binc = 0;
// 		int winc = 0;
// 		int byoyomi = 0;
// 		int movetime = -1;

// 		for (std::size_t i = 1; i < tokens.size();)
// 		{
// 			const auto &key = tokens[i];
// 			if (i + 1 >= tokens.size())
// 			{
// 				break;
// 			}
// 			const auto &val = tokens[i + 1];

// 			if (key == "btime")
// 				btime = std::stoi(val);
// 			else if (key == "wtime")
// 				wtime = std::stoi(val);
// 			else if (key == "binc")
// 				binc = std::stoi(val);
// 			else if (key == "winc")
// 				winc = std::stoi(val);
// 			else if (key == "byoyomi")
// 				byoyomi = std::stoi(val);
// 			else if (key == "movetime")
// 				movetime = std::stoi(val);

// 			i += 2;
// 		}

// 		ensureState(ctx);
// 		const auto stm = ctx.state->getSideToMove();
// 		const bool isBlack = (stm == nshogi::core::Black);

// 		const int my_time = isBlack ? btime : wtime;
// 		const int my_inc = isBlack ? binc : winc;

// 		int think_time = 0;
// 		if (movetime >= 0)
// 		{
// 			think_time = movetime;
// 		}
// 		else
// 		{
// 			// 例：残り時間の 1/20 + inc + 秒読みを少し
// 			think_time = (my_time / 20) + my_inc + (byoyomi / 2);
// 			think_time = std::max(think_time, 100);
// 		}

// 		sendInfoString("think_time(ms)=" + std::to_string(think_time));

// 		ensureNNUEWeightsLoaded(ctx);

// 		std::string bestmove;
// 		if (ctx.nnue_weights_loaded)
// 		{
// 			const auto mv = engine::searchOnePlyNNUE(*ctx.state);
// 			if (mv.isNone())
// 			{
// 				bestmove = "resign";
// 			}
// 			else
// 			{
// 				bestmove = nshogi::io::sfen::move32ToSfen(mv);
// 			}
// 		}
// 		else
// 		{
// 			// Fallback: return first legal move.
// 			bestmove = pickBestmoveSfen(ctx);
// 		}

// 		std::cout << "bestmove " << bestmove << std::endl;
// 	}

// } // namespace

// int main()
// {
// 	// Make relative paths (e.g., model/nn.bin) work when launched from GUIs.
// 	setCwdToExecutableDir();

// 	// 1) ライブラリ初期化
// 	nshogi::core::initializer::initializeAll();

// 	EngineContext ctx;

// 	// 2) USI 入出力ループ
// 	for (std::string line; std::getline(std::cin, line);)
// 	{
// 		const auto tokens = splitTokens(line);
// 		if (tokens.empty())
// 		{
// 			continue;
// 		}

// 		const auto &cmd = tokens[0];

// 		if (cmd == "usi")
// 		{
// 			std::cout << "id name nebula" << std::endl;
// 			std::cout << "id author Sasaki, Horiuchi" << std::endl;
// 			std::cout << "usiok" << std::endl;
// 			continue;
// 		}

// 		if (cmd == "isready")
// 		{
// 			// Load NNUE weights here so GUI waits until initialization is done.
// 			ensureNNUEWeightsLoaded(ctx);
// 			std::cout << "readyok" << std::endl;
// 			continue;
// 		}

// 		if (cmd == "setoption")
// 		{
// 			// 最小：無視（将来 Hash/Threads 等を読む）
// 			continue;
// 		}

// 		if (cmd == "usinewgame")
// 		{
// 			// 最小：局面を初期化
// 			ctx.state.emplace(nshogi::core::StateBuilder::getInitialState());
// 			continue;
// 		}

// 		if (cmd == "position")
// 		{
// 			try
// 			{
// 				handlePosition(ctx, tokens);
// 			}
// 			catch (const std::exception &e)
// 			{
// 				sendInfoString(std::string("position parse error: ") + e.what());
// 			}
// 			continue;
// 		}

// 		if (cmd == "go")
// 		{
// 			try
// 			{
// 				handleGo(ctx, tokens);
// 			}
// 			catch (const std::exception &e)
// 			{
// 				sendInfoString(std::string("go error: ") + e.what());
// 				std::cout << "bestmove resign" << std::endl;
// 			}
// 			continue;
// 		}

// 		if (cmd == "stop")
// 		{
// 			// 最小：即時応答なので何もしない
// 			continue;
// 		}

// 		if (cmd == "quit")
// 		{
// 			break;
// 		}
// 	}

// 	return 0;
// }
