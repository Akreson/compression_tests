#define _CRT_SECURE_NO_WARNINGS

#include <cmath>
#include "common.h"
#include "mem.cpp"
#include "suballoc.cpp"

#include "renorm.cpp"
#include "huff_tests.cpp"
#include "ac_tests.cpp"
#include "ans_tests.cpp"

#include <cstdlib>
#include <ctime>

int
main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("file name missed\n");
		exit(0);
	}
	
	std::vector<file_data> InputArr;
	ReadTestFiles(InputArr, argv[1]);

	for (auto& InputFile : InputArr)
	{
		size_t ByteCount[256] = {};
		CountByte(ByteCount, InputFile.Data, InputFile.Size);
		f64 FileByteH = Entropy(ByteCount, 256);
		printf("---------- %s %lu H:%.3f\n", InputFile.Name.c_str(), InputFile.Size, FileByteH);

		//TestHuffDefault1(InputFile);

		//TestStaticAC(InputFile);
		//TestACBasicModel(InputFile);
		//TestPPMModel(InputFile);
	
		TestBasicRans8(InputFile);
		TestBasicRans32(InputFile);
		TestFastEncodeRans8(InputFile);
		TestFastEncodeRans32(InputFile);
		TestTableDecodeRans16(InputFile);
		TestTableInterleavedRans16(InputFile);
		TestTableInterleavedRans32(InputFile);
		TestSIMDDecodeRans16(InputFile);
		TestNormalizationRans32(InputFile);
		TestPrecomputeAdaptiveOrder1Rans32(InputFile);

		TestBasicTans(InputFile);
		//TestBasicTans<false>(InputFile);
		TestInterleavedTans(InputFile);
		//TestInterleavedTans<false>(InputFile);

		printf("\n");
	}

	return 0;
}