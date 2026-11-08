-- scripts/headland/WorldDirector.lua
-- Runs the Headland showcase: hardware ray tracing switches (F1 shadows, F2 reflections), a
-- weather cycle (F4: clear -> cloudy -> overcast -> rain -> storm), time skips (F5: +3 hours),
-- localization language switch (F6: English <-> Spanish), and
-- the camp's fire and lanterns, which glow at night and flicker.
-- Uses the engine's Lua API: Render, Weather, Sky, Scene, NodeAPI, Input, Localization.

local weathers = { "Clear", "Cloudy", "Overcast", "Rain", "Storm" }
local weatherIndex = 1
local lights = {}   -- { id, base intensity, flicker }
local clock = 0.0

local function smooth(e0, e1, x)
    local t = math.max(0.0, math.min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)
end

function OnInit(self)
    for _, name in ipairs({ "Campfire Light", "Lantern Light 1", "Lantern Light 2" }) do
        local node = Scene.FindNode(name)
        if node then
            local intensity = NodeAPI.GetLight(node.id)
            table.insert(lights, { id = node.id, base = intensity or 2.0, flicker = name == "Campfire Light" })
        end
    end
    Weather.Set(weathers[weatherIndex], 1.0)
end

function OnUpdate(self, dt)
    clock = clock + dt

    if Input.IsPressed("F1") then
        local on = not Render.GetRayTracing("shadows")
        Render.SetRayTracing("shadows", on)
        print("[WorldDirector] Ray-traced shadows " .. (on and "on" or "off"))
    end
    if Input.IsPressed("F2") then
        local on = not Render.GetRayTracing("reflections")
        Render.SetRayTracing("reflections", on)
        print("[WorldDirector] Ray-traced reflections " .. (on and "on" or "off"))
    end
    if Input.IsPressed("F4") then
        weatherIndex = weatherIndex % #weathers + 1
        Weather.Set(weathers[weatherIndex], 20.0)
        print("[WorldDirector] Weather -> " .. weathers[weatherIndex])
    end
    if Input.IsPressed("F5") then
        Sky.SetTime(((Sky.GetTime() or 12.0) + 3.0) % 24.0)
    end
    if Input.IsPressed("F6") then
        if Localization and Localization.GetLanguage and Localization.SetLanguage then
            local cur = Localization.GetLanguage()
            local nextLang = (cur == "es") and "en" or "es"
            Localization.SetLanguage(nextLang)
            print("[WorldDirector] Language -> " .. nextLang)
        end
    end

    -- Fire and lanterns: faint by day, full at night (dusk 18:30-20:30, dawn 5:00-7:00)
    local t = Sky.GetTime() or 12.0
    local night = math.max(smooth(18.5, 20.5, t), 1.0 - smooth(5.0, 7.0, t))
    local glow = 0.12 + 0.88 * night
    for i, light in ipairs(lights) do
        local intensity = light.base * glow
        if light.flicker then
            intensity = intensity * (0.82 + 0.1 * math.sin(clock * 13.0) + 0.08 * math.sin(clock * 29.0 + 1.3))
        end
        NodeAPI.SetLight(light.id, intensity)
    end
end
