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

	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,
		0,
		SCREEN_WIDTH * 2,
		FONT_HEIGHT,
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);
}