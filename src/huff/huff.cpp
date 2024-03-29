#include "huff.h"

#define LOG_HUFFBUILD 0

static inline void
SwapNodes(huff_node* A, huff_node* B)
{
	huff_node Temp = *A;
	*A = *B;
	*B = Temp;
}

static inline u32
QuickSortPartitionBF(huff_node* Nodes, const u32 Low, const u32 High)
{
	const u32 Pivot = Nodes[High].Freq;

	s32 i = Low - 1;
	s32 j = Low;
	for (; j < High; j++)
	{
		if (Nodes[j].Freq > Pivot)
		{
			i++;
			SwapNodes(&Nodes[i], &Nodes[j]);
		}
	}

	u32 Result = i + 1;
	SwapNodes(&Nodes[i + 1], &Nodes[High]);

	return Result;
}

static inline void
QuickSortBF(huff_node* Nodes, u32 Low, u32 High)
{
	while (Low < High)
	{
		const u32 Index = QuickSortPartitionBF(Nodes, Low, High);
		if ((Index - Low) < (High - Index))
		{
			QuickSortBF(Nodes, Low, Index - 1);
			Low = Index + 1;
		}
		else
		{
			QuickSortBF(Nodes, Index + 1, High);
			High = Index - 1;
		}
	}
}

static inline u32
InitNodes(huff_node* const FillNode, const u32* Freqs, u32 SymbolCount)
{
	huff_node* Iter = FillNode;
	for (u32 i = 0; i < SymbolCount; i++)
	{
		if (Freqs[i])
		{
			Iter->Sym = i;
			Iter->Freq = Freqs[i];
			Iter++;
		}
	}

	u32 Count = Iter - FillNode;
	return Count;
}

struct HuffEncoder
{
	huff_enc_entry Table[256];

	HuffEncoder() = default;

	inline void encode(BitWriter& Writer, u8 Sym)
	{
		Writer.writeMSB(Table[Sym].Code, Table[Sym].Len);
	}
};

struct HuffDefaultBuild
{
	huff_node Nodes[513];
	u32 MaxSymbolIndex;
	u8 CodeLen;
	u8 MaxSymbol;

	HuffDefaultBuild() = default;

	b32 buildTable(u32* SymFreq, u32 MaxCodeLen = 0, u32 AlphSymbolCount = 256)
	{
		Assert(MaxCodeLen <= HUFF_MAX_CODELEN)
		Assert(AlphSymbolCount <= 256);

		MemSet<u8>((u8*)Nodes, sizeof(Nodes), 0);
		Nodes[0].Freq = MaxUInt32;

		MaxSymbolIndex = InitNodes(Nodes + 1, SymFreq, AlphSymbolCount);
		MaxSymbol = Nodes[MaxSymbolIndex].Sym;

		const u32 BuildStartIndex = AlphSymbolCount + 1;

		QuickSortBF(Nodes, 0, MaxSymbolIndex);

		huff_def_build_iter Iter;
		Iter.InsertAt = Iter.BuildAt = BuildStartIndex;
		Iter.NodeAt = MaxSymbolIndex;

		u32 n1 = Iter.NodeAt;
		u32 n2 = Iter.NodeAt - 1;
		Iter.NodeAt -= 2;

		Nodes[Iter.InsertAt].Freq = Nodes[n1].Freq + Nodes[n2].Freq;
		Nodes[n1].Parent = Nodes[n2].Parent = Iter.InsertAt;

		while ((Iter.NodeAt | (Iter.BuildAt ^ Iter.InsertAt)))
		{
			Iter.InsertAt++;

			n1 = getNodeIndex(Nodes, Iter);
			n2 = getNodeIndex(Nodes, Iter);

			Nodes[Iter.InsertAt].Freq = Nodes[n1].Freq + Nodes[n2].Freq;
			Nodes[n1].Parent = Nodes[n2].Parent = Iter.InsertAt;
		}

		for (u32 i = Iter.InsertAt - 1; i >= BuildStartIndex; i--)
		{
			Nodes[i].Len = Nodes[Nodes[i].Parent].Len + 1;
		}

		for (u32 i = 1; i <= MaxSymbolIndex; i++)
		{
			Nodes[i].Len = Nodes[Nodes[i].Parent].Len + 1;
		}

		huff_node* InitNodes = Nodes + 1;
		huff_node* BuildNodes = Nodes + BuildStartIndex;
		b32 Success = limitLengthForStandartHuffTree(SymFreq, InitNodes, BuildNodes, MaxSymbolIndex, MaxCodeLen);

		return Success;
	}

	inline void writeTable(BitWriter& Writer) const
	{
		u8 LenCount[17] = {};
		for (u32 i = 1; i <= MaxSymbolIndex; i++)
		{
			LenCount[Nodes[i].Len]++;
		}

		u32 MaxSymbolBits = FindMostSignificantSetBit32(MaxSymbol) + 1;
		u32 MaxCountBits = FindMostSignificantSetBit32(MaxSymbolIndex) + 1;

		Writer.writeMSB(MaxCountBits, 8);
		Writer.writeMSB(MaxSymbolBits, 8);
		Writer.writeMSB(MaxSymbolIndex, MaxCountBits);

		const huff_node* CurrNode = Nodes + 1;
		for (u32 i = 1; i < ArrayCount(LenCount); i++)
		{
			u8 CodesWithLen = LenCount[i];
			if (CodesWithLen)
			{
				Writer.writeMSB(CodesWithLen, MaxCountBits);
				Writer.writeMSB(i - 1, 4);

				while (CodesWithLen--)
				{
					Writer.writeMSB(CurrNode->Sym, MaxSymbolBits);
					++CurrNode;
				}
			}
		}
	}

	inline u32 countSize(u32* SymFreq) const
	{
		u32 Size = 0;
		for (u32 i = 1; i <= MaxSymbolIndex; i++)
		{
			Size += SymFreq[Nodes[i].Sym] * Nodes[i].Len;
		}

		Size += 8 - (Size & 0x7);
		Size >>= 3;

		return Size;
	}

	void buildCodes(HuffEncoder& Enc) const
	{
		MemSet<u8>((u8*)Enc.Table, sizeof(Enc.Table), 0);

		u32 Code = 0;
		u32 LastLen = 0;

		for (u32 i = 1; i < (MaxSymbolIndex + 1); i++)
		{
			u32 Len = Nodes[i].Len;
			if (LastLen != Len)
			{
				if (LastLen)
				{
					Code++;
					Code <<= (Len - LastLen);
				}
				LastLen = Len;
			}
			else
			{
				Code++;
			}

			u32 Sym = Nodes[i].Sym;
			Enc.Table[Sym].Len = Len;
			Enc.Table[Sym].Code = Code;

#if LOG_HUFFBUILD
			printf("code:%s hex:%x len:%d symbol:%d\n", ToBinary(Code, Len).c_str(), Code, Len, Sym);
#endif
		}
	}
private:
	inline u32 getNodeIndex(huff_node* Nodes, huff_def_build_iter& Iter) const
	{
		u32 Result;

		if ((Iter.BuildAt != Iter.InsertAt) && (Nodes[Iter.NodeAt].Freq > Nodes[Iter.BuildAt].Freq))
		{
			Result = Iter.BuildAt++;
		}
		else
		{
			Result = Iter.NodeAt--;
		}

		return Result;
	}

	inline b32 limitLengthForStandartHuffTree(u32* SymFreq, huff_node* InitNodes, huff_node* BuildNodes, u32 SymCount, u32 MaxCodeLen)
	{
		b32 Result = false;

		Assert(SymCount);
		u32 ScanResult = FindMostSignificantSetBit32(SymCount);
		u32 MinTableLog = ScanResult + 1;

		if (MaxCodeLen == 0)
		{
			// Try to find optimal table log
			// with this tree build method basically always will be 16 so it's kind of useless

			u32 SizeOfEntries = SymCount * sizeof(huff_node);
			u32 BuildLog = MinTableLog;
			u32 PrevSize = MaxUInt32;
			u32 CurrSize;

			for (; BuildLog <= HUFF_MAX_CODELEN; BuildLog++)
			{
				MemCopy(SizeOfEntries, BuildNodes, InitNodes);
				Result = limitCodeLengthByRank(BuildNodes, SymCount, BuildLog);

				if (Result)
				{
					CurrSize = 0;
					for (u32 i = 0; i < SymCount; i++)
					{
						CurrSize += BuildNodes[i].Len * SymFreq[BuildNodes[i].Sym];
					}

					if (CurrSize > PrevSize) break;
					PrevSize = CurrSize;
				}
			}

			CodeLen = --BuildLog;

			if (CodeLen != HUFF_MAX_CODELEN)
			{
				limitCodeLengthByRank(InitNodes, SymCount, CodeLen);
			}
			else
			{
				MemCopy(SizeOfEntries, InitNodes, BuildNodes);
			}
		}
		else
		{
			CodeLen = MinTableLog > MaxCodeLen ? MinTableLog : MaxCodeLen;
			if (InitNodes[SymCount - 1].Len <= CodeLen)
			{
				Result = true;
			}
			else
			{
#if 1
				Result = limitCodeLengthByRank(InitNodes, SymCount, CodeLen);
#else
				Result = limitCodeLength(InitNodes, SymCount, CodeLen);
#endif
			}
		}

		return Result;
	}

	// copy of zstd HUF_setMaxHeight
	// https://fastcompression.blogspot.com/2015/07/huffman-revisited-part-3-depth-limited.html
	// https://cbloomrants.blogspot.com/2018/04/the-kraft-number-binary-arithmetic-and.html
	inline b32 limitCodeLengthByRank(huff_node* Nodes, u32 Count, u32 MaxLen) const
	{

		u32 LastAt = Count - 1;
		s32 TotalDept = 0;
#if 0
		u32 k1 = 0;
		u32 MaxK = 1 << MaxLen;
		for (s32 i = Count - 1; i >= 0; --i)
		{
			Nodes[i].Len = (Nodes[i].Len < MaxLen) ? Nodes[i].Len : MaxLen;
			k1 += 1 << (MaxLen - Nodes[i].Len);
		}
		TotalDept = k1 - MaxK;
#else
		u32 LargestLen = Nodes[LastAt].Len;
		u32 k = 1 << (LargestLen - MaxLen);
		while (Nodes[LastAt].Len > MaxLen)
		{
			TotalDept += k - (1 << (LargestLen - Nodes[LastAt].Len));
			Nodes[LastAt].Len = MaxLen;
			LastAt--;
		}

		TotalDept >>= LargestLen - MaxLen;
#endif
		Assert(TotalDept > 0);

		while (Nodes[LastAt].Len == MaxLen) LastAt--;

		u32 NoSymbols = 0xFFFFFFFF;
		u32 Ranks[HUFF_MAX_CODELEN + 2];
		MemSet(Ranks, sizeof(Ranks) / sizeof(u32), NoSymbols);
		
		u32 MappedRank = MaxLen;
		for (s32 i = LastAt; i >= 0; i--)
		{
			u32 NodeLen = Nodes[i].Len;
			if (NodeLen >= MappedRank) continue;
			MappedRank = NodeLen;
			Ranks[MaxLen - MappedRank] = i;
		}

		while (TotalDept > 0)
		{
			u32 ScanResult = FindMostSignificantSetBit32(TotalDept);
			u32 BitsDeptIndex = ScanResult + 1;

			for (; BitsDeptIndex > 1; BitsDeptIndex--)
			{
				u32 HighIndex = Ranks[BitsDeptIndex];
				u32 LowIndex = Ranks[BitsDeptIndex - 1];

				if (HighIndex == NoSymbols) continue;
				if (LowIndex == NoSymbols) break;

				u32 HighTotal = Nodes[HighIndex].Freq;
				u32 LowTotal = 2*Nodes[LowIndex].Freq;

				if (HighTotal <= LowTotal) break;
			}

			while ((BitsDeptIndex <= HUFF_MAX_CODELEN) && (Ranks[BitsDeptIndex] == NoSymbols))
				BitsDeptIndex++;

			Assert(Ranks[BitsDeptIndex] != NoSymbols);

			TotalDept -= 1 << (BitsDeptIndex - 1);
			Nodes[Ranks[BitsDeptIndex]].Len++;

			if (Ranks[BitsDeptIndex - 1] == NoSymbols)
			{
				Ranks[BitsDeptIndex - 1] = Ranks[BitsDeptIndex];
			}

			if (Ranks[BitsDeptIndex] == 0)
			{
				Ranks[BitsDeptIndex] = NoSymbols;
			}
			else
			{
				Ranks[BitsDeptIndex]--;
				u32 LastRank = Nodes[Ranks[BitsDeptIndex]].Len;
				if (LastRank != (MaxLen - BitsDeptIndex))
				{
					Ranks[BitsDeptIndex] = NoSymbols;
				}
			}
		}

		while (TotalDept < 0)
		{
			if (Ranks[1] == NoSymbols)
			{
				while (Nodes[LastAt].Len == MaxLen) LastAt--;
				Nodes[LastAt + 1].Len--;
				Ranks[1] = LastAt;
				TotalDept++;
				continue;
			}

			Nodes[Ranks[1] + 1].Len--;
			Ranks[1]++;
			TotalDept++;
		}

		return (TotalDept <= 0);
	}

	inline b32 limitCodeLength(huff_node* Nodes, u32 Count, u32 MaxLen) const
	{
		b32 Result = true;

		u32 k = 0;
		u32 MaxK = 1 << MaxLen;

		for (s32 i = Count - 1; i >= 0; --i)
		{
			Nodes[i].Len = (Nodes[i].Len < MaxLen) ? Nodes[i].Len : MaxLen;
			k += 1 << (MaxLen - Nodes[i].Len);
		}

		for (s32 i = Count - 1; (i >= 0) && (k > MaxK); i--)
		{
			if (Nodes[i].Len < MaxLen)
			{
				u8 CodeLen = Nodes[i].Len;

				while (CodeLen < MaxLen)
				{
					CodeLen++;
					k -= 1 << (MaxLen - CodeLen);
				}

				Nodes[i].Len = CodeLen;
			}
		}

		if (k < MaxK)
		{
			for (u32 i = 0; (i < Count) && (k < MaxK); i++)
			{
				for (;;)
				{
					u32 NewK = k + (1 << (MaxLen - Nodes[i].Len));
					if (NewK > MaxK) break;

					k = NewK;
					Nodes[i].Len--;
				}
			}
		}
		else if (k > MaxK)
		{
			Result = false;
		}

		return Result;
	}
};

struct HuffDecoder
{
	huff_dec_entry* Table;

	HuffDecoder() : Table(nullptr) {}
	HuffDecoder(void* TablePtr) : Table(reinterpret_cast<huff_dec_entry*>(TablePtr)) {}

	inline u8 decode(BitReaderMSB& Reader, u32 MaxCodeLen) const
	{
		u64 Val = Reader.peek(MaxCodeLen);
		Reader.consume(Table[Val].Len);
		return Table[Val].Sym;
	}
};

struct HuffDecTableInfo
{
	u8 SymBuff[256];
	u8 CodeLenCount[HUFF_MAX_CODELEN + 1];
	u8 MinCodeLen;
	u8 MaxCodeLen;
	u32 DecTableReqSizeByte;

	inline void readTable(BitReaderMSB& Reader)
	{
		MinCodeLen = 255;
		MaxCodeLen = 0;
		MemSet<u8>(CodeLenCount, sizeof(CodeLenCount), 0);

		Reader.refillTo(8);
		u64 MaxCountBits = Reader.peek(8);
		Reader.consume(8);

		Reader.refillTo(8 + MaxCountBits);

		u64 MaxSymbolBits = Reader.peek(8);
		Reader.consume(8);

		u64 SymbolCount = Reader.peek(MaxCountBits);
		Reader.consume(MaxCountBits);

		u8* IterSym = SymBuff;
		u8* const EndSym = SymBuff + SymbolCount;

		while (IterSym != EndSym)
		{
			Reader.refillTo(4 + MaxCountBits);
			u64 CodesWithLen = Reader.peek(MaxCountBits);
			Reader.consume(MaxCountBits);

			u64 CodeLen = Reader.peek(4);
			Reader.consume(4);

			++CodeLen;

			CodeLenCount[CodeLen] = CodesWithLen;
			MinCodeLen = CodeLen < MinCodeLen ? CodeLen : MinCodeLen;
			MaxCodeLen = CodeLen > MaxCodeLen ? CodeLen : MaxCodeLen;

			while (CodesWithLen--)
			{
				Reader.refillTo(MaxSymbolBits);
				u64 Sym = Reader.peek(MaxSymbolBits);
				Reader.consume(MaxSymbolBits);

				*IterSym++ = static_cast<u8>(Sym);
			}
		}

		DecTableReqSizeByte = ((u32)1 << MaxCodeLen) * sizeof(huff_dec_entry);
	}

	inline void assignCodesMSB(HuffDecoder& Dec) const
	{
		u16* SetMem = reinterpret_cast<u16*>(Dec.Table);
		const u8* IterSym = SymBuff;

		for (u32 i = MinCodeLen; i <= MaxCodeLen; i++)
		{
			u32 SymCount = CodeLenCount[i];
			if (SymCount)
			{
				u32 Shift = MaxCodeLen - i;
				u32 Range = 1 << Shift;

				while (SymCount--)
				{
					huff_dec_entry Entry;
					Entry.Sym = *IterSym++;
					Entry.Len = i;

					MemSet<u16>(SetMem, Range, Entry.Val);
					SetMem += Range;
				}
			}
		}
	}
};
