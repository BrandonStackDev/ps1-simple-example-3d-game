/*
 * ps1-bare-metal - (C) 2023-2025 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This is a fast lookup-table-less implementation of fixed-point sine and
 * cosine, based on the isin_S4 implementation from:
 *     https://www.coranac.com/2009/07/sines
 */

 #include <stdint.h>
 #include <stdbool.h>
#include "trig.h"


#define A (1 << 12)
#define B 19900
#define	C  3516

int isin(int x) {
	int c = x << (30 - ISIN_SHIFT);
	x    -= 1 << ISIN_SHIFT;

	x <<= 31 - ISIN_SHIFT;
	x >>= 31 - ISIN_SHIFT;
	x  *= x;
	x >>= 2 * ISIN_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}

int isin2(int x) {
	int c = x << (30 - ISIN2_SHIFT);
	x    -= 1 << ISIN2_SHIFT;

	x <<= 31 - ISIN2_SHIFT;
	x >>= 31 - ISIN2_SHIFT;
	x  *= x;
	x >>= 2 * ISIN2_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}


#define CI_GPT_BUCKETS 1024
#define CI_GPT_MAX (CI_GPT_BUCKETS - 1)
#define CI_GPT_HALF (CI_GPT_BUCKETS >> 1)
#define CI_GPT_HALF_MAX (CI_GPT_HALF - 1)

/// @brief chatGPT version of bucket finding for atan2
///			- originally I wrote my own, but it didnt work nearly as well as this (it did work tho...just saying)
/// @param x 
/// @param y 
/// @return 0..CI_GPT_BUCKETS - 1
static uint16_t compute_index(int16_t x, int16_t y)
{
    if (y == 0) {return 0;}
    else if (x == 0) {return CI_GPT_MAX;}
    if (x >= y) // lower half of quadrant
	{
        return (uint16_t)(((int32_t)y * CI_GPT_HALF_MAX) / x);
    } 
	else // upper half of quadrant
	{
        return (uint16_t)(CI_GPT_MAX - (((int32_t)x * CI_GPT_HALF_MAX) / y));
    }
}

/// @brief arctan from x and y values
/// @param y 
/// @param x 
/// @return returns angle in range 0-4095 where 2048 is PI
int16_t atan2(int16_t y, int16_t x)
{
	int16_t rtn = 0;
	bool xwn = x < 0; //x was negative
	bool ywn = y < 0; //y was negative
	int16_t _x = xwn ? -x: x; //abs x
	int16_t _y = ywn ? -y: y; //abs y
	//compute index, swap rolls if x xor y was negative
	uint16_t i = xwn^ywn ? compute_index(_y,_x) : compute_index(_x,_y);
	if(xwn && !ywn) 		{rtn = (i) + 1024;} 	//-+ quad, i * 32; << 5 is same
	else if(xwn && ywn) 	{rtn = (i) + 2048;} 	//-- quad
	else if(!xwn && ywn) 	{rtn = (i) + 3072;} 	//+- quad
	else  					{rtn = (i);} 			//++ quad
	return rtn%4096; //make sure its in range
}
