# LabHammer (3D Level Editor)

[![Engine: Lab](https://img.shields.io/badge/Engine-Lab-blue?style=for-the-badge)](https://github.com/YoungJasiek/Lab)
[![Docs](https://img.shields.io/badge/Docs-VDC%20Online-green?style=for-the-badge)](https://youngjasiek.github.io/Lab/labhammer.html)
[![Output](https://img.shields.io/badge/Output-LabHammer.exe-blueviolet?style=for-the-badge)](https://github.com/YoungJasiek/labhammer)

**LabHammer** is a standalone Valve Hammer inspired CAD 3D level editor for authoring geometry, dynamic doors, light sources, player spawns, and weapon pickup nodes in plain-text `.labmap` files for the **Lab Engine**.

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
© 2026 YoungJasiek. Licensed under the MIT License.
