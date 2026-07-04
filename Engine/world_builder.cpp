// =========================================================================
// WORLD BUILDER — a tiny raylib "engine" that writes code while you model
// =========================================================================
//
// WHAT'S NEW IN THIS VERSION
//   - FLYING: Space = up, Left Shift = down, WASD = move, mouse = look.
//     There's no gravity or ground collision — you can fly anywhere.
//   - TEXTURES: drop image files (.png/.jpg/.jpeg/.bmp/.tga) into a folder
//     named "textures" next to the compiled program. On startup the editor
//     scans that folder and lets you cycle through them with 'T' (including
//     a "None" option for a flat color). Whatever texture is selected when
//     you place a shape gets applied to it. "Uploading from your device"
//     just means: put the image file in that folder — the program is a
//     desktop app, not a website, so there's no upload button, only a
//     folder it reads from.
//
// HOW IT WORKS
//   - You fly around freely with mouse + WASD + Space/Shift
//   - A translucent "ghost" shape floats in front of your camera
//   - You cycle shape type / size / color / texture, then place it
//   - Every time the world changes (place / undo / clear), this program
//     automatically rewrites "generated_world.cpp" on disk — a complete,
//     ready-to-compile raylib program (with the same flying camera and
//     the same textures) containing everything you've placed.
//   - Close the editor, compile generated_world.cpp, and that's your game.
//
// CONTROLS
//   Mouse             Look around
//   W / A / S / D     Move forward / left / back / right
//   Space             Fly up
//   Left Shift        Fly down
//   1 / 2 / 3         Select shape type: Cube / Sphere / Cylinder
//   Mouse Wheel       Move placement distance closer / further
//   [ and ]           Shrink / grow the ghost shape
//   C                 Cycle flat color (used when no texture is selected)
//   T                 Cycle texture (cycles through everything found in
//                     the "textures" folder, plus a "None" option)
//   LEFT CLICK or F   Place the shape into the world
//   BACKSPACE         Undo last placed object
//   X                 Clear entire world
//   E                 Force an export right now (also happens automatically)
//   ESC               Quit
//
// SETUP
//   Put any .png/.jpg/.jpeg/.bmp/.tga files in a folder called "textures"
//   sitting next to the compiled executable, e.g.:
//       world_builder            (the compiled program)
//       textures/
//           brick.png
//           grass.jpg
//   If the folder doesn't exist or is empty, everything just falls back
//   to flat colors — nothing breaks.
//
// BUILD
//   g++ world_builder.cpp -o world_builder -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -std=c++17
//   (the -std=c++17 is required now — the texture folder scan uses
//   std::filesystem)
//
// =========================================================================

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

// -------------------------------------------------------------------------
// Data model
// -------------------------------------------------------------------------
enum ShapeType { SHAPE_CUBE = 0, SHAPE_SPHERE = 1, SHAPE_CYLINDER = 2, SHAPE_COUNT = 3 };

struct WorldObject {
    ShapeType type;
    Vector3 position;
    Vector3 size;         // cube: width/height/length | sphere: radius,_,_ | cylinder: radius,height,_
    Color   color;        // used only when texturePath is empty
    std::string texturePath; // empty = no texture, flat color instead
};

static const char* ShapeName(ShapeType t) {
    switch (t) {
        case SHAPE_CUBE:     return "Cube";
        case SHAPE_SPHERE:   return "Sphere";
        case SHAPE_CYLINDER: return "Cylinder";
        default:             return "Unknown";
    }
}

static const Color kPalette[] = { RED, LIME, GOLD, SKYBLUE, VIOLET, ORANGE, PINK, BEIGE };
static const int   kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);
static const char* kPaletteName[] = { "RED", "LIME", "GOLD", "SKYBLUE", "VIOLET", "ORANGE", "PINK", "BEIGE" };

static int PaletteIndexOf(Color c) {
    for (int i = 0; i < kPaletteCount; i++) {
        if (kPalette[i].r == c.r && kPalette[i].g == c.g &&
            kPalette[i].b == c.b && kPalette[i].a == c.a) return i;
    }
    return -1;
}

// -------------------------------------------------------------------------
// Texture folder scanning
// -------------------------------------------------------------------------
static std::vector<std::string> ScanTexturesFolder(const std::string& dir) {
    std::vector<std::string> results;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return results;

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            results.push_back(entry.path().string());
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

// -------------------------------------------------------------------------
// Fly camera: mouse look + WASD + Space (up) / Left Shift (down)
// No gravity, no collision — pure free flight.
// -------------------------------------------------------------------------
struct FlyCamera {
    float yaw = -90.0f;   // -90 => facing -Z, matching the original camera.target
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float mouseSensitivity = 0.12f;
};

static Vector3 UpdateFlyCamera(Camera3D& camera, FlyCamera& fly, float dt) {
    Vector2 mouseDelta = GetMouseDelta();
    fly.yaw   += mouseDelta.x * fly.mouseSensitivity;
    fly.pitch -= mouseDelta.y * fly.mouseSensitivity;
    if (fly.pitch > 89.0f) fly.pitch = 89.0f;
    if (fly.pitch < -89.0f) fly.pitch = -89.0f;

    Vector3 forward;
    forward.x = cosf(DEG2RAD * fly.yaw) * cosf(DEG2RAD * fly.pitch);
    forward.y = sinf(DEG2RAD * fly.pitch);
    forward.z = sinf(DEG2RAD * fly.yaw) * cosf(DEG2RAD * fly.pitch);
    forward = Vector3Normalize(forward);

    Vector3 up = Vector3 { 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));

    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_SPACE))      move = Vector3Add(move, up);
    if (IsKeyDown(KEY_LEFT_SHIFT)) move = Vector3Subtract(move, up);

    if (Vector3Length(move) > 0.0f) {
        move = Vector3Normalize(move);
        camera.position = Vector3Add(camera.position, Vector3Scale(move, fly.moveSpeed * dt));
    }
    camera.target = Vector3Add(camera.position, forward);
    return forward;
}

// -------------------------------------------------------------------------
// Code generation: turns the current world into a standalone, compilable
// raylib program, complete with the same flying camera and textures.
// -------------------------------------------------------------------------
static void ExportWorld(const std::vector<WorldObject>& world, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    // Dedupe texture paths, preserving first-seen order.
    std::vector<std::string> usedTextures;
    for (const auto& obj : world) {
        if (obj.texturePath.empty()) continue;
        bool found = false;
        for (auto& p : usedTextures) if (p == obj.texturePath) { found = true; break; }
        if (!found) usedTextures.push_back(obj.texturePath);
    }
    auto textureIndexOf = [&](const std::string& p) -> int {
        for (size_t i = 0; i < usedTextures.size(); i++) if (usedTextures[i] == p) return (int)i;
        return -1;
    };

    out << "// Auto-generated by world_builder.cpp — do not hand-edit, it will be overwritten.\n";
    out << "// " << world.size() << " object(s), " << usedTextures.size() << " texture(s).\n";
    out << "// Keep any texture files at the same relative paths when you run this program.\n\n";
    out << "#include \"raylib.h\"\n";
    out << "#include \"raymath.h\"\n";
    out << "#include <vector>\n\n";

    out << "struct FlyCamera { float yaw = -90.0f; float pitch = 0.0f; float moveSpeed = 6.0f; float mouseSensitivity = 0.12f; };\n\n";
    out << "void UpdateFlyCamera(Camera3D &camera, FlyCamera &fly, float dt) {\n";
    out << "    Vector2 mouseDelta = GetMouseDelta();\n";
    out << "    fly.yaw   += mouseDelta.x * fly.mouseSensitivity;\n";
    out << "    fly.pitch -= mouseDelta.y * fly.mouseSensitivity;\n";
    out << "    if (fly.pitch > 89.0f) fly.pitch = 89.0f;\n";
    out << "    if (fly.pitch < -89.0f) fly.pitch = -89.0f;\n\n";
    out << "    Vector3 forward;\n";
    out << "    forward.x = cosf(DEG2RAD * fly.yaw) * cosf(DEG2RAD * fly.pitch);\n";
    out << "    forward.y = sinf(DEG2RAD * fly.pitch);\n";
    out << "    forward.z = sinf(DEG2RAD * fly.yaw) * cosf(DEG2RAD * fly.pitch);\n";
    out << "    forward = Vector3Normalize(forward);\n\n";
    out << "    Vector3 up = (Vector3){ 0.0f, 1.0f, 0.0f };\n";
    out << "    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));\n\n";
    out << "    Vector3 move = { 0 };\n";
    out << "    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);\n";
    out << "    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);\n";
    out << "    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);\n";
    out << "    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);\n";
    out << "    if (IsKeyDown(KEY_SPACE)) move = Vector3Add(move, up);\n";
    out << "    if (IsKeyDown(KEY_LEFT_SHIFT)) move = Vector3Subtract(move, up);\n\n";
    out << "    if (Vector3Length(move) > 0.0f) {\n";
    out << "        move = Vector3Normalize(move);\n";
    out << "        camera.position = Vector3Add(camera.position, Vector3Scale(move, fly.moveSpeed * dt));\n";
    out << "    }\n";
    out << "    camera.target = Vector3Add(camera.position, forward);\n";
    out << "}\n\n";

    out << "enum ShapeType { SHAPE_CUBE = 0, SHAPE_SPHERE = 1, SHAPE_CYLINDER = 2 };\n";
    out << "struct WorldObject { ShapeType type; Vector3 position; Vector3 size; Color color; int textureIndex; };\n\n";

    out << "int main() {\n";
    out << "    InitWindow(1280, 720, \"Generated World\");\n\n";
    out << "    Camera3D camera = { 0 };\n";
    out << "    camera.position = (Vector3){ 0.0f, 2.0f, 5.0f };\n";
    out << "    camera.target   = (Vector3){ 0.0f, 2.0f, 4.0f };\n";
    out << "    camera.up       = (Vector3){ 0.0f, 1.0f, 0.0f };\n";
    out << "    camera.fovy     = 60.0f;\n";
    out << "    camera.projection = CAMERA_PERSPECTIVE;\n";
    out << "    DisableCursor();\n";
    out << "    SetTargetFPS(60);\n";
    out << "    FlyCamera fly;\n\n";

    out << "    std::vector<Texture2D> textures;\n";
    out << "    Image whiteImg = GenImageColor(1, 1, WHITE);\n";
    out << "    Texture2D texWhite = LoadTextureFromImage(whiteImg);\n";
    out << "    UnloadImage(whiteImg);\n";
    for (const auto& tp : usedTextures) {
        out << "    textures.push_back(LoadTexture(\"" << tp << "\"));\n";
    }
    out << "\n";

    out << "    Model models[3];\n";
    out << "    models[SHAPE_CUBE]     = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));\n";
    out << "    models[SHAPE_SPHERE]   = LoadModelFromMesh(GenMeshSphere(1.0f, 16, 16));\n";
    out << "    models[SHAPE_CYLINDER] = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));\n\n";

    out << "    std::vector<WorldObject> world;\n";
    for (const auto& obj : world) {
        int pIdx = PaletteIndexOf(obj.color);
        std::string colorName = (pIdx >= 0) ? kPaletteName[pIdx] : "WHITE";
        int texIdx = textureIndexOf(obj.texturePath);
        const char* typeName = (obj.type == SHAPE_CUBE) ? "SHAPE_CUBE" :
                                (obj.type == SHAPE_SPHERE) ? "SHAPE_SPHERE" : "SHAPE_CYLINDER";
        out << "    world.push_back({ " << typeName << ", (Vector3){ "
            << obj.position.x << "f, " << obj.position.y << "f, " << obj.position.z << "f }, (Vector3){ "
            << obj.size.x << "f, " << obj.size.y << "f, " << obj.size.z << "f }, "
            << colorName << ", " << texIdx << " });\n";
    }
    out << "\n";

    out << "    while (!WindowShouldClose()) {\n";
    out << "        float dt = GetFrameTime();\n";
    out << "        UpdateFlyCamera(camera, fly, dt);\n\n";
    out << "        BeginDrawing();\n";
    out << "            ClearBackground(SKYBLUE);\n";
    out << "            BeginMode3D(camera);\n";
    out << "                DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 50.0f, 50.0f }, GRAY);\n";
    out << "                DrawGrid(50, 1.0f);\n\n";
    out << "                for (auto &obj : world) {\n";
    out << "                    Texture2D tex = (obj.textureIndex >= 0) ? textures[obj.textureIndex] : texWhite;\n";
    out << "                    models[obj.type].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;\n";
    out << "                    Vector3 scale;\n";
    out << "                    if (obj.type == SHAPE_CUBE) scale = obj.size;\n";
    out << "                    else if (obj.type == SHAPE_SPHERE) scale = (Vector3){ obj.size.x, obj.size.x, obj.size.x };\n";
    out << "                    else scale = (Vector3){ obj.size.x, obj.size.y, obj.size.x };\n";
    out << "                    Color tint = (obj.textureIndex >= 0) ? WHITE : obj.color;\n";
    out << "                    DrawModelEx(models[obj.type], obj.position, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f, scale, tint);\n";
    out << "                }\n";
    out << "            EndMode3D();\n";
    out << "            DrawText(\"Generated world - WASD+mouse, Space up, Shift down\", 20, 20, 16, WHITE);\n";
    out << "        EndDrawing();\n";
    out << "    }\n\n";

    out << "    for (auto &t : textures) UnloadTexture(t);\n";
    out << "    UnloadTexture(texWhite);\n";
    out << "    for (int i = 0; i < 3; i++) UnloadModel(models[i]);\n";
    out << "    CloseWindow();\n";
    out << "    return 0;\n";
    out << "}\n";

    out.close();
}

// -------------------------------------------------------------------------
// Main editor loop
// -------------------------------------------------------------------------
int main() {
    const int screenWidth = 1280;
    const int screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "World Builder - a tiny code-writing engine");

    Camera3D camera = { 0 };
    camera.position = Vector3 { 0.0f, 2.0f, 5.0f };
    camera.target   = Vector3 { 0.0f, 2.0f, 4.0f };
    camera.up       = Vector3 { 0.0f, 1.0f, 0.0f };
    camera.fovy     = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    DisableCursor();
    SetTargetFPS(60);
    FlyCamera fly;

    // ---- textures: scan the "textures" folder next to the executable ----
    TraceLog(LOG_INFO, "Working directory is: %s", GetWorkingDirectory());
    TraceLog(LOG_INFO, "Looking for a folder called 'textures' inside that directory.");
    std::vector<std::string> texturePaths = ScanTexturesFolder("textures");
    TraceLog(LOG_INFO, "Found %d texture file(s) in textures/", (int)texturePaths.size());
    for (const auto& p : texturePaths) TraceLog(LOG_INFO, "  -> %s", p.c_str());
    std::vector<Texture2D> textures;
    for (const auto& p : texturePaths) {
        Texture2D t = LoadTexture(p.c_str());
        if (t.id == 0) {
            TraceLog(LOG_WARNING, "TEXTURE FAILED TO LOAD: %s (check your working directory!)", p.c_str());
        }
        textures.push_back(t);
    }

    Image whiteImg = GenImageColor(1, 1, WHITE);
    Texture2D texWhite = LoadTextureFromImage(whiteImg);
    UnloadImage(whiteImg);

    // ---- models: one unit-sized mesh per shape, scaled per object at draw time ----
    Model models[SHAPE_COUNT];
    models[SHAPE_CUBE]     = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    models[SHAPE_SPHERE]   = LoadModelFromMesh(GenMeshSphere(1.0f, 16, 16));
    models[SHAPE_CYLINDER] = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));

    std::vector<WorldObject> world;

    ShapeType currentType   = SHAPE_CUBE;
    float placeDistance     = 6.0f;
    float ghostScale        = 1.5f;
    int   colorIndex        = 0;
    int   currentTexIndex   = -1; // -1 = none (flat color)

    const std::string exportPath = "generated_world.cpp";
    bool needsExport = true;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector3 forward = camera.target; // fallback if cursor is unlocked this frame
        if (IsCursorHidden()) {
            forward = UpdateFlyCamera(camera, fly, dt);
        } else {
            forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        }

        // ---- cursor lock toggle ----
        if (IsKeyPressed(KEY_TAB)) {
            if (IsCursorHidden()) EnableCursor();
            else DisableCursor();
        }

        // ---- shape / color / texture selection ----
        if (IsKeyPressed(KEY_ONE))   currentType = SHAPE_CUBE;
        if (IsKeyPressed(KEY_TWO))   currentType = SHAPE_SPHERE;
        if (IsKeyPressed(KEY_THREE)) currentType = SHAPE_CYLINDER;
        if (IsKeyPressed(KEY_C)) colorIndex = (colorIndex + 1) % kPaletteCount;
        if (IsKeyPressed(KEY_T)) {
            currentTexIndex++;
            if (currentTexIndex >= (int)textures.size()) currentTexIndex = -1;
        }

        // ---- size / distance ----
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) placeDistance = Clamp(placeDistance + wheel * 0.75f, 2.0f, 30.0f);
        if (IsKeyPressed(KEY_LEFT_BRACKET))  ghostScale = Clamp(ghostScale - 0.25f, 0.25f, 10.0f);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) ghostScale = Clamp(ghostScale + 0.25f, 0.25f, 10.0f);

        // ---- ghost transform ----
        Vector3 ghostPos = Vector3Add(camera.position, Vector3Scale(forward, placeDistance));
        Vector3 ghostSize;
        switch (currentType) {
            case SHAPE_CUBE:     ghostSize = Vector3 { ghostScale, ghostScale, ghostScale }; break;
            case SHAPE_SPHERE:   ghostSize = Vector3 { ghostScale * 0.5f, 0, 0 }; break;
            case SHAPE_CYLINDER: ghostSize = Vector3 { ghostScale * 0.5f, ghostScale * 2.0f, 0 }; break;
            default:             ghostSize = Vector3 { ghostScale, ghostScale, ghostScale }; break;
        }

        // ---- place / undo / clear ----
        if (IsKeyPressed(KEY_F) || (IsCursorHidden() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
            WorldObject obj;
            obj.type = currentType;
            obj.position = ghostPos;
            obj.size = ghostSize;
            obj.color = kPalette[colorIndex];
            obj.texturePath = (currentTexIndex >= 0) ? texturePaths[currentTexIndex] : "";
            world.push_back(obj);
            needsExport = true;
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !world.empty()) { world.pop_back(); needsExport = true; }
        if (IsKeyPressed(KEY_X)) { world.clear(); needsExport = true; }
        if (IsKeyPressed(KEY_E)) needsExport = true;

        if (needsExport) {
            ExportWorld(world, exportPath);
            needsExport = false;
        }

        // ---- draw ----
        BeginDrawing();
            ClearBackground(SKYBLUE);

            BeginMode3D(camera);
                DrawPlane(Vector3 { 0.0f, 0.0f, 0.0f }, Vector2 { 50.0f, 50.0f }, GRAY);
                DrawGrid(50, 1.0f);

                // placed objects (textured models)
                for (const auto& obj : world) {
                    int texIdx = -1;
                    for (size_t i = 0; i < texturePaths.size(); i++)
                        if (texturePaths[i] == obj.texturePath) { texIdx = (int)i; break; }

                    Texture2D tex = (texIdx >= 0 && textures[texIdx].id != 0) ? textures[texIdx] : texWhite;
                    models[obj.type].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;

                    Vector3 scale;
                    if (obj.type == SHAPE_CUBE) scale = obj.size;
                    else if (obj.type == SHAPE_SPHERE) scale = Vector3 { obj.size.x, obj.size.x, obj.size.x };
                    else scale = Vector3 { obj.size.x, obj.size.y, obj.size.x };

                    Color tint = (texIdx >= 0) ? WHITE : obj.color;
                    DrawModelEx(models[obj.type], obj.position, Vector3 { 0.0f, 1.0f, 0.0f }, 0.0f, scale, tint);
                }

                // ghost preview (simple wireframe, no texture needed)
                Color ghostColor = Fade(kPalette[colorIndex], 0.6f);
                switch (currentType) {
                    case SHAPE_CUBE:
                        DrawCubeWires(ghostPos, ghostSize.x, ghostSize.y, ghostSize.z, ghostColor);
                        break;
                    case SHAPE_SPHERE:
                        DrawSphereWires(ghostPos, ghostSize.x, 8, 8, ghostColor);
                        break;
                    case SHAPE_CYLINDER:
                        DrawCylinderWires(ghostPos, ghostSize.x, ghostSize.x, ghostSize.y, 16, ghostColor);
                        break;
                    default: break;
                }
            EndMode3D();

            // ---- HUD ----
            std::string texLabel = (currentTexIndex >= 0)
                ? GetFileName(texturePaths[currentTexIndex].c_str())
                : "None (flat color)";

            DrawRectangle(10, 10, 360, 210, Fade(BLACK, 0.5f));
            DrawText("WORLD BUILDER", 20, 18, 18, WHITE);
            DrawText(TextFormat("Shape:   %s   (1/2/3)", ShapeName(currentType)), 20, 44, 14, LIGHTGRAY);
            DrawText(TextFormat("Color:   %s   (C)", kPaletteName[colorIndex]), 20, 64, 14, LIGHTGRAY);
            DrawText(TextFormat("Texture: %s   (T)", texLabel.c_str()), 20, 84, 14, LIGHTGRAY);
            DrawText(TextFormat("Size:    %.2f   ([ / ])", ghostScale), 20, 104, 14, LIGHTGRAY);
            DrawText(TextFormat("Dist:    %.1f   (scroll)", placeDistance), 20, 124, 14, LIGHTGRAY);
            DrawText(TextFormat("Objects: %d   (found %d texture(s))", (int)world.size(), (int)textures.size()), 20, 144, 14, LIGHTGRAY);
            DrawText("Click/F: place   Backspace: undo   X: clear", 20, 166, 12, GRAY);
            DrawText("Move: WASD   Look: mouse   Up: Space   Down: Shift   TAB: toggle cursor lock", 20, 184, 12, GRAY);

            DrawCircle(screenWidth / 2, screenHeight / 2, 4, Fade(WHITE, 0.8f));
            DrawText("-> generated_world.cpp is being written live next to this program", 20, screenHeight - 30, 14, GREEN);

        EndDrawing();
    }

    ExportWorld(world, exportPath);

    for (auto& t : textures) UnloadTexture(t);
    UnloadTexture(texWhite);
    for (int i = 0; i < SHAPE_COUNT; i++) UnloadModel(models[i]);

    CloseWindow();
    return 0;
}
