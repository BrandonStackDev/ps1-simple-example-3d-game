#include "../ps1/cop0.h"
#include "../ps1/gpucmd.h"
#include "../ps1/gte.h"
#include "../ps1/registers.h"
#include "../lib/gpu.h"
#include "../lib/draw.h"
#include "../lib/pad.h"
#include "font.h"

#pragma once

static void GeneralSetup()
{
    //init stuff
	initSerialIO(115200);
	initControllerBus();
	
	//setup gpu
	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL)
	{
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} 
	else 
	{
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	//setup gte
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	//setup dma chain and double buffer
	DMA_DPCR |= 0 | DMA_DPCR_CH_ENABLE(DMA_GPU) | DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	int double_screen = SCREEN_WIDTH << 1;
	//font texture
	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		double_screen,
		0,
		double_screen,
		FONT_HEIGHT,
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);
	int offset_from_font = FONT_HEIGHT+16; //(16 is a guess for font palette size)
	//todo: i need a way to track this and uise the space efficiently but automatically know what x and y's to use
	//player texture
	uploadIndexedTexture(
		&playerTextInfo,
		playerTexture,
		playerPalette,
		double_screen,
		offset_from_font, 
		double_screen,
		offset_from_font + 64, //my image is 64x64, we are storing straight down (probably wastes some x space), so x is same as font, but y needs to be adjusted 
		64,
		64,
		GP0_COLOR_4BPP
	);
}