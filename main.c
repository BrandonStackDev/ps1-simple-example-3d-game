#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "lib/gpu.h"
#include "lib/trig.h"
#include "lib/char.h"
#include "lib/draw.h"



int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	DMA_DPCR |= 0
		| DMA_DPCR_CH_ENABLE(DMA_GPU)
		| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;
	int      frameCounter     = 0;

	while(true)
	{
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		//will be a loop in the future over each object in the display arena
			//draw the ground
			DrawObject(chain, 0,0,0, 0,0,0, NUM_GROUND_FACES, groundFaces, groundVertices);
			//draw the character
			DrawObject(chain, 0,0,128, 0, frameCounter*32, frameCounter*16, NUM_CUBE_FACES, cubeFaces, cubeVertices);
		//finish it up
		FinishDraw(chain, bufferX, bufferY);
		
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
		frameCounter++;
	}
	return 0;
}
