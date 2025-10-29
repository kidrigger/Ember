#pragma once

#include <flecs.h>

namespace Ember
{

class SceneTree
{
  flecs::entity m_Selected;
  char          m_NameBuffer[256]{};

  static bool   HasChildren( flecs::entity e );
  char const*   GetEntityName( flecs::entity e );
  void          Visit( flecs::entity e );

public:
  void                        Draw( flecs::entity root );

  [[nodiscard]] flecs::entity GetSelected() const;
  void                        SetSelected( flecs::entity selected );
};

} // namespace Ember
