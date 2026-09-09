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
    WeaponSpawner,
    Light
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
        setCursorCaptured(false);

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
        LabLog::info("[LabHammer] " + msg);
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

        char cmdLine[256] = "FrozenLife.exe";
        if (CreateProcessA("FrozenLife.exe", cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) ||
            CreateProcessA("Release\\FrozenLife.exe", cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            logMessage("Launched FrozenLife.exe in Game Engine!");
        } else {
            system("start FrozenLife.exe");
            logMessage("Launched FrozenLife.exe via shell!");
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

        // Compute coordinate scaling between screen window coordinates and framebuffer
        int winW = 1600, winH = 900;
        getWindowSize(winW, winH);
        int fbW = 1600, fbH = 900;
        getFramebufferSize(fbW, fbH);

        double curX = 0.0, curY = 0.0;
        getCursorPos(curX, curY);
        if (curX == 0.0 && curY == 0.0) {
            curX = (double)Input::getMouseX();
            curY = (double)Input::getMouseY();
        }

        float mouseScaleX = (winW > 0 && fbW > 0) ? ((float)fbW / (float)winW) : 1.0f;
        float mouseScaleY = (winH > 0 && fbH > 0) ? ((float)fbH / (float)winH) : 1.0f;

        float mx = (float)curX * mouseScaleX;
        float my = (float)curY * mouseScaleY;
        _mouseScreenX = mx;
        _mouseScreenY = my;

        // Raycast mouse cursor onto floor or scene when hovering 3D viewport so 3D cursor follows the mouse!
        float vpX = 42.0f;
        float vpY = 58.0f;
        float vpW = (fbW > 0 ? (float)fbW : (float)getWidth()) - vpX - 320.0f;
        float vpH = (fbH > 0 ? (float)fbH : (float)getHeight()) - vpY - 22.0f;
        bool in3DViewport = (mx >= vpX && mx <= vpX + vpW && my >= vpY && my <= vpY + vpH);

        // Mouse look in 3D Viewport when holding Right Mouse Button
        if (Input::isMouseButtonPressed(1)) {
            if (_activeTool != 3) {
                if (!_isFlyingCamera && in3DViewport) {
                    _isFlyingCamera = true;
                    setCursorCaptured(true);
                }
                if (_isFlyingCamera) {
                    _camera.update(Input::mouseDelta);
                }
            }
        } else {
            if (_isFlyingCamera) {
                _isFlyingCamera = false;
                setCursorCaptured(false);
            }
        }

        if (in3DViewport && !Input::isMouseButtonPressed(1)) {
            Vec3 rayDir = _camera.screenToWorldRay(mx, my, (float)fbW, (float)fbH);
            float closestT = 1e9f;
            Vec3 hitPoint;
            bool hitSomething = false;

            // Test raycast against existing scene geometry (brushes)
            if (_map) {
                for (const auto& b : _map->brushes) {
                    Vec3 half = b.size * 0.5f;
                    float t = 0.0f;
                    if (rayIntersectAABB(_camera.getPosition(), rayDir, b.position - half, b.position + half, t)) {
                        if (t > 0.001f && t < closestT) {
                            closestT = t;
                            hitPoint = _camera.getPosition() + rayDir * t;
                            hitSomething = true;
                        }
                    }
                }
            }

            if (hitSomething && closestT < 300.0f) {
                _cursorPos = snapToGrid(hitPoint, _gridSnap);
            } else if (std::abs(rayDir.y) > 0.0001f) {
                float t = -_camera.getPosition().y / rayDir.y;
                if (t > 0.0f && t < 300.0f) {
                    Vec3 hit = _camera.getPosition() + rayDir * t;
                    _cursorPos = snapToGrid(hit, _gridSnap);
                } else {
                    _cursorPos = snapToGrid(_camera.getPosition() + _camera.getFront() * 10.0f, _gridSnap);
                }
            } else {
                _cursorPos = snapToGrid(_camera.getPosition() + _camera.getFront() * 10.0f, _gridSnap);
            }
        } else if (Input::isMouseButtonPressed(1)) {
            _cursorPos = snapToGrid(_camera.getPosition() + _camera.getFront() * 10.0f, _gridSnap);
        }

        // Mouse Wheel: Elevate / Lower selected object along Y-axis if inside 3D Viewport
        if (std::abs(Input::scrollDelta) > 0.01f && _selectionType != SelectionType::None && in3DViewport && !Input::isMouseButtonPressed(1)) {
            float dy = (Input::scrollDelta > 0.0f) ? _gridSnap : -_gridSnap;
            moveSelection(0.0f, dy, 0.0f);
        }

        // Handle Left-Click
        if (Input::isMouseButtonJustPressed(0) || (Input::isMouseButtonPressed(0) && !_lmbPressed)) {
            LabLog::info("[LabHammer] LMB Click at (" + std::to_string((int)mx) + ", " + std::to_string((int)my) + ")");
            handleMouseClick(mx, my, false);
            _lmbPressed = true;
        } else if (!Input::isMouseButtonPressed(0)) {
            _lmbPressed = false;
        }

        // Handle Right-Click (for Tool 3 Pipette sample)
        if (Input::isMouseButtonJustPressed(1) || (Input::isMouseButtonPressed(1) && !_rmbPressed)) {
            if (_activeTool == 3) {
                handleMouseClick(mx, my, true);
            }
            _rmbPressed = true;
        } else if (!Input::isMouseButtonPressed(1)) {
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

            bool upElevateKey = Input::isKeyPressed(266) || Input::isKeyPressed(82) || Input::isKeyPressed(334) || Input::isKeyPressed(93); // PageUp, 'R', Keypad '+', ']'
            bool dwnElevateKey = Input::isKeyPressed(267) || Input::isKeyPressed(67) || Input::isKeyPressed(333) || Input::isKeyPressed(91); // PageDown, 'C', Keypad '-', '['

            if (upElevateKey) {
                if (!_pageUpPressed) { moveSelection(0, _gridSnap, 0); _pageUpPressed = true; }
            } else _pageUpPressed = false;

            if (dwnElevateKey) {
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
        getFramebufferSize(fbW, fbH);
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

        // Interactive Gizmo Y-Handle Hit Test (Elevate selected object by clicking the vertical green arrow)
        if (_selectionType != SelectionType::None && !isRmb) {
            Vec3 selPos = getSelectedPosition();
            Vec3 yHandleMin = selPos + Vec3(-0.35f, 0.2f, -0.35f);
            Vec3 yHandleMax = selPos + Vec3(0.35f, 2.3f, 0.35f);
            float tGizmo = 0.0f;
            if (rayIntersectAABB(rayOrigin, rayDir, yHandleMin, yHandleMax, tGizmo)) {
                if (tGizmo > 0.01f && tGizmo < 300.0f) {
                    moveSelection(0.0f, _gridSnap, 0.0f);
                    return;
                }
            }
        }

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

        // Test Lights
        for (size_t i = 0; i < _map->lights.size(); ++i) {
            const auto& lt = _map->lights[i];
            Vec3 half(0.6f, 0.7f, 0.6f);
            float t = 0;
            if (rayIntersectAABB(rayOrigin, rayDir, lt.position - half, lt.position + half, t)) {
                if (t < closestT) {
                    closestT = t;
                    hitType = SelectionType::Light;
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
            } else if (_selectionType == SelectionType::Light) {
                logMessage("Selected Lamp Light #" + std::to_string(hitIndex) + " (" + _map->lights[hitIndex].name + ")");
            } else {
                _selectionType = SelectionType::None;
                _selectedIndex = -1;
                logMessage("Deselected all");
            }
        } else if (_activeTool == 1 || _activeTool == 2 || _activeTool == 4 || _activeTool == 5 || _activeTool == 7) {
            if (!isRmb) {
                if (Input::isKeyPressed(340) && hitType != SelectionType::None) {
                    // Shift + Click selects existing entity even while placement tool is active
                    _selectionType = hitType;
                    _selectedIndex = hitIndex;
                    logMessage("Selected Entity #" + std::to_string(hitIndex));
                } else {
                    placeCurrentObject();
                }
            }
        }
    }

    Vec3 getSelectedPosition() const {
        if (!_map) return Vec3(0, 0, 0);
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            return _map->brushes[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            return _map->props[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            return _map->doors[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            return _map->spawnPoints[_selectedIndex].position;
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            return _map->weaponSpawners[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            return _map->lights[_selectedIndex].position;
        }
        return Vec3(0, 0, 0);
    }

    void alignSelectionToGround() {
        if (!_map || _selectionType == SelectionType::None) return;
        Vec3 curPos = getSelectedPosition();
        moveSelection(0.0f, -curPos.y, 0.0f);
        logMessage("Snapped selection to Ground (Y=0.0)");
    }

    void moveSelection(float dx, float dy, float dz) {
        if (!_map) return;
        Vec3 newPos(0, 0, 0);
        if (_selectionType == SelectionType::Brush && _selectedIndex >= 0 && _selectedIndex < (int)_map->brushes.size()) {
            _map->brushes[_selectedIndex].position += Vec3(dx, dy, dz);
            newPos = _map->brushes[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Prop && _selectedIndex >= 0 && _selectedIndex < (int)_map->props.size()) {
            _map->props[_selectedIndex].position += Vec3(dx, dy, dz);
            newPos = _map->props[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Door && _selectedIndex >= 0 && _selectedIndex < (int)_map->doors.size()) {
            _map->doors[_selectedIndex].position += Vec3(dx, dy, dz);
            newPos = _map->doors[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
            _map->spawnPoints[_selectedIndex].position += Vec3(dx, dy, dz);
            _map->spawn.position = _map->spawnPoints[0].position;
            newPos = _map->spawnPoints[_selectedIndex].position;
        } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
            _map->weaponSpawners[_selectedIndex].position += Vec3(dx, dy, dz);
            newPos = _map->weaponSpawners[_selectedIndex].position;
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            _map->lights[_selectedIndex].position += Vec3(dx, dy, dz);
            newPos = _map->lights[_selectedIndex].position;
        }
        if (std::abs(dy) > 0.0001f) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Elevated selection: Y = %.2f (offset %+0.2f)", newPos.y, dy);
            logMessage(buf);
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
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            auto& lt = _map->lights[_selectedIndex];
            lt.radius = std::max(2.0f, lt.radius + dw);
            lt.intensity = std::max(0.2f, lt.intensity + dh * 0.5f);
            logMessage("Adjusted Light #" + std::to_string(_selectedIndex) + " (Intensity: " + 
                       std::to_string(lt.intensity) + ", Radius: " + std::to_string((int)lt.radius) + "m)");
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
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            MapLight lt = _map->lights[_selectedIndex];
            lt.position.x += _gridSnap;
            lt.position.z += _gridSnap;
            lt.name = "light_" + std::to_string(_map->lights.size());
            _map->lights.push_back(lt);
            _selectedIndex = (int)_map->lights.size() - 1;
            logMessage("Duplicated Lamp Light to #" + std::to_string(_selectedIndex));
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
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            _map->lights.erase(_map->lights.begin() + _selectedIndex);
            logMessage("Deleted Lamp Light #" + std::to_string(_selectedIndex));
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
        } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
            targetPos = _map->lights[_selectedIndex].position;
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
        } else if (_activeTool == 7) { // Lamp / Light Tool
            MapLight lt;
            lt.name = "light_" + std::to_string(_map->lights.size());
            lt.position = _cursorPos + Vec3(0.0f, 1.5f, 0.0f);
            lt.color = _lampColor;
            lt.intensity = _lampIntensity;
            lt.radius = _lampRadius;
            lt.type = _lampType;
            _map->lights.push_back(lt);
            _selectionType = SelectionType::Light;
            _selectedIndex = (int)_map->lights.size() - 1;
            logMessage("Placed Lamp Light #" + std::to_string(_selectedIndex) + " (Intensity: " + std::to_string(lt.intensity) + ", Radius: " + std::to_string((int)lt.radius) + "m)");
        }
    }

    void handleMouseClick(float mx, float my, bool isRmb = false) {
        int fbW = 0, fbH = 0;
        getFramebufferSize(fbW, fbH);
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
                else if (itemIdx >= 4) stop();
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
            float startY = 68.0f;
            for (int i = 0; i < 8; ++i) {
                float ty = startY + i * 36.0f;
                if (my >= ty - 2.0f && my <= ty + 32.0f) {
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
                        case 7: // Lamp / Light Tool
                            _activeTool = 7;
                            logMessage("Tool 7: Lamp / Light Tool (E to place light source, Left-click on scene to place)");
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
                int totalEntities = (int)(totalSpawns + totalWepSpawns + _map->brushes.size() + _map->props.size() + _map->doors.size() + _map->lights.size());
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
                            int lOffset = dOffset + (int)_map->doors.size();

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
                            } else if (itemIdx >= dOffset && itemIdx < lOffset) {
                                _selectionType = SelectionType::Door;
                                _selectedIndex = itemIdx - dOffset;
                                logMessage("Outliner: Selected Door #" + std::to_string(_selectedIndex));
                            } else if (itemIdx >= lOffset) {
                                _selectionType = SelectionType::Light;
                                _selectedIndex = itemIdx - lOffset;
                                logMessage("Outliner: Selected Lamp Light #" + std::to_string(_selectedIndex) + " (" + _map->lights[_selectedIndex].name + ")");
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

                // Lamp / Light Properties Interactions
                if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
                    float propY = l.rightY + 38.0f;
                    float colBtnY = propY + 116.0f;
                    float colBtnW = 90.0f;
                    auto& lt = _map->lights[_selectedIndex];

                    // Row 1 Color Presets
                    if (my >= colBtnY && my <= colBtnY + 24.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 10.0f + colBtnW) { lt.color = Vec3(1.0f, 0.88f, 0.72f); logMessage("Light Color: Warm 2700K"); return; }
                        if (mx >= l.rightX + 106.0f && mx <= l.rightX + 106.0f + colBtnW) { lt.color = Vec3(0.95f, 0.98f, 1.0f); logMessage("Light Color: Cool 6500K"); return; }
                        if (mx >= l.rightX + 202.0f && mx <= l.rightX + 202.0f + colBtnW) { lt.color = Vec3(1.0f, 0.75f, 0.20f); logMessage("Light Color: Sodium Amber"); return; }
                    }
                    // Row 2 Color Presets
                    if (my >= colBtnY + 28.0f && my <= colBtnY + 52.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 10.0f + colBtnW) { lt.color = Vec3(0.20f, 0.90f, 1.0f); logMessage("Light Color: Neon Cyan"); return; }
                        if (mx >= l.rightX + 106.0f && mx <= l.rightX + 106.0f + colBtnW) { lt.color = Vec3(1.0f, 0.15f, 0.15f); logMessage("Light Color: Hazard Red"); return; }
                        if (mx >= l.rightX + 202.0f && mx <= l.rightX + 202.0f + colBtnW) { lt.color = Vec3(0.20f, 1.0f, 0.30f); logMessage("Light Color: Toxic Green"); return; }
                    }
                    // Intensity & Radius Adjusters
                    float paramY = colBtnY + 64.0f + 18.0f;
                    if (my >= paramY && my <= paramY + 24.0f) {
                        if (mx >= l.rightX + 10.0f && mx <= l.rightX + 78.0f) { lt.intensity = std::max(0.2f, lt.intensity - 0.5f); logMessage("Light Intensity: " + std::to_string(lt.intensity)); return; }
                        if (mx >= l.rightX + 82.0f && mx <= l.rightX + 150.0f) { lt.intensity += 0.5f; logMessage("Light Intensity: " + std::to_string(lt.intensity)); return; }
                        if (mx >= l.rightX + 158.0f && mx <= l.rightX + 226.0f) { lt.radius = std::max(2.0f, lt.radius - 2.0f); logMessage("Light Radius: " + std::to_string((int)lt.radius) + "m"); return; }
                        if (mx >= l.rightX + 230.0f && mx <= l.rightX + 298.0f) { lt.radius += 2.0f; logMessage("Light Radius: " + std::to_string((int)lt.radius) + "m"); return; }
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

                // Position translation buttons [-] [+] (Move X / Y / Z on grid)
                if (my >= l.posBtnsY && my <= l.posBtnsY + l.posBtnH) {
                    // X - / +
                    if (mx >= l.rightX + 30.0f && mx <= l.rightX + 56.0f) { moveSelection(-_gridSnap, 0, 0); return; }
                    if (mx >= l.rightX + 60.0f && mx <= l.rightX + 86.0f) { moveSelection(_gridSnap, 0, 0); return; }
                    // Y - / +
                    if (mx >= l.rightX + 123.0f && mx <= l.rightX + 149.0f) { moveSelection(0, -_gridSnap, 0); return; }
                    if (mx >= l.rightX + 153.0f && mx <= l.rightX + 179.0f) { moveSelection(0, _gridSnap, 0); return; }
                    // Z - / +
                    if (mx >= l.rightX + 216.0f && mx <= l.rightX + 242.0f) { moveSelection(0, 0, -_gridSnap); return; }
                    if (mx >= l.rightX + 246.0f && mx <= l.rightX + 272.0f) { moveSelection(0, 0, _gridSnap); return; }
                }

                // Dedicated Quick Elevation Buttons [ ^ UP (+Y) ] [ v DOWN (-Y) ] [ Floor (Y=0) ]
                if (my >= l.elevUpY && my <= l.elevUpY + l.elevUpH) {
                    if (mx >= l.elevUpX && mx <= l.elevUpX + l.elevUpW) {
                        moveSelection(0, _gridSnap, 0);
                        return;
                    }
                    if (mx >= l.elevDwnX && mx <= l.elevDwnX + l.elevDwnW) {
                        moveSelection(0, -_gridSnap, 0);
                        return;
                    }
                    if (mx >= l.elevGndX && mx <= l.elevGndX + l.elevGndW) {
                        alignSelectionToGround();
                        return;
                    }
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
        float len = 1.8f;
        float thick = 0.08f;
        float cap = 0.22f;
        // X Axis: Red
        Renderer::drawCube(pos + Vec3(len * 0.5f, 0, 0), Vec3(len, thick, thick), Vec3(1.0f, 0.18f, 0.18f), false);
        Renderer::drawCube(pos + Vec3(len, 0, 0), Vec3(cap, cap, cap), Vec3(1.0f, 0.35f, 0.35f), false);
        // Y Axis: Green (vertical elevation handle - glowing green top cap arrow)
        Renderer::drawCube(pos + Vec3(0, len * 0.5f, 0), Vec3(thick, len, thick), Vec3(0.2f, 1.0f, 0.25f), false);
        Renderer::drawCube(pos + Vec3(0, len, 0), Vec3(cap * 1.3f, cap * 1.5f, cap * 1.3f), Vec3(0.35f, 1.0f, 0.45f), false);
        // Z Axis: Blue
        Renderer::drawCube(pos + Vec3(0, 0, len * 0.5f), Vec3(thick, thick, len), Vec3(0.25f, 0.55f, 1.0f), false);
        Renderer::drawCube(pos + Vec3(0, 0, len), Vec3(cap, cap, cap), Vec3(0.45f, 0.7f, 1.0f), false);
    }

    void onRender() override {
        // 1. Begin 3D Frame & Frustum Culling
        Renderer::beginFrame(_camera);

        // Upload dynamic map lights to the renderer
        if (_map) {
            std::vector<PointLight> dynamicLights;
            dynamicLights.reserve(_map->lights.size());
            for (const auto& lgt : _map->lights) {
                dynamicLights.push_back(PointLight{
                    .position = lgt.position,
                    .color = lgt.color,
                    .intensity = lgt.intensity,
                    .radius = lgt.radius
                });
            }
            Renderer::setPointLights(dynamicLights);
        } else {
            Renderer::clearPointLights();
        }

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

            // Render All Map Dynamic Lights (Lamps)
            for (size_t i = 0; i < _map->lights.size(); ++i) {
                const auto& lgt = _map->lights[i];
                // Core glowing bulb
                Renderer::drawCube(lgt.position, Vec3(0.35f, 0.35f, 0.35f), lgt.color, nullptr, false);
                // Fixture cap / stand mount
                Renderer::drawCube(lgt.position + Vec3(0.0f, 0.22f, 0.0f), Vec3(0.42f, 0.10f, 0.42f), Vec3(0.25f, 0.27f, 0.30f), nullptr, false);
                // Wireframe light bulb cage / glow halo
                Renderer::drawWireCube(lgt.position, Vec3(0.55f, 0.55f, 0.55f), lgt.color);
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
            } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
                const auto& lgt = _map->lights[_selectedIndex];
                Renderer::drawBoundingBox(lgt.position - Vec3(0.4f, 0.4f, 0.4f), lgt.position + Vec3(0.4f, 0.4f, 0.4f), hammerOrange);
                Renderer::drawWireCube(lgt.position, Vec3(lgt.radius * 2.0f, lgt.radius * 2.0f, lgt.radius * 2.0f), lgt.color * 0.4f);
                drawGizmo(lgt.position);
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
        } else if (_activeTool == 7) {
            // Lamp Placement Tool preview
            Renderer::drawWireCube(_cursorPos, Vec3(0.6f, 0.6f, 0.6f), _lampColor);
            Renderer::drawCube(_cursorPos, Vec3(0.35f, 0.35f, 0.35f), _lampColor, nullptr, false);
            Renderer::drawCube(_cursorPos + Vec3(0.0f, 0.22f, 0.0f), Vec3(0.42f, 0.10f, 0.42f), Vec3(0.25f, 0.27f, 0.30f), nullptr, false);
            Renderer::drawWireCube(_cursorPos, Vec3(_lampRadius * 2.0f, _lampRadius * 2.0f, _lampRadius * 2.0f), _lampColor * 0.4f);
            drawGizmo(_cursorPos);
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

    // Studio Dark Industrial Styling Helpers
    static void drawDarkBevel(float x, float y, float w, float h, bool sunken) {
        Vec3 light = sunken ? Vec3(0.08f, 0.09f, 0.11f) : Vec3(0.38f, 0.40f, 0.45f);
        Vec3 dark  = sunken ? Vec3(0.38f, 0.40f, 0.45f) : Vec3(0.08f, 0.09f, 0.11f);

        // Top line
        Renderer::drawRect(x, y, w, 1.0f, light);
        // Left line
        Renderer::drawRect(x, y, 1.0f, h, light);
        // Bottom line
        Renderer::drawRect(x, y + h - 1.0f, w, 1.0f, dark);
        // Right line
        Renderer::drawRect(x + w - 1.0f, y, 1.0f, h, dark);
    }

    static void drawDarkPanel(float x, float y, float w, float h, const std::string& title = "") {
        Vec3 bgCol(0.18f, 0.19f, 0.22f); // Studio dark slate
        Renderer::drawRect(x, y, w, h, bgCol);
        drawDarkBevel(x, y, w, h, false);

        if (!title.empty()) {
            Vec3 titleBg(0.14f, 0.22f, 0.32f); // Studio dark blue header
            Renderer::drawRect(x + 2.0f, y + 2.0f, w - 4.0f, 20.0f, titleBg);
            LabFont::drawText(x + 8.0f, y + 5.0f, title, 1.5f, Vec3(1.0f, 1.0f, 1.0f), LabFontType::System);
        }
    }

    bool drawDarkButton(float x, float y, float w, float h, const std::string& label, bool active = false, bool isRed = false, bool isGreen = false) {
        bool hovered = (_mouseScreenX >= x && _mouseScreenX <= x + w && _mouseScreenY >= y && _mouseScreenY <= y + h);

        Vec3 bgCol = active ? Vec3(0.85f, 0.48f, 0.10f) :
                     isRed ? (hovered ? Vec3(0.75f, 0.22f, 0.22f) : Vec3(0.55f, 0.16f, 0.16f)) :
                     isGreen ? (hovered ? Vec3(0.22f, 0.65f, 0.32f) : Vec3(0.16f, 0.50f, 0.25f)) :
                     hovered ? Vec3(0.32f, 0.35f, 0.40f) : Vec3(0.24f, 0.25f, 0.28f);

        Renderer::drawRect(x, y, w, h, bgCol);
        drawDarkBevel(x, y, w, h, active);

        Vec3 txtCol = active ? Vec3(1.0f, 1.0f, 1.0f) :
                      hovered ? Vec3(1.0f, 0.95f, 0.70f) :
                      isRed ? Vec3(1.0f, 0.85f, 0.85f) :
                      isGreen ? Vec3(0.90f, 1.0f, 0.90f) :
                      Vec3(0.90f, 0.92f, 0.95f);

        float textX = x + 8.0f;
        float textY = y + (h - 14.0f) * 0.5f;
        LabFont::drawText(textX, textY, label, 1.4f, txtCol, LabFontType::System);
        return hovered;
    }

    // Hammer Toolbar & Palette Icon Renderers are shared from Lab::HammerIcons (LabEditor.h)
    static void drawHammerIcon(int iconId, float x, float y, const Vec3& color, const Vec3& bg) {
        Lab::HammerIcons::drawHammerIcon(iconId, x, y, color, bg);
        drawDarkBevel(x, y, 24.0f, 24.0f, false);
    }
    static void drawToolbarIcon(int iconId, float x, float y, const Vec3& color, const Vec3& bg) {
        Lab::HammerIcons::drawToolbarIcon(iconId, x, y, color, bg);
        drawDarkBevel(x, y, 24.0f, 24.0f, false);
    }

    void drawHammerInterface() {
        int fbW = 0, fbH = 0;
        getFramebufferSize(fbW, fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        // Studio Dark Theme Colors
        Vec3 panelDarkBg{ 0.18f, 0.19f, 0.22f };     // Dark Slate Panel BG
        Vec3 panelDarker{ 0.14f, 0.15f, 0.17f };     // Well / Header Dark BG
        Vec3 wellBg{ 0.11f, 0.12f, 0.14f };          // Sunken Area BG
        Vec3 textLight{ 0.92f, 0.93f, 0.96f };       // Off-White Primary Text
        Vec3 textDim{ 0.58f, 0.62f, 0.68f };         // Subdued Text
        Vec3 cyanGlow{ 0.20f, 0.75f, 0.95f };        // Studio Cyan
        Vec3 orangeGlow{ 1.0f, 0.55f, 0.10f };       // Hammer Orange
        Vec3 greenAccent{ 0.18f, 0.72f, 0.38f };     // Studio Green
        Vec3 redAccent{ 0.85f, 0.25f, 0.25f };       // Studio Red / Delete

        // ==================== 1. TOP TITLEBAR & MENUS ====================
        Renderer::drawRect(0, 0, w, 24.0f, panelDarker);
        drawDarkBevel(0, 0, w, 24.0f, false);

        bool hovFile = (_mouseScreenY >= 0.0f && _mouseScreenY <= 24.0f && _mouseScreenX >= 10.0f && _mouseScreenX <= 50.0f);
        bool hovEdit = (_mouseScreenY >= 0.0f && _mouseScreenY <= 24.0f && _mouseScreenX >= 52.0f && _mouseScreenX <= 90.0f);
        bool hovView = (_mouseScreenY >= 0.0f && _mouseScreenY <= 24.0f && _mouseScreenX >= 92.0f && _mouseScreenX <= 135.0f);
        bool hovTools = (_mouseScreenY >= 0.0f && _mouseScreenY <= 24.0f && _mouseScreenX >= 137.0f && _mouseScreenX <= 185.0f);
        bool hovHelp = (_mouseScreenY >= 0.0f && _mouseScreenY <= 24.0f && _mouseScreenX >= 187.0f && _mouseScreenX <= 230.0f);

        if (hovFile) Renderer::drawRect(10.0f, 2.0f, 40.0f, 20.0f, Vec3(0.24f, 0.28f, 0.36f));
        if (hovEdit) Renderer::drawRect(52.0f, 2.0f, 38.0f, 20.0f, Vec3(0.24f, 0.28f, 0.36f));
        if (hovView) Renderer::drawRect(92.0f, 2.0f, 43.0f, 20.0f, Vec3(0.24f, 0.28f, 0.36f));
        if (hovTools) Renderer::drawRect(137.0f, 2.0f, 48.0f, 20.0f, Vec3(0.24f, 0.28f, 0.36f));
        if (hovHelp) Renderer::drawRect(187.0f, 2.0f, 43.0f, 20.0f, Vec3(0.24f, 0.28f, 0.36f));

        LabFont::drawText(14.0f, 5.0f, "File", 1.8f, hovFile ? orangeGlow : textLight, LabFontType::System);
        LabFont::drawText(54.0f, 5.0f, "Edit", 1.8f, hovEdit ? orangeGlow : textLight, LabFontType::System);
        LabFont::drawText(94.0f, 5.0f, "View", 1.8f, hovView ? orangeGlow : textLight, LabFontType::System);
        LabFont::drawText(140.0f, 5.0f, "Tools", 1.8f, hovTools ? orangeGlow : textLight, LabFontType::System);
        LabFont::drawText(190.0f, 5.0f, "Help", 1.8f, hovHelp ? orangeGlow : textLight, LabFontType::System);

        LabFont::drawText(w - 380.0f, 5.0f, "Lab Hammer 4.1 - 3D Level Editor", 1.8f, orangeGlow, LabFontType::GeoSans);

        // ==================== 2. MAIN TOOLBAR (18 Buttons) ====================
        float tbY = 24.0f;
        float tbH = 34.0f;
        Renderer::drawRect(0, tbY, w, tbH, panelDarkBg);
        drawDarkBevel(0, tbY, w, tbH, false);

        for (int i = 0; i < 18; ++i) {
            float bx = 8.0f + i * 28.0f;
            bool isHov = (_mouseScreenX >= bx && _mouseScreenX <= bx + 26.0f && _mouseScreenY >= tbY + 4.0f && _mouseScreenY <= tbY + 30.0f);
            Vec3 bgCol = (i == 17) ? (isHov ? Vec3(0.22f, 0.80f, 0.42f) : greenAccent) :
                         (isHov ? Vec3(0.32f, 0.35f, 0.40f) : Vec3(0.22f, 0.23f, 0.26f));
            Vec3 iconCol = (i == 17) ? Vec3(1, 1, 1) :
                           (isHov ? Vec3(1.0f, 0.95f, 0.80f) : Vec3(0.85f, 0.88f, 0.92f));

            Renderer::drawRect(bx, tbY + 4.0f, 26.0f, 26.0f, bgCol);
            drawDarkBevel(bx, tbY + 4.0f, 26.0f, 26.0f, false);
            drawToolbarIcon(i, bx + 1.0f, tbY + 5.0f, iconCol, bgCol);
        }

        // ==================== 3. LEFT TOOLS PALETTE (Tools 0..7) ====================
        float leftW = 42.0f;
        float leftY = tbY + tbH;
        float leftH = h - leftY - 22.0f;
        Renderer::drawRect(0, leftY, leftW, leftH, panelDarker);
        drawDarkBevel(0, leftY, leftW, leftH, false);

        for (int i = 0; i < 8; ++i) {
            float ty = leftY + 10.0f + i * 36.0f;
            bool isSel = (_activeTool == i);
            bool isHov = (_mouseScreenX >= 6.0f && _mouseScreenX <= 36.0f && _mouseScreenY >= ty && _mouseScreenY <= ty + 30.0f);
            Vec3 bgCol = isSel ? orangeGlow : (isHov ? Vec3(0.32f, 0.35f, 0.40f) : Vec3(0.22f, 0.23f, 0.26f));
            Vec3 iconCol = isSel ? Vec3(1, 1, 1) : (isHov ? Vec3(1.0f, 0.85f, 0.50f) : Vec3(0.82f, 0.85f, 0.90f));

            Renderer::drawRect(6.0f, ty, 30.0f, 30.0f, bgCol);
            drawDarkBevel(6.0f, ty, 30.0f, 30.0f, isSel);
            drawHammerIcon(i, 9.0f, ty + 3.0f, iconCol, bgCol);
        }

        // ==================== 4. RIGHT SIDEBAR (Synchronized Layout) ====================
        SidebarLayout l = getSidebarLayout(w, h);
        Renderer::drawRect(l.rightX, l.rightY, l.rightW, l.rightH, panelDarkBg);
        drawDarkBevel(l.rightX, l.rightY, l.rightW, l.rightH, false);

        // Sidebar Tabs: [ Properties ] [ Struktura ] [ Prebuilty ]
        bool isPropTab = (_sidebarTab == SidebarTab::Properties);
        bool isOutTab = (_sidebarTab == SidebarTab::Hierarchy);
        bool isPreTab = (_sidebarTab == SidebarTab::Prebuilts);

        // Tab Properties
        Vec3 propTabBg = isPropTab ? Vec3(0.24f, 0.26f, 0.30f) : Vec3(0.16f, 0.17f, 0.19f);
        Renderer::drawRect(l.tabPropX, l.tabPropY, l.tabPropW, l.tabPropH, propTabBg);
        drawDarkBevel(l.tabPropX, l.tabPropY, l.tabPropW, l.tabPropH, isPropTab);
        if (isPropTab) Renderer::drawRect(l.tabPropX, l.tabPropY + l.tabPropH - 2.0f, l.tabPropW, 2.0f, orangeGlow);
        LabFont::drawText(l.tabPropX + 12.0f, l.tabPropY + 5.0f, "Properties", 1.5f, isPropTab ? Vec3(1, 1, 1) : textDim, LabFontType::System);

        // Tab Struktura
        Vec3 outTabBg = isOutTab ? Vec3(0.24f, 0.26f, 0.30f) : Vec3(0.16f, 0.17f, 0.19f);
        Renderer::drawRect(l.tabOutX, l.tabOutY, l.tabOutW, l.tabOutH, outTabBg);
        drawDarkBevel(l.tabOutX, l.tabOutY, l.tabOutW, l.tabOutH, isOutTab);
        if (isOutTab) Renderer::drawRect(l.tabOutX, l.tabOutY + l.tabOutH - 2.0f, l.tabOutW, 2.0f, orangeGlow);
        LabFont::drawText(l.tabOutX + 16.0f, l.tabOutY + 5.0f, "Struktura", 1.5f, isOutTab ? Vec3(1, 1, 1) : textDim, LabFontType::System);

        // Tab Prebuilty
        Vec3 preTabBg = isPreTab ? Vec3(0.24f, 0.26f, 0.30f) : Vec3(0.16f, 0.17f, 0.19f);
        Renderer::drawRect(l.tabPreX, l.tabPreY, l.tabPreW, l.tabPreH, preTabBg);
        drawDarkBevel(l.tabPreX, l.tabPreY, l.tabPreW, l.tabPreH, isPreTab);
        if (isPreTab) Renderer::drawRect(l.tabPreX, l.tabPreY + l.tabPreH - 2.0f, l.tabPreW, 2.0f, orangeGlow);
        LabFont::drawText(l.tabPreX + 16.0f, l.tabPreY + 5.0f, "Prebuilty", 1.5f, isPreTab ? Vec3(1, 1, 1) : textDim, LabFontType::System);

        // ==================== TAB CONTENT: PREBUILTS & ENTITIES ====================
        if (_sidebarTab == SidebarTab::Prebuilts) {
            float catY = l.rightY + 36.0f;
            float catW = (l.rightW - 24.0f) * 0.5f;

            bool isCat0 = (_prebuiltCategory == 0);
            bool isCat1 = (_prebuiltCategory == 1);

            Renderer::drawRect(l.rightX + 10.0f, catY, catW, 24.0f, isCat0 ? Vec3(0.26f, 0.28f, 0.32f) : Vec3(0.16f, 0.17f, 0.19f));
            drawDarkBevel(l.rightX + 10.0f, catY, catW, 24.0f, isCat0);
            if (isCat0) Renderer::drawRect(l.rightX + 10.0f, catY + 22.0f, catW, 2.0f, orangeGlow);
            LabFont::drawText(l.rightX + 16.0f, catY + 4.0f, "Encje / Baza", 1.5f, isCat0 ? Vec3(1, 1, 1) : textDim, LabFontType::System);

            Renderer::drawRect(l.rightX + 14.0f + catW, catY, catW, 24.0f, isCat1 ? Vec3(0.26f, 0.28f, 0.32f) : Vec3(0.16f, 0.17f, 0.19f));
            drawDarkBevel(l.rightX + 14.0f + catW, catY, catW, 24.0f, isCat1);
            if (isCat1) Renderer::drawRect(l.rightX + 14.0f + catW, catY + 22.0f, catW, 2.0f, orangeGlow);
            LabFont::drawText(l.rightX + 20.0f + catW, catY + 4.0f, "Bronie (Spawny)", 1.5f, isCat1 ? Vec3(1, 1, 1) : textDim, LabFontType::System);

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
                bool isHov = (_mouseScreenX >= l.rightX + 10.0f && _mouseScreenX <= l.rightX + l.rightW - 10.0f && _mouseScreenY >= cy && _mouseScreenY <= cy + cardH);
                Vec3 cardBg = isHov ? Vec3(0.26f, 0.28f, 0.32f) : Vec3(0.20f, 0.21f, 0.24f);

                Renderer::drawRect(l.rightX + 10.0f, cy, l.rightW - 20.0f, cardH, cardBg);
                drawDarkBevel(l.rightX + 10.0f, cy, l.rightW - 20.0f, cardH, false);
                Renderer::drawRect(l.rightX + 10.0f, cy, 6.0f, cardH, activeItems[i].color);

                // Dedicated entity / weapon icon for each prebuilt item
                int iconType = isCat0 ? (i == 8 ? 9 : (i == 9 ? 10 : i)) : 8; // 8 = Weapon Silhouette, 9 = Crate, 10 = Barrel
                Lab::HammerIcons::drawEntityIcon(iconType, l.rightX + 20.0f, cy + (cardH - 26.0f) * 0.5f, activeItems[i].color, Vec3(0.14f, 0.15f, 0.17f));

                LabFont::drawText(l.rightX + 52.0f, cy + 4.0f, activeItems[i].title, 1.5f, isHov ? Vec3(1.0f, 0.95f, 0.70f) : textLight, LabFontType::System);
                LabFont::drawText(l.rightX + 52.0f, cy + (isCat0 ? 22.0f : 19.0f), activeItems[i].desc, 1.3f, textDim, LabFontType::System);
            }
        }

        // ==================== TAB CONTENT: OUTLINER (STRUKTURA MAPY) ====================
        if (_sidebarTab == SidebarTab::Hierarchy) {
            LabFont::drawText(l.rightX + 12.0f, l.rightY + 38.0f, "Map Entity Outliner:", 1.7f, orangeGlow, LabFontType::System);

            Renderer::drawRect(l.outListX, l.outListY, l.outListW, l.outListH, wellBg);
            drawDarkBevel(l.outListX, l.outListY, l.outListW, l.outListH, true);

            int totalSpawns = (int)_map->spawnPoints.size();
            int totalWepSpawns = (int)_map->weaponSpawners.size();
            int totalEntities = (int)(totalSpawns + totalWepSpawns + _map->brushes.size() + _map->props.size() + _map->doors.size() + _map->lights.size());
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
                    int lOffset = dOffset + (int)_map->doors.size();

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
                    } else if (itemIdx >= dOffset && itemIdx < lOffset) {
                        int dIdx = itemIdx - dOffset;
                        isSelected = (_selectionType == SelectionType::Door && _selectedIndex == dIdx);
                        itemText = "[D#" + std::to_string(dIdx) + "] " + _map->doors[dIdx].name;
                    } else if (itemIdx >= lOffset) {
                        int lIdx = itemIdx - lOffset;
                        isSelected = (_selectionType == SelectionType::Light && _selectedIndex == lIdx);
                        itemText = "[LIGHT #" + std::to_string(lIdx) + "] " + _map->lights[lIdx].name;
                    }
                }

                if (isSelected) {
                    Renderer::drawRect(l.outListX + 2.0f, iy, l.outListW - 4.0f, l.outItemH, Vec3(0.20f, 0.32f, 0.48f));
                    Renderer::drawRect(l.outListX + 2.0f, iy, 4.0f, l.outItemH, orangeGlow);
                }

                LabFont::drawText(l.outListX + 10.0f, iy + 4.0f, itemText, 1.5f, isSelected ? Vec3(1, 1, 1) : textLight, LabFontType::System);
            }

            // Outliner action buttons
            drawDarkButton(l.outFocusX, l.outFocusY, l.outFocusW, l.outFocusH, "Focus (F)");
            drawDarkButton(l.outDupX, l.outDupY, l.outDupW, l.outDupH, "Duplicate");
            drawDarkButton(l.outDelX, l.outDelY, l.outDelW, l.outDelH, "Delete", false, true);

            // Pagination buttons
            drawDarkButton(l.outPrevX, l.outPrevY, l.outPrevW, l.outPrevH, "< Prev Page");
            drawDarkButton(l.outNextX, l.outNextY, l.outNextW, l.outNextH, "Next Page >");
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
                LabFont::drawText(l.rightX + 12.0f, propY + 24.0f, wepStr, 1.5f, greenAccent, LabFontType::System);

                std::string posStr = "Pos: (" + std::to_string((int)ws.position.x) + ", " + std::to_string((int)ws.position.y) + ", " + std::to_string((int)ws.position.z) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 46.0f, posStr, 1.5f, textLight, LabFontType::System);

                std::string respStr = "Respawn Timer: " + std::to_string((int)ws.respawnTime) + "s (Default: 60s)";
                LabFont::drawText(l.rightX + 12.0f, propY + 68.0f, respStr, 1.5f, textLight, LabFontType::System);

                // Weapon Selector Buttons [< Prev Weapon] [Next Weapon >]
                LabFont::drawText(l.rightX + 12.0f, propY + 96.0f, "Cycle Spawned Weapon:", 1.6f, textLight, LabFontType::System);
                float wepBtnY = propY + 114.0f;
                drawDarkButton(l.rightX + 10.0f, wepBtnY, 130.0f, 26.0f, "< Prev Weapon");
                drawDarkButton(l.rightX + 150.0f, wepBtnY, 130.0f, 26.0f, "Next Weapon >");

                // Respawn Time Buttons [-15s] [60s (1 min)] [+15s]
                LabFont::drawText(l.rightX + 12.0f, wepBtnY + 36.0f, "Set Respawn Delay Cooldown:", 1.6f, textLight, LabFontType::System);
                float respBtnY = wepBtnY + 54.0f;
                drawDarkButton(l.rightX + 10.0f, respBtnY, 70.0f, 24.0f, "-15s");

                bool isDefault60 = (std::abs(ws.respawnTime - 60.0f) < 0.1f);
                drawDarkButton(l.rightX + 90.0f, respBtnY, 100.0f, 24.0f, "60s (1 min)", isDefault60, false, isDefault60);
                drawDarkButton(l.rightX + 200.0f, respBtnY, 70.0f, 24.0f, "+15s");

                // Deselect & Delete buttons
                drawDarkButton(l.deselX, l.deselY, l.deselW, l.deselH, "Deselect All");
                drawDarkButton(l.delX, l.delY, l.delW, l.delH, "Delete Weapon Spawner", false, true);
            } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
                auto& sp = _map->spawnPoints[_selectedIndex];
                std::string selHeader = "Selection: " + sp.getDisplayName() + " #" + std::to_string(_selectedIndex);
                LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, orangeGlow, LabFontType::System);

                std::string classStr = "Class: " + sp.entityClass;
                LabFont::drawText(l.rightX + 12.0f, propY + 24.0f, classStr, 1.5f, textLight, LabFontType::System);

                std::string posStr = "Pos: (" + std::to_string((int)sp.position.x) + ", " + std::to_string((int)sp.position.y) + ", " + std::to_string((int)sp.position.z) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 46.0f, posStr, 1.5f, textLight, LabFontType::System);

                std::string yawStr = "Facing Yaw: " + std::to_string((int)sp.yaw) + " deg";
                LabFont::drawText(l.rightX + 12.0f, propY + 68.0f, yawStr, 1.5f, textLight, LabFontType::System);

                // Team Type selector buttons
                LabFont::drawText(l.rightX + 12.0f, propY + 98.0f, "Spawn Mode & Team Allocation:", 1.6f, textLight, LabFontType::System);
                float typeBtnY = propY + 114.0f;
                float btnW = 94.0f;

                bool isFFA = (sp.type == SpawnType::FFA);
                bool isAlpha = (sp.type == SpawnType::TeamAlpha);
                bool isBeta = (sp.type == SpawnType::TeamBeta);

                drawDarkButton(l.rightX + 10.0f, typeBtnY, btnW, 26.0f, "FFA / DM", isFFA, false, isFFA);
                drawDarkButton(l.rightX + 110.0f, typeBtnY, btnW, 26.0f, "Team Alpha", isAlpha);
                drawDarkButton(l.rightX + 210.0f, typeBtnY, btnW, 26.0f, "Team Beta", isBeta, isBeta);

                // Yaw Rotate buttons
                LabFont::drawText(l.rightX + 12.0f, typeBtnY + 38.0f, "Rotate Spawn Facing Direction:", 1.6f, textLight, LabFontType::System);
                float yawBtnY = typeBtnY + 58.0f;
                float yBtnW = 55.0f;
                const char* yLabels[5] = { "-45", "0", "90", "180", "+45" };
                for (int k = 0; k < 5; ++k) {
                    float yx = l.rightX + 10.0f + k * 58.0f;
                    drawDarkButton(yx, yawBtnY, yBtnW, 24.0f, yLabels[k]);
                }

                // Deselect & Delete buttons
                drawDarkButton(l.deselX, l.deselY, l.deselW, l.deselH, "Deselect All");
                drawDarkButton(l.delX, l.delY, l.delW, l.delH, "Delete Spawn Point", false, true);
            } else if (_selectionType == SelectionType::Light && _selectedIndex >= 0 && _selectedIndex < (int)_map->lights.size()) {
                auto& lt = _map->lights[_selectedIndex];
                std::string selHeader = "Selection: Lamp Light #" + std::to_string(_selectedIndex);
                LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, orangeGlow, LabFontType::System);

                std::string nameStr = "Name: " + lt.name;
                LabFont::drawText(l.rightX + 12.0f, propY + 24.0f, nameStr, 1.5f, greenAccent, LabFontType::System);

                std::string posStr = "Pos: (" + std::to_string((int)lt.position.x) + ", " + std::to_string((int)lt.position.y) + ", " + std::to_string((int)lt.position.z) + ")";
                LabFont::drawText(l.rightX + 12.0f, propY + 46.0f, posStr, 1.5f, textLight, LabFontType::System);

                char statsBuf[64];
                std::snprintf(statsBuf, sizeof(statsBuf), "Intensity: %.1f | Radius: %.1fm", lt.intensity, lt.radius);
                LabFont::drawText(l.rightX + 12.0f, propY + 68.0f, statsBuf, 1.5f, cyanGlow, LabFontType::System);

                // Color swatch
                Renderer::drawRect(l.rightX + 12.0f, propY + 90.0f, 280.0f, 6.0f, lt.color);

                // Color presets
                LabFont::drawText(l.rightX + 12.0f, propY + 102.0f, "Kelvin / Atmosphere Color Presets:", 1.5f, textLight, LabFontType::System);
                float colBtnY = propY + 116.0f;
                float colBtnW = 90.0f;
                drawDarkButton(l.rightX + 10.0f, colBtnY, colBtnW, 24.0f, "Warm 2700K");
                drawDarkButton(l.rightX + 106.0f, colBtnY, colBtnW, 24.0f, "Cool 6500K");
                drawDarkButton(l.rightX + 202.0f, colBtnY, colBtnW, 24.0f, "Sodium");

                drawDarkButton(l.rightX + 10.0f, colBtnY + 28.0f, colBtnW, 24.0f, "Neon Cyan");
                drawDarkButton(l.rightX + 106.0f, colBtnY + 28.0f, colBtnW, 24.0f, "Hazard Red");
                drawDarkButton(l.rightX + 202.0f, colBtnY + 28.0f, colBtnW, 24.0f, "Toxic Green");

                // Intensity & Radius adjusters
                float paramY = colBtnY + 64.0f;
                LabFont::drawText(l.rightX + 12.0f, paramY, "Adjust Intensity & Radius:", 1.5f, textLight, LabFontType::System);
                drawDarkButton(l.rightX + 10.0f, paramY + 18.0f, 68.0f, 24.0f, "Int -");
                drawDarkButton(l.rightX + 82.0f, paramY + 18.0f, 68.0f, 24.0f, "Int +");
                drawDarkButton(l.rightX + 158.0f, paramY + 18.0f, 68.0f, 24.0f, "Rad -");
                drawDarkButton(l.rightX + 230.0f, paramY + 18.0f, 68.0f, 24.0f, "Rad +");

                // Move position [-] [+]
                LabFont::drawText(l.rightX + 12.0f, l.posBtnsY - 18.0f, "Move Light Position (X / Y / Z):", 1.7f, cyanGlow, LabFontType::System);
                LabFont::drawText(l.rightX + 12.0f, l.posBtnsY + 4.0f, "X:", 1.6f, textLight, LabFontType::System);
                drawDarkButton(l.rightX + 30.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
                drawDarkButton(l.rightX + 60.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

                LabFont::drawText(l.rightX + 105.0f, l.posBtnsY + 4.0f, "Y:", 1.6f, greenAccent, LabFontType::System);
                drawDarkButton(l.rightX + 123.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
                drawDarkButton(l.rightX + 153.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

                LabFont::drawText(l.rightX + 198.0f, l.posBtnsY + 4.0f, "Z:", 1.6f, textLight, LabFontType::System);
                drawDarkButton(l.rightX + 216.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
                drawDarkButton(l.rightX + 246.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

                // Elevation buttons
                drawDarkButton(l.elevUpX, l.elevUpY, l.elevUpW, l.elevUpH, "^ UP (+Y)", false, false, true);
                drawDarkButton(l.elevDwnX, l.elevDwnY, l.elevDwnW, l.elevDwnH, "v DOWN (-Y)");
                drawDarkButton(l.elevGndX, l.elevGndY, l.elevGndW, l.elevGndH, "Floor (Y=0)");

                // Deselect & Delete buttons
                drawDarkButton(l.deselX, l.deselY, l.deselW, l.deselH, "Deselect All");
                drawDarkButton(l.delX, l.delY, l.delW, l.delH, "Delete Lamp Light", false, true);
            } else {

            // Header info on selection
            std::string selHeader = "Selection: None";
            if (_selectionType == SelectionType::Brush) selHeader = "Selection: Brush #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Prop) selHeader = "Selection: Prop #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Door) selHeader = "Selection: Door #" + std::to_string(_selectedIndex);
            else if (_selectionType == SelectionType::Spawn) selHeader = "Selection: Player Spawn";
            else if (_selectionType == SelectionType::Light) selHeader = "Selection: Lamp Light #" + std::to_string(_selectedIndex);

            LabFont::drawText(l.rightX + 12.0f, propY, selHeader, 1.7f, (_selectionType != SelectionType::None) ? orangeGlow : textLight, LabFontType::System);

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
            } else if (_selectionType == SelectionType::Spawn && _selectedIndex >= 0 && _selectedIndex < (int)_map->spawnPoints.size()) {
                pos = _map->spawnPoints[_selectedIndex].position;
            } else if (_selectionType == SelectionType::WeaponSpawner && _selectedIndex >= 0 && _selectedIndex < (int)_map->weaponSpawners.size()) {
                pos = _map->weaponSpawners[_selectedIndex].position;
            }

            std::string posStr = "Pos: (" + std::to_string((int)pos.x) + ", " + std::to_string((int)pos.y) + ", " + std::to_string((int)pos.z) + ")";
            LabFont::drawText(l.rightX + 12.0f, propY + 22.0f, posStr, 1.6f, textLight, LabFontType::System);

            std::string dimStr = "Size: (" + std::to_string((int)dims.x) + " x " + std::to_string((int)dims.y) + " x " + std::to_string((int)dims.z) + ")";
            LabFont::drawText(l.rightX + 12.0f, propY + 42.0f, dimStr, 1.6f, textLight, LabFontType::System);

            // UV Scale buttons
            LabFont::drawText(l.rightX + 12.0f, l.uvBtnY - 18.0f, "Texture UV Tiling Scale:", 1.7f, textLight, LabFontType::System);

            float scales[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
            const char* scaleLabels[4] = { "0.125", "0.25", "0.5", "1.0" };
            for (int i = 0; i < 4; ++i) {
                float sx = l.rightX + 10.0f + i * 70.0f;
                bool isCurScale = (std::abs(_activeUvScale.x - scales[i]) < 0.01f);
                drawDarkButton(sx, l.uvBtnY, l.uvBtnW, l.uvBtnH, scaleLabels[i], isCurScale);
            }

            // Texture Preview & Picker
            LabFont::drawText(l.rightX + 12.0f, l.texBoxY - 18.0f, "Active Texture:", 1.7f, textLight, LabFontType::System);
            Renderer::drawRect(l.texBoxX, l.texBoxY, l.texBoxW, l.texBoxH, wellBg);
            drawDarkBevel(l.texBoxX, l.texBoxY, l.texBoxW, l.texBoxH, true);
            LabFont::drawText(l.texBoxX + 10.0f, l.texBoxY + 4.0f, _selectedTexture, 1.6f, textLight, LabFontType::System);

            Renderer::drawRect(l.thumbX, l.thumbY, l.thumbS, l.thumbS, Vec3(0, 0, 0));
            drawDarkBevel(l.thumbX, l.thumbY, l.thumbS, l.thumbS, true);
            if (_textures.contains(_selectedTexture)) {
                Renderer::drawTextureRect(l.thumbX + 2.0f, l.thumbY + 2.0f, l.thumbS - 4.0f, l.thumbS - 4.0f, *_textures[_selectedTexture]);
            }

            drawDarkButton(l.texBrowseX, l.texBrowseY, l.texBrowseW, l.texBrowseH, "Browse Textures...");
            drawDarkButton(l.texApplyX, l.texApplyY, l.texApplyW, l.texApplyH, "Apply to Brush", false, false, true);

            // 3D Entity Prop Model Selector
            LabFont::drawText(l.rightX + 12.0f, l.modelBoxY - 18.0f, "3D Entity Model (.stl):", 1.7f, textLight, LabFontType::System);
            Renderer::drawRect(l.modelBoxX, l.modelBoxY, l.modelBoxW, l.modelBoxH, wellBg);
            drawDarkBevel(l.modelBoxX, l.modelBoxY, l.modelBoxW, l.modelBoxH, true);
            LabFont::drawText(l.modelBoxX + 10.0f, l.modelBoxY + 4.0f, _selectedModel, 1.6f, textLight, LabFontType::System);

            drawDarkButton(l.modelBrowseX, l.modelBrowseY, l.modelBrowseW, l.modelBrowseH, "Browse 3D Models...");

            // Position Translation [-] [+]
            LabFont::drawText(l.rightX + 12.0f, l.posBtnsY - 18.0f, "Move Position (X / Y / Z):", 1.7f, cyanGlow, LabFontType::System);

            // X Position
            LabFont::drawText(l.rightX + 12.0f, l.posBtnsY + 4.0f, "X:", 1.6f, textLight, LabFontType::System);
            drawDarkButton(l.rightX + 30.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
            drawDarkButton(l.rightX + 60.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

            // Y Position (Vertical Elevation)
            LabFont::drawText(l.rightX + 105.0f, l.posBtnsY + 4.0f, "Y:", 1.6f, greenAccent, LabFontType::System);
            drawDarkButton(l.rightX + 123.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
            drawDarkButton(l.rightX + 153.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

            // Z Position
            LabFont::drawText(l.rightX + 198.0f, l.posBtnsY + 4.0f, "Z:", 1.6f, textLight, LabFontType::System);
            drawDarkButton(l.rightX + 216.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "-");
            drawDarkButton(l.rightX + 246.0f, l.posBtnsY, l.posBtnW, l.posBtnH, "+");

            // Quick Elevation Action Buttons
            drawDarkButton(l.elevUpX, l.elevUpY, l.elevUpW, l.elevUpH, "^ UP (+Y)", false, false, true);
            drawDarkButton(l.elevDwnX, l.elevDwnY, l.elevDwnW, l.elevDwnH, "v DOWN (-Y)");
            drawDarkButton(l.elevGndX, l.elevGndY, l.elevGndW, l.elevGndH, "Floor (Y=0)");

            // Dimension Adjusters [-] [+]
            LabFont::drawText(l.rightX + 12.0f, l.dimBtnsY - 18.0f, "Adjust Size (X / Y / Z):", 1.7f, textLight, LabFontType::System);

            // X
            LabFont::drawText(l.rightX + 12.0f, l.dimBtnsY + 4.0f, "X:", 1.6f, textLight, LabFontType::System);
            drawDarkButton(l.rightX + 30.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "-");
            drawDarkButton(l.rightX + 60.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "+");

            // Y
            LabFont::drawText(l.rightX + 105.0f, l.dimBtnsY + 4.0f, "Y:", 1.6f, textLight, LabFontType::System);
            drawDarkButton(l.rightX + 123.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "-");
            drawDarkButton(l.rightX + 153.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "+");

            // Z
            LabFont::drawText(l.rightX + 198.0f, l.dimBtnsY + 4.0f, "Z:", 1.6f, textLight, LabFontType::System);
            drawDarkButton(l.rightX + 216.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "-");
            drawDarkButton(l.rightX + 246.0f, l.dimBtnsY, l.dimBtnW, l.dimBtnH, "+");

            // Deselect button
            drawDarkButton(l.deselX, l.deselY, l.deselW, l.deselH, "Deselect All");

            // Delete Selected button
            drawDarkButton(l.delX, l.delY, l.delW, l.delH, "Delete Selected", false, true);
        }
    }

    // ==================== 5. BOTTOM CONSOLE / "Messages" ====================
        float conW = std::min(720.0f, l.rightX - leftW - 40.0f);
        float conH = 135.0f;
        float conX = leftW + 20.0f;
        float conY = h - conH - 32.0f;

        Renderer::drawRect(conX, conY, conW, conH, wellBg);
        Renderer::drawRect(conX, conY, conW, 22.0f, Vec3(0.14f, 0.20f, 0.28f));
        drawDarkBevel(conX, conY, conW, conH, true);

        LabFont::drawText(conX + 10.0f, conY + 5.0f, "Editor Messages & Optimization Log", 1.7f, cyanGlow, LabFontType::System);

        for (int i = 0; i < (int)_consoleMessages.size(); ++i) {
            Vec3 msgCol = (i == (int)_consoleMessages.size() - 1) ? greenAccent : Vec3(0.78f, 0.82f, 0.88f);
            LabFont::drawText(conX + 12.0f, conY + 28.0f + i * 14.0f, _consoleMessages[i], 1.5f, msgCol, LabFontType::System);
        }

        // CSG Clip Tool HUD Banner when active
        if (_activeTool == 6) {
            float bannerW = 660.0f;
            float bannerH = 34.0f;
            float bx = (w - bannerW) * 0.5f;
            float by = 65.0f;
            Renderer::drawRect(bx, by, bannerW, bannerH, Vec3(0.12f, 0.14f, 0.18f));
            drawDarkBevel(bx, by, bannerW, bannerH, false);
            Renderer::drawRect(bx, by, bannerW, 2.0f, orangeGlow);

            std::string modeStr = (_clipMode == ClipMode::KeepFront) ? "KEEP FRONT (Positive Side)" :
                                  (_clipMode == ClipMode::KeepBack)  ? "KEEP BACK (Negative Side)" :
                                                                       "SPLIT & KEEP BOTH BRUSHES";
            std::string bannerText = "CSG CLIP TOOL | " + modeStr + " [Shift+X / X] | [ENTER] Slice Brush";
            LabFont::drawText(bx + 14.0f, by + 8.0f, bannerText, 1.5f, Vec3(1.0f, 0.95f, 0.90f), LabFontType::GeoSans);
        }

        // ==================== 6. STATUS BAR ====================
        float sbY = h - 22.0f;
        Renderer::drawRect(0, sbY, w, 22.0f, panelDarker);
        drawDarkBevel(0, sbY, w, 22.0f, false);

        std::string sbText = "RMB Fly | LMB Pick/Apply | Wheel / R / C: Elevate Y | E Place | X Clip | ENTER Commit | F Focus | " + _cullingStats;
        LabFont::drawText(10.0f, sbY + 5.0f, sbText, 1.5f, textLight, LabFontType::System);
        std::string gridStr = "Snap: " + std::to_string((int)_gridSnap) + " | F9: Run";
        LabFont::drawText(w - 240.0f, sbY + 5.0f, gridStr, 1.5f, orangeGlow, LabFontType::System);

        // ==================== 7. DROPDOWN FILE MENU ====================
        if (_fileMenuOpen) {
            float menuX = 10.0f;
            float menuY = 24.0f;
            float menuW = 190.0f;
            float menuH = 125.0f;
            Vec3 menuBg{ 0.16f, 0.17f, 0.20f };
            Vec3 menuBorder{ 0.35f, 0.38f, 0.44f };
            Vec3 menuShadow{ 0.05f, 0.05f, 0.07f };

            Renderer::drawRect(menuX + 3.0f, menuY + 3.0f, menuW, menuH, menuShadow);
            Renderer::drawRect(menuX, menuY, menuW, menuH, menuBg);
            drawDarkBevel(menuX, menuY, menuW, menuH, false);

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
                bool isHov = (_mouseScreenX >= menuX && _mouseScreenX <= menuX + menuW && _mouseScreenY >= iy && _mouseScreenY <= iy + 22.0f);
                if (isHov) {
                    Renderer::drawRect(menuX + 2.0f, iy, menuW - 4.0f, 22.0f, Vec3(0.24f, 0.28f, 0.36f));
                }
                if (i == 4) Renderer::drawRect(menuX + 6.0f, iy - 2.0f, menuW - 12.0f, 1.0f, menuBorder);
                LabFont::drawText(menuX + 14.0f, iy + 4.0f, items[i].name, 1.6f, isHov ? orangeGlow : textLight, LabFontType::System);
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
        getFramebufferSize(fbW, fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.04f, 0.05f, 0.06f));

        float bw = 820.0f;
        float bh = 600.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.16f, 0.17f, 0.20f));
        drawDarkBevel(bx, by, bw, bh, false);

        Renderer::drawRect(bx + 2.0f, by + 2.0f, bw - 4.0f, 28.0f, Vec3(0.14f, 0.22f, 0.34f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "Texture Browser - Choose Surface Material", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        drawDarkButton(bx + bw - 32.0f, by + 4.0f, 24.0f, 22.0f, "X", false, true);

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

            Renderer::drawRect(tx - 3.0f, ty - 3.0f, thumbSize + 6.0f, thumbSize + 22.0f, isSelected ? Vec3(1.0f, 0.55f, 0.10f) : Vec3(0.24f, 0.25f, 0.28f));
            drawDarkBevel(tx - 3.0f, ty - 3.0f, thumbSize + 6.0f, thumbSize + 22.0f, isSelected);
            Renderer::drawRect(tx, ty, thumbSize, thumbSize, Vec3(0, 0, 0));

            if (_textures.contains(_availableTextures[i].filename)) {
                Renderer::drawTextureRect(tx, ty, thumbSize, thumbSize, *_textures[_availableTextures[i].filename]);
            }

            std::string label = _availableTextures[i].filename;
            if (label.size() > 12) label = label.substr(0, 10) + "..";
            LabFont::drawText(tx, ty + thumbSize + 4.0f, label, 1.4f, isSelected ? Vec3(1.0f, 0.85f, 0.2f) : Vec3(0.9f, 0.92f, 0.95f), LabFontType::System);
        }

        Renderer::endUI();
    }

    // Modal Model Browser Gallery
    void drawModelBrowser() {
        int fbW = 0, fbH = 0;
        getFramebufferSize(fbW, fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.04f, 0.05f, 0.06f));

        float bw = 700.0f;
        float bh = 480.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.16f, 0.17f, 0.20f));
        drawDarkBevel(bx, by, bw, bh, false);

        Renderer::drawRect(bx + 2.0f, by + 2.0f, bw - 4.0f, 28.0f, Vec3(0.14f, 0.22f, 0.34f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "3D Model Browser - Place Entity Props", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        drawDarkButton(bx + bw - 32.0f, by + 4.0f, 24.0f, 22.0f, "X", false, true);

        // Browse STL from disk button
        drawDarkButton(bx + 20.0f, by + 45.0f, 300.0f, 34.0f, "Browse Disk for 3D Model (.stl)...", false, false, true);

        // List discovered models
        float startY = by + 95.0f;
        LabFont::drawText(bx + 20.0f, startY, "Available Models in assets/models/:", 1.7f, Vec3(1.0f, 0.55f, 0.10f), LabFontType::System);

        for (size_t i = 0; i < _availableModels.size(); ++i) {
            float iy = startY + 24.0f + i * 36.0f;
            bool isSel = (_availableModels[i] == _selectedModel);

            Renderer::drawRect(bx + 20.0f, iy, bw - 40.0f, 30.0f, isSel ? Vec3(0.20f, 0.32f, 0.48f) : Vec3(0.21f, 0.22f, 0.25f));
            drawDarkBevel(bx + 20.0f, iy, bw - 40.0f, 30.0f, isSel);
            if (isSel) Renderer::drawRect(bx + 20.0f, iy, 4.0f, 30.0f, Vec3(1.0f, 0.55f, 0.10f));

            LabFont::drawText(bx + 32.0f, iy + 7.0f, _availableModels[i], 1.6f, isSel ? Vec3(1, 1, 1) : Vec3(0.90f, 0.92f, 0.95f), LabFontType::System);
        }

        Renderer::endUI();
    }

    // Modal Help
    void drawHelpModal() {
        int fbW = 0, fbH = 0;
        getFramebufferSize(fbW, fbH);
        float w = (fbW > 0) ? (float)fbW : (float)getWidth();
        float h = (fbH > 0) ? (float)fbH : (float)getHeight();

        Renderer::beginUI((int)w, (int)h);

        Renderer::drawRect(0, 0, w, h, Vec3(0.04f, 0.05f, 0.06f));

        float bw = 650.0f;
        float bh = 430.0f;
        float bx = (w - bw) * 0.5f;
        float by = (h - bh) * 0.5f;

        Renderer::drawRect(bx, by, bw, bh, Vec3(0.16f, 0.17f, 0.20f));
        drawDarkBevel(bx, by, bw, bh, false);

        Renderer::drawRect(bx + 2.0f, by + 2.0f, bw - 4.0f, 28.0f, Vec3(0.14f, 0.22f, 0.34f));
        LabFont::drawText(bx + 14.0f, by + 8.0f, "Lab Hammer 2026 - Keyboard & Mouse Reference", 1.8f, Vec3(1, 1, 1), LabFontType::System);

        drawDarkButton(bx + bw - 32.0f, by + 4.0f, 24.0f, 22.0f, "X", false, true);

        const char* helpLines[] = {
            "Hold RMB + WASD: Free-cam flying (Shift = Boost, Space = Up, Ctrl = Down)",
            "LMB Click in 3D: Raycast selection of Brushes, Props, Doors, Spawns",
            "Tool 3 (Pipette): LMB applies active texture, RMB samples clicked brush texture",
            "E Key / LMB: Place object on grid at 3D cursor (Brush, Prop, Door, Spawn)",
            "F Key: Center & Focus Camera on selected entity",
            "Ctrl + D: Duplicate selected entity offset on grid",
            "Delete / Backspace: Delete selected entity",
            "Arrow Keys / PageUp / PageDn: Translate selected object along grid axes",
            "F9: Quick Save and Launch Map in Frozen-Life Engine (Lab.exe)",
            "Optimization: Frustum culling skips rendering off-screen brushes and props"
        };

        for (int i = 0; i < 10; ++i) {
            LabFont::drawText(bx + 25.0f, by + 45.0f + i * 34.0f, helpLines[i], 1.5f, Vec3(0.90f, 0.92f, 0.95f), LabFontType::System);
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

    // Lamp / Light Tool State
    Vec3 _lampColor{ 1.0f, 0.95f, 0.85f }; // Warm 2700K incandescent
    float _lampIntensity = 2.5f;
    float _lampRadius = 14.0f;
    MapLightType _lampType = MapLightType::Point;

    // CSG Clipping Tool State
    ClipMode _clipMode = ClipMode::KeepBoth;
    Vec3 _clipPointA{ 0, 0, 0 };
    Vec3 _clipPointB{ 0, 0, 0 };
    int _clipStep = 0;

    std::vector<std::string> _consoleMessages;
    std::string _cullingStats = "";

    // Input States
    bool _isFlyingCamera = false;
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
    float _mouseScreenX = 0.0f;
    float _mouseScreenY = 0.0f;
};

int main() {
    LabHammerStandalone hammer;
    hammer.run();
    return 0;
}
