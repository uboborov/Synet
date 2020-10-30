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

#include "TestClassificationPrecision.h"
#include "TestDetectionPrecision.h"
#include "TestReidentificationPrecision.h"

Test::PerformanceMeasurerStorage Test::PerformanceMeasurerStorage::s_storage;

int main(int argc, char* argv[])
{
    Test::Precision::Options options(argc, argv);

    if (options.mode == "classification")
    {
        Test::ClassificationPrecision precision(options);
        options.result = precision.Run();
    }
    else if (options.mode == "detection")
    {
        Test::DetectionPrecision precision(options);
        options.result = precision.Run();
    }
    else if (options.mode == "reidentification")
    {
        Test::ReidentificationPrecision precision(options);
        options.result = precision.Run();
    }
    else
    {
        std::cout << "Unknown mode: " << options.mode << " !" << std::endl;
        return 2;
    }

    return options.result ? 0 : 1;
}