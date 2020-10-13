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

#pragma once

#include "TestPrecision.h"

namespace Test
{
	class DetectionPrecision : public Precision
	{
	public:
		DetectionPrecision(const Options& options)
			: Precision(options)
		{
		}

	private:

		struct Test
		{
			bool skip;
			String name, path;
			Regions detected, control;
		};

		typedef std::vector<Test> Tests;
		typedef std::shared_ptr<Test> TestPtr;
		typedef std::vector<TestPtr> TestPtrs;
		typedef std::set<String> StringSet;

		Tests _tests;
		StringSet _list;

		bool LoadListFile()
		{
			_list.clear();
			String path = _options.testList;
			std::ifstream ifs(path);
			if (!ifs.is_open())
			{
				std::cout << "Can't open file '" << path << "' !" << std::endl;
				return false;
			}
			while (!ifs.eof())
			{
				String name;
				ifs >> name;
				if (!name.empty())
					_list.insert(name);
			}
			return true;
		}

		bool SaveListFile()
		{
			String path = _options.testList;
			std::ofstream ofs(path);
			if (!ofs.is_open())
			{
				std::cout << "Can't open file '" << path << "' !" << std::endl;
				return false;
			}
			for (size_t i = 0; i < _tests.size(); ++i)
			{
				if (_tests[i].skip)
					continue;
				ofs << _tests[i].name << std::endl;
			}
			return true;
		}

		bool ParseIndexFile()
		{
			String path = MakePath(_options.imageDirectory, _options.indexFile);
			std::ifstream ifs(path);
			if (!ifs.is_open())
			{
				std::cout << "Can't open file '" << path << "' !" << std::endl;
				return false;
			}
			while (!ifs.eof())
			{
				Test test;
				ifs >> test.name;
				if (test.name.empty())
					break;
				test.path = MakePath(_options.imageDirectory, test.name);
				if (!FileExists(test.path))
				{
					std::cout << "Image '" << test.path << "' is not exists!" << std::endl;
					return false;
				}
				size_t number;
				ifs >> number;
				for (size_t i = 0; i < number; ++i)
				{
					Region region;
					ifs >> region.x >> region.y >> region.w >> region.h >> region.id;
					region.x += region.w / 2;
					region.y += region.h / 2;
					for (size_t j = 0, stub; j < 5; ++j)
						ifs >> stub;
					if(region.id >= 0 && region.id <= 2)
						test.control.push_back(region);
				}
				if(_list.empty() || _list.find(test.name) != _list.end())
					_tests.push_back(test);
			}
			_options.testNumber = _tests.size();
			return true;
		}

		virtual bool LoadTestList()
		{
			if (!LoadListFile())
				return false;
			if (!ParseIndexFile())
				return false;
			return true;
		}

		Size ToSize(const Shape& shape) const
		{
			return Size(shape[3], shape[2]);
		}

		bool BadRatio(const Size & netSize, const Size& imgSize) const
		{
			float netRatio = float(netSize.x) / float(netSize.y);
			float imgRatio = float(imgSize.x) / float(imgSize.y);
			float ratioVariation = Synet::Max(netRatio, imgRatio) / Synet::Min(netRatio, imgRatio) - 1.0f;
			return ratioVariation > _options.ratioVariation;
		}

		virtual bool PerformBatch(size_t thread, size_t current, size_t batch)
		{
			if (_options.batchSize != 1)
			{
				std::cout << "Batch size can be only 1 for detection tests!" << std::endl;
				return false;
			}

			Thread& t = _threads[thread];
			Size netSize = ToSize(t.input[0].Shape());
			Test & test = _tests[current];
			Size imgSize;
			if (!SetInput(test.path, t.input[0], 0, &imgSize))
				return false;

			test.skip = BadRatio(netSize, imgSize);
			if (test.skip)
				return true;

			t.output = t.network->Predict(t.input);
			test.detected = t.network->GetRegions(imgSize, 0.25f, 0.5f);

			return true;
		}

		void Annotate(const Region& region, uint32_t color, View& image)
		{
			ptrdiff_t l = ptrdiff_t(region.x - region.w / 2);
			ptrdiff_t t = ptrdiff_t(region.y - region.h / 2);
			ptrdiff_t r = ptrdiff_t(region.x + region.w / 2);
			ptrdiff_t b = ptrdiff_t(region.y + region.h / 2);
			Simd::DrawRectangle(image, l, t, r, b, color);
		}

		bool Annotate(const Test& test)
		{
			View image;
			if (!LoadImage(test.path, image))
			{
				std::cout << "Can't read '" << test.path << "' image!" << std::endl;
				return false;
			}
			for (size_t j = 0; j < test.detected.size(); ++j)
				Annotate(test.detected[j], 0xFFFF0000, image);
			for (size_t j = 0; j < test.control.size(); ++j)
				Annotate(test.control[j], 0xFF00FF00, image);
			String path = MakePath(_options.outputDirectory, GetNameByPath(test.name));
			if (!SaveImage(image, path))
			{
				std::cout << "Can't write '" << path << "' image!" << std::endl;
				return false;
			}
			return true;
		}

		virtual bool ProcessResult()
		{
			SaveListFile();
			typedef std::pair<float, int> Pair;
			std::vector<Pair> detections;
			size_t total = 0;
			for (size_t i = 0; i < _tests.size(); ++i)
			{
				const Test& test = _tests[i];
				total += test.control.size();
				for (size_t j = 0; j < test.detected.size(); ++j)
				{
					bool found = false;
					for (size_t k = 0; k < test.control.size(); ++k)
					{
						float overlap = Synet::Overlap(test.detected[j], test.control[k]);
						if (overlap > 0.50f)
							found = true;
					}
					detections.push_back(Pair(test.detected[j].prob, found ? 1 : -1));
				}
				if(_options.annotateRegions)
					Annotate(test);
			}
			std::sort(detections.begin(), detections.end(), [](const Pair& a, const Pair& b) {return a.first < b.first; });

			return true;
		}
	};
}


