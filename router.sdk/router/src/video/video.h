/*
 * video.h
 *
 *  Created on: Jun 3, 2019
 *      Author: oslab
 */

#ifndef SRC_VIDEO_VIDEO_H_
#define SRC_VIDEO_VIDEO_H_

#include "../structs.h"

void initVideo();
void renderData(struct Route *routingTable, u32 routingTableSize, char *stats, u32 time, int *packets, int *bytes);


#endif /* SRC_VIDEO_VIDEO_H_ */
