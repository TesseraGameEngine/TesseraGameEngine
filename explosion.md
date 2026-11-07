# Tutorial: Creating Explosions in Tessera Game Engine

This step-by-step tutorial covers how to build explosive effects in the **Tessera Game Engine**. You will learn how to combine GPU Niagara compute particles, dynamic HDR light bounce (Real-Time VXGI), Jolt physics shockwaves, ground scorch decals, camera shake, and spatial audio into a single reusable explosion system.

---

## 1. Anatomy of a Video Game Explosion

In modern game engines, an explosion is not just a particle sprite; it is an orchestrated sequence of multiple engine systems firing simultaneously:

| Component | Engine System | Visual & Gameplay Impact |
| :--- | :--- | :--- |
| **Fireball & Plasma Burst** | `ParticleNode` (Flipbook / Additive) | High-temperature expanding core that vaporizes outward. |
| **Sparks & Shrapnel** | `ParticleNode` (GPU Niagara + Collision) | High-speed burning embers that bounce off floors, walls, and obstacles. |
| **Dissipating Smoke Plume** | `ParticleNode` (Billboard Alpha) | Heavy black/gray smoke that expands, slows down with drag, and rises. |
| **Heat Haze Distortion** | `ParticleNode` (Distortion Mode) | Optical refraction ring that warps the scene behind superheated air. |
| **Light Flash & Room Bounce**| `LightNode` + Real-Time VXGI | Blinding flash that casts shadows and bounces warm light off surrounding walls. |
| **Physical Blast Shockwave** | `JoltPhysics` (`AddImpulse`) | Propels nearby crates/barrels outward and flings NPC enemies into ragdolls. |
| **Ground & Wall Scorch** | `DecalNode` (Deferred OBB) | Stamps charred soot and cracked asphalt onto the impact surface. |
| **Sound & Environment Echo** | `Audio` + 3D DSP Reverb | Heavy low-end bass blast followed by realistic room reflections. |
| **Camera Trauma Shake** | Camera Script (Perlin / Exponential Decay)| Adds visceral physical impact for the player. |

---

## 2. Step 1: Setting up the Explosion Particle Emitter

Tessera includes a GPU-accelerated particle system capable of simulating up to **1,000,000 particles** via compute shaders (`particle_sim.comp`).

1. **Add the Particle Emitter**:
   - In the top menu, click **Scene &rarr; Add Entity &rarr; Particle Emitter** (or press <kbd>Ctrl+A</kbd> &rarr; **Particle Emitter**).
   - In the **Scene Hierarchy**, rename this node to `ExplosionVFX`.
2. **Apply the Explosion Preset**:
   - In the **Node Inspector** (right dock), locate the **Particle System** component.
   - Click the **Preset** dropdown and select **ExplosionBurst** (`ParticlePreset::ExplosionBurst`).
3. **Configure GPU Compute Simulation & Collision**:
   - **Simulation Mode**: Set to `GPU_Niagara` for compute-shader parallel simulation.
   - **Sprite Sheet (Flipbook)**: Set to `ParticleSheet::Explosion` (Tessera's built-in multi-frame fire blast atlas) or assign your custom 8x8 explosion atlas texture.
   - **Sparks & Trails**: Set **Trail Mode** to `PerParticle` to give flying debris glowing spark streaks.
   - **Physical Collision**: Set **Collision Mode** to `Scene` (`ParticleCollisionMode::Scene`). Flying sparks and shrapnel will physically bounce off terrain, solid meshes, and water instead of clipping through floors.
4. **Heat Haze Optical Distortion**:
   - For a cinematic shockwave distortion ring, add a secondary child particle emitter set to **Render Mode**: `Distortion`. This samples the scene color pre-pass and refracts the background around the expanding blast wave.

---

## 3. Step 2: Adding the Dynamic Flash & VXGI Light Bounce

An explosion should illuminate the surrounding environment:

1. **Add a Point Light**:
   - In the Scene Hierarchy, right-click `ExplosionVFX` &rarr; **Add Entity &rarr; Point Light**. Rename it to `ExplosionFlash`.
2. **Configure Light Settings**:
   - **Color**: Set to fiery warm orange-yellow `(1.0, 0.55, 0.15)`.
   - **Intensity**: Set to `25.0` (high HDR intensity for blinding initial flash).
   - **Radius**: Set to `15.0` meters.
   - **Cast Shadows**: `[x] Enabled` (casts crisp dynamic shadows from flying shrapnel and props).
3. **Enable Real-Time VXGI Bounce**:
   - Open **Window &rarr; Rendering, GI & Post-Processing** (<kbd>Shift+R</kbd>) and verify **Real-Time VXGI** is active.
   - When the explosion detonates, the blinding flash will dynamically bounce off walls, ceilings, and characters in the room, creating an atmospheric illumination flare.

---

## 4. Step 3: Projecting Ground & Wall Scorch Marks (`DecalNode`)

To leave permanent or fading blast marks on floors and walls without altering 3D geometry:

1. **Spawn a Decal Projector**:
   - Click **Scene &rarr; Add Entity &rarr; Decal Projector**. Rename it to `ScorchDecal`.
2. **Configure the Decal Properties**:
   - **Texture**: Set to a charred blast mark texture (e.g. `assets/textures/scorch_soot.png`).
   - **Blend Mode**: Set to `StainMultiply` (multiplies soot onto the ground) or `AlphaBlend`.
   - **Decal Size**: Set to `(4.0, 1.0, 4.0)` to cover a 4-meter blast crater radius.
   - **Normal Angle Threshold**: `75.0` degrees (ensures the decal wraps cleanly over bumpy terrain or stairs).

---

## 5. Step 4: Applying Jolt Physics Shockwaves & Ragdolls

Tessera's native **JoltPhysics** engine allows explosions to physically propel objects and characters:

1. Any dynamic body (`RigidBodyType::Dynamic`) in the area is pushed outward with a radial impulse.
2. If an enemy or humanoid character (`CharacterNode`) is caught in the blast radius, their skeleton seamlessly switches from keyframed animation to **Jolt Ragdoll physics** and is hurled backward.

---

## 6. Step 5: Full Lua Explosion Controller Script

Save the following script to `scripts/Explosion.lua`. You can attach it to an explosive barrel, grenade, or call it from any gameplay event:

```lua
-- scripts/Explosion.lua
local Explosion = {}

-- Explosion parameters
local BLAST_RADIUS = 8.0     -- meters
local BLAST_FORCE  = 450.0   -- physics impulse strength
local FLASH_DECAY  = 12.0    -- light fade speed

function Explosion.Detonate(origin_x, origin_y, origin_z)
    local pos = { x = origin_x, y = origin_y, z = origin_z }
    
    -- 1. Trigger the Niagara Particle Burst
    local vfx = Scene.find_node_by_name("ExplosionVFX")
    if vfx then
        vfx:set_world_position(pos.x, pos.y, pos.z)
        -- Trigger instantaneous burst of 150 particles
        vfx:emit_burst(150)
    end
    
    -- 2. Trigger Flash Light
    local light = Scene.find_node_by_name("ExplosionFlash")
    if light then
        light:set_world_position(pos.x, pos.y + 0.5, pos.z)
        light:set_light_intensity(30.0)
    end
    
    -- 3. Position the Ground Scorch Decal
    local decal = Scene.find_node_by_name("ScorchDecal")
    if decal then
        decal:set_world_position(pos.x, pos.y + 0.05, pos.z)
        decal:set_opacity(1.0)
    end
    
    -- 4. Play 3D Blast Audio
    Audio.play_sound("assets/audio/explosion_blast.wav", 1.0)
    
    -- 5. Physical Shockwave: Propel nearby dynamic bodies outward
    local all_nodes = Scene.get_all_nodes()
    for _, node in ipairs(all_nodes) do
        if node:is_physics_dynamic() then
            local nx, ny, nz = node:get_world_position()
            local dx = nx - pos.x
            local dy = ny - pos.y
            local dz = nz - pos.z
            local dist = math.sqrt(dx*dx + dy*dy + dz*dz)
            
            if dist < BLAST_RADIUS and dist > 0.01 then
                -- Normalize direction and scale by inverse square distance
                local falloff = 1.0 - (dist / BLAST_RADIUS)
                local impulse_x = (dx / dist) * BLAST_FORCE * falloff
                local impulse_y = ((dy / dist) + 0.4) * BLAST_FORCE * falloff -- upward launch kick
                local impulse_z = (dz / dist) * BLAST_FORCE * falloff
                
                Physics.AddImpulse(node:get_id(), impulse_x, impulse_y, impulse_z)
                
                -- If the node is a humanoid NPC, switch to Ragdoll
                if node:is_character() and node.StartRagdoll then
                    node:StartRagdoll()
                end
            end
        end
    end
    
    -- 6. Trigger Camera Shake
    local camera = Scene.get_active_camera()
    if camera and camera.AddTrauma then
        camera:AddTrauma(0.8) -- 0.0 to 1.0 screen trauma
    end
end

-- Update hook to smoothly fade out the flash light
function OnUpdate(self, dt)
    local light = Scene.find_node_by_name("ExplosionFlash")
    if light then
        local current = light:get_light_intensity()
        if current > 0.01 then
            light:set_light_intensity(math.max(0.0, current - FLASH_DECAY * dt * current))
        end
    end
end

return Explosion
```

---

## 7. Step 6: Testing the Explosion in Play Mode

1. Press **Play** (<kbd>F5</kbd>) in the editor.
2. Place a few dynamic cubes or an enemy character near your detonation point.
3. Call `Explosion.Detonate(x, y, z)` via keypress (e.g. pressing <kbd>Space</kbd> or clicking the mouse).
4. **Observe the Result**:
   - The orange fireball rapidly expands and leaves rolling dark smoke plumes.
   - Burning sparks fly outward, colliding with and bouncing off the ground.
   - The room flashes with HDR radiance that bounces into shadows via VXGI.
   - Nearby physics props are hurled outward into the air.
   - A scorched crater decal remains imprinted onto the ground.

---

## 8. Summary Checklist

- [x] Added `ParticleNode` with preset `ExplosionBurst`, `GPU_Niagara` mode, and `Scene` collision.
- [x] Added secondary `Distortion` particle emitter for shockwave optical heat haze.
- [x] Configured dynamic `PointLight` for initial HDR flash with Real-Time VXGI bounce.
- [x] Added `DecalNode` with `StainMultiply` to project a ground scorch mark.
- [x] Scripted `Physics.AddImpulse` radial blast force with distance falloff.
- [x] Added audio playback (`Audio.play_sound`) and camera trauma shake.

