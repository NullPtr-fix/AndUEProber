#pragma once

#include <cstddef>
#include <cstdint>

#include "UE/UEGameProfile.hpp"
using namespace UEMemory;

class PUBGMHDProfile : public IGameProfile
{
public:
    PUBGMHDProfile() = default;

    bool ArchSupprted() const override
    {
        auto e_machine = GetUnrealELF().header().e_machine;
        return e_machine == EM_AARCH64;
    }

    std::string GetAppName() const override
    {
        return "PUBGMHD";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.tencent.tmgp.pubgmhd"};
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
        return FindGUObjectArrayViaFinishDestroy();
    }

    uintptr_t GetNamesPtr() const override
    {
        return GetUnrealELF().base();
    }

    UE_Offsets *GetOffsets() const override
    {
        struct FFixedUObjectArray   // = ObjObjects (single inline chunk, flat-read)
        {
            struct FUObjectItem;    // 0x18 item, Object@0 (only ever pointed-to)
            int32_t NumElements;    // 0x00 (abs 0xB8)  objects in use (use this, not +0x04 = Num-1)
            int32_t _lastIndex;     // 0x04 (abs 0xBC)  == NumElements-1
            int32_t MaxElements;    // 0x08 (abs 0xC0)  single-chunk capacity (~400000)
            int32_t _pad;           // 0x0C (abs 0xC4)
            FUObjectItem* Objects;  // 0x10 (abs 0xC8)  inline chunk[0] -> FUObjectItem[] (stride 0x18, Object@0)
        };
        struct FUObjectArray
        {
            typedef FFixedUObjectArray TUObjectArray;
            uint8_t _front[0xB8];     // 0x00  reordered: GC indices + listener TArrays + locks
            TUObjectArray ObjObjects; // 0xB8
        };

        static UE_Offsets offsets = UE_DefaultOffsets::UE4_18_19(isUsingCasePreservingName());
        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.Config.IsUsingFNamePool = true; // never use
            offsets.FUObjectArray.ObjObjects = offsetof(FUObjectArray, ObjObjects);
            offsets.TUObjectArray.Objects = offsetof(FFixedUObjectArray, Objects);
            offsets.TUObjectArray.NumElements = offsetof(FFixedUObjectArray, NumElements);
            offsets.TUObjectArray.NumElementsPerChunk = 0;  // flat read of the single inline chunk
        }
        return &offsets;
    }

    uintptr_t FindGUObjectArrayViaFinishDestroy() const
    {
        static uintptr_t cached = 0;
        static bool tried = false;
        if (tried) return cached;
        tried = true;

        // "Game engine shut down" UTF-16LE
        static const char *kPat =
            "47 00 61 00 6D 00 65 00 20 00 65 00 6E 00 67 00 69 00 6E 00 65 00 "
            "20 00 73 00 68 00 75 00 74 00 20 00 64 00 6F 00 77 00 6E 00";
        uintptr_t strAddr = findIdaPattern(PATTERN_MAP_TYPE::ANY_R, kPat, 0);
        if (!strAddr) { LOGE("[FDGObj] \"Game engine shut down\" not found"); return 0; }

        uintptr_t xref = FindAdrpXrefToAddr(strAddr);
        if (!xref) { LOGE("[FDGObj] no ADRP xref to str %p", (void *)strAddr); return 0; }

        // Scan UP to the UGameEngine::FinishDestroy prologue: STP X29,X30,[SP,#imm]
        // (any addressing mode — pre-index/signed-offset). A frameless log helper
        // (WuWa) has no such STP, so the up-scan crosses it onto FinishDestroy.
        uintptr_t funcStart = 0;
        for (int i = 0; i < 0x100; i++)
        {
            uintptr_t p = xref - (uintptr_t)i * 4;
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if ((insn & 0xFC407FFF) == 0xA8007BFD) { funcStart = p; break; }  // STP X29,X30,[SP,..]
        }
        if (!funcStart) { LOGE("[FDGObj] no STP prologue above xref %p", (void *)xref); return 0; }

        // Scan DOWN to the Super::FinishDestroy call. UE4.25+ tail-calls it via a
        // far B (terminal). UE4.18 (pubgmhd) calls via BL then epilogue+RET, so the
        // call is the last BL before RET. Local CDO-skip/merge jumps (near B) ignored.
        auto decodeBr = [](uintptr_t pc, uint32_t insn) -> uintptr_t {
            int32_t imm26 = insn & 0x03FFFFFF;
            if (imm26 & 0x02000000) imm26 |= (int32_t)0xFC000000;
            return pc + (int64_t)imm26 * 4;
        };
        uintptr_t target = 0;
        for (uintptr_t p = funcStart; p < funcStart + 0x200; p += 4)
        {
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if (insn == 0xD65F03C0) break;  // RET: end of a BL-style (non-tail-call) function
            if ((insn & 0xFC000000) == 0x14000000)  // B
            {
                uintptr_t t = decodeBr(p, insn);
                if (t < funcStart || t > funcStart + 0x400) { target = t; break; }  // far = tail-call (terminal)
            }
            else if ((insn & 0xFC000000) == 0x94000000)  // BL — Super is the last one before RET
            {
                target = decodeBr(p, insn);
            }
        }
        if (!target) { LOGE("[FDGObj] no Super call in func %p", (void *)funcStart); return 0; }

        // Guard: only scan executable code for the GUObjectArray load. A mis-identified
        // Super call into data (UE4.18 shutdown path differs from 4.25+) bails cleanly
        // to the existing fallback instead of decoding garbage from a data island.
        if (!kPtrValidator.isPtrExecutable(target, sizeof(uint32_t)))
        {
            LOGE("[FDGObj] Super-call target %p not executable, bail", (void *)target);
            return 0;
        }

        // In UEngine::FinishDestroy: first ADRP + its paired LDR Xd,[Xd,#imm12].
        // ADRP and LDR may be separated by other insns (scheduling), so search the
        // LDR by matching base register within a small window. Slot holds &GUObjectArray.
        for (uintptr_t p = target; p < target + 0x80; p += 4)
        {
            uint32_t a = vm_rpm_ptr<uint32_t>((void *)p);
            if ((a & 0x9F000000) != 0x90000000) continue;  // ADRP
            uint32_t rd = a & 0x1F;
            int64_t imm = (int64_t)((((a >> 5) & 0x7FFFF) << 2) | ((a >> 29) & 0x3));
            imm = (imm << 43) >> 43;
            imm <<= 12;
            uintptr_t page = (p & ~uintptr_t(0xFFF)) + (uintptr_t)imm;
            for (uintptr_t q = p + 4; q < p + 0x20; q += 4)
            {
                uint32_t l = vm_rpm_ptr<uint32_t>((void *)q);
                if ((l & 0xFFC00000) == 0xF9400000 && ((l >> 5) & 0x1F) == rd)  // LDR Xt,[Xd,#imm12]
                {
                    uintptr_t slot = page + (uintptr_t)((l >> 10) & 0xFFF) * 8;
                    uintptr_t arr = vm_rpm_ptr<uintptr_t>((void *)slot);
                    if (arr && kPtrValidator.isPtrReadable(arr))
                    {
                        uintptr_t base = GetUnrealELF().base();
                        LOGI("[FDGObj] GUObjectArray=%p (str=+0x%lX func=+0x%lX target=+0x%lX slot=+0x%lX)",
                             (void *)arr, (unsigned long)(strAddr - base), (unsigned long)(funcStart - base),
                             (unsigned long)(target - base), (unsigned long)(slot - base));
                        cached = arr;
                        return arr;
                    }
                }
            }
        }
        LOGE("[FDGObj] no ADRP+LDR global load in target %p", (void *)target);
        return 0;
    }

    // Decode ARM64 BL instruction at given address, return absolute target
    static uintptr_t DecodeBL(uintptr_t bl_address)
    {
        if (!bl_address) return 0;
        uint32_t insn = *reinterpret_cast<uint32_t*>(bl_address);
        // BL: opcode[31:26] == 100101
        if ((insn & 0xFC000000) != 0x94000000) return 0;
        // imm26, sign-extended, shifted left 2
        int32_t imm26 = insn & 0x03FFFFFF;
        if (imm26 & 0x02000000) imm26 |= (int32_t)0xFC000000; // sign extend
        return bl_address + (int64_t)imm26 * 4;
    }

    uintptr_t FindFNameToString() const
    {
        static uintptr_t cached = 0;
        if (cached) return cached;

        static const char *kDirPat =
            "25 00 73 00 5F 00 5F 00 44 00 69 00 72 00 65 00 63 00 74 00 69 00 6F 00 6E 00 5F 00 25 00 73 00";
        uintptr_t strAddr = findIdaPattern(PATTERN_MAP_TYPE::ANY_R, kDirPat, 0);
        if (!strAddr) { LOGE("[ToStringDecrypt] \"%%s__Direction_%%s\" not found"); return 0; }

        uintptr_t xref = FindAdrpXrefToAddr(strAddr);
        if (!xref) { LOGE("[ToStringDecrypt] no ADRP xref to str %p", (void *)strAddr); return 0; }

        uintptr_t funcStart = 0;
        for (int i = 0; i < 0x200; i++)
        {
            uintptr_t p = xref - (uintptr_t)i * 4;
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if ((insn & 0xFF8003FF) == 0xD10003FF) { funcStart = p; break; }  // SUB SP, SP, #imm
        }
        if (!funcStart) return 0;

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

    uintptr_t FindAdrpXrefToAddr(uintptr_t target) const
    {
        if (!target) return 0;
        const uintptr_t targetPage = target & ~uintptr_t(0xFFF);

        ElfScanner ue = GetUnrealELF();
        const size_t kChunkInsns = 0x40000;
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
                    imm = (imm << 43) >> 43;
                    imm <<= 12;

                    uintptr_t pc = base + i * 4;
                    if (((pc & ~uintptr_t(0xFFF)) + (uintptr_t)imm) != targetPage) continue;
                    if (Arm64::DecodeADRL(pc) == target) return pc;
                }
            }
        }
        return 0;
    }

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
};
