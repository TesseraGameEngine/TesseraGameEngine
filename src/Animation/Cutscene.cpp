#include "Cutscene.h"
#include "Scene/Nodes/3D/CharacterNode.h"
#include "Audio/AudioEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Animation {

static JsonValue Vec3ToJson(const glm::vec3& v) {
    JsonValue arr = JsonValue::Array();
    arr.Push(JsonValue(static_cast<double>(v.x)));
    arr.Push(JsonValue(static_cast<double>(v.y)));
    arr.Push(JsonValue(static_cast<double>(v.z)));
    return arr;
}

static glm::vec3 JsonToVec3(const JsonValue& arr, const glm::vec3& def = glm::vec3(0.0f)) {
    if (arr.IsArray() && arr.arrVal.size() >= 3) {
        return glm::vec3(
            arr.arrVal[0].AsFloat(def.x),
            arr.arrVal[1].AsFloat(def.y),
            arr.arrVal[2].AsFloat(def.z)
        );
    }
    return def;
}

JsonValue CutsceneCameraShot::Serialize() const {
    JsonValue j = JsonValue::Object();
    j["startTime"] = JsonValue(static_cast<double>(startTime));
    j["duration"] = JsonValue(static_cast<double>(duration));
    j["startPos"] = Vec3ToJson(startPos);
    j["endPos"] = Vec3ToJson(endPos);
    j["startLookAt"] = Vec3ToJson(startLookAt);
    j["endLookAt"] = Vec3ToJson(endLookAt);
    j["lookAtNodeName"] = JsonValue(lookAtNodeName);
    j["lookAtOffset"] = Vec3ToJson(lookAtOffset);
    j["startFov"] = JsonValue(static_cast<double>(startFov));
    j["endFov"] = JsonValue(static_cast<double>(endFov));
    j["smoothBlend"] = JsonValue(smoothBlend);
    return j;
}

void CutsceneCameraShot::Deserialize(const JsonValue& j) {
    if (!j.IsObject()) return;
    startTime = j["startTime"].AsFloat(0.0f);
    duration = std::max(0.01f, j["duration"].AsFloat(3.0f));
    startPos = JsonToVec3(j["startPos"], startPos);
    endPos = JsonToVec3(j["endPos"], endPos);
    startLookAt = JsonToVec3(j["startLookAt"], startLookAt);
    endLookAt = JsonToVec3(j["endLookAt"], endLookAt);
    lookAtNodeName = j["lookAtNodeName"].AsString("");
    lookAtOffset = JsonToVec3(j["lookAtOffset"], lookAtOffset);
    startFov = j["startFov"].AsFloat(55.0f);
    endFov = j["endFov"].AsFloat(42.0f);
    smoothBlend = j["smoothBlend"].AsBool(true);
}

JsonValue CutsceneActorAction::Serialize() const {
    JsonValue j = JsonValue::Object();
    j["actorNodeName"] = JsonValue(actorNodeName);
    j["startTime"] = JsonValue(static_cast<double>(startTime));
    j["duration"] = JsonValue(static_cast<double>(duration));
    j["useCurrentStartPos"] = JsonValue(useCurrentStartPos);
    j["startPos"] = Vec3ToJson(startPos);
    j["endPos"] = Vec3ToJson(endPos);
    j["animState"] = JsonValue(animState);
    j["endAnimState"] = JsonValue(endAnimState);
    j["faceMovementDirection"] = JsonValue(faceMovementDirection);
    return j;
}

void CutsceneActorAction::Deserialize(const JsonValue& j) {
    if (!j.IsObject()) return;
    actorNodeName = j["actorNodeName"].AsString("");
    startTime = j["startTime"].AsFloat(0.0f);
    duration = std::max(0.01f, j["duration"].AsFloat(3.0f));
    useCurrentStartPos = j["useCurrentStartPos"].AsBool(true);
    startPos = JsonToVec3(j["startPos"], startPos);
    endPos = JsonToVec3(j["endPos"], endPos);
    animState = j["animState"].AsString("Walk");
    endAnimState = j["endAnimState"].AsString("Idle");
    faceMovementDirection = j["faceMovementDirection"].AsBool(true);
    runtimeStartCaptured = false;
}

JsonValue CutsceneSubtitleCue::Serialize() const {
    JsonValue j = JsonValue::Object();
    j["startTime"] = JsonValue(static_cast<double>(startTime));
    j["duration"] = JsonValue(static_cast<double>(duration));
    j["speaker"] = JsonValue(speaker);
    j["text"] = JsonValue(text);
    j["audioClipPath"] = JsonValue(audioClipPath);
    j["audioVolume"] = JsonValue(static_cast<double>(audioVolume));
    j["synthFrequencyHz"] = JsonValue(static_cast<double>(synthFrequencyHz));
    return j;
}

void CutsceneSubtitleCue::Deserialize(const JsonValue& j) {
    if (!j.IsObject()) return;
    startTime = j["startTime"].AsFloat(0.0f);
    duration = std::max(0.1f, j["duration"].AsFloat(3.0f));
    speaker = j["speaker"].AsString("Narrator");
    text = j["text"].AsString("");
    audioClipPath = j["audioClipPath"].AsString("");
    audioVolume = j["audioVolume"].AsFloat(0.85f);
    synthFrequencyHz = j["synthFrequencyHz"].AsFloat(0.0f);
    runtimeTriggered = false;
}

JsonValue CutsceneSequence::Serialize() const {
    JsonValue j = JsonValue::Object();
    j["name"] = JsonValue(name);
    j["duration"] = JsonValue(static_cast<double>(duration));
    j["skippable"] = JsonValue(skippable);
    j["silentAudio"] = JsonValue(silentAudio);
    j["restoreCameraOnEnd"] = JsonValue(restoreCameraOnEnd);
    j["letterboxAmount"] = JsonValue(static_cast<double>(letterboxAmount));

    j["colorFilter"] = JsonValue(static_cast<int>(colorFilter));
    j["overrideVignette"] = JsonValue(overrideVignette);
    j["vignetteAmount"] = JsonValue(static_cast<double>(vignetteAmount));
    j["filmGrainAmount"] = JsonValue(static_cast<double>(filmGrainAmount));
    j["chromaticAberrationAmount"] = JsonValue(static_cast<double>(chromaticAberrationAmount));
    j["enableDof"] = JsonValue(enableDof);
    j["autoFocusOnLookAt"] = JsonValue(autoFocusOnLookAt);
    j["dofFocusDistance"] = JsonValue(static_cast<double>(dofFocusDistance));
    j["dofAperture"] = JsonValue(static_cast<double>(dofAperture));

    j["triggerOnLocation"] = JsonValue(triggerOnLocation);
    j["triggerActorName"] = JsonValue(triggerActorName);
    j["triggerPosition"] = Vec3ToJson(triggerPosition);
    j["triggerRadius"] = JsonValue(static_cast<double>(triggerRadius));
    j["triggerOnce"] = JsonValue(triggerOnce);

    JsonValue shotsArr = JsonValue::Array();
    for (const auto& s : cameraShots) shotsArr.Push(s.Serialize());
    j["cameraShots"] = shotsArr;

    JsonValue actionsArr = JsonValue::Array();
    for (const auto& a : actorActions) actionsArr.Push(a.Serialize());
    j["actorActions"] = actionsArr;

    JsonValue cuesArr = JsonValue::Array();
    for (const auto& c : subtitleCues) cuesArr.Push(c.Serialize());
    j["subtitleCues"] = cuesArr;

    return j;
}

void CutsceneSequence::Deserialize(const JsonValue& j) {
    if (!j.IsObject()) return;
    name = j["name"].AsString("CinematicCutscene");
    duration = std::max(0.1f, j["duration"].AsFloat(6.0f));
    skippable = j["skippable"].AsBool(true);
    silentAudio = j["silentAudio"].AsBool(false);
    restoreCameraOnEnd = j["restoreCameraOnEnd"].AsBool(true);
    letterboxAmount = j["letterboxAmount"].AsFloat(0.12f);

    colorFilter = static_cast<CutsceneColorFilter>(j["colorFilter"].AsInt(0));
    overrideVignette = j["overrideVignette"].AsBool(true);
    vignetteAmount = j["vignetteAmount"].AsFloat(0.45f);
    filmGrainAmount = j["filmGrainAmount"].AsFloat(0.0f);
    chromaticAberrationAmount = j["chromaticAberrationAmount"].AsFloat(0.0f);
    enableDof = j["enableDof"].AsBool(false);
    autoFocusOnLookAt = j["autoFocusOnLookAt"].AsBool(true);
    dofFocusDistance = j["dofFocusDistance"].AsFloat(5.0f);
    dofAperture = j["dofAperture"].AsFloat(0.35f);

    triggerOnLocation = j["triggerOnLocation"].AsBool(false);
    triggerActorName = j["triggerActorName"].AsString("*");
    triggerPosition = JsonToVec3(j["triggerPosition"], triggerPosition);
    triggerRadius = std::max(0.1f, j["triggerRadius"].AsFloat(2.5f));
    triggerOnce = j["triggerOnce"].AsBool(true);
    runtimeHasTriggered = false;

    cameraShots.clear();
    if (j.HasKey("cameraShots") && j["cameraShots"].IsArray()) {
        for (const auto& item : j["cameraShots"].arrVal) {
            CutsceneCameraShot shot;
            shot.Deserialize(item);
            cameraShots.push_back(shot);
        }
    }

    actorActions.clear();
    if (j.HasKey("actorActions") && j["actorActions"].IsArray()) {
        for (const auto& item : j["actorActions"].arrVal) {
            CutsceneActorAction act;
            act.Deserialize(item);
            actorActions.push_back(act);
        }
    }

    subtitleCues.clear();
    if (j.HasKey("subtitleCues") && j["subtitleCues"].IsArray()) {
        for (const auto& item : j["subtitleCues"].arrVal) {
            CutsceneSubtitleCue cue;
            cue.Deserialize(item);
            subtitleCues.push_back(cue);
        }
    }
}

bool CutscenePlayer::Start(Scene& scene, const std::string& cutsceneName) {
    CutsceneSequence* seq = scene.FindCutscene(cutsceneName);
    if (!seq) return false;
    return StartSequence(scene, *seq);
}

bool CutscenePlayer::StartSequence(Scene& scene, CutsceneSequence& sequence) {
    if (m_isPlaying) {
        Stop(scene, false);
    }

    m_activeSequence = &sequence;
    m_activeCutsceneName = sequence.name;
    m_currentTime = 0.0f;
    m_activeSubtitle = nullptr;
    m_isPlaying = true;
    sequence.runtimeHasTriggered = true;

    // Reset per-playback runtime flags on actions and subtitle cues
    for (auto& act : sequence.actorActions) {
        act.runtimeStartCaptured = false;
    }
    for (auto& cue : sequence.subtitleCues) {
        cue.runtimeTriggered = false;
    }

    // 1. Snapshot SceneEnvironment so camera effects (Sepia, B&W, Vignette, DOF, etc.) restore cleanly
    m_savedEnvironment = scene.GetEnvironment();
    m_hasEnvironmentSnapshot = true;

    // 2. Snapshot active camera transform & FOV
    m_createdTempCamera = false;
    auto cam = scene.GetActiveCamera();
    if (!cam) {
        cam = scene.AddCameraNode("CutsceneCamera");
        scene.SetActiveCamera(cam->id);
        m_createdTempCamera = true;
    }
    if (cam) {
        m_hasCameraSnapshot = true;
        m_savedCameraNodeId = cam->id;
        m_savedCameraPos = cam->position;
        m_savedCameraRot = cam->rotation;
        m_savedCameraFov = cam->cameraFov;
    }

    // 3. Pause patrol AI on any CharacterNode controlled by this cutscene
    m_savedCharStates.clear();
    for (const auto& act : sequence.actorActions) {
        if (act.actorNodeName.empty()) continue;
        auto node = scene.GetNodeByName(act.actorNodeName);
        if (node && node->nodeType == NodeType::Character) {
            auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
            if (charNode) {
                bool alreadySaved = false;
                for (const auto& s : m_savedCharStates) {
                    if (s.nodeId == charNode->id) { alreadySaved = true; break; }
                }
                if (!alreadySaved) {
                    m_savedCharStates.push_back({charNode->id, charNode->isPatrolling});
                    charNode->isPatrolling = false;
                }
            }
        }
    }

    // Emit signal on all root nodes so scripts can react to cutscene start
    for (const auto& n : scene.GetNodes()) {
        if (n) n->EmitSignal("on_cutscene_started", {sequence.name});
    }

    // Evaluate t = 0 immediately so frame 0 has the exact camera shot & visual filter
    Update(scene, 0.0f);
    return true;
}

static void OrientCameraToLookAt(const std::shared_ptr<Node>& cam, const glm::vec3& eye, const glm::vec3& target, const Scene& scene) {
    if (!cam) return;
    glm::vec3 dir = target - eye;
    float len = glm::length(dir);
    if (len < 1e-4f) {
        cam->position = eye;
        return;
    }
    dir /= len;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(dir, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::mat4 view = glm::lookAt(eye, target, up);
    glm::mat4 world = glm::inverse(view);
    cam->SetWorldTransformMatrix(world, scene);
    cam->scale = glm::vec3(1.0f);
}

void CutscenePlayer::ApplyEnvironmentEffects(Scene& scene, float currentFocusDist) {
    if (!m_activeSequence || !m_hasEnvironmentSnapshot) return;

    // Start from the saved baseline environment each frame
    SceneEnvironment env = m_savedEnvironment;

    // Apply color filter preset
    switch (m_activeSequence->colorFilter) {
        case CutsceneColorFilter::None:
            break;
        case CutsceneColorFilter::Sepia:
            env.saturation = 0.22f;
            env.temperature = 0.65f;
            env.tint = 0.12f;
            env.contrast = 1.12f;
            break;
        case CutsceneColorFilter::BlackAndWhite:
            env.saturation = 0.0f;
            env.temperature = 0.0f;
            env.tint = 0.0f;
            env.contrast = 1.10f;
            break;
        case CutsceneColorFilter::NoirHighContrast:
            env.saturation = 0.0f;
            env.temperature = 0.0f;
            env.tint = 0.0f;
            env.contrast = 1.45f;
            env.vignette = std::max(env.vignette, 0.70f);
            env.filmGrain = std::max(env.filmGrain, 0.22f);
            break;
        case CutsceneColorFilter::VintageWarm:
            env.saturation = 0.80f;
            env.temperature = 0.45f;
            env.tint = 0.06f;
            env.contrast = 1.08f;
            env.filmGrain = std::max(env.filmGrain, 0.14f);
            break;
        case CutsceneColorFilter::ColdMoonlight:
            env.saturation = 0.62f;
            env.temperature = -0.65f;
            env.tint = -0.05f;
            env.contrast = 1.18f;
            break;
        case CutsceneColorFilter::DreamyBloom:
            env.bloomIntensity = std::max(env.bloomIntensity, 1.15f);
            env.contrast = 0.94f;
            env.saturation = 1.08f;
            break;
    }

    // Apply explicit camera lens & post-FX overrides
    if (m_activeSequence->overrideVignette) {
        env.vignette = std::clamp(m_activeSequence->vignetteAmount, 0.0f, 1.0f);
    }
    if (m_activeSequence->filmGrainAmount > 0.001f) {
        env.filmGrain = std::clamp(m_activeSequence->filmGrainAmount, 0.0f, 1.0f);
    }
    if (m_activeSequence->chromaticAberrationAmount > 0.001f) {
        env.chromaticAberration = std::clamp(m_activeSequence->chromaticAberrationAmount, 0.0f, 1.0f);
    }
    if (m_activeSequence->enableDof) {
        env.depthOfField = true;
        env.focusDistance = m_activeSequence->autoFocusOnLookAt ? std::max(0.3f, currentFocusDist) : std::max(0.3f, m_activeSequence->dofFocusDistance);
        env.aperture = std::clamp(m_activeSequence->dofAperture, 0.05f, 1.0f);
    }

    scene.GetEnvironment() = env;
}

void CutscenePlayer::Update(Scene& scene, float dt) {
    if (!m_isPlaying || !m_activeSequence) return;

    m_currentTime += std::max(0.0f, dt);

    // 1. Update Actor Actions (walk to mark, orient yaw, drive skeletal animation state)
    for (auto& act : m_activeSequence->actorActions) {
        if (act.actorNodeName.empty()) continue;
        auto actor = scene.GetNodeByName(act.actorNodeName);
        if (!actor) continue;

        float endTime = act.startTime + std::max(0.01f, act.duration);
        if (m_currentTime >= act.startTime && m_currentTime <= endTime) {
            if (!act.runtimeStartCaptured) {
                act.runtimeCapturedStartPos = act.useCurrentStartPos ? actor->position : act.startPos;
                act.runtimeStartCaptured = true;
            }
            float t = std::clamp((m_currentTime - act.startTime) / std::max(0.01f, act.duration), 0.0f, 1.0f);
            glm::vec3 newPos = glm::mix(act.runtimeCapturedStartPos, act.endPos, t);

            glm::vec3 moveDelta = act.endPos - act.runtimeCapturedStartPos;
            moveDelta.y = 0.0f;
            if (act.faceMovementDirection && glm::length(moveDelta) > 0.01f) {
                float targetYawDeg = glm::degrees(std::atan2(moveDelta.x, moveDelta.z));
                actor->rotation.y = targetYawDeg;
            }
            actor->position = newPos;

            if (!act.animState.empty()) {
                actor->currentAnimState = act.animState;
                actor->playAnimation = true;
            }
        } else if (m_currentTime > endTime && act.runtimeStartCaptured) {
            actor->position = act.endPos;
            if (!act.endAnimState.empty()) {
                actor->currentAnimState = act.endAnimState;
                actor->playAnimation = true;
            }
        }
    }

    // 2. Update Camera Shots (multi-shot cuts, smooth pans/dollies, look-at node tracking, FOV zoom)
    float currentFocusDist = m_activeSequence->dofFocusDistance;
    auto cam = scene.GetActiveCamera();
    if (cam && !m_activeSequence->cameraShots.empty()) {
        const CutsceneCameraShot* activeShot = nullptr;
        for (const auto& shot : m_activeSequence->cameraShots) {
            if (m_currentTime >= shot.startTime && m_currentTime <= shot.startTime + shot.duration) {
                activeShot = &shot;
            }
        }
        // Fallback to earliest or latest shot if between cuts
        if (!activeShot) {
            if (m_currentTime < m_activeSequence->cameraShots.front().startTime) {
                activeShot = &m_activeSequence->cameraShots.front();
            } else {
                activeShot = &m_activeSequence->cameraShots.back();
            }
        }

        if (activeShot) {
            float rawT = std::clamp((m_currentTime - activeShot->startTime) / std::max(0.01f, activeShot->duration), 0.0f, 1.0f);
            float t = activeShot->smoothBlend ? (rawT * rawT * (3.0f - 2.0f * rawT)) : rawT;

            glm::vec3 eye = glm::mix(activeShot->startPos, activeShot->endPos, t);
            glm::vec3 lookAt = glm::mix(activeShot->startLookAt, activeShot->endLookAt, t);
            if (!activeShot->lookAtNodeName.empty()) {
                if (auto targetNode = scene.GetNodeByName(activeShot->lookAtNodeName)) {
                    lookAt = targetNode->GetWorldPosition(scene) + activeShot->lookAtOffset;
                }
            }

            cam->cameraFov = glm::mix(activeShot->startFov, activeShot->endFov, t);
            OrientCameraToLookAt(cam, eye, lookAt, scene);
            currentFocusDist = glm::length(lookAt - eye);
        }
    }

    // 3. Update Subtitle & Audio Cues (supports silent mode or WAV / procedural synth audio)
    m_activeSubtitle = nullptr;
    for (auto& cue : m_activeSequence->subtitleCues) {
        if (m_currentTime >= cue.startTime && m_currentTime <= cue.startTime + cue.duration) {
            m_activeSubtitle = &cue;
            if (!cue.runtimeTriggered) {
                cue.runtimeTriggered = true;
                if (!m_activeSequence->silentAudio) {
                    auto& audio = Audio::AudioEngine::Get();
                    if (!cue.audioClipPath.empty()) {
                        audio.LoadWavFromFile(cue.audioClipPath, cue.audioClipPath);
                        uint32_t vid = audio.Play2D(cue.audioClipPath, "Voice", cue.audioVolume, 1.0f, false);
                        if (vid != 0) m_activeAudioVoices.push_back(vid);
                        audio.TriggerHardwarePlayback(cue.audioClipPath, "Voice", cue.audioVolume, cue.audioVolume, 1.0f, false);
                    } else if (cue.synthFrequencyHz > 20.0f) {
                        std::string clipKey = "CutsceneCue_" + std::to_string(static_cast<int>(cue.synthFrequencyHz));
                        if (!audio.GetClip(clipKey)) {
                            auto wavBytes = Audio::AudioEngine::GenerateSineWaveWav(cue.synthFrequencyHz, std::min(cue.duration, 0.35f));
                            audio.LoadWavFromMemory(clipKey, wavBytes.data(), wavBytes.size());
                        }
                        uint32_t vid = audio.Play2D(clipKey, "Voice", cue.audioVolume, 1.0f, false);
                        if (vid != 0) m_activeAudioVoices.push_back(vid);
                    }
                }
            }
        }
    }

    // 4. Apply live Camera & Post-Processing Effects (Sepia, B&W, Noir, Vignette, DOF, etc.)
    ApplyEnvironmentEffects(scene, currentFocusDist);

    // 5. Check if cutscene reached its end
    if (m_currentTime >= m_activeSequence->duration) {
        Stop(scene, true);
    }
}

void CutscenePlayer::ApplyFinalActorStates(Scene& scene) {
    if (!m_activeSequence) return;
    for (const auto& act : m_activeSequence->actorActions) {
        if (act.actorNodeName.empty()) continue;
        auto actor = scene.GetNodeByName(act.actorNodeName);
        if (!actor) continue;

        glm::vec3 startP = act.runtimeStartCaptured ? act.runtimeCapturedStartPos : (act.useCurrentStartPos ? actor->position : act.startPos);
        glm::vec3 moveDelta = act.endPos - startP;
        moveDelta.y = 0.0f;
        if (act.faceMovementDirection && glm::length(moveDelta) > 0.01f) {
            actor->rotation.y = glm::degrees(std::atan2(moveDelta.x, moveDelta.z));
        }
        actor->position = act.endPos;
        if (!act.endAnimState.empty()) {
            actor->currentAnimState = act.endAnimState;
            actor->playAnimation = true;
        }
    }
}

void CutscenePlayer::RestoreEnvironmentAndCamera(Scene& scene) {
    if (m_hasEnvironmentSnapshot) {
        scene.GetEnvironment() = m_savedEnvironment;
        m_hasEnvironmentSnapshot = false;
    }

    if (m_hasCameraSnapshot) {
        bool shouldRestoreCam = m_activeSequence ? m_activeSequence->restoreCameraOnEnd : true;
        if (m_createdTempCamera) {
            scene.DeleteNode(m_savedCameraNodeId);
            m_createdTempCamera = false;
        } else if (shouldRestoreCam) {
            if (auto cam = scene.GetNodeById(m_savedCameraNodeId)) {
                cam->position = m_savedCameraPos;
                cam->rotation = m_savedCameraRot;
                cam->cameraFov = m_savedCameraFov;
            }
        }
        m_hasCameraSnapshot = false;
    }

    for (const auto& saved : m_savedCharStates) {
        if (auto node = scene.GetNodeById(saved.nodeId)) {
            if (auto charNode = std::dynamic_pointer_cast<CharacterNode>(node)) {
                charNode->isPatrolling = saved.wasPatrolling;
            }
        }
    }
    m_savedCharStates.clear();

    auto& audio = Audio::AudioEngine::Get();
    for (uint32_t vid : m_activeAudioVoices) {
        audio.StopVoice(vid);
    }
    m_activeAudioVoices.clear();
    audio.StopHardwarePlayback();
}

bool CutscenePlayer::Skip(Scene& scene, bool force) {
    if (!m_isPlaying || !m_activeSequence) return false;
    if (!force && !m_activeSequence->skippable) return false;

    std::string finishedName = m_activeCutsceneName;
    ApplyFinalActorStates(scene);
    RestoreEnvironmentAndCamera(scene);

    m_isPlaying = false;
    m_activeSequence = nullptr;
    m_activeSubtitle = nullptr;
    m_activeCutsceneName.clear();

    for (const auto& n : scene.GetNodes()) {
        if (n) {
            n->EmitSignal("on_cutscene_skipped", {finishedName});
            n->EmitSignal("on_cutscene_finished", {finishedName});
        }
    }
    return true;
}

void CutscenePlayer::Stop(Scene& scene, bool applyFinalActorStates) {
    if (!m_isPlaying) return;

    std::string finishedName = m_activeCutsceneName;
    if (applyFinalActorStates) {
        ApplyFinalActorStates(scene);
    }
    RestoreEnvironmentAndCamera(scene);

    m_isPlaying = false;
    m_activeSequence = nullptr;
    m_activeSubtitle = nullptr;
    m_activeCutsceneName.clear();

    for (const auto& n : scene.GetNodes()) {
        if (n) n->EmitSignal("on_cutscene_finished", {finishedName});
    }
}

bool CutscenePlayer::CheckLocationTriggers(Scene& scene) {
    if (m_isPlaying) return false;

    for (auto& seq : scene.GetCutscenes()) {
        if (!seq.triggerOnLocation) continue;
        if (seq.triggerOnce && seq.runtimeHasTriggered) continue;

        float radiusSq = seq.triggerRadius * seq.triggerRadius;
        bool triggered = false;

        for (const auto& node : scene.GetNodes()) {
            if (!node || node->IsUINode()) continue;

            bool matchesActor = false;
            if (seq.triggerActorName.empty() || seq.triggerActorName == "*") {
                matchesActor = (node->nodeType == NodeType::Character) ||
                               (node->name == "Player" || node->name == "Character" || node->name == "Hero");
            } else {
                matchesActor = (node->name == seq.triggerActorName);
            }

            if (matchesActor) {
                glm::vec3 worldPos = node->GetWorldPosition(scene);
                glm::vec3 diff = worldPos - seq.triggerPosition;
                // Check horizontal XZ distance and reasonable Y proximity
                float distSq = glm::dot(diff, diff);
                if (distSq <= radiusSq) {
                    triggered = true;
                    break;
                }
            }
        }

        // Also allow the active player camera to trigger if triggerActorName is "*" or "MainCamera" / "Player"
        if (!triggered && (seq.triggerActorName == "*" || seq.triggerActorName == "Player" || seq.triggerActorName == "MainCamera")) {
            if (auto cam = scene.GetActiveCamera()) {
                glm::vec3 camPos = cam->GetWorldPosition(scene);
                glm::vec3 diff = camPos - seq.triggerPosition;
                if (glm::dot(diff, diff) <= radiusSq) {
                    triggered = true;
                }
            }
        }

        if (triggered) {
            return StartSequence(scene, seq);
        }
    }
    return false;
}

void CutscenePlayer::ResetTriggers(Scene& scene) {
    for (auto& seq : scene.GetCutscenes()) {
        seq.runtimeHasTriggered = false;
        for (auto& act : seq.actorActions) act.runtimeStartCaptured = false;
        for (auto& cue : seq.subtitleCues) cue.runtimeTriggered = false;
    }
}

float CutscenePlayer::GetLetterboxBarRatio() const {
    if (!m_isPlaying || !m_activeSequence) return 0.0f;
    float target = std::clamp(m_activeSequence->letterboxAmount, 0.0f, 0.35f);
    // Smooth 0.35s fade-in and fade-out of letterbox bars
    const float fadeTime = 0.35f;
    float fadeIn = std::clamp(m_currentTime / fadeTime, 0.0f, 1.0f);
    float fadeOut = std::clamp((m_activeSequence->duration - m_currentTime) / fadeTime, 0.0f, 1.0f);
    float factor = std::min(fadeIn, fadeOut);
    return target * factor;
}

} // namespace Animation

