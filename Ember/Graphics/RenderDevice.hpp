#pragma once

#include "BindlessManager.hpp"
#include "Buffer.hpp"
#include "CommandList.hpp"
#include "Context.hpp"
#include "DeviceHandle.hpp"
#include "PipelineFactory.hpp"
#include "Texture.hpp"
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
  ComPtr<ID3D12Device5>      m_Device;
  ComPtr<D3D12MA::Allocator> m_Allocator;

  // Swapchain and internal images.
  uint32_t                  m_SwapchainWidth{ 640 };
  uint32_t                  m_SwapchainHeight{ 480 };
  DXGI_FORMAT               m_SwapchainFormat{ DXGI_FORMAT_UNKNOWN };
  ComPtr<IDXGISwapChain4>   m_Swapchain;

  constexpr static uint32_t kUseVSyncBit       = 1 << 0;
  constexpr static uint32_t kSupportTearingBit = 1 << 1;

  uint32_t                  m_VsyncAndTearing{ 0 };

  // Descriptor Heaps
  std::unique_ptr<BindlessManager> m_Bindless;
  BufferManager                    m_BufferManager;
  TextureManager                   m_TextureManager;
  PipelineFactory                  m_PipelineFactory;

  // Swapchain images.
  std::vector<Texture> m_Backbuffers; // Must release before texture manager.
  uint32_t             m_CurrentBackbufferIndex{ 0 };

  // Commands and Sync
  Context                           m_DirectContext; // Context depends on device and bindless.
  std::vector<Context::Receipt>     m_FrameReceipts;


  static D3D_ROOT_SIGNATURE_VERSION FetchHighestRootSignatureVersionImpl( ID3D12Device* d3d_device );

public:
  RenderDevice() = default;
  RenderDevice(
      ComPtr<ID3D12Device5>            device,
      ComPtr<D3D12MA::Allocator>       allocator,
      uint32_t                         swapchain_width,
      uint32_t                         swapchain_height,
      DXGI_FORMAT                      swapchain_format,
      ComPtr<IDXGISwapChain4>          swapchain,
      std::unique_ptr<BindlessManager> bindless_manager,
      Context                          direct_context,
      bool                             is_tearing_supported );

  [[nodiscard]] ID3D12Device5*             GetDevice() const noexcept;
  [[nodiscard]] D3D12MA::Allocator*        GetAllocator() const noexcept;
  [[nodiscard]] ID3D12CommandQueue*        GetDirectQueue() const noexcept;

  [[nodiscard]] DXGI_FORMAT                GetSwapchainFormat() const;
  [[nodiscard]] D3D_ROOT_SIGNATURE_VERSION FetchHighestRootSignatureVersion() const;

  static void                              Create( RenderDevice* render_device, HWND window_handle, bool use_warp );

  void                                     ResizeSwapchain( uint32_t width, uint32_t height );

  // Pipeline Management
  [[nodiscard]] ComPtr<ID3D12RootSignature> CreateRootSignature( RootSignatureDesc const& desc ) const;
  [[nodiscard]] ComPtr<ID3D12PipelineState> CreateGraphicsPipeline( GraphicsPipelineDesc const& desc ) const;

  // Buffer Management
  [[nodiscard]] Buffer  CreateVertexBuffer( uint32_t size, uint32_t stride );
  [[nodiscard]] Buffer  CreateIndexBuffer( uint32_t size, DXGI_FORMAT format );
  [[nodiscard]] Buffer  CreateStorageBuffer( uint32_t size, uint32_t stride );
  [[nodiscard]] Buffer  CreateRawStorageBuffer( uint64_t size );
  [[nodiscard]] Buffer  CreateConstantBuffer( uint32_t size );
  [[nodiscard]] Buffer  CreateASBuffer( uint64_t size );

  [[nodiscard]] Texture CreateTexture2D( Tex2DDesc const& create_info );
  [[nodiscard]] Texture CreateTextureCube( TexCubeDesc const& create_info );
  [[nodiscard]] Texture CreateTexture( Texture::Desc const& desc );

  [[nodiscard]] Sampler CreateSampler( D3D12_SAMPLER_DESC const& sampler_desc );

  // Descriptor Management
  [[nodiscard]] SRVHandle CreateBindlessHandle(
      ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& srv_desc ) const noexcept;
  [[nodiscard]] UAVHandle CreateBindlessHandle(
      ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& uav_desc ) const noexcept;
  [[nodiscard]] SamplerHandle       CreateSamplerHandle( D3D12_SAMPLER_DESC const& sampler_desc ) const noexcept;
  [[nodiscard]] RawDescriptorHandle AllocateRawDescriptorHandle(
      D3D12_CPU_DESCRIPTOR_HANDLE* cpu_desc, D3D12_GPU_DESCRIPTOR_HANDLE* gpu_desc ) const noexcept;

  [[nodiscard]] std::array<ID3D12DescriptorHeap*, 2> GetBindlessDescriptorHeaps() const;
  void                                               FreeHandle( CBVHandle handle ) const;
  void                                               FreeHandle( SRVHandle handle ) const;
  void                                               FreeHandle( UAVHandle handle ) const;
  void                                               FreeHandle( RawDescriptorHandle handle ) const;
  void                                               FreeHandle( SamplerHandle handle ) const;

  // Wait until the all queues have finished all commands.
  Context CreateContext( D3D12_COMMAND_LIST_TYPE type );
  void    WaitOn( Context::Receipt receipt ) const;
  void    QueueWaitOn( Context::Receipt receipt ) const;
  void    WaitIdle();

  // Per Frame getters.
  [[nodiscard]] Texture     GetCurrentBackbuffer() const noexcept;
  [[nodiscard]] CommandList GetGraphicsCommandList() noexcept;
  [[nodiscard]] uint32_t    GetCurrentFrameIndex() const noexcept;
  void                      ExecuteCommandList( CommandList&& command_list );
  void                      Present();

  [[nodiscard]] bool        IsVsyncEnabled() const;
  [[nodiscard]] bool        IsTearingSupported() const;

  RenderDevice( RenderDevice const& other )                = delete;
  RenderDevice( RenderDevice&& other ) noexcept            = delete;
  RenderDevice& operator=( RenderDevice const& other )     = delete;
  RenderDevice& operator=( RenderDevice&& other ) noexcept = delete;
};

} // namespace Ember
