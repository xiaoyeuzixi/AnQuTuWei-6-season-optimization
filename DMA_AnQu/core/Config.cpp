/*
 * core/Config.cpp — SaveGlobalConfig / LoadGlobalConfig 实现
 *
 * 依赖: core/Config.h (配置变量), Offset.h (ACE_CacheTable_RVA)
 */

#include "Config.h"
#include "../Offset.h"
#include <cstdio>
#include <cstring>
#include <string>

void SaveGlobalConfig(const char* fn) {
    FILE* fp = nullptr; fopen_s(&fp, fn, "w"); if (!fp) return;
    fprintf(fp, "MaxDist=%d\n", g_MaxDist);
    fprintf(fp, "DrawAll=%d\n", g_DrawAll);
    fprintf(fp, "ShowBox=%d\n", g_ShowBox);
    fprintf(fp, "ShowRays=%d\n", g_ShowRays);
    fprintf(fp, "ShowSkeleton=%d\n", g_ShowSkeleton);
    fprintf(fp, "ShowDistance=%d\n", g_ShowDistance);
    fprintf(fp, "DrawAI=%d\n", g_DrawAI ? 1 : 0);
    fprintf(fp, "ShowAISkeleton=%d\n", g_ShowAISkeleton ? 1 : 0);
    fprintf(fp, "AIMaxDist=%d\n", g_AIMaxDist);
    fprintf(fp, "DrawSelf=%d\n", g_DrawSelf);
    fprintf(fp, "DrawTeammate=%d\n", g_DrawTeammate);
    fprintf(fp, "CheckTeam=%d\n", g_CheckTeam);
    fprintf(fp, "ShowTeamId=%d\n", g_ShowTeamId);
    fprintf(fp, "ShowWeapon=%d\n", g_ShowWeapon);
    fprintf(fp, "ShowName=%d\n", g_ShowName);
    fprintf(fp, "ShowRarity=%d,%d,%d,%d,%d,%d,%d\n",
        g_ShowRarity[0], g_ShowRarity[1], g_ShowRarity[2],
        g_ShowRarity[3], g_ShowRarity[4], g_ShowRarity[5], g_ShowRarity[6]);
    fprintf(fp, "ShowArmor=%d\n", g_ShowArmor);
    fprintf(fp, "ShowAmmo=%d\n", g_ShowAmmo);
    fprintf(fp, "ScanItems=%d\n", g_ScanItems);
    fprintf(fp, "ShowNearbyItems=%d\n", g_ShowNearbyItems);
    fprintf(fp, "NearbyItemsDist=%d\n", g_NearbyItemsDist);
    fprintf(fp, "colBox=%.4f,%.4f,%.4f,%.4f\n", g_colBox[0], g_colBox[1], g_colBox[2], g_colBox[3]);
    fprintf(fp, "colAI=%.4f,%.4f,%.4f,%.4f\n", g_colAI[0], g_colAI[1], g_colAI[2], g_colAI[3]);
    fprintf(fp, "colSkeleton=%.4f,%.4f,%.4f,%.4f\n", g_colSkeleton[0], g_colSkeleton[1], g_colSkeleton[2], g_colSkeleton[3]);
    fprintf(fp, "colRay=%.4f,%.4f,%.4f,%.4f\n", g_colRay[0], g_colRay[1], g_colRay[2], g_colRay[3]);
    fprintf(fp, "colText=%.4f,%.4f,%.4f,%.4f\n", g_colText[0], g_colText[1], g_colText[2], g_colText[3]);
    fprintf(fp, "HotkeyVK=%d\n", g_HotkeyVK);
    fprintf(fp, "ItemsHotkeyVK=%d\n", g_ItemsHotkeyVK);
    fprintf(fp, "FpsLimitEnabled=%d\n", g_FpsLimitEnabled ? 1 : 0);
    fprintf(fp, "FpsLimit=%d\n", g_FpsLimit);
    fprintf(fp, "LowSpecMode=%d\n", g_LowSpecMode ? 1 : 0);
    fprintf(fp, "LowSpecFPS=%d\n", g_LowSpecFPS);
    // ── 战斗辅助系统配置 ──
    fprintf(fp, "ShowRadar=%d\n", g_ShowRadar ? 1 : 0);
    fprintf(fp, "RadarRange=%d\n", g_RadarRange);
    fprintf(fp, "RadarPos=%d\n", g_RadarPos);
    fprintf(fp, "RadarTrail=%d\n", g_RadarTrail ? 1 : 0);
    fprintf(fp, "RadarAlpha=%.2f\n", g_RadarAlpha);
    fprintf(fp, "MarkExtraction=%d\n", g_MarkExtraction ? 1 : 0);
    fprintf(fp, "ServerVersion=%d\n", g_IsGL ? 1 : 0);  // 0=国服 1=国际服
    fclose(fp);
}

void LoadGlobalConfig(const char* fn) {
    FILE* fp = nullptr; fopen_s(&fp, fn, "r"); if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0'; std::string key(line);
        if (key == "MaxDist") g_MaxDist = atoi(eq + 1);
        else if (key == "DrawAll") g_DrawAll = atoi(eq + 1) != 0;
        else if (key == "ShowBox") g_ShowBox = atoi(eq + 1) != 0;
        else if (key == "ShowRays") g_ShowRays = atoi(eq + 1) != 0;
        else if (key == "ShowSkeleton") g_ShowSkeleton = atoi(eq + 1) != 0;
        else if (key == "ShowDistance") g_ShowDistance = atoi(eq + 1) != 0;
        else if (key == "DrawAI") g_DrawAI = atoi(eq + 1) != 0;
        else if (key == "ShowAISkeleton") g_ShowAISkeleton = atoi(eq + 1) != 0;
        else if (key == "AIMaxDist") g_AIMaxDist = atoi(eq + 1);
        else if (key == "DrawSelf") g_DrawSelf = atoi(eq + 1) != 0;
        else if (key == "DrawTeammate") g_DrawTeammate = atoi(eq + 1) != 0;
        else if (key == "CheckTeam") g_CheckTeam = atoi(eq + 1) != 0;
        else if (key == "ShowTeamId") g_ShowTeamId = atoi(eq + 1) != 0;
        else if (key == "ShowWeapon") g_ShowWeapon = atoi(eq + 1) != 0;
        else if (key == "ShowName") g_ShowName = atoi(eq + 1) != 0;
        else if (key == "ShowRarity") {
            int vals[7] = {0, 1, 1, 1, 1, 1, 1};
            int n = sscanf_s(eq + 1, "%d,%d,%d,%d,%d,%d,%d",
                &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5], &vals[6]);
            for (int i = 0; i < 7 && i < n; i++) g_ShowRarity[i] = vals[i] != 0;
        }
        else if (key == "ShowArmor") g_ShowArmor = atoi(eq + 1) != 0;
        else if (key == "ShowAmmo") g_ShowAmmo = atoi(eq + 1) != 0;
        else if (key == "ScanItems") g_ScanItems = atoi(eq + 1) != 0;
        else if (key == "ShowNearbyItems") g_ShowNearbyItems = atoi(eq + 1) != 0;
        else if (key == "NearbyItemsDist") g_NearbyItemsDist = atoi(eq + 1);
        else if (key == "colBox") sscanf_s(eq + 1, "%f,%f,%f,%f", &g_colBox[0], &g_colBox[1], &g_colBox[2], &g_colBox[3]);
        else if (key == "colAI") sscanf_s(eq + 1, "%f,%f,%f,%f", &g_colAI[0], &g_colAI[1], &g_colAI[2], &g_colAI[3]);
        else if (key == "colSkeleton") sscanf_s(eq + 1, "%f,%f,%f,%f", &g_colSkeleton[0], &g_colSkeleton[1], &g_colSkeleton[2], &g_colSkeleton[3]);
        else if (key == "colRay") sscanf_s(eq + 1, "%f,%f,%f,%f", &g_colRay[0], &g_colRay[1], &g_colRay[2], &g_colRay[3]);
        else if (key == "colText") sscanf_s(eq + 1, "%f,%f,%f,%f", &g_colText[0], &g_colText[1], &g_colText[2], &g_colText[3]);
        else if (key == "HotkeyVK") g_HotkeyVK = atoi(eq + 1);
        else if (key == "ItemsHotkeyVK") g_ItemsHotkeyVK = atoi(eq + 1);
        else if (key == "FpsLimitEnabled") g_FpsLimitEnabled = atoi(eq + 1) != 0;
        else if (key == "FpsLimit") g_FpsLimit = atoi(eq + 1);
        else if (key == "LowSpecMode") g_LowSpecMode = atoi(eq + 1) != 0;
        else if (key == "LowSpecFPS") g_LowSpecFPS = atoi(eq + 1);
        // ── 战斗辅助系统配置 ──
        else if (key == "ShowRadar") g_ShowRadar = atoi(eq + 1) != 0;
        else if (key == "RadarRange") g_RadarRange = atoi(eq + 1);
        else if (key == "RadarPos") g_RadarPos = atoi(eq + 1);
        else if (key == "RadarTrail") g_RadarTrail = atoi(eq + 1) != 0;
        else if (key == "RadarAlpha") g_RadarAlpha = (float)atof(eq + 1);
        else if (key == "MarkExtraction") g_MarkExtraction = atoi(eq + 1) != 0;
        else if (key == "ServerVersion") {
            int sv = atoi(eq + 1);
            g_IsGL = (sv == 1);
            ACE_CacheTable_RVA = g_IsGL ? ACE_CacheTable_RVA_GL : ACE_CacheTable_RVA_CN;
        }
    }
    fclose(fp);
    ApplyLowSpecProfile();
}
