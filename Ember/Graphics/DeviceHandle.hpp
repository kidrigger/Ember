#pragma once

#include "Util/Runtime.hpp"

namespace Ember
{

class IndexHandle
{
public:
  IndexHandle() = default;
  [[nodiscard]] bool IsNull() const noexcept;

protected:
  explicit IndexHandle( uint32_t index );

  [[nodiscard]] uint32_t GetInner() const
  {
    return m_Handle;
  }

private:
  constexpr static uint32_t kInvalid{ UINT32_MAX };
  uint32_t                  m_Handle{ kInvalid };
};

// Guarantee size matches the 32bit (DWORD) constants.
static_assert( sizeof( IndexHandle ) == sizeof( DWORD32 ) );

class DeviceHandle : public IndexHandle
{
public:
  DeviceHandle() = default;
  explicit DeviceHandle( uint32_t const index ) : IndexHandle{ index }
  {}
};

enum class DeviceHandleType : uint8_t
{
  kRaw,
  kCBV,
  kSRV,
  kUAV,
  kSampler,
};

#define TYPED_DEVICE_HANDLE( Type, Owner )                                                                             \
  class Type##Handle : public DeviceHandle                                                                             \
  {                                                                                                                    \
  public:                                                                                                              \
    Type##Handle() = default;                                                                                          \
                                                                                                                       \
    explicit operator UINT() const                                                                                     \
    {                                                                                                                  \
      return GetInner();                                                                                               \
    }                                                                                                                  \
                                                                                                                       \
    explicit operator bool() const                                                                                     \
    {                                                                                                                  \
      return not IsNull();                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
  protected:                                                                                                           \
    friend class Owner;                                                                                                \
                                                                                                                       \
    explicit Type##Handle( uint32_t const index ) : DeviceHandle{ index }                                              \
    {}                                                                                                                 \
  };                                                                                                                   \
  static_assert( sizeof( Type##Handle ) == sizeof( DWORD32 ) )

TYPED_DEVICE_HANDLE( SRV, BindlessManager );
TYPED_DEVICE_HANDLE( UAV, BindlessManager );
TYPED_DEVICE_HANDLE( CBV, BindlessManager );
TYPED_DEVICE_HANDLE( Sampler, BindlessManager );
TYPED_DEVICE_HANDLE( RawDescriptor, BindlessManager );

#undef TYPED_DEVICE_HANDLE

#define TYPED_HANDLE( Type, Owner )                                                                                    \
  class Type##Handle : public IndexHandle                                                                              \
  {                                                                                                                    \
  public:                                                                                                              \
    Type##Handle() = default;                                                                                          \
                                                                                                                       \
    explicit operator UINT() const                                                                                     \
    {                                                                                                                  \
      return GetInner();                                                                                               \
    }                                                                                                                  \
                                                                                                                       \
    explicit operator bool() const                                                                                     \
    {                                                                                                                  \
      return not IsNull();                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
  protected:                                                                                                           \
    friend class Owner;                                                                                                \
                                                                                                                       \
    explicit Type##Handle( uint32_t const index ) : IndexHandle{ index }                                               \
    {}                                                                                                                 \
  };                                                                                                                   \
  static_assert( sizeof( Type##Handle ) == sizeof( DWORD32 ) )

} // namespace Ember
