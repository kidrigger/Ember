#include "SceneTree.hpp"

#include <Util/DataUtil.hpp>
#include <Util/StringUtil.hpp>
#include <format>
#include <imgui.h>

bool Ember::SceneTree::HasChildren( flecs::entity const e )
{
  bool has_children = false;
  e.children( [&]( flecs::entity ) { has_children = true; } );
  return has_children;
}

char const* Ember::SceneTree::GetEntityName( flecs::entity e )
{
  if ( e.name().size() > 0 )
  {
    return FormatTo( m_NameBuffer, "{}", e.name().c_str() );
  }

  return FormatTo( m_NameBuffer, "Entity {}", e.id() );
}

void Ember::SceneTree::Visit( flecs::entity const e )
{
  ImGuiTreeNodeFlags flags = 0;

  if ( not HasChildren( e ) ) flags |= ImGuiTreeNodeFlags_Leaf;
  if ( m_Selected == e ) flags |= ImGuiTreeNodeFlags_Selected;

  bool const opened = ImGui::TreeNodeEx( GetEntityName( e ), flags );
  if ( ImGui::IsItemClicked() ) SetSelected( e );
  if ( opened )
  {
    e.children( [&]( flecs::entity const child ) { Visit( child ); } );
    ImGui::TreePop();
  }
}

void Ember::SceneTree::Draw( flecs::entity const root )
{
  ImGui::Begin( "Hierarchy" );

  root.children( [&]( flecs::entity const child ) { Visit( child ); } );

  ImGui::End();
}

flecs::entity Ember::SceneTree::GetSelected() const
{
  return m_Selected;
}

void Ember::SceneTree::SetSelected( flecs::entity const selected )
{
  m_Selected = selected;
}
