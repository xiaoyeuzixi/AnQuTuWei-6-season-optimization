#pragma once
// ════════════════════════════════════════════════════════════════
//  PhysXEngine.h — 本地 PhysX 4.1 场景管理 + 射线检测
//  从 HPJY 项目移植,适配 AnQu DMA 架构
// ════════════════════════════════════════════════════════════════
#include <PxPhysicsAPI.h>
#include <shared_mutex>
#include <fstream>
#include <vector>
#include <iostream>

namespace PhysXEngine {

// ── 全局 PhysX 对象 ──
inline physx::PxFoundation*      gFoundation   = nullptr;
inline physx::PxPhysics*         gPhysics      = nullptr;
inline physx::PxDefaultCpuDispatcher* gCpuDispatcher = nullptr;
inline physx::PxScene*           gScene        = nullptr;
inline physx::PxMaterial*        gMaterial     = nullptr;
inline physx::PxCooking*         gCooking      = nullptr;

inline physx::PxDefaultAllocator      gAllocator{};
inline physx::PxDefaultErrorCallback  gErrorCallback{};

// ── 线程安全: 读写分离 ──
inline std::shared_mutex sceneMutex;

// ── 统计 ──
inline int g_ActorCount  = 0;
inline int g_ShapeCount  = 0;
inline bool g_Initialized = false;

// ════════════════════════════════════════════════════════════════
//  初始化本地 PhysX 场景
// ════════════════════════════════════════════════════════════════
inline bool InitPhysx() {
    if (g_Initialized) return true;

    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!gFoundation) return false;

    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale());
    if (!gPhysics) return false;

    physx::PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0, -9.81f, 0);
    gCpuDispatcher = physx::PxDefaultCpuDispatcherCreate(4);
    sceneDesc.cpuDispatcher = gCpuDispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_STABILIZATION;

    gScene = gPhysics->createScene(sceneDesc);
    if (!gScene) return false;

    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.2f);
    if (!gMaterial) return false;

    gCooking = PxCreateCooking(PX_PHYSICS_VERSION, *gFoundation,
                               physx::PxCookingParams(physx::PxTolerancesScale()));
    if (!gCooking) return false;

    g_Initialized = true;
    return true;
}

// ════════════════════════════════════════════════════════════════
//  释放 PhysX 资源
// ════════════════════════════════════════════════════════════════
inline void ReleasePhysx() {
    std::unique_lock<std::shared_mutex> lock(sceneMutex);

    if (gScene)    { gScene->release();    gScene = nullptr; }
    if (gCpuDispatcher) { gCpuDispatcher->release(); gCpuDispatcher = nullptr; }
    if (gMaterial) { gMaterial->release(); gMaterial = nullptr; }
    if (gCooking)  { gCooking->release();  gCooking = nullptr; }
    if (gPhysics)  { gPhysics->release();  gPhysics = nullptr; }
    if (gFoundation) { gFoundation->release(); gFoundation = nullptr; }

    g_Initialized = false;
    g_ActorCount = 0;
    g_ShapeCount = 0;
}

// ════════════════════════════════════════════════════════════════
//  清空场景中所有 Actor (用于重新加载)
// ════════════════════════════════════════════════════════════════
inline void ClearScene() {
    std::unique_lock<std::shared_mutex> lock(sceneMutex);
    if (!gScene) return;

    physx::PxU32 nbActors = gScene->getNbActors(
        physx::PxActorTypeFlag::eRIGID_DYNAMIC | physx::PxActorTypeFlag::eRIGID_STATIC);
    if (nbActors > 0) {
        std::vector<physx::PxActor*> actors(nbActors);
        gScene->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC | physx::PxActorTypeFlag::eRIGID_STATIC,
                          actors.data(), nbActors);
        for (physx::PxU32 i = 0; i < nbActors; i++) {
            gScene->removeActor(*actors[i]);
            actors[i]->release();
        }
    }
    g_ActorCount = 0;
    g_ShapeCount = 0;
}

// ════════════════════════════════════════════════════════════════
//  射线检测 — 从 origin 到 dest, 返回是否被遮挡
// ════════════════════════════════════════════════════════════════
inline bool IsOccluded(float ox, float oy, float oz,
                       float dx, float dy, float dz) {
    if (!gScene || !g_Initialized) return false;

    physx::PxVec3 ori(ox, oy, oz);
    physx::PxVec3 des(dx, dy, dz);
    physx::PxVec3 dir = des - ori;
    physx::PxReal length = dir.magnitude();
    if (length < 1.0f) return false;  // 太近,不检测
    dir /= length;

    physx::PxRaycastBuffer hit{};
    std::shared_lock<std::shared_mutex> lock(sceneMutex);
    if (gScene) {
        gScene->raycast(ori, dir, length, hit);
    }
    return hit.hasBlock;  // 有阻挡 = 被遮挡
}

// ── FVector 兼容版本 ──
struct Vec3 { float x, y, z; };
inline bool IsOccluded(const Vec3& origin, const Vec3& dest) {
    return IsOccluded(origin.x, origin.y, origin.z, dest.x, dest.y, dest.z);
}

// ════════════════════════════════════════════════════════════════
//  创建本地碰撞网格 (供 PhysXReader 调用)
// ════════════════════════════════════════════════════════════════
inline physx::PxConvexMesh* CreateConvexMesh(physx::PxU32 numVerts, const physx::PxVec3* verts) {
    if (!gCooking || !gPhysics) return nullptr;

    physx::PxCookingParams params = gCooking->getParams();
    params.convexMeshCookingType = physx::PxConvexMeshCookingType::eQUICKHULL;
    params.gaussMapLimit = 256;
    gCooking->setParams(params);

    physx::PxConvexMeshDesc desc;
    desc.points.data  = verts;
    desc.points.count = numVerts;
    desc.points.stride = sizeof(physx::PxVec3);
    desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

    return gCooking->createConvexMesh(desc, gPhysics->getPhysicsInsertionCallback());
}

inline physx::PxTriangleMesh* CreateTriangleMesh(
    physx::PxU32 numVertices, const physx::PxVec3* vertices,
    physx::PxU32 numTriangles, const physx::PxU32* indices) {
    if (!gCooking || !gPhysics) return nullptr;

    physx::PxTriangleMeshDesc meshDesc;
    meshDesc.points.count    = numVertices;
    meshDesc.points.data     = vertices;
    meshDesc.points.stride   = sizeof(physx::PxVec3);
    meshDesc.triangles.count = numTriangles;
    meshDesc.triangles.data  = indices;
    meshDesc.triangles.stride = 3 * sizeof(physx::PxU32);

    physx::PxCookingParams params = gCooking->getParams();
    params.midphaseDesc = physx::PxMeshMidPhase::eBVH34;
    params.suppressTriangleMeshRemapTable = true;
    params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
    params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
    params.midphaseDesc.mBVH34Desc.numPrimsPerLeaf = 15;
    gCooking->setParams(params);

    return gCooking->createTriangleMesh(meshDesc, gPhysics->getPhysicsInsertionCallback());
}

inline physx::PxHeightField* CreateHeightField(
    physx::PxU32 nbRows, physx::PxU32 nbCols,
    const physx::PxHeightFieldSample* samples) {
    if (!gCooking || !gPhysics) return nullptr;

    physx::PxHeightFieldDesc hfDesc;
    hfDesc.format = physx::PxHeightFieldFormat::eS16_TM;
    hfDesc.nbColumns = nbCols;
    hfDesc.nbRows    = nbRows;
    hfDesc.samples.data    = samples;
    hfDesc.samples.stride  = sizeof(physx::PxHeightFieldSample);

    return gCooking->createHeightField(hfDesc, gPhysics->getPhysicsInsertionCallback());
}

// ════════════════════════════════════════════════════════════════
//  添加 Actor 到场景 (线程安全)
// ════════════════════════════════════════════════════════════════
inline void AddActor(physx::PxRigidActor* actor) {
    if (!actor) return;
    if (actor->getNbShapes() == 0) {
        actor->release();
        return;
    }
    std::unique_lock<std::shared_mutex> lock(sceneMutex);
    if (gScene) {
        gScene->addActor(*actor);
        g_ActorCount++;
    } else {
        actor->release();
    }
}

} // namespace PhysXEngine
