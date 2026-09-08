# Lab Hammer (3D CAD Level Editor)

[![Engine: Lab](https://img.shields.io/badge/Engine-Lab-blue?style=for-the-badge)](https://github.com/YoungJasiek/Lab)
[![Docs](https://img.shields.io/badge/Docs-VDC%20Online-green?style=for-the-badge)](https://youngjasiek.github.io/Lab/labhammer.html)
[![Repository](https://img.shields.io/badge/GitHub-Lab--Hammer-blueviolet?style=for-the-badge)](https://github.com/YoungJasiek/Lab-Hammer)

**Lab Hammer** is a standalone CAD 3D level editor for authoring brush geometry, dynamic doors, light sources, player spawns, and weapon pickup nodes in plain-text `.labmap` files for the **Lab Engine**.

---

## 📖 Online Documentation
Full editor manuals, keyboard shortcuts, CSG brush clipping workflows, and `.labmap` format specifications are published on the official documentation portal:
👉 **[https://youngjasiek.github.io/Lab/labhammer.html](https://youngjasiek.github.io/Lab/labhammer.html)**

---

## 🚀 Building and Running
```powershell
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
.\Release\LabHammer.exe
```

---

## 🛠️ Editor Features
- **CSG Brush Polygon Clipping**: Real-time Sutherland-Hodgman convex polyhedron carving and clipping.
- **Multi-Viewport Navigation**: Isometric and perspective freecam viewports with grid snapping.
- **Native Level Serialization**: Instant read and write of human-readable `.labmap` files.

---

## 👤 Author & Acknowledgments
* **Creator & Architect:** [YoungJasiek](https://github.com/YoungJasiek)
* **Special Thanks:** Sincere gratitude to **Valve Corporation** for developing the legendary Hammer Editor and Source Engine, whose iconic CAD toolsets inspired the development of Lab Hammer.
