-- scripts/headland/Player.lua
-- First-person explorer for the Headland world: walk with WASD (or the project's Move actions),
-- sprint with Shift, look around with the mouse while the right button is held. The camera
-- follows the terrain (Terrain.GetHeight) and stays above the water.
--
-- Uses only the engine's registered Lua API (Input, Terrain, self.position / self.rotation).

local walkSpeed = 4.5      -- m/s
local sprintSpeed = 11.0   -- m/s
local eyeHeight = 1.7      -- m above the ground
local lookSpeed = 0.15     -- degrees per pixel
local climbRate = 8.0      -- how quickly the eye follows the ground (1/s)

local function down(...)
    for _, name in ipairs({...}) do
        if Input.IsDown(name) then return true end
    end
    return false
end

local flying = false        -- free flight (F toggles): no ground following, Space / Ctrl rise and sink
local flySpeed = 30.0

function OnInit(self)
    -- Starting well above the ground (an aerial viewpoint) starts in flight
    local ground = Terrain.GetHeight(self.position.x, self.position.z)
    flying = ground ~= nil and (self.position.y - math.max(ground, 0.0)) > 20.0
    print("[Player] Headland explorer ready at (" .. string.format("%.0f, %.0f", self.position.x, self.position.z) .. ")"
          .. (flying and " in flight" or ""))
end

function OnUpdate(self, dt)
    if Input.IsPressed("F") then
        flying = not flying
        print("[Player] " .. (flying and "Flying" or "Walking"))
    end

    -- Mouse look (right button held)
    if Input.IsMouseDown(1) then
        local dx, dy = Input.GetMouseDelta()
        self.rotation.y = self.rotation.y - dx * lookSpeed
        self.rotation.x = math.max(-85.0, math.min(85.0, self.rotation.x - dy * lookSpeed))
    end

    -- Walking on the ground plane, relative to where the camera faces
    local forward, right = 0.0, 0.0
    if down("MoveForward", "W", "Up") then forward = forward + 1.0 end
    if down("MoveBackward", "S", "Down") then forward = forward - 1.0 end
    if down("MoveRight", "D", "Right") then right = right + 1.0 end
    if down("MoveLeft", "A", "Left") then right = right - 1.0 end
    local len = math.sqrt(forward * forward + right * right)
    if len > 0.0 then
        forward, right = forward / len, right / len
        local speed = flying and flySpeed or walkSpeed
        if down("Run", "LeftShift") then speed = flying and flySpeed * 3.0 or sprintSpeed end
        local yaw = math.rad(self.rotation.y)
        local fx, fz = -math.sin(yaw), -math.cos(yaw)   -- the camera looks down -Z
        local rx, rz = math.cos(yaw), -math.sin(yaw)
        self.position.x = self.position.x + (fx * forward + rx * right) * speed * dt
        self.position.z = self.position.z + (fz * forward + rz * right) * speed * dt
    end

    if flying then
        if down("Space") then self.position.y = self.position.y + flySpeed * dt end
        if down("LeftCtrl", "C") then self.position.y = self.position.y - flySpeed * dt end
        return
    end

    -- Follow the ground (and never sink below the sea surface)
    local ground = Terrain.GetHeight(self.position.x, self.position.z)
    if ground then
        local target = math.max(ground, 0.2) + eyeHeight
        local t = math.min(1.0, climbRate * dt)
        self.position.y = self.position.y + (target - self.position.y) * t
    end
end
