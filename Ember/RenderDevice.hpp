#pragma once

#include <mutex>

#include "BindlessHandle.hpp"
#include "BindlessManager.hpp"
#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "Texture.hpp"
#include "TextureLoader.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{

class RenderDevice
{
public:
  constexpr static size_t kNumFrames = 3;

private:
  // Device and queues.
  ComPtr<ID3D12Device2>      m_Device;
  ComPtr<D3D12MA::Allocator> m_Allocator;

  // Swapchain and internal images.
  uint32_t                            m_SwapchainWidth{ 640 };
  uint32_t                            m_SwapchainHeight{ 480 };
  ComPtr<IDXGISwapChain4>             m_Swapchain;
  std::vector<ComPtr<ID3D12Resource>> m_Backbuffers;

  constexpr static uint32_t           kUseVSyncBit       = 1 << 0;
  constexpr static uint32_t           kSupportTearingBit = 1 << 1;

  uint32_t                            m_VsyncAndTearing{ 0 };

  // Views to swapchain images.
  ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
  uint32_t                     m_RTVDescriptorSize{ 0 };
  uint32_t                     m_CurrentBackbufferIndex{ 0 };

  // Descriptor Heaps
  ComPtr<ID3D12DescriptorHeap>     m_DSVDescriptorHeap;
  std::unique_ptr<BindlessManager> m_Bindless;
  BufferManager                    m_BufferManager;
  TextureManager                   m_TextureManager;

  // Commands and Sync
  Context                       m_DirectContext;
  std::vector<Context::Receipt> m_FrameReceipts;

public:
  RenderDevice() = default;
  RenderDevice(
      ComPtr<ID3D12Device2>               device,
      ComPtr<D3D12MA::Allocator>          allocator,
      uint32_t                            swapchain_width,
      uint32_t                            swapchain_height,
      ComPtr<IDXGISwapChain4>             swapchain,
      std::vector<ComPtr<ID3D12Resource>> backbuffers,
      ComPtr<ID3D12DescriptorHeap>        rtv_descriptor_heap,
      uint32_t                            rtv_descriptor_size,
      ComPtr<ID3D12DescriptorHeap>        dsv_descriptor_heap,
      std::unique_ptr<BindlessManager>    bindless_manager,
      Context                             direct_context,
      bool                                is_tearing_supported );

  ComPtr<ID3D12Device2>      GetDevice() noexcept;
  ComPtr<D3D12MA::Allocator> GetAllocator() noexcept;

  static void                Create( RenderDevice* render_device, HWND window_handle, bool use_warp );

  void                       ResizeSwapchain( uint32_t width, uint32_t height );

  // Buffer Management
  [[nodiscard]] Buffer      CreateVertexBuffer( uint32_t size, uint32_t stride );
  [[nodiscard]] Buffer      CreateIndexBuffer( uint32_t size, DXGI_FORMAT format );
  [[nodiscard]] Buffer      CreateStorageBuffer( uint32_t size, uint32_t stride );
  [[nodiscard]] Buffer      CreateConstantBuffer( uint32_t size );

  [[nodiscard]] Texture     CreateTexture2D( DXGI_FORMAT format, uint32_t width, uint32_t height, TextureUsage usage );
  [[nodiscard]] Texture     CreateTextureCube( DXGI_FORMAT format, uint32_t side, TextureUsage usage );

  [[nodiscard]] Sampler     CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );

  [[nodiscard]] DepthBuffer CreateDepthBuffer( uint32_t width, uint32_t height ) const;

  void                      SetDepthBuffer( DepthBuffer const& depth_buffer ) const;

  // Descriptor Management
  [[nodiscard]] SRVHandle CreateBindlessHandle(
      ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc ) const noexcept;
  [[nodiscard]] UAVHandle CreateBindlessHandle(
      ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc ) const noexcept;
  [[nodiscard]] SamplerHandle CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc ) const noexcept;
  [[nodiscard]] std::array<ID3D12DescriptorHeap*, 2> GetBindlessDescriptorHeaps() const;
  void                                               FreeHandle( SRVHandle handle ) const;
  void                                               FreeHandle( UAVHandle handle ) const;
  void                                               FreeHandle( SamplerHandle handle ) const;

  // Wait until the all queues have finished all commands.
  void WaitOn( Context::Receipt receipt ) const;
  void QueueWaitOn( Context::Receipt receipt ) const;
  void WaitIdle();

  // Per Frame getters.
  [[nodiscard]] ID3D12Resource*                   GetCurrentBackbuffer() const noexcept;
  [[nodiscard]] ComPtr<ID3D12GraphicsCommandList> GetGraphicsCommandList() noexcept;
  [[nodiscard]] uint32_t                          GetCurrentFrameIndex() const noexcept;
  [[nodiscard]] CD3DX12_CPU_DESCRIPTOR_HANDLE     GetCurrentRTVCpuDescriptorHandle() const noexcept;
  [[nodiscard]] CD3DX12_CPU_DESCRIPTOR_HANDLE     GetCurrentDSVCpuDescriptorHandle() const noexcept;
  void                                            ExecuteCommandList( Context::CommandList&& command_list );
  void                                            Present();

  [[nodiscard]] bool                              IsVsyncEnabled() const;
  [[nodiscard]] bool                              IsTearingSupported() const;

  RenderDevice( RenderDevice const& other )                = delete;
  RenderDevice( RenderDevice&& other ) noexcept            = delete;
  RenderDevice& operator=( RenderDevice const& other )     = delete;
  RenderDevice& operator=( RenderDevice&& other ) noexcept = delete;
  ~RenderDevice();
};

} // namespace Ember
