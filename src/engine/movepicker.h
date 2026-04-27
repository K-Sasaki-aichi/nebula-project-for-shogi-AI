// movepicker.h
#pragma once

#include "nshogi/src/core/movegenerator.h"
#include "nshogi/src/core/state.h"
#include "nshogi/src/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#if __has_include("engine/TT.h")
#include "engine/TT.h"
#endif

namespace nebula::engine
{
    inline constexpr ::std::array<int, ::nshogi::core::NumPieceType> pieceValueTable = {
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

    inline constexpr int pieceValue(::nshogi::core::PieceTypeKind pt) noexcept
    {
        return pieceValueTable[static_cast<::std::size_t>(pt)];
    }

    inline constexpr int highPromotionBonus(const ::nshogi::core::Move32 &) noexcept
    {
        return 0;
    }

    struct OrderingInfo
    {
        ::nshogi::core::Move32 hashMove = ::nshogi::core::Move32::MoveNone();
        ::nshogi::core::Move32 killer1 = ::nshogi::core::Move32::MoveNone();
        ::nshogi::core::Move32 killer2 = ::nshogi::core::Move32::MoveNone();
    };

#if __has_include("engine/TT.h")
    inline ::nshogi::core::Move32 hash_move(const ::nshogi::core::State &state,
                                            ::engine::TranspositionTable &tt) noexcept
    {
        const uint64_t hash = state.getHash();
        const ::TTEntry *entry = tt.probe(hash);
        if (!tt.isHit(entry, hash))
            return ::nshogi::core::Move32::MoveNone();

        return entry->move;
    }

    inline OrderingInfo makeOrderingInfoFromTT(const ::nshogi::core::State &state,
                                               ::engine::TranspositionTable &tt) noexcept
    {
        OrderingInfo info{};
        info.hashMove = hash_move(state, tt);
        return info;
    }

    inline ::nshogi::core::Move32 hash_move(const ::nshogi::core::State &state) noexcept
    {
        return hash_move(state, ::engine::TT);
    }

    inline OrderingInfo makeOrderingInfoFromTT(const ::nshogi::core::State &state) noexcept
    {
        return makeOrderingInfoFromTT(state, ::engine::TT);
    }
#endif

    class MovePicker
    {
    public:
        MovePicker(const ::nshogi::core::State &state, const OrderingInfo &info)
            : state_(state), info_(info) {}

        MovePicker(const ::nshogi::core::State &state, ::nshogi::core::Move32 hashMove)
            : state_(state)
        {
            info_.hashMove = hashMove;
        }

        ::std::optional<::nshogi::core::Move32> next()
        {
            ensureInitialized_();

            const auto mv = getNextMove_();
            if (mv.isNone())
                return ::std::nullopt;
            return mv;
        }

    private:
        struct ScoredMove
        {
            ::nshogi::core::Move32 move;
            int score;
        };

        static constexpr ::std::size_t MaxMoves = 600;

        static constexpr bool isCapture_(const ::nshogi::core::Move32 &mv) noexcept
        {
            using namespace ::nshogi::core;
            return mv.capturePieceType() != PTK_Empty;
        }

        int scoreCapture_(const ::nshogi::core::Move32 &mv) const noexcept
        {
            using namespace ::nshogi::core;

            const auto cap = mv.capturePieceType();
            if (cap == PTK_Empty)
                return 0;

            const int victim = pieceValue(cap);
            const int attacker = pieceValue(mv.pieceType());
            int score = (victim * 10 - attacker);

            if (victim < attacker)
            {
                constexpr int BadCapturePenalty = 100'000;
                score -= BadCapturePenalty;
            }

            return score;
        }

        int scoreMove_(const ::nshogi::core::Move32 &mv) const noexcept
        {
            if (!info_.hashMove.isNone() && mv == info_.hashMove)
                return 1'000'000;

            if (!info_.killer1.isNone() && mv == info_.killer1)
                return 400'000;
            if (!info_.killer2.isNone() && mv == info_.killer2)
                return 399'000;

            if (isCapture_(mv))
                return 500'000 + scoreCapture_(mv);

            return 0;
        }

        ::nshogi::core::Move32 getNextMove_() noexcept
        {
            if (index_ >= count_)
                return ::nshogi::core::Move32::MoveNone();

            ::std::size_t bestIndex = index_;
            int bestScore = moves_[index_].score;
            for (::std::size_t i = index_ + 1; i < count_; ++i)
            {
                const int s = moves_[i].score;
                if (s > bestScore)
                {
                    bestScore = s;
                    bestIndex = i;
                }
            }

            if (bestIndex != index_)
                ::std::swap(moves_[index_], moves_[bestIndex]);

            return moves_[index_++].move;
        }

        void ensureInitialized_()
        {
            if (initialized_)
                return;

            const auto legalMoves = ::nshogi::core::MoveGenerator::generateLegalMoves(state_);

            count_ = 0;
            index_ = 0;
            for (const auto mv : legalMoves)
            {
                if (count_ >= MaxMoves)
                    break;
                moves_[count_++] = ScoredMove{mv, scoreMove_(mv)};
            }

            initialized_ = true;
        }

    private:
        const ::nshogi::core::State &state_;
        OrderingInfo info_{};

        bool initialized_ = false;
        ScoredMove moves_[MaxMoves] = {};
        ::std::size_t count_ = 0;
        ::std::size_t index_ = 0;
    };

} // namespace nebula::engine
