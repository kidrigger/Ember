#pragma once
#include "DeviceHandle.hpp"
#include "RenderDevice.hpp"
#include "Texture.hpp"

namespace Ember
{

class Environment
{
public:
  struct GpuRepr
  {
    SRVHandle Skybox;
    SRVHandle DiffuseIrradiance;
    SRVHandle Prefilter;
    SRVHandle BrdfLUT;
  };

private:
  Texture m_Skybox;
  Texture m_DiffuseIrradiance;
  Texture m_Prefilter;
  Texture m_BrdfLUT;
  GpuRepr m_Repr;

public:
  Environment() = default;

  Environment( Texture skybox, Texture diffuse_irradiance, Texture prefilter, Texture brdf_lut );

  [[nodiscard]] GpuRepr const& Repr() const;

  //
  static bool TryLoadFrom(
      Environment* env, RenderDevice* render_device, TextureLoader* texture_loader, char const* env_map_file );
};

} // namespace Ember
