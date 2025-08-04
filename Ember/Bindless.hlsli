#ifndef BINDLESS_HLSLI_
#define BINDLESS_HLSLI_

const static uint kInvalidIndex = 0xFFFFFFFF;

bool              IsValidHandle( uint handle )
{
  return handle != kInvalidIndex;
}

typedef uint RID;
typedef uint SamplerID;

#endif
