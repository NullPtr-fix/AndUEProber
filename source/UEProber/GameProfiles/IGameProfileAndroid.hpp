#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "UE/UEGameProfile.hpp"
using namespace UEMemory;

class IGameProfileAndroid : public IGameProfile
{
public:
    virtual uintptr_t ReportNameResolverAddr() const
    {
        return FindFNameToString();
    }

    static uintptr_t DecodeBranchTarget(uint32_t insn, uintptr_t pc)
    {
        int32_t imm26 = insn & 0x03FFFFFF;
        if (imm26 & 0x02000000) imm26 |= (int32_t)0xFC000000;
        return pc + (int64_t)imm26 * 4;
    }

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

    uintptr_t FindGUObjectArrayViaFinishDestroy() const
    {
        static uintptr_t cached = 0;
        if (cached) return cached;

        // "Game engine shut down" UTF-16
        static const char *kPat =
            "47 00 61 00 6D 00 65 00 20 00 65 00 6E 00 67 00 69 00 6E 00 65 00 "
            "20 00 73 00 68 00 75 00 74 00 20 00 64 00 6F 00 77 00 6E 00 00 00";
        uintptr_t strAddr = findIdaPattern(PATTERN_MAP_TYPE::ANY_R, kPat, 0);
        if (!strAddr) { LOGE("[FDGObj] \"Game engine shut down\" not found"); return 0; }

        uintptr_t xref = FindAdrpXrefToAddr(strAddr);
        if (!xref) { LOGE("[FDGObj] no ADRP xref to str %p", (void *)strAddr); return 0; }

        // Up-scan to GameEngine::FinishDestroy prologue STP X29,X30,[SP,#imm]
        uintptr_t funcStart = 0;
        for (int i = 0; i < 0x100; i++)
        {
            uintptr_t p = xref - (uintptr_t)i * 4;
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if ((insn & 0xFC407FFF) == 0xA8007BFD) { funcStart = p; break; }  // STP X29,X30,[SP,..]
        }
        if (!funcStart) { LOGE("[FDGObj] no STP prologue above xref %p", (void *)xref); return 0; }

        // Down-scan to Super::FinishDestroy
        uintptr_t target = 0;
        for (uintptr_t p = funcStart; p < funcStart + 0x200; p += 4)
        {
            uint32_t insn = vm_rpm_ptr<uint32_t>((void *)p);
            if (insn == 0xD65F03C0) break;  // RET: end of a BL-style (non-tail-call) function
            if ((insn & 0xFC000000) == 0x14000000)  // B
            {
                uintptr_t t = DecodeBranchTarget(insn, p);
                if (t < funcStart || t > funcStart + 0x400) { target = t; break; }  // far = tail-call
            }
            else if ((insn & 0xFC000000) == 0x94000000)  // BL — Super is the last one before RET
            {
                target = DecodeBranchTarget(insn, p);
            }
        }
        if (!target) { LOGE("[FDGObj] no Super call in func %p", (void *)funcStart); return 0; }

        ElfScanner ue = GetUnrealELF();
        const uintptr_t ueBase = ue.base();
        const uintptr_t ueEnd = ue.end();
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
                    if (arr >= ueBase && arr < ueEnd && kPtrValidator.isPtrReadable(arr))
                    {
                        LOGI("[FDGObj] GUObjectArray=%p (str=+0x%lX func=+0x%lX target=+0x%lX slot=+0x%lX)",
                             (void *)arr, (unsigned long)(strAddr - ueBase), (unsigned long)(funcStart - ueBase),
                             (unsigned long)(target - ueBase), (unsigned long)(slot - ueBase));
                        cached = arr;
                        return arr;
                    }
                }
            }
        }
        LOGE("[FDGObj] no in-module ADRP+LDR global load in target %p", (void *)target);
        return 0;
    }

    uintptr_t FindFNameToString() const
    {
        static uintptr_t cached = 0;
        if (cached) return cached;

        // "%s__Direction_%s" UTF-16
        static const char *kDirPat =
            "25 00 73 00 5F 00 5F 00 44 00 69 00 72 00 65 00 63 00 74 00 69 00 6F 00 6E 00 5F 00 25 00 73 00 00 00";
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
                uintptr_t t = DecodeBranchTarget(insn, p);
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

    virtual bool IsFNamePassedByValue() const
    {
        return false;
    }

    std::string GetNameByID_ViaToString(int32_t id) const
    {
        if (id < 0) return "";

        uintptr_t ToString = FindFNameToString();
        if (!ToString) return "";

        struct FStr { char16_t *Data; int32_t Num; int32_t Max; ~FStr() {} };
        FStr out;
        if (IsFNamePassedByValue())
        {
            using ToStringFn = FStr (*)(uint64_t);  // X0 = FName value {Comparison=id, Number=0}
            out = ((ToStringFn)ToString)((uint64_t)(uint32_t)id);
        }
        else
        {
            struct FNameKey { int32_t Comparison; int32_t Number; };
            using ToStringFn = FStr (*)(const FNameKey *);  // X0 = &FName{id,0}
            FNameKey key{ id, 0 };
            out = ((ToStringFn)ToString)(&key);
        }

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
        return "";
    }
};
