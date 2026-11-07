#version 450

#extension GL_GOOGLE_include_directive : require

#include "common_ubo.glsl"
#include "fluid_common.glsl"
#define FLUID_SMOOTH_SECOND
#include "fluid_smooth_common.glsl"
