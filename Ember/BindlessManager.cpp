#include "BindlessManager.hpp"

#include "Util/HelperUtils.hpp"

bool Ember::BindlessHandle::IsNull() const noexcept
{
  return m_Handle == kInvalid;
}

Ember::BindlessHandle::BindlessHandle( uint32_t const index ) : m_Handle{ index }
{}

Ember::BindlessManager::RabbitPullingFreeList::RabbitPullingFreeList( uint32_t const max_allowed )
  : m_MaxAllowed{ max_allowed }
{
  // Sentinel
  ASSERT( max_allowed != UINT32_MAX );
}

uint32_t Ember::BindlessManager::RabbitPullingFreeList::Allocate()
{
  if ( not m_Recycled.empty() )
  {
    uint32_t const index = m_Recycled.front();
    m_Recycled.pop();
    return index;
  }

  ASSERT( m_MaxReached < m_MaxAllowed );

  uint32_t const index = m_MaxReached++;

  return index;
}

void Ember::BindlessManager::RabbitPullingFreeList::Free( uint32_t const index )
{
  m_Recycled.push( index );
}

Ember::BindlessManager::BindlessManager(
    ComPtr<ID3D12Device2>        device,
    ComPtr<ID3D12DescriptorHeap> resource_descriptor_heap,
    uint32_t const               resource_descriptor_increment,
    uint32_t const               max_resources,
    ComPtr<ID3D12DescriptorHeap> sampler_descriptor_heap,
    uint32_t const               sampler_descriptor_increment,
    uint32_t const               max_samplers )
  : m_Device{ std::move( device ) }
  , m_ResourceFreeList{ max_resources }
  , m_ResourceDescriptorHeap{ std::move( resource_descriptor_heap ) }
  , m_ResourceDescriptorIncrement{ resource_descriptor_increment }
  , m_SamplerFreeList{ max_samplers }
  , m_SamplerDescriptorHeap{ std::move( sampler_descriptor_heap ) }
  , m_SamplerDescriptorIncrement{ sampler_descriptor_increment }
{}

void Ember::BindlessManager::Create(
    BindlessManager* bindless, ComPtr<ID3D12Device2> device, uint32_t const max_resources, uint32_t const max_samplers )
{
  ASSERT_M( max_resources <= 1'000'000, "DirectX 12 Tier 3 provides ~1 million shader visible descriptors" );
  ASSERT_M( max_samplers <= 2048, "DirectX 12 Tier 3 provides 2048 max shader visible descriptors" );

  ComPtr<ID3D12DescriptorHeap> resource_descriptor_heap;
  ComPtr<ID3D12DescriptorHeap> sampler_descriptor_heap;
  {
    D3D12_DESCRIPTOR_HEAP_DESC const desc = {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = max_resources,
      .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };

    ERR_ABORT( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &resource_descriptor_heap ) ) );
  }
  {
    D3D12_DESCRIPTOR_HEAP_DESC const desc = {
      .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
      .NumDescriptors = max_samplers,
      .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };
    ERR_ABORT( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &sampler_descriptor_heap ) ) );
  }

  uint32_t const resource_descriptor_increment =
      device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
  uint32_t const sampler_descriptor_increment =
      device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );

  new ( bindless ) BindlessManager{
    std::move( device ), std::move( resource_descriptor_heap ), resource_descriptor_increment,
    max_resources,       std::move( sampler_descriptor_heap ),  sampler_descriptor_increment,
    max_samplers,
  };
}

Ember::CBVHandle Ember::BindlessManager::CreateDescriptorHandle( D3D12_CONSTANT_BUFFER_VIEW_DESC const& cbv_desc )
{
  std::lock_guard                     lock_guard{ m_ResourceDescriptorLock };

  uint32_t const                      index = m_ResourceFreeList.Allocate();

  CD3DX12_CPU_DESCRIPTOR_HANDLE const cbv_descriptor_handle{
    m_ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    ( int )index,
    m_ResourceDescriptorIncrement,
  };

  m_Device->CreateConstantBufferView( &cbv_desc, cbv_descriptor_handle );

  return CBVHandle{ index };
}

Ember::SRVHandle Ember::BindlessManager::CreateDescriptorHandle(
    ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc )
{
  std::lock_guard                     lock_guard{ m_ResourceDescriptorLock };

  uint32_t const                      index = m_ResourceFreeList.Allocate();

  CD3DX12_CPU_DESCRIPTOR_HANDLE const srv_descriptor_handle{
    m_ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    ( int )index,
    m_ResourceDescriptorIncrement,
  };

  m_Device->CreateShaderResourceView( resource, &srv_desc, srv_descriptor_handle );

  return SRVHandle{ index };
}

Ember::UAVHandle Ember::BindlessManager::CreateDescriptorHandle(
    ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc )
{
  return CreateDescriptorHandle( resource, nullptr, uav_desc );
}

Ember::UAVHandle Ember::BindlessManager::CreateDescriptorHandle(
    ID3D12Resource* resource, ID3D12Resource* counter, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc )
{
  std::lock_guard                     lock_guard{ m_ResourceDescriptorLock };

  uint32_t const                      index = m_ResourceFreeList.Allocate();

  CD3DX12_CPU_DESCRIPTOR_HANDLE const srv_descriptor_handle{
    m_ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    ( int )index,
    m_ResourceDescriptorIncrement,
  };

  m_Device->CreateUnorderedAccessView( resource, counter, &uav_desc, srv_descriptor_handle );

  return UAVHandle{ index };
}

Ember::SamplerHandle Ember::BindlessManager::CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc )
{
  std::lock_guard                     lock_guard{ m_SamplerDescriptorLock };

  uint32_t const                      index = m_SamplerFreeList.Allocate();

  CD3DX12_CPU_DESCRIPTOR_HANDLE const sampler_descriptor_handle{
    m_SamplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    ( int )index,
    m_SamplerDescriptorIncrement,
  };

  m_Device->CreateSampler( &sampler_desc, sampler_descriptor_handle );

  return SamplerHandle{ index };
}

void Ember::BindlessManager::Free( SRVHandle const handle )
{
  if ( handle.IsNull() ) return;
  m_ResourceFreeList.Free( handle.GetInner() );
}

void Ember::BindlessManager::Free( UAVHandle const handle )
{
  if ( handle.IsNull() ) return;
  m_ResourceFreeList.Free( handle.GetInner() );
}

void Ember::BindlessManager::Free( CBVHandle const handle )
{
  if ( handle.IsNull() ) return;
  m_ResourceFreeList.Free( handle.GetInner() );
}

void Ember::BindlessManager::Free( SamplerHandle const handle )
{
  if ( handle.IsNull() ) return;
  m_SamplerFreeList.Free( handle.GetInner() );
}

std::array<ID3D12DescriptorHeap*, 2> Ember::BindlessManager::GetBindlessDescriptorHeaps() const
{
  return { m_ResourceDescriptorHeap.Get(), m_SamplerDescriptorHeap.Get() };
}
