// movepicker.h
#pragma once

#include "nshogi/src/core/movegenerator.h"
#include "nshogi/src/core/state.h"
#include "nshogi/src/core/types.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#if __has_include("engine/TT.h")
#include "engine/TT.h"
#endif

namespace nebula::engine
{
    // 駒の強さ（move ordering用）
    // 分岐を避けるため、配列テーブルを正とする。
    inline constexpr std::array<int, nshogi::core::NumPieceType> pieceValueTable = {
        0,    // Empty
        100,  // Pawn
        300,  // Lance
        300,  // Knight
        500,  // Silver
        800,  // Bishop
        1000, // Rook
        600,  // Gold
        0,    // King
        600,  // ProPawn
        600,  // ProLance
        600,  // ProKnight
        600,  // ProSilver
        900,  // ProBishop
        1100  // ProRook
    };

    inline constexpr int pieceValue(nshogi::core::PieceTypeKind pt) noexcept
    { // static_cast<std::size_t>(pt) は列挙型 pt を添字（整数）に変換するためのキャスト
        return pieceValueTable[static_cast<std::size_t>(pt)];
    }

    // 互換用（現行の MovePicker の ordering では promotion bonus を使わない）
    inline constexpr int highPromotionBonus(const nshogi::core::Move32 &) noexcept
    {
        return 0;
    }

    // MovePicker は「順番に返す」ことに責務を限定し、
    // ordering に必要な情報（hashMove/killer）は外から注入する。
    struct OrderingInfo
    {
        // 置換表由来の手（= hash_move）
        nshogi::core::Move32 hashMove = nshogi::core::Move32::MoveNone();
        nshogi::core::Move32 killer1 = nshogi::core::Move32::MoveNone();
        nshogi::core::Move32 killer2 = nshogi::core::Move32::MoveNone();
    };

#if __has_include("engine/TT.h")
    // TT (置換表) から「hash move」を取得する。
    // - 返る手は TTEntry に入っているだけなので、合法性チェックは MovePicker 側で行う。
    inline nshogi::core::Move32 hash_move(const nshogi::core::State &state,
                                          ::engine::TranspositionTable &tt) noexcept
    {
        const uint64_t hash = state.getHash();
        const ::TTEntry *entry = tt.probe(hash);
        if (!tt.isHit(entry, hash))
            return nshogi::core::Move32::MoveNone();

        return entry->move;
    }

    inline OrderingInfo makeOrderingInfoFromTT(const nshogi::core::State &state,
                                               ::engine::TranspositionTable &tt) noexcept
    {
        OrderingInfo info{};
        info.hashMove = hash_move(state, tt);
        return info;
    }

    inline nshogi::core::Move32 hash_move(const nshogi::core::State &state) noexcept
    {
        return hash_move(state, ::engine::TT);
    }

    inline OrderingInfo makeOrderingInfoFromTT(const nshogi::core::State &state) noexcept
    {
        return makeOrderingInfoFromTT(state, ::engine::TT);
    }
#endif

    class MovePicker
    {
    public:
        enum class Stage : uint8_t
        {
            HashMove = 0,
            Captures,
            Killers,
            QuietMoves,
            Done,
        };

        MovePicker(const nshogi::core::State &state, const OrderingInfo &info)
            : state_(state), info_(info) {}

        // 互換用（従来インターフェース）
        MovePicker(const nshogi::core::State &state, nshogi::core::Move32 hashMove)
            : state_(state)
        {
            info_.hashMove = hashMove;
        }

        std::optional<nshogi::core::Move32> next()
        {
            while (true)
            {
                switch (stage_)
                {
                case Stage::HashMove:
                {
                    stage_ = Stage::Captures;
                    ensureInitialized_();

                    if (!hashMoveLegal_.isNone())
                        return hashMoveLegal_;
                    break;
                }

                case Stage::Captures:
                {
                    ensureInitialized_();

                    while (captureIndex_ < scoredCaptures_.size())
                    {
                        return scoredCaptures_[captureIndex_++].move;
                    }

                    stage_ = Stage::Killers;
                    break;
                }

                case Stage::Killers:
                {
                    ensureInitialized_();

                    while (killerIndex_ < 2)
                    {
                        const std::size_t which = killerIndex_++;
                        const nshogi::core::Move32 mv = (which == 0) ? legalKiller1_ : legalKiller2_;
                        if (!mv.isNone())
                            return mv;
                    }

                    stage_ = Stage::QuietMoves;
                    break;
                }

                case Stage::QuietMoves:
                {
                    ensureInitialized_();

                    while (quietIndex_ < quietMoves_.size())
                    {
                        const auto mv = quietMoves_[quietIndex_++];
                        if (!legalKiller1_.isNone() && mv == legalKiller1_)
                            continue;
                        if (!legalKiller2_.isNone() && mv == legalKiller2_)
                            continue;
                        return mv;
                    }

                    stage_ = Stage::Done;
                    break;
                }

                case Stage::Done:
                default:
                    return std::nullopt;
                }
            }
        }

    private:
        struct ScoredMove
        {
            nshogi::core::Move32 move;
            int score;
        };

        static constexpr bool isCapture_(const nshogi::core::Move32 &mv) noexcept
        {
            using namespace nshogi::core;
            return mv.capturePieceType() != PTK_Empty;
        }

        int scoreCapture_(const nshogi::core::Move32 &mv) const noexcept
        {
            using namespace nshogi::core;

            const auto cap = mv.capturePieceType();
            const bool isCapture = (cap != PTK_Empty);

            if (!isCapture)
                return 0;

            // MVV-LVA: Most Valuable Victim - Least Valuable Attacker
            const int victim = pieceValue(cap);
            const int attacker = pieceValue(mv.pieceType());
            int score = (victim * 10 - attacker);

            // bad capture は captures 内の後ろへ回す
            if (victim < attacker)
            {
                constexpr int BadCapturePenalty = 100'000;
                score -= BadCapturePenalty;
            }

            return score;
        }
        /*generate legal moves
          → 分類（capture / quiet / hash）
          → killer確認
         → captureをソート
         → 初期化完了*/
        void ensureInitialized_()
        { // この関数は「合法手を作って、分類して、並べて、next()で高速に取り出せる状態にする初期化処理」
            if (initialized_)
                return;

            const auto legalMoves = nshogi::core::MoveGenerator::generateLegalMoves(state_);

            hashMoveLegal_ = nshogi::core::Move32::MoveNone();
            legalKiller1_ = nshogi::core::Move32::MoveNone();
            legalKiller2_ = nshogi::core::Move32::MoveNone();

            scoredCaptures_.clear();
            quietMoves_.clear();
            scoredCaptures_.reserve(legalMoves.size());
            quietMoves_.reserve(legalMoves.size());

            for (const auto &mv : legalMoves)
            {
                // hash move は最優先で返すため、以降のリストからは除外して重複を防ぐ。
                if (!info_.hashMove.isNone() && mv == info_.hashMove)
                {
                    hashMoveLegal_ = mv;
                    continue;
                }

                if (isCapture_(mv))
                    scoredCaptures_.push_back(ScoredMove{mv, scoreCapture_(mv)});
                else
                {
                    quietMoves_.push_back(mv);

                    // killer の合法性はここで確定する（後段で探索しない）
                    if (!info_.killer1.isNone() && mv == info_.killer1)
                        legalKiller1_ = mv;
                    if (!info_.killer2.isNone() && mv == info_.killer2)
                        legalKiller2_ = mv;
                }
            }

            if (!legalKiller1_.isNone() && legalKiller2_ == legalKiller1_)
                legalKiller2_ = nshogi::core::Move32::MoveNone();

            // ここの部分全部並び替えるから動作が重いかもしれない
            std::stable_sort(scoredCaptures_.begin(), scoredCaptures_.end(),
                             [](const ScoredMove &a, const ScoredMove &b)
                             {
                                 return a.score > b.score;
                             });

            initialized_ = true;
            captureIndex_ = 0;
            killerIndex_ = 0;
            quietIndex_ = 0;
        }

    private:
        const nshogi::core::State &state_;
        OrderingInfo info_{};

        nshogi::core::Move32 hashMoveLegal_ = nshogi::core::Move32::MoveNone();
        nshogi::core::Move32 legalKiller1_ = nshogi::core::Move32::MoveNone();
        nshogi::core::Move32 legalKiller2_ = nshogi::core::Move32::MoveNone();

        Stage stage_ = Stage::HashMove;

        bool initialized_ = false;
        std::vector<ScoredMove> scoredCaptures_;
        std::vector<nshogi::core::Move32> quietMoves_;
        std::size_t captureIndex_ = 0;
        std::size_t killerIndex_ = 0;
        std::size_t quietIndex_ = 0;
    };

} // namespace nebula::engine