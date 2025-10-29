#pragma once

#include <flecs.h>
#include <functional>
#include <vector>

#include "Util/DataUtil.hpp"
#include "Util/HelperUtils.hpp"

namespace Ember
{

class InspectorView
{
  using FuncType = void( char const*, void* );

  std::function<FuncType> m_DrawFunc;

public:
  InspectorView() = default;

  explicit InspectorView( std::invocable<char const*, void*> auto&& draw_func ) : m_DrawFunc{ std::move( draw_func ) }
  {}

  void Draw( char const* label, void* data ) const;
};

class Inspector
{
public:
  static void Draw( flecs::world* ecs, flecs::entity entity );

private:
  flecs::world*          m_ECS;
  std::vector<flecs::id> m_Pairs;
  std::vector<flecs::id> m_Tags;

  explicit Inspector( flecs::world* w );

  void InspectComponent( flecs::id type, flecs::entity entity );
  void InspectComponent( flecs::id type, void* ptr );

  void SerializeScope( flecs::meta::op_t* ops, void* ptr );
  void SerializeStruct( flecs::meta::op_t* ops, void* ptr );
  void SerializeArray( flecs::meta::op_t* ops, int32_t elem_count, void* ptr );
  void SerializeVector( flecs::meta::op_t* ops, void* ptr );
  void SerializeEnum( flecs::meta::op_t* op, void* ptr );
  void SerializeOps( flecs::meta::op_t* ops, int32_t op_count, void* base );
};

} // namespace Ember
