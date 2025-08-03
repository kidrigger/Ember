
const static uint kInvalidIndex = 0xFFFFFFFF;

bool              IsValidHandle( uint handle )
{
  return handle != kInvalidIndex;
}

typedef uint RID;
typedef uint SamplerID;
