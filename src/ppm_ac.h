#if !defined(PPM_AC_H)
#define PPM_AC_H

struct context_data_excl
{
	u8 Data[256];

	static constexpr u8 Mask = MaxUInt16;
	static constexpr u8 ClearMask = 0;
};

struct context;
struct context_data
{
	context* Next;
	u8 Freq;
	u8 Symbol;
};

struct context
{
	context_data* Data;
	context* Prev;
	u16 TotalFreq;
	u16 SymbolCount;

	static constexpr u32 MaxSymbol = 255;
};

struct decode_symbol_result
{
	prob Prob;
	u32 Symbol;
};

struct symbol_search_result
{
	b32 Success;
	u16 Index;
};

struct find_context_result
{
	context* Context;
	u16 SeqIndex;
	u16 ChainMissIndex;
	b16 IsNotComplete;
	b16 SymbolMiss;
};

static const u8 ExpEscape[16] = {25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2};

static constexpr u32 FreqBits = 7;
static constexpr u32 Interval = 1 << FreqBits;
static constexpr u32 MaxFreq = 124;

#endif