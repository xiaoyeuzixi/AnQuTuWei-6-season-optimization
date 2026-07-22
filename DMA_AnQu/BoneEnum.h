#pragma once

/*
 * BoneEnum.h — 骨骼索引枚举（移植自 AnQu）
 * 
 * Unreal Engine 角色骨骼索引，用于 ESP 骨骼连线绘制。
 * 头→脊椎→骨盆→四肢的层次结构。
 */

enum Bones {
    Root = 0,
    HEAD = 16,
    neck_01 = 15,
    pelvis = 1,
    spine_03 = 14,

    clavicle_l = 50,
    upperarm_l = 51,
    lowerarm_l = 53,
    hand_l = 54,

    clavicle_R = 20,
    upperarm_r = 21,
    lowerarm_r = 23,
    hand_r = 24,

    thigh_l = 2,
    thigh_twist_01_l = 3,
    calf_l = 4,
    foot_l = 5,

    thigh_r = 7,
    calf_r = 9,
    foot_r = 10,
};
