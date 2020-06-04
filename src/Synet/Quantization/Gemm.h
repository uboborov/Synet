/*
* Synet Framework (http://github.com/ermig1979/Synet).
*
* Copyright (c) 2018-2019 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "Synet/Common.h"

namespace Synet
{
    inline void CpuGemm8iNN(size_t S, size_t D, size_t K, size_t C, const uint8_t* src, size_t lda, const int8_t* weight, size_t ldb, int32_t* dst, size_t ldc, int neg)
    {
#ifdef SYNET_INT8_INT8_DISABLE 
        const size_t C2 = C / 2 * 2;
#else
        const size_t C2 = neg ? 0 : C / 2 * 2;
#endif
        for (size_t i = 0; i < S; ++i)
        {
            for (size_t j = 0; j < D; ++j)
                dst[i * ldc + j] = 0;
            for (size_t k = 0, o = 0; k < K; k++)
            {
                size_t c = 0;
                for (; c < C2; c += 2, o += 2)
                {
                    int32_t s0 = src[i * lda + o + 0];
                    int32_t s1 = src[i * lda + o + 1];
                    const int8_t* w0 = weight + (o + 0) * ldb;
                    const int8_t* w1 = weight + (o + 1) * ldb;
                    int32_t* d = dst + i * ldc;
                    for (size_t j = 0; j < D; ++j)
                    {
                        int sum = s0 * w0[j] + s1 * w1[j];
#if defined(SYNET_INT8_INT16_OWERFLOW)
                        sum = std::min(std::max(SHRT_MIN, sum), SHRT_MAX);
#endif
                        d[j] += sum;
                    }
                }
                for (; c < C; ++c, ++o)
                {
                    int32_t s0 = src[i * lda + o];
                    const int8_t* w0 = weight + o * ldb;
                    int32_t* d = dst + i * ldc;
                    if (neg)
                    {
                        for (size_t j = 0; j < D; ++j)
                        {
                            int _w0 = w0[j];
#ifdef SYNET_INT8_INT8_DISABLE
                            int _s0 = int(s0);
#else
                            if (_w0 & 1)
                                _w0 = Round(_w0 * 0.25f) * 4;
                            int _s0 = int(s0) - 128;
#endif
                            int dp = _w0 * _s0;
                            d[j] += dp;
                        }
                    }
                    else
                    {
                        for (size_t j = 0; j < D; ++j)
                            d[j] += s0 * w0[j];
                    }
                }
            }
        }
    }

    inline void CpuGemm8iNN(size_t D, size_t S, size_t C, size_t K, const int8_t* weight, size_t lda, const uint8_t* src, size_t ldb, int32_t* dst, size_t ldc, int neg)
    {
#ifdef SYNET_INT8_INT8_DISABLE 
        const size_t C2 = C / 2 * 2;
#else
        const size_t C2 = neg ? 0 : C / 2 * 2;
#endif
        for (size_t i = 0; i < D; ++i)
        {
            for (size_t j = 0; j < S; ++j)
                dst[i * ldc + j] = 0;
            size_t c = 0;
            for (; c < C2; c += 2)
            {
                for (size_t k = 0; k < K; k++)
                {
                    int32_t w0 = weight[i * lda + (c + 0) * K + k];
                    int32_t w1 = weight[i * lda + (c + 1) * K + k];
                    const uint8_t* s0 = src + ((c + 0) * K + k) * ldb;
                    const uint8_t* s1 = src + ((c + 1) * K + k) * ldb;
                    int32_t* d = dst + i * ldc;
                    for (size_t j = 0; j < S; ++j)
                    {
                        int sum = s0[j] * w0 + s1[j] * w1;
#if defined(SYNET_INT8_INT16_OWERFLOW)
                        sum = std::min(std::max(SHRT_MIN, sum), SHRT_MAX);
#endif
                        d[j] += sum;
                    }
                }
            }
            for (; c < C; ++c)
            {
                for (size_t k = 0; k < K; k++)
                {
                    int32_t w0 = weight[i * lda + (c + 0) * K + k];
                    const uint8_t* s0 = src + ((c + 0) * K + k) * ldb;
                    int32_t* d = dst + i * ldc;
                    if (neg)
                    {
                        for (size_t j = 0; j < S; ++j)
                        {
                            int _w0 = w0;
#ifdef SYNET_INT8_INT8_DISABLE
                            int _s0 = int(s0[j]);
#else
                            if (_w0 & 1)
                                _w0 = Round(_w0 * 0.25f) * 4;
                            int _s0 = int(s0[j]) - 128;
#endif
                            int dp = _w0 * _s0;
                            d[j] += dp;
                        }
                    }
                    else
                    {
                        for (size_t j = 0; j < S; ++j)
                            d[j] += s0[j] * w0;
                    }
                }
            }
        }
    }
}