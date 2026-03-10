# Ralph Task Registry — OrcaSlicer Analysis Agent
Last updated: 2026-03-10T22:26:09Z

## Legend
- [ ] PENDING   — not started
- [/] ACTIVE    — in progress (only ONE task should be ACTIVE at a time)
- [x] DONE      — complete and committed
- [!] BLOCKED   — cannot proceed without resolution (add reason inline)

---

## Phase BOOTSTRAP — Housekeeping (Complete before resuming Phase 1/2)

- [x] TB001  BOOTSTRAP · Annotate `\.ralph\ralph-tasks.md` with Phase 1/2 tasks and
             backfill all already-completed tasks as [x] DONE,
             using the session log in agent_journal.md as the source of truth.
             Assign T1xx IDs to every annotated file. Commit with prefix `docs:`.

- [x] TB002  BOOTSTRAP · Add the CURRENT STATUS living block to the very top of
             `agent_journal.md` (above Session 1). Populate it from Session 84
             state: annotation pass complete, Phase 2 docs in progress, H1191 is
             next hazard ID, open questions from Session 1 items 1–4 — verify
             which are still unresolved and list them. Commit with prefix `docs:`.

- [x] TB003  BOOTSTRAP · Compact `agent_journal.md`. The file is ~6000 lines
             covering 84 sessions. Archive Sessions 1–83 into a single new file
             `agent_journal_archive_s01_s83.md` (verbatim, no summarization),
             then replace those sessions in agent_journal.md with a single
             two-line entry:
               `## Sessions 1–83 — Archived`
               `Full log: agent_journal_archive_s01_s83.md`
             Keep Session 84 and the new CURRENT STATUS block intact.
             Commit with prefix `docs:`.

- [x] TB004  BOOTSTRAP · Add the `## Critical Blockers (P1/High — Read First)`
             section to `04_refactoring_hazards.md`, immediately after the Table
             of Contents. Scan the entire file for every entry marked `P1/High`
             and list each as a single line:
               `H<id> · <10-word description> · <file:line>`
             Do not paraphrase existing entries — extract directly.
             Commit with prefix `docs:`.

- [x] TB004b  BOOTSTRAP · Audit `04_refactoring_hazards.md` for misplaced content:
              find any multi-paragraph algorithm explanations that duplicate
              `03_algorithmic_complexities.md` and replace them with a single
              cross-reference line pointing to the relevant section of that file.
              Do NOT remove any H-numbered hazard entries. Do NOT summarize
              individual entries. Only remove prose that belongs in a different
              document. Commit with prefix `docs:`.

- [x] TB005  BOOTSTRAP · Resolve or explicitly escalate the four open questions
             recorded in Session 1 of agent_journal.md:
             (1) Does slice_mesh use the admesh adjacency table or rebuild it?
             (2) Exact memory ownership of Layer objects (raw vs unique_ptr)?
             (3) Are Arachne and classic PerimeterGenerator mutually exclusive?
             (4) What is the purpose of ClipperZUtils Z-metadata tagging?
             For each: read the relevant source file, write a one-paragraph
             resolution into the CURRENT STATUS open questions block, and add
             an [UNCLEAR] → [RESOLVED] annotation at the original call site.
             Commit with prefix `annotate:`.

---

## Phase 0 — Orientation

- [x] T001  Read CMakeLists.txt and README (Session 1)
- [x] T002  Trace entry point — OrcaSlicer.cpp main() (Session 1)
- [x] T003  Directory listing: libslic3r/, GCode/, Fill/, Support/, Arachne/ (Session 1)
- [x] T004  Create /generated_documentation/ folder (Session 1)
- [x] T005  Write initial agent_journal.md entry (Session 1)
- [x] T006  Initial git commit (unmodified codebase baseline) (Session 1)

---

## Phase 1 — Annotation
(All source files annotated across Sessions 2–84; annotation pass COMPLETE)

### Entry Point & Application Lifecycle
- [x] T101  annotate: src/OrcaSlicer.cpp (Sessions 2–3)
- [x] T102  annotate: src/OrcaSlicer.hpp (Session 1)

### Print Orchestration
- [x] T103  annotate: src/libslic3r/Print.cpp (Session 77)
- [x] T104  annotate: src/libslic3r/Print.hpp (Session 1 / Session 76)
- [x] T105  annotate: src/libslic3r/PrintObject.cpp (Session 75)
- [x] T106  annotate: src/libslic3r/PrintBase.cpp (Session 54)
- [x] T107  annotate: src/libslic3r/PrintBase.hpp (Session 54)
- [x] T108  annotate: src/libslic3r/PrintApply.cpp (Session 55)
- [x] T109  annotate: src/libslic3r/PrintRegion.cpp (Session 55)
- [x] T110  annotate: src/libslic3r/PrintConfig.cpp (Sessions 48–49 / Session 63)
- [x] T111  annotate: src/libslic3r/PrintConfig.hpp (Sessions 42 / Session 76)

### Slicing Engine
- [x] T112  annotate: src/libslic3r/TriangleMeshSlicer.cpp (Sessions 2 / 9)
- [x] T113  annotate: src/libslic3r/TriangleMeshSlicer.hpp (Session 1)
- [x] T114  annotate: src/libslic3r/TriangleMesh.cpp (Session 27)
- [x] T115  annotate: src/libslic3r/TriangleMesh.hpp (Session 27)
- [x] T116  annotate: src/libslic3r/Slicing.cpp (Session 53)
- [x] T117  annotate: src/libslic3r/Slicing.hpp (Session 53)
- [x] T118  annotate: src/libslic3r/SlicingAdaptive.cpp (Session 78)
- [x] T119  annotate: src/libslic3r/PrintObjectSlice.cpp (Session 2)

### Model & Mesh
- [x] T120  annotate: src/libslic3r/Model.cpp (Sessions 31–32)
- [x] T121  annotate: src/libslic3r/Model.hpp (Session 30)
- [x] T122  annotate: src/libslic3r/TriangleMeshDeal.cpp (Session 84)
- [x] T123  annotate: src/libslic3r/TriangleSetSampling.cpp (Session 84)
- [x] T124  annotate: src/libslic3r/TriangulateWall.cpp (Session 84)
- [x] T125  annotate: src/libslic3r/Triangulation.cpp (Session 83)
- [x] T126  annotate: src/libslic3r/MeshBoolean.cpp (Session 71)
- [x] T127  annotate: src/libslic3r/MeshBoolean.hpp (Session 71)
- [x] T128  annotate: src/libslic3r/OpenVDBUtils.cpp (Session 71)
- [x] T129  annotate: src/libslic3r/OpenVDBUtils.hpp (Session 71)
- [x] T130  annotate: src/libslic3r/QuadricEdgeCollapse.cpp (Session 82)
- [x] T131  annotate: src/libslic3r/ShortEdgeCollapse.cpp (Session 84)
- [x] T132  annotate: src/libslic3r/SlicesToTriangleMesh.cpp (Session 84)
- [x] T133  annotate: src/libslic3r/Orient.cpp (Session 71)
- [x] T134  annotate: src/libslic3r/Orient.hpp (Session 71)

### Perimeter & Fill
- [x] T135  annotate: src/libslic3r/PerimeterGenerator.cpp (Sessions 2 / 74)
- [x] T136  annotate: src/libslic3r/PerimeterGenerator.hpp (Session 73)
- [x] T137  annotate: src/libslic3r/Fill/Fill.cpp (Session 2)
- [x] T138  annotate: src/libslic3r/Fill/FillBase.cpp (Session 21)
- [x] T139  annotate: src/libslic3r/Fill/FillBase.hpp (Session 21)
- [x] T140  annotate: src/libslic3r/Fill/FillRectilinear.cpp (Session 20)
- [x] T141  annotate: src/libslic3r/Fill/FillAdaptive.cpp (Session 22)
- [x] T142  annotate: src/libslic3r/Fill/FillLightning.cpp (Session 23)
- [x] T143  annotate: src/libslic3r/Fill/FillLightning.hpp (Session 23)
- [x] T144  annotate: src/libslic3r/Fill/Lightning/Generator.cpp (Session 23)
- [x] T145  annotate: src/libslic3r/Fill/Lightning/Generator.hpp (Session 23)
- [x] T146  annotate: src/libslic3r/Fill/Lightning/Layer.cpp (Session 23)
- [x] T147  annotate: src/libslic3r/Fill/Lightning/Layer.hpp (Session 23)
- [x] T148  annotate: src/libslic3r/Fill/Lightning/TreeNode.cpp (Session 23)
- [x] T149  annotate: src/libslic3r/Fill/Lightning/TreeNode.hpp (Session 23)
- [x] T150  annotate: src/libslic3r/Fill/FillGyroid.cpp (Session 24)
- [x] T151  annotate: src/libslic3r/Fill/FillGyroid.hpp (Session 24)
- [x] T152  annotate: src/libslic3r/Fill/FillConcentric.cpp (Session 24)
- [x] T153  annotate: src/libslic3r/Fill/FillConcentric.hpp (Session 24)
- [x] T154  annotate: src/libslic3r/Fill/FillHoneycomb.cpp (Session 65)
- [x] T155  annotate: src/libslic3r/Fill/FillHoneycomb.hpp (Session 65)
- [x] T156  annotate: src/libslic3r/Fill/Fill3DHoneycomb.cpp (Session 65)
- [x] T157  annotate: src/libslic3r/Fill/Fill3DHoneycomb.hpp (Session 65)
- [x] T158  annotate: src/libslic3r/Fill/FillLine.cpp (Session 21)
- [x] T159  annotate: src/libslic3r/Fill/FillLine.hpp (Session 21)
- [x] T160  annotate: src/libslic3r/Fill/FillPlanePath.cpp (Session 21)
- [x] T161  annotate: src/libslic3r/Fill/FillPlanePath.hpp (Session 21)
- [x] T162  annotate: src/libslic3r/Fill/FillCrossHatch.cpp (Session 21)
- [x] T163  annotate: src/libslic3r/Fill/FillCrossHatch.hpp (Session 21)

### Arachne Variable-Width Perimeter
- [x] T164  annotate: src/libslic3r/Arachne/WallToolPaths.cpp (Session 15)
- [x] T165  annotate: src/libslic3r/Arachne/WallToolPaths.hpp (Session 15)
- [x] T166  annotate: src/libslic3r/Arachne/SkeletalTrapezoidation.cpp (Session 16)
- [x] T167  annotate: src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp (Session 18)
- [x] T168  annotate: src/libslic3r/Arachne/SkeletalTrapezoidationGraph.hpp (Session 18)
- [x] T169  annotate: src/libslic3r/Arachne/SkeletalTrapezoidationEdge.hpp (Session 16)
- [x] T170  annotate: src/libslic3r/Arachne/SkeletalTrapezoidationJoint.hpp (Session 16)
- [x] T171  annotate: src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.cpp (Session 17)
- [x] T172  annotate: src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp (Session 17)
- [x] T173  annotate: src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp (Session 17)
- [x] T174  annotate: src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp (Session 17)
- [x] T175  annotate: src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp (Session 17)
- [x] T176  annotate: src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp (Session 17)
- [x] T177  annotate: src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp (Session 17)
- [x] T178  annotate: src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp (Session 17)
- [x] T179  annotate: src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp (Session 17)
- [x] T180  annotate: src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp (Session 17)
- [x] T181  annotate: src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp (Session 17)
- [x] T182  annotate: src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp (Session 17)
- [x] T183  annotate: src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.cpp (Session 17)
- [x] T184  annotate: src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.hpp (Session 17)

### GCode Generation
- [x] T185  annotate: src/libslic3r/GCode.cpp (Session 2)
- [x] T186  annotate: src/libslic3r/GCodeWriter.cpp (Session 2)
- [x] T187  annotate: src/libslic3r/GCode/GCodeProcessor.cpp (Session 12)
- [x] T188  annotate: src/libslic3r/GCode/GCodeProcessor.hpp (Session 19)
- [x] T189  annotate: src/libslic3r/GCode/ToolOrdering.cpp (Session 3)
- [x] T190  annotate: src/libslic3r/GCode/CoolingBuffer.cpp (Session 3)
- [x] T191  annotate: src/libslic3r/GCode/SeamPlacer.cpp (Session 3)
- [x] T192  annotate: src/libslic3r/GCode/WipeTower2.cpp (Session 4)
- [x] T193  annotate: src/libslic3r/GCode/WipeTower2.hpp (Session 4)
- [x] T194  annotate: src/libslic3r/GCode/PressureEqualizer.cpp (Sessions 6–8)
- [x] T195  annotate: src/libslic3r/GCode/PressureEqualizer.hpp (Sessions 6–8)
- [x] T196  annotate: src/libslic3r/GCode/FanMover.cpp (Session 13)
- [x] T197  annotate: src/libslic3r/GCode/FanMover.hpp (Session 13)
- [x] T198  annotate: src/libslic3r/GCode/AdaptivePAProcessor.cpp (Session 14)
- [x] T199  annotate: src/libslic3r/GCode/AdaptivePAProcessor.hpp (Session 14)
- [x] T200  annotate: src/libslic3r/GCode/AdaptivePAInterpolator.cpp (Session 14)
- [x] T201  annotate: src/libslic3r/GCode/AdaptivePAInterpolator.hpp (Session 14)
- [x] T202  annotate: src/libslic3r/GCode/AvoidCrossingPerimeters.cpp (Session 10)
- [x] T203  annotate: src/libslic3r/GCode/ConflictChecker.cpp (Session 58)
- [x] T204  annotate: src/libslic3r/GCode/ConflictChecker.hpp (Session 58)
- [x] T205  annotate: src/libslic3r/GCode/PostProcessor.cpp (Session 58)
- [x] T206  annotate: src/libslic3r/GCode/PostProcessor.hpp (Session 58)
- [x] T207  annotate: src/libslic3r/GCode/SpiralVase.cpp (Session 58)
- [x] T208  annotate: src/libslic3r/GCode/SpiralVase.hpp (Session 58)
- [x] T209  annotate: src/libslic3r/GCode/ToolOrderUtils.cpp (Session 58)
- [x] T210  annotate: src/libslic3r/GCode/ToolOrderUtils.hpp (Session 58)
- [x] T211  annotate: src/libslic3r/GCode/PrintExtents.cpp (Session 59)
- [x] T212  annotate: src/libslic3r/GCode/PrintExtents.hpp (Session 59)
- [x] T213  annotate: src/libslic3r/GCode/RetractWhenCrossingPerimeters.cpp (Session 59)
- [x] T214  annotate: src/libslic3r/GCode/RetractWhenCrossingPerimeters.hpp (Session 59)
- [x] T215  annotate: src/libslic3r/GCode/SmallAreaInfillFlowCompensator.cpp (Session 59)
- [x] T216  annotate: src/libslic3r/GCode/SmallAreaInfillFlowCompensator.hpp (Session 59)
- [x] T217  annotate: src/libslic3r/GCodeReader.cpp (Session 58)
- [x] T218  annotate: src/libslic3r/GCodeReader.hpp (Session 58)

### Support Generation
- [x] T219  annotate: src/libslic3r/Support/SupportMaterial.cpp (Session 5)
- [x] T220  annotate: src/libslic3r/Support/SupportMaterial.hpp (Session 5)
- [x] T221  annotate: src/libslic3r/Support/TreeSupport.cpp (Session 6)
- [x] T222  annotate: src/libslic3r/Support/TreeSupport.hpp (Session 6)
- [x] T223  annotate: src/libslic3r/Support/TreeSupport3D.cpp (Session 7)
- [x] T224  annotate: src/libslic3r/Support/TreeSupport3D.hpp (Session 7)
- [x] T225  annotate: src/libslic3r/Support/TreeModelVolumes.cpp (Session 8)
- [x] T226  annotate: src/libslic3r/Support/TreeModelVolumes.hpp (Session 8)
- [x] T227  annotate: src/libslic3r/Support/SupportCommon.cpp (Session 60)
- [x] T228  annotate: src/libslic3r/Support/SupportCommon.hpp (Session 60)
- [x] T229  annotate: src/libslic3r/Support/SupportSpotsGenerator.cpp (Session 61)
- [x] T230  annotate: src/libslic3r/Support/SupportSpotsGenerator.hpp (Session 61)
- [x] T231  annotate: src/libslic3r/Support/SupportLayer.hpp (Session 62)
- [x] T232  annotate: src/libslic3r/Support/SupportParameters.hpp (Session 61)
- [x] T233  annotate: src/libslic3r/Support/TreeSupportCommon.hpp (Session 62)

### Geometry & Math Utilities
- [x] T234  annotate: src/libslic3r/Geometry.cpp (Session 34)
- [x] T235  annotate: src/libslic3r/Geometry/ArcWelder.cpp (Session 38)
- [x] T236  annotate: src/libslic3r/Geometry/ArcWelder.hpp (Session 38)
- [x] T237  annotate: src/libslic3r/Geometry/Circle.cpp (Session 38)
- [x] T238  annotate: src/libslic3r/Geometry/Circle.hpp (Session 38)
- [x] T239  annotate: src/libslic3r/Geometry/ConvexHull.cpp (Session 38)
- [x] T240  annotate: src/libslic3r/Geometry/ConvexHull.hpp (Session 38)
- [x] T241  annotate: src/libslic3r/Geometry/Curves.hpp (Session 38)
- [x] T242  annotate: src/libslic3r/Geometry/MedialAxis.cpp (Session 38)
- [x] T243  annotate: src/libslic3r/Geometry/MedialAxis.hpp (Session 38)
- [x] T244  annotate: src/libslic3r/Geometry/Voronoi.cpp (Session 38)
- [x] T245  annotate: src/libslic3r/Geometry/Voronoi.hpp (Session 38)
- [x] T246  annotate: src/libslic3r/Geometry/VoronoiOffset.cpp (Session 38)
- [x] T247  annotate: src/libslic3r/Geometry/VoronoiOffset.hpp (Session 38)
- [x] T248  annotate: src/libslic3r/Geometry/VoronoiUtils.cpp (Session 38)
- [x] T249  annotate: src/libslic3r/Geometry/VoronoiUtils.hpp (Session 38)
- [x] T250  annotate: src/libslic3r/Geometry/VoronoiUtilsCgal.cpp (Session 38)
- [x] T251  annotate: src/libslic3r/Geometry/VoronoiUtilsCgal.hpp (Session 38)
- [x] T252  annotate: src/libslic3r/Geometry/Bicubic.hpp (Session 38)
- [x] T253  annotate: src/libslic3r/Geometry/VoronoiVisualUtils.hpp (Session 38)
- [x] T254  annotate: src/libslic3r/ArcFitter.cpp (Session 47)
- [x] T255  annotate: src/libslic3r/ArcFitter.hpp (Session 47)
- [x] T256  annotate: src/libslic3r/Circle.cpp (Session 84)
- [x] T257  annotate: src/libslic3r/Circle.hpp (Session 47)
- [x] T258  annotate: src/libslic3r/EdgeGrid.cpp (Session 35)
- [x] T259  annotate: src/libslic3r/MinAreaBoundingBox.cpp (Session 84)
- [x] T260  annotate: src/libslic3r/MinimumSpanningTree.cpp (Session 84)
- [x] T261  annotate: src/libslic3r/PrincipalComponents2D.cpp (Session 84)
- [x] T262  annotate: src/libslic3r/NormalUtils.cpp (Session 84)
- [x] T263  annotate: src/libslic3r/IntersectionPoints.cpp (Session 84)

### Polygon & Clipper
- [x] T264  annotate: src/libslic3r/ClipperUtils.cpp (Session 29)
- [x] T265  annotate: src/libslic3r/ClipperUtils.hpp (Session 28)
- [x] T266  annotate: src/libslic3r/Clipper2Utils.cpp (Session 83)
- [x] T267  annotate: src/libslic3r/Point.cpp (Session 39)
- [x] T268  annotate: src/libslic3r/Point.hpp (Session 39)
- [x] T269  annotate: src/libslic3r/BoundingBox.cpp (Session 39)
- [x] T270  annotate: src/libslic3r/BoundingBox.hpp (Session 39)
- [x] T271  annotate: src/libslic3r/Polygon.cpp (Session 39)
- [x] T272  annotate: src/libslic3r/Polygon.hpp (Session 39)
- [x] T273  annotate: src/libslic3r/Polyline.cpp (Session 39)
- [x] T274  annotate: src/libslic3r/Polyline.hpp (Session 39)
- [x] T275  annotate: src/libslic3r/ExPolygon.cpp (Session 39)
- [x] T276  annotate: src/libslic3r/ExPolygon.hpp (Session 39)
- [x] T277  annotate: src/libslic3r/ExPolygonCollection.cpp (Session 84)
- [x] T278  annotate: src/libslic3r/ExPolygonsIndex.cpp (Session 84)
- [x] T279  annotate: src/libslic3r/MutablePolygon.cpp (Session 83)
- [x] T280  annotate: src/libslic3r/PolygonTrimmer.cpp (Session 84)

### Extrusion & Layer
- [x] T281  annotate: src/libslic3r/ExtrusionEntity.cpp (Session 44)
- [x] T282  annotate: src/libslic3r/ExtrusionEntity.hpp (Session 44)
- [x] T283  annotate: src/libslic3r/ExtrusionEntityCollection.cpp (Session 44)
- [x] T284  annotate: src/libslic3r/ExtrusionEntityCollection.hpp (Session 44)
- [x] T285  annotate: src/libslic3r/ExtrusionSimulator.cpp (Session 82)
- [x] T286  annotate: src/libslic3r/LayerRegion.cpp (Session 26)
- [x] T287  annotate: src/libslic3r/ElephantFootCompensation.cpp (Session 48)
- [x] T288  annotate: src/libslic3r/ElephantFootCompensation.hpp (Session 48)
- [x] T289  annotate: src/libslic3r/Flow.cpp (Session 36)
- [x] T290  annotate: src/libslic3r/Flow.hpp (Session 36)
- [x] T291  annotate: src/libslic3r/VariableWidth.cpp (Session 79)
- [x] T292  annotate: src/libslic3r/Brim.cpp (Session 46)
- [x] T293  annotate: src/libslic3r/Brim.hpp (Session 46)

### Algorithms
- [x] T294  annotate: src/libslic3r/Algorithm/LineSplit.cpp (Session 40–41)
- [x] T295  annotate: src/libslic3r/Algorithm/LineSplit.hpp (Session 40)
- [x] T296  annotate: src/libslic3r/Algorithm/RegionExpansion.cpp (Session 41)
- [x] T297  annotate: src/libslic3r/Algorithm/RegionExpansion.hpp (Session 41)
- [x] T298  annotate: src/libslic3r/AABBTreeIndirect.hpp (Session 43)
- [x] T299  annotate: src/libslic3r/AABBTreeLines.hpp (Session 43)
- [x] T300  annotate: src/libslic3r/AABBMesh.cpp (Session 45)
- [x] T301  annotate: src/libslic3r/AABBMesh.hpp (Session 45)
- [x] T302  annotate: src/libslic3r/ShortestPath.cpp (Session 36)
- [x] T303  annotate: src/libslic3r/ShortestPath.hpp (Session 36)
- [x] T304  annotate: src/libslic3r/MultiMaterialSegmentation.cpp (Session 37)
- [x] T305  annotate: src/libslic3r/JumpPointSearch.cpp (Session 78)
- [x] T306  annotate: src/libslic3r/ModelArrange.cpp (Session 83)
- [x] T307  annotate: src/libslic3r/Arrange.cpp (Session 50)
- [x] T308  annotate: src/libslic3r/Arrange.hpp (Session 50)
- [x] T309  annotate: src/libslic3r/FaceDetector.cpp (Session 84)

### Configuration & Presets
- [x] T310  annotate: src/libslic3r/Preset.cpp (Session 57)
- [x] T311  annotate: src/libslic3r/Preset.hpp (Session 57)
- [x] T312  annotate: src/libslic3r/PresetBundle.cpp (Session 80)
- [x] T313  annotate: src/libslic3r/Config.cpp (Session 56)
- [x] T314  annotate: src/libslic3r/Config.hpp (Session 56)
- [x] T315  annotate: src/libslic3r/AppConfig.cpp (Session 81)
- [x] T316  annotate: src/libslic3r/PlaceholderParser.cpp (Session 78)

### SLA Module
- [x] T317  annotate: src/libslic3r/SLAPrint.cpp (Session 52)
- [x] T318  annotate: src/libslic3r/SLAPrint.hpp (Session 52)
- [x] T319  annotate: src/libslic3r/SLAPrintSteps.cpp (Session 52)
- [x] T320  annotate: src/libslic3r/SLAPrintSteps.hpp (Session 52)
- [x] T321  annotate: src/libslic3r/SLA/SpatIndex.cpp (Session 67)
- [x] T322  annotate: src/libslic3r/SLA/SpatIndex.hpp (Session 67)
- [x] T323  annotate: src/libslic3r/SLA/ConcaveHull.cpp (Session 67)
- [x] T324  annotate: src/libslic3r/SLA/ConcaveHull.hpp (Session 67)
- [x] T325  annotate: src/libslic3r/SLA/Clustering.cpp (Session 67)
- [x] T326  annotate: src/libslic3r/SLA/Clustering.hpp (Session 67)
- [x] T327  annotate: src/libslic3r/SLA/RasterBase.cpp (Session 67)
- [x] T328  annotate: src/libslic3r/SLA/RasterBase.hpp (Session 67)
- [x] T329  annotate: src/libslic3r/SLA/RasterToPolygons.cpp (Session 67)
- [x] T330  annotate: src/libslic3r/SLA/RasterToPolygons.hpp (Session 67)
- [x] T331  annotate: src/libslic3r/SLA/AGGRaster.hpp (Session 67)
- [x] T332  annotate: src/libslic3r/SLA/Hollowing.cpp (Session 68)
- [x] T333  annotate: src/libslic3r/SLA/Hollowing.hpp (Session 68)
- [x] T334  annotate: src/libslic3r/SLA/Pad.cpp (Session 68)
- [x] T335  annotate: src/libslic3r/SLA/Pad.hpp (Session 68)
- [x] T336  annotate: src/libslic3r/SLA/Rotfinder.cpp (Session 68)
- [x] T337  annotate: src/libslic3r/SLA/Rotfinder.hpp (Session 68)
- [x] T338  annotate: src/libslic3r/SLA/SupportTree.cpp (Session 68)
- [x] T339  annotate: src/libslic3r/SLA/SupportTree.hpp (Session 68)
- [x] T340  annotate: src/libslic3r/SLA/SupportTreeBuilder.cpp (Session 68)
- [x] T341  annotate: src/libslic3r/SLA/SupportTreeBuilder.hpp (Session 68)
- [x] T342  annotate: src/libslic3r/SLA/SupportTreeBuildsteps.cpp (Session 68)
- [x] T343  annotate: src/libslic3r/SLA/SupportTreeBuildsteps.hpp (Session 68)
- [x] T344  annotate: src/libslic3r/SLA/SupportTreeMesher.cpp (Session 68)
- [x] T345  annotate: src/libslic3r/SLA/SupportTreeMesher.hpp (Session 68)
- [x] T346  annotate: src/libslic3r/SLA/SupportPointGenerator.cpp (Session 66)
- [x] T347  annotate: src/libslic3r/SLA/SupportPointGenerator.hpp (Session 66)
- [x] T348  annotate: src/libslic3r/SLA/SupportPoint.hpp (Session 66)
- [x] T349  annotate: src/libslic3r/SLA/IndexedMesh.cpp (Session 66)
- [x] T350  annotate: src/libslic3r/SLA/IndexedMesh.hpp (Session 66)
- [x] T351  annotate: src/libslic3r/SLA/JobController.hpp (Session 66)
- [x] T352  annotate: src/libslic3r/SLA/Concurrency.hpp (Session 66)
- [x] T353  annotate: src/libslic3r/SLA/BoostAdapter.hpp (Session 66)
- [x] T354  annotate: src/libslic3r/SLA/ReprojectPointsOnMesh.hpp (Session 68)
- [x] T355  annotate: src/libslic3r/SLA/bicubic.h (Session 68)

### Feature Modules
- [x] T356  annotate: src/libslic3r/Feature/FuzzySkin/FuzzySkin.cpp (Session 70)
- [x] T357  annotate: src/libslic3r/Feature/FuzzySkin/FuzzySkin.hpp (Session 70)
- [x] T358  annotate: src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp (Session 70)
- [x] T359  annotate: src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp (Session 70)
- [x] T360  annotate: src/libslic3r/Feature/Interlocking/VoxelUtils.cpp (Session 70)
- [x] T361  annotate: src/libslic3r/Feature/Interlocking/VoxelUtils.hpp (Session 70)

### Execution & CSGMesh
- [x] T362  annotate: src/libslic3r/Execution/Execution.hpp (Session 69)
- [x] T363  annotate: src/libslic3r/Execution/ExecutionSeq.hpp (Session 69)
- [x] T364  annotate: src/libslic3r/Execution/ExecutionTBB.hpp (Session 69)
- [x] T365  annotate: src/libslic3r/CSGMesh/CSGMesh.hpp (Session 69)
- [x] T366  annotate: src/libslic3r/CSGMesh/CSGMeshCopy.hpp (Session 69)
- [x] T367  annotate: src/libslic3r/CSGMesh/ModelToCSGMesh.hpp (Session 69)
- [x] T368  annotate: src/libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp (Session 69)
- [x] T369  annotate: src/libslic3r/CSGMesh/SliceCSGMesh.hpp (Session 69)
- [x] T370  annotate: src/libslic3r/CSGMesh/TriangleMeshAdapter.hpp (Session 69)
- [x] T371  annotate: src/libslic3r/CSGMesh/VoxelizeCSGMesh.hpp (Session 69)

### Optimize Module
- [x] T372  annotate: src/libslic3r/Optimize/BruteforceOptimizer.hpp (Session 69)
- [x] T373  annotate: src/libslic3r/Optimize/NLoptOptimizer.hpp (Session 69)
- [x] T374  annotate: src/libslic3r/Optimize/Optimizer.hpp (Session 69)

### Surface & Emboss
- [x] T375  annotate: src/libslic3r/Surface.cpp (Session 84)
- [x] T376  annotate: src/libslic3r/Surface.hpp (Session 55)
- [x] T377  annotate: src/libslic3r/SurfaceCollection.cpp (Session 55)
- [x] T378  annotate: src/libslic3r/SurfaceCollection.hpp (Session 55)
- [x] T379  annotate: src/libslic3r/CutUtils.cpp (Session 82)
- [x] T380  annotate: src/libslic3r/Emboss.cpp (Session 72)
- [x] T381  annotate: src/libslic3r/Measure.cpp (Session 81)

### Utilities
- [x] T382  annotate: src/libslic3r/utils.cpp (Session 82)
- [x] T383  annotate: src/libslic3r/Thread.cpp (Session 84)
- [x] T384  annotate: src/libslic3r/Time.cpp (Session 84)
- [x] T385  annotate: src/libslic3r/Timer.cpp (Session 84)
- [x] T386  annotate: src/libslic3r/LocalesUtils.cpp (Session 84)
- [x] T387  annotate: src/libslic3r/ObjectID.cpp (Session 84)
- [x] T388  annotate: src/libslic3r/Platform.cpp (Session 84)
- [x] T389  annotate: src/libslic3r/Semver.cpp (Session 84)
- [x] T390  annotate: src/libslic3r/libslic3r.cpp (Session 84)
- [x] T391  annotate: src/libslic3r/pchheader.cpp (Session 84)
- [x] T392  annotate: src/libslic3r/miniz_extension.cpp (Session 84)
- [x] T393  annotate: src/libslic3r/BlacklistedLibraryCheck.cpp (Session 84)
- [x] T394  annotate: src/libslic3r/SVG.cpp (Session 84)
- [x] T395  annotate: src/libslic3r/Zipper.cpp (Session 84)
- [x] T396  annotate: src/libslic3r/PNGReadWrite.cpp (Session 84)
- [x] T397  annotate: src/libslic3r/NSVGUtils.cpp (Session 84)
- [x] T398  annotate: src/libslic3r/ObjColorUtils.cpp (Session 84)
- [x] T399  annotate: src/libslic3r/MaterialType.cpp (Session 84)
- [x] T400  annotate: src/libslic3r/ParameterUtils.cpp (Session 84)
- [x] T401  annotate: src/libslic3r/ProjectTask.cpp (Session 84)
- [x] T402  annotate: src/libslic3r/FlushVolCalc.cpp (Session 84)
- [x] T403  annotate: src/libslic3r/FlushVolPredictor.cpp (Session 83)
- [x] T404  annotate: src/libslic3r/FilamentGroup.cpp (Session 78)
- [x] T405  annotate: src/libslic3r/FilamentGroupUtils.cpp (Session 83)
- [x] T406  annotate: src/libslic3r/Color.cpp (Session 84)
- [x] T407  annotate: src/libslic3r/CustomGCode.cpp (Session 84)
- [x] T408  annotate: src/libslic3r/TryCatchSignal.cpp (Session 84)
- [x] T409  annotate: src/libslic3r/TryCatchSignalSEH.cpp (Session 84)
- [x] T410  annotate: src/libslic3r/TriangleSelector.cpp (Session 79)
- [x] T411  annotate: src/libslic3r/BuildVolume.cpp (Session 51)
- [x] T412  annotate: src/libslic3r/BuildVolume.hpp (Session 51)
- [x] T413  annotate: src/libslic3r/PrintObject.cpp (Session 75)

---

## Phase 2 — Documentation Gaps (Resume after Bootstrap is complete)

- [x] T2200  Write 01_system_architecture.md (Session 33 and ongoing)
- [x] T2201  Write 02_core_data_structures.md (Session 33 and ongoing)
- [x] T2202  Write 03_algorithmic_complexities.md (Session 33 and ongoing)
- [x] T2203  Write 04_refactoring_hazards.md (Sessions 2–84, ongoing)

- [x] T204  Create `05_external_dependencies.md`. Follow the structure in the
            prompt exactly. Priority order: Clipper/Clipper2 → TBB → Eigen →
            admesh → OpenVDB → remaining 16 libraries. For each: API surface
            used, algorithmic role, porting strategy (REPLICATE / FIND_EQUIVALENT
            / PORT_REQUIRED / GUI_ONLY), and hazards. Commit with prefix `docs:`.

- [x] T210  Add missing data structures to `02_core_data_structures.md`:
            SupportLayer + SupportLayerPtrs, FillAdaptive::Octree,
            FillLightning::Generator (note raw pointer hazard from Fill.cpp),
            UndoRedo::StackImpl (cereal stack + friend class coupling),
            TriangleSelector (lazy raw ptr on ModelVolume, paint-on operations),
            PlaceholderParser runtime state (Spirit X3 AST, expression evaluator).
            Use the same format as existing entries: fields, invariants,
            lifecycle, consumers. Commit with prefix `docs:`.

- [x] T211  Add missing algorithm sections to `03_algorithmic_complexities.md`:
            (a) ArcFitter.cpp — segment-to-arc conversion (G2/G3), including
                the chord-error threshold and the minimum-arc-length guard.
            (b) MultiMaterialSegmentation.cpp — the painting-based filament
                region assignment algorithm (BBS addition; entirely absent
                despite being noted in Session 2 journal).
            (c) Arachne straight skeleton — explain the medial axis computation
                that drives variable-width perimeters; currently named but not
                explained in Section 2.
            Commit with prefix `docs:`.

- [x] T205A  Create `pseudocode_triangle_mesh_slicer_chaining.md` — Phase 2
             line-chaining algorithm (hash map assembly → greedy chain walk →
             T-junction handling → degenerate discard). This is the phase most
             obscured by the C++ implementation. Include Translation Notes
             flagging: implicit endpoint ID encoding, the NO_SEED/SKIP flag
             state machine, and the per-layer mutex scheme.
             Register as ACTIVE in ralph-tasks.md before starting.
             Commit with prefix `docs:`.

- [x] T205B  Create `pseudocode_fill_lightning.md` — the branch extension loop
             (unlit-point coverage → branch growth → merging heuristic). Flag
             the raw Generator pointer lifecycle in Translation Notes.
             Commit with prefix `docs:`.

- [x] T205C  Create `pseudocode_multimaterial_segmentation.md` — only after
             T211(b) is complete, since the algorithm section must exist first.
             Commit with prefix `docs:`.

- [x] T205D  Create `pseudocode_seam_placer.md` — visibility scoring
             (hemisphere raycasting → score accumulation) and B-spline alignment
             loop. Flag the monotonic-Z assumption in Translation Notes.
             Commit with prefix `docs:`.

---

## Phase 3 — Review

- [x] T300  Resolve or escalate all [UNCLEAR] tags in annotated source files.
            This task has a MANDATORY resolution-first policy:
            
            For each [UNCLEAR] tag:
            1. Read the surrounding function — at minimum 50 lines of context.
            2. Check if any other annotated file in the same module answers it.
            3. If the answer is found: update the inline comment to
               `[UNCLEAR → RESOLVED]: <one sentence explanation>` and log it
               in agent_journal.md under a `## T300 Resolutions` section.
            4. Only if steps 1–2 are exhausted without an answer: mark as
               `[UNCLEAR → ESCALATED]: <reason resolution requires external
               knowledge>` and add it to a `## T300 Escalations` table in
               agent_journal.md with columns: File | Line | Reason blocked.

            Acceptance criteria: The T300 completion entry in agent_journal.md
            must show a non-zero resolution count. If every single tag was
            escalated with no resolutions, that is a signal the task was not
            genuinely attempted — do not mark T300 done.
            Commit with prefix `annotate:`.
- [x] T301  Verify all documentation file code links are correct.
            Spot-checking is NOT acceptable. This task requires systematic
            verification of every file:line reference in every .md file
            under /generated_documentation/.

            Procedure:
            1. For each documentation file, extract every link of the form
               `file.cpp#L<n>` or `file.hpp#L<n>`.
            2. For each extracted reference: open the source file and confirm
               the referenced line is within ±20 lines of the described content.
               Drift beyond 20 lines = broken link; update it.
            3. Output results to a new file:
               `/generated_documentation/link_verification_report.md`
               with columns: Doc File | Link | Expected Content | Status (OK / UPDATED / BROKEN)
            4. For any BROKEN link that cannot be resolved (e.g., the function
               was removed): add a note in the doc file replacing the link with
               `[LINK BROKEN — function removed or file restructured]`.

            This file is the human-reviewable proof that T301 was done.
            Commit the report and any updated doc files with prefix `docs:`.

- [x] T302  Final git commit and branch summary

- [/] T303  HUMAN REVIEW GATE — produce the review package and stop.
            This is the final task. The agent does NOT mark the project complete.
            Instead, produce a file `/generated_documentation/REVIEW_PACKAGE.md`
            containing:

            1. **Coverage summary:** Total source files found / annotated /
               explicitly skipped (with skip reason breakdown).
            2. **Documentation file inventory:** Each .md file, its line count,
               its last-updated session, and one sentence on what it covers.
            3. **Pseudocode file inventory:** Each pseudocode file, which source
               file it covers, and the count of Translation Notes.
            4. **Open items list:** Every item that requires human judgment:
               - All [UNCLEAR → ESCALATED] tags (from T300)
               - All BROKEN links that could not be auto-resolved (from T301)
               - Any SKIP_GUI / SKIP_TRIVIAL decisions the human should audit
            5. **Suggested next actions:** What the translation agents should
               read first, second, and third.

            After committing REVIEW_PACKAGE.md, write a final journal entry
            titled `## Session N — Phase 3 Complete — Awaiting Human Review`
            and STOP. Do not mark T303 as [x] DONE. Leave it as [/] ACTIVE.
            The human reviewer marks it done after reviewing the package.

**Rule:** No agent run may mark the entire ralph-tasks.md as complete. The
final state of a successful run is: all tasks [x] DONE except T303 which
remains [/] ACTIVE. If T303 is found marked [x] DONE by the agent, the run
is considered invalid and must be restarted from T300.

## Phase 4 — Coverage Resolution

**Rule:** T4000 must be completed and committed before any T401x–T409x task
is marked ACTIVE. All annotation tasks in this phase are populated by T4000
and must not be started until that task's commit exists on the branch.

---

### Pre-Registration

- [x] T4000  DISCOVERY · Enumerate all unregistered source files and write every
             remaining Phase 4 annotation task entry into ralph-tasks.md before
             any annotation begins.

             Steps:
             1. Run:
                  find src/ -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | sort
             2. Subtract every file path already present in Phase 1 of this file.
             3. From the remainder, apply these classification rules in order:
                  SKIP_GUI       — path contains src/slic3r/GUI/ or src/libvgcode/
                  SKIP_GUI       — path contains src/slic3r/Utils/ AND the file
                                   has no #include of any libslic3r/ core header
                                   (check first 30 lines only)
                  SKIP_TRIVIAL   — file is a precompiled header stub, auto-generated
                                   file, or contains fewer than 15 lines of non-comment
                                   non-whitespace content
                  SKIP_VENDORED  — path contains deps/ or dep_libs/ or src/lib/
                  ANNOTATE       — everything else
             4. For every ANNOTATE file: write a new T4xxx task entry in the
                Phase 4 annotation section below, following the grouping rules
                in T4001.
             5. For every SKIP_* file: append it to the Phase 4 Skip Registry
                section at the bottom of this file with its classification reason.
             6. Verify that Format/, Arachne/utils/, and the GCode/ and Fill/
                gaps are fully covered by the tasks written in step 4.
             7. Commit ralph-tasks.md with message:
                  `docs: T4000 pre-registration — N files classified, M tasks created`

             Do NOT open or annotate any source file during this task.
             Do NOT begin any T401x task until this commit exists.

---

### Format Parsers — src/libslic3r/Format/
(Context note: each parser creates the same output type — indexed_triangle_set
loaded into ModelVolume. Read each parser's output construction together with
its header to understand the data contract.)

- [ ] T4010  annotate+verify: src/libslic3r/Format/STL.cpp
                            + src/libslic3r/Format/STL.hpp
             Context: Binary and ASCII STL loading via admesh. This is the most
             common input format. Cross-reference TriangleMesh.cpp (T114) for
             the repair step that follows loading.
             Note: These files may have been annotated in Session 2 without a
             task record. If annotations already exist and are complete per the
             tag checklist ([INTENT][STATE][MEMORY][COUPLING][HAZARD]), mark
             done after verification. If partial or absent, complete them.
             Commit: `annotate: STL parser (T4010)`

- [ ] T4011  annotate+verify: src/libslic3r/Format/OBJ.cpp
                            + src/libslic3r/Format/OBJ.hpp
             Context: OBJ mesh loading. OBJ supports multiple named objects in
             one file — document how multi-object OBJ maps to ModelVolume count.
             Same verification rule as T4010.
             Commit: `annotate: OBJ parser (T4011)`

- [ ] T4012  annotate+verify: src/libslic3r/Format/AMF.cpp
                            + src/libslic3r/Format/AMF.hpp
             Context: AMF (Additive Manufacturing Format) XML parser via expat.
             AMF supports material assignment inline — document how this maps to
             ModelVolume::type and the config system.
             Same verification rule as T4010.
             Commit: `annotate: AMF parser (T4012)`

- [ ] T4013  annotate+verify: src/libslic3r/Format/3mf.cpp
                            + src/libslic3r/Format/3mf.hpp
             Context: Base 3MF ZIP+XML parser (OPC/3MF spec). Document the
             relationship between this base implementation and bbs_3mf (T4014).
             Commit: `annotate: 3MF base parser (T4013)`

- [ ] T4014  annotate+verify: src/libslic3r/Format/bbs_3mf.cpp
                            + src/libslic3r/Format/bbs_3mf.hpp
             Context: BBL's extended 3MF format, the primary round-trip format
             for OrcaSlicer. This is the largest parser file. Read alongside
             T4013 for the base format. Document every BBL extension field and
             how it maps to Print/Model state. This file almost certainly
             contains the highest hazard density of any parser.
             WARNING: bbs_3mf.cpp is likely >3000 lines. Process in two passes:
             pass 1 (read + annotate), pass 2 (write hazard entries). Do not
             batch with any other file.
             Commit: `annotate: BBS 3MF extended parser (T4014)`

- [ ] T4015  annotate+verify: src/libslic3r/Format/STEP.cpp
                            + src/libslic3r/Format/STEP.hpp
             Context: STEP/CAD import via OpenCASCADE (OCCT). Document which
             OCCT API calls are used and what tessellation parameters control
             the output mesh quality. Cross-reference 05_external_dependencies.md
             section 16 (OCCT).
             Commit: `annotate: STEP/OCCT parser (T4015)`

- [ ] T4016  annotate+verify: src/libslic3r/Format/SL1.cpp
                            + src/libslic3r/Format/SL1.hpp
             Context: SLA archive format I/O for the Prusa SL1 ZIP container.
             These are output-only formats for the SLA pipeline. Document the
             archive structure and which SLA-specific data fields they write.
             Commit: `annotate: SL1/SLA format I/O (T4016)`

- [ ] T4017  annotate: src/libslic3r/Format/DRC.cpp
                     + src/libslic3r/Format/DRC.hpp
                     + src/libslic3r/format.hpp
                     + src/libslic3r/Format/ModelIO.hpp
                     + src/libslic3r/Format/objparser.cpp
                     + src/libslic3r/Format/objparser.hpp
                     + src/libslic3r/Format/svg.cpp
                     + src/libslic3r/Format/svg.hpp
                     + src/libslic3r/Format/ZipperArchiveImport.cpp
                     + src/libslic3r/Format/ZipperArchiveImport.hpp
             Context: Remaining format helpers and sidecar parsers not covered by
             T4010–T4016. This bucket includes objparser, SVG import/export,
             DRC serialization, model I/O helpers, and ZIP archive import glue.
             Commit: `annotate: remaining Format/ files (T4017)`

- [ ] T4018  DOCS UPDATE · After T4010–T4017 are all complete: add a
             "Section 2b — Model Loading in Detail" entry to
             03_algorithmic_complexities.md describing the parsing pipeline:
             which format parsers produce indexed_triangle_set directly vs
             which require an intermediate conversion step, and what mesh
             quality guarantees each parser provides before admesh repair.
             Commit: `docs: parser pipeline section (T4018)`

---

### Arachne Gap — src/libslic3r/Arachne/ (missing files only)
(Context note: T164–T184 annotated the main Arachne engine files. This group
covers files that were missed. Read WallToolPaths.hpp (T165) before starting
any task here — it is the public interface that contextualizes all of these.)

- [ ] T4020  annotate: src/libslic3r/Arachne/SkeletalTrapezoidation.hpp
                     + src/libslic3r/Arachne/utils/ExtrusionJunction.hpp
                     + src/libslic3r/Arachne/utils/ExtrusionLine.cpp
                     + src/libslic3r/Arachne/utils/ExtrusionLine.hpp
             Context: SkeletalTrapezoidation.hpp is the missing header for the
             already-annotated .cpp (T166). ExtrusionJunction and ExtrusionLine
             are the output types of the Arachne engine — they define what a
             variable-width wall path looks like before conversion to
             ExtrusionEntity. Group these because the junction/line types are
             referenced throughout the .hpp.
             Commit: `annotate: Arachne output types + STrap header (T4020)`

- [ ] T4021  annotate: src/libslic3r/Arachne/utils/HalfEdgeGraph.hpp
                     + src/libslic3r/Arachne/utils/HalfEdge.hpp
                     + src/libslic3r/Arachne/utils/HalfEdgeNode.hpp
             Context: Half-edge data structure used by SkeletalTrapezoidation
             to represent the straight skeleton graph. These files form a single
             conceptual unit. Read SkeletalTrapezoidationGraph.hpp (T168) first
             — it inherits or wraps these.
             Commit: `annotate: Arachne half-edge graph types (T4021)`

- [ ] T4022  annotate: src/libslic3r/Arachne/utils/PolylineStitcher.cpp
                     + src/libslic3r/Arachne/utils/PolylineStitcher.hpp
                     + src/libslic3r/Arachne/utils/SparseGrid.hpp
                     + src/libslic3r/Arachne/utils/SparseLineGrid.hpp
                     + src/libslic3r/Arachne/utils/SparsePointGrid.hpp
                     + src/libslic3r/Arachne/utils/SquareGrid.cpp
                     + src/libslic3r/Arachne/utils/SquareGrid.hpp
             Context: Spatial lookup and stitching utilities used during wall
             path ordering and stitching. SparseLineGrid is a bucketed 2D line
             index; the surrounding grid helpers and stitcher complete that
             feature slice.
             Commit: `annotate: Arachne spatial utils (T4022)`

- [ ] T4023  annotate: src/libslic3r/Arachne/utils/linearAlg2D.hpp
                     + src/libslic3r/Arachne/utils/PolygonsPointIndex.hpp
                     + src/libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp
             Context: Remaining Arachne utility files not covered by T164–T184
             and T4020–T4022. These headers provide the math and polygon index
             helpers shared across the straight-skeleton implementation.
             Commit: `annotate: remaining Arachne/ files (T4023)`

- [ ] T4024  DOCS UPDATE · After T4020–T4023 are complete: return to
             03_algorithmic_complexities.md Section 2 (Perimeter Generation)
             and fully document the Arachne straight skeleton algorithm.
             Currently this section names the algorithm but does not explain
             the medial axis computation, the beading strategy dispatch, or
             how variable widths are assigned. Add these. Cross-reference the
             newly annotated utils files.
             Then create pseudocode_arachne_straight_skeleton.md covering:
             the SkeletalTrapezoidation main loop, the beading strategy
             selection, and the ExtrusionLine output assembly.
             Commit: `docs: Arachne algorithm section + pseudocode (T4024)`

---

### GCode Gap — src/libslic3r/GCode/ (missing files only)
(Context note: T185–T218 covered the main GCode pipeline. These are the
remaining files, most likely BBL additions. Read GCode.cpp (T185) and
GCodeWriter.cpp (T186) annotations before starting any task here.)

- [ ] T4030  annotate: src/libslic3r/GCode/ExtrusionProcessor.hpp
             Context: Mentioned in 03_algorithmic_complexities.md Section 9
             (curled extrusion estimation) but absent from Phase 1 registry.
             Document the curled-line detection API surface and how it feeds into
             Layer::curled_lines. This is a BBS addition with no PrusaSlicer
             equivalent — translation agents need full documentation.
             Commit: `annotate: ExtrusionProcessor (T4030)`

- [ ] T4031  annotate: src/libslic3r/GCode/ThumbnailData.cpp
                     + src/libslic3r/GCode/ThumbnailData.hpp
                     + src/libslic3r/GCode/Thumbnails.cpp
                     + src/libslic3r/GCode/Thumbnails.hpp
             Context: Thumbnail image embedding in G-code files (PNG preview
             in gcode header). Shared image format between .gcode and .bgcode
             outputs. Group these four as they form one feature unit.
             Commit: `annotate: GCode thumbnail embedding (T4031)`

- [ ] T4032  annotate: src/libslic3r/GCode/AvoidCrossingPerimeters.hpp
                     + src/libslic3r/GCode/CoolingBuffer.hpp
                     + src/libslic3r/GCode/PchipInterpolatorHelper.cpp
                     + src/libslic3r/GCode/PchipInterpolatorHelper.hpp
                     + src/libslic3r/GCode/SeamPlacer.hpp
                     + src/libslic3r/GCode/TimelapsePosPicker.cpp
                     + src/libslic3r/GCode/TimelapsePosPicker.hpp
                     + src/libslic3r/GCode/ToolOrdering.hpp
                     + src/libslic3r/GCode/WipeTower.cpp
                     + src/libslic3r/GCode/WipeTower.hpp
                     + src/libslic3r/GCode.hpp
                     + src/libslic3r/GCodeSender.cpp
                     + src/libslic3r/GCodeSender.hpp
                     + src/libslic3r/GCodeWriter.hpp
             Context: Remaining src/libslic3r/GCode/ files not covered by
             T185–T218 and T4030–T4031. This bucket covers header-only gaps,
             interpolation helpers, timelapse placement, legacy wipe tower code,
             and the standalone sender/writer frontends.
             Commit: `annotate: remaining GCode/ files (T4032)`

---

### Fill Gap — src/libslic3r/Fill/ (missing files only)
(Context note: T137–T163 covered the main infill patterns. Read FillBase.hpp
(T139) before starting any task here — it defines the interface every pattern
implements.)

- [ ] T4040  annotate: src/libslic3r/Fill/FillTpmsD.cpp
                     + src/libslic3r/Fill/FillTpmsD.hpp
                     + src/libslic3r/Fill/FillTpmsFK.cpp
                     + src/libslic3r/Fill/FillTpmsFK.hpp
             Context: TPMS (Triply Periodic Minimal Surface) infill patterns —
             D-surface and Fourier-Kitaev approximation. Group all four because
             they share the same mathematical foundation and their implementation
             is nearly parallel. Document the implicit surface evaluation, the
             adaptive sampling mentioned in T300 escalation (FillTpmsD.cpp:176
             16-seed-per-period choice), and how the 2D cross-section is
             extracted per layer.
             Commit: `annotate: TPMS infill patterns (T4040)`

- [ ] T4041  annotate: src/libslic3r/Fill/FillAdaptive.hpp
                     + src/libslic3r/Fill/FillConcentricInternal.cpp
                     + src/libslic3r/Fill/FillConcentricInternal.hpp
                     + src/libslic3r/Fill/Fill.hpp
                     + src/libslic3r/Fill/FillRectilinear.hpp
                     + src/libslic3r/Fill/Lightning/DistanceField.cpp
                     + src/libslic3r/Fill/Lightning/DistanceField.hpp
             Context: Remaining src/libslic3r/Fill/ files not covered by
             T137–T163 and T4040. This bucket captures shared infill headers,
             concentric helper internals, and the lightning distance field.
             Commit: `annotate: remaining Fill/ files (T4041)`

- [ ] T4042  DOCS UPDATE · After T4040–T4041 are complete: add TPMS algorithm
             entries to 03_algorithmic_complexities.md Section 3 (Infill
             Algorithms). The current section mentions FillTpmsD and FillTpmsFK
             by name only. Add: the implicit surface equations for both surfaces,
             the cross-section extraction method, and the adaptive sampling
             algorithm. Then add a pseudocode_tpms_infill.md if the cross-section
             extraction is not obvious from the algorithm description alone.
             Commit: `docs: TPMS infill algorithm section (T4042)`

---

### slic3r/Utils/ Classification and Selective Annotation

- [ ] T4050  CLASSIFY · Classify all files under src/slic3r/Utils/.
             For each file, check the first 30 lines for any #include of a
             libslic3r/ core header (Model.hpp, Print.hpp, PrintConfig.hpp,
             GCode*.hpp, Layer.hpp, etc.).
             - Files with no core includes = SKIP_GUI (network/printer UI only)
             - Files that include core libslic3r types = ANNOTATE
             Write the results into the Phase 4 Skip Registry at the bottom of
             this file. Write ANNOTATE-classified files as new tasks T4051+.
             Do NOT annotate any file during this task.
             Commit: `docs: slic3r/Utils/ classification (T4050)`

- [ ] T4051  annotate: ANNOTATE-classified src/slic3r/Utils/ files.
             T4050 will determine which files these are and may split this into
             multiple tasks if more than 4 files qualify.
             Group by what libslic3r type they touch: files that both import
             PrintConfig should be in the same task, files that only touch Model
             in another, etc.
             Commit: `annotate: slic3r/Utils/ core-touching files (T4051)`

---

### Remaining Unregistered Files — All Other Directories

- [ ] T4060a  annotate: src/dev-utils/BaseException.cpp
              + src/dev-utils/BaseException.h
              + src/dev-utils/OrcaSlicer_profile_validator.cpp
              + src/dev-utils/StackWalker.cpp
              + src/dev-utils/StackWalker.h
             Commit: `annotate: src/dev-utils files (T4060a)`

- [ ] T4060b  annotate: src/libslic3r/AnyPtr.hpp
              + src/libslic3r/AppConfig.hpp
              + src/libslic3r/AStar.hpp
              + src/libslic3r/BlacklistedLibraryCheck.hpp
              + src/libslic3r/BridgeDetector.cpp
             Commit: `annotate: src/libslic3r files (T4060b)`

- [ ] T4060c  annotate: src/libslic3r/BridgeDetector.hpp
              + src/libslic3r/BrimEarsPoint.hpp
              + src/libslic3r/calib.cpp
              + src/libslic3r/calib.hpp
              + src/libslic3r/Channel.hpp
             Commit: `annotate: src/libslic3r files (T4060c)`

- [ ] T4060d  annotate: src/libslic3r/Clipper2Utils.hpp
              + src/libslic3r/Clipper2ZUtils.hpp
              + src/libslic3r/clipper.cpp
              + src/libslic3r/clipper.hpp
              + src/libslic3r/ClipperZUtils.hpp
             Commit: `annotate: src/libslic3r files (T4060d)`

- [ ] T4060e  annotate: src/libslic3r/clonable_ptr.hpp
              + src/libslic3r/Color.hpp
              + src/libslic3r/CommonDefs.hpp
              + src/libslic3r/CustomGCode.hpp
              + src/libslic3r/CutSurface.cpp
             Commit: `annotate: src/libslic3r files (T4060e)`

- [ ] T4060f  annotate: src/libslic3r/CutSurface.hpp
              + src/libslic3r/CutUtils.hpp
              + src/libslic3r/EdgeGrid.hpp
              + src/libslic3r/Emboss.hpp
              + src/libslic3r/EmbossShape.hpp
             Commit: `annotate: src/libslic3r files (T4060f)`

- [ ] T4060g  annotate: src/libslic3r/enum_bitmask.hpp
              + src/libslic3r/Exception.hpp
              + src/libslic3r/ExPolygonCollection.hpp
              + src/libslic3r/ExPolygonSerialize.hpp
              + src/libslic3r/ExPolygonsIndex.hpp
             Commit: `annotate: src/libslic3r files (T4060g)`

- [ ] T4060h  annotate: src/libslic3r/Extruder.cpp
              + src/libslic3r/Extruder.hpp
              + src/libslic3r/ExtrusionSimulator.hpp
              + src/libslic3r/FaceDetector.hpp
              + src/libslic3r/FilamentGroup.hpp
             Commit: `annotate: src/libslic3r files (T4060h)`

- [ ] T4060i  annotate: src/libslic3r/FilamentGroupUtils.hpp
              + src/libslic3r/FileParserError.hpp
              + src/libslic3r/FlushVolCalc.hpp
              + src/libslic3r/FlushVolPredictor.hpp
              + src/libslic3r/Geometry.hpp
             Commit: `annotate: src/libslic3r files (T4060i)`

- [ ] T4060j  annotate: src/libslic3r/I18N.hpp
              + src/libslic3r/Int128.hpp
              + src/libslic3r/IntersectionPoints.hpp
              + src/libslic3r/JumpPointSearch.hpp
              + src/libslic3r/KDTreeIndirect.hpp
             Commit: `annotate: src/libslic3r files (T4060j)`

- [ ] T4060k  annotate: src/libslic3r/Layer.cpp
              + src/libslic3r/Layer.hpp
              + src/libslic3r/libslic3r.h
              + src/libslic3r/Line.cpp
              + src/libslic3r/Line.hpp
             Commit: `annotate: src/libslic3r files (T4060k)`

- [ ] T4060l  annotate: src/libslic3r/LocalesUtils.hpp
              + src/libslic3r/MacUtils.hpp
              + src/libslic3r/MarchingSquares.hpp
              + src/libslic3r/MaterialType.hpp
              + src/libslic3r/Measure.hpp
             Commit: `annotate: src/libslic3r files (T4060l)`

- [ ] T4060m  annotate: src/libslic3r/MeasureUtils.hpp
              + src/libslic3r/MeshSplitImpl.hpp
              + src/libslic3r/MinAreaBoundingBox.hpp
              + src/libslic3r/MinimumSpanningTree.hpp
              + src/libslic3r/miniz_extension.hpp
             Commit: `annotate: src/libslic3r files (T4060m)`

- [ ] T4060n  annotate: src/libslic3r/ModelArrange.hpp
              + src/libslic3r/MTUtils.hpp
              + src/libslic3r/MultiMaterialSegmentation.hpp
              + src/libslic3r/MultiPoint.cpp
              + src/libslic3r/MultiPoint.hpp
             Commit: `annotate: src/libslic3r files (T4060n)`

- [ ] T4060o  annotate: src/libslic3r/MutablePolygon.hpp
              + src/libslic3r/MutablePriorityQueue.hpp
              + src/libslic3r/NormalUtils.hpp
              + src/libslic3r/NSVGUtils.hpp
              + src/libslic3r/ObjColorUtils.hpp
             Commit: `annotate: src/libslic3r files (T4060o)`

- [ ] T4060p  annotate: src/libslic3r/ObjectID.hpp
              + src/libslic3r/ParameterUtils.hpp
              + src/libslic3r/PlaceholderParser.hpp
              + src/libslic3r/Platform.hpp
              + src/libslic3r/PNGReadWrite.hpp
             Commit: `annotate: src/libslic3r files (T4060p)`

- [ ] T4060q  annotate: src/libslic3r/PolygonTrimmer.hpp
              + src/libslic3r/PresetBundle.hpp
              + src/libslic3r/PrincipalComponents2D.hpp
              + src/libslic3r/PrintConfigConstants.hpp
              + src/libslic3r/ProjectTask.hpp
             Commit: `annotate: src/libslic3r files (T4060q)`

- [ ] T4060r  annotate: src/libslic3r/QuadricEdgeCollapse.hpp
              + src/libslic3r/Semver.hpp
              + src/libslic3r/ShortEdgeCollapse.hpp
              + src/libslic3r/SlicesToTriangleMesh.hpp
              + src/libslic3r/SlicingAdaptive.hpp
             Commit: `annotate: src/libslic3r files (T4060r)`

- [ ] T4060s  annotate: src/libslic3r/StreamUtils.hpp
              + src/libslic3r/SurfaceMesh.hpp
              + src/libslic3r/SVG.hpp
              + src/libslic3r/Technologies.hpp
              + src/libslic3r/Tesselate.cpp
             Commit: `annotate: src/libslic3r files (T4060s)`

- [ ] T4060t  annotate: src/libslic3r/Tesselate.hpp
              + src/libslic3r/TextConfiguration.hpp
              + src/libslic3r/Thread.hpp
              + src/libslic3r/Time.hpp
              + src/libslic3r/Timer.hpp
             Commit: `annotate: src/libslic3r files (T4060t)`

- [ ] T4060u  annotate: src/libslic3r/TriangleMeshDeal.hpp
              + src/libslic3r/TriangleSelector.hpp
              + src/libslic3r/TriangleSetSampling.hpp
              + src/libslic3r/TriangulateWall.hpp
              + src/libslic3r/Triangulation.hpp
             Commit: `annotate: src/libslic3r files (T4060u)`

- [ ] T4060v  annotate: src/libslic3r/TryCatchSignal.hpp
              + src/libslic3r/TryCatchSignalSEH.hpp
              + src/libslic3r/Utils.hpp
              + src/libslic3r/VariableWidth.hpp
              + src/libslic3r/Zipper.hpp
             Commit: `annotate: src/libslic3r files (T4060v)`

- [ ] T4060w  annotate: src/libslic3r/Shape/TextShape.cpp
              + src/libslic3r/Shape/TextShape.hpp
             Commit: `annotate: src/libslic3r/Shape files (T4060w)`

- [ ] T4060x  annotate: src/OrcaSlicer_app_msvc.cpp
             Commit: `annotate: src files (T4060x)`

- [ ] T4060y  annotate: src/slic3r/Config/Snapshot.cpp
              + src/slic3r/Config/Snapshot.hpp
              + src/slic3r/Config/Version.cpp
              + src/slic3r/Config/Version.hpp
             Commit: `annotate: src/slic3r/Config files (T4060y)`

---

### Phase 4 Documentation Wrap-Up

- [ ] T4070  LINK VERIFICATION · Re-run T301 for all files newly annotated in
             Phase 4. Append new rows to
             generated_documentation/link_verification_report.md.
             Do not re-check Phase 1/2/3 links already verified.
             Commit: `docs: Phase 4 link verification (T4070)`

- [ ] T4071  HAZARD BLOCKERS UPDATE · Scan all new hazard entries created during
             Phase 4 annotation for P1/High severity. Add any found to the
             Critical Blockers summary at the top of 04_refactoring_hazards.md.
             Commit: `docs: Phase 4 critical blockers update (T4071)`

- [ ] T4072  REVIEW PACKAGE · Regenerate REVIEW_PACKAGE.md. The coverage gap
             line must read "0 unresolved files" for this phase to be considered
             complete. Update all inventory tables with Phase 4 additions.
             Update the CURRENT STATUS block in agent_journal.md.
             Leave T303 as [/] ACTIVE — do not mark it done.
             Commit: `docs: regenerate REVIEW_PACKAGE.md (T4072)`

---

## Phase 4 Skip Registry
(Populated by T4000 and T4050 — do not edit manually)

| File | Classification | Reason |
|------|---------------|--------|
| `src/dev-utils/encoding-check.cpp` | SKIP_TRIVIAL | PCH or tiny validation stub with no core slicer domain logic. |
| `src/libslic3r/pchheader.hpp` | SKIP_TRIVIAL | PCH or tiny validation stub with no core slicer domain logic. |
| `src/libvgcode/glad/include/glad/gles2.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/glad/include/glad/gl.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/glad/include/KHR/khrplatform.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/ColorPrint.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/ColorRange.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/GCodeInputData.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/PathVertex.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/Types.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/include/Viewer.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Bitset.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Bitset.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/CogMarker.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/CogMarker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ColorPrint.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ColorRange.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ExtrusionRoles.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ExtrusionRoles.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/GCodeInputData.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Layers.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Layers.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/OpenGLUtils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/OpenGLUtils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/OptionTemplate.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/OptionTemplate.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/PathVertex.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Range.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Range.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/SegmentTemplate.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/SegmentTemplate.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Settings.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Settings.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ShadersES.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Shaders.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ToolMarker.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ToolMarker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Types.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Utils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Utils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/Viewer.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ViewerImpl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ViewerImpl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ViewRange.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/libvgcode/src/ViewRange.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/2DBed.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/2DBed.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/3DBed.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/3DBed.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/3DScene.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/3DScene.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AboutDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AboutDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AmsMappingPopup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AmsMappingPopup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AMSMaterialsSetting.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AMSMaterialsSetting.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AMSSetting.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AMSSetting.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AmsWidgets.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AmsWidgets.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Auxiliary.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AuxiliaryDataViewModel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AuxiliaryDataViewModel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AuxiliaryDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/AuxiliaryDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Auxiliary.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BackgroundSlicingProcess.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BackgroundSlicingProcess.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BambuPlayer/BambuPlayer.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BaseTransparentDPIFrame.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BaseTransparentDPIFrame.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarBind.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarBind.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarPrint.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarPrint.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarSend.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLStatusBarSend.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLTopbar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BBLTopbar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BedShapeDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BedShapeDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BindDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BindDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BitmapCache.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BitmapCache.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BitmapComboBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BitmapComboBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BonjourDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/BonjourDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/calib_dlg.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/calib_dlg.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Calibration.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Calibration.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardCaliPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardCaliPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizard.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizard.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardPresetPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardPresetPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardSavePage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardSavePage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardStartPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CalibrationWizardStartPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CaliHistoryDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CaliHistoryDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Camera.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Camera.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CameraPopup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CameraPopup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CameraUtils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CameraUtils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CapsuleButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CapsuleButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CloneDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CloneDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigExceptions.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigManipulation.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigManipulation.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigWizard.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigWizard.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConfigWizard_private.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConnectPrinter.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ConnectPrinter.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CreatePresetsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/CreatePresetsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DailyTips.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DailyTips.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/dark_mode.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/dark_mode/dark_mode.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/dark_mode.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/dark_mode/IatHook.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/dark_mode/UAHMenuBar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DesktopIntegrationDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DesktopIntegrationDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevBed.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevBed.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevConfig.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevConfig.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevConfigUtil.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevConfigUtil.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevCtrl.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevDefs.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevExtensionTool.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevExtensionTool.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevExtruderSystem.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevExtruderSystemCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevExtruderSystem.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFan.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFan.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaAmsSetting.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaAmsSettingCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaAmsSetting.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaBlackList.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaBlackList.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaSystem.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaSystemCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFilaSystem.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFirmware.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevFirmware.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevHMS.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevHMS.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevInfo.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevInfo.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevLamp.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevLampCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevLamp.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevManager.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevMapping.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevMapping.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevNozzleSystem.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevNozzleSystem.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevPrintOptions.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevPrintOptions.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevPrintTaskInfo.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevPrintTaskInfo.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevStorage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevStorage.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevUtil.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceCore/DevUtil.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceErrorDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceErrorDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceTab/uiAmsHumidityPopup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceTab/uiDeviceUpdateVersion.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DeviceTab/uiDeviceUpdateVersion.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Downloader.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DownloaderFileGet.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DownloaderFileGet.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Downloader.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DownloadProgressDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DownloadProgressDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DragCanvas.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DragCanvas.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DragDropPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/DragDropPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/EditGCodeDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/EditGCodeDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/EncodedFilament.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/EncodedFilament.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Event.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ExtraRenderers.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ExtraRenderers.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ExtrusionCalibration.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ExtrusionCalibration.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Field.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Field.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentBitmapUtils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentBitmapUtils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentGroupPopup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentGroupPopup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentMapDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentMapDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentMapPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentMapPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentPickerDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FilamentPickerDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FileArchiveDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/FileArchiveDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/format.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/fts_fuzzy_match.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GCodeViewer.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GCodeViewer.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GizmoObjectManipulation.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoAdvancedCut.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoAdvancedCut.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoAssembly.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoAssembly.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoBase.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoBase.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoBrimEars.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoBrimEars.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoCut.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoCut.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoEmboss.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoEmboss.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFaceDetector.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFaceDetector.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFlatten.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFlatten.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFuzzySkin.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoFuzzySkin.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoHollow.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMeasure.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMeasure.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMove.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoMove.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoRotate.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoRotate.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoScale.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoScale.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSeam.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSeam.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmos.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSimplify.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSimplify.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmosManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSVG.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoSVG.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoText.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Gizmos/GLGizmoText.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLCanvas3D.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLCanvas3D.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLModel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLModel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLSelectionRectangle.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLSelectionRectangle.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLShader.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLShader.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLShadersManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLShadersManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLTexture.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLTexture.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLToolbar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GLToolbar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_App.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_App.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_AuxiliaryList.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_AuxiliaryList.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GuiColor.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GuiColor.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Colors.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Colors.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Factories.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Factories.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Geometry.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Geometry.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Init.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Init.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectLayers.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectLayers.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectList.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectList.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectSettings.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectSettings.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectTable.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectTable.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectTableSettings.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_ObjectTableSettings.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Preview.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Preview.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Utils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/GUI_Utils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HintNotification.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HintNotification.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HMS.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HMS.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HMSPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HMSPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HttpServer.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/HttpServer.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/I18N.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/I18N.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IconManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IconManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImageDPIFrame.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImageDPIFrame.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImageGrid.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImageGrid.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImGuiWrapper.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ImGuiWrapper.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IMSlider.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IMSlider.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IMToolbar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/IMToolbar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/InstanceCheck.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/InstanceCheck.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/InstanceCheckMac.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/ArrangeJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/ArrangeJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/BindJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/BindJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/BoostThreadWorker.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/BoostThreadWorker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/BusyCursorJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/CreateFontNameImageJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/CreateFontNameImageJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/CreateFontStyleImagesJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/CreateFontStyleImagesJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/EmbossJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/EmbossJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/FillBedJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/FillBedJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/Job.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/NotificationProgressIndicator.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/NotificationProgressIndicator.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/OAuthJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/OAuthJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/OrientJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/OrientJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/PlaterWorker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/PrintJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/PrintJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/ProgressIndicator.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/RotoptimizeJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/RotoptimizeJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/SendJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/SendJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/SLAImportDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/SLAImportJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/SLAImportJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/ThreadSafeQueue.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/UpgradeNetworkJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/UpgradeNetworkJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Jobs/Worker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/KBShortcutsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/KBShortcutsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/LibVGCode/LibVGCodeWrapper.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/LibVGCode/LibVGCodeWrapper.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MainFrame.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MainFrame.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MarkdownTip.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MarkdownTip.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MediaFilePanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MediaFilePanel.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MediaPlayCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MediaPlayCtrl.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MeshUtils.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MeshUtils.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ModelMall.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ModelMall.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MonitorBasePanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MonitorBasePanel.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Monitor.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Monitor.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MonitorPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MonitorPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Mouse3DController.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Mouse3DController.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MsgDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MsgDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachine.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachine.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachineManagerPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachineManagerPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachinePage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiMachinePage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiPrintJob.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiPrintJob.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiSendMachineModel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiSendMachineModel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiTaskManagerPage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiTaskManagerPage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiTaskModel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/MultiTaskModel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NetworkPluginDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NetworkPluginDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NetworkTestDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NetworkTestDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Notebook.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Notebook.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NotificationManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/NotificationManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OAuthDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OAuthDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ObjColorDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ObjColorDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ObjectDataViewModel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ObjectDataViewModel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OG_CustomCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OG_CustomCtrl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OpenGLManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OpenGLManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OptionsGroup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/OptionsGroup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ParamsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ParamsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ParamsPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ParamsPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PartPlate.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PartPlate.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PartSkipCommon.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PartSkipDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PartSkipDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PhysicalPrinterDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PhysicalPrinterDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Plater.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Plater.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PlateSettingsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PlateSettingsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Preferences.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Preferences.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrePrintChecker.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrePrintChecker.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PresetComboBoxes.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PresetComboBoxes.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PresetHints.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PresetHints.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Printer/BambuTunnel.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrinterCloudAuthDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrinterCloudAuthDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Printer/gstbambusrc.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Printer/PrinterFileSystem.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Printer/PrinterFileSystem.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrinterWebView.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrinterWebView.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrintHostDialogs.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrintHostDialogs.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrintOptionsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrintOptionsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrivacyUpdateDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PrivacyUpdateDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ProgressStatusBar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ProgressStatusBar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Project.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ProjectDirtyStateManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ProjectDirtyStateManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Project.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PublishDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/PublishDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RammingChart.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RammingChart.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RecenterDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RecenterDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ReleaseNote.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ReleaseNote.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RemovableDriveManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RemovableDriveManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/RemovableDriveManagerMM.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SafetyOptionsDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SafetyOptionsDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SavePresetDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SavePresetDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SceneRaycaster.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SceneRaycaster.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Search.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Search.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Selection.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Selection.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SelectMachine.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SelectMachine.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SelectMachinePop.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SelectMachinePop.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendMultiMachinePage.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendMultiMachinePage.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendSystemInfoDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendSystemInfoDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendToPrinter.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SendToPrinter.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SingleChoiceDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SingleChoiceDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SkipPartCanvas.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SkipPartCanvas.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SliceInfoPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SliceInfoPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SlicingProgressNotification.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SlicingProgressNotification.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/StatusPanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/StatusPanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/StepMeshDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/StepMeshDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SurfaceDrag.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SurfaceDrag.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SyncAmsInfoDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SyncAmsInfoDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SysInfoDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/SysInfoDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Tabbook.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Tabbook.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TabButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TabButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Tab.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Tab.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TaskManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TaskManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TextLines.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TextLines.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ThermalPreconditioningDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/ThermalPreconditioningDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TickCode.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/TickCode.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UnsavedChangesDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UnsavedChangesDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UpdateDialogs.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UpdateDialogs.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UpgradePanel.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UpgradePanel.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UserManager.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UserManager.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UserNotification.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/UserNotification.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebDownPluginDlg.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebDownPluginDlg.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebGuideDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebGuideDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebUpdatePlugin.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebUpdatePlugin.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebUserLoginDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebUserLoginDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebViewDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WebViewDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AMSControl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AMSControl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AMSItem.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AMSItem.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AnimaController.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AnimaController.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AxisCtrlButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/AxisCtrlButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Button.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Button.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/CheckBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/CheckBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ComboBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ComboBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/DialogButtons.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/DialogButtons.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/DropDown.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/DropDown.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ErrorMsgStaticText.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ErrorMsgStaticText.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/FanControl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/FanControl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/FilamentLoad.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/FilamentLoad.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/HyperLink.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/HyperLink.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ImageSwitchButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ImageSwitchButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Label.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/LabeledStaticBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/LabeledStaticBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Label.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/PopupWindow.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/PopupWindow.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ProgressBar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ProgressBar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ProgressDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ProgressDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RadioBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RadioBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RadioGroup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RadioGroup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RoundedRectangle.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/RoundedRectangle.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Scrollbar.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/Scrollbar.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ScrolledWindow.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/ScrolledWindow.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideMenuPopup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideMenuPopup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideTools.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SideTools.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SpinInput.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SpinInput.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StateColor.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StateColor.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StateHandler.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StateHandler.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticBox.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticBox.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticGroup.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticGroup.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticLine.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StaticLine.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StepCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/StepCtrl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SwitchButton.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/SwitchButton.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TabCtrl.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TabCtrl.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TempInput.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TempInput.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TextCtrl.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TextInput.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/TextInput.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/WebView.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/Widgets/WebView.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WipeTowerDialog.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/WipeTowerDialog.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/wxExtensions.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/wxExtensions.hpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/wxinit.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/wxMediaCtrl2.cpp` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/GUI/wxMediaCtrl2.h` | SKIP_GUI | GUI/libvgcode path excluded from the core slicer annotation pass. |
| `src/slic3r/pchheader.cpp` | SKIP_TRIVIAL | PCH or tiny validation stub with no core slicer domain logic. |
| `src/slic3r/pchheader.hpp` | SKIP_TRIVIAL | PCH or tiny validation stub with no core slicer domain logic. |
