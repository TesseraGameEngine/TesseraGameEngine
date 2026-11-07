# Tutorial: Setting Up a Playing 3D TV with Footage, Scanlines & Static Noise

This tutorial walks through creating a realistic in-game television set in the **Tessera Game Engine**. You will learn how to mount animated video footage onto a 3D TV model, layer analog static snow and CRT phosphor scanlines over it, bounce dynamic screen light into the surrounding room via Real-Time VXGI, and attach 3D spatialized audio.

---

## 1. Prerequisites & Asset Preparation

To follow this tutorial, prepare the following assets in your project directory:

| Asset | Format | Location | Purpose |
| :--- | :--- | :--- | :--- |
| **3D TV Model** | `.fbx`, `.gltf`, `.glb`, or `.obj` | `assets/models/tv_set.fbx` | The physical television housing (chassis, knobs, glass tube/screen). |
| **Video Footage** | Animated `.gif`, sprite atlas, or image sequence | `assets/textures/broadcast.gif` | The video footage to play on the screen. |
| **Static / Audio Track** | `.wav` (PCM 16-bit 44.1kHz) | `assets/audio/tv_static.wav` | White-noise hum or television audio broadcast. |

> [!TIP]
> If you do not have an external 3D TV model or video clip on hand, you can spawn Tessera's pre-built television preset from the top menu: **Scene &rarr; Add In-Game Video Screen... &rarr; Living Room TV Set (16:9)** or **Retro CRT Color Bars (4:3)**.

---

## 2. Step 1: Placing the 3D TV in Your Scene

1. **Import the Model**:
   - Place your `tv_set.fbx` file inside your project's `assets/models/` folder.
   - In the editor top menu, click **Scene &rarr; Add Entity &rarr; Mesh** (or press <kbd>Ctrl+A</kbd> and select **3D Mesh**).
2. **Assign the 3D Asset**:
   - In the **Scene Hierarchy** (left panel), select your new mesh node.
   - In the **Node Inspector** (right dock), find **3D Model Asset Picker** and select `tv_set.fbx`.
3. **Position & Scale the TV**:
   - Press <kbd>W</kbd> to translate and <kbd>R</kbd> to scale your TV onto a table, stand, or floor in your room.

---

## 3. Step 2: Configuring the Video Screen Surface

If your 3D TV model already has a distinct screen submesh (e.g., `Screen_Glass`), select that node. Otherwise, spawn a simple flat quad (**Scene &rarr; Add Primitive &rarr; Plane**) and align it flush against the TV's front bezel.

1. In the **Node Inspector**, scroll down to the **Material & Appearance (PBR)** section.
2. Check the box labeled **"Enable Video Screen"** (`isVideoScreen = true`).
3. Set **Video Source File** (`videoSourcePath`) to your footage:
   ```text
   assets/textures/broadcast.gif
   ```
4. Configure the playback options:
   - **Video Playing**: `[x] Checked` (starts playing immediately).
   - **Loop Video**: `[x] Checked` (repeats continuously).
   - **Playback Speed**: `1.0x` (standard real-time speed).

Tessera will instantly start decoding your frames in the background and uploading them every frame to dynamic Vulkan texture slot `@video_<nodeId>`.

---

## 4. Step 3: Layering CRT Scanlines & Analog Static Noise

To give the footage an authentic physical television tube aesthetic rather than looking like a clean digital sticker:

1. **Adjust Phosphor Scanlines**:
   - In the Inspector, locate **Scanline Strength** (`videoScanlineStrength`).
   - Set the slider to `0.35` for subtle modern monitor lines, or `0.65` &ndash; `0.85` for deep vintage cathode-ray tube phosphor lines.
2. **Using the Procedural CRT Static Preset**:
   - If you want the TV to show analog static noise (snow) or test bars, set **Channel Preset** (`videoChannelPreset`) to **Retro CRT Color Bars (4:3)**.
   - Tessera will procedurally render SMPTE color calibration bars, random pixel static noise, and horizontal rolling VHS tracking jitter bars across the screen buffer.
3. **Layering Static Over Custom Footage via Shader Graph**:
   - If you have custom movie footage and want to blend static snow directly on top:
     1. Open **Window &rarr; Visual Shader Graph Editor** (<kbd>Shift+G</kbd>).
     2. Create a **Texture Sample** node hooked to `@video_<nodeId>`.
     3. Create a **Procedural Noise** node (or sample a seamless static grain texture).
     4. Multiply or Screen blend the noise with the video texture and wire the output into the **Emissive Color** and **Albedo** inputs.
     5. Click **Apply to Selected Object**.

---

## 5. Step 4: Enabling Real-Time Light Bounce (VXGI)

A real television illuminates the room around it. Tessera automatically couples the video system with its lighting engine:

1. In the Inspector, set **Screen Brightness** (`videoScreenBrightness`) to `2.0` (or higher for high-contrast neon/night scenes).
2. Open **Window &rarr; Rendering, GI & Post-Processing** (<kbd>Shift+R</kbd>) or select **Render &rarr; Enable Real-Time VXGI**.
3. Verify **Voxel Global Illumination (VXGI)** and **Screen-Space Reflections (SSR)** are enabled.

**What Happens**:
* Tessera computes the average HDR color and luminosity of your video footage on every frame.
* If a bright flash occurs in the footage, a wave of bright light realistically bounces off the walls, floor, and furniture.
* If the TV switches to static snow, a cold, flickering blue-white ambiance fills the surrounding room.

---

## 6. Step 5: Adding 3D Spatial Audio

To make the television sound like it exists in physical 3D space:

1. In the Node Inspector under **Video Audio Settings**:
   - Set **Audio File** (`videoAudioPath`) to `assets/audio/tv_static.wav`.
   - Set **Volume** (`videoAudioVolume`) to `0.8`.
   - Enable **Spatial Audio** (`videoSpatialAudio = true`).
2. When the player walks around the room:
   - The television audio attenuates over distance.
   - Walking behind the TV muffles the higher frequencies.
   - Turning the camera pans the sound between left and right speakers.

---

## 7. Step 6: Controlling the TV with Lua Scripting

You can control the TV interactively (e.g. when the player presses a button on a remote control or walks up and presses <kbd>E</kbd> to interact).

Attach a script to your TV node or player controller:

```lua
-- TVController.lua
local tvNodeId = 0
local isTvOn = true

function OnInit(self)
    -- Find the TV screen node by name
    local tvScreen = Scene.find_node_by_name("TVScreen")
    if tvScreen then
        tvNodeId = tvScreen:get_id()
    end
end

function OnUpdate(self, dt)
    -- Toggle TV On/Off when pressing the 'E' key
    if Input.is_action_just_pressed("interact") then
        isTvOn = not isTvOn
        
        if isTvOn then
            Video.Play(tvNodeId)
            -- Switch channel to TV News (preset 2)
            Video.SetChannel(tvNodeId, 2)
        else
            Video.Stop(tvNodeId)
        end
    end
    
    -- Cycle channels with 'C' key
    if isTvOn and Input.is_action_just_pressed("cycle_channel") then
        -- 1 = Cinema Reel, 2 = TV News, 3 = CCTV, 5 = Retro CRT Static
        Video.SetChannel(tvNodeId, 5)
    end
end
```

### Complete Video Lua API Reference

| Function | Parameters | Description |
| :--- | :--- | :--- |
| `Video.Play(nodeId)` | `int nodeId` | Resumes or starts playback on the target screen. |
| `Video.Pause(nodeId)` | `int nodeId` | Pauses playback at the current frame. |
| `Video.Stop(nodeId)` | `int nodeId` | Stops playback and rewinds to frame 0. |
| `Video.SetChannel(nodeId, channel)`| `int nodeId, int channel` | Switches to preset: `0` Custom, `1` Cinema, `2` TV News, `3` CCTV, `4` Smartphone, `5` Retro CRT Static, `6` Sci-Fi Radar. |
| `Video.SetSource(nodeId, path)` | `int nodeId, string path` | Loads a new video file (`.gif`, atlas `.png`, or frame folder). |
| `Video.Seek(nodeId, timestamp)` | `int nodeId, float time` | Jumps playback to the specified time in seconds. |
| `Video.IsPlaying(nodeId)` | `int nodeId` | Returns `true` if the screen is actively playing. |
| `Video.PlayCutaway(title, ch, dur, pip)` | `str, int, float, bool` | Pops up an immediate skippable fullscreen or corner PiP broadcast overlay. |

---

## 8. Summary Checklist

- [x] Imported 3D TV mesh model into `assets/models/`.
- [x] Assigned screen material or quad with `isVideoScreen = true`.
- [x] Provided footage file path or selected a procedural broadcast preset.
- [x] Set scanline strength (`0.4` &ndash; `0.8`) for phosphor tube lines.
- [x] Enabled Real-Time VXGI in Render settings for dynamic screen bounce lighting.
- [x] Attached spatialized audio track for realistic 3D room acoustics.
- [x] Linked Lua script for interactive on/off and channel cycling.

