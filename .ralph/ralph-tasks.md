# Ralph Task Registry — OrcaSlicer Analysis Agent
Last updated: 2026-03-09T00:00:00Z

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

- [ ] TB005  BOOTSTRAP · Resolve or explicitly escalate the four open questions
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

- [ ] T210  Add missing data structures to `02_core_data_structures.md`:
            SupportLayer + SupportLayerPtrs, FillAdaptive::Octree,
            FillLightning::Generator (note raw pointer hazard from Fill.cpp),
            UndoRedo::StackImpl (cereal stack + friend class coupling),
            TriangleSelector (lazy raw ptr on ModelVolume, paint-on operations),
            PlaceholderParser runtime state (Spirit X3 AST, expression evaluator).
            Use the same format as existing entries: fields, invariants,
            lifecycle, consumers. Commit with prefix `docs:`.

- [ ] T211  Add missing algorithm sections to `03_algorithmic_complexities.md`:
            (a) ArcFitter.cpp — segment-to-arc conversion (G2/G3), including
                the chord-error threshold and the minimum-arc-length guard.
            (b) MultiMaterialSegmentation.cpp — the painting-based filament
                region assignment algorithm (BBS addition; entirely absent
                despite being noted in Session 2 journal).
            (c) Arachne straight skeleton — explain the medial axis computation
                that drives variable-width perimeters; currently named but not
                explained in Section 2.
            Commit with prefix `docs:`.

- [ ] T205A  Create `pseudocode_triangle_mesh_slicer_chaining.md` — Phase 2
             line-chaining algorithm (hash map assembly → greedy chain walk →
             T-junction handling → degenerate discard). This is the phase most
             obscured by the C++ implementation. Include Translation Notes
             flagging: implicit endpoint ID encoding, the NO_SEED/SKIP flag
             state machine, and the per-layer mutex scheme.
             Register as ACTIVE in ralph-tasks.md before starting.
             Commit with prefix `docs:`.

- [ ] T205B  Create `pseudocode_fill_lightning.md` — the branch extension loop
             (unlit-point coverage → branch growth → merging heuristic). Flag
             the raw Generator pointer lifecycle in Translation Notes.
             Commit with prefix `docs:`.

- [ ] T205C  Create `pseudocode_multimaterial_segmentation.md` — only after
             T211(b) is complete, since the algorithm section must exist first.
             Commit with prefix `docs:`.

- [ ] T205D  Create `pseudocode_seam_placer.md` — visibility scoring
             (hemisphere raycasting → score accumulation) and B-spline alignment
             loop. Flag the monotonic-Z assumption in Translation Notes.
             Commit with prefix `docs:`.

---

## Phase 3 — Review

- [ ] T300  Cross-check all [UNCLEAR] tags are resolved or escalated
- [ ] T301  Verify all documentation files have correct code links
- [ ] T302  Final git commit and branch summary
