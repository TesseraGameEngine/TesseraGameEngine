#pragma once

#include <glm/glm.hpp>

struct SceneEnvironment;

// Time of day: places the sun and moon from the scene's clock (SceneEnvironment::timeOfDay,
// latitude, north angle, moon phase) and works out the light they give. The renderer points the
// scene's first directional light along it (sunlight by day, moonlight by night) and the sky
// shaders draw the sun, moon, stars and clouds where the clock puts them.
namespace SkyClock {

struct State {
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};   // toward the sun
    glm::vec3 moonDir{0.0f, -1.0f, 0.0f}; // toward the moon
    float moonIllumination{0.0f};         // lit share of the moon's disc (0 new .. 1 full)
    float starRotation{0.0f};             // radians the stars have turned around the pole (sidereal: a day and a little)
    // The directional light: toward the sun by day, the moon by night
    glm::vec3 lightDir{0.0f, 1.0f, 0.0f};
    glm::vec3 lightTint{1.0f};            // multiplies the light's colour
    float lightScale{1.0f};               // multiplies the light's intensity
};

// Direction toward a body `declinationDegrees` north of the celestial equator, `hours` after it
// was lowest (12 = highest). x = east, y = up, -z = north (before the north angle turns it).
glm::vec3 CelestialDirection(float hours, float latitudeDegrees, float northDegrees, float declinationDegrees = 0.0f);

// The sun's declination on a day of the year: +23.44 degrees at the June solstice, -23.44 in
// December, 0 at the equinoxes (day 81 and 264)
float SunDeclination(float dayOfYear);

// Hours from sunrise to sunset (0 in polar night, 24 in the midnight sun)
float DayLengthHours(float latitudeDegrees, float dayOfYear);

State Evaluate(const SceneEnvironment& env);

// Moves the clock on by dt seconds (a full day takes dayLengthMinutes). Past midnight the day of
// the year moves on, and the moon a day through its phases, when advanceCalendar is set.
void Advance(SceneEnvironment& env, float dt);

} // namespace SkyClock
