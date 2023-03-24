#if !defined(PPM_AC_H)
#define PPM_AC_H

struct context_data_excl
{
	u16 Data[256];

	static constexpr u16 Mask = MaxUInt16;
};

struct context;

#pragma pack(1)
struct context_data
{
	context* Next;
	u16 Freq;
	u8 Symbol;
};

struct context
{
	context_data* Data;
	context* Prev;
	u16 TotalFreq;
	u16 EscapeFreq;
	u16 SymbolCount;

	static constexpr u32 MaxSymbol = 255;
};

struct decode_symbol_result
{
	prob Prob;
	u32 Symbol;
};

#endif