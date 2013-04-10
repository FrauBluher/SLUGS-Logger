/*
 * Circular Buffer Test Template:
 * Used to test the circular buffer outside of an embedded environment. 
 */
 
#include <string.h> 
#include <stdio.h>
#include <stdlb.h>
#include "../Libs/CircularBuffer.h"


float runningSum = 0;
char checksum(unsigned char * data, int dataSize);

int main {

	CircularBuffer circBuf;

	unsigned char cbData[512];
	char goodSum;


    	if (!CB_Init(&circBuf, cbData, 512)) FATAL_ERROR();

   	// Generate a fake input to record to the circular buffer
    	// give beginning and end special characters
   	unsigned char goodData[512];
   	int i;

	for (i=0; i<512; 
		goodData[i] = (char)
	}

	goodSum = checksum(goodData, 512);
    
	printf("Starting initial checksum calculation.\n");

	while(1)
	{
	    CB_WriteMany(&circBuf, goodData, 512, true);

            unsigned char outData[512];
            if (CB_PeekMany(&circBuf, outData, 512)) {
                    if(checksum(outData, 512) != goodSum) {
                        printf("Checksum value differs after %f tests", runningSum);
			return (1);
                    }
                    CB_Remove(&circBuf, 512);
            }
}


// calculates a basic byte Xor checksum of some data
char checksum(unsigned char *data, int dataSize)
{
    char sum = 0;
    int i;
    for(i = 0; i < dataSize; i++) {
        sum ^= data[i];
    }
    return sum;
}

	return (EXIT_SUCCESS);
}
