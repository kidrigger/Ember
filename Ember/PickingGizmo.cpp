#include "PickingGizmo.hpp"

#include <imgui.h>

#include <ImGuizmo.h>

#include "Camera.hpp"
#include "Scene.hpp"

Ember::PickingGizmo::PickingGizmo()
  : m_CurrentGizmoOperation{ ImGuizmo::TRANSLATE }, m_CurrentGizmoMode{ ImGuizmo::WORLD }
{}

void Ember::PickingGizmo::DrawGizmo( Camera const& camera, flecs::entity const selected ) const
{
  LocalTransform&       lt               = selected.get_mut<LocalTransform>();
  WorldTransform const& wt               = selected.get<WorldTransform>();

  WorldTransform const* parent_transform = nullptr;
  if ( auto parent = selected.parent(); parent.is_valid() )
  {
    parent_transform = parent.try_get<WorldTransform>();
  }

  ImGuizmo::BeginFrame();
  ImGuiIO& io = ImGui::GetIO();
  ImGuizmo::SetRect( 0, 0, io.DisplaySize.x, io.DisplaySize.y );

  DirectX::XMFLOAT4X4 view, proj;
  XMStoreFloat4x4( &view, camera.GetView() );
  XMStoreFloat4x4( &proj, camera.GetProj() );

  DirectX::XMFLOAT4X4 wt_mat;
  XMStoreFloat4x4( &wt_mat, wt.Transform );

  ImGuizmo::Manipulate(
      ( float* )&view,
      ( float* )&proj,
      m_CurrentGizmoOperation,
      m_CurrentGizmoMode,
      ( float* )&wt_mat,
      nullptr,
      nullptr );

  DirectX::XMMATRIX local_mat = XMLoadFloat4x4( &wt_mat );
  if ( parent_transform )
  {
    local_mat = XMMatrixMultiply( local_mat, parent_transform->InvTransform );
  }
  lt.SetTransform( local_mat );
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
