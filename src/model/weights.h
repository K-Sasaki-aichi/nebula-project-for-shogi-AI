#pragma once
#include <cstdint>

namespace weight {
    constexpr int ALIGN = 32;

    //各層のノード数
    constexpr int NumFeatures = 125388;
    constexpr int NumAcc = 256;
    constexpr int Numlayer = 32;

    struct alignas(ALIGN) W_input {
        int16_t bias[NumAcc];
        int16_t weight[NumFeatures][NumAcc];
    };
    struct alignas(ALIGN) W_Acc {
        // 先手後手のアキュムレータがあるため2倍する.
        int32_t bias[Numlayer];
        int8_t weight[2*NumAcc][Numlayer];
    };

    struct alignas(ALIGN) W_layert {
        int32_t bias[Numlayer];
        int8_t weight[Numlayer][Numlayer];
    };

    struct alignas(ALIGN) W_output {
        int32_t bias;
        int8_t weight[Numlayer][1];
    };

    extern W_input w_input;
    extern W_Acc w_acc;
    extern W_layert w_layer;
    extern W_output w_output;

    bool load();
}