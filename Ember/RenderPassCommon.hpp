#pragma once

#include "LightManager.hpp"

namespace Ember
{

struct PerFrameConstants
{
  SRVHandle             MaterialsBuffer;
  CBVHandle             Camera;
  CBVHandle             ConfigBuffer;
  LightManager::GpuInfo LightInfo;
};

} // namespace Ember
