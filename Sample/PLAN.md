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
- [ ] Setup D3D12 Renderer
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
		- [ ] With User Control
	- [ ] Scene Rendering
		- [ ] Load glTF2.0 Mesh
		- [ ] Render Mesh
		- [ ] Scene Hierarchy
- [ ] Rendering Features
	- [ ] Lighting
		- [ ] Blinn-Phong
		- [ ] PBR
			- [ ] Punctual
			- [ ] Static IBL
				- [ ] Diffuse Cubemap Convolution.
				- [ ] Prefilter
				- [ ] Diffuse 3rd order SH.
			- [ ] Reflection Probe
				- [ ] Diffuse 3rd order SH.
		- [ ] Shadows
			- [ ] PCSS
			- [ ] Cascaded Shadow Maps
			- [ ] Omni-Shadow maps
			- [ ] Dual-paraboloid
- [ ] Miscellaneous
	- [X] Perf Query
	- [ ] Profiling/Instrumentation