#if !defined(COMMON_H)
#define COMMON_H

#include <fstream>
#include <cstdio>
#include <cassert>
#include <vector>
#include <limits>
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef float f32;
typedef double f64;

typedef u32 b32;
typedef u16 b16;
typedef u8 b8;

static constexpr u32 PtrAlign = sizeof(void*);
static constexpr u32 MaxUInt8 = std::numeric_limits<u8>::max();
static constexpr u32 MaxUInt16 = std::numeric_limits<u16>::max();
static constexpr u32 MaxUInt32 = std::numeric_limits<u32>::max();
static constexpr u64 MaxUInt64 = std::numeric_limits<u64>::max();

static constexpr f64 MaxF64 = std::numeric_limits<f64>::max();

static constexpr u32 RUNS_COUNT = 10;

#ifdef _DEBUG
	//#define Assert(Expression) assert(Expression)
#define Assert(Expression) if (!(Expression)) *((int *)0) = 0;
#else
#define Assert(Expression)
#endif

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

template<typename T, u32 Dim0, u32 Dim1>
struct array2d
{
	static constexpr u32 d0 = Dim0;
	static constexpr u32 d1 = Dim1;

	T E[Dim0][Dim1];
};

#if _MSC_VER

#define ALIGN(type, name, N) __declspec(align(N)) type name
#include <intrin.h>

static inline u64
MulHi64(u64 a, u64 b)
{
	return __umulh(a, b);
}

inline u32
FindMostSignificantSetBit32(u32 Source)
{
	u32 Result;
	u32 S = _BitScanReverse((unsigned long*)&Result, Source);
	return S ? Result : 0;
}

inline u32
FindLeastSignificantSetBit32(u32 Source)
{
	u32 Result;
	u32 S = _BitScanForward((unsigned long*)&Result, Source);

	return Result;
}

#elif defined(__GNUC__)
#include <x86intrin.h>

#define ALIGN(type, name, N) type name __attribute__ ((aligned(N)))

static inline u64
MulHi64(u64 a, u64 b)
{
    return (u64)(((unsigned __int128)a * b) >> 64);
}

inline u32
FindMostSignificantSetBit32(u32 Source)
{
	u32 Result = 31 - __builtin_clz(Source);
	return Result;
}

inline u32
FindLeastSignificantSetBit32(u32 Source)
{	
	#error "Function not implemented"
}

#endif

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
static inline f64
timer()
{
	LARGE_INTEGER ctr, freq;
	QueryPerformanceCounter(&ctr);
	QueryPerformanceFrequency(&freq);
	return 1.0 * ctr.QuadPart / freq.QuadPart;
}

#elif defined(__linux__)

#define __STDC_FORMAT_MACROS
#include <time.h>
#include <inttypes.h>

static inline f64
timer()
{
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    u32 status = clock_gettime(CLOCK_MONOTONIC, &ts);
    Assert(status == 0);
    return double(ts.tv_sec) + 1.0e-9 * double(ts.tv_nsec);
}

#endif

struct Timer
{
	f64 StartTime;
	u64 StartClock;

	f64 Time;
	u64 Clock;

	Timer()
	{
		reset();
	}

	inline void start()
	{
		StartTime = timer();
		StartClock = __rdtsc();
	}

	inline void end()
	{
		Time = timer() - StartTime;
		Clock = __rdtsc() - StartClock;
	}

	inline void reset()
	{
		StartClock = Clock = 0;
		StartTime = Time = 0.0;
	}
};

struct AccumTime
{
	u64 Clock;
	f64 Time;

	u64 MinClock;
	f64 MinTime;

	u64 MaxClock;
	f64 MaxTime;

	AccumTime()
	{
		reset();
	};

	inline void update(u64 StepClock, f64 StepTime)
	{
		Clock += StepClock;
		Time += StepTime;

		MinClock = MinClock > StepClock ? StepClock : MinClock;
		MinTime = MinTime > StepTime ? StepTime : MinTime;

		MaxClock = MaxClock > StepClock ? MaxClock : StepClock;
		MaxTime = MaxTime > StepTime ? MaxTime : StepTime;
	}

	inline void update(Timer& Timer)
	{
		update(Timer.Clock, Timer.Time);
	}

	inline void avg(size_t Count)
	{
		Clock /= Count;
		Time /= (f64)Count;
	}

	inline void reset()
	{
		Clock = MaxClock = 0;
		Time = MaxTime = 0.0;

		MinClock = MaxUInt64;
		MinTime = MaxF64;
	}
};

inline b32
IsPowerOf2(u32 Value)
{
	b32 Result = !(Value & (Value - 1));
	return Result;
}

inline constexpr u64
AlignSizeForward(u64 Size, u32 Alignment = PtrAlign)
{
	Assert(!(Alignment & (Alignment - 1)));

	u64 Result = Size;
	u32 AlignMask = Alignment - 1;
	u32 OffsetFromMask = (Size & AlignMask);
	u32 AlignOffset = OffsetFromMask ? (Alignment - OffsetFromMask) : 0;

	Result += AlignOffset;
	return Result;
}

struct file_data
{
	u8* Data;
	size_t Size;
	std::string Name;
};

#include <iostream>
#include <filesystem>

file_data
ReadFile(const char* Name)
{
	file_data Result = {};

	FILE* f = fopen(Name, "rb");
	if (!f)
	{
		printf("can't open file: %s!\n", Name);
		exit(1);
	}

	fseek(f, 0, SEEK_END);
	Result.Size = ftell(f);
	fseek(f, 0, SEEK_SET);

	Result.Data = new uint8_t[Result.Size];

	if (fread(Result.Data, 1, Result.Size, f) != Result.Size)
	{
		printf("error during file reading!\n");
		exit(1);
	}

	fclose(f);
	return Result;
}


file_data
ReadEntireFile(const std::string& PathName)
{
	file_data Result = {};

	std::ifstream file(PathName, std::ios::binary | std::ios::in);
	if (file.is_open())
	{
		file.seekg(0, std::ios::end);
		Result.Size = file.tellg();
		file.seekg(0, std::ios::beg);

		Result.Data = new u8[Result.Size];
		file.read(reinterpret_cast<char*>(Result.Data), Result.Size);
		if (!file.good())
		{
			delete[] Result.Data;
			Result.Data = nullptr;
			std::cerr << "error during file reading!\n" << PathName << "\n";
		}
	}
	else
	{
		std::cerr << "can't open file: " << PathName << "\n";
	}

	return Result;
}

namespace fs = std::filesystem; //C++17

void
ReadTestFiles(std::vector<file_data>& InputArr, const char* Str)
{
	const std::string PathName(Str);
	const fs::path Path(PathName);

	std::error_code Err;
	if (fs::is_directory(Path, Err))
	{
		for (const auto& Entry : fs::directory_iterator(Path))
		{
			if (Entry.is_regular_file())
			{
				const auto FileName = Entry.path().string();
				file_data File = ReadEntireFile(FileName);
				if (File.Data)
				{
					File.Name = Entry.path().filename().string();
					InputArr.push_back(File);
				}
			}
		}
	}
	else
	{
		if (Err)
		{
			std::cerr << "Error in is_directory: " << Err.message() << "\n";
		}
		else
		{
			file_data File = ReadEntireFile(PathName);
			if (File.Data)
			{
				File.Name = Path.filename().string();
				InputArr.push_back(File);
			}
		}
	}
}

#define PRINT_TEST_FUNC() {printf("--- %s\n", __func__);}

inline void
PrintCompressionSize(u64 InitSize, u64 CompSize)
{
	printf(" %lu bytes | %.3f ratio\n", CompSize, (f64)InitSize / (f64)CompSize);
}

inline void
PrintSymbolEncPerfStats(u64 Clocks, f64 Time, u64 DataSize)
{
	printf("  %lu clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", Clocks, 1.0 * Clocks / DataSize, 1.0 * DataSize / (Time * 1048576.0));
}

inline void
PrintAvgPerSymbolPerfStats(AccumTime Accum, u32 RunsCount, u64 DataSize)
{
	Accum.avg(RunsCount);
	printf(" avg of %d runs ", RunsCount);
	PrintSymbolEncPerfStats(Accum.Clock, Accum.Time, DataSize);
#if 0
	printf(" min of %d runs ", RunsCount);
	PrintSymbolEncPerfStats(Accum.MinClock, Accum.MinTime, DataSize);
	printf(" max of %d runs ", RunsCount);
	PrintSymbolEncPerfStats(Accum.MaxClock, Accum.MaxTime, DataSize);
#endif
	printf("\n");
}

std::string
ToBinary(u64 Val, u32 Len)
{
	std::string Result;

	for (s32 i = Len - 1; i >= 0; i--)
	{
		Result += ((Val >> i) & 1) ? "1" : "0";
	}

	return Result;
}

template<typename T> inline void
CountByte(T* Freq, u8* Input, u64 Size)
{
	for (u64 i = 0; i < Size; i++)
	{
		Freq[Input[i]]++;
	}
}

template<typename T> inline void
CalcCumFreq(T* Freq, T* CumFreq, u32 SymCount)
{
	CumFreq[0] = 0;
	for (u32 i = 0; i < SymCount; i++)
	{
		CumFreq[i + 1] = CumFreq[i] + Freq[i];
	}
}

template<typename T> f64
Entropy(const T* Freq, u32 AlphSize)
{
	f64 H = 0;
	u64 Sum = 0;

	for (u32 i = 0; i < AlphSize; i++)
	{
		T count = Freq[i];
		if (count)
		{
			Sum += count;
			H -= count * std::log2(static_cast<f64>(count));
		}
	}

	f64 SumFP = static_cast<f64>(Sum);
	H = std::log2(SumFP) + H / SumFP;
	return H;
}

template<typename T> T
BitReverseSlow(T Val, u32 Bits)
{
	T Result = 0;

	while (Bits--)
	{
		Result <<= 1;
		Result |= (Val & 1);
		Val >>= 1;
	}

	return Result;
}

#endif
