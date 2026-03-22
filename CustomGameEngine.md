# Project Goals:
* Create an easy to use custom game engine (primarily 3d, maybe 2d later) + editor built with C++ which is performant, easy to use, and targets the following platforms: Windows, Mac, Android and iOS.
* I want to create a 3d game using this editor which can be published on all platforms

## Constraints:
* Write ECS system for data structure management, which is performant and targets each architecture/platform for best efficiency.
* Built with modern C++, target c++ version which can run on all the platforms
* Multi-threaded with flexibility (if we want only specific # of threads on certain platforms)
* 3D Rendering APIs to use: Metal for Mac/iOS, Vulkan for the rest. Suggest libraries which can target both backends
* Rendering: Implement modern techniques like ray tracing, anti-aliasing solutions, step by step, discussing as we go along (what to defer to long-term support)
* Custom Scene Graph management (let's discuss)
* Performance: upto 60fps on mobile platforms, upto 90-120 fps on desktop
* Tooling: In-game visualizer which will show useful info like FPS, CPU/GPU usage, Texture memory, etc. Can add options to toggle various features
* Formats: Start with minimal set of 3d (gltf, fbx, etc), image, audio formats to support. We can tackle more later
* Use ImGui wherever possible for simple UI 

## Quality:
* Write tests as you go along to ensure quality
* Create small commits which are easy to review and revert
* Use well known linter to keep codebase consistent
* Use an agent to specifically review commits and find issues
* To discuss: Screenshot tests for progress?
* Keep a notes document which tracks all decisions and progress made. This will be updated in github

## Open Questions:
* Be very deliberate about using external libraries - discuss with me first. Binary size is a factor. Could we write up our own for our specific needs?
* Physics Engine: Explore what are the best cross platform options
* Audio: I don't have a background in audio architecture, walk me through it step by step to figure out the best solution for loading/playing audio files
* Scene Graph: Write our own custom scene graph management or use an external library?
* Input: Walk me through each platform on how to manage input, and how we can build an input framework which targets each platform
* Networking Support: Discuss cross platform solutions. No multiplayer yet, but downloading assets/updates, etc., how can it be done?
* Editor: I want a simple and easy to use editor, DO NOT copy engines like Unity or Unreal which are too complicated. Suggest if scripting languages make sense, and how to incorporate them. Walk me through it step by step on what features to implement, and how to publish these games
* 2D support: Should we add it early on with the 3d focus, or do it later? 
* Anything else I have missed, please discuss with me

## Long Term goals to discuss later:
* Multiplayer support
* Really advanced Rendering features such as global illumination, gaussian splats? 
* Extensive support for all kinds of formats