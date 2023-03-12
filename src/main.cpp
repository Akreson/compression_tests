#define _CRT_SECURE_NO_WARNINGS

#include <cmath>
#include "common.h"
#include "suballoc.cpp"

#include "ac_tests.cpp"
#include "ans_tests.cpp"

int
main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("file name missed");
		exit(0);
	}

	//file_data InputFile = ReadFile(argv[1]);
	std::vector<file_data> InputArr;
	ReadTestFiles(InputArr, argv[1]);

	for (auto& InputFile : InputArr)
	{
		printf("%s\n", InputFile.Name.c_str());
		TestACBasicModel(InputFile);

		//TestACBasicModel(InputFile);
		//TestPPMModel(InputFile);
	
		//TestBasicRans8(InputFile);
		//TestBasicRans32(InputFile);
		//TestFastEncodeRans8(InputFile);
		//TestFastEncodeRans32(InputFile);
		//TestTableDecodeRans16(InputFile);
		//TestTableInterleavedRans16(InputFile);
		//TestTableInterleavedRans32(InputFile);
		//TestSIMDDecodeRans16(InputFile);
		//TestNormalizationRans32(InputFile);
		//TestPrecomputeAdaptiveOrder1Rans32(InputFile);
	}

	return 0;
}