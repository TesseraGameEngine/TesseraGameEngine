#include "Localization.h"
#include "Utils/SimpleJson.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

Localization& Localization::Get() {
    static Localization instance;
    return instance;
}

Localization::Localization() {
}

void Localization::Init(const std::string& customLangDir) {
    if (m_initialized) return;

    m_langDir = FindLangDirectory(customLangDir);

    // Populate embedded baseline fallbacks
    LoadEmbeddedFallbacks();

    // Ensure baseline lang files exist on disk in the discovered lang directory
    EnsureDefaultFiles();

    // Scan the lang/ directory for all language files
    ScanLanguages();

    // Load user's preferred language from editor_settings.json
    LoadSettings();

    m_initialized = true;
}

std::string Localization::FindLangDirectory(const std::string& preferred) const {
    if (!preferred.empty() && fs::exists(preferred)) {
        return preferred;
    }

    std::vector<std::string> candidates = {
        "lang",
        "../lang",
        "../../lang",
        "assets/lang"
    };

    for (const auto& c : candidates) {
        if (fs::exists(c) && fs::is_directory(c)) {
            return c;
        }
    }

    return "lang";
}

void Localization::EnsureDefaultFiles() {
    std::error_code ec;
    if (!fs::exists(m_langDir)) {
        fs::create_directories(m_langDir, ec);
    }

    std::string enPath = (fs::path(m_langDir) / "en.json").string();
    if (!fs::exists(enPath)) {
        JsonValue root = JsonValue::Object();
        root["code"] = "en";
        root["name"] = "English";
        JsonValue stringsObj = JsonValue::Object();
        for (const auto& [k, v] : m_fallbackTranslations) {
            stringsObj[k] = v;
        }
        root["strings"] = stringsObj;

        std::ofstream file(enPath);
        if (file.is_open()) {
            file << root.Serialize(2);
        }
    }

    std::string esPath = (fs::path(m_langDir) / "es.json").string();
    if (!fs::exists(esPath)) {
        std::unordered_map<std::string, std::string> esStrings;
        esStrings["+ Add Morph Target"] = "+ Añadir objetivo de deformación";
        esStrings["+ Add Node..."] = "+ Añadir nodo...";
        esStrings["+ Create & Attach Free-Fly Camera Script"] = "+ Crear y adjuntar script de cámara libre";
        esStrings["+ Create & Launch Project"] = "+ Crear y lanzar proyecto";
        esStrings["+ Initialize Default Facial & Body Morph Targets"] = "+ Inicializar objetivos de deformación facial y corporal";
        esStrings["1-Click Screen Form-Factor Presets:"] = "Ajustes preestablecidos de factor de forma de pantalla (1 clic):";
        esStrings["2D UI / Cutaway Video Player"] = "IU 2D / Reproductor de video";
        esStrings["2D UI Component Properties"] = "Propiedades de componente IU 2D";
        esStrings["2D User Interface"] = "Interfaz de usuario 2D";
        esStrings["3D Model Asset (.fbx, .gltf, .glb, .obj):"] = "Recurso de modelo 3D (.fbx, .gltf, .glb, .obj):";
        esStrings["3D Objects & Primitives"] = "Objetos y primitivas 3D";
        esStrings["3D Ruler & Measurement Tool"] = "Regla 3D y herramienta de medición";
        esStrings["3D Standard (PBR + Lighting + Camera)"] = "Estándar 3D (PBR + Iluminación + Cámara)";
        esStrings["3D Viewport"] = "Visor 3D";
        esStrings["AO Strength"] = "Intensidad de OA";
        esStrings["About Tessera Game Engine"] = "Acerca del motor de juegos Tessera";
        esStrings["About Tessera Game Engine..."] = "Acerca del motor de juegos Tessera...";
        esStrings["Add 2D UI Component"] = "Añadir componente IU 2D";
        esStrings["Add Child"] = "Añadir hijo";
        esStrings["Add Collider"] = "Añadir colisionador";
        esStrings["Add Entity"] = "Añadir entidad";
        esStrings["Add In-Game Video Screen..."] = "Añadir pantalla de video en el juego...";
        esStrings["Add Node / Entity..."] = "Añadir nodo / entidad...";
        esStrings["Add Primitive"] = "Añadir primitiva";
        esStrings["Advanced Material:"] = "Material avanzado:";
        esStrings["Alpha Cutoff"] = "Corte alfa";
        esStrings["Anchor Preset:"] = "Ajuste preestablecido de anclaje:";
        esStrings["Angular Velocity"] = "Velocidad angular";
        esStrings["Animation Speed"] = "Velocidad de animación";
        esStrings["Animation State Machine Graph"] = "Grafo de máquina de estados de animación";
        esStrings["Animation Timeline"] = "Línea de tiempo de animación";
        esStrings["Apply Upward Ragdoll Impulse Test"] = "Aplicar prueba de impulso de muñeco de trapo hacia arriba";
        esStrings["Area Width / Height (m):"] = "Ancho / Alto de área (m):";
        esStrings["Armature & Animation Tree"] = "Armazón y árbol de animación";
        esStrings["Armature / Skeleton Enabled"] = "Armazón / Esqueleto habilitado";
        esStrings["Attack"] = "Atacar";
        esStrings["Audio Mixer & Reverb Zones"] = "Mezclador de audio y zonas de reverberación";
        esStrings["Audio Source (2D / 3D)"] = "Fuente de audio (2D / 3D)";
        esStrings["Bake Static Lighting"] = "Hornear iluminación estática";
        esStrings["Base Color / Tint"] = "Color base / Tinte";
        esStrings["Basic & Text"] = "Básico y texto";
        esStrings["Blank Scene (Empty Slate)"] = "Escena en blanco (Lienzo vacío)";
        esStrings["Bottom"] = "Inferior";
        esStrings["Bounciness"] = "Rebote";
        esStrings["Box Collider"] = "Colisionador de caja";
        esStrings["Box Size"] = "Tamaño de caja";
        esStrings["Browse .ttf"] = "Examinar .ttf";
        esStrings["Browse Existing Project File (*.tess)..."] = "Examinar archivo de proyecto existente (*.tess)...";
        esStrings["Browse..."] = "Examinar...";
        esStrings["Buoyancy"] = "Flotabilidad";
        esStrings["Button"] = "Botón";
        esStrings["CCTV Monitor"] = "Monitor CCTV";
        esStrings["Camera"] = "Cámara";
        esStrings["Camera Lens & Frustum"] = "Lente y frustum de cámara";
        esStrings["Camera Pivot & Orbit Center"] = "Pivote y centro de órbita de cámara";
        esStrings["Camera Preview"] = "Vista previa de cámara";
        esStrings["Cancel"] = "Cancelar";
        esStrings["Canvas (UI Root)"] = "Lienzo (Raíz de IU)";
        esStrings["Canvas Offset (X, Y):"] = "Desplazamiento de lienzo (X, Y):";
        esStrings["Capsule Collider"] = "Colisionador de cápsula";
        esStrings["Capsule Height"] = "Altura de cápsula";
        esStrings["Capsule Radius"] = "Radio de cápsula";
        esStrings["Cast Realtime Shadows"] = "Proyectar sombras en tiempo real";
        esStrings["Cast Shadows"] = "Proyectar sombras";
        esStrings["Center Offset"] = "Desplazamiento del centro";
        esStrings["Checkbox"] = "Casilla de verificación";
        esStrings["Cinema (2.39:1)"] = "Cine (2.39:1)";
        esStrings["Cinema Screen (2.39:1 Feature Film Reel)"] = "Pantalla de cine (2.39:1 Cinta de largometraje)";
        esStrings["Circle"] = "Círculo";
        esStrings["Clear All"] = "Limpiar todo";
        esStrings["Clear Coat"] = "Capa transparente";
        esStrings["Clear Coat Roughness"] = "Rugosidad de capa transparente";
        esStrings["Click any node in Scene Hierarchy or Viewport to inspect properties and transforms."] = "Haga clic en cualquier nodo de la Jerarquía de la escena o del Visor para inspeccionar propiedades y transformaciones.";
        esStrings["Close"] = "Cerrar";
        esStrings["Close All Secondary Windows"] = "Cerrar todas las ventanas secundarias";
        esStrings["Collider Properties"] = "Propiedades del colisionador";
        esStrings["Collider Shape"] = "Forma del colisionador";
        esStrings["Color"] = "Color";
        esStrings["Color Picker"] = "Selector de color";
        esStrings["Colour Depth (cm)"] = "Profundidad de color (cm)";
        esStrings["Cone"] = "Cono";
        esStrings["Console Log"] = "Registro de consola";
        esStrings["Containers & Windows"] = "Contenedores y ventanas";
        esStrings["Continue Current Project >>"] = "Continuar con el proyecto actual >>";
        esStrings["Copy"] = "Copiar";
        esStrings["Copy Selected Node"] = "Copiar nodo seleccionado";
        esStrings["Create"] = "Crear";
        esStrings["Create Lua Script"] = "Crear script Lua";
        esStrings["Create New Folder"] = "Crear nueva carpeta";
        esStrings["Create New Project"] = "Crear Nuevo Proyecto";
        esStrings["Create New Scene"] = "Crear nueva escena";
        esStrings["Create Project"] = "Crear proyecto";
        esStrings["Create Scene"] = "Crear escena";
        esStrings["Create a new project on the right, or browse your disk for an existing .tess project file."] = "Cree un nuevo proyecto a la derecha o examine su disco en busca de un archivo .tess existente.";
        esStrings["Cube"] = "Cubo";
        esStrings["Custom TrueType Font (.ttf):"] = "Fuente TrueType personalizada (.ttf):";
        esStrings["Cutscene & Cinematic Sequencer"] = "Secuenciador cinemático";
        esStrings["Cutscene & Cinematic Sequencer..."] = "Secuenciador cinemático...";
        esStrings["Cylinder"] = "Cilindro";
        esStrings["Data & Media"] = "Datos y multimedia";
        esStrings["Date/Time Picker"] = "Selector de fecha y hora";
        esStrings["Decal Projector"] = "Proyector de calcomanía";
        esStrings["Delete"] = "Eliminar";
        esStrings["Delete Node"] = "Eliminar nodo";
        esStrings["Delete Selected"] = "Eliminar seleccionado";
        esStrings["Delete Selected Node"] = "Eliminar nodo seleccionado";
        esStrings["Delete Selected [Del]"] = "Eliminar seleccionado [Supr]";
        esStrings["Desktop"] = "Escritorio";
        esStrings["Destination Directory:"] = "Directorio de destino:";
        esStrings["Directional Light"] = "Luz direccional";
        esStrings["Directory Location:"] = "Ubicación del directorio:";
        esStrings["Double-Sided"] = "Doble cara";
        esStrings["Dropdown Box"] = "Cuadro desplegable";
        esStrings["Duplicate"] = "Duplicar";
        esStrings["Duplicate Selected Node"] = "Duplicar nodo seleccionado";
        esStrings["Edit"] = "Editar";
        esStrings["Emissive Color"] = "Color emisivo";
        esStrings["Emissive Intensity (HDR)"] = "Intensidad emisiva (HDR)";
        esStrings["Empty Group Node"] = "Nodo de grupo vacío";
        esStrings["Enable ACES Filmic Tonemapping"] = "Habilitar mapeo tonal fílmico ACES";
        esStrings["Enable Declarative Sync"] = "Habilitar sincronización declarativa";
        esStrings["Enable Distance-Based LOD"] = "Habilitar LOD basado en distancia";
        esStrings["Enable Hardware Ray Tracing (Vulkan / Metal)"] = "Habilitar trazado de rayos por hardware (Vulkan / Metal)";
        esStrings["Enable Humanoid Ragdoll Physics"] = "Habilitar física de muñeco de trapo humanoide";
        esStrings["Enable Meshlet GPU Virtualization"] = "Habilitar virtualización GPU Meshlet";
        esStrings["Enable Realtime Shadows & CSM"] = "Habilitar sombras en tiempo real y CSM";
        esStrings["Enable Spherical Harmonics Sky GI"] = "Habilitar iluminación global celeste por armónicos esféricos";
        esStrings["Enable Temporal Upscaling (FSR 3 / DLSS)"] = "Habilitar escalado temporal (FSR 3 / DLSS)";
        esStrings["Enable Two-Bone IK Solver"] = "Habilitar solucionador IK de dos huesos";
        esStrings["Enable Voxel Global Illumination (VXGI)"] = "Habilitar iluminación global por vóxeles (VXGI)";
        esStrings["Enabled"] = "Habilitado";
        esStrings["Enter folder name:"] = "Ingrese el nombre de la carpeta:";
        esStrings["Enter name for the new scene (.scene):"] = "Ingrese el nombre para la nueva escena (.scene):";
        esStrings["Enter new name for current scene:"] = "Ingrese el nuevo nombre para la escena actual:";
        esStrings["Enter script filename (with .lua):"] = "Ingrese el nombre del script (con .lua):";
        esStrings["Enter the path to a 3D model file (.obj, .fbx, .gltf, .glb):"] = "Ingrese la ruta a un archivo de modelo 3D (.obj, .fbx, .gltf, .glb):";
        esStrings["Entities & Lighting"] = "Entidades e iluminación";
        esStrings["Environment, GI & Post-Processing..."] = "Entorno, IG y posprocesamiento...";
        esStrings["Export Standalone Game..."] = "Exportar juego independiente...";
        esStrings["Far Clip Plane:"] = "Plano de recorte lejano:";
        esStrings["Field of View:"] = "Campo de visión:";
        esStrings["File"] = "Archivo";
        esStrings["Fill"] = "Llenado";
        esStrings["Floating Window"] = "Ventana flotante";
        esStrings["Floats on Water"] = "Flota en el agua";
        esStrings["Fluid Volume"] = "Volumen de fluido";
        esStrings["Focus Pivot on Selected Node [F]"] = "Enfocar pivote en el nodo seleccionado [F]";
        esStrings["Focus [F]"] = "Enfocar [F]";
        esStrings["Font Size (px):"] = "Tamaño de fuente (px):";
        esStrings["Friction"] = "Fricción";
        esStrings["Front"] = "Frontal";
        esStrings["GPU Profiler"] = "Generador de perfiles de GPU";
        esStrings["Game Active [Play]"] = "Juego activo [Reproducir]";
        esStrings["Game Profiler & Resource Monitor"] = "Generador de perfiles del juego y monitor de recursos";
        esStrings["Game [Standalone Window]"] = "Juego [Ventana independiente]";
        esStrings["Gizmo"] = "Manipulador";
        esStrings["Glass"] = "Vidrio";
        esStrings["Grid: OFF"] = "Cuadrícula: NO";
        esStrings["Grid: ON"] = "Cuadrícula: SÍ";
        esStrings["Help"] = "Ayuda";
        esStrings["Idle"] = "Reposo";
        esStrings["Image"] = "Imagen";
        esStrings["Import 3D Model"] = "Importar modelo 3D";
        esStrings["Import 3D Model..."] = "Importar modelo 3D...";
        esStrings["Import 3D..."] = "Importar 3D...";
        esStrings["Import Model"] = "Importar modelo";
        esStrings["In-Game Video Screen (TV / Cinema / Phone / Monitor)"] = "Pantalla de video en el juego (TV / Cine / Teléfono / Monitor)";
        esStrings["Inner Cone Angle:"] = "Ángulo de cono interior:";
        esStrings["Inputs & Controls"] = "Entradas y controles";
        esStrings["Inspector"] = "Inspector";
        esStrings["Instantiate Subscene / Prefab (.scene)..."] = "Instanciar subescena / prefab (.scene)...";
        esStrings["Intensity:"] = "Intensidad:";
        esStrings["Inverse Kinematics (IK) & Morph Targets"] = "Cinemática inversa (IK) y objetivos de deformación";
        esStrings["Is Trigger (Overlap Only)"] = "Es disparador (solo superposición)";
        esStrings["Iso"] = "Isométrica";
        esStrings["Joint / Constraint"] = "Articulación / Restricción";
        esStrings["Language"] = "Idioma";
        esStrings["Language:"] = "Idioma:";
        esStrings["Layout Options"] = "Opciones de diseño";
        esStrings["Level of Detail (LOD) & Mesh Decimation"] = "Nivel de detalle (LOD) y decimación de malla";
        esStrings["Light Color:"] = "Color de luz:";
        esStrings["Light Properties"] = "Propiedades de luz";
        esStrings["Light Type:"] = "Tipo de luz:";
        esStrings["Lightmap Static"] = "Estático para mapa de luz";
        esStrings["Linear Velocity"] = "Velocidad lineal";
        esStrings["Liquid Fill"] = "Llenado de líquido";
        esStrings["Local"] = "Local";
        esStrings["Local Coordinates"] = "Coordenadas locales";
        esStrings["Local Position:"] = "Posición local:";
        esStrings["Local Rotation (Degrees):"] = "Rotación local (Grados):";
        esStrings["Local Scale:"] = "Escala local:";
        esStrings["Mass (kg)"] = "Masa (kg)";
        esStrings["Material & Appearance (PBR)"] = "Material y apariencia (PBR)";
        esStrings["Material Model"] = "Modelo de material";
        esStrings["Menu Bar"] = "Barra de menús";
        esStrings["Metallic (Default)"] = "Metálico (Predeterminado)";
        esStrings["Mirror"] = "Espejo";
        esStrings["Multiplayer & Authority"] = "Multijugador y autoridad";
        esStrings["Multiplayer & Networking"] = "Multijugador y redes";
        esStrings["Navigation & NavMesh Baker"] = "Navegación y horneador de NavMesh";
        esStrings["Near Clip Plane:"] = "Plano de recorte cercano:";
        esStrings["Nested Scene Instance (Prefab)"] = "Instancia de escena anidada (Prefab)";
        esStrings["New Project Name:"] = "Nombre del nuevo proyecto:";
        esStrings["New Project..."] = "Nuevo proyecto...";
        esStrings["New Scene..."] = "Nueva escena...";
        esStrings["No node selected."] = "Ningún nodo seleccionado.";
        esStrings["No other scenes in project"] = "No hay otras escenas en el proyecto";
        esStrings["No recent projects"] = "Sin proyectos recientes";
        esStrings["No recent projects found."] = "No se encontraron proyectos recientes.";
        esStrings["Node Name:"] = "Nombre del nodo:";
        esStrings["Ocean"] = "Océano";
        esStrings["Open"] = "Abrir";
        esStrings["Open HTML Documentation..."] = "Abrir documentación HTML...";
        esStrings["Open Multiplayer & Networking Panel >>"] = "Abrir panel de multijugador y redes >>";
        esStrings["Open Project..."] = "Abrir proyecto...";
        esStrings["Open Scene"] = "Abrir escena";
        esStrings["Open Visual Shader Graph Editor >>"] = "Abrir editor visual de grafos de sombreado >>";
        esStrings["Orbit Pivot Point:"] = "Punto de pivote de órbita:";
        esStrings["Outer Cone Angle:"] = "Ángulo de cono exterior:";
        esStrings["Panel"] = "Panel";
        esStrings["Particle Emitter (CPU / Niagara)"] = "Emisor de partículas (CPU / Niagara)";
        esStrings["Password Field"] = "Campo de contraseña";
        esStrings["Paste"] = "Pegar";
        esStrings["Paste Node"] = "Pegar nodo";
        esStrings["Path:"] = "Ruta:";
        esStrings["Pause [||]"] = "Pausar [||]";
        esStrings["Physics & Colliders"] = "Física y colisionadores";
        esStrings["Physics & Dynamics (JoltPhysics)"] = "Física y dinámica (JoltPhysics)";
        esStrings["Plane"] = "Plano";
        esStrings["Play Animation"] = "Reproducir animación";
        esStrings["Play Game [Play]"] = "Jugar [Reproducir]";
        esStrings["Point Light"] = "Luz puntual";
        esStrings["Pour Spout"] = "Caño vertedor";
        esStrings["Preview"] = "Vista previa";
        esStrings["Progress Bar"] = "Barra de progreso";
        esStrings["Project"] = "Proyecto";
        esStrings["Project Files"] = "Archivos del proyecto";
        esStrings["Project Files (File Viewer)"] = "Archivos del proyecto (Visor de archivos)";
        esStrings["Project Hub"] = "Centro de proyectos";
        esStrings["Project Hub & Workspace Manager"] = "Centro de proyectos y administrador de espacio de trabajo";
        esStrings["Project Input & Key Bindings..."] = "Entrada y asignaciones de teclas del proyecto...";
        esStrings["Project Launcher / Hub..."] = "Lanzador de proyectos / Centro...";
        esStrings["Project Location (Parent Directory):"] = "Ubicación del proyecto (Directorio padre):";
        esStrings["Project Name:"] = "Nombre del proyecto:";
        esStrings["Project Scripts:"] = "Scripts del proyecto:";
        esStrings["Project Template:"] = "Plantilla de proyecto:";
        esStrings["Project name cannot be empty."] = "El nombre del proyecto no puede estar vacío.";
        esStrings["Quick Switch:"] = "Cambio rápido:";
        esStrings["Radio Button"] = "Botón de opción";
        esStrings["Range / Falloff (m):"] = "Alcance / Atenuación (m):";
        esStrings["Recapture Reflection Probes"] = "Recapturar sondas de reflexión";
        esStrings["Recent Projects"] = "Proyectos Recientes";
        esStrings["Rectangle"] = "Rectángulo";
        esStrings["Redo"] = "Rehacer";
        esStrings["Redo (Ctrl+Y)"] = "Rehacer (Ctrl+Y)";
        esStrings["Refraction (IOR)"] = "Refracción (IOR)";
        esStrings["Reload Languages"] = "Recargar idiomas";
        esStrings["Reload Prefab From Disk"] = "Recargar prefab desde el disco";
        esStrings["Render"] = "Renderizado";
        esStrings["Rendering, GI & Post-Processing"] = "Renderizado, IG y posprocesamiento";
        esStrings["Replicate Transform Over Network"] = "Replicar transformación por red";
        esStrings["Reset All Morph Weights"] = "Restablecer todos los pesos de deformación";
        esStrings["Reset Cam"] = "Restablecer cámara";
        esStrings["Reset Pivot to Origin (0,0,0)"] = "Restablecer pivote al origen (0,0,0)";
        esStrings["Reset Scene to Default"] = "Restablecer escena a predeterminada";
        esStrings["Reset Transform"] = "Restablecer transformación";
        esStrings["Reset View"] = "Restablecer vista";
        esStrings["Reset to Default Window Layout"] = "Restablecer diseño de ventanas predeterminado";
        esStrings["Restitution (Bounciness)"] = "Restitución (Rebote)";
        esStrings["RigidBody Mode"] = "Modo de cuerpo rígido";
        esStrings["Rotate"] = "Rotar";
        esStrings["Rotate [E]"] = "Rotar [E]";
        esStrings["Roughness (Default)"] = "Rugosidad (Predeterminada)";
        esStrings["Rounded Rectangle"] = "Rectángulo redondeado";
        esStrings["Ruler: OFF [M]"] = "Regla: NO [M]";
        esStrings["Ruler: ON [M]"] = "Regla: SÍ [M]";
        esStrings["Run"] = "Correr";
        esStrings["Save Copy"] = "Guardar copia";
        esStrings["Save Project"] = "Guardar proyecto";
        esStrings["Save Project As"] = "Guardar proyecto como";
        esStrings["Save Project As..."] = "Guardar proyecto como...";
        esStrings["Save Scene"] = "Guardar escena";
        esStrings["Save Scene As"] = "Guardar escena como";
        esStrings["Save Scene As..."] = "Guardar escena como...";
        esStrings["Save Scene Copy"] = "Guardar copia de escena";
        esStrings["Save copy of project to a new location:"] = "Guardar copia del proyecto en una nueva ubicación:";
        esStrings["Scale"] = "Escalar";
        esStrings["Scale [R]"] = "Escalar [R]";
        esStrings["Scene"] = "Escena";
        esStrings["Scene Hierarchy"] = "Jerarquía de Escena";
        esStrings["Screen Glow (HDR Emissive)"] = "Brillo de pantalla (Emisivo HDR)";
        esStrings["Script Component"] = "Componente de script";
        esStrings["Script Editor"] = "Editor de scripts";
        esStrings["Scroll Pane"] = "Panel de desplazamiento";
        esStrings["Security CCTV Monitor"] = "Monitor de seguridad CCTV";
        esStrings["Select Location for New Project"] = "Seleccionar ubicación para el nuevo proyecto";
        esStrings["Selectable List"] = "Lista seleccionable";
        esStrings["Set as Active Preview Camera"] = "Establecer como cámara de vista previa activa";
        esStrings["Shadow Bias:"] = "Sesgo de sombra:";
        esStrings["Shape Type:"] = "Tipo de forma:";
        esStrings["Show 3D Pivot Marker"] = "Mostrar marcador de pivote 3D";
        esStrings["Show All Workspace Panels"] = "Mostrar todos los paneles del espacio de trabajo";
        esStrings["Show Ground Grid"] = "Mostrar cuadrícula del suelo";
        esStrings["Side"] = "Lateral";
        esStrings["Size (W, H):"] = "Tamaño (An, Al):";
        esStrings["Slider"] = "Control deslizante";
        esStrings["Slosh"] = "Chapoteo";
        esStrings["Smartphone (9:16)"] = "Smartphone (9:16)";
        esStrings["Smartphone Screen (9:16 Incoming Call)"] = "Pantalla de smartphone (9:16 Llamada entrante)";
        esStrings["Snap"] = "Ajuste";
        esStrings["Solid"] = "Sólido";
        esStrings["Specialized Tools"] = "Herramientas especializadas";
        esStrings["Specify Project Name and Destination Directory:"] = "Especifique el nombre del proyecto y el directorio de destino:";
        esStrings["Sphere"] = "Esfera";
        esStrings["Sphere Collider"] = "Colisionador esférico";
        esStrings["Sphere Radius"] = "Radio de esfera";
        esStrings["Spinner"] = "Indicador de carga";
        esStrings["Spline / 3D Curve"] = "Spline / Curva 3D";
        esStrings["Spot Light"] = "Luz focal";
        esStrings["Standalone Game View"] = "Vista de juego independiente";
        esStrings["Start Offset (s)"] = "Desplazamiento inicial (s)";
        esStrings["Stop [X]"] = "Detener [X]";
        esStrings["Surface Colour"] = "Color de superficie";
        esStrings["TV Set (16:9 Live News Broadcast)"] = "Televisor (16:9 Emisión de noticias en vivo)";
        esStrings["TV Set (16:9)"] = "Televisor (16:9)";
        esStrings["Table Grid"] = "Cuadrícula de tabla";
        esStrings["Tabs View"] = "Vista de pestañas";
        esStrings["Terrain (Landscape)"] = "Terreno (Paisaje)";
        esStrings["Test Corner PiP Transmission Now"] = "Probar transmisión PiP en esquina ahora";
        esStrings["Test Full-Screen Video Cutaway Now"] = "Probar corte de video en pantalla completa ahora";
        esStrings["Text Field"] = "Campo de texto";
        esStrings["Text Label"] = "Etiqueta de texto";
        esStrings["Third-Person Character Sandbox"] = "Caja de arena de personaje en tercera persona";
        esStrings["Tip: You can switch or create projects at any time from File -> Project Launcher / Hub..."] = "Consejo: Puede cambiar o crear proyectos en cualquier momento desde Archivo -> Lanzador de proyectos / Centro...";
        esStrings["Top"] = "Superior";
        esStrings["Transform"] = "Transformación";
        esStrings["Translate"] = "Trasladar";
        esStrings["Translate [W]"] = "Trasladar [W]";
        esStrings["Triangle"] = "Triángulo";
        esStrings["Triplanar Mapping"] = "Mapeo triplanar";
        esStrings["Two-Sided Emission"] = "Emisión de doble cara";
        esStrings["Undo"] = "Deshacer";
        esStrings["Undo (Ctrl+Z)"] = "Deshacer (Ctrl+Z)";
        esStrings["Unparent"] = "Desvincular de padre";
        esStrings["Unparent (Move to Root)"] = "Desvincular (Mover a la raíz)";
        esStrings["Use Gravity"] = "Usar gravedad";
        esStrings["Vector Shapes"] = "Formas vectoriales";
        esStrings["Video Channel"] = "Canal de video";
        esStrings["Video Player"] = "Reproductor de video";
        esStrings["Visible"] = "Visible";
        esStrings["Visual Shader Graph Editor"] = "Editor visual de grafos de sombreado";
        esStrings["Visual State Machine Graph >>"] = "Grafo de máquina de estados visual >>";
        esStrings["Walk"] = "Caminar";
        esStrings["Water"] = "Agua";
        esStrings["Window"] = "Ventana";
        esStrings["Wire+Shaded"] = "Alambre+Sombreado";
        esStrings["Wireframe"] = "Estructura alámbrica";
        esStrings["Workspace Panels"] = "Paneles del espacio de trabajo";
        esStrings["World"] = "Mundo";
        esStrings["World Coordinates"] = "Coordenadas globales";
        esStrings["World Position"] = "Posición global";
        esStrings["Zoom +"] = "Acercar +";
        esStrings["Zoom -"] = "Alejar -";
        esStrings["[..] Up"] = "[..] Subir";
        esStrings["[Missing]"] = "[No encontrado]";
        esStrings["from Scene [Del]"] = "de la escena [Supr]";
        esStrings["nodes"] = "nodos";
        esStrings["presets"] = "preajustes";
        esStrings["| Editor Mode"] = "| Modo editor";
        esStrings["| GAME PAUSED"] = "| JUEGO PAUSADO";
        esStrings["| GAME RUNNING in Standalone Window"] = "| JUEGO EN EJECUCIÓN en ventana independiente";

        JsonValue root = JsonValue::Object();
        root["code"] = "es";
        root["name"] = "Español";
        JsonValue stringsObj = JsonValue::Object();
        for (const auto& [k, v] : esStrings) {
            stringsObj[k] = v;
        }
        root["strings"] = stringsObj;

        std::ofstream file(esPath);
        if (file.is_open()) {
            file << root.Serialize(2);
        }
    }
}

void Localization::LoadEmbeddedFallbacks() {
    m_fallbackTranslations["+ Add Morph Target"] = "+ Add Morph Target";
    m_fallbackTranslations["+ Add Node..."] = "+ Add Node...";
    m_fallbackTranslations["+ Create & Attach Free-Fly Camera Script"] = "+ Create & Attach Free-Fly Camera Script";
    m_fallbackTranslations["+ Create & Launch Project"] = "+ Create & Launch Project";
    m_fallbackTranslations["+ Initialize Default Facial & Body Morph Targets"] = "+ Initialize Default Facial & Body Morph Targets";
    m_fallbackTranslations["1-Click Screen Form-Factor Presets:"] = "1-Click Screen Form-Factor Presets:";
    m_fallbackTranslations["2D UI / Cutaway Video Player"] = "2D UI / Cutaway Video Player";
    m_fallbackTranslations["2D UI Component Properties"] = "2D UI Component Properties";
    m_fallbackTranslations["2D User Interface"] = "2D User Interface";
    m_fallbackTranslations["3D Model Asset (.fbx, .gltf, .glb, .obj):"] = "3D Model Asset (.fbx, .gltf, .glb, .obj):";
    m_fallbackTranslations["3D Objects & Primitives"] = "3D Objects & Primitives";
    m_fallbackTranslations["3D Ruler & Measurement Tool"] = "3D Ruler & Measurement Tool";
    m_fallbackTranslations["3D Standard (PBR + Lighting + Camera)"] = "3D Standard (PBR + Lighting + Camera)";
    m_fallbackTranslations["3D Viewport"] = "3D Viewport";
    m_fallbackTranslations["AO Strength"] = "AO Strength";
    m_fallbackTranslations["About Tessera Game Engine"] = "About Tessera Game Engine";
    m_fallbackTranslations["About Tessera Game Engine..."] = "About Tessera Game Engine...";
    m_fallbackTranslations["Add 2D UI Component"] = "Add 2D UI Component";
    m_fallbackTranslations["Add Child"] = "Add Child";
    m_fallbackTranslations["Add Collider"] = "Add Collider";
    m_fallbackTranslations["Add Entity"] = "Add Entity";
    m_fallbackTranslations["Add In-Game Video Screen..."] = "Add In-Game Video Screen...";
    m_fallbackTranslations["Add Node / Entity..."] = "Add Node / Entity...";
    m_fallbackTranslations["Add Primitive"] = "Add Primitive";
    m_fallbackTranslations["Advanced Material:"] = "Advanced Material:";
    m_fallbackTranslations["Alpha Cutoff"] = "Alpha Cutoff";
    m_fallbackTranslations["Anchor Preset:"] = "Anchor Preset:";
    m_fallbackTranslations["Angular Velocity"] = "Angular Velocity";
    m_fallbackTranslations["Animation Speed"] = "Animation Speed";
    m_fallbackTranslations["Animation State Machine Graph"] = "Animation State Machine Graph";
    m_fallbackTranslations["Animation Timeline"] = "Animation Timeline";
    m_fallbackTranslations["Apply Upward Ragdoll Impulse Test"] = "Apply Upward Ragdoll Impulse Test";
    m_fallbackTranslations["Area Width / Height (m):"] = "Area Width / Height (m):";
    m_fallbackTranslations["Armature & Animation Tree"] = "Armature & Animation Tree";
    m_fallbackTranslations["Armature / Skeleton Enabled"] = "Armature / Skeleton Enabled";
    m_fallbackTranslations["Attack"] = "Attack";
    m_fallbackTranslations["Audio Mixer & Reverb Zones"] = "Audio Mixer & Reverb Zones";
    m_fallbackTranslations["Audio Source (2D / 3D)"] = "Audio Source (2D / 3D)";
    m_fallbackTranslations["Bake Static Lighting"] = "Bake Static Lighting";
    m_fallbackTranslations["Base Color / Tint"] = "Base Color / Tint";
    m_fallbackTranslations["Basic & Text"] = "Basic & Text";
    m_fallbackTranslations["Blank Scene (Empty Slate)"] = "Blank Scene (Empty Slate)";
    m_fallbackTranslations["Bottom"] = "Bottom";
    m_fallbackTranslations["Bounciness"] = "Bounciness";
    m_fallbackTranslations["Box Collider"] = "Box Collider";
    m_fallbackTranslations["Box Size"] = "Box Size";
    m_fallbackTranslations["Browse .ttf"] = "Browse .ttf";
    m_fallbackTranslations["Browse Existing Project File (*.tess)..."] = "Browse Existing Project File (*.tess)...";
    m_fallbackTranslations["Browse..."] = "Browse...";
    m_fallbackTranslations["Buoyancy"] = "Buoyancy";
    m_fallbackTranslations["Button"] = "Button";
    m_fallbackTranslations["CCTV Monitor"] = "CCTV Monitor";
    m_fallbackTranslations["Camera"] = "Camera";
    m_fallbackTranslations["Camera Lens & Frustum"] = "Camera Lens & Frustum";
    m_fallbackTranslations["Camera Pivot & Orbit Center"] = "Camera Pivot & Orbit Center";
    m_fallbackTranslations["Camera Preview"] = "Camera Preview";
    m_fallbackTranslations["Cancel"] = "Cancel";
    m_fallbackTranslations["Canvas (UI Root)"] = "Canvas (UI Root)";
    m_fallbackTranslations["Canvas Offset (X, Y):"] = "Canvas Offset (X, Y):";
    m_fallbackTranslations["Capsule Collider"] = "Capsule Collider";
    m_fallbackTranslations["Capsule Height"] = "Capsule Height";
    m_fallbackTranslations["Capsule Radius"] = "Capsule Radius";
    m_fallbackTranslations["Cast Realtime Shadows"] = "Cast Realtime Shadows";
    m_fallbackTranslations["Cast Shadows"] = "Cast Shadows";
    m_fallbackTranslations["Center Offset"] = "Center Offset";
    m_fallbackTranslations["Checkbox"] = "Checkbox";
    m_fallbackTranslations["Cinema (2.39:1)"] = "Cinema (2.39:1)";
    m_fallbackTranslations["Cinema Screen (2.39:1 Feature Film Reel)"] = "Cinema Screen (2.39:1 Feature Film Reel)";
    m_fallbackTranslations["Circle"] = "Circle";
    m_fallbackTranslations["Clear All"] = "Clear All";
    m_fallbackTranslations["Clear Coat"] = "Clear Coat";
    m_fallbackTranslations["Clear Coat Roughness"] = "Clear Coat Roughness";
    m_fallbackTranslations["Click any node in Scene Hierarchy or Viewport to inspect properties and transforms."] = "Click any node in Scene Hierarchy or Viewport to inspect properties and transforms.";
    m_fallbackTranslations["Close"] = "Close";
    m_fallbackTranslations["Close All Secondary Windows"] = "Close All Secondary Windows";
    m_fallbackTranslations["Collider Properties"] = "Collider Properties";
    m_fallbackTranslations["Collider Shape"] = "Collider Shape";
    m_fallbackTranslations["Color"] = "Color";
    m_fallbackTranslations["Color Picker"] = "Color Picker";
    m_fallbackTranslations["Colour Depth (cm)"] = "Colour Depth (cm)";
    m_fallbackTranslations["Cone"] = "Cone";
    m_fallbackTranslations["Console Log"] = "Console Log";
    m_fallbackTranslations["Containers & Windows"] = "Containers & Windows";
    m_fallbackTranslations["Continue Current Project >>"] = "Continue Current Project >>";
    m_fallbackTranslations["Copy"] = "Copy";
    m_fallbackTranslations["Copy Selected Node"] = "Copy Selected Node";
    m_fallbackTranslations["Create"] = "Create";
    m_fallbackTranslations["Create Lua Script"] = "Create Lua Script";
    m_fallbackTranslations["Create New Folder"] = "Create New Folder";
    m_fallbackTranslations["Create New Project"] = "Create New Project";
    m_fallbackTranslations["Create New Scene"] = "Create New Scene";
    m_fallbackTranslations["Create Project"] = "Create Project";
    m_fallbackTranslations["Create Scene"] = "Create Scene";
    m_fallbackTranslations["Create a new project on the right, or browse your disk for an existing .tess project file."] = "Create a new project on the right, or browse your disk for an existing .tess project file.";
    m_fallbackTranslations["Cube"] = "Cube";
    m_fallbackTranslations["Custom TrueType Font (.ttf):"] = "Custom TrueType Font (.ttf):";
    m_fallbackTranslations["Cutscene & Cinematic Sequencer"] = "Cutscene & Cinematic Sequencer";
    m_fallbackTranslations["Cutscene & Cinematic Sequencer..."] = "Cutscene & Cinematic Sequencer...";
    m_fallbackTranslations["Cylinder"] = "Cylinder";
    m_fallbackTranslations["Data & Media"] = "Data & Media";
    m_fallbackTranslations["Date/Time Picker"] = "Date/Time Picker";
    m_fallbackTranslations["Decal Projector"] = "Decal Projector";
    m_fallbackTranslations["Delete"] = "Delete";
    m_fallbackTranslations["Delete Node"] = "Delete Node";
    m_fallbackTranslations["Delete Selected"] = "Delete Selected";
    m_fallbackTranslations["Delete Selected Node"] = "Delete Selected Node";
    m_fallbackTranslations["Delete Selected [Del]"] = "Delete Selected [Del]";
    m_fallbackTranslations["Desktop"] = "Desktop";
    m_fallbackTranslations["Destination Directory:"] = "Destination Directory:";
    m_fallbackTranslations["Directional Light"] = "Directional Light";
    m_fallbackTranslations["Directory Location:"] = "Directory Location:";
    m_fallbackTranslations["Double-Sided"] = "Double-Sided";
    m_fallbackTranslations["Dropdown Box"] = "Dropdown Box";
    m_fallbackTranslations["Duplicate"] = "Duplicate";
    m_fallbackTranslations["Duplicate Selected Node"] = "Duplicate Selected Node";
    m_fallbackTranslations["Edit"] = "Edit";
    m_fallbackTranslations["Emissive Color"] = "Emissive Color";
    m_fallbackTranslations["Emissive Intensity (HDR)"] = "Emissive Intensity (HDR)";
    m_fallbackTranslations["Empty Group Node"] = "Empty Group Node";
    m_fallbackTranslations["Enable ACES Filmic Tonemapping"] = "Enable ACES Filmic Tonemapping";
    m_fallbackTranslations["Enable Declarative Sync"] = "Enable Declarative Sync";
    m_fallbackTranslations["Enable Distance-Based LOD"] = "Enable Distance-Based LOD";
    m_fallbackTranslations["Enable Hardware Ray Tracing (Vulkan / Metal)"] = "Enable Hardware Ray Tracing (Vulkan / Metal)";
    m_fallbackTranslations["Enable Humanoid Ragdoll Physics"] = "Enable Humanoid Ragdoll Physics";
    m_fallbackTranslations["Enable Meshlet GPU Virtualization"] = "Enable Meshlet GPU Virtualization";
    m_fallbackTranslations["Enable Realtime Shadows & CSM"] = "Enable Realtime Shadows & CSM";
    m_fallbackTranslations["Enable Spherical Harmonics Sky GI"] = "Enable Spherical Harmonics Sky GI";
    m_fallbackTranslations["Enable Temporal Upscaling (FSR 3 / DLSS)"] = "Enable Temporal Upscaling (FSR 3 / DLSS)";
    m_fallbackTranslations["Enable Two-Bone IK Solver"] = "Enable Two-Bone IK Solver";
    m_fallbackTranslations["Enable Voxel Global Illumination (VXGI)"] = "Enable Voxel Global Illumination (VXGI)";
    m_fallbackTranslations["Enabled"] = "Enabled";
    m_fallbackTranslations["Enter folder name:"] = "Enter folder name:";
    m_fallbackTranslations["Enter name for the new scene (.scene):"] = "Enter name for the new scene (.scene):";
    m_fallbackTranslations["Enter new name for current scene:"] = "Enter new name for current scene:";
    m_fallbackTranslations["Enter script filename (with .lua):"] = "Enter script filename (with .lua):";
    m_fallbackTranslations["Enter the path to a 3D model file (.obj, .fbx, .gltf, .glb):"] = "Enter the path to a 3D model file (.obj, .fbx, .gltf, .glb):";
    m_fallbackTranslations["Entities & Lighting"] = "Entities & Lighting";
    m_fallbackTranslations["Environment, GI & Post-Processing..."] = "Environment, GI & Post-Processing...";
    m_fallbackTranslations["Export Standalone Game..."] = "Export Standalone Game...";
    m_fallbackTranslations["Far Clip Plane:"] = "Far Clip Plane:";
    m_fallbackTranslations["Field of View:"] = "Field of View:";
    m_fallbackTranslations["File"] = "File";
    m_fallbackTranslations["Fill"] = "Fill";
    m_fallbackTranslations["Floating Window"] = "Floating Window";
    m_fallbackTranslations["Floats on Water"] = "Floats on Water";
    m_fallbackTranslations["Fluid Volume"] = "Fluid Volume";
    m_fallbackTranslations["Focus Pivot on Selected Node [F]"] = "Focus Pivot on Selected Node [F]";
    m_fallbackTranslations["Focus [F]"] = "Focus [F]";
    m_fallbackTranslations["Font Size (px):"] = "Font Size (px):";
    m_fallbackTranslations["Friction"] = "Friction";
    m_fallbackTranslations["Front"] = "Front";
    m_fallbackTranslations["GPU Profiler"] = "GPU Profiler";
    m_fallbackTranslations["Game Active [Play]"] = "Game Active [Play]";
    m_fallbackTranslations["Game Profiler & Resource Monitor"] = "Game Profiler & Resource Monitor";
    m_fallbackTranslations["Game [Standalone Window]"] = "Game [Standalone Window]";
    m_fallbackTranslations["Gizmo"] = "Gizmo";
    m_fallbackTranslations["Glass"] = "Glass";
    m_fallbackTranslations["Grid: OFF"] = "Grid: OFF";
    m_fallbackTranslations["Grid: ON"] = "Grid: ON";
    m_fallbackTranslations["Help"] = "Help";
    m_fallbackTranslations["Idle"] = "Idle";
    m_fallbackTranslations["Image"] = "Image";
    m_fallbackTranslations["Import 3D Model"] = "Import 3D Model";
    m_fallbackTranslations["Import 3D Model..."] = "Import 3D Model...";
    m_fallbackTranslations["Import 3D..."] = "Import 3D...";
    m_fallbackTranslations["Import Model"] = "Import Model";
    m_fallbackTranslations["In-Game Video Screen (TV / Cinema / Phone / Monitor)"] = "In-Game Video Screen (TV / Cinema / Phone / Monitor)";
    m_fallbackTranslations["Inner Cone Angle:"] = "Inner Cone Angle:";
    m_fallbackTranslations["Inputs & Controls"] = "Inputs & Controls";
    m_fallbackTranslations["Inspector"] = "Inspector";
    m_fallbackTranslations["Instantiate Subscene / Prefab (.scene)..."] = "Instantiate Subscene / Prefab (.scene)...";
    m_fallbackTranslations["Intensity:"] = "Intensity:";
    m_fallbackTranslations["Inverse Kinematics (IK) & Morph Targets"] = "Inverse Kinematics (IK) & Morph Targets";
    m_fallbackTranslations["Is Trigger (Overlap Only)"] = "Is Trigger (Overlap Only)";
    m_fallbackTranslations["Iso"] = "Iso";
    m_fallbackTranslations["Joint / Constraint"] = "Joint / Constraint";
    m_fallbackTranslations["Language"] = "Language";
    m_fallbackTranslations["Language:"] = "Language:";
    m_fallbackTranslations["Layout Options"] = "Layout Options";
    m_fallbackTranslations["Level of Detail (LOD) & Mesh Decimation"] = "Level of Detail (LOD) & Mesh Decimation";
    m_fallbackTranslations["Light Color:"] = "Light Color:";
    m_fallbackTranslations["Light Properties"] = "Light Properties";
    m_fallbackTranslations["Light Type:"] = "Light Type:";
    m_fallbackTranslations["Lightmap Static"] = "Lightmap Static";
    m_fallbackTranslations["Linear Velocity"] = "Linear Velocity";
    m_fallbackTranslations["Liquid Fill"] = "Liquid Fill";
    m_fallbackTranslations["Local"] = "Local";
    m_fallbackTranslations["Local Coordinates"] = "Local Coordinates";
    m_fallbackTranslations["Local Position:"] = "Local Position:";
    m_fallbackTranslations["Local Rotation (Degrees):"] = "Local Rotation (Degrees):";
    m_fallbackTranslations["Local Scale:"] = "Local Scale:";
    m_fallbackTranslations["Mass (kg)"] = "Mass (kg)";
    m_fallbackTranslations["Material & Appearance (PBR)"] = "Material & Appearance (PBR)";
    m_fallbackTranslations["Material Model"] = "Material Model";
    m_fallbackTranslations["Menu Bar"] = "Menu Bar";
    m_fallbackTranslations["Metallic (Default)"] = "Metallic (Default)";
    m_fallbackTranslations["Mirror"] = "Mirror";
    m_fallbackTranslations["Multiplayer & Authority"] = "Multiplayer & Authority";
    m_fallbackTranslations["Multiplayer & Networking"] = "Multiplayer & Networking";
    m_fallbackTranslations["Navigation & NavMesh Baker"] = "Navigation & NavMesh Baker";
    m_fallbackTranslations["Near Clip Plane:"] = "Near Clip Plane:";
    m_fallbackTranslations["Nested Scene Instance (Prefab)"] = "Nested Scene Instance (Prefab)";
    m_fallbackTranslations["New Project Name:"] = "New Project Name:";
    m_fallbackTranslations["New Project..."] = "New Project...";
    m_fallbackTranslations["New Scene..."] = "New Scene...";
    m_fallbackTranslations["No node selected."] = "No node selected.";
    m_fallbackTranslations["No other scenes in project"] = "No other scenes in project";
    m_fallbackTranslations["No recent projects"] = "No recent projects";
    m_fallbackTranslations["No recent projects found."] = "No recent projects found.";
    m_fallbackTranslations["Node Name:"] = "Node Name:";
    m_fallbackTranslations["Ocean"] = "Ocean";
    m_fallbackTranslations["Open"] = "Open";
    m_fallbackTranslations["Open HTML Documentation..."] = "Open HTML Documentation...";
    m_fallbackTranslations["Open Multiplayer & Networking Panel >>"] = "Open Multiplayer & Networking Panel >>";
    m_fallbackTranslations["Open Project..."] = "Open Project...";
    m_fallbackTranslations["Open Scene"] = "Open Scene";
    m_fallbackTranslations["Open Visual Shader Graph Editor >>"] = "Open Visual Shader Graph Editor >>";
    m_fallbackTranslations["Orbit Pivot Point:"] = "Orbit Pivot Point:";
    m_fallbackTranslations["Outer Cone Angle:"] = "Outer Cone Angle:";
    m_fallbackTranslations["Panel"] = "Panel";
    m_fallbackTranslations["Particle Emitter (CPU / Niagara)"] = "Particle Emitter (CPU / Niagara)";
    m_fallbackTranslations["Password Field"] = "Password Field";
    m_fallbackTranslations["Paste"] = "Paste";
    m_fallbackTranslations["Paste Node"] = "Paste Node";
    m_fallbackTranslations["Path:"] = "Path:";
    m_fallbackTranslations["Pause [||]"] = "Pause [||]";
    m_fallbackTranslations["Physics & Colliders"] = "Physics & Colliders";
    m_fallbackTranslations["Physics & Dynamics (JoltPhysics)"] = "Physics & Dynamics (JoltPhysics)";
    m_fallbackTranslations["Plane"] = "Plane";
    m_fallbackTranslations["Play Animation"] = "Play Animation";
    m_fallbackTranslations["Play Game [Play]"] = "Play Game [Play]";
    m_fallbackTranslations["Point Light"] = "Point Light";
    m_fallbackTranslations["Pour Spout"] = "Pour Spout";
    m_fallbackTranslations["Preview"] = "Preview";
    m_fallbackTranslations["Progress Bar"] = "Progress Bar";
    m_fallbackTranslations["Project"] = "Project";
    m_fallbackTranslations["Project Files"] = "Project Files";
    m_fallbackTranslations["Project Files (File Viewer)"] = "Project Files (File Viewer)";
    m_fallbackTranslations["Project Hub"] = "Project Hub";
    m_fallbackTranslations["Project Hub & Workspace Manager"] = "Project Hub & Workspace Manager";
    m_fallbackTranslations["Project Input & Key Bindings..."] = "Project Input & Key Bindings...";
    m_fallbackTranslations["Project Launcher / Hub..."] = "Project Launcher / Hub...";
    m_fallbackTranslations["Project Location (Parent Directory):"] = "Project Location (Parent Directory):";
    m_fallbackTranslations["Project Name:"] = "Project Name:";
    m_fallbackTranslations["Project Scripts:"] = "Project Scripts:";
    m_fallbackTranslations["Project Template:"] = "Project Template:";
    m_fallbackTranslations["Project name cannot be empty."] = "Project name cannot be empty.";
    m_fallbackTranslations["Quick Switch:"] = "Quick Switch:";
    m_fallbackTranslations["Radio Button"] = "Radio Button";
    m_fallbackTranslations["Range / Falloff (m):"] = "Range / Falloff (m):";
    m_fallbackTranslations["Recapture Reflection Probes"] = "Recapture Reflection Probes";
    m_fallbackTranslations["Recent Projects"] = "Recent Projects";
    m_fallbackTranslations["Rectangle"] = "Rectangle";
    m_fallbackTranslations["Redo"] = "Redo";
    m_fallbackTranslations["Redo (Ctrl+Y)"] = "Redo (Ctrl+Y)";
    m_fallbackTranslations["Refraction (IOR)"] = "Refraction (IOR)";
    m_fallbackTranslations["Reload Languages"] = "Reload Languages";
    m_fallbackTranslations["Reload Prefab From Disk"] = "Reload Prefab From Disk";
    m_fallbackTranslations["Render"] = "Render";
    m_fallbackTranslations["Rendering, GI & Post-Processing"] = "Rendering, GI & Post-Processing";
    m_fallbackTranslations["Replicate Transform Over Network"] = "Replicate Transform Over Network";
    m_fallbackTranslations["Reset All Morph Weights"] = "Reset All Morph Weights";
    m_fallbackTranslations["Reset Cam"] = "Reset Cam";
    m_fallbackTranslations["Reset Pivot to Origin (0,0,0)"] = "Reset Pivot to Origin (0,0,0)";
    m_fallbackTranslations["Reset Scene to Default"] = "Reset Scene to Default";
    m_fallbackTranslations["Reset Transform"] = "Reset Transform";
    m_fallbackTranslations["Reset View"] = "Reset View";
    m_fallbackTranslations["Reset to Default Window Layout"] = "Reset to Default Window Layout";
    m_fallbackTranslations["Restitution (Bounciness)"] = "Restitution (Bounciness)";
    m_fallbackTranslations["RigidBody Mode"] = "RigidBody Mode";
    m_fallbackTranslations["Rotate"] = "Rotate";
    m_fallbackTranslations["Rotate [E]"] = "Rotate [E]";
    m_fallbackTranslations["Roughness (Default)"] = "Roughness (Default)";
    m_fallbackTranslations["Rounded Rectangle"] = "Rounded Rectangle";
    m_fallbackTranslations["Ruler: OFF [M]"] = "Ruler: OFF [M]";
    m_fallbackTranslations["Ruler: ON [M]"] = "Ruler: ON [M]";
    m_fallbackTranslations["Run"] = "Run";
    m_fallbackTranslations["Save Copy"] = "Save Copy";
    m_fallbackTranslations["Save Project"] = "Save Project";
    m_fallbackTranslations["Save Project As"] = "Save Project As";
    m_fallbackTranslations["Save Project As..."] = "Save Project As...";
    m_fallbackTranslations["Save Scene"] = "Save Scene";
    m_fallbackTranslations["Save Scene As"] = "Save Scene As";
    m_fallbackTranslations["Save Scene As..."] = "Save Scene As...";
    m_fallbackTranslations["Save Scene Copy"] = "Save Scene Copy";
    m_fallbackTranslations["Save copy of project to a new location:"] = "Save copy of project to a new location:";
    m_fallbackTranslations["Scale"] = "Scale";
    m_fallbackTranslations["Scale [R]"] = "Scale [R]";
    m_fallbackTranslations["Scene"] = "Scene";
    m_fallbackTranslations["Scene Hierarchy"] = "Scene Hierarchy";
    m_fallbackTranslations["Screen Glow (HDR Emissive)"] = "Screen Glow (HDR Emissive)";
    m_fallbackTranslations["Script Component"] = "Script Component";
    m_fallbackTranslations["Script Editor"] = "Script Editor";
    m_fallbackTranslations["Scroll Pane"] = "Scroll Pane";
    m_fallbackTranslations["Security CCTV Monitor"] = "Security CCTV Monitor";
    m_fallbackTranslations["Select Location for New Project"] = "Select Location for New Project";
    m_fallbackTranslations["Selectable List"] = "Selectable List";
    m_fallbackTranslations["Set as Active Preview Camera"] = "Set as Active Preview Camera";
    m_fallbackTranslations["Shadow Bias:"] = "Shadow Bias:";
    m_fallbackTranslations["Shape Type:"] = "Shape Type:";
    m_fallbackTranslations["Show 3D Pivot Marker"] = "Show 3D Pivot Marker";
    m_fallbackTranslations["Show All Workspace Panels"] = "Show All Workspace Panels";
    m_fallbackTranslations["Show Ground Grid"] = "Show Ground Grid";
    m_fallbackTranslations["Side"] = "Side";
    m_fallbackTranslations["Size (W, H):"] = "Size (W, H):";
    m_fallbackTranslations["Slider"] = "Slider";
    m_fallbackTranslations["Slosh"] = "Slosh";
    m_fallbackTranslations["Smartphone (9:16)"] = "Smartphone (9:16)";
    m_fallbackTranslations["Smartphone Screen (9:16 Incoming Call)"] = "Smartphone Screen (9:16 Incoming Call)";
    m_fallbackTranslations["Snap"] = "Snap";
    m_fallbackTranslations["Solid"] = "Solid";
    m_fallbackTranslations["Specialized Tools"] = "Specialized Tools";
    m_fallbackTranslations["Specify Project Name and Destination Directory:"] = "Specify Project Name and Destination Directory:";
    m_fallbackTranslations["Sphere"] = "Sphere";
    m_fallbackTranslations["Sphere Collider"] = "Sphere Collider";
    m_fallbackTranslations["Sphere Radius"] = "Sphere Radius";
    m_fallbackTranslations["Spinner"] = "Spinner";
    m_fallbackTranslations["Spline / 3D Curve"] = "Spline / 3D Curve";
    m_fallbackTranslations["Spot Light"] = "Spot Light";
    m_fallbackTranslations["Standalone Game View"] = "Standalone Game View";
    m_fallbackTranslations["Start Offset (s)"] = "Start Offset (s)";
    m_fallbackTranslations["Stop [X]"] = "Stop [X]";
    m_fallbackTranslations["Surface Colour"] = "Surface Colour";
    m_fallbackTranslations["TV Set (16:9 Live News Broadcast)"] = "TV Set (16:9 Live News Broadcast)";
    m_fallbackTranslations["TV Set (16:9)"] = "TV Set (16:9)";
    m_fallbackTranslations["Table Grid"] = "Table Grid";
    m_fallbackTranslations["Tabs View"] = "Tabs View";
    m_fallbackTranslations["Terrain (Landscape)"] = "Terrain (Landscape)";
    m_fallbackTranslations["Test Corner PiP Transmission Now"] = "Test Corner PiP Transmission Now";
    m_fallbackTranslations["Test Full-Screen Video Cutaway Now"] = "Test Full-Screen Video Cutaway Now";
    m_fallbackTranslations["Text Field"] = "Text Field";
    m_fallbackTranslations["Text Label"] = "Text Label";
    m_fallbackTranslations["Third-Person Character Sandbox"] = "Third-Person Character Sandbox";
    m_fallbackTranslations["Tip: You can switch or create projects at any time from File -> Project Launcher / Hub..."] = "Tip: You can switch or create projects at any time from File -> Project Launcher / Hub...";
    m_fallbackTranslations["Top"] = "Top";
    m_fallbackTranslations["Transform"] = "Transform";
    m_fallbackTranslations["Translate"] = "Translate";
    m_fallbackTranslations["Translate [W]"] = "Translate [W]";
    m_fallbackTranslations["Triangle"] = "Triangle";
    m_fallbackTranslations["Triplanar Mapping"] = "Triplanar Mapping";
    m_fallbackTranslations["Two-Sided Emission"] = "Two-Sided Emission";
    m_fallbackTranslations["Undo"] = "Undo";
    m_fallbackTranslations["Undo (Ctrl+Z)"] = "Undo (Ctrl+Z)";
    m_fallbackTranslations["Unparent"] = "Unparent";
    m_fallbackTranslations["Unparent (Move to Root)"] = "Unparent (Move to Root)";
    m_fallbackTranslations["Use Gravity"] = "Use Gravity";
    m_fallbackTranslations["Vector Shapes"] = "Vector Shapes";
    m_fallbackTranslations["Video Channel"] = "Video Channel";
    m_fallbackTranslations["Video Player"] = "Video Player";
    m_fallbackTranslations["Visible"] = "Visible";
    m_fallbackTranslations["Visual Shader Graph Editor"] = "Visual Shader Graph Editor";
    m_fallbackTranslations["Visual State Machine Graph >>"] = "Visual State Machine Graph >>";
    m_fallbackTranslations["Walk"] = "Walk";
    m_fallbackTranslations["Water"] = "Water";
    m_fallbackTranslations["Window"] = "Window";
    m_fallbackTranslations["Wire+Shaded"] = "Wire+Shaded";
    m_fallbackTranslations["Wireframe"] = "Wireframe";
    m_fallbackTranslations["Workspace Panels"] = "Workspace Panels";
    m_fallbackTranslations["World"] = "World";
    m_fallbackTranslations["World Coordinates"] = "World Coordinates";
    m_fallbackTranslations["World Position"] = "World Position";
    m_fallbackTranslations["Zoom +"] = "Zoom +";
    m_fallbackTranslations["Zoom -"] = "Zoom -";
    m_fallbackTranslations["[..] Up"] = "[..] Up";
    m_fallbackTranslations["[Missing]"] = "[Missing]";
    m_fallbackTranslations["from Scene [Del]"] = "from Scene [Del]";
    m_fallbackTranslations["nodes"] = "nodes";
    m_fallbackTranslations["presets"] = "presets";
    m_fallbackTranslations["| Editor Mode"] = "| Editor Mode";
    m_fallbackTranslations["| GAME PAUSED"] = "| GAME PAUSED";
    m_fallbackTranslations["| GAME RUNNING in Standalone Window"] = "| GAME RUNNING in Standalone Window";
}


bool Localization::LoadLanguageFile(const std::string& filePath, std::string& outCode, std::string& outName,
                                    std::unordered_map<std::string, std::string>& outStrings) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    JsonValue root = JsonValue::Parse(content);
    if (!root.IsObject()) return false;

    if (root.HasKey("code") && root["code"].IsString()) {
        outCode = root["code"].AsString();
    }
    if (root.HasKey("name") && root["name"].IsString()) {
        outName = root["name"].AsString();
    }
    if (root.HasKey("strings") && root["strings"].IsObject()) {
        for (const auto& [k, v] : root["strings"].objVal) {
            if (v.IsString()) {
                outStrings[k] = v.AsString();
            }
        }
    }
    return !outCode.empty();
}


void Localization::ScanLanguages() {
    m_languages.clear();

    if (fs::exists(m_langDir) && fs::is_directory(m_langDir)) {
        for (const auto& entry : fs::directory_iterator(m_langDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string code, name;
                std::unordered_map<std::string, std::string> tempStrings;
                if (LoadLanguageFile(entry.path().string(), code, name, tempStrings)) {
                    LanguageInfo info;
                    info.code = code;
                    info.displayName = name;
                    info.filePath = entry.path().string();
                    m_languages.push_back(info);
                }
            }
        }
    }

    // Ensure English and Spanish exist in list even if directory had issues
    auto hasCode = [this](const std::string& c) {
        for (const auto& l : m_languages) {
            if (l.code == c) return true;
        }
        return false;
    };

    if (!hasCode("en")) {
        m_languages.push_back({"en", "English", (fs::path(m_langDir) / "en.json").string()});
    }
    if (!hasCode("es")) {
        m_languages.push_back({"es", "Español", (fs::path(m_langDir) / "es.json").string()});
    }

    // Sort languages so English is first, Spanish second, and others follow alphabetically
    std::sort(m_languages.begin(), m_languages.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
        if (a.code == "en") return true;
        if (b.code == "en") return false;
        if (a.code == "es") return true;
        if (b.code == "es") return false;
        return a.displayName < b.displayName;
    });
}

std::string Localization::GetCurrentLanguageName() const {
    for (const auto& l : m_languages) {
        if (l.code == m_currentCode) {
            return l.displayName;
        }
    }
    return m_currentCode;
}

bool Localization::SetLanguage(const std::string& code, bool saveSettings) {
    std::string targetCode = code;
    std::string filePath = "";

    for (const auto& l : m_languages) {
        if (l.code == targetCode) {
            filePath = l.filePath;
            break;
        }
    }

    if (filePath.empty()) {
        filePath = (fs::path(m_langDir) / (targetCode + ".json")).string();
    }

    std::unordered_map<std::string, std::string> loadedStrings;
    std::string outCode, outName;
    if (fs::exists(filePath) && LoadLanguageFile(filePath, outCode, outName, loadedStrings)) {
        m_translations = std::move(loadedStrings);
        m_currentCode = targetCode;
    } else {
        // Fallback for en or es if file not found
        if (targetCode == "en") {
            m_translations = m_fallbackTranslations;
            m_currentCode = "en";
        } else if (targetCode == "es") {
            EnsureDefaultFiles();
            if (LoadLanguageFile(filePath, outCode, outName, loadedStrings)) {
                m_translations = std::move(loadedStrings);
            }
            m_currentCode = "es";
        } else {
            return false;
        }
    }

    m_idStringCache.clear();

    if (saveSettings) {
        SaveSettings();
    }

    return true;
}

void Localization::ReloadCurrentLanguage() {
    ScanLanguages();
    SetLanguage(m_currentCode, false);
}

const char* Localization::Translate(const char* key) {
    if (!key || key[0] == '\0') return "";

    // 1. Direct match in active language dictionary
    auto it = m_translations.find(key);
    if (it != m_translations.end()) {
        return it->second.c_str();
    }

    // 2. Check for Dear ImGui ID suffix "##" or "###"
    std::string keyStr(key);
    size_t hashPos = keyStr.find("##");
    if (hashPos != std::string::npos) {
        // If already cached, return from cache
        auto cit = m_idStringCache.find(keyStr);
        if (cit != m_idStringCache.end()) {
            return cit->second.c_str();
        }

        std::string label = keyStr.substr(0, hashPos);
        std::string idPart = keyStr.substr(hashPos);

        std::string translatedLabel = Translate(label);
        std::string combined = translatedLabel + idPart;

        m_idStringCache[keyStr] = std::move(combined);
        return m_idStringCache[keyStr].c_str();
    }

    // 3. Fallback to English dictionary if active isn't English
    if (m_currentCode != "en") {
        auto fit = m_fallbackTranslations.find(key);
        if (fit != m_fallbackTranslations.end()) {
            return fit->second.c_str();
        }
    }

    // 4. Fallback to original key
    return key;
}

std::string Localization::Translate(const std::string& key) {
    return std::string(Translate(key.c_str()));
}

void Localization::AddTranslation(const std::string& langCode, const std::string& key, const std::string& value) {
    if (langCode == m_currentCode) {
        m_translations[key] = value;
        m_idStringCache.clear();
    }
    if (langCode == "en") {
        m_fallbackTranslations[key] = value;
    }
}

void Localization::LoadSettings() {
    std::string path = "editor_settings.json";
    if (!fs::exists(path)) return;

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    JsonValue root = JsonValue::Parse(content);
    if (root.HasKey("language") && root["language"].IsString()) {
        std::string code = root["language"].AsString();
        if (!code.empty()) {
            SetLanguage(code, false);
        }
    }
}

void Localization::SaveSettings() {
    std::string path = "editor_settings.json";
    JsonValue root = JsonValue::Object();

    if (fs::exists(path)) {
        std::ifstream inFile(path);
        if (inFile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            inFile.close();
            root = JsonValue::Parse(content);
            if (!root.IsObject()) root = JsonValue::Object();
        }
    }

    root["language"] = m_currentCode;

    std::ofstream file(path);
    if (file.is_open()) {
        file << root.Serialize(2);
    }
}

