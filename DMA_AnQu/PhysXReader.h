#pragma once
// ════════════════════════════════════════════════════════════════
//  PhysXReader.h — DMA 读取游戏 PhysX 场景, 本地重建用于射线检测
//  从 HPJY 项目 PhysX3.ixx 移植, 适配 AnQu DMA 架构
//
//  读取链路:
//    g_World + 0x3A0 → FPhysScene*
//    FPhysScene + 0xD0 → PxScene* (NpScene)
//    NpScene + 0x2590 → PArray {Data, NumElements, MaxElements}
//    PArray.Data[i*8] → PxActor*
//    PxActor + 0x8 → mConcreteType (6=Dynamic, 7=Static)
//    PxActor + 0x28 → ShapeManager → PtrTable {mSingle, mCount}
//    NpShape + 0x98 → mGeometry (type + data)
// ════════════════════════════════════════════════════════════════
#include <PxPhysicsAPI.h>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_set>
#include "Mem.h"
#include "Offset.h"
#include "PhysXEngine.h"

namespace PhysXReader {

// ── PhysX 内存结构体定义 (匹配 PhysX 3.x 内存布局) ──

// PhysX 内联数组 (类 TArray)
struct PArray {
    int64_t Data;
    int32_t NumElements;
    int32_t MaxElements;
};

// NpShapeManager 内的 PtrTable (内联指针表)
struct PtrTable {
    int64_t mSingle;     // 单个时直接是指针, 多个时是数组指针
    int16_t mCount;
    bool    mOwnsMemory;
    bool    mBufferUsed;
};

// NpShapeManager
struct NpShapeManager {
    PtrTable mShapes;
};

// PxGeometry 头部
struct PxGeometryHeader {
    int16_t type;
};

// PxFilterData
struct PxFilterData {
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3;
    bool operator==(const PxFilterData& o) const {
        return word0 == o.word0 && word1 == o.word1 && word2 == o.word2 && word3 == o.word3;
    }
};

// PxBoxGeometry 内存布局
struct PxBoxGeometryMem {
    int16_t type;       // 0
    uint16_t padding;
    physx::PxVec3 halfExtents;
};

// PxConvexMeshGeometry 内存布局
struct PxConvexMeshGeometryMem {
    int16_t type;           // 3
    uint16_t padding1;
    physx::PxMeshScale scale;
    int64_t convexMesh;     // PxConvexMesh*
    float maxMargin;
    uint8_t meshFlags;
};

// PxTriangleMeshGeometry 内存布局
struct PxTriangleMeshGeometryMem {
    int16_t type;           // 4
    uint16_t padding1;
    physx::PxMeshScale scale;
    uint8_t meshFlags;
    uint8_t padding2[3];
    uint8_t padding3[4];
    int64_t triangleMesh;   // PxTriangleMesh*
};

// PxHeightFieldGeometry 内存布局
struct PxHeightFieldGeometryMem {
    int16_t type;           // 5
    uint16_t padding[3];
    int64_t heightField;    // PxHeightField*
    float heightScale;
    float rowScale;
    float columnScale;
    int32_t heightFieldFlags;
};

// PxTriangleMesh 内部数据
struct PxTriangleMeshData {
    uint8_t  paddingV[0x20];
    uint32_t mNbVertices;
    uint32_t mNbTriangles;
    int64_t  mVertices;     // PxVec3* array
    int64_t  mTriangles;    // PxU16* or PxU32* array
    // AABB (28 bytes) follows
    uint8_t  mFlags;        // bit 1 = 16bit indices
};

// PxConvexMesh 内部数据
struct PxConvexHullData {
    // AABB (24 bytes)
    physx::PxVec3 mCenter;      // +0x18
    // mNbEdges (PxBitAndWord)
    uint16_t mNbEdgesValue;     // +0x24
    uint16_t mNbEdgesBits;
    uint8_t  mNbHullVertices;   // +0x28
    uint8_t  mNbPolygons;       // +0x29
    // padding
    int64_t  mPolygons;         // +0x30 (pointer to PhHullPolygonData array)
    // After polygons: mNbHullVertices * PxVec3 (hull vertices)
};

struct PxConvexMeshData {
    uint8_t padding1[0x20];
    PxConvexHullData mHullData;
};

// PxHeightField 内部数据
struct PxHeightFieldData {
    // AABB (24 bytes: center + extents)
    physx::PxVec3 mAABBCenter;
    physx::PxVec3 mAABBExtents;
    uint32_t mRows;
    uint32_t mColumns;
    float mRowLimit;
    float mColLimit;
    float mNbColumns;
    uint8_t pad[4];
    int64_t mSamples;  // PxHeightFieldSample*[]
};

struct PxHeightFieldMem {
    uint8_t paddingV[0x20];
    PxHeightFieldData mData;
    uint32_t mSampleStride;
    uint32_t mNbSamples;
    float mMinHeight;
    float mMaxHeight;
    uint32_t mModifyCount;
};

// ── 已加载 Actor 去重 ──
inline std::unordered_set<int64_t> g_LoadedActors;
inline int g_LastActorCount = 0;
inline int g_LastShapeCount = 0;
inline bool g_SceneValid = false;  // PhysX 场景链路是否验证通过
inline int g_InitRetryCount = 0;  // 初始化重试计数

// ════════════════════════════════════════════════════════════════
//  获取 PxScene 指针
//  链路: UWorld → FPhysScene(0x3A0) → PxScene(0xD0)
// ════════════════════════════════════════════════════════════════
inline int64_t GetPxScene(DWORD64 world) {
    if (!world) return 0;
    int64_t physScene = mem.Read<int64_t>(world + Offset_PhysicsScene);
    if (!physScene || physScene < 0x10000) return 0;
    int64_t pxScene = mem.Read<int64_t>(physScene + Offset_mPhysXScene);
    if (!pxScene || pxScene < 0x10000) return 0;

    // 验证: 读取 NpScene 的 actors PArray, 检查合理性
    PArray actorsArray = mem.Read<PArray>(pxScene + PxOffset_NpSceneActors);
    if (actorsArray.NumElements < 0 || actorsArray.NumElements > 100000) return 0;
    if (!actorsArray.Data || actorsArray.Data < 0x10000) return 0;

    g_SceneValid = true;
    return pxScene;
}

// ════════════════════════════════════════════════════════════════
//  读取 PxTransform (PxQuat + PxVec3 + pad = 32 bytes)
// ════════════════════════════════════════════════════════════════
inline physx::PxTransform ReadPxTransform(int64_t addr) {
    return mem.Read<physx::PxTransform>(addr);
}

// ════════════════════════════════════════════════════════════════
//  处理单个 Shape — 读取几何并附加到本地 Actor
// ════════════════════════════════════════════════════════════════
inline void ProcessShape(int64_t shapePtr, physx::PxRigidActor* localActor,
                         const physx::PxTransform& globalPos) {
    if (!shapePtr || !localActor) return;

    // 读取 filterData
    PxFilterData filterData = mem.Read<PxFilterData>(shapePtr + PxOffset_FilterData);
    // 过滤: 只处理 word3==19 或 HEIGHTFIELD (同 HPJY)
    // word3==19 = WorldStatic 碰撞通道
    bool isHeightField = false;
    auto geomType = mem.Read<int16_t>(shapePtr + PxOffset_ShapeGeometry);
    if (geomType == (int16_t)physx::PxGeometryType::eHEIGHTFIELD) {
        isHeightField = true;
    }
    if (filterData.word3 != 19 && !isHeightField) {
        return;
    }

    // 读取 local pose
    physx::PxTransform localPos = ReadPxTransform(shapePtr + PxOffset_ShapeLocalPos);

    // 读取完整 geometry
    PxGeometryHeader geomHeader = mem.Read<PxGeometryHeader>(shapePtr + PxOffset_ShapeGeometry);

    switch (geomHeader.type) {

    case (int16_t)physx::PxGeometryType::eBOX: {
        auto boxGeom = mem.Read<PxBoxGeometryMem>(shapePtr + PxOffset_ShapeGeometry);
        if (boxGeom.halfExtents.x <= 0 || boxGeom.halfExtents.y <= 0 || boxGeom.halfExtents.z <= 0)
            break;

        physx::PxBoxGeometry boxGeo(boxGeom.halfExtents);
        physx::PxShape* shape = PhysXEngine::gPhysics->createShape(boxGeo, *PhysXEngine::gMaterial);
        if (shape) {
            shape->setQueryFilterData(physx::PxFilterData(filterData.word0, filterData.word1,
                                                           filterData.word2, filterData.word3));
            shape->setLocalPose(localPos);
            localActor->attachShape(*shape);
            shape->release();
            PhysXEngine::g_ShapeCount++;
        }
        break;
    }

    case (int16_t)physx::PxGeometryType::eCONVEXMESH: {
        auto convGeom = mem.Read<PxConvexMeshGeometryMem>(shapePtr + PxOffset_ShapeGeometry);
        if (!convGeom.convexMesh) break;

        // 读取 ConvexMesh 内部数据
        auto convexData = mem.Read<PxConvexMeshData>(convGeom.convexMesh);
        uint8_t nbVerts = convexData.mHullData.mNbHullVertices;
        if (nbVerts == 0 || nbVerts > 255) break;

        // 顶点存储在 mPolygons + sizeof(HullPolygonData) * mNbPolygons 之后
        // HPJY 的 getHullVertices() 逻辑:
        //   ptr = mPolygons + sizeof(PhHullPolygonData) * mNbPolygons
        //   return (PxVec3*)ptr
        // PhHullPolygonData = PxPlane(16) + PxU16(2) + PxU8(1) + PxU8(1) = 20 bytes
        // 但实际 sizeof 可能因对齐而不同, 用 HPJY 的偏移计算方式
        uint8_t nbPolygons = convexData.mHullData.mNbPolygons;
        int64_t vertAddr = convexData.mHullData.mPolygons
                         + (20 * nbPolygons);  // 20 = sizeof(PhHullPolygonData)

        auto vertBuf = std::make_unique<physx::PxVec3[]>(nbVerts);
        mem.Read(vertAddr, vertBuf.get(), nbVerts * sizeof(physx::PxVec3));

        auto convMesh = PhysXEngine::CreateConvexMesh(nbVerts, vertBuf.get());
        if (convMesh) {
            physx::PxConvexMeshGeometry convGeo(convMesh, convGeom.scale);
            physx::PxShape* shape = PhysXEngine::gPhysics->createShape(convGeo, *PhysXEngine::gMaterial);
            if (shape) {
                shape->setQueryFilterData(physx::PxFilterData(filterData.word0, filterData.word1,
                                                               filterData.word2, filterData.word3));
                shape->setLocalPose(localPos);
                localActor->attachShape(*shape);
                shape->release();
                PhysXEngine::g_ShapeCount++;
            }
            convMesh->release();
        }
        break;
    }

    case (int16_t)physx::PxGeometryType::eTRIANGLEMESH: {
        auto triGeom = mem.Read<PxTriangleMeshGeometryMem>(shapePtr + PxOffset_ShapeGeometry);
        if (!triGeom.triangleMesh) break;

        // 读取 TriangleMesh 内部数据
        auto triData = mem.Read<PxTriangleMeshData>(triGeom.triangleMesh);
        uint32_t nbVerts = triData.mNbVertices;
        uint32_t nbTris  = triData.mNbTriangles;
        if (nbTris == 0 || nbTris > 1000000 || nbVerts == 0 || nbVerts > 1000000) break;

        // 判断索引格式 (16bit or 32bit)
        bool has16bit = (triData.mFlags & 2) != 0;

        // 读取顶点
        auto vertBuf = std::make_unique<physx::PxVec3[]>(nbVerts);
        mem.Read(triData.mVertices, vertBuf.get(), nbVerts * sizeof(physx::PxVec3));

        // 读取索引并转换为 32bit
        std::vector<physx::PxU32> indices;
        indices.reserve(nbTris * 3);
        if (has16bit) {
            auto idxBuf = std::make_unique<uint16_t[]>(nbTris * 3);
            mem.Read(triData.mTriangles, idxBuf.get(), nbTris * 3 * sizeof(uint16_t));
            for (uint32_t i = 0; i < nbTris * 3; i++) {
                if (idxBuf[i] < nbVerts) indices.push_back(idxBuf[i]);
                else indices.push_back(0);  // 无效索引, 后续三角形会被跳过
            }
        } else {
            auto idxBuf = std::make_unique<uint32_t[]>(nbTris * 3);
            mem.Read(triData.mTriangles, idxBuf.get(), nbTris * 3 * sizeof(uint32_t));
            for (uint32_t i = 0; i < nbTris * 3; i++) {
                if (idxBuf[i] < nbVerts) indices.push_back(idxBuf[i]);
                else indices.push_back(0);
            }
        }

        // 清理无效三角形 (任一索引为0的占位)
        // 实际上 createTriangleMesh 会自行处理

        auto triMesh = PhysXEngine::CreateTriangleMesh(nbVerts, vertBuf.get(),
                                                         indices.size() / 3, indices.data());
        if (triMesh) {
            physx::PxTriangleMeshGeometry triGeo(triMesh, triGeom.scale);
            physx::PxShape* shape = PhysXEngine::gPhysics->createShape(triGeo, *PhysXEngine::gMaterial);
            if (shape) {
                shape->setQueryFilterData(physx::PxFilterData(filterData.word0, filterData.word1,
                                                               filterData.word2, filterData.word3));
                shape->setLocalPose(localPos);
                localActor->attachShape(*shape);
                shape->release();
                PhysXEngine::g_ShapeCount++;
            }
            triMesh->release();
        }
        break;
    }

    case (int16_t)physx::PxGeometryType::eHEIGHTFIELD: {
        auto hfGeom = mem.Read<PxHeightFieldGeometryMem>(shapePtr + PxOffset_ShapeGeometry);
        if (!hfGeom.heightField) break;

        auto hfData = mem.Read<PxHeightFieldMem>(hfGeom.heightField);
        uint32_t nbRows = hfData.mData.mRows;
        uint32_t nbCols = hfData.mData.mColumns;
        uint32_t nbSamples = nbRows * nbCols;
        if (nbCols == 0 || nbRows == 0 || nbCols > 1000000) break;

        auto sampleBuf = std::make_unique<physx::PxHeightFieldSample[]>(nbSamples);
        mem.Read(hfData.mData.mSamples, sampleBuf.get(), nbSamples * sizeof(physx::PxHeightFieldSample));

        auto heightField = PhysXEngine::CreateHeightField(nbRows, nbCols, sampleBuf.get());
        if (heightField) {
            physx::PxHeightFieldGeometry hfGeo(heightField, physx::PxMeshGeometryFlags(),
                                                hfGeom.heightScale, hfGeom.rowScale, hfGeom.columnScale);
            physx::PxShape* shape = PhysXEngine::gPhysics->createShape(hfGeo, *PhysXEngine::gMaterial);
            if (shape) {
                shape->setQueryFilterData(physx::PxFilterData(filterData.word0, filterData.word1,
                                                               filterData.word2, filterData.word3));
                shape->setLocalPose(localPos);
                localActor->attachShape(*shape);
                shape->release();
                PhysXEngine::g_ShapeCount++;
            }
            heightField->release();
        }
        break;
    }

    default:
        break;
    } // switch
}

// ════════════════════════════════════════════════════════════════
//  处理单个 PxActor — 读取变换 + shapes, 创建本地 Actor
// ════════════════════════════════════════════════════════════════
inline void ProcessActor(int64_t actorPtr) {
    if (!actorPtr) return;

    // 去重: 已加载的 actor 跳过
    if (g_LoadedActors.count(actorPtr)) return;

    // 读取 ConcreteType
    uint16_t concreteType = mem.Read<uint16_t>(actorPtr + PxOffset_ConcreteType);

    physx::PxTransform globalPos{};
    physx::PxRigidActor* localActor = nullptr;

    if (concreteType == 6) {
        // PxRigidDynamic
        auto body2Actor = ReadPxTransform(actorPtr + PxOffset_body2Actor);

        // 检查 mBodyBufferFlags & 0x200 → body2Actor 在 stream buffer 中
        int32_t bodyBufferFlags = mem.Read<int32_t>(actorPtr + PxOffset_BodyBufferFlags);
        if (bodyBufferFlags & 0x200) {
            int64_t streamPtr = mem.Read<int64_t>(actorPtr + PxOffset_ActorStreamPtr);
            if (streamPtr) {
                body2Actor = ReadPxTransform(streamPtr + 0xE0);
            }
        }

        auto body2World = ReadPxTransform(actorPtr + PxOffset_Body2World_Dyn);
        globalPos = body2World * body2Actor.getInverse();

        if (!globalPos.isValid()) return;
        if (globalPos.p.x == 0 || globalPos.p.y == 0) return;

        localActor = PhysXEngine::gPhysics->createRigidDynamic(globalPos);
    }
    else if (concreteType == 7) {
        // PxRigidStatic
        globalPos = ReadPxTransform(actorPtr + PxOffset_Body2World_Sta);

        // 检查 mControlState & 0x40 → Body2World 在 stream buffer 中
        uint8_t controlState = mem.Read<uint8_t>(actorPtr + PxOffset_StaticControlState);
        if (controlState & 0x40) {
            int64_t streamPtr = mem.Read<int64_t>(actorPtr + PxOffset_ActorStreamPtr);
            if (streamPtr) {
                globalPos = ReadPxTransform(streamPtr + 0xB0);
            }
        }

        if (!globalPos.isValid()) return;
        if (globalPos.p.x == 0 || globalPos.p.y == 0) return;

        localActor = PhysXEngine::gPhysics->createRigidStatic(globalPos);
    }
    else {
        return;
    }

    if (!localActor) return;

    // 读取 ShapeManager
    auto shapeManager = mem.Read<NpShapeManager>(actorPtr + PxOffset_ShapeManager);
    int16_t shapeCount = shapeManager.mShapes.mCount;

    if (shapeCount <= 0) {
        localActor->release();
        return;
    }

    // 读取 shape 指针数组
    std::vector<int64_t> shapePtrs(shapeCount);
    if (shapeCount == 1) {
        shapePtrs[0] = shapeManager.mShapes.mSingle;
    } else {
        mem.Read(shapeManager.mShapes.mSingle, shapePtrs.data(), shapeCount * sizeof(int64_t));
    }

    // 处理每个 shape
    for (int i = 0; i < shapeCount; i++) {
        ProcessShape(shapePtrs[i], localActor, globalPos);
    }

    // 添加到本地场景 (如果有 shape)
    if (localActor->getNbShapes() > 0) {
        PhysXEngine::AddActor(localActor);
        g_LoadedActors.insert(actorPtr);
    } else {
        localActor->release();
    }
}

// ════════════════════════════════════════════════════════════════
//  主入口: 读取游戏 PhysX 场景并重建本地场景
//  调用时机: 每隔几秒执行一次 (增量加载, 不清空已有)
// ════════════════════════════════════════════════════════════════
inline void ReadPhysXScene(DWORD64 world) {
    if (!PhysXEngine::g_Initialized) return;

    int64_t pxScene = GetPxScene(world);
    if (!pxScene) {
        g_InitRetryCount++;
        // 连续失败 5 次, 标记 PhysX 不可用
        if (g_InitRetryCount >= 5 && g_LoadedActors.empty()) {
            g_SceneValid = false;
        }
        return;
    }
    g_InitRetryCount = 0;

    // 读取 actors PArray
    PArray actorsArray = mem.Read<PArray>(pxScene + PxOffset_NpSceneActors);
    if (actorsArray.NumElements <= 0 || actorsArray.NumElements > 100000) return;
    if (!actorsArray.Data) return;

    // 读取所有 actor 指针
    std::vector<int64_t> actorPtrs(actorsArray.NumElements);
    mem.Read(actorsArray.Data, actorPtrs.data(), actorsArray.NumElements * sizeof(int64_t));

    // 处理每个 actor (增量, 跳过已加载)
    int newCount = 0;
    for (int i = 0; i < actorsArray.NumElements; i++) {
        if (!actorPtrs[i]) continue;
        if (g_LoadedActors.count(actorPtrs[i])) continue;

        ProcessActor(actorPtrs[i]);
        newCount++;

        // 每批最多处理 50 个新 actor (避免单次 DMA 读取过多)
        if (newCount >= 50) break;
    }

    g_LastActorCount = (int)g_LoadedActors.size();
    g_LastShapeCount = PhysXEngine::g_ShapeCount;
}

// ════════════════════════════════════════════════════════════════
//  重置: 清空已加载列表和本地场景
//  调用时机: 切换地图 / 手动重置
// ════════════════════════════════════════════════════════════════
inline void ResetScene() {
    PhysXEngine::ClearScene();
    g_LoadedActors.clear();
    g_LastActorCount = 0;
    g_LastShapeCount = 0;
}

} // namespace PhysXReader
