#pragma once

#include <flecs.h>

namespace ImGuizmo
{
// ReSharper disable once CppInconsistentNaming
enum OPERATION : int;
// ReSharper disable once CppInconsistentNaming
enum MODE : int;
} // namespace ImGuizmo

namespace Ember
{
class Camera;

class PickingGizmo
{
  ImGuizmo::OPERATION m_CurrentGizmoOperation;
  ImGuizmo::MODE      m_CurrentGizmoMode;

  void                DrawGizmo( Camera const& camera, flecs::entity selected ) const;
  void                DrawMenu();

public:
  PickingGizmo();
  void Draw( Camera const& camera, flecs::entity selected );
};

} // namespace Ember
