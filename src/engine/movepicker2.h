// movepicker.h
#pragma once

#include "core/movegenerator.h"
#include "core/state.h"
#include "core/types.h"
#include "../nshogi/src/core/internal/stateadapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#if __has_include("engine/TT.h")
#include "engine/TT.h"
#endif

namespace engine
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

    // 駒取りがGoodかBadかを判定する関数
    inline bool isGoodCapture(const ::nshogi::core::State& state, ::nshogi::core::Move32 move) {
        using namespace nshogi::core;
        Square to = move.to();
        Color opponent = ~state.getSideToMove();

        // 追加したラッパー関数でタダ取り判定（1-ply check）
        if (!state.isAttacked(opponent, to)) {
            return true; // 相手の紐がない完全なタダ取り！
        }

        // --- 相手の紐がある場合は、価値差 (MVV-LVA) で判定 ---
        PieceTypeKind attacker_type = move.pieceType();
        PieceTypeKind victim_type = move.capturePieceType();

        int value_diff = pieceValue(victim_type) - pieceValue(attacker_type);

        // 同価値交換（銀で銀を取るなど）を許容するためのマージン（-200）
        return value_diff >= -200; 
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

    template <bool isQsearch>
    class MovePicker2
    {
    public:
        MovePicker2(const ::nshogi::core::State &state, const OrderingInfo &info)
            : state_(state), info_(info), C_(state.getSideToMove())
        {
            is_in_check_ = state_.isInCheck();
        }

        MovePicker2(const ::nshogi::core::State &state, ::nshogi::core::Move32 hashMove)
            : state_(state), C_(state.getSideToMove())
        {
            info_.hashMove = hashMove;
            is_in_check_ = state_.isInCheck();
        }

        ::nshogi::core::Move32 next()
        {
            while (stage_ != Stage::Done)
            {
                switch (stage_)
                {
                case Stage::TTMove:
                {
                    stage_ = Stage::GenerateCaptures;
                    if (!info_.hashMove.isNone())
                    {
                        if constexpr (isQsearch)
                        {
                            if (isTactical_(info_.hashMove))
                                return info_.hashMove;
                        }
                        else
                        {
                            return info_.hashMove;
                        }
                    }
                    break;
                }

                case Stage::GenerateCaptures:
                {
                    generateCaptures_();
                    stage_ = Stage::YieldCaptures;
                    break;
                }

                case Stage::YieldCaptures:
                {
                    const auto mv = pickHighestScoreCapture_();
                    if(!mv.isNone()){
                        if(captures_[cap_idx_ - 1].score < 0){
                            cap_idx_--; // これ以降はbadなので配列に戻す。

                            if constexpr(isQsearch){
                                if(is_in_check_){
                                    stage_ = Stage::Killers;
                                } else {
                                    stage_ = Stage::Done;
                                }
                            } else {
                                stage_ = Stage::Killers;
                            }
                            break;
                        }
                    
                        if (!info_.hashMove.isNone() && mv == info_.hashMove)
                            continue;
                        return mv;
                    }

                    if constexpr (isQsearch) {
                        if (is_in_check_) {
                            stage_ = Stage::Killers;
                        } else {
                            stage_ = Stage::Done;
                        }
                    } else {
                        stage_ = Stage::Killers;
                    }
                    
                    break;
                }

                case Stage::Killers:
                {
                    stage_ = Stage::GenerateQuiets;
                    if(is_in_check_) break;
                    // !isCapture_(info_.killer1) は isLegalMoveで判定済みなので消去
                    if (!info_.killer1.isNone() && info_.killer1 != info_.hashMove && 
                        state_.isLegalMove(C_, info_.killer1))
                        return info_.killer1;
                    if (!info_.killer2.isNone() && info_.killer2 != info_.hashMove && 
                        info_.killer2 != info_.killer1 && state_.isLegalMove(C_, info_.killer2))
                        return info_.killer2;
                    break;
                }

                case Stage::GenerateQuiets:
                {
                    generateQuiets_();
                    stage_ = Stage::YieldQuiets;
                    break;
                }

                case Stage::YieldQuiets:
                {
                    const auto mv = pickNextQuiet_();
                    if (!mv.isNone())
                    {
                        if (!info_.hashMove.isNone() && mv == info_.hashMove)
                            continue;
                        if (!info_.killer1.isNone() && mv == info_.killer1)
                            continue;
                        if (!info_.killer2.isNone() && mv == info_.killer2)
                            continue;
                        return mv;
                    }
                    stage_ = Stage::YieldBadCaptures;
                    break;
                }

                case Stage::YieldBadCaptures:
                {
                    const auto mv = pickNextBadCapture_();
                    if(!mv.isNone())
                    {
                        if (!info_.hashMove.isNone() && mv == info_.hashMove)
                            continue;
                        return mv;
                    }
                    stage_ = Stage::Done;
                    break;
                }

                case Stage::Done:
                    break;
                }
            }

            return ::nshogi::core::Move32::MoveNone();
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

        static constexpr bool isTactical_(const ::nshogi::core::Move32 &mv) noexcept
        {
            return isCapture_(mv) || mv.promote();
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

            if (isGoodCapture(state_, mv)) {
                score += 1000000; // Goodは絶対優先
            } else {
                score -= 1000000; // Badは後回し（ペナルティ）
            }

            return score;
        }

        ::nshogi::core::Move32 pickHighestScoreCapture_() noexcept
        {
            if (cap_idx_ >= cap_count_)
                return ::nshogi::core::Move32::MoveNone();

            ::std::size_t bestIndex = cap_idx_;
            int bestScore = captures_[cap_idx_].score;
            for (::std::size_t i = cap_idx_ + 1; i < cap_count_; ++i)
            {
                const int s = captures_[i].score;
                if (s > bestScore)
                {
                    bestScore = s;
                    bestIndex = i;
                }
            }

            if (bestIndex != cap_idx_)
                ::std::swap(captures_[cap_idx_], captures_[bestIndex]);

            return captures_[cap_idx_++].move;
        }

        ::nshogi::core::Move32 pickNextQuiet_() noexcept {
            if(quiet_idx_ >= quiet_count_)
                return ::nshogi::core::Move32::MoveNone();

            return quiets_[quiet_idx_++].move;
        }

        ::nshogi::core::Move32 pickNextBadCapture_() noexcept {
            if(cap_idx_ >= cap_count_)
                return ::nshogi::core::Move32::MoveNone();

            return captures_[cap_idx_++].move;
        }

        void generateCaptures_()
        {
            cap_count_ = 0;
            cap_idx_ = 0;

            // 通常探索ではまず駒取りだけを高速に列挙（成りで駒を取らない手は quiet 側で拾う）。
            const auto captureMoves = ::nshogi::core::MoveGenerator::generateLegalCaptureMoves(state_);
            for (const auto mv : captureMoves)
            {
                if (cap_count_ >= MaxMoves)
                    break;
                captures_[cap_count_++] = ScoredMove{mv, 500'000 + scoreCapture_(mv) + highPromotionBonus(mv)};
            }
        }

        void generateQuiets_()
        {
            quiet_count_ = 0;
            quiet_idx_ = 0;

            const auto legalMoves = ::nshogi::core::MoveGenerator::generateLegalMoves(state_);
            for (const auto mv : legalMoves)
            {
                if (quiet_count_ >= MaxMoves)
                    break;

                // 駒取りは capture フェーズで扱う。駒を取らない成りは quiet として残す。
                if (isCapture_(mv))
                    continue;

                if (!info_.hashMove.isNone() && mv == info_.hashMove)
                    continue;
                if (!info_.killer1.isNone() && mv == info_.killer1)
                    continue;
                if (!info_.killer2.isNone() && mv == info_.killer2)
                    continue;

                quiets_[quiet_count_++] = ScoredMove{mv, 0};
            }
        }

        enum class Stage
        {
            TTMove,
            GenerateCaptures,
            YieldCaptures,
            Killers,
            GenerateQuiets,
            YieldQuiets,
            YieldBadCaptures,
            Done
        };

    private:
        const ::nshogi::core::State &state_;
        OrderingInfo info_{};
        bool is_in_check_ = false;
        const::nshogi::core::Color C_;

        Stage stage_ = Stage::TTMove;

        // 駒取り用の配列
        ScoredMove captures_[MaxMoves] = {};
        ::std::size_t cap_count_ = 0;
        ::std::size_t cap_idx_ = 0;

        // Quiet（駒を取らない手）用の配列
        ScoredMove quiets_[MaxMoves] = {};
        ::std::size_t quiet_count_ = 0;
        ::std::size_t quiet_idx_ = 0;
    };

} // namespace engine