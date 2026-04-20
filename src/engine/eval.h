#pragma once
#include "StatewithNNUE.h"
#include "nn/nnue_nn.h"

namespace nnue{
    template <nshogi::core::Color C>
    inline int32_t eval(StatewithNNUE& statewithNNUE){
        return nnue::NN::calNN(statewithNNUE.getAcc());
    }
}