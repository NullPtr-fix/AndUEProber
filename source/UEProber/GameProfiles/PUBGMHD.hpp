#pragma once

#include "UE/UEGameProfile.hpp"
using namespace UEMemory;

// PUBG Mobile HD (com.tencent.tmgp.pubgmhd). Object side is the standard chunked
// GUObjectArray reached via GOT — same finder/offsets as Valorant(codev). Name side
// uses the "%s__Direction_%s" ToString anchor, but PUBG family passes the FName BY
// VALUE in X0 (unlike DFM/Valorant which pass &FName).
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
        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        // Direct ADRP+ADD patterns (work for builds that reference GUObjectArray inline).
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

        for (const auto &it : idaPatterns)
        {
            uintptr_t adrl = Arm64::DecodeADRL(findIdaPattern(map_type, it.first, it.second));
            if (adrl != 0) return adrl;
        }

        // Fallback: this build references GUObjectArray via the GOT, so all the
        // ADRP+ADD patterns above (which end in `... 91`) miss. The Slua bindings
        // expose three tiny GUObjectArray.NumElements accessors (registered in Lua
        // as GetObjectArrayInfo / GetObjectArrayMax / GetObjectArrayNum) that all
        // compile to the same 4-instruction body:
        //
        //     ADRP X8, GOT_PAGE
        //     LDR  X8, [X8, #GOT_OFF]   ; X8 = *(GOT slot) = &GUObjectArray
        //     LDR  W0, [X8, #0x24]      ; FUObjectArray.ObjObjects.NumElements
        //     RET
        //
        // Anchor on the LDR(GOT) + LDR(#0x24) + RET tail, step back to the ADRP,
        // decode ADRP+LDR to get the GOT slot address, then dereference it once
        // to obtain the actual GUObjectArray runtime address.
        // `08 ? ? F9` = LDR X8, [X8, #imm12] (Rn=Rt=X8, 64-bit, any GOT offset).
        // Each `?` is one wildcard byte — KittyScanner's IDA parser does not treat
        // `??` as a single byte, it expands to two wildcard bytes.
        {
            std::string ida_pattern = "08 ? ? F9 00 25 40 B9 C0 03 5F D6";
            const int step = -4;
            uintptr_t got_slot = Arm64::DecodeADRL(findIdaPattern(map_type, ida_pattern, step), 4);
            if (got_slot != 0)
            {
                uintptr_t arr = vm_rpm_ptr<uintptr_t>((void *)got_slot);
                if (arr != 0) return arr;
            }
        }

        return 0;
    }

    uintptr_t GetNamesPtr() const override
    {
        return GetUnrealELF().base();
    }

    UE_Offsets *GetOffsets() const override
    {
        // UE 4.18 — old UProperty reflection model: properties are UObject-derived
        // UProperty on UStruct::Children (no FField/ChildProperties). The UE4_18_19
        // table supplies the right UProperty.* offsets (ArrayDim@0x30 ElementSize@0x34
        // PropertyFlags@0x38 Offset_Internal@0x44 sizeof@0x70) and leaves
        // ChildProperties=0, so the dumper's unified walk skips the FField chain and
        // reads members off Children. The Prober auto-detects the model (a non-CDO
        // *Property *instance* in GObjects ⇒ old UProperty model) and skips the FField
        // Phase5 for this profile.
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_18_19(isUsingCasePreservingName());
        static bool once = false;
        if (!once)
        {
            once = true;
            // Names resolve via FName::ToString (GetNameByID override); the FNamePool
            // config is never consulted, but flag it so the emitted offset dump matches
            // the IsUsingFNamePool() virtual.
            offsets.Config.IsUsingFNamePool = true;
            // Customized FLAT FFixedUObjectArray (runtime-verified): a contiguous
            // FUObjectItem array (stride 0x18, Object@0) at GObjects+0xC8, with
            // NumElements@+0xB8 and MaxElements@+0xC0 (~400000 preallocated).
            // NOT chunked — *(+0xC8) points straight at FUObjectItems, not chunk ptrs.
            offsets.FUObjectArray.ObjObjects = 0;
            offsets.TUObjectArray.Objects = 0xC8;
            offsets.TUObjectArray.NumElements = 0xB8;
            offsets.TUObjectArray.NumElementsPerChunk = 0;  // flat
        }
        return &offsets;
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

    // FName::ToString anchor via UTimelineTemplate::UpdateCachedNames() — see
    // DeltaForce.hpp. First BL of that function (located by the Printf literal
    // "%s__Direction_%s") is GetName()==FName::ToString.
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

        // Fallback: standard FNameEntry read
        return IGameProfile::GetNameByID(id);
    }
};
