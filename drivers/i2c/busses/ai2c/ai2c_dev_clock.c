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

#include "ai2c_plat.h"
#include "ai2c_dev_clock_ext.h"

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
