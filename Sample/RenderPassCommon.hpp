#pragma once

#include "LightManager.hpp"

namespace Ember
{

struct PerFrameConstants
{
  CBVHandle             Camera;
  CBVHandle             ConfigBuffer;
  LightManager::GpuInfo LightInfo;
};

} // namespace Ember
