#if !defined(AC_PARAMS_H)
#define AC_PARAMS_H

static constexpr u32 CODE_BITS = 24;
static constexpr u32 FREQ_BITS = 14;
static constexpr u32 FREQ_VALUE_BITS = 14;
static constexpr u32 PROB_MAX_VALUE = (1 << FREQ_BITS);
static constexpr u32 CODE_MAX_VALUE = (1 << CODE_BITS) - 1;
static constexpr u32 FREQ_MAX_VALUE = (1 << FREQ_VALUE_BITS);
static constexpr u32 ONE_FOURTH = (1 << (CODE_BITS - 2));
static constexpr u32 TREE_FOURTHS = ONE_FOURTH * 3;
static constexpr u32 ONE_HALF = ONE_FOURTH * 2;

struct prob
{
	u32 lo;
	u32 hi;
	u32 scale;
};

#endif