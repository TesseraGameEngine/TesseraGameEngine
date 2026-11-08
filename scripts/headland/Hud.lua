-- scripts/headland/Hud.lua
-- Fills in the Headland HUD (built by the world builder): time, weather, frame rate, hardware
-- ray tracing and world streaming statistics, and a compass. F3 shows / hides the stats panel.
-- Uses the engine's Lua API: Scene, NodeAPI, Sky, Weather, Render, Streaming, Input.

local refresh = 0.0
local showStats = true
local ids = {}
local loggedOnce = false
local headings = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }

local function find(name)
    if ids[name] == nil then
        local node = Scene.FindNode(name)
        ids[name] = node and node.id or false
    end
    return ids[name] or nil
end

local function onOff(on) return on and "on" or "off" end

function OnUpdate(self, dt)
    if Input.IsPressed("F3") then
        showStats = not showStats
        local panel = find("HUD Panel")
        if panel then NodeAPI.SetVisible(panel, showStats) end
    end

    refresh = refresh - dt
    if refresh > 0.0 then return end
    refresh = 0.25

    -- Compass: the camera looks down -Z at yaw 0 (north); yaw grows counter-clockwise
    local player = Scene.FindNode("Player")
    local compass = find("HUD Compass")
    if player and compass then
        local heading = (360.0 - player.rotation.y) % 360.0
        local name = headings[math.floor((heading + 22.5) / 45.0) % 8 + 1]
        NodeAPI.SetText(compass, string.format("%s  %03.0f\194\176", name, heading))
    end

    local stats = find("HUD Stats")
    if not stats or not showStats then return end

    local t = Sky.GetTime() or 0
    local hours = math.floor(t)
    local minutes = math.floor((t - hours) * 60)
    local line1 = string.format("%02d:%02d   %s   %.0f fps", hours, minutes, Weather.Get() or "", Render.GetFPS())

    local rt = Render.GetRayTracingStats()
    local line2
    if rt.supported and Render.GetRayTracing("all") then
        line2 = string.format("Ray tracing: shadows %s, reflections %s   %d instances, %d BLAS, %.0f MB",
            onOff(Render.GetRayTracing("shadows")), onOff(Render.GetRayTracing("reflections")),
            rt.instances, rt.blas, rt.memoryMB)
    else
        line2 = "Ray tracing: off (shadow maps + screen-space reflections)"
    end

    local s = Streaming.GetStats()
    local line3 = string.format("Streaming: %d / %d sectors   %d objects   %d loading   %.0f MB read",
        s.active or 0, s.sectors or 0, s.nodes or 0, (s.loading or 0) + (s.ready or 0), s.mbRead or 0)

    NodeAPI.SetText(stats, line1 .. "\n" .. line2 .. "\n" .. line3)
    if not loggedOnce and (s.sectors or 0) > 0 and rt.instances > 0 then
        loggedOnce = true
        print("[HUD] " .. line1 .. " | " .. line2 .. " | " .. line3)
    end
end
