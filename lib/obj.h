#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "../ps1/cop0.h"
#include "../ps1/gpucmd.h"
#include "../ps1/gte.h"
#include "../ps1/registers.h"
#include "trig.h"

#pragma once

typedef struct {
	uint16_t  vertices[3];
    uint16_t  _padding;
	uint32_t color;
} Face;

#define NUM_PLAYER_VERTICES 170
#define NUM_PLAYER_FACES 336
extern const GTEVector16 playerVertices[NUM_PLAYER_VERTICES];
extern const Face playerFaces[NUM_PLAYER_FACES];

#define PLAYER_TEXTURE_LEN 2048
#define PLAYER_PALETTE_LEN 32

//defined pre build externally
extern const uint8_t playerTexture[PLAYER_TEXTURE_LEN], playerPalette[PLAYER_PALETTE_LEN];
TextureInfo playerTextInfo;

#define NUM_LEVEL_VERTICES 780
#define NUM_LEVEL_FACES 1556
extern const GTEVector16 levelVertices[NUM_LEVEL_VERTICES];
extern const Face levelFaces[NUM_LEVEL_FACES];
