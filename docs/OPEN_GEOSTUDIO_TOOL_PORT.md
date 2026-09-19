# OpenGeoStudio to InfraForge Tool Porting Guide

This document audits and classifies every authoring tool from the **OpenGeoStudio** donor repository (`OpenGeoStudio-Qt`) and defines its implementation path into **InfraForge**.

---

## Architectural Principles & Non-Negotiable Rules

1. **Native Architectural Authority**:
   InfraForge strictly follows:
   $$\text{Frontend UI} \longrightarrow \text{Protocol Command} \longrightarrow \text{Application Service} \longrightarrow \text{Native Domain} \longrightarrow \text{Persistence} \longrightarrow \text{Derived Tessellation} \longrightarrow \text{Vulkan Viewport}$$
   The React frontend is never the authoritative owner of road geometry.

2. **True Mathematical Representation**:
   Arcs, straight segments, and clothoids are canonical mathematical primitives (`CircularArcSegment`, `LineSegment`, `ClothoidSegment`). They must **never** be flattened into dense polylines upon authoring. Polyline sampling is used only for transient viewport preview and derived mesh tessellation.

3. **No Fake Features or Shortcuts**:
   Do not introduce placeholder tools that appear in the UI without working end-to-end. Every tool merged into InfraForge must persist to SQLite, survive save/reopen, support undo/redo, update the world partition, and render through the Vulkan viewport.

4. **Transient Draft vs. Atomic Commit**:
   Drafting (pointer clicks, preview lines, hover snapping) is ephemeral UI state. Only upon tool completion condition (e.g., third point of an arc, Enter on polyline) is an atomic native engine command issued.

---

## Tool Classification Matrix

| Tool ID | Donor Name | Classification | Donor Files & Functions | Target InfraForge Subsystem |
| :--- | :--- | :--- | :--- | :--- |
| `select` | Select | **Implemented (Milestone A/B)** | `src/pages/EditorPage.tsx`<br>`src/editor/tooling.ts` | `apps/frontend/src/editor/selection/`<br>`apps/frontend/src/editor/tools/` |
| `draw-straight` | Insert Segment | **Implemented (Milestone B / First PR)** | `src/pages/EditorPage.tsx`<br>`src/engine/xyFunctions.ts` | `engine/domain/road/AlignmentPrimitives.hpp`<br>`engine/application/RoadService.cpp`<br>`apps/frontend/src/features/road/` |
| `draw-arc` | Insert Circle Arc | **Implemented (Milestone B / First PR)** | `src/engine/arcFitting.ts` (`fitArcThroughPoints`)<br>`src/pages/EditorPage.tsx` | `engine/domain/road/AlignmentPrimitives.hpp`<br>`engine/application/RoadService.cpp`<br>`apps/frontend/src/features/road/` |
| `draw-clothoid` | Insert Clothoid Arc | **Implemented (Milestone B / First PR)** | `src/engine/clothoid.ts`<br>`src/pages/EditorPage.tsx` | `engine/domain/road/AlignmentPrimitives.hpp`<br>`engine/application/RoadService.cpp`<br>`apps/frontend/src/features/road/` |
| `draw-polyline` | Insert Polyline | **Implemented (Milestone B / First PR)** | `src/pages/EditorPage.tsx`<br>`src/editor/DraftPointsToolbar.tsx` | `engine/application/RoadService.cpp` (`createRoad`)<br>`apps/frontend/src/features/road/roadToolStore.ts` |
| `draw-bezier` | Insert Bezier | **Planned (Milestone E)** | `src/engine/geometry.ts`<br>`src/pages/EditorPage.tsx` | `engine/domain/road/AlignmentFitter.cpp`<br>`apps/frontend/src/features/road/` |
| `draw-spline` | Insert ClothoidSpline | **Planned (Milestone E)** | `src/engine/clothoidSpline.ts`<br>`src/pages/EditorPage.tsx` | `engine/domain/road/AlignmentFitter.cpp`<br>`apps/frontend/src/features/road/` |
| `move` | Move End / Control | **Partial / Existing Backend** | `src/pages/EditorPage.tsx`<br>`src/engine/roadGeometry.ts` | `engine/application/RoadService.cpp` (`moveControl`)<br>`apps/frontend/src/features/road/` |
| `insert-control` | Insert Control Point | **Partial / Existing Backend** | `src/pages/EditorPage.tsx` | `engine/application/RoadService.cpp` (`insertControl`)<br>`apps/frontend/src/features/road/` |
| `extend` | Extend | **Planned (Milestone C)** | `src/pages/EditorPage.tsx`<br>`src/engine/tracks.ts` | `engine/domain/road/Road.cpp`<br>`engine/application/RoadService.cpp` |
| `split` | Split | **Planned (Milestone C)** | `src/engine/tracks.ts` (`splitTrack`)<br>`src/engine/elevation.ts` | `engine/domain/road/Road.cpp`<br>`engine/application/RoadService.cpp` |
| `delete` | Delete | **Implemented** | `src/pages/EditorPage.tsx` | `engine/application/RoadService.cpp` (`deleteRoad`)<br>`road.proto` (`DeleteRoadCommand`) |
| `insert-intersection`| Insert Intersection | **Blocked by #8 (network topology domain)** | `src/engine/intersections.ts`<br>`src/pages/EditorPage.tsx` | `engine/domain/network/` (Future topology domain) |
| `junction` | Junction | **Blocked by #8 (network topology domain)** | `src/engine/junctions.ts`<br>`src/pages/EditorPage.tsx` | `engine/domain/network/` (Future topology domain) |
| `lane-begin` | Begin Lane | **Blocked by missing domain** | `src/lanes/LanesPanel.tsx` | `engine/domain/lane/` (Future lane domain) |
| `lane-end` | End Lane | **Blocked by missing domain** | `src/lanes/LanesPanel.tsx` | `engine/domain/lane/` (Future lane domain) |
| `lane-insert` | Insert Lane | **Blocked by missing domain** | `src/lanes/LanesPanel.tsx` | `engine/domain/lane/` (Future lane domain) |
| `lane-remove` | Remove Lane | **Blocked by missing domain** | `src/lanes/LanesPanel.tsx` | `engine/domain/lane/` (Future lane domain) |
| `lane-border` | Edit Border | **Blocked by missing domain** | `src/lanes/` | `engine/domain/lane/` (Future lane domain) |
| `lane-sidewalk` | Add Sidewalk | **Blocked by missing domain** | `src/lanes/` | `engine/domain/lane/` (Future lane domain) |
| `rail-point` | Insert Turnout | **Future milestone (Milestone H)** | `src/domain/rail.ts`<br>`src/editor/sidebar/TrainTab.tsx` | `engine/domain/rail/` |
| `rail-crossing` | Insert Crossing | **Future milestone (Milestone H)** | `src/domain/rail.ts`<br>`src/editor/sidebar/TrainTab.tsx` | `engine/domain/rail/` |
| `catch-point` | Catch Point | **Future milestone (Milestone H)** | `src/domain/rail.ts`<br>`src/editor/sidebar/TrainTab.tsx` | `engine/domain/rail/` |

---

## Detailed Analysis by Category

### 1. Road Authoring Curves

#### Circle Arc (`road.arc`)
- **Donor Math**: `OpenGeoStudio-Qt/src/engine/arcFitting.ts` (`fitArcThroughPoints`).
- **Algorithm**: Given $P_0, P_1, P_2$, computes circumcenter $(x_c, y_c)$ via determinant $\Delta = 2(x_0(y_1-y_2) + x_1(y_2-y_0) + x_2(y_0-y_1))$, checks collinearity ($|\Delta| < 10^{-7}$), determines turn direction ($+1$ CCW, $-1$ CW) by testing if $P_1$ angle lies within sweep from $P_0$ to $P_2$, derives tangent start heading $\psi_0 = \theta_0 \pm \pi/2$, signed curvature $\kappa = \pm 1/R$, and arc length $L = R \cdot \theta_{\text{sweep}}$.
- **InfraForge Implementation**: Pure native C++ in `AlignmentPrimitives.cpp` returning `std::expected<CircularArcSegment, RoadDiagnostic>`. Creates `RoadRecord` with a single canonical `CircularArcSegment`. Never converted to polyline.

#### Straight Segment (`road.straight`)
- **Donor Math**: Straight vector between $P_0$ and $P_1$.
- **InfraForge Implementation**: Constructs canonical `LineSegment` with start point $P_0$, heading $\psi = \text{atan2}(dy, dx)$, and length $L = \text{hypot}(dx, dy)$.

#### Clothoid Arc (`road.clothoid`)
- **Donor Math**: `OpenGeoStudio-Qt/src/engine/clothoid.ts`.
- **InfraForge Implementation**: Constructs canonical `ClothoidSegment` with start curvature $\kappa_0$, end curvature $\kappa_1 = \pm 1 / R_{\text{out}}$, start heading $\psi_0$, and arc length $L$.

#### Polyline (`road.polyline`)
- **Donor Math**: Point accumulation and multi-section curve fitting.
- **InfraForge Implementation**: Reuses existing canonical `RoadService::createRoad` / `fitAlignment` pipeline.

---

## Precision & Snapping Foundation

- **Donor Reference**: `OpenGeoStudio-Qt/src/editor/snapping.ts`.
- **InfraForge Snapping Abstraction**: `SnapService` managing:
  1. `gridSnap`: Snaps point to nearest configured grid interval ($0.5\text{m}, 1\text{m}, 2\text{m}, 5\text{m}, 10\text{m}, 25\text{m}, 50\text{m}$).
  2. `angleSnap`: Constrains heading from anchor point to steps of $15^\circ, 30^\circ, 45^\circ, 90^\circ$.
  3. `endpointSnap`: Snaps to closest road endpoint (start station $0$ or end station $L$) within threshold radius ($15\text{m}$).
  4. `centerlineSnap`: Projects point onto active canonical road alignments.

---

## Roadmap & Milestones

- **Milestone A & B (First PR)**:
  - Unified Authoring Tool Foundation & Authoring Tool State (`AuthoringToolId`).
  - Tool Rail UI & Tool Options Panel.
  - Keyboard Shortcuts (`V`, `S`, `A`, `C`, `P`, `Esc`, `Enter`, `Backspace`, `Del`, `Ctrl+Z`, `Ctrl+Y`).
  - Snapping Foundation (`SnapService`).
  - Canonical 3-Point Arc Construction & Native `CreateArcRoadCommand`.
  - Canonical Straight Segment & Native `CreateStraightRoadCommand`.
  - Canonical Clothoid Segment & Native `CreateClothoidRoadCommand`.
  - Polyline Tool integration.
  - Transient preview mesh pipeline (`__authoring_preview__`).
- **Milestone C**: Modification Tools (`move`, `extend`, `split`, `delete`).
- **Milestone D**: Precision Snapping enhancements (perpendicular, tangent, station snap).
- **Milestone E**: Advanced Curve authoring (Bezier, ClothoidSpline with native fitting).
- **Milestone F**: Lane Tools (native lane domain and the first lane editor/presets exist; additional direct lane tools remain planned).
- **Milestone G**: Network Tools (native junction topology and the first junction inspector/CRUD path exist; interactive network construction remains planned).
- **Milestone H**: Rail Authoring & Fixtures.

## Verification status

Automated protocol, TypeScript, frontend/desktop, native CTest, self-check, and authenticated engine-smoke verification is green for the consolidated implementation. A live Windows/Vulkan pass created and persisted all four Milestone A/B road types and reopened the resulting project successfully. Terrain-backed conformance, endpoint snapping, canonical picking/control/profile editing, lane editing, junction editing, and their complete persistence round-trip still require recorded manual acceptance; the tool-port statuses above do not imply those workflows were manually verified together.
