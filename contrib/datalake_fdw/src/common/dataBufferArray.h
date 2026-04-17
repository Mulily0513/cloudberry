#ifndef DATALAKE_DATABUFFERARRAY_H
#define DATALAKE_DATABUFFERARRAY_H

extern "C" {
#include "postgres.h"
}
#include <stdio.h>
#include <vector>

#define BUFFER_LENGTH (32*1024)
namespace Datalake {
namespace Internal {

typedef struct dataBuff
{
    char *buffer;
    int length;
} dataBuffer;

class dataBufferArray
{
public:
	dataBufferArray() {
		mColumns = 0;
		mDataBuffer.clear();
	}

    void allocDataBufferArray(int columns);

    void copyToDataBuffer(char *data, int size, int column);

    void resizeDataBuffer(int column, int length);

    void freeDataBuffer();

    inline dataBuffer *getDataBuffer(int column) { return mDataBuffer[column]; }

private:
    std::vector<dataBuffer*> mDataBuffer;
    MemoryContext alloc_ctx;
    int mColumns;
};

}
}
#endif