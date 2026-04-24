// movepicker.h
#pragma once

#include "nshogi/src/core/movegenerator.h"
#include "nshogi/src/core/state.h"
#include "nshogi/src/core/types.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace nebula::engine
{

    inline constexpr int pieceValue(nshogi::core::PieceTypeKind pt) noexcept
    {
        using namespace nshogi::core;
        switch (pt)
        {
        case PTK_Pawn:
            return 100;
        case PTK_Lance:
            return 300;
        case PTK_Knight:
            return 300;
        case PTK_Silver:
            return 500;
        case PTK_Gold:
            return 600;
        case PTK_Bishop:
            return 800;
        case PTK_Rook:
            return 1000;
        case PTK_King:
            return 0;
        case PTK_ProPawn:
            return 600;
        case PTK_ProLance:
            return 600;
        case PTK_ProKnight:
            return 600;
        case PTK_ProSilver:
            return 600;
        case PTK_ProBishop:
            return 900;
        case PTK_ProRook:
            return 1100;
        default:
            return 0;
        }
    }

    inline constexpr int highPromotionBonus(const nshogi::core::Move32 &mv) noexcept
    {
        using namespace nshogi::core;
        if (!mv.promote())
            return 0;

        const auto pt = mv.pieceType(); // 成る前の種類が入っている想定
        if (pt == PTK_Pawn)
            return 400;
        if (pt == PTK_Bishop)
            return 600;
        if (pt == PTK_Rook)
            return 600;
        return 0;
    }

    class MovePicker
    {
    public:
        enum class Stage : uint8_t
        {
            TTMove = 0,
            GoodCapturesAndHighPromotions,
            QuietMoves,
            Done,
        };

        MovePicker(const nshogi::core::State &state, nshogi::core::Move32 ttMove)
            : state_(state), ttMove_(ttMove) {}

        std::optional<nshogi::core::Move32> next()
        {
            while (true)
            {
                switch (stage_)
                {
                case Stage::TTMove:
                {
                    stage_ = Stage::GoodCapturesAndHighPromotions;
                    if (!ttMove_.isNone())
                        return ttMove_;
                    break;
                }

                case Stage::GoodCapturesAndHighPromotions:
                {
                    if (!initialized_)
                        initializeAndSort_();

                    while (goodIndex_ < scoredCaptures_.size())
                    {
                        const auto mv = scoredCaptures_[goodIndex_++].move;
                        if (isDuplicateTT_(mv))
                            continue;
                        if (isStage2_(mv))
                            return mv;
                    }

                    stage_ = Stage::QuietMoves;
                    break;
                }

                case Stage::QuietMoves:
                {
                    if (!initialized_)
                        initializeAndSort_();

                    while (quietIndex_ < scoredQuiet_.size())
                    {
                        const auto mv = scoredQuiet_[quietIndex_++].move;
                        if (isDuplicateTT_(mv))
                            continue;
                        if (!isStage2_(mv))
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

        void initializeAndSort_()
        {
            const auto moves = nshogi::core::MoveGenerator::generateLegalMoves(state_);

            scoredCaptures_.clear();
            scoredQuiet_.clear();

            scoredCaptures_.reserve(moves.size());
            scoredQuiet_.reserve(moves.size());

            for (std::size_t i = 0; i < moves.size(); ++i)
            {
                const auto mv = moves[i];
                ScoredMove sm{mv, scoreLight_(mv)};
                if (isStage2_(mv))
                {
                    scoredCaptures_.push_back(sm);
                }
                else
                {
                    scoredQuiet_.push_back(sm);
                }
            }

            std::sort(scoredCaptures_.begin(), scoredCaptures_.end(),
                      [](const ScoredMove &a, const ScoredMove &b)
                      {
                          return a.score > b.score;
                      });

            initialized_ = true;
            goodIndex_ = 0;
            quietIndex_ = 0;
        }

        int scoreLight_(const nshogi::core::Move32 &mv) const noexcept
        {
            using namespace nshogi::core;

            int s = 0;

            const auto cap = mv.capturePieceType();
            const bool isCapture = (cap != PTK_Empty);

            if (isCapture)
            {
                const int victim = pieceValue(cap);
                const int attacker = pieceValue(mv.pieceType());
                s += 10'000 + (victim * 10 - attacker);
            }

            s += highPromotionBonus(mv);
            return s;
        }

        bool isDuplicateTT_(const nshogi::core::Move32 &mv) const noexcept
        {
            return (!ttMove_.isNone() && mv == ttMove_);
        }

        bool isStage2_(const nshogi::core::Move32 &mv) const noexcept
        {
            using namespace nshogi::core;

            if (highPromotionBonus(mv) != 0)
                return true;

            const auto cap = mv.capturePieceType();
            const bool isCapture = (cap != PTK_Empty);
            if (!isCapture)
                return false;

            const int victim = pieceValue(cap);
            const int attacker = pieceValue(mv.pieceType());
            return victim > attacker;
        }

    private:
        const nshogi::core::State &state_;
        const nshogi::core::Move32 ttMove_;

        Stage stage_ = Stage::TTMove;

        bool initialized_ = false;
        std::vector<ScoredMove> scoredCaptures_;
        std::vector<ScoredMove> scoredQuiet_;
        std::size_t goodIndex_ = 0;
        std::size_t quietIndex_ = 0;
    };

} // namespace nebula::engine