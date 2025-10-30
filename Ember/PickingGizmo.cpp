#include "PickingGizmo.hpp"

#include <imgui.h>

#include <ImGuizmo.h>

#include "Camera.hpp"
#include "Scene.hpp"
#include "Util/DataUtil.hpp"

Ember::PickingGizmo::PickingGizmo()
  : m_CurrentGizmoOperation{ ImGuizmo::TRANSLATE }, m_CurrentGizmoMode{ ImGuizmo::WORLD }
{}

void Ember::PickingGizmo::DrawGizmo( Camera const& camera, flecs::entity const selected ) const
{
  WorldTransform const& wt               = selected.get<WorldTransform>();

  WorldTransform const* parent_transform = nullptr;
  if ( flecs::entity const parent = selected.parent(); parent.is_valid() )
  {
    parent_transform = parent.try_get<WorldTransform>();
  }

  ImGuizmo::BeginFrame();
  ImGuiIO const& io = ImGui::GetIO();
  ImGuizmo::SetRect( 0, 0, io.DisplaySize.x, io.DisplaySize.y );

  DirectX::XMFLOAT4X4 view, proj;
  XMStoreFloat4x4( &view, camera.GetView() );
  XMStoreFloat4x4( &proj, camera.GetProj() );

  DirectX::XMFLOAT4X4 wt_mat;
  XMStoreFloat4x4( &wt_mat, wt.Transform );

  bool manipulated = Manipulate(
      ( float* )&view,
      ( float* )&proj,
      m_CurrentGizmoOperation,
      m_CurrentGizmoMode,
      ( float* )&wt_mat,
      nullptr,
      nullptr );

  if ( not manipulated ) return;

  DirectX::XMMATRIX local_mat = XMLoadFloat4x4( &wt_mat );
  if ( parent_transform )
  {
    local_mat = XMMatrixMultiply( local_mat, parent_transform->InvTransform );
  }

  Translation* local_translation = nullptr;
  Rotation*    local_rotation    = nullptr;
  Scale*       local_scale       = nullptr;

  switch ( m_CurrentGizmoOperation )
  {
    case ImGuizmo::TRANSLATE:
      local_translation = &selected.ensure<Translation>();
      break;
    case ImGuizmo::ROTATE:
      local_rotation = &selected.ensure<Rotation>();
      break;
    case ImGuizmo::SCALE:
      local_scale = &selected.ensure<Scale>();
      break;
    default:
      UNREACHABLE;
  }

  TransformUtil::DecomposeMatrixPartial( local_scale, local_rotation, local_translation, local_mat );
}

void Ember::PickingGizmo::DrawMenu()
{
  ImGui::Begin( "Gizmo Menu" );
  ImGui::RadioButton( "Translate", ( int* )&m_CurrentGizmoOperation, ImGuizmo::TRANSLATE );
  ImGui::SameLine();
  ImGui::RadioButton( "Rotate", ( int* )&m_CurrentGizmoOperation, ImGuizmo::ROTATE );
  ImGui::SameLine();
  ImGui::RadioButton( "Scale", ( int* )&m_CurrentGizmoOperation, ImGuizmo::SCALE );
  ImGui::RadioButton( "Local", ( int* )&m_CurrentGizmoMode, ImGuizmo::LOCAL );
  ImGui::SameLine();
  ImGui::RadioButton( "World", ( int* )&m_CurrentGizmoMode, ImGuizmo::WORLD );
  ImGui::End();
}

void Ember::PickingGizmo::Draw( Camera const& camera, flecs::entity const selected )
{
  if ( selected.is_valid() ) DrawGizmo( camera, selected );

  DrawMenu();
}
