/*
* Tests for Synet Framework (http://github.com/ermig1979/Synet).
*
* Copyright (c) 2018-2020 Yermalayeu Ihar.
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

#include "TestCompare.h"
#include "TestReport.h"

#ifdef SYNET_TEST_FIRST_RUN

#define SYNET_ONNX_ENABLE
#include "Synet/Converters/Onnx.h"

#include "TestInferenceEngine.h"

#else //SYNET_FIRST_RUN
namespace Test
{
    struct InferenceEngineNetwork : public Network
    {
    };
}
#endif//SYNET_FIRST_RUN

Test::PerformanceMeasurerStorage Test::PerformanceMeasurerStorage::s_storage;

int main(int argc, char* argv[])
{
    Test::Options options(argc, argv);

#ifdef SYNET_ONNX_ENABLE
    if (options.mode == "convert")
    {
        SYNET_PERF_FUNC();
        std::cout << "Convert network from Onnx to Synet : ";
        options.result = Synet::ConvertOnnxToSynet(options.firstModel, options.firstWeight, options.tensorFormat == 1, options.secondModel, options.secondWeight);
        std::cout << (options.result ? "OK." : " Conversion finished with errors!") << std::endl;
    }
    else 
#endif
    if (options.mode == "compare")
    {
        Test::Comparer<Test::InferenceEngineNetwork, Test::SynetNetwork> comparer(options);
        options.result = comparer.Run();
    }
    else
        std::cout << "Unknown mode : " << options.mode << std::endl;

    return options.result ? 0 : 1;
}


