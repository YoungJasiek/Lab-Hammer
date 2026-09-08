# LabHammer (Level Editor)

[![Engine: Lab](https://img.shields.io/badge/Engine-Lab-blue?style=for-the-badge)](https://github.com/YoungJasiek/Lab)
[![Docs](https://img.shields.io/badge/Docs-VDC%20Online-green?style=for-the-badge)](https://youngjasiek.github.io/Lab/labhammer.html)

**LabHammer** is a standalone, Source Engine / Valve Hammer Editor styled 3D Level Editor built for creating maps (.labmap) for **Frozen Life**.

---

## đź“– Online Documentation
Editor shortcuts, CSG clipping operations, entity placement, and map compilation documentation:
đź‘‰ **[https://youngjasiek.github.io/Lab/labhammer.html](https://youngjasiek.github.io/Lab/labhammer.html)**

---

## đź› ď¸Ź Editor Features
- **CSG Constructive Solid Geometry**: Real-time Carve, Clip, Hollow, Union, and Intersection operations.
- **Quad-View & 3D Textured Viewport**: Top (XY), Front (XZ), Side (YZ), and fully textured real-time OpenGL 3D perspective.
- **Entity Placement**: Player spawns, weapon spawn nodes, dynamic props, light sources, and interactive terminals.
- **Texture Browser**: Texture application, alignment, scaling, and rotation.
- **Undo / Redo Stack**: Comprehensive transactional operation history.

---

## đźš€ Building and Running
``powershell
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
.\Release\LabHammer.exe
``

---
Â© 2026 YoungJasiek. Licensed under the MIT License.
