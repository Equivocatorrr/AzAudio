/*
	File: azaChannelMatrix.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZACHANNELMATRIX_H
#define AZAUDIO_AZACHANNELMATRIX_H

#include "../channel_layout.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaChannelMatrix {
	uint8_t inputs, outputs;
	float *matrix;
} azaChannelMatrix;

// allocates the matrix initialized to all zeroes
// May return AZA_ERROR_OUT_OF_MEMORY
int azaChannelMatrixInit(azaChannelMatrix *data, uint8_t inputs, uint8_t outputs);
void azaChannelMatrixDeinit(azaChannelMatrix *data);

// Expects data to have been initted with srcLayout.count cols and dstLayout.count rows
// Also assumes the existing values in the matrix are all zero (won't set to zero if they're not)
void azaChannelMatrixGenerateRoutingFromLayouts(azaChannelMatrix *data, azaChannelLayout srcLayout, azaChannelLayout dstLayout);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZACHANNELMATRIX_H
