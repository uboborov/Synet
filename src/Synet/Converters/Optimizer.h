/*
* Synet Framework (http://github.com/ermig1979/Synet).
*
* Copyright (c) 2018-2021 Yermalayeu Ihar.
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
#include "Synet/Params.h"
#include "Synet/Utils/FileUtils.h"

namespace Synet
{
    struct OptimizerParam
    {
        SYNET_PARAM_VALUE(bool, mergeTwoConvolutions, true);
        SYNET_PARAM_VALUE(int, mergeTwoConvolutionsOutputNumMax, 256);
        SYNET_PARAM_VALUE(bool, mergeInt8Convolutions, true);
    };

    SYNET_PARAM_HOLDER(OptimizerParamHolder, OptimizerParam, optimizer);

    class Optimizer
    {
    public:
        Optimizer(const OptimizerParam& param)
            : _param(param)
        {
        }

        bool Run(Synet::NetworkParam & network, Floats & bin)
        {
            for (int stage = 0; stage < 8; stage++)
            {
                if (!OptimizeLayers(network, bin, stage))
                    return false;
            }
            if (!ReuseLayers(network))
                return false;
            if (!RemoveStub(network))
                return false;
            return true;
        }

    private:
        typedef std::vector<Synet::LayerParam> LayerParams;
        typedef std::pair<String, String> Change;
        typedef std::vector<Change> Changes;
        typedef std::vector<LayerType> LayerTypes;
        typedef std::set<String> StringSet;

        const OptimizerParam & _param;

        bool OptimizeLayers(Synet::NetworkParam& network, Floats& bin, int stage)
        {
            QuantizationMethod method = network.quantization().method();
            const bool is8i = network.quantization().method() != QuantizationMethodUnknown;
            Changes changes;
            LayerParams merged;
            Floats buf;
            for (size_t i = 0; i < network.layers().size(); ++i)
            {
                switch (stage)
                {
                case 0:
                {
                    if (ReduceTensorIteratorIO(network.layers(), i, bin, buf, merged))
                        continue;
                    break;
                }
                case 1:
                {
                    if (TransposeInnerProduct(network.layers(), i, bin, buf, merged))
                        continue;
                    break;
                }
                case 2:
                {
                    if (MergeCurrentAndBias(network.layers(), i, bin, merged, changes))
                        continue;
                    break;
                }
                case 3:
                {
                    if (MergeConvolutionAndScale(network.layers(), i, bin, buf, merged, changes))
                        continue;
                    if (MergeInnerProductAndScale(network.layers(), i, bin, buf, merged, changes))
                        continue;
                    break;
                }
                case 4:
                {
                    if (MergeHswish(network.layers(), i, merged, changes))
                        continue;
                    if (MergeMish(network.layers(), i, merged, changes))
                        continue;
                    if (MergePrelu(network.layers(), i, bin, merged, changes))
                        continue;
                    if (MergeShuffle0(network.layers(), i, merged, changes))
                        continue;
                    if (MergeShuffle1(network.layers(), i, merged, changes))
                        continue;
                    if (MergeSoftmax(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused0(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused1(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused2(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused3(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused4(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused5(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused6(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused7(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused8(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused9(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused10(network.layers(), i, merged, changes))
                        continue;
                    if (MergeFused11(network.layers(), i, merged, changes))
                        continue;
                    if (MergePooling(network.layers(), i, merged, changes))
                        continue;
                    break;
                }
                case 5:
                {
                    if (MergeConvolutionOrDeconvolutionAndActivation(network.layers(), i, method, merged, changes))
                        continue;
                    if (MergeRnnGruBd(network.layers(), i, merged, changes))
                        continue;
                    break;
                }
                case 6:
                {
                    if (MergeThreeConvolutions(network.layers(), i, method, merged, changes))
                        continue;
                    if (MergeSqueezeExcitation(network.layers(), i, merged, changes))
                        continue;
                    break;
                }
                case 7:
                {
                    if (MergeTwoConvolutions(network.layers(), i, method, merged, changes))
                        continue;
                    break;
                }
                default:
                    assert(0);
                    return false;
                }
                merged.push_back(network.layers()[i]);
            }
            Rename(changes, merged);
            network.layers() = merged;
            if (buf.size())
                bin.swap(buf);
            return true;
        }

        bool ReduceTensorIteratorIO(const LayerParams& src, size_t& index, const Floats& bin, Floats& buf, LayerParams& dst)
        {
            const LayerParam & stt = src[index];
            if (stt.type() != LayerTypeTensorIterator || stt.src().size() < 3 || stt.tensorIterator().back().size() < 1)
                return false;
            size_t srcDupls = 0;
            for (size_t i = 2; i < stt.src().size(); ++i)
            {
                if (stt.src()[1] == stt.src()[i])
                    srcDupls++;
            }
            size_t backDupls = 0;
            for (size_t i = 1; i < stt.tensorIterator().back().size(); ++i)
            {
                if (stt.tensorIterator().back()[0].src() == stt.tensorIterator().back()[i].src())
                    backDupls++;
            }
            if (srcDupls == 0 || srcDupls != backDupls || srcDupls < stt.src().size() - 2)
                return false;
            dst.push_back(stt);
            LayerParam& dtt = dst.back();
            dtt.src().resize(2);
            String rem, iter;
            for (size_t i = 0; i < dtt.tensorIterator().input().size() && iter.empty(); ++i)
                if (dtt.tensorIterator().input()[i].axis() != -1)
                    iter = dtt.tensorIterator().input()[i].dst();
            for (size_t i = index + 1; i < src.size() && rem.empty(); ++i)
            {
                if (src[i].parent() != stt.name())
                    break;
                if (src[i].type() == LayerTypeInput && src[i].name() != iter)
                    rem = src[i].name();
            }
            StringSet del;
            std::vector<ConnectionParam> back, input;
            for (size_t i = 0; i < dtt.tensorIterator().input().size(); ++i)
            {
                ConnectionParam & p = dtt.tensorIterator().input()[i];
                if (p.dst() == rem || p.dst() == iter)
                {
                    p.port() = Synet::Min<int>(1, p.port());
                    input.push_back(p);
                }
                else
                    del.insert(p.dst());
            }
            dtt.tensorIterator().input().swap(input);
            for (size_t i = 0; i < stt.tensorIterator().back().size(); ++i)
            {
                if (del.find(dtt.tensorIterator().back()[i].dst()) == del.end())
                    back.push_back(dtt.tensorIterator().back()[i]);
            }
            dtt.tensorIterator().back().swap(back);
            for (size_t i = index + 1; i < src.size(); ++i)
            {
                if (src[i].parent() != stt.name())
                    break;
                if (src[i].type() != LayerTypeInput || del.find(src[i].name()) == del.end())
                    dst.push_back(src[i]);
                for (size_t j = 0; j < dst.back().src().size(); ++j)
                {
                    if (del.find(dst.back().src()[j]) != del.end())
                        dst.back().src()[j] = rem;
                }
                index++;
            }
            return true;
        }

        bool TransposeInnerProduct(const LayerParams& src, size_t& index, const Floats& bin, Floats& buf, LayerParams& dst)
        {
            const LayerParam& ip = src[index];
            if (ip.type() != LayerTypeInnerProduct || !ip.innerProduct().transposeB())
                return false;
            const Shape & dim = ip.weight()[0].dim();
            size_t offset = ip.weight()[0].offset() / 4;
            if (buf.empty())
                buf = bin;
            dst.push_back(ip);
            dst.back().innerProduct().transposeB() = false;
            dst.back().weight()[0].dim() = Shp(dim[1], dim[0]);
            const float* pSrc = bin.data() + offset;
            float* pDst = buf.data() + offset;
            for (size_t i = 0; i < dim[0]; ++i)
                for (size_t j = 0; j < dim[1]; ++j)
                    pDst[j * dim[0] + i] = pSrc[i * dim[1] + j];
            return true;
        }

        bool MergeCurrentAndBias(const LayerParams& src, size_t& index, Floats& bin, LayerParams& dst, Changes& changes)
        {
            if (index == 0)
                return false;
            const LayerParam & current = src[index - 1];
            const LayerParam & bias = src[index];
            if (bias.type() != LayerTypeBias || bias.src()[0] != current.name())
                return false;
            if (InsideLink(src, index - 1, 2))
                return false;
            switch (current.type())
            {
            case LayerTypeConvolution:
                if (current.convolution().biasTerm())
                    return false;
                dst.back().convolution().biasTerm() = true;
                break;
            case LayerTypeInnerProduct:
                if (current.innerProduct().biasTerm())
                    return false;
                dst.back().innerProduct().biasTerm() = true;
                break;
            case LayerTypePower:
                if (current.power().power() != 1.0f || current.power().shift() != 0.0f)
                    return false;
                dst.back().type() = LayerTypeScale;
                dst.back().scale().biasTerm() = true;
                dst.back().weight().push_back(bias.weight()[0]);
                dst.back().weight()[0].offset() = bin.size() * sizeof(float);
                for (size_t i = 0; i < dst.back().weight()[0].dim()[0]; ++i)
                    bin.push_back(current.power().scale());
                dst.back().power().scale() = 1.0f;
                break;
            case LayerTypeScale:
                if (current.scale().biasTerm())
                    return false;
                dst.back().scale().biasTerm() = true;
                break;
                default:
                    return false;
            }
            dst.back().name() = bias.name();
            dst.back().dst() = bias.dst();
            dst.back().weight().push_back(bias.weight()[0]);
            return true;
        }

        bool MergeConvolutionAndScale(const LayerParams& src, size_t& index, const Floats& bin, Floats& buf, LayerParams& dst, Changes& changes)
        {
            if (index == 0)
                return false;
            const LayerParam& conv = src[index - 1];
            const LayerParam& scale = src[index];
            if (conv.type() != LayerTypeConvolution || conv.convolution().biasTerm() || 
                conv.convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (scale.type() != LayerTypeScale || scale.src()[0] != conv.name())
                return false;
            if (InsideLink(src, index - 1, 2))
                return false;
            if (conv.weight()[0].format() != TensorFormatNhwc)
                return false;
            if (buf.empty())
                buf = bin;
            dst.back().name() = scale.name();
            dst.back().dst() = scale.dst();
            if (scale.scale().biasTerm())
            {
                dst.back().convolution().biasTerm() = true;
                dst.back().weight().push_back(scale.weight()[1]);
            }
            const float* pSrc = bin.data() + conv.weight()[0].offset() / 4;
            const float* pScale = bin.data() + scale.weight()[0].offset() / 4;
            float * pDst = buf.data() + conv.weight()[0].offset() / 4;
            const Shape & dim = conv.weight()[0].dim();
            for (size_t i = 0, n = dim[0] * dim[1] * dim[2]; i < n; ++i)
                for (size_t j = 0, m = dim[3]; j < m; ++j)
                    pDst[i * m + j] = pSrc[i * m + j] * pScale[j];
            return true;
        }

        bool MergeInnerProductAndScale(const LayerParams& src, size_t& index, const Floats& bin, Floats& buf, LayerParams& dst, Changes& changes)
        {
            if (index == 0)
                return false;
            const LayerParam& ip = src[index - 1];
            const LayerParam& scale = src[index];
            if (ip.type() != LayerTypeInnerProduct || ip.innerProduct().biasTerm() || ip.innerProduct().transposeB())
                return false;
            if (scale.type() != LayerTypeScale || scale.src()[0] != ip.name())
                return false;
            if (InsideLink(src, index - 1, 2))
                return false;
            if (buf.empty())
                buf = bin;
            dst.back().name() = scale.name();
            dst.back().dst() = scale.dst();
            if (scale.scale().biasTerm())
            {
                dst.back().innerProduct().biasTerm() = true;
                dst.back().weight().push_back(scale.weight()[1]);
            }
            const float* pSrc = bin.data() + ip.weight()[0].offset() / 4;
            const float* pScale = bin.data() + scale.weight()[0].offset() / 4;
            float* pDst = buf.data() + ip.weight()[0].offset() / 4;
            const Shape& dim = ip.weight()[0].dim();
            for (size_t i = 0; i < dim[0]; ++i)
                for (size_t j = 0; j < dim[1]; ++j)
                    pDst[i * dim[1] + j] = pSrc[i * dim[1] + j] * pScale[i];
            return true;
        }

        bool MergeHswish(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 4)
                return false;
            if (src[index + 0].type() != LayerTypePower || src[index + 0].power().power() != 1.0f ||
                src[index + 0].power().scale() != 1.0f)
                return false;
            if (src[index + 1].type() != LayerTypeRestrictRange || src[index + 1].src()[0] != src[index + 0].name() ||
                src[index + 1].restrictRange().lower() != 0.0f)
                return false;
            if (src[index + 2].type() != LayerTypePower || src[index + 2].power().power() != 1.0f ||
                src[index + 2].power().shift() != 0.0f || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeEltwise || src[index + 3].src().size() != 2 ||
                src[index + 3].src()[0] != src[index + 0].src()[0] || src[index + 3].src()[1] != src[index + 2].name() ||
                src[index + 3].eltwise().operation() != EltwiseOperationTypeProduct)
                return false;
            if (!Equal(src[index + 0].power().shift() * 2.0f, src[index + 1].restrictRange().upper()))
                return false;
            if (InsideLink(src, index + 1, 3))
                return false;

            LayerParam layer;
            layer.type() = LayerTypeHswish;
            layer.name() = src[index + 3].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.hswish().shift() = src[index + 0].power().shift();
            layer.hswish().scale() = src[index + 2].power().scale();
            dst.push_back(layer);
            index += 3;
            return true;
        }

        bool MergeMish(const LayerParams& src, size_t& index, LayerParams& dst, Changes& changes)
        {
            if (src.size() < index + 5)
                return false;
            if (src[index + 0].type() != LayerTypeUnaryOperation || 
                src[index + 0].unaryOperation().type() != UnaryOperationTypeExp)
                return false;
            if (src[index + 1].type() != LayerTypePower || src[index + 1].power().power() != 1.0f ||
                src[index + 1].power().scale() != 1.0f || src[index + 1].power().shift() != 1.0f ||
                src[index + 1].src()[0] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeUnaryOperation ||
                src[index + 2].unaryOperation().type() != UnaryOperationTypeLog ||
                src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeUnaryOperation ||
                src[index + 3].unaryOperation().type() != UnaryOperationTypeTanh ||
                src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeEltwise || src[index + 4].src().size() != 2 ||
                src[index + 4].src()[0] != src[index + 0].src()[0] || src[index + 4].src()[1] != src[index + 3].name() ||
                src[index + 4].eltwise().operation() != EltwiseOperationTypeProduct)
                return false;
            if (InsideLink(src, index + 1, 4))
                return false;

            LayerParam layer;
            layer.type() = LayerTypeMish;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergePrelu(const LayerParams & src, size_t & index, const Floats & bin, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 2)
                return false;
            if (src[index + 0].type() != LayerTypeScale)
                return false;
            if (src[index + 1].type() != LayerTypeEltwise || src[index + 1].src().size() != 2 ||
                src[index + 1].src()[1] != src[index + 0].src()[0] || src[index + 1].src()[0] != src[index + 0].name() ||
                src[index + 1].eltwise().operation() != EltwiseOperationTypeMax)
                return false;
            if (InsideLink(src, index + 1, 1))
                return false;
            const float * scale = bin.data() + src[index].weight()[0].offset() / 4;
            for (size_t i = 0, n = src[index].weight()[0].size() / 4; i < n; ++i)
                if (scale[i] < -1.0f || scale[i] > 1.0f)
                    return false;
            if (src[index + 0].weight().size() > 1)
            {
                const float * shift = bin.data() + src[index].weight()[1].offset() / 4;
                for (size_t i = 0, n = src[index].weight()[1].size() / 4; i < n; ++i)
                    if (shift[i] != 0.0f)
                        return false;
            }
            LayerParam layer;
            layer.type() = LayerTypePrelu;
            layer.name() = src[index + 1].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.prelu().axis() = src[index + 0].scale().axis();
            layer.weight().push_back(src[index + 0].weight()[0]);
            dst.push_back(layer);
            index += 1;
            return true;
        }

        bool MergeConvolutionOrDeconvolutionAndActivation(const LayerParams & src, size_t index, QuantizationMethod method, LayerParams & dst, Changes & changes)
        {
            if (index == 0)
                return false;
            const LayerParam& conv = src[index - 1];
            const LayerParam& act = src[index];
            if (conv.type() != LayerTypeConvolution && conv.type() != LayerTypeDeconvolution)
                return false;
            if (act.src().size() != 1 || act.src()[0] != conv.name())
                return false;
            if (InsideLink(src, index - 1, 2))
                return false;
            bool result = false;
            if (act.type() == LayerTypeRestrictRange)
            {
                dst.back().convolution().activationType() = ActivationFunctionTypeRestrictRange;
                dst.back().convolution().activationParam0() = act.restrictRange().lower();
                dst.back().convolution().activationParam1() = act.restrictRange().upper();
                result = true;
            }
            if (act.type() == LayerTypeRelu)
            {
                dst.back().convolution().activationType() = act.relu().negativeSlope() == 0.0f ? ActivationFunctionTypeRelu : ActivationFunctionTypeLeakyRelu;
                dst.back().convolution().activationParam0() = act.relu().negativeSlope();
                result = true;
            }
            if (act.type() == LayerTypePrelu && method != QuantizationMethodIECompatible)
            {
                dst.back().convolution().activationType() = ActivationFunctionTypePrelu;
                dst.back().weight().push_back(act.weight()[0]);
                result = true;
            }
            if (act.type() == LayerTypeElu)
            {
                dst.back().convolution().activationType() = ActivationFunctionTypeElu;
                dst.back().convolution().activationParam0() = act.elu().alpha();
                result = true;
            }
            if (act.type() == LayerTypeHswish)
            {
                dst.back().convolution().activationType() = ActivationFunctionTypeHswish;
                dst.back().convolution().activationParam0() = act.hswish().shift();
                dst.back().convolution().activationParam1() = act.hswish().scale();
                result = true;
            }
            if (act.type() == LayerTypeMish)
            {
                dst.back().convolution().activationType() = ActivationFunctionTypeMish;
                dst.back().convolution().activationParam0() = act.softplus().threshold();
                result = true;
            }
            if (result)
            {
                if (dst.back().convolution().quantizationLevel() == TensorType8i)
                {
                    dst.back().origin().push_back(conv.name());
                    dst.back().name() = act.name();
                    dst.back().dst()[0] = act.name();
                }
                else
                    changes.push_back(Change(act.name(), conv.name()));
            }
            return result;
        }

        bool MergeThreeConvolutions(const LayerParams & src, size_t & index, QuantizationMethod method, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 3 || (method != QuantizationMethodUnknown && !_param.mergeInt8Convolutions()))
                return false;
            const LayerParam & l0 = src[index + 0];
            const Shape & k0 = l0.convolution().kernel();
            const LayerParam & l1 = src[index + 1];
            const Shape & k1 = l1.convolution().kernel();
            const LayerParam & l2 = src[index + 2];
            const Shape & k2 = l2.convolution().kernel();
            if (l0.type() != LayerTypeConvolution || l1.type() != LayerTypeConvolution || 
                l2.type() != LayerTypeConvolution || l1.src()[0] != l0.dst()[0] || l2.src()[0] != l1.dst()[0])
                return false;
            if (l0.weight()[0].format() != TensorFormatNhwc)
                return false;
            if (k0.size() < 2 || (k0[0] != k0[1] || (k0[0] != 1 && k0[0] != 3)))
                return false;
            if (l1.convolution().outputNum() != l1.convolution().group())
                return false;
            if (k1.size() < 2 || (k1[0] != k1[1] || (k1[0] != 3 && k1[0] != 5 && k1[0] != 7)))
                return false;
            if (k2.size() < 2 || k2[0] != 1 || k2[1] != 1)
                return false;
            if (InsideLink(src, index, 3))
                return false;
            if (l1.convolution().outputNum() < l2.convolution().outputNum()*0.75 && l2.convolution().outputNum() > 256)
                return false;
            if (index && _param.mergeTwoConvolutions())
            {
                const LayerParam& ln = src[index - 1];
                if (ln.type() == LayerTypeConvolution && l0.src()[0] == ln.dst()[0] &&
                    ln.convolution().outputNum() == ln.convolution().group() && !InsideLink(src, index - 1, 4) &&
                    l2.convolution().outputNum() >= l1.convolution().outputNum())
                    return false;
            }
            if (src.size() > index + 3 && _param.mergeTwoConvolutions())
            {
                const LayerParam& l3 = src[index + 3];
                if (l3.type() == LayerTypeConvolution && l3.src()[0] == l2.dst()[0] &&
                    l3.convolution().outputNum() == l3.convolution().group() && !InsideLink(src, index, 4) &&
                    l2.convolution().outputNum() >= l1.convolution().outputNum())
                    return false;
            }
            LayerParam layer;
            layer.type() = LayerTypeMergedConvolution;
            layer.name() = l2.name();
            layer.src() = l0.src();
            layer.dst().push_back(layer.name());
            for (size_t l = 0; l < 3; ++l)
                for(size_t i = 0; i < src[index + l].weight().size(); ++i)
                    layer.weight().push_back(src[index + l].weight()[i]);
            layer.mergedConvolution().conv().push_back(l0.convolution());
            layer.mergedConvolution().conv().push_back(l1.convolution());
            layer.mergedConvolution().conv().push_back(l2.convolution());
            if (layer.mergedConvolution().conv()[0].quantizationLevel() == TensorType8i || 
                layer.mergedConvolution().conv()[2].quantizationLevel() == TensorType8i)
            {
                layer.origin().push_back(l0.name());
                layer.origin().push_back(l1.name());
            }
            index += 2;
            dst.push_back(layer);
            if (src.size() > index + 1 && method == QuantizationMethodUnknown)
            {
                const LayerParam & l3 = src[index + 1];
                if (l2.convolution().activationType() == ActivationFunctionTypeIdentity && l3.type() == LayerTypeEltwise && l3.eltwise().operation() == EltwiseOperationTypeSum &&
                    l3.eltwise().coefficients().empty() && l3.src().size() == 2 && l3.src()[0] == l0.src()[0] && l3.src()[1] == l2.dst()[0] && !InsideLink(src, index - 2, 4))
                {
                    dst.back().mergedConvolution().add() = true;
                    dst.back().name() = l3.name();
                    dst.back().dst()[0] = dst.back().name();
                    index += 1;
                    if (src.size() > index + 1)
                    {
                        const LayerParam & l4 = src[index + 1];
                        if (l4.src().size() == 1 && l4.src()[0] == l3.name() && !InsideLink(src, index - 3, 5))
                        {
                            bool result = false;
                            if (l4.type() == LayerTypeRestrictRange)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = ActivationFunctionTypeRestrictRange;
                                dst.back().mergedConvolution().conv()[2].activationParam0() = l4.restrictRange().lower();
                                dst.back().mergedConvolution().conv()[2].activationParam1() = l4.restrictRange().upper();
                                result = true;
                            }
                            if (l4.type() == LayerTypeRelu)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = l4.relu().negativeSlope() == 0.0f ? ActivationFunctionTypeRelu : ActivationFunctionTypeLeakyRelu;
                                dst.back().mergedConvolution().conv()[2].activationParam0() = l4.relu().negativeSlope();
                                result = true;
                            }
                            if (l4.type() == LayerTypePrelu)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = ActivationFunctionTypePrelu;
                                dst.back().weight().push_back(l4.weight()[0]);
                                result = true;
                            }
                            if (l4.type() == LayerTypeElu)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = ActivationFunctionTypeElu;
                                dst.back().mergedConvolution().conv()[2].activationParam0() = l4.elu().alpha();
                                result = true;
                            }
                            if (l4.type() == LayerTypeHswish)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = ActivationFunctionTypeHswish;
                                dst.back().mergedConvolution().conv()[2].activationParam0() = l4.hswish().shift();
                                dst.back().mergedConvolution().conv()[2].activationParam1() = l4.hswish().scale();
                                result = true;
                            }
                            if (l4.type() == LayerTypeMish)
                            {
                                dst.back().mergedConvolution().conv()[2].activationType() = ActivationFunctionTypeMish;
                                dst.back().mergedConvolution().conv()[2].activationParam0() = l4.softplus().threshold();
                                result = true;
                            }
                            if (result)
                            {
                                dst.back().name() = l4.name();
                                dst.back().dst()[0] = dst.back().name();
                                index += 1;
                            }
                        }
                    }
                }
            }
            return true;
        }

        bool MergeTwoConvolutions(const LayerParams& src, size_t& index, QuantizationMethod method, LayerParams& dst, Changes& changes)
        {
            if (src.size() < index + 2 || !_param.mergeTwoConvolutions() || (method != QuantizationMethodUnknown && !_param.mergeInt8Convolutions()))
                return false;
            const LayerParam& l0 = src[index + 0];
            const Shape& k0 = l0.convolution().kernel();
            const LayerParam& l1 = src[index + 1];
            const Shape& k1 = l1.convolution().kernel();
            if (l0.type() != LayerTypeConvolution || l1.type() != LayerTypeConvolution || l1.src()[0] != l0.dst()[0])
                return false;
            if (l0.weight()[0].format() != TensorFormatNhwc)
                return false;
            if (InsideLink(src, index, 2))
                return false;
            if (l0.convolution().outputNum() > _param.mergeTwoConvolutionsOutputNumMax() &&
                l1.convolution().outputNum() > _param.mergeTwoConvolutionsOutputNumMax())
                return false;
            if (l0.convolution().group() != 1)
            {
                if (l0.convolution().outputNum() != l0.convolution().group())
                    return false;
                if (k0.size() < 2 || (k0[0] != k0[1] || (k0[0] != 3 && k0[0] != 5 && k0[0] != 7)))
                    return false;
                if (k1.size() < 2 || (k1[0] != k1[1] || (k1[0] != 1)))
                    return false;
            }
            else
            {
                if (k0.size() < 2 || (k0[0] != k0[1] || (k0[0] != 1 && k0[0] != 3)))
                    return false;
                if (l1.convolution().outputNum() != l1.convolution().group())
                    return false;
                if (k1.size() < 2 || (k1[0] != k1[1] || (k1[0] != 3 && k1[0] != 5 && k1[0] != 7)))
                    return false;
            }
            LayerParam layer;
            layer.type() = LayerTypeMergedConvolution;
            layer.name() = l1.name();
            layer.src() = l0.src();
            layer.dst().push_back(layer.name());
            for (size_t l = 0; l < 2; ++l)
                for (size_t i = 0; i < src[index + l].weight().size(); ++i)
                    layer.weight().push_back(src[index + l].weight()[i]);
            layer.mergedConvolution().conv().push_back(l0.convolution());
            layer.mergedConvolution().conv().push_back(l1.convolution());
            if (layer.mergedConvolution().conv()[0].quantizationLevel() == TensorType8i || 
                layer.mergedConvolution().conv()[1].quantizationLevel() == TensorType8i)
                layer.origin().push_back(l0.name());
            index += 1;
            dst.push_back(layer);
            return true;
        }

        bool MergeSoftmax(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 5)
                return false;
            if (src[index + 0].type() != LayerTypeReduction || src[index + 0].reduction().type() != ReductionTypeMax ||
                src[index + 0].reduction().axis().size() != 1)
                return false;
            if (src[index + 1].type() != LayerTypeBinaryOperation || src[index + 1].binaryOperation().type() != BinaryOperationTypeSub ||
                src[index + 1].src()[0] != src[index + 0].src()[0] || src[index + 1].src()[1] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeUnaryOperation || src[index + 2].unaryOperation().type() != UnaryOperationTypeExp ||
                src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeReduction || src[index + 3].reduction().type() != ReductionTypeSum ||
                src[index + 3].reduction().axis() != src[index + 0].reduction().axis() || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeBinaryOperation || src[index + 4].binaryOperation().type() != BinaryOperationTypeDiv ||
                src[index + 4].src()[0] != src[index + 2].name() || src[index + 4].src()[1] != src[index + 3].name())
                return false;
            for (size_t i = index + 5; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = 0; k < 4; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeSoftmax;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.softmax().axis() = src[index + 0].reduction().axis()[0];
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeShuffle0(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 5)
                return false;
            if (src[index + 0].type() != LayerTypeConcat || src[index + 0].src().size() != 2)
                return false;
            if (src[index + 1].type() != LayerTypeReshape || src[index + 1].reshape().shape().size() != 3)
                return false;
            if (src[index + 2].type() != LayerTypePermute)
                return false;
            if (src[index + 3].type() != LayerTypeUnpack || src[index + 3].dst().size() != 2)
                return false;
            if (src[index + 4].type() != LayerTypeReshape || 
                src[index + 4].reshape().shape().size() + src[index + 4].reshape().axis() != 4)
                return false;
            if (src[index + 5].type() != LayerTypeReshape || 
                src[index + 5].reshape().shape().size() + src[index + 5].reshape().axis() != 4)
                return false;
            if (InsideLink(src, index, 4, 1))
                return false;
            LayerParam layer;
            layer.type() = LayerTypeShuffle;
            layer.name() = src[index + 0].name();
            layer.src() = src[index + 0].src();
            layer.shuffle().type() = 0;
            layer.dst().push_back(src[index + 4].dst()[0]);
            layer.dst().push_back(src[index + 5].dst()[0]);
            index += 5;
            dst.push_back(layer);
            return true;
        }

        bool MergeShuffle1(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 4)
                return false;
            if (src[index + 0].type() != LayerTypeConcat || src[index + 0].src().size() != 2)
                return false;
            if (src[index + 1].type() != LayerTypeReshape || src[index + 1].reshape().shape().size() != 4)
                return false;
            if (src[index + 2].type() != LayerTypePermute)
                return false;
            if (src[index + 3].type() != LayerTypeReshape || src[index + 3].reshape().shape().size() != 3)
                return false;
            if (src[index + 4].type() != LayerTypeUnpack || src[index + 4].dst().size() != 2)
                return false;
            if (InsideLink(src, index, 4, 0))
                return false;
            LayerParam layer;
            layer.type() = LayerTypeShuffle;
            layer.name() = src[index + 0].name();
            layer.src() = src[index + 0].src();
            layer.shuffle().type() = 1;
            layer.dst().push_back(src[index + 4].dst()[0]);
            layer.dst().push_back(src[index + 4].dst()[1]);
            index += 4;
            dst.push_back(layer);
            return true;
        }

        bool MergeSqueezeExcitation(const LayerParams& src, size_t& index, LayerParams& dst, Changes& changes)
        {
            if (src.size() <= index + 4)
                return false;
            if (src[index + 0].type() != LayerTypePooling || src[index + 0].pooling().method() != PoolingMethodTypeAverage)
                return false;
            if (src[index + 1].type() != LayerTypeConvolution || src[index + 1].convolution().kernel() != Shp(1, 1) || 
                src[index + 1].convolution().biasTerm() || src[index + 1].src()[0] != src[index + 0].name() || 
                src[index + 1].convolution().activationType() != ActivationFunctionTypeRelu)
                return false;
            if (src[index + 2].type() != LayerTypeConvolution || src[index + 2].convolution().kernel() != Shp(1, 1) ||
                src[index + 2].convolution().biasTerm() || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeSigmoid || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeEltwise || src[index + 4].eltwise().operation() != EltwiseOperationTypeProduct ||
                src[index + 4].src()[0] != src[index + 0].src()[0] || src[index + 4].src()[1] != src[index + 3].dst()[0])
                return false;
            if (InsideLink(src, index + 1, 4))
                return false;
            LayerParam layer;
            layer.type() = LayerTypeSqueezeExcitation;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 2].weight()[0]);
            layer.dst().push_back(src[index + 4].dst()[0]);
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeFused0(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 6)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || !src[index - 1].convolution().biasTerm() || 
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeRelu || src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeUnaryOperation || src[index + 1].unaryOperation().type() != UnaryOperationTypeAbs || 
                src[index + 1].src()[0] != src[index - 1].name())
                return false;
            if (!IsSub(src[index + 2]) || src[index + 2].src() != Strings({ src[index - 1].name(), src[index + 1].name() }))
                return false;
            if (src[index + 3].type() != LayerTypeScale || src[index + 3].scale().biasTerm() || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeScale || src[index + 4].scale().biasTerm() || src[index + 4].src()[0] != src[index + 3].name())
                return false;
            if (src[index + 5].type() != LayerTypeEltwise || src[index + 5].eltwise().operation() != EltwiseOperationTypeSum || 
                !src[index + 5].eltwise().coefficients().empty() || src[index + 5].src() != Strings({ src[index + 0].name(), src[index + 4].name() }))
                return false;
            for (size_t i = index + 6; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 5; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 5].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst().push_back(layer.name());
            layer.fused().type() = 0;
            layer.weight().push_back(src[index - 1].weight()[1]);
            layer.weight().push_back(src[index + 3].weight()[0]);
            layer.weight().push_back(src[index + 4].weight()[0]);
            dst.back().weight().resize(1);
            dst.back().convolution().biasTerm() = false;
            dst.push_back(layer);
            index += 5;
            return true;
        }

        bool MergeFused1(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 5)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || !src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeRelu || src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeScale || src[index + 1].scale().axis() != 0 || !src[index + 1].scale().biasTerm() ||
                src[index + 1].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeScale || !src[index + 3].scale().biasTerm() || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeEltwise || src[index + 4].eltwise().operation() != EltwiseOperationTypeSum ||
                !src[index + 4].eltwise().coefficients().empty() || src[index + 4].src() != Strings({ src[index + 0].name(), src[index + 3].name() }))
                return false;
            for (size_t i = index + 5; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 4; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst().push_back(layer.name());
            layer.fused().type() = 1;
            layer.weight().push_back(src[index - 1].weight()[1]);
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 1].weight()[1]);
            layer.weight().push_back(src[index + 3].weight()[0]);
            layer.weight().push_back(src[index + 3].weight()[1]);
            changes.push_back(Change(layer.dst()[0], layer.src()[0]));
            layer.dst()[0] = layer.src()[0];
            dst.back().weight().resize(1);
            dst.back().convolution().biasTerm() = false;
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeFused2(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 2)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeBatchNorm || !src[index + 0].batchNorm().useGlobalStats() || !src[index + 0].batchNorm().yoloCompatible() || 
                src[index + 0].src()[0] != src[index - 1].name() || src[index + 0].dst()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeScale || !src[index + 1].scale().biasTerm() || src[index + 1].scale().axis() != 1 ||
                src[index + 1].src()[0] != src[index - 1].name() || src[index + 1].dst()[0] != src[index - 1].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || 
                src[index + 2].src()[0] != src[index - 1].name() || src[index + 2].dst()[0] != src[index - 1].name())
                return false;
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 2].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst() = src[index + 2].dst();
            layer.fused().type() = 2;
            layer.fused().floats().resize(2);
            layer.fused().floats()[0] = src[index + 0].batchNorm().eps();
            layer.fused().floats()[1] = src[index + 2].relu().negativeSlope();
            layer.weight().push_back(src[index + 0].weight()[0]);
            layer.weight().push_back(src[index + 0].weight()[1]);
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 1].weight()[1]);
            dst.push_back(layer);
            index += 2;
            return true;
        }

        bool MergeFused3(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 6)
                return false;
            if ((src[index - 1].type() != LayerTypeConvolution || !src[index - 1].convolution().biasTerm() || 
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity) &&
                (src[index - 1].type() != LayerTypeInnerProduct || !src[index - 1].innerProduct().biasTerm()))
                return false;
            if (src[index + 0].type() != LayerTypeRelu || src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeUnaryOperation || src[index + 1].unaryOperation().type() != UnaryOperationTypeNeg ||
                src[index + 1].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeUnaryOperation || src[index + 3].unaryOperation().type() != UnaryOperationTypeNeg ||
                src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeScale || src[index + 4].scale().biasTerm() || src[index + 4].src()[0] != src[index + 3].name())
                return false;
            if (src[index + 5].type() != LayerTypeEltwise || src[index + 5].eltwise().operation() != EltwiseOperationTypeSum ||
                !src[index + 5].eltwise().coefficients().empty() || src[index + 5].src() != Strings({ src[index + 0].name(), src[index + 4].name() }))
                return false;
            for (size_t i = index + 6; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 5; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            if (dst.back().type() == LayerTypeConvolution)
            {
                dst.back().name() = src[index + 5].name();
                dst.back().dst().back() = dst.back().name();
                dst.back().convolution().activationType() = ActivationFunctionTypePrelu;
                dst.back().weight().push_back(src[index + 4].weight()[0]);
            }
            else
            {
                LayerParam layer;
                layer.type() = LayerTypeFused;
                layer.name() = src[index + 5].name();
                layer.src().push_back(src[index - 1].name());
                layer.dst().push_back(layer.name());
                layer.fused().type() = 3;
                layer.weight().push_back(src[index - 1].weight()[1]);
                layer.weight().push_back(src[index + 4].weight()[0]);
                dst.back().weight().resize(1);
                dst.back().innerProduct().biasTerm() = false;
                dst.push_back(layer);
            }
            index += 5;
            return true;
        }

        bool MergeFused4(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 3)
                return false;
            if ((src[index - 1].type() != LayerTypeConvolution || !src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity))
                return false;
            if (src[index + 0].type() != LayerTypePower || src[index + 0].power().power() != 1.0f || src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeConcat || src[index + 1].src().size() != 2 ||
                src[index + 1].src()[0] != src[index - 1].name() || src[index + 1].src()[1] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            for (size_t i = index + 3; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 2; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 2].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst().push_back(layer.name());
            layer.fused().type() = 4;
            layer.weight().push_back(src[index - 1].weight()[1]);
            layer.fused().floats().push_back(src[index + 0].power().scale());
            layer.fused().floats().push_back(src[index + 0].power().shift());
            dst.back().weight().resize(1);
            dst.back().convolution().biasTerm() = false;
            dst.push_back(layer);
            index += 2;
            return true;
        }

        bool MergeFused5(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 3)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeScale || !src[index + 0].scale().biasTerm() || src[index + 0].scale().axis() != 1 ||
                src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeScale || !src[index + 1].scale().biasTerm() || src[index + 1].scale().axis() != 1 ||
                src[index + 1].src()[0] != src[index - 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu ||
                src[index + 2].src()[0] != src[index + 1].name())
                return false;
            for (size_t i = index + 3; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 2; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 2].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst() = src[index + 2].dst();
            layer.fused().type() = 5;
            layer.weight().push_back(src[index + 0].weight()[0]);
            layer.weight().push_back(src[index + 0].weight()[1]);
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 1].weight()[1]);
            changes.push_back(Change(layer.dst()[0], layer.src()[0]));
            layer.dst()[0] = layer.src()[0];
            dst.push_back(layer);
            index += 2;
            return true;
        }

        bool MergeFused6(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 2)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeScale || !src[index + 0].scale().biasTerm() || src[index + 0].scale().axis() != 1 ||
                src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypeRelu ||
                src[index + 1].src()[0] != src[index + 0].name())
                return false;
            for (size_t i = index + 2; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 1; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 1].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst() = src[index + 1].dst();
            layer.fused().type() = 6;
            layer.weight().push_back(src[index + 0].weight()[0]);
            layer.weight().push_back(src[index + 0].weight()[1]);
            changes.push_back(Change(layer.dst()[0], layer.src()[0]));
            layer.dst()[0] = layer.src()[0];
            dst.push_back(layer);
            index += 1;
            return true;
        }

        bool MergeFused7(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (index == 0 || src.size() < index + 5)
                return false;
            if (src[index - 1].type() != LayerTypeConvolution || !src[index - 1].convolution().biasTerm() ||
                src[index - 1].convolution().activationType() != ActivationFunctionTypeIdentity)
                return false;
            if (src[index + 0].type() != LayerTypeRelu || src[index + 0].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 1].type() != LayerTypePower || src[index + 1].power().power() != 1.0f || src[index + 1].power().scale() != -1.0f ||
                src[index + 1].power().shift() != 0.0f || src[index + 1].src()[0] != src[index - 1].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeScale || !src[index + 3].scale().biasTerm() || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypeEltwise || src[index + 4].eltwise().operation() != EltwiseOperationTypeSum ||
                !src[index + 4].eltwise().coefficients().empty() || src[index + 4].src() != Strings({ src[index + 0].name(), src[index + 3].name() }))
                return false;
            for (size_t i = index + 5; i < src.size(); ++i)
            {
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (ptrdiff_t k = -1; k < 4; ++k)
                    {
                        if (src[i].src()[j] == src[index + k].name())
                            return false;
                    }
                }
            }
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index - 1].name());
            layer.dst().push_back(layer.name());
            layer.fused().type() = 7;
            layer.weight().push_back(src[index - 1].weight()[1]);
            layer.weight().push_back(src[index + 3].weight()[0]);
            layer.weight().push_back(src[index + 3].weight()[1]);
            changes.push_back(Change(layer.dst()[0], layer.src()[0]));
            layer.dst()[0] = layer.src()[0];
            dst.back().weight().resize(1);
            dst.back().convolution().biasTerm() = false;
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeFused8(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 5)
                return false;
            if (src[index + 0].type() != LayerTypeTile)
                return false;
            if (src[index + 1].type() != LayerTypeTile || src[index + 1].src()[0] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeEltwise || src[index + 2].eltwise().operation() != EltwiseOperationTypeProduct ||
                src[index + 2].src().size() != 2 || src[index + 2].src()[1] != src[index + 1].name())
                return false;
            if (InsideLink(src, index, 3))
                return false;
            if (src[index + 3].type() != LayerTypePooling && src[index + 3].type() != LayerTypeConvolution)
                return false;
            if (src[index + 4].type() != LayerTypeEltwise || src[index + 4].eltwise().operation() != EltwiseOperationTypeSum ||
                src[index + 4].src().size() != 2 || src[index + 4].src()[0] != src[index + 2].name() || src[index + 4].src()[1] != src[index + 3].name())
                return false;

            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index + 4].src()[1]);
            layer.src().push_back(src[index + 2].src()[0]);
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.fused().type() = 8;
            dst.push_back(src[index + 3]);
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeFused9(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 3)
                return false;
            if (src[index + 0].type() != LayerTypeConcat || src[index + 0].src().size() != 2)
                return false;
            if (src[index + 1].type() != LayerTypeScale || src[index + 1].src()[0] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypeRelu || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (InsideLink(src, index + 1, 2))
                return false;

            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 0].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.src().push_back(src[index + 0].src()[1]);
            layer.dst().push_back(src[index + 2].name());
            if (InsideLink(src, index + 0, 2, 2))
                layer.dst().push_back(src[index + 0].name());
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 1].weight()[1]);
            layer.fused().type() = 9;
            dst.push_back(layer);
            index += 2;
            return true;
        }

        bool MergeFused10(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            bool pre = false, scale = false, post = false;
            if (src.size() > index + 0 && src[index + 0].type() == LayerTypePower && src[index + 0].power().power() == 1.0f)
                pre = true;
            if (src.size() > index + 1 && src[index + 1].type() == LayerTypeScale && (pre ? src[index + 1].src()[0] == src[index + 0].name() : true) && src[index + 1].scale().biasTerm())
                scale = true;
            if (src.size() > index + 2 && src[index + 2].type() == LayerTypePower && src[index + 2].power().power() == 1.0f && src[index + 2].src()[0] == src[index + 1].name())
                post = true;
            if (!(scale && (pre || post)))
                return false;
            if (InsideLink(src, index + (pre ? 0 : 1), 1 + (pre ? 1 : 0) + (post ? 1 : 0), 0, LayerTypes({LayerTypePriorBox, LayerTypePriorBoxClustered, LayerTypeMeta})))
                return false;
            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 1].name();
            layer.src().push_back(pre ? src[index + 0].src()[0] : src[index + 1].src()[0]);
            layer.dst().push_back(post ? src[index + 2].dst()[0] : src[index + 1].dst()[0]);
            layer.weight().push_back(src[index + 1].weight()[0]);
            layer.weight().push_back(src[index + 1].weight()[1]);
            layer.fused().floats().push_back(pre ? src[index + 0].power().scale() : 1.0f);
            layer.fused().floats().push_back(pre ? src[index + 0].power().shift() : 0.0f);
            layer.fused().floats().push_back(post ? src[index + 2].power().scale() : 1.0f);
            layer.fused().floats().push_back(post ? src[index + 2].power().shift() : 0.0f);
            layer.fused().type() = 10;
            if(pre)
                changes.push_back(Change(src[index + 0].dst()[0], layer.dst()[0]));
            index += (pre ? 1 : 0) + (post ? 1 : 0);
            dst.push_back(layer);
            return true;
        }

        bool MergeFused11(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 4)
                return false;
            if (src[index + 0].type() != LayerTypePower || src[index + 0].power().power() != 1.0f || 
                src[index + 0].power().scale() != 1.0f)
                return false;
            if (src[index + 1].type() != LayerTypeRestrictRange || src[index + 1].src()[0] != src[index + 0].name())
                return false;
            if (src[index + 2].type() != LayerTypePower || src[index + 2].power().power() != 1.0f || 
                src[index + 2].power().shift() != 0.0f || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeEltwise || src[index + 3].src().size() != 2 ||
                src[index + 3].src()[0] != src[index + 0].src()[0] || src[index + 3].src()[1] != src[index + 2].name() ||
                src[index + 3].eltwise().operation() != EltwiseOperationTypeProduct)
                return false;
            if (InsideLink(src, index + 1, 3))
                return false;

            LayerParam layer;
            layer.type() = LayerTypeFused;
            layer.name() = src[index + 3].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.fused().type() = 11;
            layer.fused().floats().push_back(src[index + 0].power().shift());
            layer.fused().floats().push_back(src[index + 1].restrictRange().lower());
            layer.fused().floats().push_back(src[index + 1].restrictRange().upper());
            layer.fused().floats().push_back(src[index + 2].power().scale());
            dst.push_back(layer);
            index += 3;
            return true;
        }

        bool MergePooling(const LayerParams & src, size_t & index, LayerParams & dst, Changes & changes)
        {
            if (src.size() < index + 5)
                return false;
            if (src[index + 0].type() != LayerTypeReshape)
                return false;
            if (src[index + 1].type() != LayerTypePooling || src[index + 1].src()[0] != src[index + 0].name() || src[index + 1].pooling().kernel()[1] != 1)
                return false;
            if (src[index + 2].type() != LayerTypeReshape || src[index + 2].src()[0] != src[index + 1].name())
                return false;
            if (src[index + 3].type() != LayerTypeReshape || src[index + 3].src()[0] != src[index + 2].name())
                return false;
            if (src[index + 4].type() != LayerTypePooling || src[index + 4].src()[0] != src[index + 3].name() || src[index + 4].pooling().kernel()[1] != 1)
                return false;
            if (InsideLink(src, index + 1, 4))
                return false;

            LayerParam layer;
            layer.type() = LayerTypePooling;
            layer.name() = src[index + 4].name();
            layer.src().push_back(src[index + 0].src()[0]);
            layer.dst().push_back(layer.name());
            layer.pooling().method() = src[index + 4].pooling().method();
            layer.pooling().kernel() = Shape({ src[index + 1].pooling().kernel()[0], src[index + 4].pooling().kernel()[0] });
            layer.pooling().pad() = src[index + 4].pooling().pad();
            layer.pooling().stride() = src[index + 4].pooling().stride();
            layer.pooling().excludePad() = src[index + 4].pooling().excludePad();
            dst.push_back(layer);
            index += 4;
            return true;
        }

        bool MergeRnnGruBd(const LayerParams& src, size_t& index, LayerParams& dst, Changes& changes)
        {
            const size_t RNN_GRU_BD_SIZE = 19;
            if (index == 0 || index + RNN_GRU_BD_SIZE >= src.size())
                return false;
            const LayerParam& parent = src[index - 1];
            if (parent.type() != LayerTypeTensorIterator || parent.src().size() != 2 || 
                parent.dst().size() != 1 || parent.tensorIterator().back().size() != 1)
                return false;
            for (size_t i = 0; i < RNN_GRU_BD_SIZE; ++i)
            {
                if (src[index + i].parent() != parent.name())
                    return false;
            }
            if (src[index + 0].type() != LayerTypeInput || src[index + 1].type() != LayerTypeMeta)
                return false;
            if (src[index + 2].type() != LayerTypeSqueeze || src[index + 3].type() != LayerTypeInput)
                return false;
            if (src[index + 4].type() != LayerTypeConcat || src[index + 5].type() != LayerTypeInnerProduct || src[index + 5].weight().size() != 2)
                return false;
            if (src[index + 6].type() != LayerTypeSigmoid || src[index + 7].type() != LayerTypeUnpack)
                return false;
            if (src[index + 8].type() != LayerTypeEltwise || src[index + 9].type() != LayerTypePower)
                return false;
            if (src[index + 10].type() != LayerTypeEltwise || src[index + 11].type() != LayerTypeConcat)
                return false;
            if (src[index + 12].type() != LayerTypeInnerProduct || src[index + 12].weight().size() != 2 || src[index + 13].type() != LayerTypeUnaryOperation)
                return false;
            if (src[index + 14].type() != LayerTypeEltwise || src[index + 15].type() != LayerTypeEltwise)
                return false;
            if (src[index + 16].type() != LayerTypeStub || src[index + 17].type() != LayerTypeExpandDims || src[index + 18].type() != LayerTypeStub)
                return false;
            if (!src[index + RNN_GRU_BD_SIZE].parent().empty())
                return false;

            dst.push_back(src[index + 0]);
            dst.push_back(src[index + 3]);

            LayerParam layer;
            layer.type() = LayerTypeRnnGruBd;
            layer.parent() = parent.name();
            layer.name() = parent.name() + "_RnnGruBd";
            layer.src().push_back(src[index + 0].dst()[0]);
            layer.src().push_back(src[index + 3].dst()[0]);
            layer.dst().push_back(src[index + 18].dst()[0]);
            layer.dst().push_back(src[index + 16].dst()[0]);
            layer.weight().push_back(src[index + 5].weight()[0]);
            layer.weight().push_back(src[index + 5].weight()[1]);
            layer.weight().push_back(src[index + 12].weight()[0]);
            layer.weight().push_back(src[index + 12].weight()[1]);
            dst.push_back(layer);

            index += RNN_GRU_BD_SIZE - 1;
            return true;
        }

        bool IsSub(const LayerParam & layer) const
        {
            if (layer.type() == LayerTypeEltwise && layer.eltwise().operation() == EltwiseOperationTypeSum && layer.eltwise().coefficients() == Floats({ 1.0f, -1.0f }))
                return true;
            if (layer.type() == LayerTypeBinaryOperation && layer.binaryOperation().type() == BinaryOperationTypeSub)
                return true;
            return false;
        }

        bool InsideLink(const LayerParams & src, size_t start, size_t count, size_t skip = 0, const LayerTypes & ignored = LayerTypes()) const
        {
            for (size_t i = start + count + skip; i < src.size(); ++i)
            {
                bool ignore = false;
                for (size_t j = 0; j < ignored.size(); ++j)
                    if (src[i].type() == ignored[j])
                        ignore = true;
                if (ignore)
                    continue;
                for (size_t j = 0; j < src[i].src().size(); ++j)
                {
                    for (size_t k = 0; k < count - 1; ++k)
                    {
                        if (src[i].src()[j] == src[start + k].name())
                            return true;
                    }
                }
            }
            return false;
        }

        bool Equal(float a, float b, float e = 0.000001f)
        {
            return abs(a - b) < e;
        }

        bool Rename(const Change & change, LayerParams & layers)
        {
            for (size_t i = 0; i < layers.size(); ++i)
            {
                for (size_t j = 0; j < layers[i].src().size(); ++j)
                {
                    if (layers[i].src()[j] == change.first)
                    {
                        if (layers[i].src()[0] == layers[i].dst()[0] && layers[i].src().size() == 1)
                            layers[i].dst()[0] = change.second;
                        layers[i].src()[j] = change.second;
                    }
                }
            }
            return true;
        }

        bool Rename(const Changes & changes, LayerParams & layers)
        {
            for (size_t k = 0; k < changes.size(); ++k)
            {
                if (!Rename(changes[k], layers))
                    return false;
            }
            return true;
        }

        size_t Users(const String& name, const LayerParams& layers, size_t start, const String & parent) const
        {
            size_t users = 0;
            for (size_t i = start; i < layers.size(); ++i)
            {
                if (layers[i].parent() != parent)
                    continue;
                for (size_t j = 0; j < layers[i].src().size(); ++j)
                {
                    if (layers[i].src()[j] == name)
                        users++;
                }
            }
            return users;
        }

        const LayerParam * GetLayer(const String& name, const LayerParams& layers) const
        {
            for (size_t i = 0; i < layers.size(); ++i)
            {
                if (layers[i].name() == name)
                    return &layers[i];
            }
            return NULL;
        }

        bool CanReuse(const LayerParam & layer)
        {
            if (layer.type() == LayerTypeSigmoid)
                return true;
            if (layer.type() == LayerTypeScale)
                return true;
            if (layer.type() == LayerTypeEltwise)
                return true;
            if (layer.type() == LayerTypeRelu)
                return true;
            if (layer.type() == LayerTypeSqueezeExcitation)
                return true;
            if (layer.type() == LayerTypePooling && layer.pooling().method() == PoolingMethodTypeMax && 
                layer.pooling().kernel() == Shp(1, 1) && layer.pooling().stride() == Shp(1, 1))
                return true;
            return false;
        }

        bool HasOutput(const Synet::NetworkParam& network, const LayerParam & layer)
        {
            for (size_t l = 0; l < layer.dst().size(); ++l)
                for (size_t d = 0; d < network.dst().size(); ++d)
                    if (layer.dst()[l] == network.dst()[d])
                        return true;
            return false;
        }

        bool ReuseLayers(Synet::NetworkParam& network)
        {
            if (network.quantization().method() != QuantizationMethodUnknown)
                return true;
            LayerParams & layers = network.layers();
            for (size_t i = 0; i < layers.size(); ++i)
            {
                LayerParam & layer = layers[i];
                if (layer.src().empty())
                    continue;
                if (Users(layer.src()[0], layers, i + 1, "") > 0)
                    continue;
                if (i && layer.src()[0] == layers[i - 1].name() && layers[i - 1].type() == LayerTypeConst)
                    continue;
                if (Users(layer.dst()[0], layers, i + 1, "") == 0)
                    continue;
                if (HasOutput(network, layer))
                    continue;
                if (!CanReuse(layer))
                    continue;
                if (!Rename(Change(layer.dst()[0], layer.src()[0]), layers))
                    return false;
                layer.dst()[0] = layer.src()[0];
            }
            return true;
        }

        bool IsStub(const LayerParam& layer, const Synet::NetworkParam& network)
        {
            if (layer.type() == LayerTypeStub)
            {
                if (Users(layer.dst()[0], network.layers(), 0, layer.parent()) > 0)// && !HasOutput(network, layer))
                    return true;
                LayerType type = GetLayer(layer.src()[0], network.layers())->type();
                if (type == LayerTypeDetectionOutput)
                    return true;
            }
            if (layer.type() == LayerTypePooling && layer.pooling().method() == PoolingMethodTypeMax &&
                layer.pooling().kernel() == Shp(1, 1) && layer.pooling().stride() == Shp(1, 1))
                return true;
            return false;
        }

        bool RemoveStub(Synet::NetworkParam& network)
        {
            LayerParams& layers = network.layers();
            for (size_t i = 1; i < layers.size(); ++i)
            {
                LayerParam & layer = layers[i];
                if (!IsStub(layer, network))
                    continue;
                if (layer.src().size() != 1 || layer.dst().size() != 1)
                    continue;
                if (!Rename(Change(layer.dst()[0], layer.src()[0]), layers))
                    return false;
                layers.erase(layers.begin() + i);
            }
            return true;
        }
    };

    inline bool OptimizeSynetModel(const String& srcXml, const String& srcBin, const String& dstXml, const String & dstBin)
    {
        NetworkParamHolder network;
        if (!network.Load(srcXml))
        {
            std::cout << "Can't load Synet model '" << srcXml << "' !" << std::endl;
            return false;
        }
        Floats bin;
        if (!srcBin.empty() && !LoadBinaryData(srcBin, bin))
        {
            std::cout << "Can't load Synet weight '" << srcBin << "' !" << std::endl;
            return false;
        }
        OptimizerParamHolder param;
        Optimizer optimizer(param());
        if (!optimizer.Run(network(), bin))
        {
            std::cout << "Can't optimize Synet model!" << std::endl;
            return false;
        }
        if (!network.Save(dstXml, false))
        {
            std::cout << "Can't save Synet model '" << dstXml << "' !" << std::endl;
            return false;
        }
        if (!dstBin.empty() && !SaveBinaryData(bin, dstBin))
        {
            std::cout << "Can't save Synet weight '" << dstBin << "' !" << std::endl;
            return false;
        }
        return true;
    }
}