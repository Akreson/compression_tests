#include "ac/ac_bit.cpp"
#include "ac/ac_byte.cpp"

#if 0
using ArithEncoder = ArithBitEncoder;
using ArithDecoder = ArithBitDecoder;
#else
using ArithEncoder = ArithByteEncoder;
using ArithDecoder = ArithByteDecoder;
#endif

#include "ac_models/basic_ac.cpp"
#include "ac_models/ppm_ac.cpp"

template <typename TypeEncoder, b32 UseShift = false> void
CompressStaticACFile(u16* ByteCumFreq, file_data& InputFile, ByteVec& OutBuffer)
{
	TypeEncoder Encoder(OutBuffer);
	
	prob Prob;
	Prob.scale = ByteCumFreq[256];

	if constexpr (UseShift)
	{
		Assert(IsPowerOf2(Prob.scale));
		Prob.scale = FindMostSignificantSetBit32(Prob.scale);
	}

	Timer Timer;
	AccumTime Accum;
	for (u32 Run = 0; Run < RUNS_COUNT; Run++)
	{
		Timer.start();

		for (u32 i = 0; i < InputFile.Size; ++i)
		{
			u8 Byte = InputFile.Data[i];
			Prob.lo = ByteCumFreq[Byte];
			Prob.hi = ByteCumFreq[Byte + 1];

			if constexpr (UseShift)
			{
				Encoder.encodeShift(Prob);
			}
			else
			{
				Encoder.encode(Prob);
			}

			Encoder.normalize();
		}

		Encoder.flush();

		Timer.end();
		Accum.update(Timer);

		if ((Run + 1) < RUNS_COUNT)
		{
			Encoder.reset();
		}
	}

	PrintAvgPerSymbolPerfStats(Accum, RUNS_COUNT, InputFile.Size);
}

template <typename TypeDecoder, b32 UseShift = false> void
DecompressStaticACFile(u16* ByteCumFreq, file_data& OutputFile, ByteVec& InputBuffer, file_data& InputFile)
{
	TypeDecoder Decoder(InputBuffer);

	prob Prob;
	Prob.scale = ByteCumFreq[256];
	
	u32 ShiftScale;
	if constexpr (UseShift)
	{
		Assert(IsPowerOf2(Prob.scale));
		ShiftScale = FindMostSignificantSetBit32(Prob.scale);
	}

	Timer Timer;
	AccumTime Accum;
	for (u32 Run = 0; Run < RUNS_COUNT; Run++)
	{
		Timer.start();

		for (u64 ByteIndex = 0; ByteIndex < OutputFile.Size; ByteIndex++)
		{
			u32 DecodedFreq;
			if constexpr (UseShift)
			{
				DecodedFreq = Decoder.getCurrFreqShift(ShiftScale);
			}
			else
			{
				DecodedFreq = Decoder.getCurrFreq(Prob.scale);
			}

			u32 DecodedSymbol = 0;
			for (u32 i = 0; i < 256; ++i)
			{
				if (DecodedFreq < ByteCumFreq[i + 1])
				{
					DecodedSymbol = i;

					Prob.lo = ByteCumFreq[i];
					Prob.hi = ByteCumFreq[i + 1];
					break;
				}
			}

			Decoder.updateDecodeRange(Prob);

			Assert(InputFile.Data[ByteIndex] == DecodedSymbol);
			OutputFile.Data[ByteIndex] = DecodedSymbol;
		}

		Timer.end();
		Accum.update(Timer);

		Decoder.reset();
	}

	PrintAvgPerSymbolPerfStats(Accum, RUNS_COUNT, InputFile.Size);
}

void
TestStaticAC(file_data& InputFile)
{
	PRINT_TEST_FUNC();

	u32 Freq[256] = {};
	CountByte(Freq, InputFile.Data, InputFile.Size);

	u16 NormFreq[256];
	u16 CumFreq[257];
	
	ZeroSize(NormFreq, sizeof(NormFreq));
	ZeroSize(CumFreq, sizeof(CumFreq));

	OptimalNormalize(Freq, NormFreq, InputFile.Size, 256, 1 << 14);
	CalcCumFreq(NormFreq, CumFreq, 256);

	ByteVec CompressBuffer;
	file_data OutputFile;
	OutputFile.Size = InputFile.Size;
	OutputFile.Data = new u8[OutputFile.Size];

#if 0
	printf(" - ArithBitEncoder\n");
	CompressStaticACFile<ArithBitEncoder>(CumFreq, InputFile, CompressBuffer);
	u64 CompressedSize = CompressBuffer.size();
	PrintCompressionSize(InputFile.Size, CompressedSize);
	DecompressStaticACFile<ArithBitDecoder>(CumFreq, OutputFile, CompressBuffer, InputFile);
	CompressBuffer.clear();

	printf(" - ArithByteEncoder\n");
	CompressStaticACFile<ArithByteEncoder>(CumFreq, InputFile, CompressBuffer);
	CompressedSize = CompressBuffer.size();
	PrintCompressionSize(InputFile.Size, CompressedSize);
	DecompressStaticACFile<ArithByteDecoder>(CumFreq, OutputFile, CompressBuffer, InputFile);
#else
	printf(" - ArithBitEncoder\n");
	CompressStaticACFile<ArithBitEncoder, true>(CumFreq, InputFile, CompressBuffer);
	u64 CompressedSize = CompressBuffer.size();
	PrintCompressionSize(InputFile.Size, CompressedSize);
	DecompressStaticACFile<ArithBitDecoder, true>(CumFreq, OutputFile, CompressBuffer, InputFile);
	CompressBuffer.clear();

	printf(" - ArithByteEncoder\n");
	CompressStaticACFile<ArithByteEncoder, true>(CumFreq, InputFile, CompressBuffer);
	CompressedSize = CompressBuffer.size();
	PrintCompressionSize(InputFile.Size, CompressedSize);
	DecompressStaticACFile<ArithByteDecoder, true>(CumFreq, OutputFile, CompressBuffer, InputFile);
#endif

	delete[] OutputFile.Data;
}

void
CompressFile(BasicByteModel& Model, file_data& InputFile, ByteVec& OutBuffer)
{
	ArithEncoder Encoder(OutBuffer);

	for (u32 i = 0; i < InputFile.Size; ++i)
	{
		prob SymbolProb = Model.getProb(InputFile.Data[i]);
		Encoder.encode(SymbolProb);
		Encoder.normalize();
		Model.update(InputFile.Data[i]);
	}

	prob SymbolProb = Model.getEndStreamProb();
	Encoder.encode(SymbolProb);
	Encoder.flush();
}

void
DecompressFile(BasicByteModel& Model, file_data& OutputFile, ByteVec& InputBuffer)
{
	ArithDecoder Decoder(InputBuffer);

	u64 ByteIndex = 0;
	for (;;)
	{
		u32 DecodedFreq = Decoder.getCurrFreq(Model.getCount());

		u32 DecodedSymbol;
		prob Prob = Model.getByteFromFreq(DecodedFreq, &DecodedSymbol);

		if (DecodedSymbol == BasicByteModel::EndOfStreamSymbolIndex) break;

		Decoder.updateDecodeRange(Prob);
		Model.update(DecodedSymbol);

		Assert(ByteIndex <= OutputFile.Size);
		OutputFile.Data[ByteIndex++] = DecodedSymbol;
	}
}

void
TestACBasicModel(file_data& InputFile)
{
	PRINT_TEST_FUNC();

	BasicByteModel Model;
	ByteVec CompressBuffer;

	CompressFile(Model, InputFile, CompressBuffer);
	u64 CompressedSize = CompressBuffer.size();

	PrintCompressionSize(InputFile.Size, CompressedSize);

	Model.reset();

	file_data OutputFile;
	OutputFile.Size = InputFile.Size;
	OutputFile.Data = new u8[OutputFile.Size];

	DecompressFile(Model, OutputFile, CompressBuffer);

	for (u32 i = 0; i < InputFile.Size; ++i)
	{
		b32 cmp = InputFile.Data[i] == OutputFile.Data[i];
		Assert(cmp);
	}

	delete[] OutputFile.Data;

	printf("\n");
}

void
CompressFile(PPMByte& Model, file_data& InputFile, ByteVec& OutBuffer)
{
	ArithEncoder Encoder(OutBuffer);

	for (u32 i = 0; i < InputFile.Size; ++i)
	{
		Model.encode(Encoder, InputFile.Data[i]);

		if (!(i & 0xffff))
		{
			//printf("%d %d %d %d\n", i, Model.SubAlloc.FreeListCount, Model.SubAlloc.FreeMem >> 10, Model.SubAlloc.FreeMem >> 20);
			printf("%d\r", i);
		}
	}

	Model.encodeEndOfStream(Encoder);
	Encoder.flush();
}

void
DecompressFile(PPMByte& Model, file_data& OutputFile, ByteVec& InputBuffer, file_data& InputFile)
{
	ArithDecoder Decoder(InputBuffer);

	u64 ByteIndex = 0;
	for (;;)
	{
		u32 DecodedSymbol = Model.decode(Decoder);
		if (DecodedSymbol == PPMByte::EscapeSymbol) break;

		Assert(ByteIndex <= OutputFile.Size);
		Assert(InputFile.Data[ByteIndex] == DecodedSymbol)
		OutputFile.Data[ByteIndex++] = DecodedSymbol;

		if (!(ByteIndex & 0xffff))
		{
			//printf("%d %d %d %d\r", ByteIndex, Model.SubAlloc.FreeListCount, Model.SubAlloc.FreeMem >> 10, Model.SubAlloc.FreeMem >> 20);
			printf("%lu\r", ByteIndex);
		}
	}
}

void
TestPPMModel(file_data& InputFile)
{
	PRINT_TEST_FUNC();

	u32 Order = 4;
	u32 MemLimit = 20 << 20;
	printf(" MemLim: %u Order: %u\n", MemLimit, Order);

	PPMByte PPMModel(Order, MemLimit);
	ByteVec CompressBuffer;

	f64 StartTime = timer();
	CompressFile(PPMModel, InputFile, CompressBuffer);
	f64 EndTime = timer() - StartTime;
	printf("EncTime %.3f\n", EndTime);

	u64 CompressedSize = CompressBuffer.size();
	PrintCompressionSize(InputFile.Size, CompressedSize);

#ifdef _DEBUG
	printf("Sym: %.3f | Esc: %.3f\n", PPMModel.SymEnc, PPMModel.EscEnc);
#endif

	PPMModel.reset();

#if 1
	file_data OutputFile;
	OutputFile.Size = InputFile.Size;
	OutputFile.Data = new u8[OutputFile.Size];

	StartTime = timer();
	DecompressFile(PPMModel, OutputFile, CompressBuffer, InputFile);
	EndTime = timer() - StartTime;
	printf("DecTime %.3f\n", EndTime);

	delete[] OutputFile.Data;
#endif

	printf("\n");
}