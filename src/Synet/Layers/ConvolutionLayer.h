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
#include "Synet/Layer.h"
#include "Synet/Utils/Gemm.h"
#include "Synet/Utils/ImgToCol.h"
#include "Synet/Utils/Winograd.h"
#include "Synet/Utils/Convolution.h"
#include "Synet/Layers/PreluLayer.h"

namespace Synet
{
    template <class T> class ConvolutionLayer : public Synet::Layer<T>
    {
    public:
        typedef T Type;
        typedef Layer<T> Base;
        typedef typename Base::Tensor Tensor;
        typedef std::vector<Tensor> Tensors;
        typedef typename Base::TensorPtrs TensorPtrs;

        ConvolutionLayer(const LayerParam & param)
            : Base(param)
        {
            const ConvolutionParam & p = this->Param().convolution();
            _is8i = p.quantizationLevel() == TensorType8i;
            _hasPad = false;
            for (size_t i = 0; i < p.pad().size(); ++i)
                if (p.pad()[i])
                    _hasPad = true;
            _internal = 0;
        }

        virtual size_t MemoryUsage() const
        {
            return Base::MemoryUsage() + _convolution.InternalBufferSize() * sizeof(Type);
        }

        virtual void CompactWeight()
        {
            if (_internal)
                ((Tensor&)this->Weight()[0]).Clear();
        }

        virtual bool Can8i() const
        {
            return _is8i;
        }

        virtual bool Is8i() const
        {
            return _is8i;
        }

        virtual bool HasPad() const
        {
            return _hasPad;
        }

        virtual void Reshape(const TensorPtrs & src, const TensorPtrs & buf, const TensorPtrs & dst)
        {
            assert(src.size() == 1);

            const ConvolutionParam & param = this->Param().convolution();
            const Tensors & weight = this->Weight();

            _conv.Set(param);
            _conv.Set(*src[0]);

            _is1x1 = _conv.Is1x1();
            _biasTerm = param.biasTerm();
            if (_biasTerm)
                assert(weight[1].Size() == _conv.dstC);

            _params[0] = param.activationParam0();
            _params[1] = param.activationParam1();

            assert(weight.size() == 1 + _biasTerm + (_conv.activation == ActivationFunctionTypePrelu));
            if (_conv.activation == ActivationFunctionTypePrelu)
            {
                if (weight.back().Size() == 1)
                {
                    _conv.activation = ActivationFunctionTypeLeakyRelu;
                    _params[0] = weight.back().CpuData()[0];
                }
                else
                    assert(weight.back().Size() == _conv.dstC);
            }

            _axis = param.axis();
            assert(src[0]->Count() == _axis + 3);

            _num = src[0]->Size(0, _axis);
            _trans = src[0]->Format() == TensorFormatNhwc;
            assert(weight[0].Shape() == _conv.WeightShape(_trans != 0) && weight[0].Format() == src[0]->Format());

            Shape dstShape(src[0]->Shape().begin(), src[0]->Shape().begin() + _axis);
            if (_trans)
            {
                dstShape.push_back(_conv.dstH);
                dstShape.push_back(_conv.dstW);
                dstShape.push_back(_conv.dstC);

                _siW = _conv.srcC * _conv.kernelY * _conv.kernelX / _conv.group;
                _ldW = _conv.dstC;
                _grW = _conv.dstC / _conv.group;

                _siS = _conv.dstH * _conv.dstW;
                _ldS = _siW;
                _grS = _siS * _siW;

                _siD = _conv.dstC / _conv.group;
                _ldD = _conv.dstC;
                _grD = _siD;
            }
            else
            {
                dstShape.push_back(_conv.dstC);
                dstShape.push_back(_conv.dstH);
                dstShape.push_back(_conv.dstW);

                _siW = _conv.srcC * _conv.kernelY * _conv.kernelX / _conv.group;
                _ldW = _siW;
                _grW = _conv.dstC * _siW / _conv.group;

                _siS = _conv.dstH * _conv.dstW;
                _ldS = _siS;
                _grS = _siS * _siW;

                _siD = _conv.dstC / _conv.group;
                _ldD = _conv.dstH * _conv.dstW;
                _grD = _siD * _siS;
            }

            if (_is8i)
            {
                dst[0]->As8u().Reshape(dstShape, src[0]->Format());
                buf[TensorType8u]->As8u().Extend(Shape({ _conv.kernelY * _conv.kernelX * _conv.srcC, _conv.dstH * _conv.dstW }));
                buf[TensorType32i]->As8u().Extend(dstShape, src[0]->Format());
                Init8i();
            }
            else
            {
                dst[0]->Reshape(dstShape, src[0]->Format());

                _convolution.Init(_trans, _num, &_conv, SYNET_EXTERNAL_GEMM);
                if (_convolution.Enable())
                {
                    buf[TensorType32f]->Extend({ _convolution.ExternalBufferSize() });
                    _convolution.SetParams(weight[0].CpuData(), &_internal, _biasTerm ? weight[1].CpuData() : NULL,
                        _conv.activation == ActivationFunctionTypePrelu ? weight.back().CpuData() : _params);
                }
                else
                    buf[TensorType32f]->Extend(Shape({ _conv.kernelY * _conv.kernelX * _conv.srcC, _conv.dstH * _conv.dstW }));
            }
            _srcSize = src[0]->Size(_axis);
            _dstSize = dst[0]->Size(_axis);
        }

    protected:
        virtual void ForwardCpu(const TensorPtrs & src, const TensorPtrs & buf, const TensorPtrs & dst)
        {
            SYNET_PERF_FUNC();

            if (_is8i)
                 ForwardCpu8i(src[0]->As8u().CpuData(), buf[TensorType8u]->As8u().CpuData(), buf[TensorType32i]->As32i().CpuData(), dst[0]->As8u().CpuData());
            else
                 ForwardCpu(src[0]->CpuData(), buf[TensorType32f]->CpuData(), dst[0]->CpuData());
        }

        void ForwardCpu(const T * src, T * buf, T * dst)
        {
#ifdef SYNET_SIZE_STATISTIC
            std::stringstream ss;
            ss << "i=" << _num << "x" << _conv.srcC << "x" << _conv.srcH << "x" << _conv.srcW << " o=" << _conv.dstC;
            ss << " k=" << _conv.kernelY << " s=" << _conv.strideY << " g=" << _conv.group;
            SYNET_PERF_BLOCK(ss.str().c_str());
#else
            SYNET_PERF_FUNC();
#endif
            if (_convolution.Enable())
                _convolution.Forward(src, buf, dst);
            else
            {
                const Type * weight = this->Weight()[0].CpuData();
                for (size_t n = 0; n < _num; ++n)
                {
                    const Type * tmp = src;
                    if (!_is1x1)
                    {
                        if (_trans)
                            Synet::ImgToRow(tmp, _conv.srcH, _conv.srcW, _conv.srcC, _conv.kernelY, _conv.kernelX, 
                                _conv.padY, _conv.padX, _conv.padH, _conv.padW, _conv.strideY, _conv.strideX, _conv.dilationY, _conv.dilationX, _conv.group, (const Type*)NULL, buf);
                        else
                            Synet::ImgToCol(tmp, _conv.srcC, _conv.srcH, _conv.srcW, _conv.kernelY, _conv.kernelX, 
                                _conv.padY, _conv.padX, _conv.padH, _conv.padW, _conv.strideY, _conv.strideX, _conv.dilationY, _conv.dilationX, (const Type*)NULL, buf);
                        tmp = buf;
                    }
                    if (_trans)
                    {
                        assert(_conv.group == 1 || _conv.group == _conv.srcC);
                        for (size_t g = 0; g < _conv.group; ++g)
                            CpuGemm(CblasNoTrans, CblasNoTrans, _siS, _siD, _siW, Type(1), tmp + _grS * g, _ldS, weight + _grW * g, _ldW, Type(0), dst + _grD * g, _ldD);
                    }
                    else
                    {
                        for (size_t g = 0; g < _conv.group; ++g)
                            CpuGemm(CblasNoTrans, CblasNoTrans, _siD, _siS, _siW, Type(1), weight + _grW * g, _ldW, tmp + _grS * g, _ldS, Type(0), dst + _grD * g, _ldD);
                    }
                    if (_biasTerm)
                        CpuAddBias(this->Weight()[1].CpuData(), _conv.dstC, _conv.dstH*_conv.dstW, dst, _trans);
                    switch (_conv.activation)
                    {
                    case ActivationFunctionTypeIdentity:
                        break;
                    case ActivationFunctionTypeRelu:
                        CpuRelu(dst, _dstSize, 0.0f, dst);
                        break;
                    case ActivationFunctionTypeLeakyRelu:
                        CpuRelu(dst, _dstSize, _params[0], dst);
                        break;
                    case ActivationFunctionTypeRestrictRange:
                        CpuRestrictRange(dst, _dstSize, _params[0], _params[1], dst);
                        break;
                    case ActivationFunctionTypePrelu:
                        Detail::PreluLayerForwardCpu(dst, this->Weight().back().CpuData(), _conv.dstC, _conv.dstH * _conv.dstW, dst, _trans);
                        break;
                    case ActivationFunctionTypeElu:
                        CpuElu(dst, _dstSize, _params[0], dst);
                    default:
                        assert(0);
                    }
                    src += _srcSize;
                    dst += _dstSize;
                }
            }
        }

        void Init8i()
        {
            const Stat & stat = *this->Stats(0)[0];
            _zero8u.Reshape(Shape({ _conv.srcC }));
            _weight8i.Reshape(Shape({ _conv.srcC*_conv.dstC*_conv.kernelX*_conv.kernelY }));
            _bias32i.Reshape(Shape({ _conv.dstC }));
        }

        void ForwardCpu8i(const uint8_t * src, uint8_t * buf, int32_t * sum, uint8_t * dst)
        {
            const uint8_t * zero = _zero8u.CpuData();
            const int8_t * weight = _weight8i.CpuData();
            for (size_t n = 0; n < _num; ++n)
            {
                const uint8_t * tmp = src;
                if (!_is1x1)
                {
                    if (_trans)
                        Synet::ImgToRow(tmp, _conv.srcH, _conv.srcW, _conv.srcC, _conv.kernelY, _conv.kernelX,
                            _conv.padY, _conv.padX, _conv.padH, _conv.padW, _conv.strideY, _conv.strideX, _conv.dilationY, _conv.dilationX, _conv.group, zero, buf);
                    else
                        Synet::ImgToCol(tmp, _conv.srcC, _conv.srcH, _conv.srcW, _conv.kernelY, _conv.kernelX,
                            _conv.padY, _conv.padX, _conv.padH, _conv.padW, _conv.strideY, _conv.strideX, _conv.dilationY, _conv.dilationX, zero, buf);
                    tmp = buf;
                }
                //if (_trans)
                //{
                //    assert(_conv.group == 1 || _conv.group == _conv.srcC);
                //    for (size_t g = 0; g < _conv.group; ++g)
                //        CpuGemm(CblasNoTrans, CblasNoTrans, _siS, _siD, _siW, Type(1), tmp + _grS * g, _ldS, weight + _grW * g, _ldW, Type(0), dst + _grD * g, _ldD);
                //}
                //else
                //{
                //    for (size_t g = 0; g < _conv.group; ++g)
                //        CpuGemm(CblasNoTrans, CblasNoTrans, _siD, _siS, _siW, Type(1), weight + _grW * g, _ldW, tmp + _grS * g, _ldS, Type(0), dst + _grD * g, _ldD);
                //}
                //if (_biasTerm)
                //    CpuAddBias(this->Weight()[1].CpuData(), _conv.dstC, _conv.dstH*_conv.dstW, dst, _trans);
                //switch (_conv.activation)
                //{
                //case ActivationFunctionTypeIdentity:
                //    break;
                //case ActivationFunctionTypeRelu:
                //    CpuRelu(dst, _dstSize, 0.0f, dst);
                //    break;
                //case ActivationFunctionTypeLeakyRelu:
                //    CpuRelu(dst, _dstSize, _params[0], dst);
                //    break;
                //case ActivationFunctionTypeRestrictRange:
                //    CpuRestrictRange(dst, _dstSize, _params[0], _params[1], dst);
                //    break;
                //case ActivationFunctionTypePrelu:
                //    Detail::PreluLayerForwardCpu(dst, this->Weight().back().CpuData(), _conv.dstC, _conv.dstH * _conv.dstW, dst, _trans);
                //    break;
                //default:
                //    assert(0);
                //}
                src += _srcSize;
                dst += _dstSize;
            }

        }

    private:
        bool _is1x1, _biasTerm, _is8i, _hasPad;
        int _trans, _internal;
        ConvParam _conv;
        size_t _axis, _num, _srcSize, _dstSize, _ldW, _ldS, _ldD, _grW, _grS, _grD, _siW, _siS, _siD;
        float _params[2];

        Convolution<Type> _convolution;

        Tensor8u _zero8u;
        Tensor8i _weight8i;
        Tensor32i _bias32i;
    };
}