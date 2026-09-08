#include "Lab.h"
#include "LabFont.h"
#include "LabDialogs.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace Lab;

struct TextureEntry {
    std::string name;
    std::string filename;
};

enum class SelectionType {
    None,
    Brush,
    Prop,
    Door,
    Spawn,
    WeaponSpawner
};

class LabHammerStandalone : public Engine {
public:
    LabHammerStandalone()
        : Engine("Hammer - [Lab Map Editor 2026]", 1600, 900),
          _camera(70.0f, 16.0f / 9.0f, 0.01f, 3000.0f) {
    }

    void onInit() override {
        LabLog::info("Launching Lab Hammer 3D Level Editor...");
        Renderer::init();

        // Scan textures and models
        discoverTextures();
        discoverModels();

        // Load default or facility map
        std::string targetMap = "assets/maps/facility_alpha.labmap";
        if (std::filesystem::exists(targetMap)) {
            _map = LabMap::loadFromFile(targetMap);
            if (_map) _currentMapPath = targetMap;
        }
        if (!_map) {
            newMap();
        }

        // Check weapon assets once at startup
        cacheWeaponAssets();

        // Camera initial pose
        _camera.setPosition(Vec3(0, 8.0f, 18.0f));

        // Unlock mouse cursor for UI desktop interaction
        glfwSetInputMode(getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        logMessage("Hammer initialized. Ready.");
        logMessage("Textures loaded: " + std::to_string(_availableTextures.size()) + " | Models: " + std::to_string(_availableModels.size()));
        logMessage("Frustum Culling active: 'To czego oko nie widzi tego maszyna renderowac nie musi'");
    }

    void discoverTextures() {
        _availableTextures.clear();
        std::vector<std::string> searchDirs = { "assets/textures", "../assets/textures", "../../assets/textures" };
        for (const auto& dir : searchDirs) {
            if (std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.path().extension() == ".bmp") {
                        std::string fname = entry.path().filename().string();
                        _availableTextures.push_back({ fname, fname });
                        if (!_textures.contains(fname)) {
                            _textures[fname] = std::make_unique<Texture>(fname);
                        }
                    }
                }
                if (!_availableTextures.empty()) break;
            }
        }
        if (_availableTextures.empty()) {
            _availableTextures.push_back({ "concrete_wall.bmp", "concrete_wall.bmp" });
            _textures["concrete_wall.bmp"] = std::make_unique<Texture>("concrete_wall.bmp");
        }
        _selectedTexture = _availableTextures[0].filename;
    }

    void discoverModels() {
        _availableModels.clear();
        std::vector<std::string> searchDirs = { "assets/models", "../assets/models", "../../assets/models" };
        for (const auto& dir : searchDirs) {
            if (std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".stl" || ext == ".STL") {
                        std::string fname = entry.path().filename().string();
                        _availableModels.push_back(fname);
                        if (!_meshes.contains(fname)) {
                            Mesh* m = Mesh::loadSTL(fname);
                            if (m) _meshes[fname] = std::unique_ptr<Mesh>(m);
                        }
                    }
                }
                if (!_availableModels.empty()) break;
            }
        }
        if (_availableModels.empty()) {
            _availableModels.push_back("Model.stl");
        }
        _selectedModel = _availableModels[0];
    }

    Texture* getTexture(const std::string& path) {
        if (path.empty()) return nullptr;
        auto it = _textures.find(path);
        if (it != _textures.end()) return it->second.get();
        if (_missingTextures.contains(path)) return nullptr;

        auto tex = std::make_unique<Texture>(path);
        if (tex && tex->getId() != 0) {
            Texture* ptr = tex.get();
            _textures[path] = std::move(tex);
            return ptr;
        }
        _missingTextures.insert(path);
        return nullptr;
    }

    Mesh* getMesh(const std::string& path) {
        if (path.empty()) return nullptr;
        auto it = _meshes.find(path);
        if (it != _meshes.end()) return it->second.get();
        if (_missingMeshes.contains(path)) return nullptr;

        Mesh* m = Mesh::loadSTL(path);
        if (m) {
            Mesh* ptr = m;
            _meshes[path] = std::unique_ptr<Mesh>(m);
            return ptr;
        }
        _missingMeshes.insert(path);
        return nullptr;
    }

    void cacheWeaponAssets() {
        const char* stlNames[9] = {
            "assets/models/pipe.stl", "assets/models/pistol.stl", "assets/models/shotgun.stl",
            "assets/models/m4a4s.stl", "assets/models/sg553.stl", "assets/models/minigun.stl",
            "assets/models/plasma.stl", "assets/models/railgun.stl", "assets/models/rpg.stl"
        };
        const char* texFallbacks[9] = {
            "weapon_pipe.bmp", "weapon_pistol.bmp", "weapon_shotgun.bmp",
            "weapon_m4a4s.bmp", "weapon_sg553.bmp", "weapon_minigun.bmp",
            "weapon_plasma.bmp", "weapon_railgun.bmp", "weapon_rpg.bmp"
        };
        int customCount = 0;
        for (int i = 0; i < 9; ++i) {
            _weaponMeshes[i] = getMesh(stlNames[i]);
            std::string texName = Renderer::resolveModelTexture(stlNames[i], texFallbacks[i]);
            _weaponTextures[i] = getTexture(texName);
            if (_weaponMeshes[i]) customCount++;
        }
        logMessage("Weapons check-once: " + std::to_string(customCount) + " STL, " + std::to_string(9 - customCount) + " built-in procedural.");
    }

    void logMessage(const std::string& msg) {
        _consoleMessages.push_back(msg);
        if (_consoleMessages.size() > 8) {
            _consoleMessages.erase(_consoleMessages.begin());
        }
    }

    void onFixedUpdate(float fixedDelta) override {
        // Noclip camera flight (active when holding Right Mouse Button)
        if (Input::isMouseButtonPressed(1)) {
            bool isFast = Input::isKeyPressed(340); // Shift
            float flySpeed = isFast ? 35.0f : 15.0f;

            Vec3 camPos = _camera.getPosition();
            if (Input::isKeyPressed('W') || Input::isKeyPressed('w')) camPos += _camera.getFront() * flySpeed * fixedDelta;
            if (Input::isKeyPressed('S') || Input::isKeyPressed('s')) camPos -= _camera.getFront() * flySpeed * fixedDelta;
            if (Input::isKeyPressed('A') || Input::isKeyPressed('a')) camPos -= _camera.getRight() * flySpeed * fixedDelta;
            if (Input::isKeyPressed('D') || Input::isKeyPressed('d')) camPos += _camera.getRight() * flySpeed * fixedDelta;
            if (Input::isKeyPressed(32)) camPos.y += flySpeed * fixedDelta; // Space = Up
            if (Input::isKeyPressed(341)) camPos.y -= flySpeed * fixedDelta; // Ctrl = Down

            _camera.setPosition(camPos);
        }
    }

    void newMap() {
        _map = std::make_unique<LabMap>();
        _map->metadata.name = "untitled_map";
        _map->metadata.author = "Mapper";
        _map->spawn.position = Vec3(0, 1.8f, 0);

        // Standard ground brush
        MapBrush floor;
        floor.position = Vec3(0, -0.5f, 0);
        floor.size = Vec3(32.0f, 1.0f, 32.0f);
        floor.color = Vec3(1.0f, 1.0f, 1.0f);
        floor.texturePath = "floor_tiles.bmp";
        floor.uvScale = Vec2(0.25f, 0.25f);
        floor.uvMode = 1;
        _map->brushes.push_back(floor);

        _currentMapPath = "";
        _selectionType = SelectionType::None;
        _selectedIndex = -1;
        logMessage("Created New Map.");
    }

    void openMapDialog() {
        std::string openPath = LabDialogs::openFileDialog(getWindow(), "Lab Map Files (*.labmap)\0*.labmap\0All Files (*.*)\0*.*\0", "assets\\maps");
        if (!openPath.empty()) {
            auto loaded = LabMap::loadFromFile(openPath);
            if (loaded) {
                _map = std::move(loaded);
                _currentMapPath = openPath;
                _selectionType = SelectionType::None;
                _selectedIndex = -1;
                logMessage("Loaded Map: " + openPath);
            }
        }
    }

    void saveMapAction(bool forceSaveAs = false) {
        if (!_map) return;
        if (forceSaveAs || _currentMapPath.empty()) {
            std::string savePath = LabDialogs::saveFileDialog(getWindow(), "Lab Map Files (*.labmap)\0*.labmap\0All Files (*.*)\0*.*\0", "my_level.labmap", "assets\\maps");
            if (!savePath.empty()) {
                _currentMapPath = savePath;
                _map->saveToFile(_currentMapPath);
                logMessage("Map saved to: " + _currentMapPath);
            }
        } else {
            _map->saveToFile(_currentMapPath);
            logMessage("Map saved to: " + _currentMapPath);
        }
    }

    void runInEngine() {
        if (!_map) return;
        std::string runPath = _currentMapPath.empty() ? "assets/maps/hammer_run.labmap" : _currentMapPath;
        _map->saveToFile(runPath);
        logMessage("Saved map for engine: " + runPath);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        char cmdLine[256] = "Lab.exe";
        if (CreateProcessA("Lab.exe", cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) ||
            CreateProcessA("Release\\Lab.exe", cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            logMessage("Launched Lab.exe in Game Engine!");
        } else {
            system("start Lab.exe");
            logMessage("Launched Lab.exe via shell!");
        }
    }

    void openModelDialog() {
        std::string picked = LabDialogs::openFileDialog(getWindow(), "3D STL Model (*.stl)\0*.stl\0All Files (*.*)\0*.*\0", "assets\\models");
        if (!picked.empty()) {
            std::string fname = std::filesystem::path(picked).filename().string();
            if (!_meshes.contains(fname)) {
                Mesh* m = Mesh::loadSTL(picked);
                if (m) {
                    _meshes[fname] = std::unique_ptr<Mesh>(m);
                    _availableModels.push_back(fname);
                    _selectedModel = fname;
                    logMessage("Loaded Model from disk: " + fname);
                } else {
                    logMessage("Failed to load model: " + picked);
                }
            } else {
                _selectedModel = fname;
                logMessage("Selected Model: " + fname);
            }
            _modelBrowserOpen = false;
        }
    }

    void onUpdate(const Time& time) override {
        (void)time;

        // Mouse look in 3D Viewport when holding Right Mouse Button
        if (Input::isMouseButtonPressed(1)) {
            _camera.update(Input::mouseDelta);
        }

        // Snap 3D cursor to grid
        _cursorPos = snapToGrid(_camera.getPosition() + _camera.getFront() * 10.0f, _gridSnap);

        // Compute coordinate scaling between screen window coordinates and framebuffer
        int winW = 0, winH = 0;
        glfwGetWindowSize(getWindow(), &winW, &winH);
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);

        float mouseScaleX = (winW > 0 && fbW > 0) ? ((float)fbW / (float)winW) : 1.0f;
        float mouseScaleY = (winH > 0 && fbH > 0) ? ((float)fbH / (float)winH) : 1.0f;

        float mx = Input::mousePos.x * mouseScaleX;
        float my = Input::mousePos.y * mouseScaleY;

        // Handle Left-Click
        if (Input::isMouseButtonPressed(0)) {
            if (!_lmbPressed) {
                handleMouseClick(mx, my, false);
                _lmbPressed = true;
            }
        } else {
            _lmbPressed = false;
        }

        // Handle Right-Click (for Tool 3 Pipette sample)
        if (Input::isMouseButtonPressed(1)) {
            if (!_rmbPressed) {
                if (_activeTool == 3) {
                    handleMouseClick(mx, my, true);
                }
                _rmbPressed = true;
            }
        } else {
            _rmbPressed = false;
        }

        // Keyboard Shortcuts
        bool ctrlDown = Input::isKeyPressed(341) || Input::isKeyPressed(345); // Left/Right Ctrl
        bool shiftDown = Input::isKeyPressed(340) || Input::isKeyPressed(344);

        // F9: Run in Engine
        if (Input::isKeyPressed(298)) { // GLFW_KEY_F9
            if (!_f9Pressed) {
                runInEngine();
                _f9Pressed = true;
            }
        } else {
            _f9Pressed = false;
        }

        // Ctrl+N: New Map
        if (ctrlDown && (Input::isKeyPressed('N') || Input::isKeyPressed('n'))) {
            if (!_ctrlNPressed) {
                newMap();
                _ctrlNPressed = true;
            }
        } else {
            _ctrlNPressed = false;
        }

        // Ctrl+O: Open Map
        if (ctrlDown && (Input::isKeyPressed('O') || Input::isKeyPressed('o'))) {
            if (!_ctrlOPressed) {
                openMapDialog();
                _ctrlOPressed = true;
            }
        } else {
            _ctrlOPressed = false;
        }

        // Ctrl+S: Save Map
        if (ctrlDown && (Input::isKeyPressed('S') || Input::isKeyPressed('s'))) {
            if (!_ctrlSPressed) {
                saveMapAction(false);
                _ctrlSPressed = true;
            }
        } else {
            _ctrlSPressed = false;
        }

        // Ctrl+D: Duplicate Selection
        if (ctrlDown && (Input::isKeyPressed('D') || Input::isKeyPressed('d'))) {
            if (!_ctrlDPressed) {
                duplicateSelection();
                _ctrlDPressed = true;
            }
        } else {
            _ctrlDPressed = false;
        }

        // F: Focus camera on selection
        if (!ctrlDown && (Input::isKeyPressed('F') || Input::isKeyPressed('f'))) {
            if (!_fPressed) {
                focusCamera();
                _fPressed = true;
            }
        } else {
            _fPressed = false;
        }

        // E: Place Brush / Prop / Door / Spawn
        if (!ctrlDown && (Input::isKeyPressed('E') || Input::isKeyPressed('e'))) {
            if (!_ePressed && _map) {
                placeCurrentObject();
                _ePressed = true;
            }
        } else {
            _ePressed = false;
        }

        // X / Shift+X: Toggle Clip Tool / Cycle Clip Mode
        if (!ctrlDown && (Input::isKeyPressed('X') || Input::isKeyPressed('x'))) {
            if (!_xPressed) {
                if (_activeTool != 6) {
                    _activeTool = 6;
                    logMessage("CSG Clip Tool Active (X / Shift+X cycles mode, ENTER to commit)");
                } else {
                    _clipMode = (ClipMode)(((int)_clipMode + 1) % 3);
                    if (_clipMode == ClipMode::KeepFront) {
                        logMessage("CSG Clip Mode: Keep Front (Positive Side)");
                    } else if (_clipMode == ClipMode::KeepBack) {
                        logMessage("CSG Clip Mode: Keep Back (Negative Side)");
                    } else {
                        logMessage("CSG Clip Mode: Keep Both (Split into 2 Brushes)");
                    }
                }
                _xPressed = true;
            }
        } else {
            _xPressed = false;
        }

        // Enter: Commit CSG Clip
        if (Input::isKeyPressed(257)) { // GLFW_KEY_ENTER
            if (!_enterPressed) {
                if (_activeTool == 6) {
                    commitClip();
                }
                _enterPressed = true;
            }
        } else {
            _enterPressed = false;
        }

        // Backspace / Delete: Delete selected object (or last brush if none selected)
        if (Input::isKeyPressed(259) || Input::isKeyPressed(261)) { // Backspace or Del
            if (!_delPressed) {
                if (_selectionType != SelectionType::None) {
                    deleteSelection();
                } else if (_map && !_map->brushes.empty()) {
                    _map->brushes.pop_back();
                    logMessage("Undo: Deleted last brush.");
                }
                _delPressed = true;
            }
        } else {
            _delPressed = false;
        }

        // Arrow Keys / PageUp / PageDown: Move selected object on grid
        if (_selectionType != SelectionType::None && !Input::isMouseButtonPressed(1)) {
            if (Input::isKeyPressed(263)) { // Left
                if (!_arrowLeftPressed) { moveSelection(-_gridSnap, 0, 0); _arrowLeftPressed = true; }
            } else _arrowLeftPressed = false;

            if (Input::isKeyPressed(262)) { // Right
                if (!_arrowRightPressed) { moveSelection(_gridSnap, 0, 0); _arrowRightPressed = true; }
            } else _arrowRightPressed = false;

            if (Input::isKeyPressed(265)) { // Up
                if (!_arrowUpPressed) {
                    if (shiftDown) moveSelection(0, _gridSnap, 0);
                    else moveSelection(0, 0, -_gridSnap);
                    _arrowUpPressed = true;
                }
            } else _arrowUpPressed = false;

            if (Input::isKeyPressed(264)) { // Down
                if (!_arrowDownPressed) {
                    if (shiftDown) moveSelection(0, -_gridSnap, 0);
                    else moveSelection(0, 0, _gridSnap);
                    _arrowDownPressed = true;
                }
            } else _arrowDownPressed = false;

            if (Input::isKeyPressed(266)) { // PageUp
                if (!_pageUpPressed) { moveSelection(0, _gridSnap, 0); _pageUpPressed = true; }
            } else _pageUpPressed = false;

            if (Input::isKeyPressed(267)) { // PageDown
                if (!_pageDownPressed) { moveSelection(0, -_gridSnap, 0); _pageDownPressed = true; }
            } else _pageDownPressed = false;
        }
    }

    static Vec3 snapToGrid(const Vec3& v, float snap) {
        if (snap <= 0.001f) return v;
        return Vec3(
            std::round(v.x / snap) * snap,
            std::round(v.y / snap) * snap,
            std::round(v.z / snap) * snap
        );
    }

    // Fast Ray-AABB intersection
    static bool rayIntersectAABB(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& boxMin, const Vec3& boxMax, float& tOut) {
        float tmin = 0.001f;
        float tmax = 10000.0f;

        // X slab
        if (std::abs(rayDir.x) < 1e-6f) {
            if (rayOrigin.x < boxMin.x || rayOrigin.x > boxMax.x) return false;
        } else {
            float invD = 1.0f / rayDir.x;
            float t1 = (boxMin.x - rayOrigin.x) * invD;
            float t2 = (boxMax.x - rayOrigin.x) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        // Y slab
        if (std::abs(rayDir.y) < 1e-6f) {
            if (rayOrigin.y < boxMin.y || rayOrigin.y > boxMax.y) return false;
        } else {
            float invD = 1.0f / rayDir.y;
            float t1 = (boxMin.y - rayOrigin.y) * invD;
            float t2 = (boxMax.y - rayOrigin.y) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        // Z slab
        if (std::abs(rayDir.z) < 1e-6f) {
            if (rayOrigin.z < boxMin.z || rayOrigin.z > boxMax.z) return false;
        } else {
            float invD = 1.0f / rayDir.z;
            float t1 = (boxMin.z - rayOrigin.z) * invD;
            float t2 = (boxMax.z - rayOrigin.z) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        tOut = tmin;
        return true;
    }

    void pickObjectInViewport(float mx, float my, bool isRmb = false) {
        if (!_map) return;
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        SidebarLayout l = getSidebarLayout(w, h);
        float vpX = 42.0f;
        float vpY = 58.0f;
        float vpW = l.rightX - vpX;
        float vpH = (h - 22.0f) - vpY;
        if (mx < vpX || mx > vpX + vpW || my < vpY || my > vpY + vpH) return;

        float ndcX = ((mx - vpX) / vpW) * 2.0f - 1.0f;
        float ndcY = 1.0f - ((my - vpY) / vpH) * 2.0f;
        float aspect = vpW / vpH;
        float tanHalfFov = std::tan((70.0f * 0.5f) * 3.14159265f / 180.0f);
        Vec3 rayDir = (_camera.getFront() + _camera.getRight() * (ndcX * tanHalfFov * aspect) + _camera.getUp() * (ndcY * tanHalfFov)).normalized();
        Vec3 rayOrigin = _camera.getPosition();

        float closestT = 1e9f;
        SelectionType hitType = SelectionType::None;
        int hitIndex = -1;

        // Test Brushes
        for (size_t i = 0; i < _map->brushes.size(); ++i) {
            const auto& b = _map->brushes[i];
            Vec3 half = b.size * 0.5f;
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, b.position - half, b.position + half, t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::Brush;
                    hitIndex = (int)i;
                }
            }
        }

        // Test Props
        for (size_t i = 0; i < _map->props.size(); ++i) {
            const auto& p = _map->props[i];
            Vec3 half = p.scale * 0.5f;
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, p.position - half, p.position + half, t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::Prop;
                    hitIndex = (int)i;
                }
            }
        }

        // Test Doors
        for (size_t i = 0; i < _map->doors.size(); ++i) {
            const auto& d = _map->doors[i];
            Vec3 half = d.size * 0.5f;
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, d.position - half, d.position + half, t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::Door;
                    hitIndex = (int)i;
                }
            }
        }

        // Test Spawns
        for (size_t i = 0; i < _map->spawnPoints.size(); ++i) {
            const auto& sp = _map->spawnPoints[i];
            Vec3 half(0.6f, 0.9f, 0.6f);
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, sp.position - half, sp.position + half, t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::Spawn;
                    hitIndex = (int)i;
                }
            }
        }

        // Test Weapon Spawners
        for (size_t i = 0; i < _map->weaponSpawners.size(); ++i) {
            const auto& ws = _map->weaponSpawners[i];
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, ws.position - Vec3(0.7f, 0.0f, 0.7f), ws.position + Vec3(0.7f, 0.8f, 0.7f), t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::WeaponSpawner;
                    hitIndex = (int)i;
                }
            }
        }

        // Tool 3: Texture pipette & application
        if (_activeTool == 3) {
            if (hitType == SelectionType::Brush) {
                if (isRmb) {
                    _selectedTexture = _map->brushes[hitIndex].texturePath;
                    logMessage("Pipette: Sampled texture '" + _selectedTexture + "' from Brush #" + std::to_string(hitIndex));
                } else {
                    _map->brushes[hitIndex].texturePath = _selectedTexture;
                    _map->brushes[hitIndex].uvScale = _activeUvScale;
                    logMessage("Applied texture '" + _selectedTexture + "' to Brush #" + std::to_string(hitIndex));
                }
            }
            return;
        }

        // Tool 6: CSG Clip Tool
        if (_activeTool == 6) {
            if (hitType == SelectionType::Brush) {
                _selectionType = SelectionType::Brush;
                _selectedIndex = hitIndex;
                const auto& b = _map->brushes[hitIndex];
                Vec3 half = b.size * 0.5f;
                _clipPointA = b.position + Vec3(-half.x * 1.2f, 0.0f, -half.z * 0.6f);
                _clipPointB = b.position + Vec3(half.x * 1.2f, 0.0f, half.z * 0.6f);
                logMessage("Clip Tool: Selected Brush #" + std::to_string(hitIndex) + " for CSG Slicing");
            } else {
                if (_clipStep == 0) {
                    _clipPointA = _cursorPos;
                    _clipStep = 1;
                    logMessage("Clip Tool: Set Cut Point A to (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
                } else {
                    _clipPointB = _cursorPos;
                    _clipStep = 0;
                    logMessage("Clip Tool: Set Cut Point B to (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
                }
            }
            return;
        }

        // Tool 0: Selection
        if (_activeTool == 0) {
            _selectionType = hitType;
            _selectedIndex = hitIndex;
            if (_selectionType == SelectionType::Brush) {
                _selectedTexture = _map->brushes[hitIndex].texturePath;
                logMessage("Selected Brush #" + std::to_string(hitIndex) + " (" + _selectedTexture + ")");
            } else if (_selectionType == SelectionType::Prop) {
                _selectedModel = _map->props[hitIndex].modelPath;
                logMessage("Selected Prop #" + std::to_string(hitIndex) + " (" + _selectedModel + ")");
            } else if (_selectionType == SelectionType::Door) {
                logMessage("Selected Door #" + std::to_string(hitIndex) + " (" + _map->doors[hitIndex].name + ")");
            } else if (_selectionType == SelectionType::Spawn) {
                logMessage("Selected " + _map->spawnPoints[hitIndex].getDisplayName() + " #" + std::to_string(hitIndex));
            } else if (_selectionType == SelectionType::WeaponSpawner) {
                logMessage("Selected Weapon Spawner [" + _map->weaponSpawners[hitIndex].getWeaponName() + "] #" + std::to_string(hitIndex));
            } else {
                logMessage("Deselected all");
            }
        }
    }

    void moveSelection(float dx, float dy, float dz) {
        if (!_map) return;
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            _map->brushes[_selectedIndex].position += Vec3(dx, dy, dz);
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            _map->props[_selectedIndex].position += Vec3(dx, dy, dz);
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            _map->doors[_selectedIndex].position += Vec3(dx, dy, dz);
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            _map->spawnPoints[_selectedIndex].position += Vec3(dx, dy, dz);
            _map->spawn.position = _map->spawnPoints[0].position;
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            _map->weaponSpawners[_selectedIndex].position += Vec3(dx, dy, dz);
        }
    }

    void resizeSelection(float dw, float dh, float dd) {
        if (!_map) return;
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            auto& b = _map->brushes[_selectedIndex];
            b.size.x = std::max(0.5f, b.size.x + dw);
            b.size.y = std::max(0.5f, b.size.y + dh);
            b.size.z = std::max(0.5f, b.size.z + dd);
            logMessage("Resized Brush #" + std::to_string(_selectedIndex) + " to (" + 
                       std::to_string((int)b.size.x) + "x" + std::to_string((int)b.size.y) + "x" + std::to_string((int)b.size.z) + ")");
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            auto& p = _map->props[_selectedIndex];
            p.scale.x = std::max(0.2f, p.scale.x + dw);
            p.scale.y = std::max(0.2f, p.scale.y + dh);
            p.scale.z = std::max(0.2f, p.scale.z + dd);
            logMessage("Rescaled Prop #" + std::to_string(_selectedIndex) + " to (" + 
                       std::to_string((int)p.scale.x) + "x" + std::to_string((int)p.scale.y) + "x" + std::to_string((int)p.scale.z) + ")");
        }
    }

    void duplicateSelection() {
        if (!_map) return;
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            MapBrush b = _map->brushes[_selectedIndex];
            b.position.x += _gridSnap;
            b.position.z += _gridSnap;
            _map->brushes.push_back(b);
            _selectedIndex = (int)_map->brushes.size() - 1;
            logMessage("Duplicated Brush to #" + std::to_string(_selectedIndex));
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            MapProp p = _map->props[_selectedIndex];
            p.position.x += _gridSnap;
            p.position.z += _gridSnap;
            _map->props.push_back(p);
            _selectedIndex = (int)_map->props.size() - 1;
            logMessage("Duplicated Prop to #" + std::to_string(_selectedIndex));
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            MapDoor d = _map->doors[_selectedIndex];
            d.position.x += _gridSnap;
            d.position.z += _gridSnap;
            _map->doors.push_back(d);
            _selectedIndex = (int)_map->doors.size() - 1;
            logMessage("Duplicated Door to #" + std::to_string(_selectedIndex));
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            MapSpawnPoint sp = _map->spawnPoints[_selectedIndex];
            sp.position.x += _gridSnap;
            sp.position.z += _gridSnap;
            _map->spawnPoints.push_back(sp);
            _selectedIndex = (int)_map->spawnPoints.size() - 1;
            logMessage("Duplicated " + sp.getDisplayName() + " to #" + std::to_string(_selectedIndex));
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            MapWeaponSpawner ws = _map->weaponSpawners[_selectedIndex];
            ws.position.x += _gridSnap;
            ws.position.z += _gridSnap;
            _map->weaponSpawners.push_back(ws);
            _selectedIndex = (int)_map->weaponSpawners.size() - 1;
            logMessage("Duplicated Weapon Spawner [" + ws.getWeaponName() + "] to #" + std::to_string(_selectedIndex));
        }
    }

    void deleteSelection() {
        if (!_map) return;
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            _map->brushes.erase(_map->brushes.begin() + _selectedIndex);
            logMessage("Deleted Brush #" + std::to_string(_selectedIndex));
            _selectionType = SelectionType::None;
            _selectedIndex = -1;
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            _map->props.erase(_map->props.begin() + _selectedIndex);
            logMessage("Deleted Prop #" + std::to_string(_selectedIndex));
            _selectionType = SelectionType::None;
            _selectedIndex = -1;
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            _map->doors.erase(_map->doors.begin() + _selectedIndex);
            logMessage("Deleted Door #" + std::to_string(_selectedIndex));
            _selectionType = SelectionType::None;
            _selectedIndex = -1;
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            if (_map->spawnPoints.size() > 1) {
                _map->spawnPoints.erase(_map->spawnPoints.begin() + _selectedIndex);
                logMessage("Deleted Spawn Point #" + std::to_string(_selectedIndex));
                _selectionType = SelectionType::None;
                _selectedIndex = -1;
            } else {
                logMessage("Cannot delete the last remaining spawn point!");
            }
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            _map->weaponSpawners.erase(_map->weaponSpawners.begin() + _selectedIndex);
            logMessage("Deleted Weapon Spawner #" + std::to_string(_selectedIndex));
            _selectionType = SelectionType::None;
            _selectedIndex = -1;
        }
    }

    void focusCamera() {
        Vec3 targetPos = _cursorPos;
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            targetPos = _map->brushes[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            targetPos = _map->props[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            targetPos = _map->doors[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            targetPos = _map->spawnPoints[_selectedIndex].position;
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            targetPos = _map->weaponSpawners[_selectedIndex].position;
        }
        _camera.setPosition(targetPos - _camera.getFront() * 10.0f);
        logMessage("Focused Camera on selection at (" + std::to_string((int)targetPos.x) + ", " + std::to_string((int)targetPos.y) + ", " + std::to_string((int)targetPos.z) + ")");
    }

    void commitClip() {
        if (!_map) return;
        if (_selectionType != SelectionType::Brush || _selectedIndex < 0 || _selectedIndex >= (int)_map->brushes.size()) {
            logMessage("CSG Clip: Please select a brush to clip first!");
            return;
        }

        auto& targetBrush = _map->brushes[_selectedIndex];
        Vec3 planePt = (_clipPointA + _clipPointB) * 0.5f;
        Vec3 planeNormal;
        Vec2 p1(_clipPointA.x, _clipPointA.z);
        Vec2 p2(_clipPointB.x, _clipPointB.z);
        CSGTool::makePlaneFrom2DLine(p1, p2, planePt, planeNormal);

        if (planeNormal.lengthSq() < 1e-4f) {
            planeNormal = Vec3(1.0f, 0.0f, 0.0f);
            planePt = targetBrush.position;
        }

        ClipResult res;
        if (targetBrush.type == "poly" && !targetBrush.customVertices.empty()) {
            res = CSGTool::sliceConvexMesh(targetBrush.customVertices, targetBrush.customIndices, planePt, planeNormal, targetBrush.uvScale);
        } else {
            Vec3 half = targetBrush.size * 0.5f;
            Vec3 bMin = targetBrush.position - half;
            Vec3 bMax = targetBrush.position + half;
            res = CSGTool::sliceBox(bMin, bMax, planePt, planeNormal, targetBrush.uvScale);
        }

        if (!res.didIntersect) {
            logMessage("CSG Clip: Plane did not intersect the selected brush.");
            return;
        }

        std::string tex = targetBrush.texturePath;
        Vec2 uvScale = targetBrush.uvScale;
        Vec3 col = targetBrush.color;

        if (_clipMode == ClipMode::KeepFront) {
            if (!res.frontPiece.isValid()) {
                logMessage("CSG Clip: Front piece empty.");
                return;
            }
            targetBrush.type = "poly";
            targetBrush.position = res.frontPiece.centroid;
            targetBrush.size = res.frontPiece.aabbMax - res.frontPiece.aabbMin;
            targetBrush.customVertices = res.frontPiece.vertices;
            targetBrush.customIndices = res.frontPiece.indices;
            targetBrush.runtimeMesh = nullptr;
            logMessage("CSG Clip: Brush clipped (Keep Front, " + std::to_string(res.frontPiece.triangleCount) + " tris)");
        } else if (_clipMode == ClipMode::KeepBack) {
            if (!res.backPiece.isValid()) {
                logMessage("CSG Clip: Back piece empty.");
                return;
            }
            targetBrush.type = "poly";
            targetBrush.position = res.backPiece.centroid;
            targetBrush.size = res.backPiece.aabbMax - res.backPiece.aabbMin;
            targetBrush.customVertices = res.backPiece.vertices;
            targetBrush.customIndices = res.backPiece.indices;
            targetBrush.runtimeMesh = nullptr;
            logMessage("CSG Clip: Brush clipped (Keep Back, " + std::to_string(res.backPiece.triangleCount) + " tris)");
        } else { // KeepBoth
            if (!res.frontPiece.isValid() || !res.backPiece.isValid()) {
                logMessage("CSG Clip: Could not split both pieces.");
                return;
            }
            targetBrush.type = "poly";
            targetBrush.position = res.frontPiece.centroid;
            targetBrush.size = res.frontPiece.aabbMax - res.frontPiece.aabbMin;
            targetBrush.customVertices = res.frontPiece.vertices;
            targetBrush.customIndices = res.frontPiece.indices;
            targetBrush.runtimeMesh = nullptr;

            MapBrush backB;
            backB.type = "poly";
            backB.position = res.backPiece.centroid;
            backB.size = res.backPiece.aabbMax - res.backPiece.aabbMin;
            backB.color = col;
            backB.texturePath = tex;
            backB.uvScale = uvScale;
            backB.uvMode = 1;
            backB.customVertices = res.backPiece.vertices;
            backB.customIndices = res.backPiece.indices;
            _map->brushes.push_back(backB);

            logMessage("CSG Clip: Split brush into 2 convex brushes (Front: " +
                       std::to_string(res.frontPiece.triangleCount) + " tris, Back: " +
                       std::to_string(res.backPiece.triangleCount) + " tris)");
        }
    }

    void drawClipToolOverlay() {
        if (!_map) return;
        Vec3 planePt = (_clipPointA + _clipPointB) * 0.5f;
        Vec3 planeNormal;
        Vec2 p1(_clipPointA.x, _clipPointA.z);
        Vec2 p2(_clipPointB.x, _clipPointB.z);
        CSGTool::makePlaneFrom2DLine(p1, p2, planePt, planeNormal);

        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            const auto& b = _map->brushes[_selectedIndex];
            Vec3 half = b.size * 0.5f;
            if ((_clipPointA - _clipPointB).lengthSq() < 1e-4f) {
                _clipPointA = b.position + Vec3(-half.x * 1.2f, 0.0f, -half.z * 0.5f);
                _clipPointB = b.position + Vec3(half.x * 1.2f, 0.0f, half.z * 0.5f);
                p1 = Vec2(_clipPointA.x, _clipPointA.z);
                p2 = Vec2(_clipPointB.x, _clipPointB.z);
                CSGTool::makePlaneFrom2DLine(p1, p2, planePt, planeNormal);
            }

            Vec3 modeCol = (_clipMode == ClipMode::KeepFront) ? Vec3(0.2f, 0.85f, 1.0f) :
                           (_clipMode == ClipMode::KeepBack)  ? Vec3(1.0f, 0.45f, 0.2f) :
                                                                Vec3(1.0f, 0.92f, 0.3f);

            Renderer::drawCube(_clipPointA, Vec3(0.25f, 0.25f, 0.25f), Vec3(0.2f, 0.9f, 1.0f), nullptr, false);
            Renderer::drawWireCube(_clipPointA, Vec3(0.28f, 0.28f, 0.28f), Vec3(1, 1, 1));
            Renderer::drawCube(_clipPointB, Vec3(0.25f, 0.25f, 0.25f), Vec3(1.0f, 0.5f, 0.2f), nullptr, false);
            Renderer::drawWireCube(_clipPointB, Vec3(0.28f, 0.28f, 0.28f), Vec3(1, 1, 1));

            Vec3 diff = _clipPointB - _clipPointA;
            float len = diff.length();
            if (len > 0.01f) {
                float h = std::max(b.size.y * 1.4f, 2.0f);
                int steps = 14;
                for (int s = -2; s <= steps + 2; ++s) {
                    float t = (float)s / (float)steps;
                    Vec3 pt = _clipPointA + diff * t;
                    pt.y = b.position.y;
                    Renderer::drawCube(pt, Vec3(0.08f, h, 0.08f), modeCol * 0.85f, nullptr, false);
                }
                Vec3 normStart = planePt;
                normStart.y = b.position.y;
                Vec3 normEnd = normStart + planeNormal * 1.5f;
                Renderer::drawCube((normStart + normEnd) * 0.5f, Vec3(0.08f, 0.08f, 1.5f), modeCol, nullptr, false);
                Renderer::drawCube(normEnd, Vec3(0.2f, 0.2f, 0.2f), Vec3(1, 1, 1), nullptr, false);
            }
        }

        Renderer::drawWireCube(_cursorPos, Vec3(1.0f, 1.0f, 1.0f), Vec3(0.9f, 0.9f, 0.3f));
        drawGizmo(_cursorPos);
    }

    void placePrebuilt(int prebuiltId) {
        if (!_map) return;
        switch (prebuiltId) {
            case 0: { // Spawn: FFA / DM (Neutral)
                MapSpawnPoint sp;
                sp.entityClass = "info_player_deathmatch";
                sp.position = _cursorPos;
                sp.yaw = 0.0f;
                sp.type = SpawnType::FFA;
                _map->spawnPoints.push_back(sp);
                _selectionType = SelectionType::Spawn;
                _selectedIndex = (int)_map->spawnPoints.size() - 1;
                logMessage("Placed Prebuilt: Spawn [FFA / DM] at (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
                break;
            }
            case 1: { // Spawn: Team Alpha (Blue)
                MapSpawnPoint sp;
                sp.entityClass = "info_player_team1";
                sp.position = _cursorPos;
                sp.yaw = 0.0f;
                sp.type = SpawnType::TeamAlpha;
                _map->spawnPoints.push_back(sp);
                _selectionType = SelectionType::Spawn;
                _selectedIndex = (int)_map->spawnPoints.size() - 1;
                logMessage("Placed Prebuilt: Spawn [Team Alpha - Blue HQ] at (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
                break;
            }
            case 2: { // Spawn: Team Beta (Red)
                MapSpawnPoint sp;
                sp.entityClass = "info_player_team2";
                sp.position = _cursorPos;
                sp.yaw = 180.0f;
                sp.type = SpawnType::TeamBeta;
                _map->spawnPoints.push_back(sp);
                _selectionType = SelectionType::Spawn;
                _selectedIndex = (int)_map->spawnPoints.size() - 1;
                logMessage("Placed Prebuilt: Spawn [Team Beta - Red HQ] at (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
                break;
            }
            case 3: { // Prebuilt: Skrzynka Amunicji
                MapBrush b;
                b.type = "cube";
                b.position = _cursorPos + Vec3(0, 0.25f, 0);
                b.size = Vec3(0.8f, 0.5f, 0.5f);
                b.color = Vec3(0.2f, 0.45f, 0.22f);
                b.texturePath = "hazard_stripes.bmp";
                b.uvScale = Vec2(0.5f, 0.5f);
                b.uvMode = 1;
                _map->brushes.push_back(b);
                _selectionType = SelectionType::Brush;
                _selectedIndex = (int)_map->brushes.size() - 1;
                logMessage("Placed Prebuilt: Skrzynka Amunicji");
                break;
            }
            case 4: { // Prebuilt: Apteczka Polowa
                MapBrush b;
                b.type = "cube";
                b.position = _cursorPos + Vec3(0, 0.25f, 0);
                b.size = Vec3(0.6f, 0.45f, 0.4f);
                b.color = Vec3(0.95f, 0.95f, 0.95f);
                b.texturePath = "";
                b.uvScale = Vec2(1.0f, 1.0f);
                b.uvMode = 1;
                _map->brushes.push_back(b);
                _selectionType = SelectionType::Brush;
                _selectedIndex = (int)_map->brushes.size() - 1;
                logMessage("Placed Prebuilt: Apteczka Polowa");
                break;
            }
            case 5: { // Prebuilt: Barykada Taktyczna
                MapBrush b;
                b.type = "cube";
                b.position = _cursorPos + Vec3(0, 0.6f, 0);
                b.size = Vec3(3.0f, 1.2f, 0.6f);
                b.color = Vec3(0.8f, 0.8f, 0.85f);
                b.texturePath = "concrete_wall.bmp";
                b.uvScale = Vec2(0.25f, 0.25f);
                b.uvMode = 1;
                _map->brushes.push_back(b);
                _selectionType = SelectionType::Brush;
                _selectedIndex = (int)_map->brushes.size() - 1;
                logMessage("Placed Prebuilt: Barykada Taktyczna (3x1.2)");
                break;
            }
            case 6: { // Prebuilt: Filar Betonowy
                MapBrush b;
                b.type = "cube";
                b.position = _cursorPos + Vec3(0, 3.0f, 0);
                b.size = Vec3(1.5f, 6.0f, 1.5f);
                b.color = Vec3(0.7f, 0.75f, 0.8f);
                b.texturePath = "concrete_wall.bmp";
                b.uvScale = Vec2(0.25f, 0.25f);
                b.uvMode = 1;
                _map->brushes.push_back(b);
                _selectionType = SelectionType::Brush;
                _selectedIndex = (int)_map->brushes.size() - 1;
                logMessage("Placed Prebuilt: Filar Betonowy (1.5x6)");
                break;
            }
            case 7: { // Prebuilt: Brama Bezpieczenstwa
                MapDoor d;
                d.name = "security_gate_" + std::to_string(_map->doors.size());
                d.position = _cursorPos + Vec3(0, 1.75f, 0);
                d.size = Vec3(3.5f, 3.5f, 0.4f);
                d.openOffset = Vec3(0.0f, 4.0f, 0.0f);
                d.color = Vec3(0.25f, 0.35f, 0.45f);
                d.openSpeed = 2.5f;
                d.triggerRadius = 5.0f;
                _map->doors.push_back(d);
                _selectionType = SelectionType::Door;
                _selectedIndex = (int)_map->doors.size() - 1;
                logMessage("Placed Prebuilt: Brama Bezpieczenstwa (Drzwi)");
                break;
            }
            case 8: { // Prebuilt: Drewniana Skrzynia Fizyczna (prop_crate)
                MapProp p;
                p.modelPath = "models/props/crate.obj";
                p.position = _cursorPos + Vec3(0, 0.45f, 0);
                p.scale = Vec3(0.9f, 0.9f, 0.9f);
                p.color = Vec3(0.65f, 0.5f, 0.35f);
                p.texturePath = "crate_wood.png";
                _map->props.push_back(p);
                _selectionType = SelectionType::Prop;
                _selectedIndex = (int)_map->props.size() - 1;
                logMessage("Placed Physical Prop: Drewniana Skrzynia (Havok Rigid Body)");
                break;
            }
            case 9: { // Prebuilt: Wybuchowa Beczka Paliwa (prop_barrel)
                MapProp p;
                p.modelPath = "models/props/barrel.obj";
                p.position = _cursorPos + Vec3(0, 0.48f, 0);
                p.scale = Vec3(0.64f, 0.96f, 0.64f);
                p.color = Vec3(0.9f, 0.2f, 0.2f);
                p.texturePath = "barrel_hazard.png";
                _map->props.push_back(p);
                _selectionType = SelectionType::Prop;
                _selectedIndex = (int)_map->props.size() - 1;
                logMessage("Placed Physical Prop: Wybuchowa Beczka Paliwa (Explosive Hazard)");
                break;
            }
            case 10: { // Spawner: Pipe
                MapWeaponSpawner ws;
                ws.weaponId = 0;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Pipe (60s Respawn)");
                break;
            }
            case 11: { // Spawner: Pistol
                MapWeaponSpawner ws;
                ws.weaponId = 1;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Pistol (60s Respawn)");
                break;
            }
            case 12: { // Spawner: Shotgun
                MapWeaponSpawner ws;
                ws.weaponId = 2;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Shotgun (60s Respawn)");
                break;
            }
            case 13: { // Spawner: M4A4-S
                MapWeaponSpawner ws;
                ws.weaponId = 3;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: M4A4-S (60s Respawn)");
                break;
            }
            case 14: { // Spawner: SG553
                MapWeaponSpawner ws;
                ws.weaponId = 4;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: SG553 (60s Respawn)");
                break;
            }
            case 15: { // Spawner: Minigun
                MapWeaponSpawner ws;
                ws.weaponId = 5;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Minigun (60s Respawn)");
                break;
            }
            case 16: { // Spawner: Plasma Gun
                MapWeaponSpawner ws;
                ws.weaponId = 6;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Plasma Gun (60s Respawn)");
                break;
            }
            case 17: { // Spawner: Railgun
                MapWeaponSpawner ws;
                ws.weaponId = 7;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: Railgun (60s Respawn)");
                break;
            }
            case 18: { // Spawner: RPG
                MapWeaponSpawner ws;
                ws.weaponId = 8;
                ws.position = _cursorPos;
                ws.yaw = 0.0f;
                ws.respawnTime = 60.0f;
                _map->weaponSpawners.push_back(ws);
                _selectionType = SelectionType::WeaponSpawner;
                _selectedIndex = (int)_map->weaponSpawners.size() - 1;
                logMessage("Placed Weapon Spawner: RPG (60s Respawn)");
                break;
            }
        }
    }

    void placeCurrentObject() {
        if (!_map) return;
        if (_activeTool == 1) { // Brush Tool
            MapBrush b;
            b.position = _cursorPos;
            b.size = _brushSize;
            b.color = Vec3(1.0f, 1.0f, 1.0f);
            b.texturePath = _selectedTexture;
            b.uvScale = _activeUvScale;
            b.uvMode = 1;
            _map->brushes.push_back(b);
            _selectionType = SelectionType::Brush;
            _selectedIndex = (int)_map->brushes.size() - 1;
            logMessage("Placed Brush #" + std::to_string(_selectedIndex) + " (" + _selectedTexture + ")");
        } else if (_activeTool == 2) { // Prop Tool
            MapProp p;
            p.modelPath = _selectedModel;
            p.position = _cursorPos;
            p.rotation = Vec3(0, 0, 0);
            p.scale = _propScale;
            p.color = Vec3(1, 1, 1);
            p.texturePath = "";
            _map->props.push_back(p);
            _selectionType = SelectionType::Prop;
            _selectedIndex = (int)_map->props.size() - 1;
            logMessage("Placed Prop #" + std::to_string(_selectedIndex) + " (" + _selectedModel + ")");
        } else if (_activeTool == 4) { // Door Tool
            MapDoor d;
            d.name = "door_" + std::to_string(_map->doors.size());
            d.position = _cursorPos;
            d.size = Vec3(2.5f, 3.5f, 0.4f);
            d.openOffset = Vec3(0.0f, 3.5f, 0.0f);
            d.color = Vec3(0.35f, 0.4f, 0.45f);
            d.openSpeed = 3.0f;
            d.triggerRadius = 4.0f;
            _map->doors.push_back(d);
            _selectionType = SelectionType::Door;
            _selectedIndex = (int)_map->doors.size() - 1;
            logMessage("Placed Dynamic Door #" + std::to_string(_selectedIndex));
        } else if (_activeTool == 5) { // Spawn Tool
            MapSpawnPoint sp;
            sp.position = _cursorPos;
            sp.yaw = 0.0f;
            sp.type = _spawnToolType;
            if (sp.type == SpawnType::TeamAlpha) sp.entityClass = "info_player_team1";
            else if (sp.type == SpawnType::TeamBeta) sp.entityClass = "info_player_team2";
            else sp.entityClass = "info_player_deathmatch";
            _map->spawnPoints.push_back(sp);
            _selectionType = SelectionType::Spawn;
            _selectedIndex = (int)_map->spawnPoints.size() - 1;
            logMessage("Placed " + sp.getDisplayName() + " #" + std::to_string(_selectedIndex) + " at (" + std::to_string((int)_cursorPos.x) + ", " + std::to_string((int)_cursorPos.z) + ")");
        }
    }

    void handleMouseClick(float mx, float my, bool isRmb = false) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        // 1. Texture Browser Modal
        if (_browserOpen) {
            float bw = 820.0f;
            float bh = 600.0f;
            float bx = (w - bw) * 0.5f;
            float by = (h - bh) * 0.5f;

            // Close button click
            if (mx >= bx + bw - 36.0f && mx <= bx + bw - 6.0f && my >= by + 2.0f && my <= by + 26.0f) {
                _browserOpen = false;
                return;
            }
            int cols = 6;
            float thumbSize = 110.0f;
            float gap = 15.0f;
            float startX = bx + 25.0f;
            float startY = by + 45.0f;

            for (size_t i = 0; i < _availableTextures.size(); ++i) {
                int col = (int)(i % cols);
                int row = (int)(i / cols);
                float tx = startX + col * (thumbSize + gap);
                float ty = startY + row * (thumbSize + gap);

                if (mx >= tx && mx <= tx + thumbSize && my >= ty && my <= ty + thumbSize) {
                    _selectedTexture = _availableTextures[i].filename;
                    logMessage("Selected Texture: " + _selectedTexture);
                    if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
                        _map->brushes[_selectedIndex].texturePath = _selectedTexture;
                    }
                    _browserOpen = false;
                    return;
                }
            }
            return;
        }

        // 2. Model Browser Modal
        if (_modelBrowserOpen) {
            float bw = 700.0f;
            float bh = 480.0f;
            float bx = (w - bw) * 0.5f;
            float by = (h - bh) * 0.5f;

            // Close button click
            if (mx >= bx + bw - 36.0f && mx <= bx + bw - 6.0f && my >= by + 2.0f && my <= by + 26.0f) {
                _modelBrowserOpen = false;
                return;
            }
            // Browse Disk STL button
            if (mx >= bx + 20.0f && mx <= bx + 320.0f && my >= by + 45.0f && my <= by + 80.0f) {
                openModelDialog();
                return;
            }
            // Model list items
            float startY = by + 95.0f;
            for (size_t i = 0; i < _availableModels.size(); ++i) {
                float iy = startY + 24.0f + i * 36.0f;
                if (mx >= bx + 20.0f && mx <= bx + bw - 20.0f && my >= iy && my <= iy + 30.0f) {
                    _selectedModel = _availableModels[i];
                    logMessage("Selected 3D Model: " + _selectedModel);
                    if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
                        _map->props[_selectedIndex].modelPath = _selectedModel;
                    }
                    _modelBrowserOpen = false;
                    return;
                }
            }
            return;
        }

        // 3. Help Modal
        if (_helpModalOpen) {
            float bw = 650.0f;
            float bh = 420.0f;
            float bx = (w - bw) * 0.5f;
            float by = (h - bh) * 0.5f;

            if (mx >= bx + bw - 36.0f && mx <= bx + bw - 6.0f && my >= by + 2.0f && my <= by + 26.0f) {
                _helpModalOpen = false;
                return;
            }
            return;
        }

        // 4. Dropdown File Menu
        if (_fileMenuOpen) {
            float menuX = 10.0f;
            float menuY = 24.0f;
            float menuW = 200.0f;
            float menuH = 130.0f;
            if (mx >= menuX && mx <= menuX + menuW && my >= menuY && my <= menuY + menuH) {
                int itemIdx = (int)((my - menuY) / 25.0f);
                if (itemIdx == 0) newMap();
                else if (itemIdx == 1) openMapDialog();
                else if (itemIdx == 2) saveMapAction(false);
                else if (itemIdx == 3) saveMapAction(true);
                else if (itemIdx >= 4) glfwSetWindowShouldClose(getWindow(), GLFW_TRUE);
                _fileMenuOpen = false;
                return;
            } else {
                _fileMenuOpen = false;
            }
        }

        // 5. Top Menu Bar (y: 0..24)
        if (my >= 0.0f && my <= 24.0f) {
            if (mx >= 10.0f && mx <= 50.0f) {
                _fileMenuOpen = !_fileMenuOpen;
                return;
            } else if (mx >= 52.0f && mx <= 90.0f) { // Edit
                if (_selectionType != SelectionType::None) deleteSelection();
                else if (_map && !_map->brushes.empty()) { _map->brushes.pop_back(); logMessage("Undo: deleted last brush"); }
                return;
            } else if (mx >= 92.0f && mx <= 135.0f) { // View
                _wireframeMode = !_wireframeMode;
                glPolygonMode(GL_FRONT_AND_BACK, _wireframeMode ? GL_LINE : GL_FILL);
                logMessage("Wireframe Mode: " + std::string(_wireframeMode ? "ON" : "OFF"));
                return;
            } else if (mx >= 137.0f && mx <= 185.0f) { // Tools
                _browserOpen = !_browserOpen;
                return;
            } else if (mx >= 187.0f && mx <= 230.0f) { // Help
                _helpModalOpen = !_helpModalOpen;
                return;
            }
        }

        // 6. Top Toolbar Buttons (y: 24..58, 18 concrete functional buttons!)
        if (my >= 24.0f && my <= 58.0f) {
            for (int i = 0; i < 18; ++i) {
                float bx = 8.0f + i * 28.0f;
                if (mx >= bx && mx <= bx + 26.0f) {
                    switch (i) {
                        case 0: newMap(); break;
                        case 1: openMapDialog(); break;
                        case 2: saveMapAction(false); break;
                        case 3: saveMapAction(true); break;
                        case 4: // Undo
                            if (_map && !_map->brushes.empty()) { _map->brushes.pop_back(); logMessage("Undo last action."); }
                            break;
                        case 5: deleteSelection(); break;
                        case 6: duplicateSelection(); break;
                        case 7: focusCamera(); break;
                        case 8: _browserOpen = true; break;
                        case 9: _modelBrowserOpen = true; break;
                        case 10: _activeTool = 0; logMessage("Tool: Pointer / Selection Tool"); break;
                        case 11: _activeTool = 1; logMessage("Tool: Brush / Block Tool"); break;
                        case 12: _activeTool = 2; logMessage("Tool: Entity / Prop Tool"); break;
                        case 13: _activeTool = 3; logMessage("Tool: Texture Pipette & Apply"); break;
                        case 14: _activeTool = 4; logMessage("Tool: Dynamic Door Tool"); break;
                        case 15: // Grid snap down
                            _gridSnap = std::max(0.125f, _gridSnap * 0.5f);
                            logMessage("Grid Snap: " + std::to_string(_gridSnap));
                            break;
                        case 16: // Grid snap up
                            _gridSnap = std::min(16.0f, _gridSnap * 2.0f);
                            logMessage("Grid Snap: " + std::to_string(_gridSnap));
                            break;
                        case 17: // Run in Engine (F9)
                            runInEngine();
                            break;
                    }
                    return;
                }
            }
        }

        // 7. Left Tools Palette (x: 0..42, y: 58..878)
        if (mx >= 0.0f && mx <= 42.0f) {
            float startY = 65.0f;
            for (int i = 0; i < 8; ++i) {
                float ty = startY + i * 36.0f;
                if (my >= ty && my <= ty + 32.0f) {
                    _activeTool = i;
                    switch (i) {
                        case 0: logMessage("Tool 0: Selection / Pointer Tool"); break;
                        case 1: logMessage("Tool 1: Brush / Block Tool (E to place)"); break;
                        case 2: logMessage("Tool 2: Entity / Prop Tool (E to place model)"); break;
                        case 3: logMessage("Tool 3: Texture Tool (LMB apply, RMB pipette)"); break;
                        case 4: logMessage("Tool 4: Door Tool (E to place dynamic door)"); break;
                        case 5:
                            if (_spawnToolType == SpawnType::FFA) {
                                _spawnToolType = SpawnType::TeamAlpha;
                                logMessage("Tool 5: Spawn Tool [Team Alpha - Blue HQ] (E to place)");
                            } else if (_spawnToolType == SpawnType::TeamAlpha) {
                                _spawnToolType = SpawnType::TeamBeta;
                                logMessage("Tool 5: Spawn Tool [Team Beta - Red HQ] (E to place)");
                            } else {
                                _spawnToolType = SpawnType::FFA;
                                logMessage("Tool 5: Spawn Tool [FFA / DM Neutral] (E to place)");
                            }
                            break;
                        case 6: // CSG Clip tool
                            if (_activeTool != 6) {
                                _activeTool = 6;
                                logMessage("Tool 6: CSG Clip Tool (X / Shift+X to cycle mode, ENTER to commit)");
                            } else {
                                _clipMode = (ClipMode)(((int)_clipMode + 1) % 3);
                                if (_clipMode == ClipMode::KeepFront) logMessage("CSG Clip Mode: Keep Front (Positive Side)");
                                else if (_clipMode == ClipMode::KeepBack) logMessage("CSG Clip Mode: Keep Back (Negative Side)");
                                else logMessage("CSG Clip Mode: Keep Both (Split into 2 Brushes)");
                            }
                            break;
                        case 7: // Lighting tool
                            _sunAngle += 30.0f;
                            if (_sunAngle >= 360.0f) _sunAngle = 0.0f;
                            {
                                float rad = _sunAngle * 3.14159f / 180.0f;
                                Renderer::setSunLight(Vec3(std::cos(rad), -0.8f, std::sin(rad)), Vec3(1.0f, 0.95f, 0.9f), Vec3(0.25f, 0.28f, 0.35f));
                            }
                            logMessage("Tool 7: Adjusted Sun Light Angle (" + std::to_string((int)_sunAngle) + " deg)");
                            break;
                    }
                    return;
                }
            }
        }

        // 8. Right Sidebar (Using exact synchronized layout!)
        SidebarLayout l = getSidebarLayout(w, h);
        if (mx >= l.rightX && mx <= w && my >= l.rightY && my <= h - 22.0f) {
            // Tab headers (Properties vs Outliner vs Prebuilts)
            if (mx >= l.tabPropX && mx <= l.tabPropX + l.tabPropW && my >= l.tabPropY && my <= l.tabPropY + l.tabPropH) {
                _sidebarTab = SidebarTab::Properties;
                logMessage("Sidebar: Switched to Properties Tab");
                return;
            }
            if (mx >= l.tabOutX && mx <= l.tabOutX + l.tabOutW && my >= l.tabOutY && my <= l.tabOutY + l.tabOutH) {
                _sidebarTab = SidebarTab::Hierarchy;
                logMessage("Sidebar: Switched to Struktura (Outliner) Tab");
                return;
            }
            if (mx >= l.tabPreX && mx <= l.tabPreX + l.tabPreW && my >= l.tabPreY && my <= l.tabPreY + l.tabPreH) {
                _sidebarTab = SidebarTab::Prebuilts;
                logMessage("Sidebar: Switched to Prebuilty & Encje Tab");
                return;
            }

            // PREBUILTS TAB INTERACTIONS
            if (_sidebarTab == SidebarTab::Prebuilts) {
                float catY = l.rightY + 36.0f;
                float catW = (l.rightW - 24.0f) * 0.5f;
                if (my >= catY && my <= catY + 24.0f) {
                    if (mx >= l.rightX + 10.0f && mx <= l.rightX + 10.0f + catW) {
                        _prebuiltCategory = 0;
                        logMessage("Prebuilts: Kategoria [Encje & Baza]");
                        return;
                    }
                    if (mx >= l.rightX + 14.0f + catW && mx <= l.rightX + l.rightW - 10.0f) {
                        _prebuiltCategory = 1;
                        logMessage("Prebuilts: Kategoria [Bronie / Spawners]");
                        return;
                    }
                }

                float startY = catY + 30.0f;
                float cardH = (_prebuiltCategory == 0) ? 44.0f : 36.0f;
                float cardSpacing = cardH + 5.0f;
                int count = (_prebuiltCategory == 0) ? 10 : 9;

                for (int i = 0; i < count; ++i) {
                    float cy = startY + i * cardSpacing;
                    if (mx >= l.rightX + 10.0f && mx <= l.rightX + l.rightW - 10.0f && my >= cy && my <= cy + cardH) {
                        int prebuiltId = (_prebuiltCategory == 0) ? i : (10 + i);
                        placePrebuilt(prebuiltId);
                        return;
                    }
                }
                return;
            }

            // OUTLINER TAB INTERACTIONS
            if (_sidebarTab == SidebarTab::Hierarchy) {
                int totalSpawns = (int)_map->spawnPoints.size();
                int totalWepSpawns = (int)_map->weaponSpawners.size();
                int totalEntities = (int)(totalSpawns + totalWepSpawns + _map->brushes.size() + _map->props.size() + _map->doors.size());
                int maxItems = (int)(l.outListH / l.outItemSpacing);

                // Click on entity items
                if (mx >= l.outListX && mx <= l.outListX + l.outListW && my >= l.outListY && my <= l.outListY + l.outListH) {
                    int clickedSlot = (int)((my - l.outListY - 4.0f) / l.outItemSpacing);
                    int itemIdx = _outlinerScroll + clickedSlot;
                    if (clickedSlot >= 0 && itemIdx >= 0 && itemIdx < totalEntities) {
                        if (itemIdx < totalSpawns) {
                            _selectionType = SelectionType::Spawn;
                            _selectedIndex = itemIdx;
                            logMessage("Outliner: Selected " + _map->spawnPoints[itemIdx].getDisplayName() + " #" + std::to_string(itemIdx));
                        } else if (itemIdx < totalSpawns + totalWepSpawns) {
                            _selectionType = SelectionType::WeaponSpawner;
                            _selectedIndex = itemIdx - totalSpawns;
                            logMessage("Outliner: Selected Weapon Spawner [" + _map->weaponSpawners[_selectedIndex].getWeaponName() + "] #" + std::to_string(_selectedIndex));
                        } else {
                            int bOffset = totalSpawns + totalWepSpawns;
                            int pOffset = bOffset + (int)_map->brushes.size();
                            int dOffset = pOffset + (int)_map->props.size();

                            if (itemIdx >= bOffset && itemIdx < pOffset) {
                                _selectionType = SelectionType::Brush;
                                _selectedIndex = itemIdx - bOffset;
                                _selectedTexture = _map->brushes[_selectedIndex].texturePath;
                                logMessage("Outliner: Selected Brush #" + std::to_string(_selectedIndex));
                            } else if (itemIdx >= pOffset && itemIdx < dOffset) {
                                _selectionType = SelectionType::Prop;
                                _selectedIndex = itemIdx - pOffset;
                                _selectedModel = _map->props[_selectedIndex].modelPath;
                                logMessage("Outliner: Selected Prop #" + std::to_string(_selectedIndex));
                            } else if (itemIdx >= dOffset) {
                                _selectionType = SelectionType::Door;
                                _selectedIndex = itemIdx - dOffset;
                                logMessage("Outliner: Selected Door #" + std::to_string(_selectedIndex));
                            }
                        }
                        return;
                    }
                }

                // Focus (F)
                if (mx >= l.outFocusX && mx <= l.outFocusX + l.outFocusW && my >= l.outFocusY && my <= l.outFocusY + l.outFocusH) {
                    focusCamera();
                    return;
                }
                // Duplicate (Ctrl+D)
                if (mx >= l.outDupX && mx <= l.outDupX + l.outDupW && my >= l.outDupY && my <= l.outDupY + l.outDupH) {
                    duplicateSelection();
                    return;
                }
                // Delete (Del)
                if (mx >= l.outDelX && mx <= l.outDelX + l.outDelW && my >= l.outDelY && my <= l.outDelY + l.outDelH) {
                    deleteSelection();
                    return;
                }
                // Prev Page
                if (mx >= l.outPrevX && mx <= l.outPrevX + l.outPrevW && my >= l.outPrevY && my <= l.outPrevY + l.outPrevH) {
                    _outlinerScroll = std::max(0, _outlinerScroll - maxItems);
                    return;
                }
                // Next Page
                if (mx >= l.outNextX && mx <= l.outNextX + l.outNextW && my >= l.outNextY && my <= l.outNextY + l.outNextH) {
                    if (_outlinerScroll + maxItems < totalEntities) _outlinerScroll += maxItems;
                    return;
                }
                return;
            }

            // PROPERTIES TAB INTERACTIONS
            if (_sidebarTab == SidebarTab::Properties) {
                // Weapon Spawner Properties Interactions
                if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
                    auto& ws = _map->weaponSpawners[_selectedIndex];
                    float propY = l.rightY + 38.0f;
                    float wepBtnY = propY + 114.0f;
                    if (my >= wepBtnY && my <= wepBtnY + 26.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 140.0f) {
                            ws.weaponId = (ws.weaponId - 1 + 9) % 9;
                            logMessage("Changed Spawner to: " + ws.getWeaponName());
                            return;
                        }
                        if (mx >= l.rightX + 150.0f && mx <= l.rightX + 280.0f) {
                            ws.weaponId = (ws.weaponId + 1) % 9;
                            logMessage("Changed Spawner to: " + ws.getWeaponName());
                            return;
                        }
                    }

                    float respBtnY = wepBtnY + 54.0f;
                    if (my >= respBtnY && my <= respBtnY + 24.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 80.0f) {
                            ws.respawnTime = std::max(10.0f, ws.respawnTime - 15.0f);
                            logMessage("Respawn Time: " + std::to_string((int)ws.respawnTime) + "s");
                            return;
                        }
                        if (mx >= l.rightX + 90.0f && mx <= l.rightX + 190.0f) {
                            ws.respawnTime = 60.0f;
                            logMessage("Respawn Time: 60s (1 min)");
                            return;
                        }
                        if (mx >= l.rightX + 200.0f && mx <= l.rightX + 270.0f) {
                            ws.respawnTime = std::min(300.0f, ws.respawnTime + 15.0f);
                            logMessage("Respawn Time: " + std::to_string((int)ws.respawnTime) + "s");
                            return;
                        }
                    }

                    // Deselect
                    if (mx >= l.deselX && mx <= l.deselX + l.deselW && my >= l.deselY && my <= l.deselY + l.deselH) {
                        _selectionType = SelectionType::None;
                        _selectedIndex = -1;
                        logMessage("Deselected all");
                        return;
                    }
                    // Delete
                    if (mx >= l.delX && mx <= l.delX + l.delW && my >= l.delY && my <= l.delY + l.delH) {
                        deleteSelection();
                        return;
                    }
                }

                // Spawn Point Properties Interactions
                if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
                    float propY = l.rightY + 38.0f;
                    float typeBtnY = propY + 114.0f;
                    float btnW = 94.0f;
                    auto& sp = _map->spawnPoints[_selectedIndex];

                    if (my >= typeBtnY && my <= typeBtnY + 26.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 10.0f + btnW) {
                            sp.type = SpawnType::FFA;
                            sp.entityClass = "info_player_deathmatch";
                            logMessage("Set Spawn #" + std::to_string(_selectedIndex) + " to FFA / DM");
                            return;
                        }
                        if (mx >= l.rightX + 110.0f && mx <= l.rightX + 110.0f + btnW) {
                            sp.type = SpawnType::TeamAlpha;
                            sp.entityClass = "info_player_team1";
                            logMessage("Set Spawn #" + std::to_string(_selectedIndex) + " to Team Alpha (Blue)");
                            return;
                        }
                        if (mx >= l.rightX + 210.0f && mx <= l.rightX + 210.0f + btnW) {
                            sp.type = SpawnType::TeamBeta;
                            sp.entityClass = "info_player_team2";
                            logMessage("Set Spawn #" + std::to_string(_selectedIndex) + " to Team Beta (Red)");
                            return;
                        }
                    }

                    float yawBtnY = typeBtnY + 58.0f;
                    if (my >= yawBtnY && my <= yawBtnY + 24.0f) {
                        float yBtnW = 55.0f;
                        float yVals[5] = { -45.0f, 0.0f, 90.0f, 180.0f, 45.0f };
                        for (int k = 0; k < 5; ++k) {
                            float yx = l.rightX + 10.0f + k * 58.0f;
                            if (mx >= yx && mx <= yx + yBtnW) {
                                if (k == 0) sp.yaw -= 45.0f;
                                else if (k == 4) sp.yaw += 45.0f;
                                else sp.yaw = yVals[k];
                                while (sp.yaw < 0.0f) sp.yaw += 360.0f;
                                while (sp.yaw >= 360.0f) sp.yaw -= 360.0f;
                                logMessage("Set Spawn #" + std::to_string(_selectedIndex) + " Yaw: " + std::to_string((int)sp.yaw));
                                return;
                            }
                        }
                    }
                }

                // UV Scale buttons [0.125] [0.25] [0.5] [1.0]
                float scales[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
                for (int i = 0; i < 4; ++i) {
                    float sx = l.rightX + 10.0f + i * 70.0f;
                    if (mx >= sx && mx <= sx + l.uvBtnW && my >= l.uvBtnY && my <= l.uvBtnY + l.uvBtnH) {
                        _activeUvScale = Vec2(scales[i], scales[i]);
                        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
                            _map->brushes[_selectedIndex].uvScale = _activeUvScale;
                        }
                        logMessage("Set UV Scale: " + std::to_string(scales[i]));
                        return;
                    }
                }

                // Thumbnail click -> open texture browser
                if (mx >= l.thumbX && mx <= l.thumbX + l.thumbS && my >= l.thumbY && my <= l.thumbY + l.thumbS) {
                    _browserOpen = true;
                    return;
                }

                // Browse Textures button
                if (mx >= l.texBrowseX && mx <= l.texBrowseX + l.texBrowseW && my >= l.texBrowseY && my <= l.texBrowseY + l.texBrowseH) {
                    _browserOpen = true;
                    return;
                }

                // Apply Texture button
                if (mx >= l.texApplyX && mx <= l.texApplyX + l.texApplyW && my >= l.texApplyY && my <= l.texApplyY + l.texApplyH) {
                    if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
                        _map->brushes[_selectedIndex].texturePath = _selectedTexture;
                        _map->brushes[_selectedIndex].uvScale = _activeUvScale;
                        logMessage("Applied texture '" + _selectedTexture + "' to Brush #" + std::to_string(_selectedIndex));
                    } else {
                        placeCurrentObject();
                    }
                    return;
                }

                // Browse Models button
                if (mx >= l.modelBrowseX && mx <= l.modelBrowseX + l.modelBrowseW && my >= l.modelBrowseY && my <= l.modelBrowseY + l.modelBrowseH) {
                    _modelBrowserOpen = true;
                    return;
                }

                // Dimension adjusters [-] [+]
                if (my >= l.dimBtnsY && my <= l.dimBtnsY + l.dimBtnH) {
                    // X - / +
                    if (mx >= l.rightX + 30.0f && mx <= l.rightX + 56.0f) { resizeSelection(-_gridSnap, 0, 0); _brushSize.x = std::max(0.5f, _brushSize.x - _gridSnap); return; }
                    if (mx >= l.rightX + 60.0f && mx <= l.rightX + 86.0f) { resizeSelection(_gridSnap, 0, 0); _brushSize.x += _gridSnap; return; }
                    // Y - / +
                    if (mx >= l.rightX + 123.0f && mx <= l.rightX + 149.0f) { resizeSelection(0, -_gridSnap, 0); _brushSize.y = std::max(0.5f, _brushSize.y - _gridSnap); return; }
                    if (mx >= l.rightX + 153.0f && mx <= l.rightX + 179.0f) { resizeSelection(0, _gridSnap, 0); _brushSize.y += _gridSnap; return; }
                    // Z - / +
                    if (mx >= l.rightX + 216.0f && mx <= l.rightX + 242.0f) { resizeSelection(0, 0, -_gridSnap); _brushSize.z = std::max(0.5f, _brushSize.z - _gridSnap); return; }
                    if (mx >= l.rightX + 246.0f && mx <= l.rightX + 272.0f) { resizeSelection(0, 0, _gridSnap); _brushSize.z += _gridSnap; return; }
                }

                // Deselect All button
                if (mx >= l.deselX && mx <= l.deselX + l.deselW && my >= l.deselY && my <= l.deselY + l.deselH) {
                    _selectionType = SelectionType::None;
                    _selectedIndex = -1;
                    logMessage("Deselected all.");
                    return;
                }

                // Delete Selected button
                if (mx >= l.delX && mx <= l.delX + l.delW && my >= l.delY && my <= l.delY + l.delH) {
                    deleteSelection();
                    return;
                }
            }
            return; // Guaranteed: right sidebar clicks never bleed into 3D world!
        }

        // 9. 3D Viewport Raycast Picking (Selection / Texture Tool / Object placement)
        pickObjectInViewport(mx, my, isRmb);
    }

    void drawGizmo(const Vec3& pos) {
        float len = 1.6f;
        float thick = 0.06f;
        // X Axis: Red
        Renderer::drawCube(pos + Vec3(len * 0.5f, 0, 0), Vec3(len, thick, thick), Vec3(1.0f, 0.15f, 0.15f), false);
        // Y Axis: Green
        Renderer::drawCube(pos + Vec3(0, len * 0.5f, 0), Vec3(thick, len, thick), Vec3(0.15f, 1.0f, 0.2f), false);
        // Z Axis: Blue
        Renderer::drawCube(pos + Vec3(0, 0, len * 0.5f), Vec3(thick, thick, len), Vec3(0.2f, 0.55f, 1.0f), false);
    }

    void onRender() override {
        // 1. Begin 3D Frame & Frustum Culling
        Renderer::beginFrame(_camera);

        int totalBrushes = 0, renderedBrushes = 0;
        int totalProps = 0, renderedProps = 0;

        if (_map) {
            totalBrushes = (int)_map->brushes.size();
            totalProps = (int)_map->props.size();

            // Render Brushes with 6-plane Frustum Culling & Source Engine UV Tiling
            for (const auto& b : _map->brushes) {
                Vec3 halfSize = b.size * 0.5f;
                Vec3 bMin = b.position - halfSize;
                Vec3 bMax = b.position + halfSize;
                if (!_camera.isInFrustum(bMin, bMax)) continue; // "To czego oko nie widzi tego maszyna renderowac nie musi"

                renderedBrushes++;
                Texture* tex = b.texturePath.empty() ? nullptr : getTexture(b.texturePath);
                if (b.type == "poly" && !b.customVertices.empty()) {
                    if (!b.runtimeMesh) {
                        b.runtimeMesh = std::make_shared<Mesh>(b.customVertices, b.customIndices);
                    }
                    Renderer::drawMesh(*b.runtimeMesh, Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(1, 1, 1), b.color, tex);
                } else {
                    Renderer::drawCube(b.position, b.size, b.color, tex, true, b.uvScale, b.uvMode);
                }
            }

            // Render Props (STL models) with Frustum Culling
            for (const auto& p : _map->props) {
                Vec3 halfScale = p.scale * 0.5f;
                Vec3 pMin = p.position - halfScale;
                Vec3 pMax = p.position + halfScale;
                if (!_camera.isInFrustum(pMin, pMax)) continue; // Frustum culling

                renderedProps++;
                Mesh* m = getMesh(p.modelPath);
                Texture* tex = p.texturePath.empty() ? nullptr : getTexture(p.texturePath);
                if (!tex) {
                    if (p.modelPath.find("barrel") != std::string::npos) {
                        tex = getTexture("barrel_hazard.png");
                    } else if (p.modelPath.find("crate") != std::string::npos) {
                        tex = getTexture("crate_wood.png");
                    }
                }
                if (m) {
                    Renderer::drawMesh(*m, p.position, p.rotation, p.scale, p.color, tex);
                } else {
                    Renderer::drawCube(p.position, p.scale, p.color, tex);
                }
            }

            // Render Doors with Frustum Culling
            for (const auto& d : _map->doors) {
                Vec3 halfSize = d.size * 0.5f;
                if (!_camera.isInFrustum(d.position - halfSize, d.position + halfSize)) continue;
                Renderer::drawCube(d.position, d.size, d.color);
            }

            // Render All Spawn Markers with Team Colors, Landing Pads, and Facing Yaw Arrows
            for (size_t i = 0; i < _map->spawnPoints.size(); ++i) {
                const auto& sp = _map->spawnPoints[i];
                Vec3 teamCol = (sp.type == SpawnType::TeamAlpha) ? Vec3(0.2f, 0.6f, 1.0f) :
                               (sp.type == SpawnType::TeamBeta)  ? Vec3(1.0f, 0.25f, 0.25f) :
                                                                   Vec3(0.2f, 0.9f, 0.4f);
                // Base landing pad
                Renderer::drawCube(sp.position + Vec3(0.0f, -0.85f, 0.0f), Vec3(1.2f, 0.1f, 1.2f), teamCol * 0.7f, nullptr, false);
                Renderer::drawWireCube(sp.position + Vec3(0.0f, -0.85f, 0.0f), Vec3(1.22f, 0.11f, 1.22f), teamCol);

                // Holographic player silhouette
                Renderer::drawWireCube(sp.position, Vec3(0.8f, 1.8f, 0.8f), teamCol);
                Renderer::drawCube(sp.position + Vec3(0.0f, 0.55f, 0.0f), Vec3(0.35f, 0.35f, 0.35f), teamCol * 0.85f, nullptr, false);

                // Yaw directional pointer arrow
                float rad = sp.yaw * 3.14159265f / 180.0f;
                Vec3 fwd(std::sin(rad), 0.0f, std::cos(rad));
                Vec3 arrowPos = sp.position + fwd * 0.85f + Vec3(0.0f, -0.2f, 0.0f);
                Renderer::drawCube(arrowPos, Vec3(0.18f, 0.18f, 0.42f), teamCol, nullptr, false);
            }

            // Render All Weapon Spawners with Pedestal & 3D Weapon Model (Check-once cached assets)
            for (size_t i = 0; i < _map->weaponSpawners.size(); ++i) {
                const auto& ws = _map->weaponSpawners[i];
                // Base ground pedestal
                Renderer::drawCube(ws.position + Vec3(0.0f, 0.04f, 0.0f), Vec3(0.0f, ws.yaw, 0.0f),
                                   Vec3(1.35f, 0.08f, 1.35f), Vec3(0.18f, 0.20f, 0.24f), nullptr, false);
                Renderer::drawCube(ws.position + Vec3(0.0f, 0.082f, 0.0f), Vec3(0.0f, ws.yaw, 0.0f),
                                   Vec3(1.15f, 0.01f, 1.15f), Vec3(0.2f, 0.75f, 0.95f), nullptr, false);

                // Render 3D Weapon Model (Using pre-cached STL mesh or built-in procedural)
                float weaponY = ws.position.y + 0.38f;
                int wId = (ws.weaponId >= 0 && ws.weaponId < 9) ? ws.weaponId : 0;
                Mesh* wMesh = _weaponMeshes[wId];
                Texture* wTex = _weaponTextures[wId];

                if (wMesh) {
                    Renderer::drawMesh(*wMesh, Vec3(ws.position.x, weaponY, ws.position.z),
                                       Vec3(0.0f, ws.yaw, 0.0f), Vec3(0.018f, 0.018f, 0.018f), Vec3(1, 1, 1), wTex);
                } else {
                    Renderer::drawCube(Vec3(ws.position.x, weaponY, ws.position.z), Vec3(0.0f, ws.yaw, 0.0f),
                                       Vec3(0.12f, 0.14f, 0.82f), Vec3(0.35f, 0.65f, 0.95f), wTex, false);
                }
            }
        }

        _cullingStats = "Frustum Culling: Brushes " + std::to_string(renderedBrushes) + "/" + std::to_string(totalBrushes) +
                        " | Props " + std::to_string(renderedProps) + "/" + std::to_string(totalProps);

        // Render Selection Wireframe Bounding Box & Gizmo
        if (_map) {
            Vec3 hammerOrange{ 1.0f, 0.55f, 0.1f };
            if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
                const auto& b = _map->brushes[_selectedIndex];
                Vec3 half = b.size * 0.5f;
                Renderer::drawBoundingBox(b.position - half, b.position + half, hammerOrange);
                drawGizmo(b.position);
            } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
                const auto& p = _map->props[_selectedIndex];
                Vec3 half = p.scale * 0.5f;
                Renderer::drawBoundingBox(p.position - half, p.position + half, hammerOrange);
                drawGizmo(p.position);
            } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
                const auto& d = _map->doors[_selectedIndex];
                Vec3 half = d.size * 0.5f;
                Renderer::drawBoundingBox(d.position - half, d.position + half, hammerOrange);
                drawGizmo(d.position);
            } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
                const auto& sp = _map->spawnPoints[_selectedIndex];
                Vec3 half(0.6f, 0.9f, 0.6f);
                Renderer::drawBoundingBox(sp.position - half, sp.position + half, hammerOrange);
                drawGizmo(sp.position);
            } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
                const auto& ws = _map->weaponSpawners[_selectedIndex];
                Renderer::drawBoundingBox(ws.position - Vec3(0.7f, 0.0f, 0.7f), ws.position + Vec3(0.7f, 0.8f, 0.7f), hammerOrange);
                drawGizmo(ws.position);
            }
        }

        // Draw 3D Cursor Placement Box & Gizmo
        if (_activeTool == 1) {
            Renderer::drawWireCube(_cursorPos, _brushSize, Vec3(0.9f, 0.9f, 0.95f));
            drawGizmo(_cursorPos);
        } else if (_activeTool == 2) {
            Renderer::drawWireCube(_cursorPos, _propScale, Vec3(0.3f, 0.8f, 1.0f));
            drawGizmo(_cursorPos);
        } else if (_activeTool == 4) {
            Renderer::drawWireCube(_cursorPos, Vec3(2.5f, 3.5f, 0.4f), Vec3(0.9f, 0.5f, 0.2f));
            drawGizmo(_cursorPos);
        } else if (_activeTool == 5) {
            Vec3 toolCol = (_spawnToolType == SpawnType::TeamAlpha) ? Vec3(0.2f, 0.6f, 1.0f) :
                           (_spawnToolType == SpawnType::TeamBeta)  ? Vec3(1.0f, 0.25f, 0.25f) :
                                                                     Vec3(0.2f, 0.9f, 0.4f);
            Renderer::drawWireCube(_cursorPos, Vec3(0.8f, 1.8f, 0.8f), toolCol);
            Renderer::drawCube(_cursorPos + Vec3(0.0f, -0.85f, 0.0f), Vec3(1.2f, 0.1f, 1.2f), toolCol * 0.5f, nullptr, false);
            drawGizmo(_cursorPos);
        } else if (_activeTool == 6) {
            drawClipToolOverlay();
        }

        // 2. Render 2D Lab Hammer Desktop Interface
        drawHammerInterface();

        // 3. Render Modal Texture Browser if open
        if (_browserOpen) {
            drawTextureBrowser();
        }

        // 4. Render Modal Model Browser if open
        if (_modelBrowserOpen) {
            drawModelBrowser();
        }

        // 5. Render Modal Help if open
        if (_helpModalOpen) {
            drawHelpModal();
        }

        Renderer::endFrame();
    }

    // Hammer Toolbar & Palette Icon Renderers are shared from Lab::HammerIcons (LabEditor.h)
    static void drawHammerIcon(int iconId, float x, float y, const Vec3& color, const Vec3& bg) {
        Lab::HammerIcons::drawHammerIcon(iconId, x, y, color, bg);
    }
    static void drawToolbarIcon(int iconId, float x, float y, const Vec3& color, const Vec3& bg) {
        Lab::HammerIcons::drawToolbarIcon(iconId, x, y, color, bg);
    }

    void drawHammerInterface() {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Vec3 winBg{ 0.93f, 0.93f, 0.94f };          // Win32 Editor Gray
        Vec3 winBorder{ 0.65f, 0.65f, 0.68f };      // Bevel Gray
        Vec3 textDark{ 0.12f, 0.12f, 0.12f };       // Dark Gray Text
        Vec3 textDim{ 0.45f, 0.45f, 0.45f };        // Dim Label
        Vec3 cyanGlow{ 0.2f, 0.75f, 0.95f };        // Cyan accent
        Vec3 orangeGlow{ 1.0f, 0.55f, 0.1f };       // Hammer Orange

        // ==================== 1. TOP TITLEBAR & MENUS ====================
        Renderer::drawRect(0, 0, w, 24.0f, winBg);
        Renderer::drawRect(0, 23.0f, w, 1.0f, winBorder);

        LabFont::drawText(14.0f, 5.0f, "File", 1.8f, textDark, LabFontType::System);
        LabFont::drawText(54.0f, 5.0f, "Edit", 1.8f, textDark, LabFontType::System);
        LabFont::drawText(94.0f, 5.0f, "View", 1.8f, textDark, LabFontType::System);
        LabFont::drawText(140.0f, 5.0f, "Tools", 1.8f, textDark, LabFontType::System);
        LabFont::drawText(190.0f, 5.0f, "Help", 1.8f, textDark, LabFontType::System);

        LabFont::drawText(w - 360.0f, 5.0f, "Lab Hammer 4.1 - 3D Level Editor", 1.8f, Vec3(0.15f, 0.45f, 0.75f), LabFontType::GeoSans);

        // ==================== 2. MAIN TOOLBAR (18 Buttons) ====================
        float tbY = 24.0f;
        float tbH = 34.0f;
        Renderer::drawRect(0, tbY, w, tbH, winBg);
        Renderer::drawRect(0, tbY + tbH - 1.0f, w, 1.0f, winBorder);

        for (int i = 0; i < 18; ++i) {
            float bx = 8.0f + i * 28.0f;
            drawToolbarIcon(i, bx, tbY + 5.0f, (i == 17) ? Vec3(1, 1, 1) : Vec3(0.25f, 0.3f, 0.35f), (i == 17) ? Vec3(0.15f, 0.65f, 0.35f) : Vec3(0.88f, 0.88f, 0.90f));
        }

        // ==================== 3. LEFT TOOLS PALETTE (Tools 0..7) ====================
        float leftW = 42.0f;
        float leftY = tbY + tbH;
        float leftH = h - leftY - 22.0f;
        Renderer::drawRect(0, leftY, leftW, leftH, winBg);
        Renderer::drawRect(leftW - 1.0f, leftY, 1.0f, leftH, winBorder);

        for (int i = 0; i < 8; ++i) {
            float ty = leftY + 10.0f + i * 36.0f;
            bool isSel = (_activeTool == i);
            Vec3 bgCol = isSel ? Vec3(0.78f, 0.88f, 1.0f) : Vec3(0.88f, 0.88f, 0.90f);
            Vec3 iconCol = isSel ? orangeGlow : Vec3(0.25f, 0.28f, 0.32f);

            Renderer::drawRect(6.0f, ty, 30.0f, 30.0f, bgCol);
            Renderer::drawRect(6.0f, ty, 30.0f, 1.0f, isSel ? cyanGlow : winBorder);
            drawHammerIcon(i, 9.0f, ty + 3.0f, iconCol, bgCol);
        }

        // ==================== 4. RIGHT SIDEBAR (Synchronized Layout) ====================
        SidebarLayout l = getSidebarLayout(w, h);
        Renderer::drawRect(l.rightX, l.rightY, l.rightW, l.rightH, winBg);
        Renderer::drawRect(l.rightX, l.rightY, 1.0f, l.rightH, winBorder);

        // Sidebar Tabs: [ Properties ] [ Struktura ] [ Prebuilty ]
        bool isPropTab = (_sidebarTab == SidebarTab::Properties);
        bool isOutTab = (_sidebarTab == SidebarTab::Hierarchy);
        bool isPreTab = (_sidebarTab == SidebarTab::Prebuilts);

        Renderer::drawRect(l.tabPropX, l.tabPropY, l.tabPropW, l.tabPropH, isPropTab ? Vec3(1, 1, 1) : Vec3(0.85f, 0.85f, 0.88f));
        Renderer::drawRect(l.tabPropX, l.tabPropY, l.tabPropW, 1.0f, isPropTab ? orangeGlow : winBorder);
        LabFont::drawText(l.tabPropX + 12.0f, l.tabPropY + 5.0f, "Properties", 1.5f, isPropTab ? textDark : textDim, LabFontType::System);

        Renderer::drawRect(l.tabOutX, l.tabOutY, l.tabOutW, l.tabOutH, isOutTab ? Vec3(1, 1, 1) : Vec3(0.85f, 0.85f, 0.88f));
        Renderer::drawRect(l.tabOutX, l.tabOutY, l.tabOutW, 1.0f, isOutTab ? orangeGlow : winBorder);
        LabFont::drawText(l.tabOutX + 16.0f, l.tabOutY + 5.0f, "Struktura", 1.5f, isOutTab ? textDark : textDim, LabFontType::System);

        Renderer::drawRect(l.tabPreX, l.tabPreY, l.tabPreW, l.tabPreH, isPreTab ? Vec3(1, 1, 1) : Vec3(0.85f, 0.85f, 0.88f));
        Renderer::drawRect(l.tabPreX, l.tabPreY, l.tabPreW, 1.0f, isPreTab ? orangeGlow : winBorder);
        LabFont::drawText(l.tabPreX + 16.0f, l.tabPreY + 5.0f, "Prebuilty", 1.5f, isPreTab ? textDark : textDim, LabFontType::System);

        // ==================== TAB CONTENT: PREBUILTS & ENTITIES ====================
        if (_sidebarTab == SidebarTab::Prebuilts) {
            float catY = l.rightY + 36.0f;
            float catW = (l.rightW - 24.0f) * 0.5f;

            bool isCat0 = (_prebuiltCategory == 0);
            bool isCat1 = (_prebuiltCategory == 1);

            Renderer::drawRect(l.rightX + 10.0f, catY, catW, 24.0f, isCat0 ? Vec3(1, 1, 1) : Vec3(0.85f, 0.85f, 0.88f));
            Renderer::drawRect(l.rightX + 10.0f, catY, catW, 1.0f, isCat0 ? orangeGlow : winBorder);
            LabFont::drawText(l.rightX + 16.0f, catY + 4.0f, "Encje / Baza", 1.5f, isCat0 ? textDark : textDim, LabFontType::System);

            Renderer::drawRect(l.rightX + 14.0f + catW, catY, catW, 24.0f, isCat1 ? Vec3(1, 1, 1) : Vec3(0.85f, 0.85f, 0.88f));
            Renderer::drawRect(l.rightX + 14.0f + catW, catY, catW, 1.0f, isCat1 ? orangeGlow : winBorder);
            LabFont::drawText(l.rightX + 20.0f + catW, catY + 4.0f, "Bronie (Spawny)", 1.5f, isCat1 ? textDark : textDim, LabFontType::System);

            struct PrebuiltDesc {
                std::string title;
                std::string desc;
                Vec3 color;
            };

            PrebuiltDesc cat0Items[] = {
                { "Spawn: FFA / DM", "Neutralny spawn dla kazdego gracza", Vec3(0.18f, 0.65f, 0.35f) },
                { "Spawn: Team Alpha", "Baza Druzyny 1 (Niebiescy / Blue HQ)", Vec3(0.18f, 0.45f, 0.85f) },
                { "Spawn: Team Beta", "Baza Druzyny 2 (Czerwoni / Red HQ)", Vec3(0.85f, 0.25f, 0.25f) },
                { "Skrzynka Amunicji", "Zasobnik amunicji (+36 pociskow)", Vec3(0.75f, 0.65f, 0.15f) },
                { "Apteczka Polowa", "Pakiet medyczny (+50 HP zdrowia)", Vec3(0.85f, 0.85f, 0.90f) },
                { "Barykada Taktyczna", "Mur ochronny ze skrajnia (3x1.2m)", Vec3(0.45f, 0.50f, 0.58f) },
                { "Filar Betonowy", "Cylinder nosny konstrukcji (1.5x6m)", Vec3(0.55f, 0.58f, 0.65f) },
                { "Brama Bezpieczenstwa", "Przesuwne pancerne drzwi z czujnikiem", Vec3(0.25f, 0.35f, 0.45f) },
                { "Fizyczna Skrzynia", "Drewniana skrzynia Havok (prop_crate)", Vec3(0.65f, 0.50f, 0.35f) },
                { "Wybuchowa Beczka", "Czerwona beczka paliwa (prop_barrel)", Vec3(0.90f, 0.20f, 0.20f) }
            };

            PrebuiltDesc cat1Items[] = {
                { "Spawn: Pipe", "Bron biala rura (Melee)", Vec3(0.55f, 0.55f, 0.60f) },
                { "Spawn: Pistol", "Pistolet taktyczny (Respawn: 60s)", Vec3(0.35f, 0.60f, 0.75f) },
                { "Spawn: Shotgun", "Strzelba bojowa pompka (Respawn: 60s)", Vec3(0.75f, 0.45f, 0.25f) },
                { "Spawn: M4A4-S", "Karabin szturmowy z tlumikiem (60s)", Vec3(0.25f, 0.60f, 0.40f) },
                { "Spawn: SG553", "Karabin szturmowy z celownikiem (60s)", Vec3(0.40f, 0.50f, 0.65f) },
                { "Spawn: Minigun", "Ciezki rotacyjny karabin (Respawn: 60s)", Vec3(0.85f, 0.55f, 0.15f) },
                { "Spawn: Plasma Gun", "Wyrzutnia pociskow plazmowych (60s)", Vec3(0.20f, 0.75f, 0.85f) },
                { "Spawn: Railgun", "Karabin elektromagnetyczny (Respawn: 60s)", Vec3(0.75f, 0.30f, 0.85f) },
                { "Spawn: RPG", "Wyrzutnia rakiet eksplodujacych (60s)", Vec3(0.85f, 0.25f, 0.25f) }
            };

            float startY = catY + 30.0f;
            float cardH = isCat0 ? 44.0f : 36.0f;
            float cardSpacing = cardH + 5.0f;
            int count = isCat0 ? 10 : 9;
            const PrebuiltDesc* activeItems = isCat0 ? cat0Items : cat1Items;

            for (int i = 0; i < count; ++i) {
                float cy = startY + i * cardSpacing;
                Renderer::drawRect(l.rightX + 10.0f, cy, l.rightW - 20.0f, cardH, Vec3(1, 1, 1));
                Renderer::drawRect(l.rightX + 10.0f, cy, l.rightW - 20.0f, 1.0f, winBorder);
                Renderer::drawRect(l.rightX + 10.0f, cy, 6.0f, cardH, activeItems[i].color);

                // Dedicated entity / weapon icon for each prebuilt item
                int iconType = isCat0 ? (i == 8 ? 9 : (i == 9 ? 10 : i)) : 8; // 8 = Weapon Silhouette, 9 = Crate, 10 = Barrel
                Lab::HammerIcons::drawEntityIcon(iconType, l.rightX + 20.0f, cy + (cardH - 26.0f) * 0.5f, activeItems[i].color, Vec3(0.93f, 0.94f, 0.96f));

                LabFont::drawText(l.rightX + 52.0f, cy + 4.0f, activeItems[i].title, 1.5f, textDark, LabFontType::System);
                LabFont::drawText(l.rightX + 52.0f, cy + (isCat0 ? 22.0f : 19.0f), activeItems[i].desc, 1.3f, textDim, LabFontType::System);
            }
        }

        // ==================== TAB CONTENT: OUTLINER (STRUKTURA MAPY) ====================
        if (_sidebarTab == SidebarTab::Hierarchy) {
            LabFont::drawText(l.rightX + 12.0f, l.rightY + 38.0f, "Map Entity Outliner:", 1.7f, textDark, LabFontType::System);

            Renderer::drawRect(l.outListX, l.outListY, l.outListW, l.outListH, Vec3(1, 1, 1));
            Renderer::drawRect(l.outListX, l.outListY, l.outListW, 1.0f, winBorder);

            int totalSpawns = (int)_map->spawnPoints.size();
            int totalWepSpawns = (int)_map->weaponSpawners.size();
            int totalEntities = (int)(totalSpawns + totalWepSpawns + _map->brushes.size() + _map->props.size() + _map->doors.size());
            int maxItems = (int)(l.outListH / l.outItemSpacing);

            for (int i = 0; i < maxItems; ++i) {
                int itemIdx = _outlinerScroll + i;
                if (itemIdx >= totalEntities) break;

                float iy = l.outListY + 4.0f + i * l.outItemSpacing;
                bool isSelected = false;
                std::string itemText = "";

                if (itemIdx < totalSpawns) {
                    isSelected = (_selectionType == SelectionType::Spawn && _selectedIndex == itemIdx);
                    const auto& sp = _map->spawnPoints[itemIdx];
                    itemText = "[" + sp.getDisplayName() + " #" + std::to_string(itemIdx) + "] (" +
                               std::to_string((int)sp.position.x) + "," + std::to_string((int)sp.position.z) + ")";
                } else if (itemIdx < totalSpawns + totalWepSpawns) {
                    int wsIdx = itemIdx - totalSpawns;
                    isSelected = (_selectionType == SelectionType::WeaponSpawner && _selectedIndex == wsIdx);
                    const auto& ws = _map->weaponSpawners[wsIdx];
                    itemText = "[WEP: " + ws.getWeaponName() + " #" + std::to_string(wsIdx) + "] (" +
                               std::to_string((int)ws.position.x) + "," + std::to_string((int)ws.position.z) + ")";
                } else {
                    int bOffset = totalSpawns + totalWepSpawns;
                    int pOffset = bOffset + (int)_map->brushes.size();
                    int dOffset = pOffset + (int)_map->props.size();

                    if (itemIdx >= bOffset && itemIdx < pOffset) {
                        int bIdx = itemIdx - bOffset;
                        isSelected = (_selectionType == SelectionType::Brush && _selectedIndex == bIdx);
                        std::string tName = _map->brushes[bIdx].texturePath;
                        if (tName.size() > 14) tName = tName.substr(0, 12) + "..";
                        itemText = "[B#" + std::to_string(bIdx) + "] " + tName;
                    } else if (itemIdx >= pOffset && itemIdx < dOffset) {
                        int pIdx = itemIdx - pOffset;
                        isSelected = (_selectionType == SelectionType::Prop && _selectedIndex == pIdx);
                        std::string mName = _map->props[pIdx].modelPath;
                        if (mName.size() > 14) mName = mName.substr(0, 12) + "..";
                        itemText = "[P#" + std::to_string(pIdx) + "] " + mName;
                    } else if (itemIdx >= dOffset) {
                        int dIdx = itemIdx - dOffset;
                        isSelected = (_selectionType == SelectionType::Door && _selectedIndex == dIdx);
                        itemText = "[D#" + std::to_string(dIdx) + "] " + _map->doors[dIdx].name;
                    }
                }

                if (isSelected) {
                    Renderer::drawRect(l.outListX + 2.0f, iy, l.outListW - 4.0f, l.outItemH, Vec3(0.85f, 0.92f, 1.0f));
                    Renderer::drawRect(l.outListX + 2.0f, iy, 4.0f, l.outItemH, orangeGlow);
                }

                LabFont::drawText(l.outListX + 10.0f, iy + 4.0f, itemText, 1.5f, isSelected ? Vec3(0.1f, 0.35f, 0.7f) : textDark, LabFontType::System);
            }

            // Outliner action buttons
            Renderer::drawRect(l.outFocusX, l.outFocusY, l.outFocusW, l.outFocusH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.outFocusX, l.outFocusY, l.outFocusW, 1.0f, winBorder);
            LabFont::drawText(l.outFocusX + 14.0f, l.outFocusY + 6.0f, "Focus (F)", 1.5f, textDark, LabFontType::System);

            Renderer::drawRect(l.outDupX, l.outDupY, l.outDupW, l.outDupH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.outDupX, l.outDupY, l.outDupW, 1.0f, winBorder);
            LabFont::drawText(l.outDupX + 14.0f, l.outDupY + 6.0f, "Duplicate", 1.5f, textDark, LabFontType::System);

            Renderer::drawRect(l.outDelX, l.outDelY, l.outDelW, l.outDelH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.outDelX, l.outDelY, l.outDelW, 1.0f, winBorder);
            LabFont::drawText(l.outDelX + 18.0f, l.outDelY + 6.0f, "Delete", 1.5f, Vec3(0.7f, 0.1f, 0.1f), LabFontType::System);

            // Pagination buttons
            Renderer::drawRect(l.outPrevX, l.outPrevY, l.outPrevW, l.outPrevH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.outPrevX, l.outPrevY, l.outPrevW, 1.0f, winBorder);
            LabFont::drawText(l.outPrevX + 45.0f, l.outPrevY + 5.0f, "< Prev Page", 1.5f, textDark, LabFontType::System);

            Renderer::drawRect(l.outNextX, l.outNextY, l.outNextW, l.outNextH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.outNextX, l.outNextY, l.outNextW, 1.0f, winBorder);
            LabFont::drawText(l.outNextX + 45.0f, l.outNextY + 5.0f, "Next Page >", 1.5f, textDark, LabFontType::System);
        }

        // ==================== TAB CONTENT: PROPERTIES ====================
        if (_sidebarTab == SidebarTab::Properties) {
            float propY = l.rightY + 38.0f;

            // Dedicated Weapon Spawner Properties when a weapon spawner is selected
            if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
                auto& ws = _map->weaponSpawners[_selectedIndex];
                std::string selHeader = "Selection: Weapon Spawner #" + std::to_string(_selectedIndex);
                LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, orangeGlow, LabFontType::System);

                std::string wepStr = "Weapon: " + ws.getWeaponName() + " (ID: " + std::to_string(ws.weaponId) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 24.0f, wepStr, 1.5f, Vec3(0.15f, 0.55f, 0.35f), LabFontType::System);

                std::string posStr = "Pos: (" + std::to_string((int)ws.position.x) + ", " + std::to_string((int)ws.position.y) + ", " + std::to_string((int)ws.position.z) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 46.0f, posStr, 1.5f, textDark, LabFontType::System);

                std::string respStr = "Respawn Timer: " + std::to_string((int)ws.respawnTime) + "s (Default: 60s)";
                LabFont::drawText(l.rightX + 12.0f, propY + 68.0f, respStr, 1.5f, textDark, LabFontType::System);

                // Weapon Selector Buttons [< Prev Weapon] [Next Weapon >]
                LabFont::drawText(l.rightX + 12.0f, propY + 96.0f, "Cycle Spawned Weapon:", 1.6f, textDark, LabFontType::System);
                float wepBtnY = propY + 114.0f;
                Renderer::drawRect(l.rightX + 10.0f, wepBtnY, 130.0f, 26.0f, Vec3(0.85f, 0.88f, 0.92f));
                Renderer::drawRect(l.rightX + 10.0f, wepBtnY, 130.0f, 1.0f, winBorder);
                LabFont::drawText(l.rightX + 18.0f, wepBtnY + 5.0f, "< Prev Weapon", 1.5f, textDark, LabFontType::System);

                Renderer::drawRect(l.rightX + 150.0f, wepBtnY, 130.0f, 26.0f, Vec3(0.85f, 0.88f, 0.92f));
                Renderer::drawRect(l.rightX + 150.0f, wepBtnY, 130.0f, 1.0f, winBorder);
                LabFont::drawText(l.rightX + 158.0f, wepBtnY + 5.0f, "Next Weapon >", 1.5f, textDark, LabFontType::System);

                // Respawn Time Buttons [-15s] [60s (1 min)] [+15s]
                LabFont::drawText(l.rightX + 12.0f, wepBtnY + 36.0f, "Set Respawn Delay Cooldown:", 1.6f, textDark, LabFontType::System);
                float respBtnY = wepBtnY + 54.0f;
                Renderer::drawRect(l.rightX + 10.0f, respBtnY, 70.0f, 24.0f, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.rightX + 10.0f, respBtnY, 70.0f, 1.0f, winBorder);
                LabFont::drawText(l.rightX + 22.0f, respBtnY + 4.0f, "-15s", 1.5f, textDark, LabFontType::System);

                bool isDefault60 = (std::abs(ws.respawnTime - 60.0f) < 0.1f);
                Renderer::drawRect(l.rightX + 90.0f, respBtnY, 100.0f, 24.0f, isDefault60 ? Vec3(0.78f, 0.92f, 0.82f) : Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.rightX + 90.0f, respBtnY, 100.0f, 1.0f, isDefault60 ? Vec3(0.2f, 0.6f, 0.3f) : winBorder);
                LabFont::drawText(l.rightX + 100.0f, respBtnY + 4.0f, "60s (1 min)", 1.5f, isDefault60 ? Vec3(0.1f, 0.45f, 0.2f) : textDark, LabFontType::System);

                Renderer::drawRect(l.rightX + 200.0f, respBtnY, 70.0f, 24.0f, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.rightX + 200.0f, respBtnY, 70.0f, 1.0f, winBorder);
                LabFont::drawText(l.rightX + 212.0f, respBtnY + 4.0f, "+15s", 1.5f, textDark, LabFontType::System);

                // Deselect & Delete buttons
                Renderer::drawRect(l.deselX, l.deselY, l.deselW, l.deselH, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.deselX, l.deselY, l.deselW, 1.0f, winBorder);
                LabFont::drawText(l.deselX + 85.0f, l.deselY + 7.0f, "Deselect All", 1.6f, textDark, LabFontType::System);

                Renderer::drawRect(l.delX, l.delY, l.delW, l.delH, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.delX, l.delY, l.delW, 1.0f, winBorder);
                LabFont::drawText(l.delX + 70.0f, l.delY + 7.0f, "Delete Weapon Spawner", 1.6f, Vec3(0.7f, 0.1f, 0.1f), LabFontType::System);
            } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
                auto& sp = _map->spawnPoints[_selectedIndex];
                std::string selHeader = "Selection: " + sp.getDisplayName() + " #" + std::to_string(_selectedIndex);
                LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, orangeGlow, LabFontType::System);

                std::string classStr = "Class: " + sp.entityClass;
                LabFont::drawText(l.rightX + 12.0f, propY + 24.0f, classStr, 1.5f, textDark, LabFontType::System);

                std::string posStr = "Pos: (" + std::to_string((int)sp.position.x) + ", " + std::to_string((int)sp.position.y) + ", " + std::to_string((int)sp.position.z) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 46.0f, posStr, 1.5f, textDark, LabFontType::System);

                std::string yawStr = "Facing Yaw: " + std::to_string((int)sp.yaw) + " deg";
                LabFont::drawText(l.rightX + 12.0f, propY + 68.0f, yawStr, 1.5f, textDark, LabFontType::System);

                // Team Type selector buttons
                LabFont::drawText(l.rightX + 12.0f, propY + 98.0f, "Spawn Mode & Team Allocation:", 1.6f, textDark, LabFontType::System);
                float typeBtnY = propY + 114.0f;
                float btnW = 94.0f;

                bool isFFA = (sp.type == SpawnType::FFA);
                bool isAlpha = (sp.type == SpawnType::TeamAlpha);
                bool isBeta = (sp.type == SpawnType::TeamBeta);

                Renderer::drawRect(l.rightX + 10.0f, typeBtnY, btnW, 26.0f, isFFA ? Vec3(0.2f, 0.75f, 0.4f) : Vec3(0.88f, 0.88f, 0.90f));
                LabFont::drawText(l.rightX + 22.0f, typeBtnY + 5.0f, "FFA / DM", 1.5f, isFFA ? Vec3(1, 1, 1) : textDark, LabFontType::System);

                Renderer::drawRect(l.rightX + 110.0f, typeBtnY, btnW, 26.0f, isAlpha ? Vec3(0.2f, 0.5f, 0.85f) : Vec3(0.88f, 0.88f, 0.90f));
                LabFont::drawText(l.rightX + 118.0f, typeBtnY + 5.0f, "Team Alpha", 1.5f, isAlpha ? Vec3(1, 1, 1) : textDark, LabFontType::System);

                Renderer::drawRect(l.rightX + 210.0f, typeBtnY, btnW, 26.0f, isBeta ? Vec3(0.85f, 0.25f, 0.25f) : Vec3(0.88f, 0.88f, 0.90f));
                LabFont::drawText(l.rightX + 220.0f, typeBtnY + 5.0f, "Team Beta", 1.5f, isBeta ? Vec3(1, 1, 1) : textDark, LabFontType::System);

                // Yaw Rotate buttons
                LabFont::drawText(l.rightX + 12.0f, typeBtnY + 38.0f, "Rotate Spawn Facing Direction:", 1.6f, textDark, LabFontType::System);
                float yawBtnY = typeBtnY + 58.0f;
                float yBtnW = 55.0f;
                const char* yLabels[5] = { "-45", "0", "90", "180", "+45" };
                for (int k = 0; k < 5; ++k) {
                    float yx = l.rightX + 10.0f + k * 58.0f;
                    Renderer::drawRect(yx, yawBtnY, yBtnW, 24.0f, Vec3(0.88f, 0.88f, 0.90f));
                    Renderer::drawRect(yx, yawBtnY, yBtnW, 1.0f, winBorder);
                    LabFont::drawText(yx + 12.0f, yawBtnY + 4.0f, yLabels[k], 1.5f, textDark, LabFontType::System);
                }

                // Deselect & Delete buttons
                Renderer::drawRect(l.deselX, l.deselY, l.deselW, l.deselH, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.deselX, l.deselY, l.deselW, 1.0f, winBorder);
                LabFont::drawText(l.deselX + 85.0f, l.deselY + 7.0f, "Deselect All", 1.6f, textDark, LabFontType::System);

                Renderer::drawRect(l.delX, l.delY, l.delW, l.delH, Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(l.delX, l.delY, l.delW, 1.0f, winBorder);
                LabFont::drawText(l.delX + 75.0f, l.delY + 7.0f, "Delete Spawn Point", 1.6f, Vec3(0.7f, 0.1f, 0.1f), LabFontType::System);
            } else {

            // Header info on selection
            std::string selHeader = "Selection: None";
            if (_selectionType == SelectionType::Brush) selHeader = "Selection: Brush #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Prop) selHeader = "Selection: Prop #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Door) selHeader = "Selection: Door #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Spawn) selHeader = "Selection: Player Spawn";

            LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, (_selectionType != SelectionType::None) ? orangeGlow : textDark, LabFontType::System);

            // Coordinates & Dimensions
            Vec3 pos = _cursorPos;
            Vec3 dims = _brushSize;
            if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
                pos = _map->brushes[_selectedIndex].position;
                dims = _map->brushes[_selectedIndex].size;
            } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
                pos = _map->props[_selectedIndex].position;
                dims = _map->props[_selectedIndex].scale;
            } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
                pos = _map->doors[_selectedIndex].position;
                dims = _map->doors[_selectedIndex].size;
            } else if (_selectionType == SelectionType::Spawn) {
                pos = _map->spawn.position;
            }

            std::string posStr = "Pos: (" + std::to_string((int)pos.x) + ", " + std::to_string((int)pos.y) + ", " + std::to_string((int)pos.z) + ")";
            LabFont::drawText(l.rightX + 12.0f, propY + 22.0f, posStr, 1.6f, textDark, LabFontType::System);

            std::string dimStr = "Size: (" + std::to_string((int)dims.x) + " x " + std::to_string((int)dims.y) + " x " + std::to_string((int)dims.z) + ")";
            LabFont::drawText(l.rightX + 12.0f, propY + 42.0f, dimStr, 1.6f, textDark, LabFontType::System);

            // UV Scale buttons
            LabFont::drawText(l.rightX + 12.0f, l.uvBtnY - 18.0f, "Texture UV Tiling Scale:", 1.7f, textDark, LabFontType::System);

            float scales[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
            const char* scaleLabels[4] = { "0.125", "0.25", "0.5", "1.0" };
            for (int i = 0; i < 4; ++i) {
                float sx = l.rightX + 10.0f + i * 70.0f;
                bool isCurScale = (std::abs(_activeUvScale.x - scales[i]) < 0.01f);
                Renderer::drawRect(sx, l.uvBtnY, l.uvBtnW, l.uvBtnH, isCurScale ? Vec3(0.78f, 0.88f, 1.0f) : Vec3(0.88f, 0.88f, 0.90f));
                Renderer::drawRect(sx, l.uvBtnY, l.uvBtnW, 1.0f, isCurScale ? cyanGlow : winBorder);
                LabFont::drawText(sx + 14.0f, l.uvBtnY + 5.0f, scaleLabels[i], 1.5f, isCurScale ? Vec3(0.1f, 0.4f, 0.8f) : textDark, LabFontType::System);
            }

            // Texture Preview & Picker
            LabFont::drawText(l.rightX + 12.0f, l.texBoxY - 18.0f, "Active Texture:", 1.7f, textDark, LabFontType::System);
            Renderer::drawRect(l.texBoxX, l.texBoxY, l.texBoxW, l.texBoxH, Vec3(1, 1, 1));
            Renderer::drawRect(l.texBoxX, l.texBoxY, l.texBoxW, 1.0f, winBorder);
            LabFont::drawText(l.texBoxX + 10.0f, l.texBoxY + 4.0f, _selectedTexture, 1.6f, textDark, LabFontType::System);

            Renderer::drawRect(l.thumbX, l.thumbY, l.thumbS, l.thumbS, Vec3(0, 0, 0));
            if (_textures.contains(_selectedTexture)) {
                Renderer::drawTextureRect(l.thumbX + 2.0f, l.thumbY + 2.0f, l.thumbS - 4.0f, l.thumbS - 4.0f, *_textures[_selectedTexture]);
            }

            Renderer::drawRect(l.texBrowseX, l.texBrowseY, l.texBrowseW, l.texBrowseH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.texBrowseX, l.texBrowseY, l.texBrowseW, 1.0f, winBorder);
            LabFont::drawText(l.texBrowseX + 22.0f, l.texBrowseY + 8.0f, "Browse Textures...", 1.6f, textDark, LabFontType::System);

            Renderer::drawRect(l.texApplyX, l.texApplyY, l.texApplyW, l.texApplyH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.texApplyX, l.texApplyY, l.texApplyW, 1.0f, winBorder);
            LabFont::drawText(l.texApplyX + 28.0f, l.texApplyY + 8.0f, "Apply to Brush", 1.6f, textDark, LabFontType::System);

            // 3D Entity Prop Model Selector
            LabFont::drawText(l.rightX + 12.0f, l.modelBoxY - 18.0f, "3D Entity Model (.stl):", 1.7f, textDark, LabFontType::System);
            Renderer::drawRect(l.modelBoxX, l.modelBoxY, l.modelBoxW, l.modelBoxH, Vec3(1, 1, 1));
            Renderer::drawRect(l.modelBoxX, l.modelBoxY, l.modelBoxW, 1.0f, winBorder);
            LabFont::drawText(l.modelBoxX + 10.0f, l.modelBoxY + 4.0f, _selectedModel, 1.6f, textDark, LabFontType::System);

            Renderer::drawRect(l.modelBrowseX, l.modelBrowseY, l.modelBrowseW, l.modelBrowseH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.modelBrowseX, l.modelBrowseY, l.modelBrowseW, 1.0f, winBorder);
            LabFont::drawText(l.modelBrowseX + 55.0f, l.modelBrowseY + 8.0f, "Browse 3D Models...", 1.6f, textDark, LabFontType::System);

            // Dimension Adjusters [-] [+]
            LabFont::drawText(l.rightX + 12.0f, l.dimBtnsY - 18.0f, "Adjust Size (X / Y / Z):", 1.7f, textDark, LabFontType::System);

            // X
            LabFont::drawText(l.rightX + 12.0f, l.dimBtnsY + 4.0f, "X:", 1.6f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 30.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 38.0f, l.dimBtnsY + 3.0f, "-", 1.8f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 60.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 66.0f, l.dimBtnsY + 3.0f, "+", 1.8f, textDark, LabFontType::System);

            // Y
            LabFont::drawText(l.rightX + 105.0f, l.dimBtnsY + 4.0f, "Y:", 1.6f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 123.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 131.0f, l.dimBtnsY + 3.0f, "-", 1.8f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 153.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 159.0f, l.dimBtnsY + 3.0f, "+", 1.8f, textDark, LabFontType::System);

            // Z
            LabFont::drawText(l.rightX + 198.0f, l.dimBtnsY + 4.0f, "Z:", 1.6f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 216.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 224.0f, l.dimBtnsY + 3.0f, "-", 1.8f, textDark, LabFontType::System);
            Renderer::drawRect(l.rightX + 246.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, Vec3(0.88f, 0.88f, 0.90f));
            LabFont::drawText(l.rightX + 252.0f, l.dimBtnsY + 3.0f, "+", 1.8f, textDark, LabFontType::System);

            // Deselect button
            Renderer::drawRect(l.deselX, l.deselY, l.deselW, l.deselH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.deselX, l.deselY, l.deselW, 1.0f, winBorder);
            LabFont::drawText(l.deselX + 85.0f, l.deselY + 7.0f, "Deselect All", 1.6f, textDark, LabFontType::System);

            // Delete Selected button
            Renderer::drawRect(l.delX, l.delY, l.delW, l.delH, Vec3(0.88f, 0.88f, 0.90f));
            Renderer::drawRect(l.delX, l.delY, l.delW, 1.0f, winBorder);
            LabFont::drawText(l.delX + 75.0f, l.delY + 7.0f, "Delete Selected", 1.6f, Vec3(0.7f, 0.1f, 0.1f), LabFontType::System);
        }
    }

    // ==================== 5. BOTTOM CONSOLE / "Messages" ====================
        float conW = std::min(720.0f, l.rightX - leftW - 40.0f);
        float conH = 135.0f;
        float conX = leftW + 20.0f;
        float conY = h - conH - 32.0f;

        Renderer::drawRect(conX, conY, conW, conH, Vec3(1, 1, 1));
        Renderer::drawRect(conX, conY, conW, 22.0f, Vec3(0.85f, 0.90f, 0.96f));
        Renderer::drawRect(conX, conY, conW, 1.0f, winBorder);
        Renderer::drawRect(conX, conY + conH - 1.0f, conW, 1.0f, winBorder);
        Renderer::drawRect(conX, conY, 1.0f, conH, winBorder);
        Renderer::drawRect(conX + conW - 1.0f, conY, 1.0f, conH, winBorder);

        LabFont::drawText(conX + 10.0f, conY + 5.0f, "Editor Messages & Optimization Log", 1.7f, textDark, LabFontType::System);

        for (int i = 0; i < (int)_consoleMessages.size(); ++i) {
            LabFont::drawText(conX + 12.0f, conY + 28.0f + i * 14.0f, _consoleMessages[i], 1.5f, Vec3(0.1f, 0.15f, 0.2f), LabFontType::System);
        }

        // CSG Clip Tool HUD Banner when active
        if (_activeTool == 6) {
            float bannerW = 660.0f;
            float bannerH = 34.0f;
            float bx = (w - bannerW) * 0.5f;
            float by = 65.0f;
            Renderer::drawRect(bx, by, bannerW, bannerH, Vec3(0.12f, 0.14f, 0.18f));
            Renderer::drawRect(bx, by, bannerW, 2.0f, orangeGlow);
            Renderer::drawRect(bx, by + bannerH - 1.0f, bannerW, 1.0f, winBorder);

            std::string modeStr = (_clipMode == ClipMode::KeepFront) ? "KEEP FRONT (Positive Side)" :
                                  (_clipMode == ClipMode::KeepBack)  ? "KEEP BACK (Negative Side)" :
                                                                       "SPLIT & KEEP BOTH BRUSHES";
            std::string bannerText = "CSG CLIP TOOL | " + modeStr + " [Shift+X / X] | [ENTER] Slice Brush";
            LabFont::drawText(bx + 14.0f, by + 8.0f, bannerText, 1.5f, Vec3(1.0f, 0.95f, 0.90f), LabFontType::GeoSans);
        }

        // ==================== 6. STATUS BAR ====================
        float sbY = h - 22.0f;
        Renderer::drawRect(0, sbY, w, 22.0f, winBg);
        Renderer::drawRect(0, sbY, w, 1.0f, winBorder);

        std::string sbText = "RMB Fly | LMB Pick/Apply | E Place | X Clip | ENTER Commit | F Focus | Ctrl+D Duplicate | Del Delete | " + _cullingStats;
        LabFont::drawText(10.0f, sbY + 5.0f, sbText, 1.5f, textDark, LabFontType::System);
        std::string gridStr = "Snap: " + std::to_string((int)_gridSnap) + " | F9: Run";
        LabFont::drawText(w - 240.0f, sbY + 5.0f, gridStr, 1.5f, textDark, LabFontType::System);

        // ==================== 7. DROPDOWN FILE MENU ====================
        if (_fileMenuOpen) {
            float menuX = 10.0f;
            float menuY = 24.0f;
            float menuW = 190.0f;
            float menuH = 125.0f;
            Vec3 menuBg{ 0.96f, 0.96f, 0.97f };
            Vec3 menuBorder{ 0.55f, 0.55f, 0.60f };
            Vec3 menuShadow{ 0.2f, 0.2f, 0.2f };

            Renderer::drawRect(menuX + 3.0f, menuY + 3.0f, menuW, menuH, menuShadow * 0.35f);
            Renderer::drawRect(menuX, menuY, menuW, menuH, menuBg);
            Renderer::drawRect(menuX, menuY, menuW, 1.0f, menuBorder);
            Renderer::drawRect(menuX, menuY, 1.0f, menuH, menuBorder);
            Renderer::drawRect(menuX + menuW - 1.0f, menuY, 1.0f, menuH, menuBorder);
            Renderer::drawRect(menuX, menuY + menuH - 1.0f, menuW, 1.0f, menuBorder);

            struct MenuItem { std::string name; std::string shortcut; };
            MenuItem items[] = {
                { "New Map", "Ctrl+N" },
                { "Open Map...", "Ctrl+O" },
                { "Save Map", "Ctrl+S" },
                { "Save Map As...", "" },
                { "Exit", "Alt+F4" }
            };

            for (int i = 0; i < 5; ++i) {
                float iy = menuY + 3.0f + i * 24.0f;
                if (i == 4) Renderer::drawRect(menuX + 6.0f, iy - 2.0f, menuW - 12.0f, 1.0f, menuBorder);
                LabFont::drawText(menuX + 14.0f, iy + 4.0f, items[i].name, 1.6f, textDark, LabFontType::System);
                if (!items[i].shortcut.empty()) {
                    LabFont::drawText(menuX + menuW - 65.0f, iy + 4.0f, items[i].shortcut, 1.5f, textDim, LabFontType::System);
                }
            }
        }

        Renderer::endUI();
    }

    // Modal Texture Browser Gallery
    void drawTextureBrowser() {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.05f, 0.06f, 0.08f));

        float bw = 820.0f;
        float bh = 600.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.92f, 0.92f, 0.94f));
        Renderer::drawRect(bx, by, bw, 28.0f, Vec3(0.2f, 0.35f, 0.55f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "Texture Browser - Choose Surface Material", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        Renderer::drawRect(bx + bw - 32.0f, by + 4.0f, 24.0f, 20.0f, Vec3(0.85f, 0.25f, 0.25f));
        LabFont::drawText(bx + bw - 25.0f, by + 7.0f, "X", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        int cols = 6;
        float thumbSize = 110.0f;
        float gap = 15.0f;
        float startX = bx + 25.0f;
        float startY = by + 45.0f;

        for (size_t i = 0; i < _availableTextures.size(); ++i) {
            int col = (int)(i % cols);
            int row = (int)(i / cols);
            float tx = startX + col * (thumbSize + gap);
            float ty = startY + row * (thumbSize + gap);

            bool isSelected = (_availableTextures[i].filename == _selectedTexture);

            Renderer::drawRect(tx - 3.0f, ty - 3.0f, thumbSize + 6.0f, thumbSize + 22.0f, isSelected ? Vec3(1.0f, 0.55f, 0.1f) : Vec3(0.7f, 0.72f, 0.75f));
            Renderer::drawRect(tx, ty, thumbSize, thumbSize, Vec3(0, 0, 0));

            if (_textures.contains(_availableTextures[i].filename)) {
                Renderer::drawTextureRect(tx, ty, thumbSize, thumbSize, *_textures[_availableTextures[i].filename]);
            }

            std::string label = _availableTextures[i].filename;
            if (label.size() > 12) label = label.substr(0, 10) + "..";
            LabFont::drawText(tx, ty + thumbSize + 4.0f, label, 1.4f, Vec3(0.1f, 0.1f, 0.1f), LabFontType::System);
        }

        Renderer::endUI();
    }

    // Modal Model Browser Gallery
    void drawModelBrowser() {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.05f, 0.06f, 0.08f));

        float bw = 700.0f;
        float bh = 480.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.92f, 0.92f, 0.94f));
        Renderer::drawRect(bx, by, bw, 28.0f, Vec3(0.15f, 0.45f, 0.75f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "3D Model Browser - Place Entity Props", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        Renderer::drawRect(bx + bw - 32.0f, by + 4.0f, 24.0f, 20.0f, Vec3(0.85f, 0.25f, 0.25f));
        LabFont::drawText(bx + bw - 25.0f, by + 7.0f, "X", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        // Browse STL from disk button
        Renderer::drawRect(bx + 20.0f, by + 45.0f, 300.0f, 34.0f, Vec3(0.2f, 0.65f, 0.4f));
        LabFont::drawText(bx + 35.0f, by + 54.0f, "Browse Disk for 3D Model (.stl)...", 1.6f, Vec3(1, 1, 1), LabFontType::System);

        // List discovered models
        float startY = by + 95.0f;
        LabFont::drawText(bx + 20.0f, startY, "Available Models in assets/models/:", 1.7f, Vec3(0.1f, 0.1f, 0.1f), LabFontType::System);

        for (size_t i = 0; i < _availableModels.size(); ++i) {
            float iy = startY + 24.0f + i * 36.0f;
            bool isSel = (_availableModels[i] == _selectedModel);

            Renderer::drawRect(bx + 20.0f, iy, bw - 40.0f, 30.0f, isSel ? Vec3(0.78f, 0.88f, 1.0f) : Vec3(1, 1, 1));
            Renderer::drawRect(bx + 20.0f, iy, bw - 40.0f, 1.0f, isSel ? Vec3(0.2f, 0.75f, 0.95f) : Vec3(0.8f, 0.8f, 0.85f));
            if (isSel) Renderer::drawRect(bx + 20.0f, iy, 4.0f, 30.0f, Vec3(1.0f, 0.55f, 0.1f));

            LabFont::drawText(bx + 32.0f, iy + 7.0f, _availableModels[i], 1.6f, isSel ? Vec3(0.1f, 0.35f, 0.75f) : Vec3(0.15f, 0.15f, 0.15f), LabFontType::System);
        }

        Renderer::endUI();
    }

    // Modal Help
    void drawHelpModal() {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(getWindow(), &fbW, &fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.05f, 0.06f, 0.08f));

        float bw = 650.0f;
        float bh = 420.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.92f, 0.92f, 0.94f));
        Renderer::drawRect(bx, by, bw, 28.0f, Vec3(0.2f, 0.35f, 0.55f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "Lab Hammer 2026 - Keyboard & Mouse Reference", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        Renderer::drawRect(bx + bw - 32.0f, by + 4.0f, 24.0f, 20.0f, Vec3(0.85f, 0.25f, 0.25f));
        LabFont::drawText(bx + bw - 25.0f, by + 7.0f, "X", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        const char* helpLines[] = {
            "Hold RMB + WASD: Free-cam flying (Shift = Boost, Space = Up, Ctrl = Down)",
            "LMB Click in 3D: Raycast selection of Brushes, Props, Doors, Spawns",
            "Tool 3 (Pipette): LMB applies active texture, RMB samples clicked brush texture",
            "E Key: Place object on grid at 3D cursor (Brush, Prop, Door, Spawn)",
            "F Key: Center & Focus Camera on selected entity",
            "Ctrl + D: Duplicate selected entity offset on grid",
            "Delete / Backspace: Delete selected entity",
            "Arrow Keys / PageUp / PageDn: Translate selected object along grid axes",
            "F9: Quick Save and Launch Map in Frozen-Life Engine (Lab.exe)",
            "Optimization: Frustum culling skips rendering off-screen brushes and props"
        };

        for (int i = 0; i < 10; ++i) {
            LabFont::drawText(bx + 25.0f, by + 45.0f + i * 34.0f, helpLines[i], 1.5f, Vec3(0.12f, 0.15f, 0.2f), LabFontType::System);
        }

        Renderer::endUI();
    }

    void onShutdown() override {
        Renderer::shutdown();
    }

private:
    Camera _camera;
    std::unique_ptr<LabMap> _map;
    std::unordered_map<std::string, std::unique_ptr<Texture>> _textures;
    std::unordered_map<std::string, std::unique_ptr<Mesh>> _meshes;
    std::unordered_set<std::string> _missingTextures;
    std::unordered_set<std::string> _missingMeshes;
    Mesh* _weaponMeshes[9] = { nullptr };
    Texture* _weaponTextures[9] = { nullptr };

    std::vector<TextureEntry> _availableTextures;
    std::vector<std::string> _availableModels;
    std::string _selectedTexture = "concrete_wall.bmp";
    std::string _selectedModel = "Model.stl";
    Vec2 _activeUvScale{ 0.25f, 0.25f };

    bool _browserOpen = false;
    bool _modelBrowserOpen = false;
    bool _helpModalOpen = false;
    bool _fileMenuOpen = false;
    bool _wireframeMode = false;
    std::string _currentMapPath = "";

    // Selection State
    SelectionType _selectionType = SelectionType::None;
    int _selectedIndex = -1;

    // Sidebar State
    SidebarTab _sidebarTab = SidebarTab::Properties;
    int _outlinerScroll = 0;
    int _prebuiltCategory = 0; // 0 = Entities / Spawns, 1 = Weapon Spawners

    int _activeTool = 1; // 0=Select, 1=Brush, 2=Prop, 3=Texture, 4=Door, 5=Spawn, 6=Resize, 7=Sun
    SpawnType _spawnToolType = SpawnType::FFA;
    Vec3 _cursorPos{ 0, 0, 0 };
    Vec3 _brushSize{ 2.0f, 2.0f, 2.0f };
    Vec3 _propScale{ 1.0f, 1.0f, 1.0f };
    float _gridSnap = 1.0f;
    float _sunAngle = 45.0f;

    // CSG Clipping Tool State
    ClipMode _clipMode = ClipMode::KeepBoth;
    Vec3 _clipPointA{ 0, 0, 0 };
    Vec3 _clipPointB{ 0, 0, 0 };
    int _clipStep = 0;

    std::vector<std::string> _consoleMessages;
    std::string _cullingStats = "";

    // Input States
    bool _lmbPressed = false;
    bool _rmbPressed = false;
    bool _ePressed = false;
    bool _xPressed = false;
    bool _enterPressed = false;
    bool _ctrlSPressed = false;
    bool _ctrlOPressed = false;
    bool _ctrlNPressed = false;
    bool _ctrlDPressed = false;
    bool _delPressed = false;
    bool _fPressed = false;
    bool _f9Pressed = false;
    bool _arrowLeftPressed = false;
    bool _arrowRightPressed = false;
    bool _arrowUpPressed = false;
    bool _arrowDownPressed = false;
    bool _pageUpPressed = false;
    bool _pageDownPressed = false;
};

int main() {
    LabHammerStandalone hammer;
    hammer.run();
    return 0;
}
