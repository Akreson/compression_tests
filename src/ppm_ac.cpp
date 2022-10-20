struct context_data_excl
{
	u16 Data[256];
};

struct context;
struct context_data
{
	context* Next;
	u16 Freq;
	u8 Symbol;
};

struct context
{
	context_data* Data;
	u32 TotalFreq;
	u16 EscapeFreq;
	u16 SymbolCount;

	static constexpr u32 EscapeSymbol = 256;
};

struct decode_symbol_result
{
	prob Prob;
	u32 SymbolIndex;
	u32 Symbol;
};

struct symbol_search_result
{
	b32 Success;
	u32 Index;
};

class PPMByte
{
	StaticSubAlloc SubAlloc;

	context_data_excl* Exclusion;
	context* StaticContext; // order -1
	context* ContextZero;

	u32 OrderCount;
	u32 CurrSetOrderCount;

	u32* ContextSeq;
	context** ContextStack;

public:
	static constexpr u32 EscapeSymbol = context::EscapeSymbol;

	// For debug
	u64 ContextCount;

	PPMByte() = delete;
	PPMByte(u32 MaxOrderContext, u32 MemLimit) :
		SubAlloc(MemLimit), OrderCount(MaxOrderContext), CurrSetOrderCount(0), ContextCount(0)
	{
		StaticContext = new context;
		ZeroStruct(*StaticContext);

		StaticContext->Data = new context_data[256];
		StaticContext->EscapeFreq = 1;
		StaticContext->TotalFreq = 256;
		StaticContext->SymbolCount = 256;

		for (u32 i = 0; i < StaticContext->SymbolCount; ++i)
		{
			StaticContext->Data[i].Freq = 1;
			StaticContext->Data[i].Symbol = i;
			StaticContext->Data[i].Next = nullptr;
		}

		Exclusion = new context_data_excl;
		clearExclusion();

		ContextZero = new context;
		ZeroStruct(*ContextZero);

		// last encoded symbols
		ContextSeq = new u32[MaxOrderContext];

		// max symbol seq + context for that seq
		ContextStack = new context* [MaxOrderContext + 1];
	}

	~PPMByte()
	{
		reset();

		delete StaticContext;
		delete ContextZero;
		delete ContextSeq;
		delete ContextStack;
		delete Exclusion;
	}

	void encode(ArithEncoder& Encoder, u32 Symbol)
	{
		u32 LookFromSeqIndex = 0;
		u32 OrderLooksLeft = CurrSetOrderCount + 1;
		//context* PrevContext = 0;
		while (OrderLooksLeft)
		{
			context* LookAtContext = findContext(LookFromSeqIndex, CurrSetOrderCount);
			ContextStack[LookFromSeqIndex++] = LookAtContext;

			b32 Success = encodeSymbol(Encoder, LookAtContext, Symbol);
			if (Success) break;

			if (LookAtContext->TotalFreq)
			{
				updateExclusionData(LookAtContext);
			}

			OrderLooksLeft--;
		}

		if (!OrderLooksLeft)
		{
			prob Prob = {};
			b32 Success = getProb(StaticContext, Prob, Symbol);
			Assert(Success);
			Encoder.encode(Prob);

			Assert(LookFromSeqIndex);
			/*if (!LookFromSeqIndex)
			{
				LookFromSeqIndex++;
				ContextStack[0] = ContextZero;
			}*/
		}

		updateOrderSeq(Symbol);
		update(Symbol, LookFromSeqIndex);
		clearExclusion();
	}

	void encodeEndOfStream(ArithEncoder& Encoder)
	{
		u32 OrderIndex = 0;
		while (OrderIndex <= CurrSetOrderCount)
		{
			context* Context = ContextStack[OrderIndex++];
			encodeSymbol(Encoder, Context, EscapeSymbol);
			updateExclusionData(Context);
		}

		prob Prob = {};
		getProb(StaticContext, Prob, EscapeSymbol);
		Encoder.encode(Prob);
	}

	u32 decode(ArithDecoder& Decoder)
	{
		u32 ResultSymbol;
		u32 LookFromSeqIndex = 0;
		u32 OrderLooksLeft = CurrSetOrderCount + 1;
		//context* PrevContext = 0;
		while (OrderLooksLeft)
		{
			context* LookAtContext = findContext(LookFromSeqIndex, CurrSetOrderCount);
			ContextStack[LookFromSeqIndex++] = LookAtContext;

			b32 Success = false;
			if (LookAtContext->TotalFreq)
			{
				Success = decodeSymbol(Decoder, LookAtContext, &ResultSymbol);
				updateExclusionData(LookAtContext);
			}

			if (Success) break;

			OrderLooksLeft--;
		}

		if (!OrderLooksLeft)
		{
			u32 ExclTotal = getExcludedTotal(StaticContext) + 1;
			u32 CurrFreq = Decoder.getCurrFreq(ExclTotal);
			decode_symbol_result DecodedSymbol = getSymbolFromFreq(StaticContext, CurrFreq, ExclTotal);
			ResultSymbol = DecodedSymbol.Symbol;

			// if it not end of stream
			if (ResultSymbol != EscapeSymbol)
			{
				Decoder.updateDecodeRange(DecodedSymbol.Prob);
				Assert(LookFromSeqIndex);

				/*if (!LookFromSeqIndex)
				{
					LookFromSeqIndex++;
					ContextStack[0] = ContextZero;
				}*/
			}
		}

		updateOrderSeq(ResultSymbol);
		update(ResultSymbol, LookFromSeqIndex);
		clearExclusion();

		return ResultSymbol;
	}

	void reset()
	{
		freeContext(ContextZero);
		ZeroStruct(*ContextZero);

		CurrSetOrderCount = 0;
		ZeroSize(static_cast<void*>(ContextSeq), sizeof(*ContextSeq) * OrderCount);
		ZeroSize(static_cast<void*>(ContextStack), sizeof(*ContextStack) * (OrderCount + 1));

		clearExclusion();
	}

private:

	decode_symbol_result getSymbolFromFreq(context* Context, u32 DecodeFreq, u32 ExclTotal)
	{
		decode_symbol_result Result = {};

		u32 CumFreq = 0;
		for (; Result.SymbolIndex < Context->SymbolCount; ++Result.SymbolIndex)
		{
			context_data* Data = Context->Data + Result.SymbolIndex;
			u32 ModFreq = Data->Freq & Exclusion->Data[Data->Symbol];
			u32 CheckFreq = CumFreq + ModFreq;

			if (CheckFreq > DecodeFreq) break;

			CumFreq += ModFreq;
		}

		Result.Prob.lo = CumFreq;
		if (Result.SymbolIndex < Context->SymbolCount)
		{
			context_data* Data = Context->Data + Result.SymbolIndex;
			Result.Symbol = Data->Symbol;
			Result.Prob.hi = CumFreq + Data->Freq;
			Result.Prob.count = ExclTotal;
		}
		else
		{
			Result.Prob.count = Result.Prob.hi = ExclTotal;
			Result.Symbol = EscapeSymbol;
		}

		return Result;
	}

	u32 getExcludedTotal(context* Context)
	{
		u32 Result = 0;
		for (u32 i = 0; i < Context->SymbolCount; ++i)
		{
			context_data* Data = Context->Data + i;
			Result += Data->Freq & Exclusion->Data[Data->Symbol];
		}

		return Result;
	}

	b32 decodeSymbol(ArithDecoder& Decoder, context* Context, u32* ResultSymbol)
	{
		b32 Success = false;
		u32 ExclTotal = getExcludedTotal(Context) + Context->EscapeFreq;
		u32 CurrFreq = Decoder.getCurrFreq(ExclTotal);

		decode_symbol_result DecodedSymbol = getSymbolFromFreq(Context, CurrFreq, ExclTotal);
		Decoder.updateDecodeRange(DecodedSymbol.Prob);

		if (DecodedSymbol.Symbol != EscapeSymbol)
		{
			*ResultSymbol = DecodedSymbol.Symbol;
			Success = true;
		}

		return Success;
	}

	void rescale(context* Context)
	{
		Context->TotalFreq = 0;
		for (u32 i = 0; i < Context->SymbolCount; ++i)
		{
			context_data* Data = Context->Data + i;
			u32 NewFreq = (Data->Freq + 1) / 2;
			Data->Freq = NewFreq;
			Context->TotalFreq += NewFreq;
		}
	}

	// TODO: remove finding symbol on update
	void update(u32 Symbol, u32 StackSize)
	{
		u32 UpdateIndex = 0;
		while (StackSize--)
		{
			context* UpdateContext = ContextStack[UpdateIndex++];
			context_data* UpdateData = UpdateContext->Data;

			if (UpdateContext->TotalFreq >= FreqMaxValue)
			{
				rescale(UpdateContext);
			}

			symbol_search_result Search = findSymbolIndex(UpdateContext, Symbol);
			
			// method C
			u32 UpdateSymbolIndex;
			if (Search.Success)
			{
				UpdateSymbolIndex = Search.Index;
			}
			else
			{
				UpdateContext->EscapeFreq++;
				UpdateSymbolIndex = addSymbol(UpdateContext, Symbol);
			}

			u32 Inc = 1;
			UpdateContext->Data[UpdateSymbolIndex].Freq += Inc;
			UpdateContext->TotalFreq += Inc;
		}
	}

	b32 getProb(context* Context, prob& Prob, u32 Symbol)
	{
		b32 Result = false;

		u32 SymbolIndex = 0;
		for (; SymbolIndex < Context->SymbolCount; ++SymbolIndex)
		{
			context_data* Data = Context->Data + SymbolIndex;
			if (Data->Symbol == Symbol) break;
			
			u32 Freq = Data->Freq & Exclusion->Data[Data->Symbol];
			Prob.lo += Freq;
		}

		if (SymbolIndex < Context->SymbolCount)
		{
			Prob.hi = Prob.lo + Context->Data[SymbolIndex].Freq;

			u32 CumFreqHi = 0;
			for (u32 i = SymbolIndex; i < Context->SymbolCount; ++i)
			{
				context_data* Data = Context->Data + i;
				u32 Freq = Data->Freq & Exclusion->Data[Data->Symbol];
				CumFreqHi += Freq;
			}

			Prob.count = Prob.lo + CumFreqHi + Context->EscapeFreq;
			Result = true;
		}
		else
		{
			Prob.count = Prob.hi = Prob.lo + Context->EscapeFreq;
		}

		return Result;
	}

	symbol_search_result findSymbolIndex(context* Context, u32 Symbol)
	{
		symbol_search_result Result = {};

		for (u32 i = 0; i < Context->SymbolCount; ++i)
		{
			context_data* Data = Context->Data + i;
			if (Data->Symbol == Symbol)
			{
				Result.Index = i;
				Result.Success = true;
				break;
			}
		}

		return Result;
	}

	b32 encodeSymbol(ArithEncoder& Encoder, context* Context, u32 Symbol)
	{
		b32 Success = false;

		if (Context->EscapeFreq)
		{
			prob Prob = {};
			Success = getProb(Context, Prob, Symbol);
			Encoder.encode(Prob);
		}

		return Success;
	}

	u32 addSymbol(context* Context, u32 Symbol)
	{
		u32 ResultIndex;
		context_data NewStat = {};
		NewStat.Symbol = Symbol;
		NewStat.Next = nullptr; // maybe delete this line

		if (Context->SymbolCount)
		{
			context_data* NewData = new context_data[Context->SymbolCount + 1];

			for (u32 i = 0; i < Context->SymbolCount; ++i)
			{
				NewData[i] = Context->Data[i];
			}

			//Copy(sizeof(context_data) * Context->SymbolCount, NewData, Context->Data);

			delete[] Context->Data;
			Context->Data = NewData;
		}
		else
		{
			Context->Data = new context_data[1];
		}

		ResultIndex = Context->SymbolCount;
		Context->Data[Context->SymbolCount++] = NewStat;

		return ResultIndex;
	}

	// TODO: break from loop if context not created?
	context* findContext(u32 From, u32 To)
	{
		u32 LookAtOrder = From;
		context* CurrContext = ContextZero;

		while (LookAtOrder < To)
		{
			u32 SymbolAtContext = ContextSeq[LookAtOrder];
			Assert(SymbolAtContext < 256);
			
			u32 InContextSymbolIndex;
			context_data* Data = 0;
			if (CurrContext->Data)
			{
				symbol_search_result Search = findSymbolIndex(CurrContext, SymbolAtContext);
				if (Search.Success)
				{
					InContextSymbolIndex = Search.Index;
					Data = CurrContext->Data + InContextSymbolIndex;
				}
			}

			if (!Data)
			{
				InContextSymbolIndex = addSymbol(CurrContext, SymbolAtContext);
				Data = CurrContext->Data + InContextSymbolIndex;
			}

			if (!Data->Next)
			{
				Data->Next = new context;
				ZeroStruct(*Data->Next);
			}

			context* NextContext = Data->Next;

			CurrContext = NextContext;
			LookAtOrder++;
		}

		return CurrContext;
	}

	inline void updateOrderSeq(u32 Symbol)
	{
		u32 UpdateSeqIndex = CurrSetOrderCount;

		if (CurrSetOrderCount == OrderCount)
		{
			UpdateSeqIndex--;

			for (u32 i = 0; i < (OrderCount - 1); ++i)
			{
				ContextSeq[i] = ContextSeq[i + 1];
			}
		}
		else
		{
			CurrSetOrderCount++;
		}

		ContextSeq[UpdateSeqIndex] = Symbol;
	}

	inline void clearExclusion()
	{
		MemSet<u16>(reinterpret_cast<u16*>(Exclusion), sizeof(context_data_excl) / sizeof(Exclusion->Data[0]), MaxUInt32);
	}

	void updateExclusionData(context* Context)
	{
		context_data* ContextData = Context->Data;
		for (u32 i = 0; i < Context->SymbolCount; ++i)
		{
			context_data* Data = ContextData + i;
			u32 ToExcl = Data->Freq ? 0 : MaxUInt32;
			Exclusion->Data[Data->Symbol] = ToExcl;
		}
	}

	void freeContext(context* Context)
	{
		for (u32 i = 0; i < Context->SymbolCount; ++i)
		{
			if (Context->Data[i].Next)
			{
				freeContext(Context->Data[i].Next);
				delete Context->Data[i].Next;
			}
		}

		if (Context->Data) delete[] Context->Data;
	}
};