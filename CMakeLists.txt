cmake_minimum_required(VERSION 3.20)

# Prioritize local thirdparty/ directory on Windows, or thirdparty-linux on Linux
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
if(WIN32 AND EXISTS "${THIRDPARTY_DIR}")
    list(PREPEND CMAKE_PREFIX_PATH "${THIRDPARTY_DIR}")
    list(APPEND CMAKE_MODULE_PATH "${THIRDPARTY_DIR}/share/stb")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty-linux")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty-linux")
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty-linux/share/stb")
else()
    list(APPEND CMAKE_MODULE_PATH "${THIRDPARTY_DIR}/share/stb")
    if(EXISTS "${THIRDPARTY_DIR}/stb/stb_image.h")
        set(Stb_INCLUDE_DIR "${THIRDPARTY_DIR}/stb" CACHE PATH "Stb include directory")
    endif()
endif()

project(Tessera CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Wno-template-body" HAS_NO_TEMPLATE_BODY)
    if(HAS_NO_TEMPLATE_BODY)
        add_compile_options(-Wno-template-body)
    endif()
endif()

# On Linux, ensure we link against system Vulkan loader (with X11/Wayland WSI surface support)
if(UNIX AND NOT APPLE)
    find_library(SYSTEM_VULKAN_LIB NAMES vulkan libvulkan.so.1 libvulkan.so PATHS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib NO_DEFAULT_PATH)
    if(SYSTEM_VULKAN_LIB)
        message(STATUS "Prioritizing System Vulkan Loader: ${SYSTEM_VULKAN_LIB}")
        set(Vulkan_LIBRARY "${SYSTEM_VULKAN_LIB}" CACHE FILEPATH "Vulkan Library" FORCE)
        set(Vulkan_LIBRARIES "${SYSTEM_VULKAN_LIB}" CACHE FILEPATH "Vulkan Library" FORCE)
    endif()
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(unofficial-openfbx CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(Jolt CONFIG REQUIRED)

# Shader compilation
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Shared GLSL include files; every shader is rebuilt when one of these changes
set(SHADER_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common_ubo.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common_material.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/env_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/instancing_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lights_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/material_ext.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fluid_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sun_shadow_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fluid_smooth_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/wind_common.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle_common.glsl"
)

set(SPV_SHADERS "")
if(GLSLC_EXECUTABLE)
    # Compiles shaders/<source> to <spv_name>, and copies the result back into shaders/
    function(tessera_compile_shader source spv_name)
        set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}")
        set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${spv_name}")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND ${GLSLC_EXECUTABLE} "${src}" -o "${out}"
            COMMAND ${CMAKE_COMMAND} -E copy "${out}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${spv_name}"
            DEPENDS "${src}" ${SHADER_INCLUDES}
            COMMENT "Compiling shader ${source} to SPIR-V"
        )
        set(SPV_SHADERS ${SPV_SHADERS} "${out}" PARENT_SCOPE)
    endfunction()

    # Hardware ray tracing variant of the forward shader (ray query; only used when the GPU supports it)
    function(tessera_compile_shader_rt source spv_name)
        set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}")
        set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${spv_name}")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 -DTESSERA_RT=1 "${src}" -o "${out}"
            COMMAND ${CMAKE_COMMAND} -E copy "${out}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${spv_name}"
            DEPENDS "${src}" ${SHADER_INCLUDES} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/rt_common.glsl"
            COMMENT "Compiling ray tracing shader ${source} to SPIR-V"
        )
        set(SPV_SHADERS ${SPV_SHADERS} "${out}" PARENT_SCOPE)
    endfunction()

    tessera_compile_shader(shader.vert vert.spv)
    tessera_compile_shader(shader.frag frag.spv)
    tessera_compile_shader_rt(shader.frag frag_rt.spv)
    tessera_compile_shader(shadow.vert shadow_vert.spv)
    tessera_compile_shader(shadow.frag shadow_frag.spv)
    tessera_compile_shader(sky.vert sky_vert.spv)
    tessera_compile_shader(sky.frag sky_frag.spv)
    tessera_compile_shader(post.vert post_vert.spv)
    tessera_compile_shader(bloom_down.frag bloom_down_frag.spv)
    tessera_compile_shader(bloom_up.frag bloom_up_frag.spv)
    tessera_compile_shader(composite.frag composite_frag.spv)
    tessera_compile_shader(prepass.frag prepass_frag.spv)
    tessera_compile_shader(ssao.frag ssao_frag.spv)
    tessera_compile_shader(ssao_blur.frag ssao_blur_frag.spv)
    tessera_compile_shader(fxaa.frag fxaa_frag.spv)
    tessera_compile_shader(taa.frag taa_frag.spv)
    tessera_compile_shader(prepass.vert prepass_vert.spv)
    tessera_compile_shader(dof.frag dof_frag.spv)
    tessera_compile_shader(motionblur.frag motionblur_frag.spv)
    tessera_compile_shader(ssr.frag ssr_frag.spv)
    tessera_compile_shader(skinning.comp skinning_comp.spv)
    tessera_compile_shader(occlusion.frag occlusion_frag.spv)
    tessera_compile_shader(overlay.vert overlay_vert.spv)
    tessera_compile_shader(overlay.frag overlay_frag.spv)
    tessera_compile_shader(exposure.frag exposure_frag.spv)
    tessera_compile_shader(ocean.vert ocean_vert.spv)
    tessera_compile_shader(ocean.frag ocean_frag.spv)
    tessera_compile_shader(fluid_sprite.vert fluid_sprite_vert.spv)
    tessera_compile_shader(fluid_sprite.frag fluid_sprite_frag.spv)
    tessera_compile_shader(fluid_fullscreen.vert fluid_fullscreen_vert.spv)
    tessera_compile_shader(fluid_smooth.frag fluid_smooth_frag.spv)
    tessera_compile_shader(fluid_smooth_wide.frag fluid_smooth_wide_frag.spv)
    tessera_compile_shader(fluid_composite.frag fluid_composite_frag.spv)
    tessera_compile_shader(fluid_sim.comp fluid_sim_comp.spv)
    tessera_compile_shader(sky_view_lut.comp sky_view_lut_comp.spv)
    tessera_compile_shader(cloud_volume.frag cloud_volume_frag.spv)
    tessera_compile_shader(fluid_foam.vert fluid_foam_vert.spv)
    tessera_compile_shader(decal.vert decal_vert.spv)
    tessera_compile_shader(decal.frag decal_frag.spv)
    tessera_compile_shader(fluid_foam.frag fluid_foam_frag.spv)
    tessera_compile_shader(precip.vert precip_vert.spv)
    tessera_compile_shader(precip.frag precip_frag.spv)
    tessera_compile_shader(particle.vert particle_vert.spv)
    tessera_compile_shader(particle.frag particle_frag.spv)
    tessera_compile_shader(particle_sim.comp particle_sim_comp.spv)
    tessera_compile_shader(particle_mesh.vert particle_mesh_vert.spv)
    tessera_compile_shader(particle_mesh.frag particle_mesh_frag.spv)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/Core/Camera.cpp
    src/Core/InputManager.cpp
    src/Core/UndoRedoManager.cpp
    src/Scene/Mesh.cpp
    src/Scene/MeshSimplifier.cpp
    src/Scene/ObjLoader.cpp
    src/Scene/FbxLoader.cpp
    src/Scene/GltfLoader.cpp
    src/Scene/Node.cpp
    src/Scene/SceneNodes.cpp
    src/Scene/Nodes/3D/MeshNode.cpp
    src/Scene/Nodes/3D/CharacterNode.cpp
    src/Scene/Nodes/3D/CameraNode.cpp
    src/Scene/Nodes/3D/LightNode.cpp
    src/Scene/Nodes/3D/ColliderNode.cpp
    src/Scene/Nodes/3D/EmptyNode.cpp
    src/Scene/Nodes/3D/SplineNode.cpp
    src/Scene/Nodes/3D/DecalNode.cpp
    src/Scene/Nodes/3D/ParticleNode.cpp
    src/Scene/Nodes/3D/AudioNode.cpp
    src/Scene/Nodes/3D/TerrainNode.cpp
    src/Renderer/stb_image_impl.cpp
    src/Scene/Nodes/UI/UINode.cpp
    src/Scene/Nodes/UI/UICanvasNode.cpp
    src/Scene/Nodes/UI/UIPanelNode.cpp
    src/Scene/Nodes/UI/UITextNode.cpp
    src/Scene/Nodes/UI/UIButtonNode.cpp
    src/Scene/Nodes/UI/UIProgressBarNode.cpp
    src/Scene/Nodes/UI/UISpinnerNode.cpp
    src/Scene/Nodes/UI/UITextFieldNode.cpp
    src/Scene/Nodes/UI/UIPasswordFieldNode.cpp
    src/Scene/Nodes/UI/UISliderNode.cpp
    src/Scene/Nodes/UI/UICheckboxNode.cpp
    src/Scene/Nodes/UI/UIRadioButtonNode.cpp
    src/Scene/Nodes/UI/UIWindowNode.cpp
    src/Scene/Nodes/UI/UIImageNode.cpp
    src/Scene/Nodes/UI/UIVideoNode.cpp
    src/Scene/Nodes/UI/UIScrollPaneNode.cpp
    src/Scene/Nodes/UI/UIDropdownNode.cpp
    src/Scene/Nodes/UI/UIDatePickerNode.cpp
    src/Scene/Nodes/UI/UISelectableListNode.cpp
    src/Scene/Nodes/UI/UIColorPickerNode.cpp
    src/Scene/Nodes/UI/UITableNode.cpp
    src/Scene/Nodes/UI/UITabsNode.cpp
    src/Scene/Nodes/UI/UIMenuBarNode.cpp
    src/Scene/Nodes/UI/UIShapeNode.cpp
    src/Scene/Nodes/UI/UIBoxContainerNode.cpp
    src/Scene/Nodes/UI/UIGridContainerNode.cpp
    src/Resource/Resource.cpp
    src/Resource/ResourceManager.cpp
    src/Scene/Nodes/NodeFactory.cpp
    src/Scene/Scene.cpp
    src/Project/Project.cpp
    src/Localization/Localization.cpp
    src/Localization/GameLocalization.cpp
    src/Animation/Animation.cpp
    src/Animation/Skeleton.cpp
    src/Animation/IKSolver.cpp
    src/Animation/MorphTarget.cpp
    src/Animation/Cutscene.cpp
    src/Animation/MotionMatching.cpp
    src/Video/VideoPlayer.cpp
    src/Renderer/RayTracing/MetalRayTracing.cpp
    src/Renderer/RayTracing/VulkanRayTracing.cpp
    src/Renderer/RayTracing/RayTracingManager.cpp
    src/Renderer/Meshlet/MeshletBuilder.cpp
    src/Renderer/Meshlet/MeshletManager.cpp
    src/Renderer/Pipeline/VulkanPipelineCache.cpp
    src/Renderer/Upscaling/TemporalUpscaler.cpp
    src/Core/LargeWorldCoordinates.cpp
    src/Streaming/DirectStorageQueue.cpp
    src/Streaming/WorldPartitionManager.cpp
    src/Streaming/WorldStreamer.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/XPBDClothSim.cpp
    src/Navigation/NavMesh.cpp
    src/Audio/AudioEngine.cpp
    src/Audio/AcousticRayTracer.cpp
    src/Networking/NetworkManager.cpp
    src/Gameplay/GameplayScripts.cpp
    src/Gameplay/GameLoop.cpp
    src/Renderer/VulkanContext.cpp
    src/Renderer/VulkanBuffer.cpp
    src/Renderer/VulkanTexture.cpp
    src/Renderer/stb_image_impl.cpp
    src/Renderer/VulkanOffscreen.cpp
    src/Renderer/VulkanPipeline.cpp
    src/Renderer/VulkanShadowMap.cpp
    src/Renderer/VulkanReflectionProbes.cpp
    src/Renderer/VulkanSkinning.cpp
    src/Renderer/Bc7Encoder.cpp
    src/Renderer/VulkanVolumeTexture.cpp
    src/Renderer/VulkanLightmapTexture.cpp
    src/Renderer/LightmapBaker.cpp
    src/Renderer/VulkanFluidTargets.cpp
    src/Renderer/VulkanFluidSim.cpp
    src/Renderer/VulkanParticleSim.cpp
    src/Renderer/VulkanGpuProfiler.cpp
    src/Renderer/VulkanPreviewImage.cpp
    src/Renderer/VulkanSkyResources.cpp
    src/Scene/OceanWaves.cpp
    src/Scene/Liquids.cpp
    src/Scene/SkyClock.cpp
    src/Scene/Weather.cpp
    src/Scene/FoliageMeshes.cpp
    src/Scene/ParticleCollision.cpp
    src/Scene/ParticleSheets.cpp
    src/Renderer/ShaderGraph.cpp
    src/Renderer/RenderEffects.cpp
    src/UI/EditorUI.cpp
    src/UI/UINodeRenderer.cpp
    src/UI/NodeInspector.cpp
    src/Scripting/LuaScriptSystem.cpp
    src/Monitoring/GameProfiler.cpp
    src/Export/WebExporter.cpp
    src/Export/LinuxExporter.cpp
)

if(WIN32)
    list(APPEND SOURCES src/resources.rc)
endif()

set(HEADERS
    src/Core/Camera.h
    src/Core/InputManager.h
    src/Core/UndoRedoManager.h
    src/Physics/Collision.h
    src/Physics/PhysicsWorld.h
    src/Navigation/NavMesh.h
    src/Audio/AudioEngine.h
    src/Networking/NetworkManager.h
    src/Animation/Animation.h
    src/Animation/Skeleton.h
    src/Animation/IKSolver.h
    src/Animation/MorphTarget.h
    src/Animation/Cutscene.h
    src/Animation/MotionMatching.h
    src/Physics/XPBDClothSim.h
    src/Audio/AcousticRayTracer.h
    src/Video/VideoPlayer.h
    src/Gameplay/Time.h
    src/Gameplay/GameplayScripts.h
    src/Gameplay/GameLoop.h
    src/Scene/Mesh.h
    src/Scene/MeshSimplifier.h
    src/Scene/ObjLoader.h
    src/Scene/FbxLoader.h
    src/Scene/GltfLoader.h
    src/Scene/Node.h
    src/Scene/Scene.h
    src/Project/Project.h
    src/Localization/Localization.h
    src/Localization/GameLocalization.h
    src/Utils/SimpleJson.h
    src/Renderer/VulkanContext.h
    src/Renderer/VulkanBuffer.h
    src/Renderer/VulkanTexture.h
    src/Renderer/VulkanOffscreen.h
    src/Renderer/VulkanPipeline.h
    src/Renderer/VulkanSkinning.h
    src/Renderer/Bc7Encoder.h
    src/Renderer/VulkanVolumeTexture.h
    src/Renderer/VulkanLightmapTexture.h
    src/Renderer/LightmapBaker.h
    src/Renderer/VulkanFluidTargets.h
    src/Renderer/VulkanFluidSim.h
    src/Renderer/VulkanParticleSim.h
    src/Renderer/VulkanGpuProfiler.h
    src/Renderer/VulkanPreviewImage.h
    src/Renderer/VulkanSkyResources.h
    src/Scene/OceanWaves.h
    src/Scene/Liquids.h
    src/Scene/SkyClock.h
    src/Scene/Weather.h
    src/Scene/FoliageMeshes.h
    src/Scene/ParticleCollision.h
    src/Scene/ParticleSheets.h
    src/Renderer/VulkanShadowMap.h
    src/Renderer/VulkanReflectionProbes.h
    src/Renderer/ShaderGraph.h
    src/Renderer/RenderEffects.h
    src/Streaming/StreamingTypes.h
    src/Streaming/DirectStorageQueue.h
    src/Streaming/WorldPartitionManager.h
    src/UI/EditorUI.h
    src/UI/UINodeRenderer.h
    src/UI/NodeInspector.h
    src/Scripting/LuaScriptSystem.h
    src/Monitoring/GameProfiler.h
    src/Export/WebExporter.h
    src/Export/LinuxExporter.h
)

add_executable(Tessera ${SOURCES} ${HEADERS} ${SPV_SHADERS})
# Shader graph materials are compiled at runtime from shader.frag (see VulkanPipeline::GetShaderGraphPipeline)
target_compile_definitions(Tessera PRIVATE
    TESSERA_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    TESSERA_GLSLC_PATH="${GLSLC_EXECUTABLE}")
add_executable(TesseraGameEngine ALIAS Tessera)

if(UNIX AND NOT APPLE)
    add_custom_target(linux_export_template
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/templates/linux"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Tessera>"
            "${CMAKE_CURRENT_SOURCE_DIR}/templates/linux/TesseraGameRunner.x86_64"
        DEPENDS Tessera
        COMMENT "Installing Linux Standalone Export Template"
    )
endif()

target_include_directories(Tessera PRIVATE
    src
    ${Vulkan_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
    ${Stb_INCLUDE_DIR}
)
if(WIN32)
    target_include_directories(Tessera PRIVATE "${THIRDPARTY_DIR}/include")
endif()

target_link_libraries(Tessera PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
    unofficial::openfbx::openfbx
    ${LUA_LIBRARIES}
    Jolt::Jolt
)

if(WIN32)
    target_link_libraries(Tessera PRIVATE comdlg32 ws2_32 winmm psapi)
    
    # Standalone Single-File Game Bootstrapper
    add_executable(StandaloneBootstrapper WIN32 src/Standalone/BootstrapperMain.cpp)
    set_property(TARGET StandaloneBootstrapper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(StandaloneBootstrapper PRIVATE shell32 ole32)

    # Optional target: Export Templates for Windows (templates/windows/)
    add_custom_target(windows_export_template
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/templates/windows"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Tessera>"
            "${CMAKE_CURRENT_SOURCE_DIR}/templates/windows/TesseraGameRunner.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:StandaloneBootstrapper>"
            "${CMAKE_CURRENT_SOURCE_DIR}/templates/windows/StandaloneBootstrapper.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${THIRDPARTY_DIR}/bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/templates/windows"
        DEPENDS Tessera StandaloneBootstrapper
        COMMENT "Installing Windows Standalone Export Templates (templates/windows/)"
    )
endif()

# Copy shaders to output directory for debugging
add_custom_command(TARGET Tessera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:Tessera>/shaders"
)

# Copy lang directory to output directory
add_custom_command(TARGET Tessera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lang"
        "$<TARGET_FILE_DIR:Tessera>/lang"
)

# Copy assets to output directory
add_custom_command(TARGET Tessera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Tessera>/assets"
)

# Copy runtime thirdparty DLLs to executable directory
if(WIN32 AND EXISTS "${THIRDPARTY_DIR}")
    add_custom_command(TARGET Tessera POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${THIRDPARTY_DIR}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
            "$<TARGET_FILE_DIR:Tessera>"
    )
endif()

# Test Suite Executable
add_executable(EngineTestSuite
    tests/MasterTestSuite.cpp
    src/Localization/Localization.cpp
    src/Localization/GameLocalization.cpp
    src/Core/Camera.cpp
    src/Core/InputManager.cpp
    src/Core/UndoRedoManager.cpp
    src/Scene/Mesh.cpp
    src/Scene/MeshSimplifier.cpp
    src/Scene/ObjLoader.cpp
    src/Scene/FbxLoader.cpp
    src/Scene/GltfLoader.cpp
    src/Scene/Node.cpp
    src/Scene/SceneNodes.cpp
    src/Scene/Nodes/3D/MeshNode.cpp
    src/Scene/Nodes/3D/CharacterNode.cpp
    src/Scene/Nodes/3D/CameraNode.cpp
    src/Scene/Nodes/3D/LightNode.cpp
    src/Scene/Nodes/3D/ColliderNode.cpp
    src/Scene/Nodes/3D/EmptyNode.cpp
    src/Scene/Nodes/3D/SplineNode.cpp
    src/Scene/Nodes/3D/DecalNode.cpp
    src/Scene/Nodes/3D/ParticleNode.cpp
    src/Scene/Nodes/3D/AudioNode.cpp
    src/Scene/Nodes/3D/TerrainNode.cpp
    src/Renderer/stb_image_impl.cpp
    src/Scene/Nodes/UI/UINode.cpp
    src/Scene/Nodes/UI/UICanvasNode.cpp
    src/Scene/Nodes/UI/UIPanelNode.cpp
    src/Scene/Nodes/UI/UITextNode.cpp
    src/Scene/Nodes/UI/UIButtonNode.cpp
    src/Scene/Nodes/UI/UIProgressBarNode.cpp
    src/Scene/Nodes/UI/UISpinnerNode.cpp
    src/Scene/Nodes/UI/UITextFieldNode.cpp
    src/Scene/Nodes/UI/UIPasswordFieldNode.cpp
    src/Scene/Nodes/UI/UISliderNode.cpp
    src/Scene/Nodes/UI/UICheckboxNode.cpp
    src/Scene/Nodes/UI/UIRadioButtonNode.cpp
    src/Scene/Nodes/UI/UIWindowNode.cpp
    src/Scene/Nodes/UI/UIImageNode.cpp
    src/Scene/Nodes/UI/UIVideoNode.cpp
    src/Scene/Nodes/UI/UIScrollPaneNode.cpp
    src/Scene/Nodes/UI/UIDropdownNode.cpp
    src/Scene/Nodes/UI/UIDatePickerNode.cpp
    src/Scene/Nodes/UI/UISelectableListNode.cpp
    src/Scene/Nodes/UI/UIColorPickerNode.cpp
    src/Scene/Nodes/UI/UITableNode.cpp
    src/Scene/Nodes/UI/UITabsNode.cpp
    src/Scene/Nodes/UI/UIMenuBarNode.cpp
    src/Scene/Nodes/UI/UIShapeNode.cpp
    src/Scene/Nodes/UI/UIBoxContainerNode.cpp
    src/Scene/Nodes/UI/UIGridContainerNode.cpp
    src/Resource/Resource.cpp
    src/Resource/ResourceManager.cpp
    src/Scene/Nodes/NodeFactory.cpp
    src/Scene/Scene.cpp
    src/Scene/SkyClock.cpp
    src/Scene/Weather.cpp
    src/Scene/FoliageMeshes.cpp
    src/Scene/ParticleCollision.cpp
    src/Scene/ParticleSheets.cpp
    src/Animation/Animation.cpp
    src/Animation/Skeleton.cpp
    src/Animation/IKSolver.cpp
    src/Animation/MorphTarget.cpp
    src/Animation/Cutscene.cpp
    src/Animation/MotionMatching.cpp
    src/Video/VideoPlayer.cpp
    src/Renderer/RayTracing/MetalRayTracing.cpp
    src/Renderer/RayTracing/VulkanRayTracing.cpp
    src/Renderer/RayTracing/RayTracingManager.cpp
    src/Renderer/VulkanContext.cpp
    src/Renderer/VulkanBuffer.cpp
    src/Renderer/Meshlet/MeshletBuilder.cpp
    src/Renderer/Meshlet/MeshletManager.cpp
    src/Renderer/Pipeline/VulkanPipelineCache.cpp
    src/Renderer/Upscaling/TemporalUpscaler.cpp
    src/Core/LargeWorldCoordinates.cpp
    src/Streaming/DirectStorageQueue.cpp
    src/Streaming/WorldPartitionManager.cpp
    src/Streaming/WorldStreamer.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/XPBDClothSim.cpp
    src/Navigation/NavMesh.cpp
    src/Audio/AudioEngine.cpp
    src/Audio/AcousticRayTracer.cpp
    src/Networking/NetworkManager.cpp
    src/Renderer/ShaderGraph.cpp
    src/Renderer/RenderEffects.cpp
    src/UI/UINodeRenderer.cpp
    src/Scripting/LuaScriptSystem.cpp
    src/Gameplay/GameplayScripts.cpp
    src/Gameplay/GameLoop.cpp
    src/Scene/OceanWaves.cpp
    src/Monitoring/GameProfiler.cpp
    src/Export/WebExporter.cpp
    src/Export/LinuxExporter.cpp
)

if(WIN32)
    list(APPEND SOURCES src/resources.rc)
endif()

target_include_directories(EngineTestSuite PRIVATE
    src
    tests
    ${glm_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
    ${Stb_INCLUDE_DIR}
)
if(WIN32)
    target_include_directories(EngineTestSuite PRIVATE "${THIRDPARTY_DIR}/include")
endif()

target_link_libraries(EngineTestSuite PRIVATE
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
    ${LUA_LIBRARIES}
    unofficial::openfbx::openfbx
    Jolt::Jolt
)

if(WIN32)
    target_link_libraries(EngineTestSuite PRIVATE shell32 ws2_32 winmm psapi)
endif()

if(WIN32 AND EXISTS "${THIRDPARTY_DIR}")
    add_custom_command(TARGET EngineTestSuite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${THIRDPARTY_DIR}/$<IF:$<CONFIG:Debug>,debug/bin,bin>"
            "$<TARGET_FILE_DIR:EngineTestSuite>"
    )
endif()

enable_testing()
add_test(NAME EngineTestSuite COMMAND EngineTestSuite)
