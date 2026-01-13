# Additional Information

config flag STRIP_META_INFO is used to remove all the device profiling information from the build.
Possibly good for deployment.

# Plan for Ember development

- [X] Open a window
- [X] Setup D3D12 Device Interface
	- [X] Device
	- [X] Queues
	- [X] Swapchain
	- [X] Presentation
	- [X] Synchronization Primitives
- [X] Setup D3D12 Renderer
	- [X] Pipeline Creation
	- [X] Render Triangle
	- [X] Render Box
		- [X] Switch to Vertex/Index buffer
		- [X] Bindless Vertex Pulling
	- [X] Render Texture
		- [X] Load Texture
		- [X] Render on Screen
	- [X] Camera
		- [X] Fixed
		- [X] With User Control
	- [X] Scene Rendering
		- [X] Load glTF2.0 Mesh
		- [X] Render Mesh
		- [X] Scene Hierarchy
- [ ] Rendering Features
	- [X] Lighting
		- [X] Blinn-Phong
		- [X] PBR
			- [X] Punctual
			- [X] Static IBL
				- [X] Diffuse Cubemap Convolution.
				- [X] Prefilter
				- [X] Diffuse 3rd order SH.
			- [ ] Reflection Probe
			- [ ] Light Probe (Diffuse 3rd order SH.)
		- [ ] Shadows
			- [ ] PCSS
			- [X] Cascaded Shadow Maps
			- [X] Omni-Shadow maps
			- [ ] Dual-paraboloid
		- [ ] Global Illumination
			- [ ] PBRT
		- [ ] Ambient Occlusion
			- [ ] SSAO
			- [ ] GTAO
	- [ ] Raytracing
		- [X] Shadows
		- [ ] Lighting (ReSTIR-DI)
		- [X] Reflections
		- [ ] AO
		- [ ] GI
			- [ ] DDGI
			- [ ] ReSTIR-GI
- [X] Miscellaneous
	- [X] Perf Query
	- [X] Profiling/Instrumentation