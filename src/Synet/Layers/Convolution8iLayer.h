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

#include "Synet/Layers/ConvolutionLayer.h"
#include "Synet/Quantization/Gemm.h"
#include "Synet/Quantization/Convert.h"

namespace Synet
{
    template <class T> class Convolution8iLayer : public Synet::ConvolutionLayer<T>
    {
    public:
        typedef T Type;
        typedef Layer<T> Base;
        typedef typename Base::Tensor Tensor;
        typedef std::vector<Tensor> Tensors;
        typedef typename Base::TensorPtr TensorPtr;
        typedef typename Base::TensorPtrs TensorPtrs;

        Convolution8iLayer(const LayerParam & param)
            : ConvolutionLayer<T>(param)
        {
            const ConvolutionParam & p = this->Param().convolution();
            assert(p.quantizationLevel() == TensorType8i);
            _src8u = false;
            _dst8u = false;
        }

        virtual size_t MemoryUsage() const
        {
            return Base::MemoryUsage() + _convolution8i.InternalBufferSize() + _weight8i.Size();
        }

        virtual bool Can8i() const
        {
            return true;
        }

        virtual bool Is8i() const
        {
            return true;
        }

        virtual void DebugPrint(std::ostream& os, int flag, int first, int last, int precision)
        {
            const Stat& statS = *this->Stats(0)[0];
            Synet::DebugPrint(os, statS.scale8uTo32f, "pSrcScaleInv", first, last, precision);
            Synet::DebugPrint(os, statS.scale32fTo8u, "pSrcScale", first, last, precision);
            Synet::DebugPrint(os, statS.shift32fTo8u, "pSrcShift", first, last, precision);
            const Stat& statD = *this->Stats(2)[0];
            Synet::DebugPrint(os, statD.scale8uTo32f, "pDstScale", first, last, precision);
            Synet::DebugPrint(os, statD.shift8uTo32f, "pDstShift", first, last, precision);
            _weight8i.DebugPrint(os, "_weight8i", true, first, last, precision);
            _norm32i.DebugPrint(os, "_norm32i", false, first, last, precision);
            Synet::DebugPrint(os, _dstCvt.scale, _dstCvt.channels, "_dstCvt.scale", first, last, precision);
            Synet::DebugPrint(os, _dstCvt.shift, _dstCvt.channels, "_dstCvt.shift", first, last, precision);

            Synet::DebugPrint(_weight8i.CpuData(), Shape({ _weight8i.Size() }), "sy_weight8i");
            Synet::DebugPrint(_norm32i.CpuData(Shape{ 1u, 0u }), Shape({ _norm32i.Size()/2 }), "sy_norm32i");
        }

    protected:
        typedef typename ConvolutionLayer<T>::AlgParam AlgParam;

        virtual void Reshape(const TensorPtr& src, const TensorPtrs& buf, const TensorPtr& dst)
        {
            const Tensors& weight = this->Weight();
            const ConvParam& conv = this->_conv;
            AlgParam& alg = this->_alg;

            _src8u = src->GetType() == TensorType8u;
            _dst8u = dst->GetType() == TensorType8u;
            Shape shape = conv.DstShape(alg.batch);
            if (_dst8u)
                dst->As8u().Reshape(shape, src->Format());
            else
                dst->As32f().Reshape(shape, src->Format());
            _convolution8i.Init(alg.batch, &conv);
            if (_convolution8i.Enable())
            {
                buf[TensorType8u * BUFFER_COUNT]->As8u().Extend(Shp(_convolution8i.ExternalBufferSize()));
                const float* bias = alg.bias ? weight[1].CpuData() : NULL;
                const float* params = conv.activation == ActivationFunctionTypePrelu ? weight.back().CpuData() : alg.params;
                const float* stats[4] = {
                    this->Stats(0).empty() ? NULL : this->Stats(0)[0]->min.data(),
                    this->Stats(0).empty() ? NULL : this->Stats(0)[0]->max.data(),
                    this->Stats(2).empty() ? NULL : this->Stats(2)[0]->min.data(),
                    this->Stats(2).empty() ? NULL : this->Stats(2)[0]->max.data() };
                _convolution8i.SetParams(weight[0].CpuData(), bias, params, stats);
            }
            else
            {
                if (!_src8u)
                    buf[TensorType8u * BUFFER_COUNT + 1]->As8u().Extend(src->Shape());
                buf[TensorType8u * BUFFER_COUNT]->As8u().Extend(Shp(conv.kernelY * conv.kernelX * conv.srcC * conv.dstH * conv.dstW));
                buf[TensorType32i * BUFFER_COUNT]->As32i().Extend(shape, src->Format());
                Quantize();
            }
        }

        virtual void ForwardCpu(const TensorPtrs & src, const TensorPtrs & buf, const TensorPtrs & dst)
        {
            if (_convolution8i.Enable())
                _convolution8i.Forward(src[0]->RawCpuData(), buf[TensorType8u * BUFFER_COUNT]->RawCpuData(), dst[0]->RawCpuData());
            else
            {
                uint8_t* buf0 = buf[TensorType8u * BUFFER_COUNT]->As8u().CpuData();
                uint8_t* tmp = _src8u ? src[0]->As8u().CpuData() : buf[TensorType8u * BUFFER_COUNT + 1]->As8u().CpuData();
                int32_t* sum = buf[TensorType32i * BUFFER_COUNT]->As32i().CpuData();
                if (!_src8u)
                    Convert32fTo8u(src[0]->As32f().CpuData(), _srcCvt, tmp);
                ForwardCpu(tmp, buf0, sum);
                if (_dst8u)
                    Convert32iTo8u(sum, _dstCvt, dst[0]->As8u().CpuData());
                else
                    Convert32iTo32f(sum, _dstCvt, dst[0]->As32f().CpuData());
            }
        }

        void Quantize()
        {
            const ConvParam& conv = this->_conv;
            const AlgParam& alg = this->_alg;
            Stat & statS = *this->Stats(0)[0];
            Stat & statD = *this->Stats(2)[0];
            statS.Init8u();
            statD.Init8u();
            _negSrc = statS.negative;
            _weight8i.Reshape(this->Weight()[0].Shape(), alg.trans ? TensorFormatNhwc : TensorFormatNchw);
            _norm32i.Reshape(Shp(2, conv.dstC));
            _norm32f.Reshape(Shp(2, conv.dstC));
            if (!_src8u)
            {
                _srcCvt.batch = alg.batch;
                _srcCvt.channels = conv.srcC;
                _srcCvt.spatial = conv.srcH * conv.srcW;
                _srcCvt.format = (TensorFormat)alg.trans;
                _srcCvt.scale = statS.scale32fTo8u.data();
                _srcCvt.shift = statS.shift32fTo8u.data();
            }
            size_t G = conv.group, D = conv.dstC / G, C = conv.srcC / G, K = conv.kernelY*conv.kernelX, CK = C * K, GD = G*D;
            Floats normW(CK);
            const float * pSrcW = this->Weight()[0].CpuData();
            const float * pSrcB = alg.bias ? this->Weight()[1].CpuData() : NULL;
            const float * pSrcScaleInv = statS.scale8uTo32f.data();
            const float * pSrcScale = statS.scale32fTo8u.data();
            const float * pSrcShift = statS.shift32fTo8u.data();
            const float * pDstScale = statD.scale8uTo32f.data();
            const float * pDstScaleInv = statD.scale32fTo8u.data();
            const float * pDstShift = statD.shift8uTo32f.data();
            float * pNormW = normW.data();
            int8_t * pDstW = _weight8i.CpuData();
            int32_t * pDstS = _norm32i.CpuData();
            int32_t * pDstB = pDstS + conv.dstC;
            float * pNormScale = _norm32f.CpuData();
            float * pNormShift = pNormScale + conv.dstC;
            _dstCvt.batch = alg.batch;
            _dstCvt.channels = conv.dstC;
            _dstCvt.spatial = conv.dstH * conv.dstW;
            _dstCvt.format = (TensorFormat)alg.trans;
            _dstCvt.scale = pNormScale;
            _dstCvt.shift = pNormShift;
            for (size_t g = 0; g < G; ++g)
            {
                for (size_t d = 0; d < D; ++d)
                {
                    float normB = 0, minW = FLT_MAX, maxW = -FLT_MAX, scale = 1.0f;
                    if (alg.trans)
                    {
                        for (size_t k = 0, kc = 0; k < K; ++k)
                            for (size_t c = 0; c < C; ++c, ++kc)
                            {
#ifdef SYNET_INT8_INPUT_ROUND_BUGFIX
                                pNormW[kc] = pSrcW[kc * GD + d] * pSrcScaleInv[c];
#else
                                pNormW[kc] = pSrcW[kc * GD + d] / pSrcScale[c];
#endif
                                minW = std::min(minW, pNormW[kc]);
                                maxW = std::max(maxW, pNormW[kc]);
                            }
                        float abs = std::max(::abs(maxW), ::abs(minW));
                        if (pSrcB)
                            abs = std::max(abs, ::abs(pSrcB[d]) / float(128 * 256 * 256));
                        scale = 127.0f / abs;
                        for (size_t k = 0, kc = 0; k < K; ++k)
                            for (size_t c = 0; c < C; ++c, ++kc)
                                if (_negSrc)
                                {
#ifdef SYNET_INT8_INT8_DISABLE
                                    int w = Convert32fTo8iSym(pNormW[kc], scale);
                                    if (w & 1)
                                        w = Round(w*0.25f) * 4;
                                    pDstW[kc*GD + d] = w / 2;
                                    normB -= w * pSrcShift[c];
#else
                                    pDstW[kc*GD + d] = Convert32fTo8iSym(pNormW[kc], scale);
#endif
                                }
                                else
                                {
                                    pDstW[kc*GD + d] = Convert32fTo8iSym(pNormW[kc], scale);
                                    normB -= pDstW[kc*GD + d] * pSrcShift[c];
                                }
                    }
                    else
                    {
                        for (size_t c = 0, ck = 0; c < C; ++c)
                            for (size_t k = 0; k < K; ++k, ++ck)
                            {
#ifdef SYNET_INT8_INPUT_ROUND_BUGFIX
                                pNormW[ck] = pSrcW[d * CK + ck] * pSrcScaleInv[c];
#else
                                pNormW[ck] = pSrcW[d * CK + ck] / pSrcScale[c];
#endif
                                minW = std::min(minW, pNormW[ck]);
                                maxW = std::max(maxW, pNormW[ck]);
                            }
                        float abs = std::max(::abs(maxW), ::abs(minW));
                        if (pSrcB)
                            abs = std::max(abs, ::abs(pSrcB[d]) / float(128 * 256 * 256));
                        scale = 127.0f / abs;
                        for (size_t c = 0, ck = 0; c < C; ++c)
                            for (size_t k = 0; k < K; ++k, ++ck)
                                if (_negSrc)
                                {
#ifdef SYNET_INT8_INT8_DISABLE
                                    int w = Convert32fTo8iSym(pNormW[ck], scale);
                                    if (w & 1)
                                        w = Round(w*0.25f) * 4;
                                    pDstW[d*CK + ck] = w / 2;
                                    normB -= w * pSrcShift[c];
#else
                                    pDstW[d*CK + ck] = Convert32fTo8iSym(pNormW[ck], scale);
#endif
                                }
                                else
                                {
                                    pDstW[d*CK + ck] = Convert32fTo8iSym(pNormW[ck], scale);
                                    normB -= pDstW[d*CK + ck] * pSrcShift[c];
                                }
                    }
#ifdef SYNET_INT8_INT8_DISABLE
                    pDstS[d] = _negSrc ? 2 : 1;
#else
                    pDstS[d] = 1;
#endif
                    if (pSrcB)
                        normB += pSrcB[d] * scale;
                    pDstB[d] = Synet::Quantize(normB);
                    if (_dst8u)
                    {
                        pNormScale[d] = (1.0f / scale) * pDstScaleInv[d];
                        pNormShift[d] = -pDstShift[d] / pDstScale[d];
                    }
                    else
                    {
                        pNormScale[d] = 1.0f / scale;
                        pNormShift[d] = 0;
                    }
                }
                if (alg.trans)
                {
                    pSrcW += D;
                    pDstW += D;
                }
                else
                {
                    pSrcW += CK*D;
                    pDstW += CK*D;
                }
                if (pSrcB)
                    pSrcB += D;
                pDstB += D;
                pDstS += D;
                pSrcScale += C;
                pSrcScaleInv += C;
                pSrcShift += C;
                pDstScale += D;
                pDstScaleInv += D;
                pDstShift += D;
                pNormScale += D;
                pNormShift += D;           
            }
        }

        void ForwardCpu(const uint8_t * src, uint8_t * buf, int32_t * dst)
        {
            const ConvParam& conv = this->_conv;
            const AlgParam& alg = this->_alg;
            const uint8_t * zero = this->Stats(0)[0]->zero8u.data();
            const int8_t * weight = _weight8i.CpuData();
            const int32_t * scale = _norm32i.CpuData();
            const int32_t * shift = scale + conv.dstC;
            for (size_t b = 0; b < alg.batch; ++b)
            {
                const uint8_t * tmp = src;
                if (!alg.is1x1)
                {
                    if (alg.trans)
                        Synet::ImgToRow(tmp, conv.srcH, conv.srcW, conv.srcC, conv.kernelY, conv.kernelX,
                            conv.padY, conv.padX, conv.padH, conv.padW, conv.strideY, conv.strideX, conv.dilationY, conv.dilationX, conv.group, zero, buf);
                    else
                        Synet::ImgToCol(tmp, conv.srcC, conv.srcH, conv.srcW, conv.kernelY, conv.kernelX,
                            conv.padY, conv.padX, conv.padH, conv.padW, conv.strideY, conv.strideX, conv.dilationY, conv.dilationX, zero, buf);
                    tmp = buf;
                }
                if (alg.trans)
                {
                    assert(conv.group == 1 || conv.group == conv.srcC);
                    if(conv.group == 1)
                        Synet::CpuGemm8iNN(alg.siS, alg.siD, conv.kernelY*conv.kernelX, conv.srcC, tmp, alg.ldS, weight, alg.ldW, dst, alg.ldD, _negSrc);
                    else
                        for (size_t g = 0; g < conv.group; ++g)
                            Synet::CpuGemmNN(alg.siS, alg.siD, alg.siW, tmp + alg.grS * g, alg.ldS, weight + alg.grW * g, alg.ldW, dst + alg.grD * g, alg.ldD);
                }
                else
                {
                    if (conv.group == 1)
                        Synet::CpuGemm8iNN(alg.siD, alg.siS, conv.srcC, conv.kernelY*conv.kernelX, weight, alg.ldW, tmp, alg.ldS, dst, alg.ldD, _negSrc);
                    else
                        for (size_t g = 0; g < conv.group; ++g)
                            Synet::CpuGemmNN(alg.siD, alg.siS, alg.siW, weight + alg.grW * g, alg.ldW, tmp + alg.grS * g, alg.ldS, dst + alg.grD * g, alg.ldD);
                }
                Detail::ScaleLayerForwardCpu(dst, scale, shift, conv.dstC, conv.dstH, conv.dstW, dst, alg.trans, 1);

                switch (conv.activation)
                {
                case ActivationFunctionTypeIdentity:
                    break;
                case ActivationFunctionTypeRelu:
                    CpuRelu(dst, alg.dSize, 0, dst);
                    break;
                default:
                    assert(0);
                }
                src += alg.sSize;
                dst += alg.dSize;
            }
        }

    private:
        bool _is8i, _src8u, _dst8u, _negSrc;
        ConvertParam _srcCvt, _dstCvt;

        Convolution8i _convolution8i;

        Tensor8i _weight8i;
        Tensor32i _norm32i;
        Tensor32f _norm32f;
    };
}