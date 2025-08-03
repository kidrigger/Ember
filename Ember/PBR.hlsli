static const float PI = 3.14159265f;

float              TrowbridgeReitzGGX( float3 normal, float3 halfway, float roughness )
{
  float alpha       = roughness * roughness;
  float alpha2      = alpha * alpha;
  float n_dot_h     = max( dot( normal, halfway ), 0.0f );
  float n_dot_h_2   = n_dot_h * n_dot_h;

  float numerator   = alpha2;
  float denominator = n_dot_h_2 * ( alpha2 - 1.0f ) + 1.0f;
  denominator       = PI * denominator * denominator;

  return numerator / denominator;
}

float GeometrySchlickGGX( float n_dot_v, float roughness )
{
  float r           = roughness + 1.0f;
  float k           = ( r * r ) / 8.0f;

  float numerator   = n_dot_v;
  float denominator = n_dot_v * ( 1.0f - k ) + k;

  return numerator / denominator;
}

float GeometrySmith( float n_dot_v, float n_dot_l, float roughness )
{
  float ggx1 = GeometrySchlickGGX( n_dot_v, roughness );
  float ggx2 = GeometrySchlickGGX( n_dot_l, roughness );

  return ggx1 * ggx2;
}

// https://en.wikipedia.org/wiki/Schlick%27s_approximation
float3 FresnelSchlick( float cosine, float3 f_0 )
{
  return f_0 + ( 1.0f - f_0 ) * pow( clamp( 1.0f - cosine, 0.0f, 1.0f ), 5.0f ); // Clamp to avoid artifacts.
}

// Sebastian Lagarde
float3 FresnelSchlickRoughness( float cosine, float3 f_0, float roughness )
{
  return f_0 + ( max( ( 1.0f - roughness ).xxx, f_0 ) - f_0 ) *
                   pow( clamp( 1.0f - cosine, 0.0f, 1.0f ), 5.0f ); // Clamp to avoid artifacts.
}

struct BRDFCookTorranceGGX
{
  float3 Albedo;
  float  Metallic;
  float3 Normal;
  float  Roughness;
  float3 F0;

  float3 Evaluate( float3 radiance, float3 view_dir, float3 light_dir )
  {
    float3 halfway        = normalize( view_dir + light_dir );

    float  cosine_factor  = max( dot( halfway, view_dir ), 0.0f );
    float  n_dot_v        = max( dot( Normal, view_dir ), 0.0f );
    float  n_dot_l        = max( dot( Normal, light_dir ), 0.0f );

    float  normal_dist    = TrowbridgeReitzGGX( Normal, halfway, Roughness );
    float  geometry       = GeometrySmith( n_dot_v, n_dot_l, Roughness );
    float3 fresnel        = FresnelSchlickRoughness( cosine_factor, F0, Roughness );

    float3 numerator      = ( normal_dist * geometry ) * fresnel;
    float  denominator    = 4.0f * n_dot_v * n_dot_l;
    float3 specular       = numerator / ( denominator + 0.00001f );

    float3 specular_part  = fresnel;
    float3 diffuse_part   = 1.0f - specular_part;

    diffuse_part         *= 1.0f - Metallic;

    return n_dot_l * radiance * ( diffuse_part * Albedo / PI + specular );
  }
};
