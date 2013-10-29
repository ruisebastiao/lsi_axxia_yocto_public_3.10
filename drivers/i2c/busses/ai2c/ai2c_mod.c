/*
 *  Copyright (C) 2013 LSI Corporation
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*! @file ai2c_mod.c

    @brief Linux driver implementation of I2C using the ACP I2C
	   features upon an LSI development board (San Antonio,
	   Mission, El Paso, ...)

    @details Command line module parameters (with defaults) include,
		int ai2c_is_enabled  = -1;
			//Override: DTB option enabled
			//Values: 0=not, 1=is, -1=check DTB
		int ai2c_trace_level = (AI2C_MSG_INFO | AI2C_MSG_ERROR);
		int ai2c_chip_ver    = -1;
			//Optional: Needed to figure out memory map, etc.
			//Can verify against value from 0xa.0x10.0x2c
			//Values; 0=X1_REL1
			//	1=X1_REL2+
			//	7=X7_REL1+

    @details Several items contained in the 'i2c' section of the '.dts'
	     are used to configure this module including the addresses of
	     the memory partition, IRQ number, number of DMEs to use (when
	     we want to override the inferences based on the chipType), etc.
*/

/*
#define DATA_STREAM_DEBUG
#define AI2C_EXTERNAL_BUILD

#define CONFIG_LSI_UBOOTENV
#define CONFIG_I2C
*/

#include "ai2c_bus.h"
#include "ai2c_i2c_regs.h"
#include "asm/lsi/acp_ncr.h"


/******************************************************************************
* --- Linux Module Interface                                              --- *
******************************************************************************/

#define AI2C_PARM_INT	          int
#define AI2C_PARM_UINT	          uint
#define AI2C_MODULE_PARM(v, t)    module_param(v, t, (S_IRUSR|S_IRGRP|S_IROTH|\
						      S_IWUSR|S_IWGRP))
#define AI2C_MODULE_INIT(fn)      module_init(fn)
#define AI2C_MODULE_EXIT(fn)      module_exit(fn)
#define AI2C_MODULE_VERSION(v)    MODULE_VERSION(v)

/*****************************
* --- Module Parameters  --- *
*****************************/

int AI2C_MSG_TRACE_LEVEL;
int ai2c_chip_ver;		/* Opt: Needed to figure out memory map, etc.
				 * Can verify against value from 0xa.0x10.0x2c
				 * Values; 0=X1_REL1
				 *	 1=X1_REL2+
				 *	 9=X7_REL1+
				 */

MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("LSI ACP I2C Platform Driver");
MODULE_AUTHOR("LSI");
AI2C_MODULE_VERSION("1.0.0");

AI2C_MODULE_PARM(AI2C_MSG_TRACE_LEVEL, AI2C_PARM_INT);
AI2C_MODULE_PARM(ai2c_chip_ver, AI2C_PARM_INT);


/***************************************
* --- Character Driver Definitions --- *
***************************************/


/*****************************************************************************
 * Local State
 *****************************************************************************/

static struct ai2c_priv *ai2cState;

struct local_state {
	struct i2c_adapter	  adapter;
	struct i2c_client	  *client;
};

static  struct local_state *ai2cModState;

/*****************************************************************************
 * Local Functions: Memory
 *****************************************************************************/

/*
 * definitions of the ai2c_malloc/ai2c_nvm_malloc family of functions.
 */

void *ai2c_malloc(size_t size)
{
	void *p;

	if (size <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_malloc(%d) passed a zero or "
			"less size.\n",
			size);
#endif
	return 0;
	}

	p = __ai2c_malloc(size);
	if (p == NULL)
		AI2C_MSG(AI2C_MSG_ERROR, "ai2c_malloc(%d) failed.\n", size);

	return p;
}

void *ai2c_calloc(size_t no, size_t size)
{
	void *p;

	if (size <= 0 || no <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_calloc(no=%d, size=%d) "
			"passed a zero or less size.\n",
			no, size);
#endif
		return 0;
	}

	p = __ai2c_calloc(no, size);
	if (p == NULL) {
		AI2C_MSG(AI2C_MSG_ERROR,
			"ai2c_calloc(no=%d, size=%d) failed.\n", no, size);
	}
	return p;
}

void *ai2c_realloc(void *ptr, size_t size)
{
	if (size <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_realloc(%d) passed a zero or "
			"less size.\n",
			size);
#endif
		return 0;
	}

	ptr = __ai2c_realloc(ptr, size);
	if (ptr == NULL) {
		AI2C_MSG(AI2C_MSG_ERROR,
			"ai2c_realloc(ptr=%p, size=%d) failed.\n",
			ptr, size);
	}
	return ptr;
}

void ai2c_free(void *ptr)
{
	if (ptr == NULL) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING:  ai2c_free(%p) passed a NULL pointer.\n",
			ptr);
#endif
		return;
	}

	__ai2c_free(ptr);
}

/*****************************************************************************
 * Clock Support
 *****************************************************************************/

/*
 * Clock MHz calculation support Tables:
 */

/* Table 1-7: Feedback Divider Pre-Scalar */

static const u8 Prescale[][2] = {
	{0, 1},
	{1, 3},
	{2, 2},
	{3, 4},
};

#define Prescale_COUNT (sizeof(Prescale)/(2*sizeof(u8)))

/* Table 1-6: PLL Predivider */

static const u8 Predivider[][2] = {
	{0, 1},
	{1, 16},
	{2, 17},
	{3, 30},
	{4, 13},
	{5, 18},
	{6, 7},
	{7, 31},
	{8, 14},
	{9, 11},
	{10, 19},
	{11, 21},
	{12, 27},
	{13, 8},
	{14, 23},
	{15, 32},
	{16, 15},
	{17, 29},
	{18, 12},
	{19, 6},
	{20, 10},
	{21, 20},
	{22, 26},
	{23, 22},
	{24, 28},
	{25, 5},
	{26, 9},
	{27, 25},
	{28, 4},
	{29, 24},
	{30, 3},
	{31, 2},
};

#define Predivider_COUNT (sizeof(Predivider)/(2*sizeof(u8)))

/* Table 1-5: PLL Forward Dividers A,B */

static const u8 ForwardDivider[][2] = {
	{0, 1},
	{1, 2},
	{2, 28},
	{3, 27},
	{4, 22},
	{5, 21},
	{6, 30},
	{7, 29},
	{8, 24},
	{9, 23},
	{10, 12},
	{11, 11},
	{12, 16},
	{13, 15},
	{14, 32},
	{15, 31},
	{16, 26},
	{17, 25},
	{18, 20},
	{19, 19},
	{20, 10},
	{21, 9},
	{22, 14},
	{23, 13},
	{24, 18},
	{25, 17},
	{26, 8},
	{27, 7},
	{28, 6},
	{29, 5},
	{30, 4},
	{31, 3},
};

#define ForwardDivider_COUNT (sizeof(ForwardDivider)/(2*sizeof(u8)))

/* Table 1-11: PLL Feedback Divider */

static const u8 FeedbackDivider[][2] = {
	{0, 1},
	{1, 123},
	{2, 117},
	{3, 251},
	{4, 245},
	{5, 69},
	{6, 111},
	{7, 125},
	{8, 119},
	{9, 95},
	{10, 105},
	{11, 197},
	{12, 239},
	{13, 163},
	{14, 63},
	{15, 253},
	{16, 247},
	{17, 187},
	{18, 57},
	{19, 223},
	{20, 233},
	{21, 207},
	{22, 157},
	{23, 71},
	{24, 113},
	{25, 15},
	{26, 89},
	{27, 37},
	{28, 191},
	{29, 19},
	{30, 99},
	{31, 127},
	{32, 121},
	{33, 109},
	{34, 93},
	{35, 61},
	{36, 185},
	{37, 155},
	{38, 13},
	{39, 97},
	{40, 107},
	{41, 11},
	{42, 9},
	{43, 81},
	{44, 31},
	{45, 49},
	{46, 83},
	{47, 199},
	{48, 241},
	{49, 33},
	{50, 181},
	{51, 143},
	{52, 217},
	{53, 173},
	{54, 51},
	{55, 165},
	{56, 65},
	{57, 85},
	{58, 151},
	{59, 147},
	{60, 227},
	{61, 41},
	{62, 201},
	{63, 255},
	{64, 249},
	{65, 243},
	{66, 195},
	{67, 237},
	{68, 221},
	{69, 231},
	{70, 35},
	{71, 189},
	{72, 59},
	{73, 183},
	{74, 79},
	{75, 29},
	{76, 141},
	{77, 215},
	{78, 145},
	{79, 225},
	{80, 235},
	{81, 219},
	{82, 27},
	{83, 139},
	{84, 137},
	{85, 135},
	{86, 175},
	{87, 209},
	{88, 159},
	{89, 53},
	{90, 45},
	{91, 177},
	{92, 211},
	{93, 23},
	{94, 167},
	{95, 73},
	{96, 115},
	{97, 67},
	{98, 103},
	{99, 161},
	{100, 55},
	{101, 205},
	{102, 87},
	{103, 17},
	{104, 91},
	{105, 153},
	{106, 7},
	{107, 47},
	{108, 179},
	{109, 171},
	{110, 149},
	{111, 39},
	{112, 193},
	{113, 229},
	{114, 77},
	{115, 213},
	{116, 25},
	{117, 133},
	{118, 43},
	{119, 21},
	{120, 101},
	{121, 203},
	{122, 5},
	{123, 169},
	{124, 75},
	{125, 131},
	{126, 3},
	{127, 129},
	{128, 1},
	{129, 250},
	{130, 244},
	{131, 124},
	{132, 118},
	{133, 196},
	{134, 238},
	{135, 252},
	{136, 246},
	{137, 222},
	{138, 232},
	{139, 70},
	{140, 112},
	{141, 36},
	{142, 190},
	{143, 126},
	{144, 120},
	{145, 60},
	{146, 184},
	{147, 96},
	{148, 106},
	{149, 80},
	{150, 30},
	{151, 198},
	{152, 240},
	{153, 142},
	{154, 216},
	{155, 164},
	{156, 64},
	{157, 146},
	{158, 226},
	{159, 254},
	{160, 248},
	{161, 236},
	{162, 220},
	{163, 188},
	{164, 58},
	{165, 28},
	{166, 140},
	{167, 224},
	{168, 234},
	{169, 138},
	{170, 136},
	{171, 208},
	{172, 158},
	{173, 176},
	{174, 210},
	{175, 72},
	{176, 114},
	{177, 160},
	{178, 54},
	{179, 16},
	{180, 90},
	{181, 46},
	{182, 178},
	{183, 38},
	{184, 192},
	{185, 212},
	{186, 24},
	{187, 20},
	{188, 100},
	{189, 168},
	{190, 74},
	{191, 128},
	{192, 122},
	{193, 116},
	{194, 68},
	{195, 110},
	{196, 94},
	{197, 104},
	{198, 162},
	{199, 62},
	{200, 186},
	{201, 56},
	{202, 206},
	{203, 156},
	{204, 14},
	{205, 88},
	{206, 18},
	{207, 98},
	{208, 108},
	{209, 92},
	{210, 154},
	{211, 12},
	{212, 10},
	{213, 8},
	{214, 48},
	{215, 82},
	{216, 32},
	{217, 180},
	{218, 172},
	{219, 50},
	{220, 84},
	{221, 150},
	{222, 40},
	{223, 200},
	{224, 242},
	{225, 194},
	{226, 230},
	{227, 34},
	{228, 182},
	{229, 78},
	{230, 214},
	{231, 144},
	{232, 218},
	{233, 26},
	{234, 134},
	{235, 174},
	{236, 52},
	{237, 44},
	{238, 22},
	{239, 166},
	{240, 66},
	{241, 102},
	{242, 204},
	{243, 86},
	{244, 152},
	{245, 6},
	{246, 170},
	{247, 148},
	{248, 228},
	{249, 76},
	{250, 132},
	{251, 42},
	{252, 202},
	{253, 4},
	{254, 130},
	{255, 2},
};

#define FeedbackDivider_COUNT (sizeof(FeedbackDivider)/(2*sizeof(u8)))

int ai2c_dev_clock_mhz(
	struct ai2c_priv         *priv,       /* IN */
	u32       *clockMhz)   /* OUT */
{
	int       ai2cStatus = AI2C_ST_SUCCESS;
	u32       sysPllCtrl;

/*
 * Equation:
 * PLLOUTA = (CLKI * MULTINT.predivide * MULTINT.maindivide) /
 *           (PREDIV * RANGEA.predivide * RANGEA.maindivide)
 *
 * For SYSCLK, read content of sys_pll_ctrl (0x18d.0x0.0xc) defined as:
 *
 * Bits    SW      Name            Description                     Reset
 * 31:26   R/W     prediv          SYS PLL pre-divide value         6'h0
 * 25:19   R/W     rangeA          SYS PLL range A value            7'h0
 * 18:12   R/W     rangeB          SYS PLL range B value            7'h0
 * 11:1    R/W     multInt         SYS PLL multiplier value        11'h0
 * 0       R/W     force_reset     SYS PLL FF enable bit            1'h1
 */
	u32       prediv,
		rangeA,
		/* rangeB, */
		multInt,
		/* force_reset, */
		v,
		clki,
		multInt_predivide,
		multInt_maindivide,
		rangeA_predivide,
		rangeA_maindivide,
		SYSCLK;

	if ((clockMhz == NULL) || (priv == NULL))
		AI2C_CALL(-ENOEXEC);

	if (priv->hw_rev.isFpga) {
		*clockMhz = 6;
		return AI2C_ST_SUCCESS;
	}

	AI2C_CALL(ai2c_dev_read32(priv,
		AI2C_REGION_CLK_CTRL, 0xC, &sysPllCtrl));

	prediv = (sysPllCtrl >> 26) & 0x3f;
	rangeA = (sysPllCtrl >> 19) & 0x7f;
	/* rangeB = (sysPllCtrl >> 12) & 0x7f; */
	multInt = (sysPllCtrl >> 1) & 0x7ff;
	/* force_reset = (sysPllCtrl >> 0) & 0x1; */

/*
 * CLKI is 125Mhz
 * MULTINT.predivide is the translated value from bits 8:9 of the
 *     multInt field.
 * MULTINT.maindivide is the translated value from bits 7:0 of the
 *     multInt field.
 * PREDIV is the translated value form the prediv field.
 * RANGEA.predivide is the translated value form bits 6:5 of the
 *     rangeA field.
 * RANGEA.maindivide is the translated value from bits 4:0 of the
 *     rangeA field.
 */
	clki = 125;

	v = (multInt >> 8) & 0x3;
	multInt_predivide = Prescale[v][1];

	v = (multInt >> 0) & 0xff;
	multInt_maindivide = FeedbackDivider[v][1];

	v = prediv;
	prediv = Predivider[v][1];

	v = (rangeA >> 5) & 0x3;
	rangeA_predivide = Prescale[v][1];

	v = (rangeA >> 0) & 0x1f;
	rangeA_maindivide = ForwardDivider[v][1];

/*
 * As an example of the SYS clock running at 400Mhz:
 *
 * The control register value is 0x02ed4566.  It decodes as:
 *	 prediv  = 0x000
 *	 rangeA  = 0x05d
 *	 multint = 0x2b3
 *
 * To get values for the equation:
 *	 MULTINT.predivide  = 0x02  Translated value from the tables: 2
 *	 MULTINT.maindivide = 0xB3  Translated value from the tables: 16
 *	 PREDIV	     = 0x00  Translated value from the tables: 1
 *	 RANGEA.predivide   = 0x02  Translated value from the tables: 2
 *	 RANGEA.maindivide  = 0x1d  Translated value from the tables: 5
 *
 * Filling in the above values:
 *
 * SYSCLK = (CLKI * MULTINT.predivide * MULTINT.maindivide) /
 *	  (PREDIV * RANGEA.predivide * RANGEA.maindivide)
 *	=   (125Mhz * 2 * 16) / (1 * 2 * 5)
 *	=   4000Mhz / 10
 *	=   400Mhz
 */

	SYSCLK = (clki * multInt_predivide * multInt_maindivide) /
		(prediv * rangeA_predivide * rangeA_maindivide);

	(*clockMhz) = SYSCLK;

ai2c_return:
	return ai2cStatus;
}

/*****************************************************************************
 * ACP/AXXIA Memory mapping & Device I/O
 *****************************************************************************/

/*
 * This block of code defines the memory addresses for each h/w block
 * that is accessible as a direct bus i/o operation.
 *
 * IMPORTANT: ALL BUS GROUPINGS MUST BE MAINTAINED
 */
static struct ai2c_dev_page_s ai2c_dev_page[AI2C_DEV_PAGE_END_MARKER] = {
	{
		AI2C_DEV_PAGE_I2C_0, "AXXIA_I2C0", 0, 0x00000000000ULL,
		AI2C_DEV_SIZE_4KB, AI2C_DEV_ACCESS_LITTLE_ENDIAN,
		AI2C_PAGE_FLAGS_I2CBUS, NULL,
	},
	{
		AI2C_DEV_PAGE_I2C_1, "AXXIA_I2C1", 0, 0x00000000000ULL,
		AI2C_DEV_SIZE_4KB, AI2C_DEV_ACCESS_LITTLE_ENDIAN,
		AI2C_PAGE_FLAGS_I2CBUS, NULL,
	},
	{
		AI2C_DEV_PAGE_I2C_2, "AXXIA_I2C2", 0, 0x00000000000ULL,
		AI2C_DEV_SIZE_4KB, AI2C_DEV_ACCESS_LITTLE_ENDIAN,
		AI2C_PAGE_FLAGS_I2CBUS, NULL,
	},
	{
		AI2C_DEV_PAGE_I2C_3, "AXXIA_SMB", 0, 0x00000000000ULL,
		AI2C_DEV_SIZE_4KB, AI2C_DEV_ACCESS_LITTLE_ENDIAN,
		AI2C_PAGE_FLAGS_I2CBUS, NULL,
	},
	{
		AI2C_DEV_PAGE_END_MARKER, NULL, 0, 0x00000000000ULL, 0, 0,
		AI2C_PAGE_FLAGS_NONE, NULL,
	},
	{
		AI2C_DEV_PAGE_END_MARKER, NULL, 0, 0x00000000000ULL, 0, 0,
		AI2C_PAGE_FLAGS_NONE, NULL,
	},
	{
		AI2C_DEV_PAGE_END_MARKER, NULL, 0, 0x00000000000ULL, 0, 0,
		AI2C_PAGE_FLAGS_NONE, NULL,
	},
	{
		AI2C_DEV_PAGE_END_MARKER, NULL, 0, 0x00000000000ULL, 0, 0,
		AI2C_PAGE_FLAGS_NONE, NULL,
	},
};

static struct ai2c_dev_chip_entry_s ai2c_chip_id[] = {
	{ AI2C_CHIP_ACP55xx, "AXM55xx", 4, &ai2c_axm5500_cfg, },
	{ AI2C_CHIP_ACP35xx, "AXM35xx", 3, &ai2c_axm5500_cfg, },
};

static u32 ai2c_chip_id_count = sizeof(ai2c_chip_id)/
				sizeof(struct ai2c_dev_chip_entry_s);

	/* Region Map
	 *   Note: Must be same number of entries (and in same order) as
	 *	 the "AI2C_DEV_PAGE_xxx" enumeration.
	 */

static struct ai2c_access_map ai2cDummyRegionMap[] = AI2C_DUMMY_REGION_MAP_INIT;

static struct ai2c_region_io ai2c_region_io_map[] = {
	/* 323.0 */
	{
		AI2C_REGION_I2C_0, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_I2C_0,
	},
	/* 332.0 */
	{
		AI2C_REGION_I2C_1, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_I2C_1,
	},
	/* 332.0 */
	{
		AI2C_REGION_I2C_2, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_I2C_2,
	},
	/* 348.0 */
	{
		AI2C_REGION_I2C_3, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_I2C_3,
	},
	/* 320.0 */
	{
		AI2C_REGION_GPIO_0, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_GPIO_0,
	},
	/* 398.0 */
	{
		AI2C_REGION_RESET_CTRL, ai2cDummyRegionMap,
		__ai2c_dev_dcr_read, __ai2c_dev_dcr_write,
		AI2C_DEV_PAGE_RESET_CTRL,
	},
	/* 326.0 */
	{
		AI2C_REGION_TIMER, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_TIMER,
	},
	/* 329.0 */
	{
		AI2C_REGION_GPREG, ai2cDummyRegionMap,
		__ai2c_dev_direct_read, __ai2c_dev_direct_write,
		AI2C_DEV_PAGE_GPREG,
	},
};

static const u32 ai2c_region_pages_max =
	sizeof(ai2c_region_io_map) / sizeof(struct ai2c_region_io);


/*****************************************************************************
 * Miscellaneous Utility functions
 *****************************************************************************/

u32 ai2c_page_to_region(
	struct ai2c_priv	  *priv,
	u32	 pageId)
{
	int i;
	for (i = 0; i < ai2c_region_pages_max; i++)
		if (pageId == ai2c_region_io_map[i].pageId)
			return ai2c_region_io_map[i].regionId;
	return AI2C_REGION_NULL;
}

struct ai2c_region_io *ai2c_region_lookup(
	struct ai2c_priv	  *priv,
	u32      regionId)
{
	int i;
	for (i = 0; i < ai2c_region_pages_max; i++)
		if (regionId == ai2c_region_io_map[i].regionId)
			return &ai2c_region_io_map[i];
	return NULL;
}

/*****************************************************************************
 * Read/Write ACP Memory Spaces
 *****************************************************************************/

/*
 * ai2c_dev_direct_read
 *
 *   Perform 32-bit AI2C device I/O to non-ConfigRing region.
 */
int ai2c_dev_direct_read(
	struct ai2c_priv        *priv,
	struct ai2c_region_io   *region,
	u64	offset,
	u32      *buffer,
	u32	count,
	u32	flags,
	u32	cmdType,
	u32	xferWidth)
{
	int           st = 0;
	u32       endianness;
	unsigned long       busAddr;
	u32       i;

	AI2C_MSG(AI2C_MSG_ENTRY,
		"direct_read enter: %x.%x.%llx %d\n",
		AI2C_NODE_ID(region->regionId),
		AI2C_TARGET_ID(region->regionId),
		(unsigned long long) offset, count);

	if (priv->pageAddr[region->pageId] == 0) {
		st = -EBADSLT;
		goto cleanup;
	}

	busAddr = AI2C_DEV_BUS_ADDR(priv, region->pageId, offset);
	endianness = AI2C_DEV_PAGE_ENDIANNESS(region->pageId);

	switch (xferWidth) {
	case 4:
		for (i = 0; i < count; i++, busAddr += 4, offset += 4) {
			buffer[i] = AI2C_BUS_READ32(busAddr, endianness);
			AI2C_MSG(AI2C_MSG_IOR,
				"direct_read: region=%x offset = %llx "
				"busAddr=%lx v=%x\n",
				region->regionId, offset, busAddr, buffer[i]);
		}
		break;
	case 2:
		{
			u16 *p16 = (u16 *) buffer;
			for (i = 0; i < count; i++, busAddr += 2)
				p16[i] = AI2C_BUS_READ16(busAddr, endianness);
		}
		break;
	case 1:
		{
			u8 *p8 = (u8 *) buffer;
			for (i = 0; i < count; i++, busAddr += 1)
				p8[i] = AI2C_BUS_READ8(busAddr);
		}
		break;
	default:
		st = -EACCES;
		break;
	}

cleanup:
	AI2C_MSG(AI2C_MSG_EXIT,
		"direct_read exit: st=%d %x.%x.%llx=0x%08x\n",
		st, AI2C_NODE_ID(region->regionId),
		AI2C_TARGET_ID(region->regionId), (unsigned long long) offset,
		buffer[0]);
	return (int) st;
}

/*
 * ai2c_dev_direct_write
 *
 *   Perform 32-bit AI2C device I/O to non-ConfigRing region.
 */
int ai2c_dev_direct_write(
	struct ai2c_priv        *priv,
	struct ai2c_region_io   *region,
	u64	offset,
	u32      *buffer,
	u32	count,
	u32	flags,
	u32	cmdType,
	u32	xferWidth)
{
	int           st = 0;
	u32       endianness;
	unsigned long       busAddr;
	u32       i;

	AI2C_MSG(AI2C_MSG_ENTRY,
		"direct_write enter: %x.%x.%llx 0x%08x (%d)\n",
		AI2C_NODE_ID(region->regionId),
		AI2C_TARGET_ID(region->regionId),
		(unsigned long long) offset,
		buffer[0], count);

	if (priv->pageAddr[region->pageId] == 0) {
		st = -EBADSLT;
		goto cleanup;
	}

	busAddr = AI2C_DEV_BUS_ADDR(priv, region->pageId, offset);
	endianness = AI2C_DEV_PAGE_ENDIANNESS(region->pageId);

	switch (xferWidth) {
	case 4:
		for (i = 0; i < count; i++, busAddr += 4, offset += 4) {
			AI2C_BUS_WRITE32(busAddr, buffer[i], endianness);
			AI2C_MSG(AI2C_MSG_IOW,
				"direct_write: region=%x offset=%llx "
				"busAddr=%lx v=%x\n",
				region->regionId, offset, busAddr, buffer[i]);
		}
		break;

	case 2:
		{
			u16 *buf16 = (u16 *) buffer;
			for (i = 0; i < count; i++, busAddr += 2) {
				AI2C_BUS_WRITE16(busAddr, buf16[i], endianness);
				AI2C_MSG(AI2C_MSG_IOW,
					"direct_write: region=%x offset=%llx "
					"busAddr=%lx v=%x\n",
					region->regionId,
					offset, busAddr, buf16[i]);
			}
		}
		break;
	case 1:
		{
			u8 *buf8 = (u8 *) buffer;
			for (i = 0; i < count; i++, busAddr++) {
				AI2C_BUS_WRITE8(busAddr, buf8[i]);
				AI2C_MSG(AI2C_MSG_IOW,
					"direct_write: region=%x offset=%llx "
					"busAddr=%lx v=%x\n",
					region->regionId,
					offset, busAddr, buf8[i]);
			}
		}
		break;
	default:
		st = -EACCES;
		break;
	}

cleanup:
	AI2C_MSG(AI2C_MSG_EXIT, "direct_write exit st=%d\n", st);
	return (int) st;
}

/*
 * ai2c_dev_read32
 *
 */
int ai2c_dev_read32(
	struct ai2c_priv         *priv,
	u32     regionId,
	u64        offset,
	u32       *buffer)
{
	int	ai2cStatus = 0;
	struct ai2c_region_io *region = ai2c_region_lookup(priv, regionId);
	unsigned long lflags = 0;

	AI2C_SPINLOCK_INTERRUPT_DISABLE(&priv->regLock, lflags);

	AI2C_MSG(AI2C_MSG_ENTRY,
		"dev_read32 enter: %x.%x.%llx %d\n",
		AI2C_NODE_ID(regionId), AI2C_TARGET_ID(regionId),
		(unsigned long long) offset, 1);

	if (region) {
		ai2cStatus =
			AI2C_EDEV_BUS_BLOCK_READ32(priv,
				region->pageId, offset, 1, buffer);

	} else {

#ifdef CONFIG_LSI_UBOOTENV
		ai2cStatus = ncr_read(regionId, (u32) offset,
			1 * sizeof(u32), buffer);
#else
		ai2cStatus = -ENOSYS;
#endif
	}

	AI2C_SPINLOCK_INTERRUPT_ENABLE(&priv->regLock, lflags);

	return ai2cStatus;
}

/*
 * ai2c_dev_write32
 *
 */
int ai2c_dev_write32(
	struct ai2c_priv         *priv,
	u32     regionId,
	u64        offset,
	u32        buffer)
{
	int ai2cStatus = 0;
	struct ai2c_region_io    *region = ai2c_region_lookup(priv, regionId);
	unsigned long lflags = 0;

	AI2C_SPINLOCK_INTERRUPT_DISABLE(&priv->regLock, lflags);

	AI2C_MSG(AI2C_MSG_ENTRY,
		"dev_write32 enter: %x.%x.%llx 0x%08x (%d)\n",
		AI2C_NODE_ID(regionId), AI2C_TARGET_ID(regionId),
		(unsigned long long) offset, (unsigned int)&buffer, 1);

	if (region) {
		ai2cStatus =
			AI2C_EDEV_BUS_BLOCK_WRITE32(priv,
				region->pageId, offset, 1,
			&buffer);

	} else {

#ifdef CONFIG_LSI_UBOOTENV
	ai2cStatus = ncr_write(regionId, (u32) offset,
		1 * sizeof(u32), &buffer);
#else
	ai2cStatus = -ENOSYS;
#endif
	}

	AI2C_SPINLOCK_INTERRUPT_ENABLE(&priv->regLock, lflags);

	return ai2cStatus;
}

/*
 * ai2c_dev_dcr_read
 *
 *   Perform 32-bit AI2C device I/O to non-Config Ring region.
 */
int ai2c_dev_dcr_read(
	struct ai2c_priv      *priv,
	struct ai2c_region_io *region,
	u64	  offset,
	u32    *buffer,
	u32	  count,
	u32	  flags,
	u32	  cmdType,
	u32	  xferWidth)
{
	return -ENOSYS;
}

/*
 * ai2c_dev_dcr_write
 *
 *   Perform 32-bit AI2C device I/O from non-Config Ring region.
 */
int ai2c_dev_dcr_write(
	struct ai2c_priv         *priv,
	struct ai2c_region_io    *region,
	u64	offset,
	u32       *buffer,
	u32	count,
	u32	flags,
	u32	cmdType,
	u32	xferWidth)
{
	return -ENOSYS;
}

/*****************************************************************************
 * Basic configuration Fill-in
 *****************************************************************************/

static int ai2c_getChipType(struct ai2c_priv *priv)
{
	int            ai2cStatus = AI2C_ST_SUCCESS;
	u32	       i;
#ifdef CONFIG_LSI_UBOOTENV
	ai2c_bool_t    has_ECID = TRUE;
	u32	       rev_reg;
	u32	       pt_reg;
	ai2c_cfg_node_node_cfg_r_t  node_cfg;
	ai2c_cfg_node_node_info_0_r_t node_info;

	/*
	 * Determine device revision
	 */

	/* Read the NCA local config node to see if we are an ASIC or FPGA */
	AI2C_CALL(ai2c_dev_read32(priv, AI2C_REGION_NCA_CFG,
		AI2C_CFG_NODE_NODE_CFG,
		(u32 *) &node_cfg));
	AI2C_CALL(ai2c_dev_read32(priv, AI2C_REGION_NCA_CFG,
		AI2C_CFG_NODE_NODE_INFO_0,
		(u32 *) &node_info));

	if (node_cfg.fpga) {
		priv->hw_rev.isFpga = 1;
		/* v1 FPGA doesn't have the ECID block */
		if (node_info.module_revision == 0)
			has_ECID = FALSE;

	} else
		priv->hw_rev.isAsic = 1;

	if (node_info.module_revision == AI2C_CHIP_ACP25xx ||
	    node_info.module_revision == AI2C_CHIP_ACP55xx)
		has_ECID = FALSE;

	/* Get the device chipType/Version from the ECID fuse block */
	if (has_ECID) {

		AI2C_CALL(ai2c_dev_read32(priv,
			AI2C_REGION_ID(AI2C_NODE_X1_ECID, 0x10),
			0x2c, (u32 *) &rev_reg));

		AI2C_CALL(ai2c_dev_read32(priv,
			AI2C_REGION_ID(AI2C_NODE_X1_ECID, 0x10),
			0x20, &pt_reg));

		priv->hw_rev.chipType = (rev_reg & 0x0000001f);
		priv->hw_rev.chipVersion = (rev_reg & 0x000007e0) >> 5;
		priv->hw_rev.cpuDisable = (rev_reg & 0x00007800) >> 11;
		priv->hw_rev.sppDisable = (rev_reg & 0x00008000) >> 15;

		priv->hw_rev.packageType = (pt_reg & 0xf0000000) >> 28;
	} else {
		/* if we don't have an ECID just use the NCA module version */
		priv->hw_rev.chipType = node_info.module_revision;
		priv->hw_rev.chipVersion = 0;
		priv->hw_rev.packageType = 0;
		priv->hw_rev.cpuDisable = 0;
		priv->hw_rev.sppDisable = 0;
	}

	/* fixup chipType for ACP344x variants */
	switch (priv->hw_rev.chipType) {
	case 3:
	case 4:
		priv->hw_rev.chipType = AI2C_CHIP_ACP34xx;
		break;
	case 5:
		priv->hw_rev.chipType = AI2C_CHIP_ACP34xx;
		break;
	default:
		break;
	}
#endif

	/* Environment variable override */
	if (ai2c_chip_ver != -1) {
		priv->hw_rev.chipType    = ai2c_chip_ver;
		priv->hw_rev.chipVersion = 0;
	}
#ifdef AI2C_CHIP_VER
	else {
		priv->hw_rev.chipType    = AI2C_CHIP_VER;
		priv->hw_rev.chipVersion = 0;
	}
#endif

	for (i = 0; i < ai2c_chip_id_count; i++) {
		if (ai2c_chip_id[i].chipType == priv->hw_rev.chipType) {
			priv->busCfg = &ai2c_chip_id[i];
			priv->numActiveBusses = ai2c_chip_id[i].numActiveBusses;
		}
	}
	if (priv->busCfg == NULL) {
		ai2cStatus = -ENXIO;
		goto ai2c_return;
	}

	AI2C_LOG(AI2C_MSG_INFO, "%s %d.%d.%d %s\n",
		priv->busCfg->chipName,
		priv->hw_rev.chipType, priv->hw_rev.chipVersion,
		priv->hw_rev.packageType,
		(priv->hw_rev.isFpga) ? "FPGA" : "ASIC");

ai2c_return:
	return ai2cStatus;
}

int ai2c_stateSetup(
    struct ai2c_priv           **outPriv)
{
	int                     ai2cStatus = AI2C_ST_SUCCESS;
	struct ai2c_priv        *priv = NULL;

	/* Now for the private memory for this module. */
	priv = ai2c_malloc(sizeof(struct ai2c_priv));
	if (!priv) {
		AI2C_LOG(AI2C_MSG_ERROR,
			"Could not allocate AI2C private memory root!\n");
		ai2cStatus = -ENOMEM;
		goto ai2c_return;
	}
	memset(priv, 0, sizeof(struct ai2c_priv));

	/* Check chipType/chipVersion fields of 0xa.0x10.0x2c, first */
	ai2cStatus = ai2c_getChipType(priv);
	if (ai2cStatus != AI2C_ST_SUCCESS)
		goto ai2c_return;

ai2c_return:
	if (ai2cStatus != AI2C_ST_SUCCESS)
		(*outPriv) = NULL;
	else
		(*outPriv) = priv;

	return ai2cStatus;
}

int ai2c_memSetup(
    struct platform_device      *pdev,
    struct ai2c_priv            *priv)
{
	int                     ai2cStatus = AI2C_ST_SUCCESS;
	struct axxia_i2c_bus_platform_data  *pdata;
	u32                     busNdx;
	int                     i;

	/* Where is the current I2C device found on this platform? */
	pdata = (struct axxia_i2c_bus_platform_data *) pdev->dev.platform_data;
	if (pdata == NULL) {
		AI2C_LOG(AI2C_MSG_ERROR,
			"Can't find platform-specific data!\n");
		ai2cStatus = -ENXIO;
		goto ai2c_return;
	}
	busNdx = pdata->index;

	priv->pages = ai2c_dev_page;

	if (busNdx > (priv->numActiveBusses-1)) {
		AI2C_LOG(AI2C_MSG_ERROR, "Invalid I2C bus index (%d)\n",
			busNdx);
		ai2cStatus = -ENXIO;
		goto ai2c_return;
	}

	priv->pages[busNdx].busName = &pdata->name[0];
	priv->pages[busNdx].bus_nr  = pdata->bus_nr;
	priv->pages[busNdx].busAddr = pdata->dev_space.start;
	priv->pages[busNdx].size    =
		pdata->dev_space.end - pdata->dev_space.start + 1;
	priv->pages[busNdx].pdata   = pdata;

	AI2C_LOG(AI2C_MSG_DEBUG,
		"[%d] ba=0x%010llx (%llx, %llx) sz=0x%x\n",
		busNdx,
		priv->pages[busNdx].busAddr,
		pdata->dev_space.start, pdata->dev_space.end,
		priv->pages[busNdx].size);

	/*
	* Interrupt for this bus is in priv->pdata[i].int_space.start
	*/

	/*
	* Program Address Map driver tables
	*/
	if (priv->pageAddr == NULL) {
		priv->pageAddr =
			ai2c_malloc(AI2C_DEV_PAGE_END_MARKER * sizeof(u32));
		if (priv->pageAddr == NULL) {
			AI2C_LOG(AI2C_MSG_ERROR,
				"Could not allocate AI2C pageAddr memory!\n");
			ai2cStatus = -ENOMEM;
			goto ai2c_return;
		}
		memset(priv->pageAddr, 0,
			AI2C_DEV_PAGE_END_MARKER * sizeof(u32));
	}

	for (i = 0; i < AI2C_DEV_PAGE_END_MARKER; i++) {

		if (priv->pageAddr[i] ||
		    (priv->pages[i].busAddr == 0) ||
		    (priv->pages[i].size == 0) ||
		    (priv->pages[i].pageId == AI2C_DEV_PAGE_END_MARKER))
			continue;

		priv->pageAddr[i] =
			(u32) ioremap(priv->pages[i].busAddr,
					priv->pages[i].size);
		if (priv->pageAddr[i] == 0) {
			AI2C_LOG(AI2C_MSG_ERROR,
				"Could not ioremap AI2C pageAddr memory %d!\n",
				i);
			AI2C_LOG(AI2C_MSG_DEBUG,
				"ba=0x%010llx sz=0x%x\n",
				priv->pages[i].busAddr,
				priv->pages[i].size);
			ai2cStatus = -ENOMEM;
			goto ai2c_return;
		} else {
			AI2C_LOG(AI2C_MSG_DEBUG,
				"Map page %d (%08x) / %llx for %x => %x\n",
				priv->pages[i].pageId,
				ai2c_page_to_region(priv,
						priv->pages[i].pageId),
				(unsigned long long) priv->pages[i].busAddr,
				priv->pages[i].size,
				priv->pageAddr[i]);
		}
	}

	AI2C_SPINLOCK_INIT(&priv->regLock);
	AI2C_SPINLOCK_INIT(&priv->ioLock);

ai2c_return:

	if (ai2cStatus != AI2C_ST_SUCCESS) {
		if (priv) {
			if (priv->pageAddr) {
				for (i = 0; i < AI2C_DEV_PAGE_END_MARKER; i++)
					if (priv->pageAddr[i] != 0)
						iounmap(
						    (void __iomem *)
						    priv->pageAddr[i]);
				ai2c_free(priv->pageAddr);
			}
			ai2c_free(priv);
		}
	}

	return ai2cStatus;
}

int ai2c_memDestroy(struct ai2c_priv *inPriv)
{
	int	    ai2cStatus = AI2C_ST_SUCCESS;
	int         i;

	if (inPriv) {
		if (inPriv->pageAddr) {
			for (i = 0; i < AI2C_DEV_PAGE_END_MARKER; i++)
				if (inPriv->pageAddr[i] != 0)
					iounmap((void *)inPriv->pageAddr[i]);

			ai2c_free(inPriv->pageAddr);
		}

		ai2c_free(inPriv);
	}

	return ai2cStatus;
}

/*****************************************************************************
 * I2C Algorithm
 *****************************************************************************/

static int ai2c_master_xfer(
	struct i2c_adapter  *adap,
	struct i2c_msg       msgs[],
	int		     num)
{
	u32  regionId = (u32) i2c_get_adapdata(adap);
	struct ai2c_priv	 *priv = ai2cState;
	int		  err = 0;
	int		  i;

	AI2C_LOG(AI2C_MSG_ENTRY, ">>>Enter ai2c_master_xfer\n");

	for (i = 0; (i < num) && (err == 0); i++) {

		int stop = (i == (num - 1)) ? 1 : 0;

		if (msgs[i].flags & I2C_M_RD) {

#ifdef DATA_STREAM_DEBUG
			int j;
			char buf[80];
			strcat(buf, "mstRead:");
#endif /* DATA_STREAM_DEBUG */

			err = priv->busCfg->api->rdFn(priv, regionId,
						      adap, &msgs[i], stop);

#ifdef DATA_STREAM_DEBUG
			for (j = 0; j < msgs[i].len; j++) {

				char hb[4];
				sprintf(hb, " %02x", msgs[i].buf[j]);
				strcat(buf, hb);
			}
			printk(KERN_INFO "%s\n", buf);
#endif /* DATA_STREAM_DEBUG */

		} else {

#ifdef DATA_STREAM_DEBUG
			int j;
			char buf[80];
			strcat(buf, "mstWrite:");
			for (j = 0; j < msgs[i].len; j++) {
				char hb[4];
				sprintf(hb, " %02x", msgs[i].buf[j]);
				strcat(buf, hb);
			}
			printk(KERN_INFO "%s\n", buf);
#endif /* DATA_STREAM_DEBUG */

			err = priv->busCfg->api->wrFn(priv, regionId,
						      adap, &msgs[i], stop);
		}
	}

	AI2C_LOG(AI2C_MSG_EXIT, ">>>Exit ai2c_master_xfer %d\n", err);
	return err;
}


static u32 ai2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm ai2c_algorithm = {
	.master_xfer	= ai2c_master_xfer,
	.functionality      = ai2c_func,
};


/*****************************************************************************
 * Device Probe/Setup
 *****************************************************************************/

static int __devinit ai2c_probe(struct platform_device *pdev)
{
	int                     ai2cStatus = AI2C_ST_SUCCESS;
	struct ai2c_priv	*priv = NULL;
	struct axxia_i2c_bus_platform_data  *pdata;
	u32                     busNdx;
	u32                     rid;

	/* Initialization of externals, initial state */
	AI2C_MSG_TRACE_LEVEL = (AI2C_MSG_INFO | AI2C_MSG_ERROR);
	ai2c_chip_ver = -1;

	AI2C_LOG(AI2C_MSG_ENTRY, ">>>Enter ai2c_probe/init\n");

	/* Global state across all of the same kind of platforms */
	if (ai2cState == NULL) {
		AI2C_CALL(ai2c_stateSetup(&priv));
		ai2cState = priv;
	} else {
		priv = ai2cState;
	}

	/* State memory for each instance of the platform */
	if (ai2cModState == NULL) {
		ai2cModState =
			ai2c_malloc(priv->numActiveBusses *
				    sizeof(struct local_state));
		if (!ai2cModState) {
			ai2cStatus = -ENOMEM;
			goto exit_release;
		}
		memset(ai2cModState, 0,
			priv->numActiveBusses * sizeof(struct local_state));
	}

	/* Associate this platform with the correct bus entry */
	AI2C_CALL(ai2c_memSetup(pdev, priv));
	pdata = (struct axxia_i2c_bus_platform_data *) pdev->dev.platform_data;
	busNdx = pdata->index;

	/* Hook up bus driver(s) and devices to tree */
	if ((busNdx > (priv->numActiveBusses-1)) ||
	    (priv->pages[busNdx].busName == NULL)) {
		printk(KERN_ERR
			"Invalid description for adding I2C adapter [%d]\n",
			busNdx);
		goto exit_release;
	}
	if (ai2cModState[busNdx].adapter.algo != NULL) {
		printk(KERN_ERR
			"Duplicate I2C bus %d description found\n", busNdx);
		goto exit_release;
	}

	rid = ai2c_page_to_region(priv, priv->pages[busNdx].pageId);
	i2c_set_adapdata(&ai2cModState[busNdx].adapter, (void *)rid);

	snprintf(ai2cModState[busNdx].adapter.name,
		sizeof(ai2cModState[busNdx].adapter.name),
		"%s", ai2cState->pages[busNdx].busName);
	ai2cModState[busNdx].adapter.algo = &ai2c_algorithm;
	ai2cModState[busNdx].adapter.owner = THIS_MODULE;
	ai2cModState[busNdx].adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	ai2cModState[busNdx].adapter.retries = 3;
		/* Retry up to 3 times on lost
		 * arbitration */
	ai2cModState[busNdx].adapter.dev.parent = &pdev->dev;
	ai2cModState[busNdx].adapter.dev.of_node = NULL;

	/* Add I2C adapter to I2C tree */
	if (priv->pages[busNdx].bus_nr != (~0)) {
		ai2cModState[busNdx].adapter.nr = priv->pages[busNdx].bus_nr;
		ai2cStatus =
			i2c_add_numbered_adapter(&ai2cModState[busNdx].adapter);
	} else {
		ai2cStatus = i2c_add_adapter(&ai2cModState[busNdx].adapter);
	}
	if (ai2cStatus) {
		printk(KERN_ERR "Failed to add I2C adapter [%d]\n", busNdx);
		goto exit_release;
	}

	/* Any detailed bus-specific initialization */
	ai2cStatus = priv->busCfg->api->initFn(priv, rid);
	if (ai2cStatus)
		goto exit_release;

	platform_set_drvdata(pdev, priv);

	AI2C_LOG(AI2C_MSG_EXIT, ">>>Exit ai2c_probe/init %d\n", 0);
	return 0;

ai2c_return:
	if (ai2cStatus != -ENOMEM)
		ai2cStatus = -ENOSYS;

exit_release:
	ai2c_memDestroy(priv);

	AI2C_LOG(AI2C_MSG_EXIT, ">>>Exit ai2c_probe/init %d\n", ai2cStatus);
	return ai2cStatus;
}

static int __devexit ai2c_remove(struct platform_device *dev)
{
	int	 i;

	AI2C_LOG(AI2C_MSG_ENTRY, ">>>Enter ai2c_remove/exit\n");

	if (ai2cState != NULL) {

		if (ai2cModState != NULL) {

			for (i = 0; i < ai2cState->numActiveBusses; i++) {

				if (ai2cModState[i].client)
					i2c_unregister_device(
						ai2cModState[i].client);
				i2c_del_adapter(&ai2cModState[i].adapter);
			}
			ai2c_free(ai2cModState);
		}

		ai2c_memDestroy(ai2cState);
	}

	platform_set_drvdata(dev, NULL);

	AI2C_LOG(AI2C_MSG_EXIT, ">>>Exit ai2c_remove/exit %d\n", 0);

	return 0;
}

/* ------------------------------------------------------------------------- */

#define ai2c_suspend	NULL
#define ai2c_resume	NULL

static struct platform_driver ai2c_driver = {
	.driver = {
		.name   = "axxia_ai2c",	 /* Must match with platform-specific
					 * code! */
		.owner  = THIS_MODULE,
	},
	.probe      = ai2c_probe,
	.remove     = __devexit_p(ai2c_remove),
	.suspend    = ai2c_suspend,
	.resume     = ai2c_resume,
};

static int __init ai2c_init(void)
{
	AI2C_LOG(AI2C_MSG_ENTRY, ">>>Enter ai2c_init\n");
	return platform_driver_register(&ai2c_driver);
}

static void __exit ai2c_exit(void)
{
	AI2C_LOG(AI2C_MSG_ENTRY, ">>>Enter ai2c_exit\n");
	platform_driver_unregister(&ai2c_driver);
}

module_init(ai2c_init);
module_exit(ai2c_exit);
