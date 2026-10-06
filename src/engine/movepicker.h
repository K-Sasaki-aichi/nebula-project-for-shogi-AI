// // movepicker.h
// #pragma once

// #include "core/movegenerator.h"
// #include "core/state.h"
// #include "core/types.h"

// #include <array>
// #include <cstddef>
// #include <cstdint>
// #include <optional>
// #include <utility>

// #if __has_include("engine/TT.h")
// #include "engine/TT.h"
// #endif

// namespace engine
// {
//     inline constexpr ::std::array<int, ::nshogi::core::NumPieceType> pieceValueTable = {
//         0,    // Empty
//         100,  // Pawn
//         300,  // Lance
//         300,  // Knight
//         500,  // Silver
//         800,  // Bishop
//         1000, // Rook
//         600,  // Gold
//         0,    // King
//         600,  // ProPawn
//         600,  // ProLance
//         600,  // ProKnight
//         600,  // ProSilver
//         900,  // ProBishop
//         1100  // ProRook
//     };

//     inline constexpr int pieceValue(::nshogi::core::PieceTypeKind pt) noexcept
//     {
//         return pieceValueTable[static_cast<::std::size_t>(pt)];
//     }

//     inline constexpr int highPromotionBonus(const ::nshogi::core::Move32 &) noexcept
//     {
//         return 0;
//     }

//     struct OrderingInfo
//     {
//         ::nshogi::core::Move32 hashMove = ::nshogi::core::Move32::MoveNone();
//         ::nshogi::core::Move32 killer1 = ::nshogi::core::Move32::MoveNone();
//         ::nshogi::core::Move32 killer2 = ::nshogi::core::Move32::MoveNone();
//     };

// #if __has_include("engine/TT.h")
//     inline ::nshogi::core::Move32 hash_move(const ::nshogi::core::State &state,
//                                             ::engine::TranspositionTable &tt) noexcept
//     {
//         const uint64_t hash = state.getHash();
//         const ::TTEntry *entry = tt.probe(hash);
//         if (!tt.isHit(entry, hash))
//             return ::nshogi::core::Move32::MoveNone();

//         return entry->move;
//     }

//     inline OrderingInfo makeOrderingInfoFromTT(const ::nshogi::core::State &state,
//                                                ::engine::TranspositionTable &tt) noexcept
//     {
//         OrderingInfo info{};
//         info.hashMove = hash_move(state, tt);
//         return info;
//     }

//     inline ::nshogi::core::Move32 hash_move(const ::nshogi::core::State &state) noexcept
//     {
//         return hash_move(state, ::engine::TT);
//     }

//     inline OrderingInfo makeOrderingInfoFromTT(const ::nshogi::core::State &state) noexcept
//     {
//         return makeOrderingInfoFromTT(state, ::engine::TT);
//     }
// #endif

//     template <bool isQsearch>
//     class MovePicker
//     {
//     public:
//         MovePicker(const ::nshogi::core::State &state, const OrderingInfo &info)
//             : state_(state), info_(info)
//         {
//         }

//         MovePicker(const ::nshogi::core::State &state, ::nshogi::core::Move32 hashMove)
//             : state_(state)
//         {
//             info_.hashMove = hashMove;
//         }

//         ::nshogi::core::Move32 next()
//         {
//             while (stage_ != Stage::Done)
//             {
//                 switch (stage_)
//                 {
//                 case Stage::TTMove:
//                     stage_ = Stage::GenerateCaptures;
//                     if (!info_.hashMove.isNone())
//                     {
//                         if constexpr (isQsearch)
//                         {
//                             if (isTactical_(info_.hashMove))
//                                 return info_.hashMove;
//                         }
//                         else
//                         {
//                             return info_.hashMove;
//                         }
//                     }
//                     break;

//                 case Stage::GenerateCaptures:
//                     generateCaptures_();
//                     stage_ = Stage::YieldCaptures;
//                     break;

//                 case Stage::YieldCaptures:
//                 {
//                     const auto mv = pickHighestScoreMove_();
//                     if (!mv.isNone())
//                     {
//                         if (!info_.hashMove.isNone() && mv == info_.hashMove)
//                             continue;
//                         return mv;
//                     }

//                     if constexpr (isQsearch)
//                     {
//                         stage_ = Stage::Done;
//                     }
//                     else
//                     {
//                         stage_ = Stage::Killers;
//                     }
//                     break;
//                 }

//                 case Stage::Killers:
//                     stage_ = Stage::GenerateQuiets;
//                     if (!info_.killer1.isNone() && info_.killer1 != info_.hashMove && !isCapture_(info_.killer1))
//                         return info_.killer1;
//                     if (!info_.killer2.isNone() && info_.killer2 != info_.hashMove && info_.killer2 != info_.killer1 && !isCapture_(info_.killer2))
//                         return info_.killer2;
//                     break;

//                 case Stage::GenerateQuiets:
//                     generateQuiets_();
//                     stage_ = Stage::YieldQuiets;
//                     break;

//                 case Stage::YieldQuiets:
//                 {
//                     const auto mv = pickHighestScoreMove_();
//                     if (!mv.isNone())
//                     {
//                         if (!info_.hashMove.isNone() && mv == info_.hashMove)
//                             continue;
//                         if (!info_.killer1.isNone() && mv == info_.killer1)
//                             continue;
//                         if (!info_.killer2.isNone() && mv == info_.killer2)
//                             continue;
//                         return mv;
//                     }
//                     stage_ = Stage::Done;
//                     break;
//                 }

//                 case Stage::Done:
//                     break;
//                 }
//             }

//             return ::nshogi::core::Move32::MoveNone();
//         }

//     private:
//         struct ScoredMove
//         {
//             ::nshogi::core::Move32 move;
//             int score;
//         };

//         static constexpr ::std::size_t MaxMoves = 600;

//         static constexpr bool isCapture_(const ::nshogi::core::Move32 &mv) noexcept
//         {
//             using namespace ::nshogi::core;
//             return mv.capturePieceType() != PTK_Empty;
//         }

//         static constexpr bool isTactical_(const ::nshogi::core::Move32 &mv) noexcept
//         {
//             return isCapture_(mv) || mv.promote();
//         }

//         int scoreCapture_(const ::nshogi::core::Move32 &mv) const noexcept
//         {
//             using namespace ::nshogi::core;

//             const auto cap = mv.capturePieceType();
//             if (cap == PTK_Empty)
//                 return 0;

//             const int victim = pieceValue(cap);
//             const int attacker = pieceValue(mv.pieceType());
//             int score = (victim * 10 - attacker);

//             if (victim < attacker)
//             {
//                 constexpr int BadCapturePenalty = 100'000;
//                 score -= BadCapturePenalty;
//             }

//             return score;
//         }

//         ::nshogi::core::Move32 pickHighestScoreMove_() noexcept
//         {
//             if (index_ >= count_)
//                 return ::nshogi::core::Move32::MoveNone();

//             ::std::size_t bestIndex = index_;
//             int bestScore = moves_[index_].score;
//             for (::std::size_t i = index_ + 1; i < count_; ++i)
//             {
//                 const int s = moves_[i].score;
//                 if (s > bestScore)
//                 {
//                     bestScore = s;
//                     bestIndex = i;
//                 }
//             }

//             if (bestIndex != index_)
//                 ::std::swap(moves_[index_], moves_[bestIndex]);

//             return moves_[index_++].move;
//         }

//         void generateCaptures_()
//         {
//             count_ = 0;
//             index_ = 0;

//             if constexpr (isQsearch)
//             {
//                 // qsearchでは「駒取り + 駒を取らない成り」も入れないと手こぼれし得る。
//                 const auto legalMoves = ::nshogi::core::MoveGenerator::generateLegalMoves(state_);
//                 for (const auto mv : legalMoves)
//                 {
//                     if (count_ >= MaxMoves)
//                         break;
//                     if (!isTactical_(mv))
//                         continue;
//                     moves_[count_++] = ScoredMove{mv, 500'000 + scoreCapture_(mv) + highPromotionBonus(mv)};
//                 }
//             }
//             else
//             {
//                 // 通常探索ではまず駒取りだけを高速に列挙（成りで駒を取らない手は quiet 側で拾う）。
//                 const auto captureMoves = ::nshogi::core::MoveGenerator::generateLegalCaptureMoves(state_);
//                 for (const auto mv : captureMoves)
//                 {
//                     if (count_ >= MaxMoves)
//                         break;
//                     moves_[count_++] = ScoredMove{mv, 500'000 + scoreCapture_(mv) + highPromotionBonus(mv)};
//                 }
//             }
//         }

//         void generateQuiets_()
//         {
//             count_ = 0;
//             index_ = 0;

//             const auto legalMoves = ::nshogi::core::MoveGenerator::generateLegalMoves(state_);
//             for (const auto mv : legalMoves)
//             {
//                 if (count_ >= MaxMoves)
//                     break;

//                 // 駒取りは capture フェーズで扱う。駒を取らない成りは quiet として残す。
//                 if (isCapture_(mv))
//                     continue;

//                 if (!info_.hashMove.isNone() && mv == info_.hashMove)
//                     continue;
//                 if (!info_.killer1.isNone() && mv == info_.killer1)
//                     continue;
//                 if (!info_.killer2.isNone() && mv == info_.killer2)
//                     continue;

//                 moves_[count_++] = ScoredMove{mv, 0};
//             }
//         }

//         enum class Stage
//         {
//             TTMove,
//             GenerateCaptures,
//             YieldCaptures,
//             Killers,
//             GenerateQuiets,
//             YieldQuiets,
//             Done
//         };

//     private:
//         const ::nshogi::core::State &state_;
//         OrderingInfo info_{};

//         Stage stage_ = Stage::TTMove;
//         ScoredMove moves_[MaxMoves] = {};
//         ::std::size_t count_ = 0;
//         ::std::size_t index_ = 0;
//     };

// } // namespace nebula::engine
