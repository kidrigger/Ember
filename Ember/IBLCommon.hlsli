/*
 The Goal is simply to convert from a (0,0,0) to (CubeSide, CubeSide, FaceCount) Invocation ID space to a
 (-1,-1,-1) to (1, 1, 1) space.

| Axis | Layer | Up |
|:----:|:-----:|:--:|
|  +x  |   0   | -y |
|  -x  |   1   | -y |
|  +y  |   2   | +z |
|  -y  |   3   | -z |
|  -z  |   4   | -y |
|  +z  |   5   | -y |
*/
float3 GetCubeDir( uint3 global_invocation_id, float side_length )
{
  float2 face_uv = float2( global_invocation_id.xy ) / side_length; // (0, SideLength) -> (0, 1)
  face_uv        = 2.0f * face_uv - 1.0f;                           // (0, 1) -> (-1, 1)

  switch ( global_invocation_id.z )
  {
    case 0:
      return normalize( float3( 1.0f, -face_uv.y, -face_uv.x ) );  // Face +X; x = 1, y = -v, z = -u
    case 1:
      return normalize( float3( -1.0f, -face_uv.y, face_uv.x ) );  // Face -X; x = -1, y = -v, z = u
    case 2:
      return normalize( float3( face_uv.x, 1.0f, face_uv.y ) );    // Face +Y; x = u, y = 1, z = v
    case 3:
      return normalize( float3( face_uv.x, -1.0f, -face_uv.y ) );  // Face -Y; x=u, y=-1, z=-v
    case 4:
      return normalize( float3( face_uv.x, -face_uv.y, 1.0f ) );   // Face +Z; x=u,y=-v, z=1
    case 5:
      return normalize( float3( -face_uv.x, -face_uv.y, -1.0f ) ); // Face -Z; x=u,y=-v, z=-1
    default:
      // Never reach here.
      return 0.0f;
  }
}
