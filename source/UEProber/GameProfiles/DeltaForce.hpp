#pragma once

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
        return FindNamePoolDataPtr();
    }

    UE_Offsets *GetOffsets() const override
    {
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_25_27(isUsingCasePreservingName());
        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FNamePool.BlocksBit = 18;
            offsets.FNamePool.BlocksOff -= sizeof(void *);
            offsets.TUObjectArray.NumElements = sizeof(int32_t);
            offsets.TUObjectArray.Objects = offsets.TUObjectArray.NumElements + (sizeof(int32_t) * 3);
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

    uintptr_t FindGetPlainANSIString() const
    {
        std::vector<std::pair<std::string, int>> idaPatterns = {
            {"? ? ? 94 ? ? ? 91 ? ? ? 94 ? 06 00 F9 ? 02 40 F9 ? ? ? ? E1 03 00 AA ? ? ? 91 08 85 40 F9", 0},
            {"? ? ? 94 ? 03 00 AA ? ? ? ? ? ? ? ? ? ? ? 94 ? ? ? 91 ? ? ? 94 ? 06 00 F9", 0x10},
            {"? 06 00 F9 ? 02 40 F9 ? ? ? ? E1 03 00 AA ? ? ? 91 08 85 40 F9 E0 03 ? AA", -0xC},
            {"E1 03 00 AA ? ? ? 91 08 85 40 F9 E0 03 ? AA E3 03 15 AA 00 01 3F D6", -0x18},
        };

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        for (const auto &it : idaPatterns)
        {
            uintptr_t bl_addr = findIdaPattern(map_type, it.first, it.second);
            uintptr_t target = DecodeBL(bl_addr);
            if (target != 0 && kPtrValidator.isPtrExecutable(target, sizeof(uint32_t))) return target;
        }

        return 0;
    }

    uintptr_t FindNamePoolDataPtr() const
    {
        static uintptr_t cached = 0;
        if (cached != 0) return cached;

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;
        std::vector<std::pair<std::string, int>> idaPatterns = {
            {"C8 ? ? D0 08 ? ? 91 ? ? ? 39 ? ? ? 36 C9 7E 52 D3 E0 E3 03 91 03 80 80 52 FB E3 03 91 08 0D 09 8B C9 46 7F D3 08 1D 40 F9", 0},
            {"C9 7E 52 D3 E0 E3 03 91 03 80 80 52 FB E3 03 91 08 0D 09 8B C9 46 7F D3 08 1D 40 F9 01 01 09 8B 28 24 40 78 16 FD 46 D3 E2 03 16 AA", -0x10},
            {"03 80 80 52 FB E3 03 91 08 0D 09 8B C9 46 7F D3 08 1D 40 F9 01 01 09 8B 28 24 40 78 16 FD 46 D3 E2 03 16 AA", -0x18},
        };

        for (const auto &it : idaPatterns)
        {
            uintptr_t candidate = Arm64::DecodeADRL(findIdaPattern(map_type, it.first, it.second));
            if (IsValidNamePoolData(candidate))
            {
                cached = candidate;
                return cached;
            }
        }

        cached = FindNamePoolDataFromWrapper(FindGetPlainANSIString());
        return cached;
    }

    uintptr_t FindDecryptFName() const
    {
        static uintptr_t cached = 0;
        if (cached != 0) return cached;

        // Anchors leverage the FName fetch call sequence:
        //   LSR Xn, X8, #6        ; len = header >> 6              (? FD 46 D3)
        //   MOV X2, Xn            ; arg3 = len                     (E2 03 ? AA)
        //   BL  __memcpy_chk      ; copy raw entry to stack buf    (? ? ? ??)
        //   ADD X0, SP, #0xF8     ; arg1 = buf                     (E0 E3 03 91)
        //   MOV W1, Wn            ; arg2 = len                     (E1 03 ? 2A)
        //   BL  DecryptFName      ; <-- decode this target         (? ? ? ??)
        //   ADD X0, SP, #0xF8                                       (E0 E3 03 91)
        //   STRB WZR, [Xn, Xm]    ; null-terminate                 (? ? ? 38)
        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;
        std::vector<std::pair<std::string, int>> idaPatterns = {
            // P1: full LSR→memcpy→DecryptFName window (most distinctive, lowest false-positive risk)
            {"? FD 46 D3 E2 03 ? AA ? ? ? ? E0 E3 03 91 E1 03 ? 2A ? ? ? ?", 0x14},
            // P2: from MOV X2 onward, no LSR anchor
            {"E2 03 ? AA ? ? ? ? E0 E3 03 91 E1 03 ? 2A ? ? ? ?", 0x10},
            // P3: original 5-insn window around BL DecryptFName (BL ... ADD ... MOV ... BL ... ADD ... STRB)
            {"? ? ? ? E0 E3 03 91 E1 03 ? 2A ? ? ? ? E0 E3 03 91 ? ? ? 38", 0xC},
            // P4: same as P3 but with extra downstream context (STR X0,[Xn,#8] + MOV X1,X0)
            {"E0 E3 03 91 E1 03 ? 2A ? ? ? ? E0 E3 03 91 ? ? ? 38 ? ? ? ? ? 06 00 F9 E1 03 00 AA", 0x8},
        };

        for (const auto &it : idaPatterns)
        {
            uintptr_t bl_addr = findIdaPattern(map_type, it.first, it.second);
            uintptr_t target = DecodeBL(bl_addr);
            if (target != 0 && kPtrValidator.isPtrExecutable(target, sizeof(uint32_t)))
            {
                cached = target;
                return cached;
            }
        }

        cached = FindDecryptFromWrapper(FindGetPlainANSIString());
        return cached;
    }

    // FName::ToString anchor: UTimelineTemplate::UpdateCachedNames() opens with
    //   FString TimelineName = GetName();   // GetName() = GetFName().ToString()
    // so the first BL of that function is FName::ToString — which on enc builds
    // IS the decrypt (it inlines the entry copy + decrypt). The function is
    // located by the Printf format literal "%s__Direction_%s" further down.
    // Functional (not log-gated) → survives logging strip; UE4→UE6 stable.
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
    uint8_t *GetNameEntry(int32_t id) const override
    {
        if (id < 0) return nullptr;

        uintptr_t namesPtr = GetNamesPtr();
        if (!IsValidNamePoolData(namesPtr)) return nullptr;

        UE_Offsets *offsets = GetOffsets();
        uintptr_t blockBit = offsets->FNamePool.BlocksBit;
        uintptr_t blocksOff = offsets->FNamePool.BlocksOff;
        uintptr_t chunkMask = (uintptr_t(1) << blockBit) - 1;
        uintptr_t blockOffset = (static_cast<uintptr_t>(id) >> blockBit) * sizeof(void *);
        uintptr_t chunkOffset = (static_cast<uintptr_t>(id) & chunkMask) * offsets->FNamePool.Stride;

        uint8_t *chunk = vm_rpm_ptr<uint8_t *>((void *)(namesPtr + blocksOff + blockOffset));
        if (!kPtrValidator.isPtrReadable(chunk, sizeof(uint16_t))) return nullptr;

        return chunk + chunkOffset;
    }

    std::string GetNameEntryString(uint8_t *entry) const override
    {
        std::string name = IGameProfile::GetNameEntryString(entry);
        if (name.empty()) return "";

        uintptr_t decrypt = FindDecryptFName();
        if (decrypt != 0)
        {
            using DecryptFName_t = void (*)(char *, uint32_t);
            ((DecryptFName_t)decrypt)(name.data(), static_cast<uint32_t>(name.length()));
        }

        return name;
    }

    // Prefer calling FName::ToString directly (it decrypts in one shot). Falls
    // back to the entry-read + in-place DecryptFName path when ToString isn't
    // located. id is the FName ComparisonIndex (pool handle); Number = 0.
    std::string GetNameByID(int32_t id) const override
    {
        if (id < 0) return "";

        uintptr_t toString = FindFNameToString();
        if (!toString)
            return IGameProfile::GetNameByID(id);

        // validate id → readable pool entry before calling into game code
        uint8_t *entry = GetNameEntry(id);
        if (!entry || !kPtrValidator.isPtrReadable(entry, sizeof(uint16_t)))
            return "";

        // FString FName::ToString() const → void(FName* this@X0, FString* sret@X8).
        // FStr has a user-provided dtor → non-trivial → AAPCS64 returns via X8.
        // TCHAR is UTF-16 (char16_t) on UE Android builds — NOT 4-byte wchar_t.
        struct FNameKey { int32_t Comparison; int32_t Number; };
        struct FStr
        {
            char16_t *Data; int32_t Num; int32_t Max;
            ~FStr() {}
        };
        using ToStringFn = FStr (*)(const FNameKey *);

        FNameKey key{ id, 0 };
        FStr out = ((ToStringFn)toString)(&key);
        if (!out.Data || out.Num <= 1) return "";

        std::string result;
        result.reserve(out.Num);
        for (int32_t i = 0; i + 1 < out.Num && i < 1024; i++)  // Num counts the null terminator
        {
            char16_t wc = out.Data[i];
            if (!wc) break;
            result.push_back(static_cast<char>(wc));
        }
        // FString buffer intentionally leaked (one-shot dumper)
        return result;
    }

private:
    static bool IsAddX0Sp(uint32_t insn)
    {
        return (insn & 0xFF0003FF) == 0x910003E0;
    }

    static bool IsMovW1FromReg(uint32_t insn)
    {
        return (insn & 0xFFE0FFFF) == 0x2A0003E1;
    }

    bool IsValidNamePoolData(uintptr_t candidate) const
    {
        if (!kPtrValidator.isPtrReadable(candidate, sizeof(uintptr_t))) return false;

        uintptr_t blocksPtrAddr = candidate + GetOffsets()->FNamePool.BlocksOff;
        if (!kPtrValidator.isPtrReadable(blocksPtrAddr, sizeof(uintptr_t))) return false;

        uint8_t *chunk = vm_rpm_ptr<uint8_t *>((void *)blocksPtrAddr);
        return kPtrValidator.isPtrReadable(chunk, sizeof(uint16_t));
    }

    uintptr_t FindNamePoolDataFromWrapper(uintptr_t wrapper) const
    {
        if (!kPtrValidator.isPtrExecutable(wrapper, sizeof(uint32_t))) return 0;

        for (uintptr_t cursor = wrapper; cursor < wrapper + 0x180; cursor += sizeof(uint32_t))
        {
            uintptr_t candidate = Arm64::DecodeADRL(cursor);
            if (IsValidNamePoolData(candidate)) return candidate;
        }

        return 0;
    }

    uintptr_t FindDecryptFromWrapper(uintptr_t wrapper) const
    {
        if (!kPtrValidator.isPtrExecutable(wrapper, sizeof(uint32_t))) return 0;

        for (uintptr_t cursor = wrapper; cursor < wrapper + 0x180; cursor += sizeof(uint32_t))
        {
            uint32_t insn0 = vm_rpm_ptr<uint32_t>((void *)(cursor));
            uint32_t insn1 = vm_rpm_ptr<uint32_t>((void *)(cursor + 0x4));
            uint32_t insn2 = vm_rpm_ptr<uint32_t>((void *)(cursor + 0x8));
            uint32_t insn3 = vm_rpm_ptr<uint32_t>((void *)(cursor + 0xC));
            uint32_t insn4 = vm_rpm_ptr<uint32_t>((void *)(cursor + 0x10));

            if ((insn0 & 0xFC000000) != 0x94000000) continue;
            if (!IsAddX0Sp(insn1)) continue;
            if (!IsMovW1FromReg(insn2)) continue;
            if ((insn3 & 0xFC000000) != 0x94000000) continue;
            if (!IsAddX0Sp(insn4)) continue;

            uintptr_t target = DecodeBL(cursor + 0xC);
            if (target != 0 && kPtrValidator.isPtrExecutable(target, sizeof(uint32_t)))
                return target;
        }

        return 0;
    }

    // Scan UE .text for the ADRP(+ADD) that materializes `target` (a data addr).
    // Cheap raw-bit ADRP prefilter + page compare, then Arm64::DecodeADRL to
    // confirm the full ADRP+ADD resolves exactly. Returns the ADRP insn addr.
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
