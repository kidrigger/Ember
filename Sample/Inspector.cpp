#include "Inspector.hpp"

#include <imgui.h>

#include <Util/HelperUtils.hpp>
#include <Util/StringUtil.hpp>

void Ember::InspectorView::Draw( char const* label, void* data ) const
{
  ASSERT( m_DrawFunc );
  if ( m_DrawFunc ) m_DrawFunc( label, data );
}

void Ember::Inspector::Draw( flecs::world* ecs, flecs::entity const entity )
{
  ImGui::Begin( "Inspector" );

  if ( not entity.is_valid() or not entity.is_alive() )
  {
    ImGui::Text( "<No Valid Entity Selected>" );
    ImGui::End();
    return;
  }

  Inspector inspector{ ecs };
  entity.each( [&]( flecs::id const id ) { inspector.InspectComponent( id, entity ); } );

  if ( ImGui::CollapsingHeader( "Pairs" ) )
  {
    if ( not inspector.m_Pairs.empty() )
    {
      ImGui::BeginTable( "Pairs", 3 );
      for ( auto& pair : inspector.m_Pairs )
      {
        ImGui::PushID( pair.str().c_str() );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text( "%s", pair.first().str().c_str() );
        ImGui::TableNextColumn();
        ImGui::Text( "%s", pair.second().str().c_str() );
        ImGui::TableNextColumn();
        if ( ImGui::Button( "Del" ) )
        {
          _ = entity.remove( pair );
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    else
    {
      ImGui::Text( "<No Pairs>" );
    }
  }

  if ( ImGui::CollapsingHeader( "Tags" ) )
  {
    if ( not inspector.m_Tags.empty() )
    {
      ImGui::BeginTable( "Tags", 2 );
      for ( auto& tag : inspector.m_Tags )
      {
        ImGui::PushID( tag.str().c_str() );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text( "%s", tag.str().c_str() );
        ImGui::TableNextColumn();
        if ( ImGui::Button( "Del" ) )
        {
          _ = entity.remove( tag );
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    else
    {
      ImGui::Text( "<No Tags>" );
    }
  }

  char buf[128];
  ZeroMemory( DataOf( buf ), CountOf( buf ) );
  if ( ImGui::InputText( "Add", DataOf( buf ), CountOf( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
  {
    flecs::entity const component = ecs->lookup( buf, ".", "." );
    if ( component.is_valid() )
    {
      _ = entity.ensure( component );
    }
  }

  ImGui::End();
}

void Ember::Inspector::InspectComponent( flecs::id const type, flecs::entity const entity )
{
  if ( type.is_pair() )
  {
    m_Pairs.push_back( type );
    return;
  }

  flecs::type_info_t const* type_info = m_ECS->type_info( type );

  if ( not type_info or type_info->size == 0 )
  {
    m_Tags.push_back( type );
    return;
  }

  // The TypeSerializer component contains a vector of instructions that
  // tells us how to serialize the type.
  flecs::TypeSerializer const* ts = type.entity().try_get<flecs::TypeSerializer>();

  if ( not ts )
  {
    if ( ImGui::CollapsingHeader( type.str().c_str() ) )
    {
      ImGui::PushID( type.str().c_str() );

      ImGui::Text( "<Opaque>" );

      if ( ImGui::Button( "Delete" ) )
      {
        _ = entity.remove( type );
      }

      ImGui::PopID();
    }
    return;
  }

  if ( ImGui::CollapsingHeader( type.str().c_str() ) )
  {
    ImGui::PushID( type.str().c_str() );
    void* ptr = entity.get_mut( type );
    SerializeOps( ecs_vec_first_t( &ts->ops, flecs::meta::op_t ), ecs_vec_count( &ts->ops ), ptr );

    if ( ImGui::Button( "Delete" ) )
    {
      _ = entity.remove( type );
    }

    ImGui::PopID();
  }
}

void Ember::Inspector::InspectComponent( flecs::id const type, void* ptr )
{
  if ( type.is_pair() )
  {
    m_Pairs.push_back( type );
    return;
  }

  flecs::type_info_t const* type_info = m_ECS->type_info( type );

  if ( not type_info or type_info->size == 0 )
  {
    m_Tags.push_back( type );
    return;
  }

  // The TypeSerializer component contains a vector of instructions that
  // tells us how to serialize the type.
  flecs::TypeSerializer const* ts = type.entity().try_get<flecs::TypeSerializer>();

  if ( not ts )
  {
    if ( ImGui::CollapsingHeader( type.str().c_str() ) )
    {
      ImGui::Text( "<Opaque>" );
    }
    return;
  }

  if ( ImGui::CollapsingHeader( type.str().c_str() ) )
  {
    SerializeOps( ecs_vec_first_t( &ts->ops, flecs::meta::op_t ), ecs_vec_count( &ts->ops ), ptr );
  }
}

Ember::Inspector::Inspector( flecs::world* w ) : m_ECS( w )
{}

void Ember::Inspector::SerializeScope( flecs::meta::op_t* ops, void* ptr )
{
  // A scope starts with a Push and ends with a Pop, so trim the first and
  // last instruction before forwarding.
  SerializeOps( ops + 1, ops->op_count - 2, ptr );
}

void Ember::Inspector::SerializeStruct( flecs::meta::op_t* ops, void* ptr )
{
  if ( auto* type_info = m_ECS->type_info( ops->type ) )
  {
    if ( InspectorView const* view = flecs::entity{ m_ECS->c_ptr(), type_info->component }.try_get<InspectorView>() )
    {
      ImGui::PushID( type_info->name );
      view->Draw( ops->name, ECS_OFFSET( ptr, ops[1].offset ) );
      ImGui::PopID();
      return;
    }
  }

  if ( ops->name )
  {
    char buf[128];
    if ( ImGui::TreeNode( FormatTo( buf, "{}", ops->name ) ) )
    {
      SerializeScope( ops, ptr );

      ImGui::TreePop();
    }
  }
  else
  {
    SerializeScope( ops, ptr );
  }
}

void Ember::Inspector::SerializeArray( flecs::meta::op_t* ops, int32_t elem_count, void* ptr )
{
  if ( ImGui::TreeNode( ops->name ) )
  {
    // Iterate elements of the array. Skip the first and last instruction
    // since they are PushStruct and Pop.
    for ( int i = 0; i < elem_count; i++ )
    {
      SerializeScope( ops, ptr );

      ptr = ECS_OFFSET( ptr, ops->elem_size );
    }

    ImGui::TreePop();
  }
}

void Ember::Inspector::SerializeVector( flecs::meta::op_t* ops, void* ptr )
{
  auto vec = ( ecs_vec_t const* )( ptr );
  SerializeArray( ops, vec->count, vec->array );
}

void Ember::Inspector::SerializeEnum( flecs::meta::op_t* op, void* ptr )
{
  ecs_meta_op_kind_t kind = op->underlying_kind;
  ecs_map_key_t      value;

  if ( kind == EcsOpU8 || kind == EcsOpI8 )
  {
    value = *( uint8_t const* )ptr;
  }
  else if ( kind == EcsOpU16 || kind == EcsOpI16 )
  {
    value = *( uint16_t const* )ptr;
  }
  else if ( kind == EcsOpU32 || kind == EcsOpI32 )
  {
    value = *( uint32_t const* )ptr;
  }
  else if ( kind == EcsOpUPtr || kind == EcsOpIPtr )
  {
    value = *( uintptr_t const* )ptr;
  }
  else if ( kind == EcsOpU64 || kind == EcsOpI64 )
  {
    value = *( uint64_t const* )ptr;
  }
  else
  {
    printf( "<<invalid underlying enum type>>" );
    return;
  }

  ecs_enum_constant_t* c = ecs_map_get_deref( op->is.constants, ecs_enum_constant_t, value );
  printf( "%s", c->name );
}

void Ember::Inspector::SerializeOps( flecs::meta::op_t* ops, int32_t op_count, void* base )
{
  char label[128];
  for ( int i = 0; i < op_count; i++ )
  {
    flecs::meta::op_t* op = &ops[i];

    // Get pointer for current field
    void* ptr = ECS_OFFSET( base, op->offset );

    if ( op->name )
    {
      FormatTo( label, "{}", op->name );
    }
    else
    {
      FormatTo( label, "##{:p}", ( void* )op );
    }

    switch ( op->kind )
    {
      // Instructions that forward to a type scope, like a (nested) struct
      // or collection
      case EcsOpPushStruct:
        SerializeStruct( op, ptr );
        break;
      case EcsOpPushArray:
        SerializeArray( op, ecs_meta_op_get_elem_count( ops, ptr ), ptr );
        break;
      case EcsOpPushVector:
        SerializeVector( op, ptr );
        break;

      // Opaque types have in-memory representations that are opaque to
      // the reflection framework and cannot be serialized by just taking
      // a pointer + an offset. See src/addons/script/serialize.c for an
      // example of how to handle opaque types.
      case EcsOpOpaqueStruct:
      case EcsOpOpaqueArray:
      case EcsOpOpaqueVector:
      case EcsOpOpaqueValue:
        break;

      // Forward to type. Used for members of array/vector types.
      case EcsOpForward:
        Inspector{ m_ECS }.InspectComponent( flecs::entity( *m_ECS, op->type ), ptr );
        break;

      // Serialize single values
      case EcsOpEnum:
        SerializeEnum( op, ptr );
        break;
      case EcsOpBitmask:
        // Bitmask serialization requires iterating all the bits in a
        // value and looking up the corresponding constant. For
        // an example, see src/addons/script/serialize.c.
        break;
      case EcsOpBool:
        printf( "%s", ( *( bool const* )ptr ) ? "true" : "false" );
        break;
      case EcsOpChar:
        printf( "'%c'", *( char const* )ptr );
        break;
      case EcsOpByte:
      case EcsOpU8:
        ImGui::Text( "%hhu", *( uint8_t const* )ptr );
        break;
      case EcsOpU16:
        ImGui::Text( "%hu", *( uint16_t const* )ptr );
        break;
      case EcsOpU32:
        ImGui::Text( "%u", *( uint32_t const* )ptr );
        break;
      case EcsOpU64:
        ImGui::Text( "%llu", *( uint64_t const* )( ptr ) );
        break;
      case EcsOpI8:
        ImGui::Text( "%hhi", *( int8_t const* )( ptr ) );
        break;
      case EcsOpI16:
        ImGui::Text( "%hi", *( int16_t const* )( ptr ) );
        break;
      case EcsOpI32:
        ImGui::Text( "%i", *( int32_t const* )( ptr ) );
        break;
      case EcsOpI64:
        ImGui::Text( "%lli", *( int64_t const* )( ptr ) );
        break;
      case EcsOpF32:
        ImGui::InputFloat( label, ( float* )ptr );
        break;
      case EcsOpF64:
        ImGui::Text( "%lf", *( double* )ptr );
        break;
      case EcsOpUPtr:
        ImGui::Text( "%llu", *( uintptr_t const* )( ptr ) );
        break;
      case EcsOpIPtr:
        printf( "%lli", *( intptr_t const* )( ptr ) );
        break;
      case EcsOpString:
        printf( "\"%s\"", *( char* const* )ptr );
        break;
      case EcsOpEntity:
      {
        flecs::entity e( *m_ECS, *( flecs::entity_t* )ptr );
        ImGui::Text( "%s", e.path().c_str() );
        break;
      }
      case EcsOpId:
      {
        flecs::id component( *m_ECS, *( flecs::id_t* )ptr );
        ImGui::Text( "%s", component.str().c_str() );
        break;
      }
      case EcsOpPop:
      case EcsOpScope:
      case EcsOpPrimitive:
        // Not serializable
        break;
    }

    i += op->op_count - 1; // Skip over already processed instructions
  }
}
