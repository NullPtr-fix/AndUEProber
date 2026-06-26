#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "UE/UEGameProfile.hpp"
using namespace UEMemory;

class DeltaForceProfile : public IGameProfile
{
public:
    DeltaForceProfile() = default;

    bool ArchSupprted() const override
    {
        auto e_machine = GetUnrealELF().header().e_machine;
        return e_machine == EM_AARCH64;
    }

    std::string GetAppName() const override
    {
        return "Delta Force";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.proxima.dfm", "com.garena.game.df", "com.tencent.tmgp.dfm"};
    }

    bool isUsingCasePreservingName() const override
    {
        return false;
    }

    bool IsUsingFNamePool() const override
    {
        return true;
    }

    bool isUsingOutlineNumberName() const override
    {
        return false;
    }

    uintptr_t GetGUObjectArrayPtr() const override
    {
        // P1: GObjects.GetObjectByIndex 路径——FUObjectArray 用 0x10000 作为 NumElementsPerChunk，
        //     先用 SDIV/MSUB 把 index 拆成 chunk_idx / within_chunk_idx，再 ADRP+ADD 取 GObjects：
        //         MOV  W?, #0x10000        ; ?? 00 A0 52
        //         MOV  W?, #0x10000        ; ?? 00 A0 52
        //         SDIV W?, W?, W?          ; ?? ?? ?? 1A
        //         MSUB W?, W?, W?, W?      ; ?? ?? ?? 1B
        //         ADRP X?, GObjects        ; ?? ?? ?? ??   <-- decode
        //         ADD  X?, X?, #imm12      ; ?? ?? ?? 91
        //
        // P2: FreeUObjectIndex 调用点——清 pendingKill 前后的固定模板：
        //         LDRB W8, [X19, #8]       ; 68 22 40 39
        //         CBZ  W8, ?               ; ?? ?? ?? 34
        //         ADRP X0, GObjects        ; ?? ?? ?? ??   <-- decode
        //         ADD  X0, X0, #imm12      ; ?? ?? ?? 91
        //         MOV  X1, X19             ; E1 03 13 AA
        //         STRB WZR, [X19, #8]      ; 7F 22 00 39
        std::vector<std::pair<std::string, int>> idaPatterns = {
            {"? 00 A0 52 ? 00 A0 52 ? ? ? 1A ? ? ? 1B ? ? ? ? ? ? ? 91", 0x10},
            {"68 22 40 39 ? ? ? 34 ? ? ? ? ? ? ? 91 E1 03 13 AA 7F 22 00 39", 0x8},
            {"91 E1 03 ? AA E0 03 08 AA E2 03 1F 2A", -7},
            {"B4 21 0C 40 B9 ? ? ? ? ? ? ? 91", 5},
            {"9F E5 00 ? 00 E3 FF ? 40 E3 ? ? A0 E1", -2},
            {"96 df 02 17 ? ? ? ? 54 ? ? ? ? ? ? ? 91 e1 03 13 aa", 9},
            {"f4 03 01 2a ? 00 00 34 ? ? ? ? ? ? ? ? ? ? 00 54 ? 00 00 14 ? ? ? ? ? ? ? 91", 0x18},
            {"69 3e 40 b9 1f 01 09 6b ? ? ? 54 e1 03 13 aa ? ? ? ? f4 4f ? a9 ? ? ? ? ? ? ? 91", 0x18},
        };

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        for (const auto &it : idaPatterns)
        {
            uintptr_t adrl = Arm64::DecodeADRL(findIdaPattern(map_type, it.first, it.second));
            if (adrl != 0 && kPtrValidator.isPtrReadable(adrl)) return adrl;
        }

        return 0;
    }

    uintptr_t GetNamesPtr() const override
    {
        return GetUnrealELF().base();
    }

    UE_Offsets *GetOffsets() const override
    {
        struct FChunkedFixedUObjectArray // = ObjObjects (reordered)
        {
            struct FUObjectItem;    // 0x18 item, Object@0 (only ever pointed-to)
            int32_t MaxChunks;      // 0x00  (=7)
            int32_t NumElements;    // 0x04  objects in use
            int32_t NumChunks;      // 0x08  (=2 live)
            int32_t _pad;           // 0x0C
            FUObjectItem** Objects; // 0x10  chunk table (separate alloc)
            uint8_t _lock[0x10];    // 0x18  ObjObjectsCritical / PreAllocatedObjects (0)
            int32_t MaxElements;    // 0x28 (abs 0x38)  458752 (= MaxChunks × PerChunk)
        };
        struct FUObjectArray
        {
            typedef FChunkedFixedUObjectArray TUObjectArray;
            uint8_t _gc[0x10];         // 0x00  ObjFirst/LastGCIndex, MaxObjectsNotConsideredByGC, OpenForDisregardForGC
            TUObjectArray ObjObjects;  // 0x10
        };

        static UE_Offsets offsets = UE_DefaultOffsets::UE4_25_27(isUsingCasePreservingName());
        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FNamePool.BlocksBit = 18; // never use
            offsets.FNamePool.BlocksOff -= sizeof(void *); // never use
            offsets.FUObjectArray.ObjObjects = offsetof(FUObjectArray, ObjObjects);
            offsets.TUObjectArray.Objects = offsetof(FChunkedFixedUObjectArray, Objects);
            offsets.TUObjectArray.NumElements = offsetof(FChunkedFixedUObjectArray, NumElements);
        }
        return &offsets;
    }

    static uintptr_t DecodeBL(uintptr_t bl_address)
    {
        if (!bl_address) return 0;
        uint32_t insn = *reinterpret_cast<uint32_t*>(bl_address);
        if ((insn & 0xFC000000) != 0x94000000) return 0;
        int32_t imm26 = insn & 0x03FFFFFF;
        if (imm26 & 0x02000000) imm26 |= (int32_t)0xFC000000;
        return bl_address + (int64_t)imm26 * 4;
    }

    uintptr_t FindFNameToString() const
    {
        static uintptr_t cached = 0;
        if (cached) return cached;

        // UTF-16LE "%s__Direction_%s"
        static const char *kDirPat =
            "25 00 73 00 5F 00 5F 00 44 00 69 00 72 00 65 00 63 00 74 00 69 00 6F 00 6E 00 5F 00 25 00 73 00";
        uintptr_t strAddr = findIdaPattern(PATTERN_MAP_TYPE::ANY_R, kDirPat, 0);
        if (!strAddr) { LOGE("[ToStringDecrypt] \"%%s__Direction_%%s\" not found"); return 0; }

        uintptr_t xref = FindAdrpXrefToAddr(strAddr);
        if (!xref) { LOGE("[ToStringDecrypt] no ADRP xref to str %p", (void *)strAddr); return 0; }

        // walk up to the prologue: SUB SP, SP, #imm
        uintptr_t funcStart = 0;
        for (int i = 0; i < 0x200; i++)
        {
            uintptr_t p = xref - (uintptr_t)i * 4;
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if ((insn & 0xFF8003FF) == 0xD10003FF) { funcStart = p; break; }  // SUB SP, SP, #imm
        }
        if (!funcStart) return 0;

        // first BL after prologue = GetName() == FName::ToString (thunk ok — it tail-calls)
        for (uintptr_t p = funcStart; p < funcStart + 0x100; p += 4)
        {
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if ((insn & 0xFC000000) == 0x94000000)  // BL
            {
                uintptr_t t = DecodeBL(p);
                if (t && kPtrValidator.isPtrExecutable(t, sizeof(uint32_t)))
                {
                    cached = t;
                    uintptr_t base = GetUnrealELF().base();
                    LOGI("[ToStringDecrypt] FName::ToString @ %p (+0x%lX) str=+0x%lX func=+0x%lX",
                         (void *)t, (unsigned long)(t - base),
                         (unsigned long)(strAddr - base), (unsigned long)(funcStart - base));
                    return t;
                }
            }
        }
        LOGE("[ToStringDecrypt] no BL in prologue window @ func=%p", (void *)funcStart);
        return 0;
    }

protected:
    std::string GetNameByID(int32_t id) const override
    {
        if (id < 0) return "";

        // Primary: FName::ToString — PUBG family passes the FName BY VALUE in X0
        // (the getter copies it to a stack slot internally), unlike DFM/Valorant
        // which stage &FName and pass a pointer. FString returned via X8 sret.
        if (uintptr_t toString = FindFNameToString())
        {
            struct FStr { char16_t *Data; int32_t Num; int32_t Max; ~FStr() {} };
            using ToStringFn = FStr (*)(uint64_t);  // X0 = FName value {Comparison=id, Number=0}
            FStr out = ((ToStringFn)toString)((uint64_t)(uint32_t)id);
            if (out.Data && out.Num > 1)
            {
                std::string result;
                result.reserve(out.Num);
                for (int32_t i = 0; i + 1 < out.Num && i < 1024; i++)
                {
                    char16_t wc = out.Data[i];
                    if (!wc) break;
                    result.push_back(static_cast<char>(wc));
                }
                return result;
            }
        }

        return IGameProfile::GetNameByID(id);
    }

private:
    uintptr_t FindAdrpXrefToAddr(uintptr_t target) const
    {
        if (!target) return 0;
        const uintptr_t targetPage = target & ~uintptr_t(0xFFF);

        ElfScanner ue = GetUnrealELF();
        const size_t kChunkInsns = 0x40000;  // 1 MiB / read
        std::vector<uint32_t> buf(kChunkInsns);

        for (auto &seg : ue.segments())
        {
            if (!seg.readable || !seg.is_private) continue;
            if (!isEmulator() && !seg.executable) continue;

            for (uintptr_t base = seg.startAddress; base < seg.endAddress; base += kChunkInsns * 4)
            {
                size_t remain = (seg.endAddress - base) / 4;
                size_t n = remain < kChunkInsns ? remain : kChunkInsns;
                if (!vm_rpm_ptr((void *)base, buf.data(), n * sizeof(uint32_t)))
                    continue;

                for (size_t i = 0; i < n; i++)
                {
                    uint32_t insn = buf[i];
                    if ((insn & 0x9F000000) != 0x90000000) continue;  // ADRP

                    int64_t imm = (int64_t)((((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 0x3));
                    imm = (imm << 43) >> 43;  // sign-extend 21-bit
                    imm <<= 12;

                    uintptr_t pc = base + i * 4;
                    if (((pc & ~uintptr_t(0xFFF)) + (uintptr_t)imm) != targetPage) continue;

                    if (Arm64::DecodeADRL(pc) == target) return pc;
                }
            }
        }
        return 0;
    }
};
