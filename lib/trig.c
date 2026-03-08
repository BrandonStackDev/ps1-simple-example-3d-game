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

/// @brief compute index (bucket given quadrant is divided into 32s) 
///      - in quadrant from abs values of x and y (++ quad)
/// @param x expects this to be positive even tho unsigned
/// @param y expects this to be positive even tho unsigned
/// @return 0-32
uint8_t compute_index(int16_t x, int16_t y)
{
	if(y==0)		{return 0;}
	else if(x==0)	{return 32;}
	int r = 0;
	if(x > y)
	{
		int _y = y << 12;
		r = _y/x;
		if(r>3737)		{return 16;}
		else if(r>2221)	{return ((r/267)+2);}
		else 			{return (r/202);}
	}
	else
	{
		int _x = x << 12;
		r = _x/y;
		int i=16;
		if(r>3737)		{return 16;}
		else if(r>2221)	{return 29-(r/267);}
		else 			{return 31-(r/202);}
	}
}

/// @brief arctan from x and y values
/// @param x 
/// @param y 
/// @return returns angle in range 0-4095 where 2048 is PI
int16_t atan2(int16_t x, int16_t y)
{
	int16_t rtn = 0;
	bool xwn = x < 0; //x was negative
	bool ywn = y < 0; //y was negative
	int16_t _x = xwn ? -x: x; //abs x
	int16_t _y = ywn ? -y: y; //abs y
	//compute index, swap rolls if x xor y was negative
	uint8_t i = xwn^ywn ? compute_index(_y,_x) : compute_index(_x,_y);
	if(xwn && !ywn) 		{rtn = (i << 5) + 1024;} 	//-+ quad, i * 32; << 5 is same
	else if(xwn && ywn) 	{rtn = (i << 5) + 2048;} 	//-- quad
	else if(!xwn && ywn) 	{rtn = (i << 5) + 3072;} 	//+- quad
	else  					{rtn = (i << 5);} 			//++ quad
	return (4096-rtn)%4096; //make sure its in range, and reverse (idk why reverse, but that seems to work?)
}
