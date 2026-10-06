#pragma once
#include "StatewithNNUE.h"
#include "nn/nnue_nn.h"

namespace nnue{
    namespace eval{
        template <nshogi::core::Color C>
        inline int16_t eval(StatewithNNUE& statewithNNUE){
            return (nnue::NN::calNN<C>(statewithNNUE.getAcc()) >> 4);
        }
    }
}