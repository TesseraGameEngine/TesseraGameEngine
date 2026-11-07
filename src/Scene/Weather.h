#pragma once

#include <string>

struct SceneEnvironment;

// Weather: presets that blend the sky's clouds, wind and fog with rain, snow and lightning
// (SceneEnvironment::weather*). Rain wets surfaces and fills puddles over time, snow settles on
// them; both dry or melt away afterwards. The renderer draws the rain and snow around the camera
// (precip.vert/.frag), the wet and snowy surfaces (weather_common.glsl) and the lightning.
namespace Weather {

enum Preset { Clear = 0, Cloudy, Overcast, Rain, Storm, Snow, PresetCount };

struct Targets {
    float cloudCoverage;
    float cloudOpacity;
    float rain;
    float snow;
    float fogDensity;
    float windSpeed;
    float lightningRate; // strikes per minute
};

const char* PresetName(int preset);
int PresetFromName(const std::string& name); // -1 when unknown (case-insensitive)
Targets PresetTargets(int preset);

// Moves the weather on by dt seconds: blends toward the preset (when presets are on), wets and
// dries surfaces, settles and melts snow, and runs lightning
void Update(SceneEnvironment& env, float dt);

// A lightning strike now (flash and bolt). Its thunder follows after the time sound takes to
// arrive from it (env.thunderDue is set then, for the audio player).
void Strike(SceneEnvironment& env);

} // namespace Weather
