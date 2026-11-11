# Vulkan headers (vendored)

`vulkan/vulkan_core.h`, `vulkan/vk_platform.h` and `vk_video/*.h` are copied unchanged from the
LunarG Vulkan SDK 1.4.363.1 (Khronos Vulkan-Headers, header version 363).

License: Apache-2.0 OR MIT, as stated in each file (Copyright The Khronos Group Inc.).

Only `hlrad-ng/gpu.cpp` includes them, with `VK_NO_PROTOTYPES`: the Vulkan loader is opened at run
time, so nothing links against Vulkan and the build needs no SDK.
