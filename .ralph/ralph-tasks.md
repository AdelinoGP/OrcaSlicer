# Ralph Task Registry — OrcaSlicer Analysis Agent
Last updated: 2026-03-11T04:42:00Z

## Legend
- [ ] PENDING   — not started
- [/] ACTIVE    — in progress (only ONE task should be ACTIVE at a time)
- [x] DONE      — complete and committed
- [!] BLOCKED   — cannot proceed without resolution (add reason inline)

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


## Phase 3 — Review


## Phase 4 — Coverage Resolution

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

- [x] T4010  annotate+verify: src/libslic3r/Format/STL.cpp
                            + src/libslic3r/Format/STL.hpp
             Context: Binary and ASCII STL loading via admesh. This is the most
             common input format. Cross-reference TriangleMesh.cpp (T114) for
             the repair step that follows loading.
             Note: These files may have been annotated in Session 2 without a
             task record. If annotations already exist and are complete per the
             tag checklist ([INTENT][STATE][MEMORY][COUPLING][HAZARD]), mark
             done after verification. If partial or absent, complete them.
             Commit: `annotate: STL parser (T4010)`

- [x] T4011  annotate+verify: src/libslic3r/Format/OBJ.cpp
                            + src/libslic3r/Format/OBJ.hpp
             Context: OBJ mesh loading. OBJ supports multiple named objects in
             one file — document how multi-object OBJ maps to ModelVolume count.
             Same verification rule as T4010.
             Commit: `annotate: OBJ parser (T4011)`

- [x] T4012  annotate+verify: src/libslic3r/Format/AMF.cpp
                            + src/libslic3r/Format/AMF.hpp
             Context: AMF (Additive Manufacturing Format) XML parser via expat.
             AMF supports material assignment inline — document how this maps to
             ModelVolume::type and the config system.
             Same verification rule as T4010.
             Commit: `annotate: AMF parser (T4012)`

- [x] T4013  annotate+verify: src/libslic3r/Format/3mf.cpp
                            + src/libslic3r/Format/3mf.hpp
             Context: Base 3MF ZIP+XML parser (OPC/3MF spec). Document the
             relationship between this base implementation and bbs_3mf (T4014).
             Commit: `annotate: 3MF base parser (T4013)`

- [x] T4014  annotate+verify: src/libslic3r/Format/bbs_3mf.cpp
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

- [x] T4015  annotate+verify: src/libslic3r/Format/STEP.cpp
                            + src/libslic3r/Format/STEP.hpp
             Context: STEP/CAD import via OpenCASCADE (OCCT). Document which
             OCCT API calls are used and what tessellation parameters control
             the output mesh quality. Cross-reference 05_external_dependencies.md
             section 16 (OCCT).
             Commit: `annotate: STEP/OCCT parser (T4015)`

- [x] T4016  annotate+verify: src/libslic3r/Format/SL1.cpp
                            + src/libslic3r/Format/SL1.hpp
             Context: SLA archive format I/O for the Prusa SL1 ZIP container.
             These are output-only formats for the SLA pipeline. Document the
             archive structure and which SLA-specific data fields they write.
             Commit: `annotate: SL1/SLA format I/O (T4016)`

- [x] T4017  annotate: src/libslic3r/Format/DRC.cpp
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

- [x] T4018  DOCS UPDATE · After T4010–T4017 are all complete: add a
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

- [x] T4020  annotate: src/libslic3r/Arachne/SkeletalTrapezoidation.hpp
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

- [x] T4021  annotate: src/libslic3r/Arachne/utils/HalfEdgeGraph.hpp
                     + src/libslic3r/Arachne/utils/HalfEdge.hpp
                     + src/libslic3r/Arachne/utils/HalfEdgeNode.hpp
             Context: Half-edge data structure used by SkeletalTrapezoidation
             to represent the straight skeleton graph. These files form a single
             conceptual unit. Read SkeletalTrapezoidationGraph.hpp (T168) first
             — it inherits or wraps these.
             Commit: `annotate: Arachne half-edge graph types (T4021)`

- [x] T4022  annotate: src/libslic3r/Arachne/utils/PolylineStitcher.cpp
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

- [x] T4023  annotate: src/libslic3r/Arachne/utils/linearAlg2D.hpp
                     + src/libslic3r/Arachne/utils/PolygonsPointIndex.hpp
                     + src/libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp
             Context: Remaining Arachne utility files not covered by T164–T184
             and T4020–T4022. These headers provide the math and polygon index
             helpers shared across the straight-skeleton implementation.
             Commit: `annotate: remaining Arachne/ files (T4023)`

- [x] T4024  DOCS UPDATE · After T4020–T4023 are complete: return to
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

- [x] T4030  annotate: src/libslic3r/GCode/ExtrusionProcessor.hpp
             Context: Mentioned in 03_algorithmic_complexities.md Section 9
             (curled extrusion estimation) but absent from Phase 1 registry.
             Document the curled-line detection API surface and how it feeds into
             Layer::curled_lines. This is a BBS addition with no PrusaSlicer
             equivalent — translation agents need full documentation.
             Commit: `annotate: ExtrusionProcessor (T4030)`

- [x] T4031  annotate: src/libslic3r/GCode/ThumbnailData.cpp
                     + src/libslic3r/GCode/ThumbnailData.hpp
                     + src/libslic3r/GCode/Thumbnails.cpp
                     + src/libslic3r/GCode/Thumbnails.hpp
             Context: Thumbnail image embedding in G-code files (PNG preview
             in gcode header). Shared image format between .gcode and .bgcode
             outputs. Group these four as they form one feature unit.
             Commit: `annotate: GCode thumbnail embedding (T4031)`

- [x] T4032  annotate: src/libslic3r/GCode/AvoidCrossingPerimeters.hpp
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

- [x] T4040  annotate: src/libslic3r/Fill/FillTpmsD.cpp
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

- [x] T4041  annotate: src/libslic3r/Fill/FillAdaptive.hpp
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

- [x] T4042  DOCS UPDATE · After T4040–T4041 are complete: add TPMS algorithm
             entries to 03_algorithmic_complexities.md Section 3 (Infill
             Algorithms). The current section mentions FillTpmsD and FillTpmsFK
             by name only. Add: the implicit surface equations for both surfaces,
             the cross-section extraction method, and the adaptive sampling
             algorithm. Then add a pseudocode_tpms_infill.md if the cross-section
             extraction is not obvious from the algorithm description alone.
             Commit: `docs: TPMS infill algorithm section (T4042)`

---

### slic3r/Utils/ Classification and Selective Annotation

- [x] T4050  CLASSIFY · Classify all files under src/slic3r/Utils/.
             For each file, check the first 30 lines for any #include of a
             libslic3r/ core header (Model.hpp, Print.hpp, PrintConfig.hpp,
             GCode*.hpp, Layer.hpp, etc.).
             - Files with no core includes = SKIP_GUI (network/printer UI only)
             - Files that include core libslic3r types = ANNOTATE
             Write the results into the Phase 4 Skip Registry at the bottom of
             this file. Write ANNOTATE-classified files as new tasks T4051+.
             Do NOT annotate any file during this task.
             Commit: `docs: slic3r/Utils/ classification (T4050)`

- [x] T4051  annotate: src/slic3r/Utils/AstroBox.cpp
             + src/slic3r/Utils/CrealityPrint.hpp
             + src/slic3r/Utils/Duet.cpp
             + src/slic3r/Utils/ESP3D.cpp
             + src/slic3r/Utils/ElegooLink.hpp
             + src/slic3r/Utils/FlashAir.cpp
             + src/slic3r/Utils/Flashforge.cpp
             + src/slic3r/Utils/Flashforge.hpp
             + src/slic3r/Utils/MKS.cpp
             + src/slic3r/Utils/Obico.hpp
             + src/slic3r/Utils/OctoPrint.hpp
             + src/slic3r/Utils/PrintHost.cpp
             + src/slic3r/Utils/PrintHost.hpp
             + src/slic3r/Utils/Repetier.cpp
             Context: PrintConfig-driven print-host adapters and the shared
             PrintHost upload entrypoints. Grouped because their first-order
             coupling is printer/upload config extracted from libslic3r.
             Commit: `annotate: slic3r/Utils print-host PrintConfig adapters (T4051)`

- [x] T4052  annotate: src/slic3r/Utils/CrealityPrint.cpp
             + src/slic3r/Utils/ElegooLink.cpp
             + src/slic3r/Utils/NetworkAgentFactory.hpp
             + src/slic3r/Utils/Obico.cpp
             + src/slic3r/Utils/OctoPrint.cpp
             + src/slic3r/Utils/Process.cpp
             + src/slic3r/Utils/bambu_networking.hpp
             Context: AppConfig-backed network/upload helpers and process glue.
             These files couple the GUI/network layer to persisted runtime
             config in libslic3r.
             Commit: `annotate: slic3r/Utils AppConfig network glue (T4052)`

- [ ] T4053  annotate: src/slic3r/Utils/BBLNetworkPlugin.cpp
             + src/slic3r/Utils/BBLNetworkPlugin.hpp
             + src/slic3r/Utils/ICloudServiceAgent.hpp
             + src/slic3r/Utils/NetworkAgent.cpp
             + src/slic3r/Utils/NetworkAgent.hpp
             + src/slic3r/Utils/OrcaCloudServiceAgent.cpp
             + src/slic3r/Utils/SimplyPrint.cpp
             + src/slic3r/Utils/json_diff.cpp
             Context: ProjectTask/Utils-backed cloud-service orchestration.
             Grouped because these files bridge libslic3r job/runtime helpers
             into network agents rather than pure printer-specific adapters.
             Commit: `annotate: slic3r/Utils cloud agent orchestration (T4053)`

- [ ] T4054  annotate: src/slic3r/Utils/MoonrakerPrinterAgent.cpp
             + src/slic3r/Utils/PresetUpdater.cpp
             + src/slic3r/Utils/QidiPrinterAgent.cpp
             + src/slic3r/Utils/SnapmakerPrinterAgent.cpp
             Context: Preset and PresetBundle consumers in slic3r/Utils.
             Group these because they couple network/device utilities to saved
             printer preset state rather than direct model or geometry types.
             Commit: `annotate: slic3r/Utils preset-driven agents (T4054)`

- [ ] T4055  annotate: src/slic3r/Utils/CalibUtils.cpp
             + src/slic3r/Utils/CalibUtils.hpp
             + src/slic3r/Utils/RaycastManager.hpp
             Context: Model and geometry touching utilities. CalibUtils reaches
             into Model/CutUtils/ClipperUtils while RaycastManager exposes core
             mesh/raycast structures to GUI tooling.
             Commit: `annotate: slic3r/Utils model geometry helpers (T4055)`

- [ ] T4056  annotate: src/slic3r/Utils/InstanceID.cpp
             + src/slic3r/Utils/UndoRedo.cpp
             + src/slic3r/Utils/UndoRedo.hpp
             Context: Undo/config identity plumbing. These files couple the GUI
             history stack to ObjectID, Config, PrintConfig, and shared runtime
             helpers in libslic3r.
             Commit: `annotate: slic3r/Utils undo identity bridge (T4056)`

- [ ] T4057  annotate: src/slic3r/Utils/EmbossStyleManager.cpp
             + src/slic3r/Utils/EmbossStyleManager.hpp
             + src/slic3r/Utils/FontConfigHelp.cpp
             + src/slic3r/Utils/WxFontUtils.cpp
             + src/slic3r/Utils/WxFontUtils.hpp
             Context: Emboss/text-facing utilities that pull core geometry,
             text configuration, and emboss data types into wxWidgets helpers.
             Commit: `annotate: slic3r/Utils emboss font helpers (T4057)`

- [ ] T4058  annotate: src/slic3r/Utils/Http.hpp
             + src/slic3r/Utils/Serial.cpp
             Context: Remaining core-coupled utility interfaces. These files
             only touch Exception-based libslic3r error/reporting surfaces.
             Commit: `annotate: slic3r/Utils exception utility glue (T4058)`

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

- [ ] T4070  LINK VERIFICATION · Verify all file:line references in documentation
             files that were created or updated during Phase 4.

             Scope: only documentation changes made in Phase 4. Do not re-check
             links already verified.

             Procedure:
             1. Identify every documentation file touched during Phase 4:
                - 03_algorithmic_complexities.md (updated by T4018, T4024, T4042)
                - 04_refactoring_hazards.md (updated by all annotation tasks)
                - pseudocode_arachne_straight_skeleton.md (created by T4024)
                - pseudocode_tpms_infill.md (created by T4042, if applicable)
                - Any other .md file written or modified in Phase 4.

             2. From each of those files, extract every reference of the form
                `file.cpp#L<n>`, `file.hpp#L<n>`, or `path/to/file.ext#L<n>`.
                Extract only references added or modified in Phase 4 —
                references carried over from prior phases are already verified.

             3. For each extracted reference:
                a. Open the source file at the given line number.
                b. Confirm the referenced content (function name, variable,
                   comment block, or structural feature) is present within
                   ±20 lines of the stated line number.
                c. If content is present within ±20 lines but the line number
                   has drifted: update the reference in the documentation file
                   to the correct line number. Record as UPDATED.
                d. If content is present and line number is exact: record as OK.
                e. If the referenced content cannot be found within ±20 lines
                   of the stated number (function renamed, removed, or file
                   restructured): replace the link in the documentation file
                   with `[LINK BROKEN — <reason>]` and record as BROKEN.

             4. Append results to
                `generated_documentation/link_verification_report.md`
                as a new section titled `## Phase 4 Verification`
                using this table format:
                  | Doc file | Link | Expected content | Status |
                  |----------|------|-----------------|--------|

             5. If any BROKEN entries exist: add a note under the table
                explaining what investigation would be needed to fix each one.

             Acceptance criteria: the Phase 4 section of the report must exist
             and contain an entry for every extracted reference. Zero entries
             means either no links were added in Phase 4 (note this explicitly)
             or the extraction step was skipped (not acceptable).
             Commit: `docs: Phase 4 link verification (T4070)`

- [ ] T4071  HAZARD BLOCKERS UPDATE · Scan all new hazard entries created during
             Phase 4 annotation for P1/High severity. Add any found to the
             Critical Blockers summary at the top of 04_refactoring_hazards.md.
             Commit: `docs: Phase 4 critical blockers update (T4071)`

- [ ] T4072  REVIEW PACKAGE UPDATE · Regenerate the human-review handoff
             document to reflect the completed Phase 4 work.

             Procedure:
             1. Open `generated_documentation/REVIEW_PACKAGE.md` and rewrite
                it in full. Do not append to the existing file — replace it.
                The new file must contain all five sections below.

             2. Section 1 — Coverage Summary:
                - Re-run the file enumeration:
                    find src/ -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | sort
                - Count total source files found.
                - Count annotated files: all [x] DONE tasks across Phase 1
                  and Phase 4 with the `annotate:` prefix.
                - Count explicitly skipped files: sum of all entries in the
                  Phase 4 Skip Registry plus all prior SKIP_GUI / SKIP_TRIVIAL /
                  SKIP_VENDORED entries recorded in T4000 and T4050.
                - Compute: unresolved = total − annotated − skipped.
                - The unresolved count MUST be 0. If it is not 0, do not
                  proceed to sections 2–5. Instead, write a single line:
                  "PHASE 4 INCOMPLETE — N files unresolved. Complete all
                  remaining annotation tasks before regenerating this document."
                  Commit that stub, then stop.

             3. Section 2 — Documentation File Inventory:
                Produce a table with one row per file in
                `generated_documentation/`. Columns:
                  | File | Line count | Last-updated task/commit | Coverage summary |
                Count lines with `wc -l` or equivalent. Write the coverage
                summary as one sentence describing what the file contains.
                Include every .md file: documentation files, pseudocode files,
                the journal, the archive, the link verification report, and
                this review package itself.

             4. Section 3 — Pseudocode File Inventory:
                Produce a table with one row per pseudocode file. Columns:
                  | File | Source files covered | Translation Note count |
                Count Translation Notes by searching each pseudocode file for
                the string "TN " or "Translation Note" — use whichever prefix
                the existing pseudocode files use for consistency.

             5. Section 4 — Open Items Requiring Human Judgment:
                4.1 Escalated [UNCLEAR] tags: list every tag still marked
                    `[UNCLEAR → ESCALATED]` across all annotated source files.
                    Format as a table: File | Line | Reason blocked.
                    Do not list tags marked `[UNCLEAR → RESOLVED]`.
                4.2 Broken links: copy all BROKEN entries from
                    link_verification_report.md (both the T301 section and
                    the Phase 4 section). If none exist, write "None."
                4.3 Skip decisions to audit: list every SKIP_GUI,
                    SKIP_TRIVIAL, and SKIP_VENDORED entry from the Phase 4
                    Skip Registry that a human should sanity-check before
                    claiming full coverage. Flag any SKIP_GUI file that
                    imports a libslic3r core header — these may have been
                    misclassified.
                4.4 Duplicate task entries: note that T105 and T413 both
                    record src/libslic3r/PrintObject.cpp. Confirm the file
                    was annotated once and the duplicate entry is harmless.

             6. Section 5 — Suggested Reading Order for Translation Agents:
                Write a numbered list of at most 8 items. Each item names a
                specific documentation file or pseudocode file and gives one
                sentence explaining what a translation agent will learn from
                it and when in the porting process they need it. Base this
                on the actual content of the files as they exist after Phase 4,
                not on a prior version of this section.

             7. Update the CURRENT STATUS block at the top of
                `generated_documentation/agent_journal.md`:
                - Set "Last session" to the current session number.
                - Set "Active task" to "T4072 — complete".
                - Set "Next action" to "Awaiting human review."
                - Set "Unresolved files" to 0 (or the actual count if
                  the phase is incomplete).
                - Set "Files remaining (Phase 4)" to 0.

             Acceptance criteria: REVIEW_PACKAGE.md must exist, must have all
             five sections populated with real counts (not placeholders), and
             the coverage summary unresolved count must be 0.
             Commit both files together:
             `docs: regenerate REVIEW_PACKAGE.md and update journal (T4072)`

---

## Phase 4 Skip Registry
(Populated by T4000 and T4050 — do not edit manually)

| File | Classification | Reason |
|------|---------------|--------|
| src/slic3r/Utils/ASCIIFolding.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/ASCIIFolding.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/AstroBox.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/BBLCloudServiceAgent.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/BBLCloudServiceAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/BBLPrinterAgent.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/BBLPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Bonjour.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Bonjour.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/ColorSpaceConvert.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/ColorSpaceConvert.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Duet.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/ESP3D.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FileHelp.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FileHelp.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FileTransferUtils.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FileTransferUtils.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FixModelByWin10.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FixModelByWin10.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FlashAir.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/FontConfigHelp.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/HexFile.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/HexFile.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Http.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/IPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/InstanceID.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/MKS.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/MacDarkMode.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/MacDarkMode.mm | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/MoonrakerPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/NetworkAgentFactory.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/OrcaCloudServiceAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/OrcaPrinterAgent.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/OrcaPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/PresetUpdater.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Process.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Profile.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/ProfileDescription.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/QidiPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/RaycastManager.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Repetier.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/RetinaHelper.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/RetinaHelperImpl.hmm | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/RetinaHelperImpl.mm | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/Serial.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/SerialMessage.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/SerialMessageType.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/SimplyPrint.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/SnapmakerPrinterAgent.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/TCPConsole.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/TCPConsole.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/WebSocketClient.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/json_diff.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/minilzo_extension.cpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
| src/slic3r/Utils/minilzo_extension.hpp | SKIP_GUI | No libslic3r core include in first 30 lines. |
