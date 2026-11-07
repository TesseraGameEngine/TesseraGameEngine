-- scripts/OrbiterCollectible.lua
-- Demonstrates smooth procedural rotation, hovering animation, and collection trigger

local rotSpeed = 75.0
local hoverSpeed = 3.0
local hoverAmp = 0.25
local baseY = 1.2
local timeAccum = 0.0

function OnInit(selfNode)
    baseY = selfNode.position.y
    timeAccum = 0.0
end

function OnUpdate(selfNode, dt)
    timeAccum = timeAccum + dt

    -- 1. Continuous rotation on Y axis
    selfNode.rotation.y = selfNode.rotation.y + (rotSpeed * dt)
    if selfNode.rotation.y >= 360.0 then
        selfNode.rotation.y = selfNode.rotation.y - 360.0
    end

    -- 2. Vertical sinusoidal bobbing/hovering
    selfNode.position.y = baseY + math.sin(timeAccum * hoverSpeed) * hoverAmp

    -- 3. Collision / proximity check with Player
    local player = Scene.FindNode("Player")
    if player then
        local dx = player.position.x - selfNode.position.x
        local dy = player.position.y - selfNode.position.y
        local dz = player.position.z - selfNode.position.z
        local distSq = dx * dx + dy * dy + dz * dz

        if distSq < 1.8 then
            -- Collect item: move to another spot and trigger visual feedback
            selfNode.position.x = math.random(-8, 8)
            selfNode.position.z = math.random(-8, 8)
            print("[OrbiterCollectible.lua] Collectible picked up by Player! Relocated to (" .. selfNode.position.x .. ", " .. selfNode.position.z .. ")")
        end
    end

    return selfNode
end
