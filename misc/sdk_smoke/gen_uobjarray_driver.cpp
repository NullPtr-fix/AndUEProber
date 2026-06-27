// Standalone check: emit UEAssert.h + GenUObjectArray() for a layout, wrapped in a
// harness mirroring real SDK consumption (UE check/checkf/ensure + pointer-to-live
// GUObjectArray + UE-idiom IndexToObject/GetObjectArrayNum/ForEachObject call sites).
// Usage: gen_driver <itemStride> <objectOffset> <numElementsPerChunk>
//        [objObjectsOff objectsOff maxElemOff numElemOff maxChunksOff numChunksOff]
// The optional 6 offsets exercise the live-overlay path (reordered FUObjectArray);
// omit them for the canonical layout. The emitted offsetof static_asserts fail the
// compile if the padded struct doesn't land its fields at the requested offsets.
#include "../../external/AndUEDumper/AndUEDumper/src/UECoreEmbed.hpp"
#include "../../external/AndUEDumper/AndUEDumper/src/SDKCoreGen.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    sdkcoregen::UObjectArrayLayout ua;
    if (argc >= 4) { ua.ItemStride = atoi(argv[1]); ua.ObjectOffset = atoi(argv[2]); ua.NumElementsPerChunk = atoi(argv[3]); }
    if (argc >= 10)
    {
        ua.ObjObjectsOffset  = atoi(argv[4]);
        ua.ObjectsOffset     = atoi(argv[5]);
        ua.MaxElementsOffset = atoi(argv[6]);
        ua.NumElementsOffset = atoi(argv[7]);
        ua.MaxChunksOffset   = atoi(argv[8]);
        ua.NumChunksOffset   = atoi(argv[9]);
    }

    printf("%s",
        "#include <cstdint>\n#include <cstring>\n#include <string>\n#include <functional>\n#include <type_traits>\n#include <cstddef>\n"
        "using int32 = int32_t; using uint8 = uint8_t; using int64 = int64_t;\n");
    printf("%s\n", kUECoreUEAssertH);                  // UEAssert.h (real check/checkf/ensure)
    printf("namespace SDK {\nclass UObject;\n");
    printf("%s\n", sdkcoregen::GenUObjectArray(ua).c_str());   // object-array block (uses check/checkf)
    printf("%s",
        "class UObject { public: void* vt; int32 InternalIndex; };\n"
        "FUObjectArray* GUObjectArray = nullptr;\n"                 // pointer-to-live overlay
        "static UObject* FindObjectImpl() {\n"
        "  const int32 N = GUObjectArray->GetObjectArrayNum();\n"
        "  for (int32 i=0;i<N;++i){ SDK::FUObjectItem* it = GUObjectArray->IndexToObject(i); UObject* o = it?it->Object:nullptr; if(o) return o; }\n"
        "  return nullptr;\n}\n"
        "static void Exercise() {\n"                   // exercise the macro set + ForEachObject
        "  GUObjectArray->ForEachObject([](UObject* o){ return o != nullptr; });\n"
        "  if (!ensure(GUObjectArray->GetObjectArrayNum() >= 0)) {}\n"
        "  if (!ensureMsgf(true, \"num=%d\", GUObjectArray->GetObjectArrayNum())) {}\n"
        "  check(true); checkf(GUObjectArray->GetObjectArrayNum() >= 0, \"n=%d\", 1);\n"
        "}\n"
        "} // namespace SDK\n"
        "int main(){ SDK::Exercise(); return SDK::FindObjectImpl()?0:0; }\n");
    return 0;
}
