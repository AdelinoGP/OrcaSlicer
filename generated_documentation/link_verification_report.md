# Link Verification Report

- Scanned all Markdown files under `generated_documentation/`.
- Found 22 source line references matching the documentation link pattern.
- Result: 19 `UPDATED`, 0 `BROKEN`, 3 unchanged `OK` references.
- All extracted references were in `generated_documentation/04_refactoring_hazards.md`.

| Doc File | Link | Expected Content | Status |
| --- | --- | --- | --- |
| `generated_documentation/04_refactoring_hazards.md` | `Print.hpp:175` | `typedef std::vector<Layer*> LayerPtrs` | `OK` |
| `generated_documentation/04_refactoring_hazards.md` | `Print.hpp:182` | `typedef std::vector<SupportLayer*> SupportLayerPtrs` | `OK` |
| `generated_documentation/04_refactoring_hazards.md` | `Model.hpp:123 -> Model.hpp:182-185` | `ModelMaterialMap`, `ModelObjectPtrs`, `ModelVolumePtrs`, `ModelInstancePtrs` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `ExtrusionEntity.hpp:148 -> ExtrusionEntity.hpp:167` | `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Point.hpp:56 -> Point.hpp:71` | `using PointsAllocator = tbb::scalable_allocator<BaseType>` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `pchheader.hpp:103` | global TBB includes | `OK` |
| `generated_documentation/04_refactoring_hazards.md` | `TriangleMeshSlicer.cpp:521 -> TriangleMeshSlicer.cpp:590` | `tbb::parallel_for` over triangles | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `TreeSupport.cpp:769 -> TreeSupport.cpp:899` | `tbb::parallel_for` over layers | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `TreeSupport3D.cpp:218 -> TreeSupport3D.cpp:2569` | nested `tbb::parallel_for` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `TreeModelVolumes.cpp:244 -> TreeModelVolumes.cpp:360` | `tbb::task_group` for concurrent avoidance + wall restriction passes | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `ExtrusionEntity.hpp:105 -> ExtrusionEntity.hpp:115-167` | abstract base with pure virtual `clone()` and `clone_move()` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Model.hpp:128 -> Model.hpp:187-227` | `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro generating `new_copy`, `new_clone`, `assign_copy`, `assign_clone` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `ExtrusionEntity.hpp:585 -> ExtrusionEntity.hpp:772-780` | `extrusion_entities_append_loops` manually appends `points.front()` to close | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `PrintBase.hpp:46 -> PrintBase.hpp:96-180` | `PrintStateBase` with `StateWithTimeStamp`, `StateWithWarnings` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `PrintBase.hpp:150 -> PrintBase.hpp:239-320` | `set_started()`, `set_done()`, `invalidate()` all take `std::mutex &mtx` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `PrintBase.hpp:104 -> PrintBase.hpp:180` | `static size_t g_last_timestamp` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Point.hpp:56 -> Point.hpp:71` | `using PointsAllocator = tbb::scalable_allocator<BaseType>` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `PrintBase.hpp:40 -> PrintBase.hpp:90-94` | `class CanceledException : public std::exception` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `ExtrusionEntity.hpp:148 -> ExtrusionEntity.hpp:167` | `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Print.hpp:291 -> Print.hpp:298-299` | `PrintObjectRegions` with manual `ref_cnt_inc()` / `ref_cnt_dec()` and `delete this` | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Print.hpp:210 -> Print.hpp:199` | `PrintInstance::shift` world coordinate shift | `UPDATED` |
| `generated_documentation/04_refactoring_hazards.md` | `Print.hpp:211 -> Print.hpp:210` | `instance_shift is too large because of multi-plate, apply without plate offset` comment | `UPDATED` |
