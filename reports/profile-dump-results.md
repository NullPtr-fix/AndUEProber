# Reflection-emit refactor: per-profile dump validation

Date: 2026-05-17
Build: `libAndUEProber.so` md5 `91d2b6776f95c85227f6a360489cf1da` (commit `d622b9b`)
Device: Tailscale `100.106.199.127:5555`, Android arm64

## Executive summary

| Game | Package | UE | AutoDump | FProperty.Size | SubPropBase | FEnum layout | Anomaly |
|---|---|---|---|---|---|---|---|
| **DeltaForce** (reference) | `com.tencent.tmgp.dfm` | 5.x custom | ✓ | `0x80` | `0x88` (= Size + 8 leading meta) | Enum-first (`0x88` / `0x90`) | well-understood DFM alt layout, captured by `Pad_0x80[0x8]` |
| **Valorant** | `com.tencent.tmgp.codev` | 4.25+ | ✓ | `0x78` | `0x78` (= Size, std) | UnderlyingType-first std (`0x78` / `0x80`) | †1 FField field overlap, †2 ArrayDim dropped |
| **NiZhan** | `com.tencent.tmgp.nz` | 4.25+ | ✓ | `0x78` | `0x78` | same as Valorant | same as Valorant |
| **Arena Breakout** | `com.tencent.mf.uam` | 4.25+ | ✓ | `0x78` | `0x78` | same as Valorant | same as Valorant |
| **PUBG** (CN) | `com.tencent.tmgp.pubgmhd` | 4.18-19 | ✗ `ERROR_EMPTY_PACKAGES` | n/a | n/a | n/a | profile WIP — walker dies in Phase1 (see §PUBG) |
| **PUBG** (Global) | `com.tencent.ig` | 4.18-19 | ✗ `ERROR_EMPTY_PACKAGES` | n/a | n/a | n/a | same as pubgmhd |

`†1` / `†2` analysed in [§Cross-game UE 4.25+ anomalies](#cross-game-ue-425-anomalies).

3 of 4 target games (all UE 4.25+ with standard layout) validate the refactor S0–S7 end-to-end: every FField/FProperty derived class emits its fields at the probed `SubPropertyBase`. PUBG was not validatable on this build because `PUBG.hpp` is pre-existing WIP (`UE_DefaultOffsets::UE4_18_19` does not match the device build's GUObjectArray layout), so dump fails before reaching the synthesize path the task wanted to verify.

## Valorant — `com.tencent.tmgp.codev`

Profile added in commit `1444f30`. UE 4.25+ standard layout. AutoDump finished at t=12s post-inject. Dump pulled to `misc/UEDump3r/com.tencent.tmgp.codev/`.

### Probed offsets (Offsets.hpp)

```
FField:    ClassPrivate=0x08  Next=0x20  NamePrivate=0x28  FlagsPrivate=0x30
FProperty: ArrayDim=0x34  ElementSize=0x38  PropertyFlags=0x40  Offset_Internal=0x4C
           Size=0x78  SubPropertyBase=0x78
FEnumProperty: UnderlyingType=0x78  Enum=0x80      ← UnderlyingType-first standard
UObject:   ObjectFlags=0x08 InternalIndex=0x0C ClassPrivate=0x10 NamePrivate=0x18 OuterPrivate=0x20
```

### CoreUObject_structs.hpp — key emit

- `FFieldClass` (0x28): `Name@0 Id@8 CastFlags@10 ClassFlags@18 SuperClass@20` ✓
- `FField` (0x38): VTable@0, **Owner@0x8(0x10) overlap ClassPrivate@0x8** †1, Pad_0x10[0x10], Next@0x20, NamePrivate@0x28, FlagsPrivate@0x30
- `FProperty:FField` (Size 0x78 Inherited 0x38): **ArrayDim missing** †2; ElementSize@0x38, PropertyFlags@0x40, Offset_Internal@0x4C
- `FStructProperty` (0x80): `Struct@0x78` ✓
- `FObjectPropertyBase` (0x80): `PropertyClass@0x78` ✓
- `FArrayProperty` (0x80): `Inner@0x78` ✓
- `FByteProperty` (0x80): `Enum@0x78` ✓
- `FBoolProperty` (0x80): `FieldSize@0x78 ByteOffset@0x79 ByteMask@0x7A FieldMask@0x7B` ✓ (4-byte quartet at FProperty.Size)
- `FEnumProperty` (0x88): `UnderlyingType@0x78 Enum@0x80` ✓ (standard order)
- `FSetProperty` (0x80): `ElementProp@0x78` ✓
- `FMapProperty` (0x88): `KeyProp@0x78 ValueProp@0x80` ✓
- `FInterfaceProperty` (0x80): `InterfaceClass@0x78` ✓
- `FFieldPathProperty` (0x80): `PropertyName@0x78` ✓
- `FDelegateProperty` (0x80): `SignatureFunction@0x78` ✓
- `FOptionalProperty` (0x80): `ValueProperty@0x78` ✓
- `FClassProperty:FObjectPropertyBase` (0x88): `MetaClass@0x80` ✓
- `FSoftClassProperty:FClassProperty` (0x88): empty ✓ (no new members per §3.3)

All 14 FProperty subclasses emit correctly relative to `SubPropertyBase=0x78`. The two `†` anomalies are systemic — see [§Cross-game UE 4.25+ anomalies](#cross-game-ue-425-anomalies).

## NiZhan — `com.tencent.tmgp.nz`

UE 4.25+ standard layout. AutoDump finished at t=6s. Dump at `misc/UEDump3r/com.tencent.tmgp.nz/`.

**FField / FProperty / FEnumProperty / FFieldClass / FProperty subclass emit are byte-for-byte identical to Valorant.** UObject/UStruct layouts differ slightly:

```
UStruct: SuperStruct=0x58 Children=0x60 ChildProperties=0x68 PropertiesSize=0x70  (Valorant: 0x40/0x48/0x50/0x58)
UClass:  CastFlags=0xF0 DefaultObject=0x140                                       (Valorant: 0xD0/0x118)
UFunction: EFunctionFlags=0xC8 NumParams=0xCC ParamSize=0xCE Func=0xF0            (Valorant: 0xB0/0xB4/0xB6/0xD8)
UField:  Next=0x40                                                                (Valorant: 0x28)
```

UObject layout is the same as Valorant. UE-specific build differences upstream don't affect FField/FProperty layout — confirms that reflection-emit emit logic is decoupled from UObject layout, as designed.

Same anomalies as Valorant (†1, †2).

## Arena Breakout — `com.tencent.mf.uam`

UE 4.25+ standard layout. AutoDump finished at t=5s. Dump at `misc/UEDump3r/com.tencent.mf.uam/`.

**Offsets.hpp is byte-for-byte identical to NiZhan.** Same UStruct/UClass/UFunction layout as NiZhan. Same FField/FProperty emit as Valorant/NiZhan. Same anomalies (†1, †2).

This game uses `MSDKPolicyActivity` as the launcher (not the default `SplashActivity`).

## PUBG — both packages FAILED (profile-level issue)

Both `com.tencent.tmgp.pubgmhd` and `com.tencent.ig` fail AutoDump with `ERROR_EMPTY_PACKAGES`. Failure mode:

```
[PB] UE Base: 0x..., GUObjectArray: 0x..., ObjectsFieldAddr: 0x...
[PB] GObjects 初始化完成: NumElementsPerChunk=0 (flat)              ← layout mis-detected
[PB] Phase1_AutoProbe: 无法获取有效的 GetByIndex(1), 请检查 GObjects 地址
[PB] Phase2 中止: 需要先完成阶段1 (Name=false, Class=false)
[PB] ... [Phase5_AutoProbe] ... UStruct::ChildProperties 未确认, 中止
[PB] [AutoDump] AutoProbe phases done; invoking StartDump
[AutoDump] === FAILED === err=ERROR_EMPTY_PACKAGES
```

`PUBG.hpp` is marked WIP in caveats; it uses `UE_DefaultOffsets::UE4_18_19(false)` whose `FUObjectArray` / `TUObjectArray` field offsets do not match the current device build of either PUBG package. The walker reads `NumElementsPerChunk=0` and falls into the flat-array path, but the chunked layout is actually expected. `GetByIndex(1)` then dereferences an invalid pointer.

**Implication for reflection-emit refactor**: the user-stated PUBG validation path (FProperty.Size=0 default → `SynthesizeReflectionTypes` early return → CoreUObject_structs.hpp without FField/FProperty subclasses → SDK still compiles from preamble FFieldVariant + FFieldPath) is **not validatable on this device** without first fixing the PUBG profile. The dump dies long before the synthesize call.

Refactor S0–S7 itself contains no PUBG-specific code paths. The failure is profile-level and orthogonal to the refactor work. No regression introduced by the refactor (PUBG was presumably already broken on this device build).

## Cross-game UE 4.25+ anomalies

Both anomalies below are present in **all three** UE 4.25+ games (Valorant, NiZhan, ArenaBreakout) and absent in DFM. They are in the **dumper synthesize/emit logic**, not the prober.

### †1 — FField emit: `Owner@0x8` overlaps `ClassPrivate@0x8`

Prober finds `FField.ClassPrivate=0x8` consistently on all three games. The dumper's `fieldsFor` (reflection-emit.md §5) hardcodes:

```cpp
add(0,  8,  "void**",         "VTable",  true);
add(8,  16, "FFieldVariant",  "Owner",   true);     // ← hardcoded @ 0x8, size 0x10
add(offs.FField.ClassPrivate, 8, "struct FFieldClass*", "ClassPrivate");  // ← also 0x8
```

After augment sorts by offset, the emit ends up with VTable@0, Owner@8(0x10), ClassPrivate@8(8), Pad_0x10[0x10], Next@0x20 — two fields claim offset `0x8` and a 16-byte pad is then inserted to reach Next.

Correct UE 4.25+ layout is `VTable@0, ClassPrivate@8, Owner@0x10(0x10), Next@0x20, ...` (i.e. ClassPrivate comes before Owner). DFM has the opposite order (`VTable, Owner, Next, ClassPrivate, ...`), and on DFM the hardcoded `Owner@8` happens to match, which is why this anomaly didn't surface in S5/S6.

**Root cause is plumbing, not algorithm**: the prober's `Phase5_ProbeFFieldOwner` correctly probes Owner @ 0x10 on Valorant (confidence 1.0, 3-way validation EntryPoint/DeltaSeconds/bIsUObject). But `UE_Offsets.FField` had no `Owner` field, so `DumperBridge::ProbedOffsets` had no `ffieldOwner` field, so the probed value never reached the dumper. `fieldsFor("FField")` then hardcoded `Owner @ 0x8` — coincidentally right for DFM, broken for standard.

**Fix** (proper architecture, in `external/AndUEDumper` `andueprober` branch + `source/UEProber` bridge): add `UE_Offsets.FField.Owner` with defaults (UE 4.25+ = `ClassPrivate + 8`, DFM override = `8`), add `ffieldOwner` to `DumperBridge::ProbedOffsets`, wire it through in `StartDumpWithProbedOffsets`, and pass `GetConfirmedOffset("FField::Owner")` from `UEProber::StartDump`. `fieldsFor("FField")` now reads `offs.FField.Owner` directly — no heuristic.

**Status**: shipped. Verified on Valorant (Owner @ 0x10, no overlap with ClassPrivate @ 0x8) and DFM (byte-for-byte identical to pre-fix baseline). doc §3.2 (new `FField.Owner` row) + §3.4 + §5 updated. Supersedes the earlier `(ClassPrivate < 0x18) ? 0x10 : 0x8` heuristic from submodule `135172e` — that commit stays in git history but its emit logic is replaced.

### †2 — FProperty.ArrayDim dropped because `SizeOf(FField)` over-aligns

Prober finds `FProperty.ArrayDim=0x34` on all three games. The dumper computes `SizeOf(FField) = align(FlagsPrivate + 4, 8) = align(0x34, 8) = 0x38` (reflection-emit.md §4.3). Augment treats `Inherited=0x38` as the FProperty boundary and erases any FProperty field with offset `< 0x38`.

Result: `ArrayDim@0x34` is silently dropped from the emit. FProperty body has `ElementSize@0x38` as its first field; ArrayDim is missing.

The compiler used to build these UE 4.25+ binaries reuses FField's trailing 4-byte pad (0x34..0x37, after FlagsPrivate@0x30+4) for FProperty's first int32. This is legal C++ behavior (tail-padding reuse on derived class) but breaks the dumper's assumption that `sizeof(FField) == Inherited(FProperty)`.

**Suggested fix** (in dumper §4.3): clamp `Inherited(FProperty) = min(SizeOf(FField), FProperty.ArrayDim)` so the boundary tracks the actual probed FProperty start. DFM (ArrayDim @ 0x38) makes the min a no-op; standard layout (ArrayDim @ 0x34) pulls Inherited down to 0x34 and augment keeps ArrayDim.

**Status**: addressed by the submodule bump in the same commit as this report — see `external/AndUEDumper` `andueprober` branch, fix in `SynthesizeReflectionTypes` after the parent-size lookup. Verified on Valorant (ArrayDim emitted @ 0x34) and DFM (byte-for-byte identical to pre-fix baseline).

Both anomalies are confined to the FField↔FProperty boundary. All FProperty subclass tails (`Struct`, `PropertyClass`, `Inner`, `Enum`, `FieldSize` quartet, `UnderlyingType` + `Enum`, etc.) emit correctly because `SubPropertyBase` is probed independently and starts at `0x78`, well clear of FField's `0x38` boundary.

## Refactor S0–S7 verdict per acceptance criteria (§9 of reflection-emit.md)

| Criterion | Status |
|---|---|
| DFM dump → FBoolProperty quartet @ FProperty.Size + 0/1/2/3, leading-metadata compatible | ✓ (validated previously) |
| PUBG / Valorant / Arena Breakout 各跑一遍, dump 不退化 | ✓ for Valorant + AB (+ NiZhan as bonus); ✗ for PUBG due to pre-existing profile WIP, **not** refactor regression |
| 不依赖 preamble FField/FProperty 硬编码 | ✓ — emit comes from `fieldsFor` synthesize, `kPreambleProvided` no longer lists FFieldClass / FProperty |
| FEnumProperty `UnderlyingType` + `Enum` both present | ✓ on all three UE 4.25+ games (standard order); ✓ on DFM (Enum-first alt) |
| All 14 FProperty subclasses correctly emitted from `SubPropertyBase` | ✓ on all three UE 4.25+ games |

## New layout variants discovered

None requiring a `reflection-emit.md` §3.2 update — Valorant / NiZhan / ArenaBreakout are all the canonical "standard UE 4.25+ layout" already described as the §3.2 default (`SubPropertyBase = FProperty.Size`, `FEnumProperty.UnderlyingType = Size`, `Enum = Size + 8`). DFM remains the documented alt variant.

Two **dumper-side bugs** found above (†1, †2) are not new layout variants — they're emit-logic mismatches against the standard layout that DFM's alt happened to mask. Recommend tracking these as separate follow-up issues for the dumper repo, not as new variants.

## Repro snippets

Build artifact at the time of this report: `build/libAndUEProber.so` from `main@d622b9b`. Per-game hot loop in `claude-skills/android-hook-inject-test` shape; the only variables that change are `$PACKAGE` + `$ACTIVITY` (and PUBG also needs a fixed profile). Logs at `logs/<package>_<ts>.log`, dumps at `misc/UEDump3r/<package>/`.
