-- scripts/PlayerCharacter.lua
-- Controls player movement, jump, gravity, stamina, and 2D HUD updates in pure Lua.

local speed = 7.0
local jumpForce = 9.5
local gravity = -20.0
local velocityY = 0.0
local isGrounded = true

-- Player state
local maxHealth = 100.0
local currentHealth = 100.0
local maxStamina = 100.0
local currentStamina = 100.0
local score = 0

function OnInit(selfNode)
    print("[PlayerCharacter.lua] Initializing Player Character: " .. selfNode.name)
    currentHealth = 100.0
    currentStamina = 100.0
    score = 0
    velocityY = 0.0
    isGrounded = true
end

function OnUpdate(selfNode, dt)
    -- 1. Read input actions
    local moveX = 0.0
    local moveZ = 0.0

    if Input.IsDown("MoveForward") or Input.IsDown("W") or Input.IsDown("Up") then
        moveZ = moveZ - 1.0
    end
    if Input.IsDown("MoveBackward") or Input.IsDown("S") or Input.IsDown("Down") then
        moveZ = moveZ + 1.0
    end
    if Input.IsDown("MoveLeft") or Input.IsDown("A") or Input.IsDown("Left") then
        moveX = moveX - 1.0
    end
    if Input.IsDown("MoveRight") or Input.IsDown("D") or Input.IsDown("Right") then
        moveX = moveX + 1.0
    end

    -- Normalize diagonal movement
    local length = math.sqrt(moveX * moveX + moveZ * moveZ)
    if length > 0.0001 then
        moveX = moveX / length
        moveZ = moveZ / length
    end

    -- Sprint check
    local currentSpeed = speed
    if (Input.IsDown("Sprint") or Input.IsDown("LeftShift")) and currentStamina > 5.0 and length > 0.0 then
        currentSpeed = speed * 1.6
        currentStamina = math.max(0.0, currentStamina - (20.0 * dt))
    else
        currentStamina = math.min(maxStamina, currentStamina + (15.0 * dt))
    end

    -- Apply horizontal movement
    selfNode.position.x = selfNode.position.x + (moveX * currentSpeed * dt)
    selfNode.position.z = selfNode.position.z + (moveZ * currentSpeed * dt)

    -- 2. Jump input
    if (Input.IsPressed("Jump") or Input.IsPressed("Space")) and isGrounded then
        velocityY = jumpForce
        isGrounded = false
        print("[PlayerCharacter.lua] Jump activated!")
    end

    -- 3. Gravity and vertical physics
    if not isGrounded then
        velocityY = velocityY + (gravity * dt)
        selfNode.position.y = selfNode.position.y + (velocityY * dt)

        -- Ground collision at y = 0.5
        if selfNode.position.y <= 0.5 then
            selfNode.position.y = 0.5
            velocityY = 0.0
            isGrounded = true
        end
    end

    -- 4. Update In-Game 2D HUD UI elements
    local healthBar = Scene.FindNode("Health Bar")
    if healthBar then
        NodeAPI.SetUIProgress(healthBar.id, currentHealth / maxHealth)
    end

    local staminaBar = Scene.FindNode("Stamina Bar")
    if staminaBar then
        NodeAPI.SetUIProgress(staminaBar.id, currentStamina / maxStamina)
    end

    local scoreLabel = Scene.FindNode("Score Text")
    if scoreLabel then
        NodeAPI.SetUIText(scoreLabel.id, "Score: " .. tostring(score) .. " | Pos: (" .. string.format("%.1f", selfNode.position.x) .. ", " .. string.format("%.1f", selfNode.position.z) .. ")")
    end

    -- Return updated table to engine
    return selfNode
end
