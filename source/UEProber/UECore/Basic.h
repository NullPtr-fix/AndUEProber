#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "UnrealContainers.h"

// Minimal UE surface for the prober. It walks GObjects and reads every field at
// *probed* offsets through raw pointers, so it needs only GObjects iteration +
// FName resolution — never a typed SDK layout. The full CoreUObject_* SDK that
// used to live alongside this header was intentionally dropped; pull a real one
// from a dump when reversing a specific game.

namespace SDK
{
using namespace UC;

class UObject;

class FName final
{
public:
	static inline std::function<std::string(int32_t)> s_NameResolver;

#define bWITH_CASE_PRESERVING_NAME false
#if !bWITH_CASE_PRESERVING_NAME
	union {
#endif
	int32                                         ComparisonIndex;
	int32                                         DisplayIndex;
#if !bWITH_CASE_PRESERVING_NAME
	};
#endif
	uint32                                        Number;

public:
	int32 GetDisplayIndex() const { return DisplayIndex; }

	static std::string GetPlainANSIString(const FName* Name)
	{
		if (s_NameResolver)
			return s_NameResolver(Name->ComparisonIndex);
		return {};
	}

	std::string GetRawString() const { return GetPlainANSIString(this); }

	std::string ToString() const
	{
		std::string OutputString = GetRawString();
		size_t pos = OutputString.rfind('/');
		if (pos == std::string::npos)
			return OutputString;
		return OutputString.substr(pos + 1);
	}

	const char* ToCString() const { return ToString().c_str(); }

	bool operator==(const FName& Other) const
	{
		return ComparisonIndex == Other.ComparisonIndex && Number == Other.Number;
	}
	bool operator!=(const FName& Other) const
	{
		return ComparisonIndex != Other.ComparisonIndex || Number != Other.Number;
	}
};

struct FUObjectItem final
{
	class UObject*                                Object;
	uint8                                         Pad_8[0x10];
};

class TUObjectArray final
{
public:
	static inline int32                           NumElementsPerChunk = 0x10000;

	struct FUObjectItem**                         Objects;
	uint8                                         Pad_8[0x8];
	int32                                         MaxElements;
	int32                                         NumElements;
	int32                                         MaxChunks;
	int32                                         NumChunks;

public:
	inline int32 Num() const { return NumElements; }

	inline class UObject* GetByIndex(const int32 Index) const
	{
		if (Index < 0 || Index >= NumElements || !Objects)
			return nullptr;

		if (NumElementsPerChunk <= 0)
			return *reinterpret_cast<UObject**>((uintptr_t)Objects + Index * sizeof(FUObjectItem) + offsetof(FUObjectItem, Object));

		const int32_t ChunkIndex = Index / NumElementsPerChunk;
		const int32_t WithinChunkIndex = Index % NumElementsPerChunk;

		uint64_t chunk = *reinterpret_cast<uint64_t*>(Objects + ChunkIndex);
		if (!chunk)
			return nullptr;

		return *reinterpret_cast<UObject**>(chunk + (WithinChunkIndex * sizeof(FUObjectItem)) + offsetof(FUObjectItem, Object));
	}

	void ForEachObject(const std::function<bool(UObject*)> &callback) const
	{
		if (!callback) return;
		for (int32_t i = 0; i < NumElements; i++)
		{
			UObject* object = GetByIndex(i);
			if (!object) continue;
			if (callback(object)) return;
		}
	}
};

struct TUObjectArrayWrapper
{
private:
	friend class UObject;

	void* GObjectsAddress = nullptr;

	TUObjectArrayWrapper() = default;

public:
	TUObjectArrayWrapper(TUObjectArrayWrapper&&) = delete;
	TUObjectArrayWrapper(const TUObjectArrayWrapper&) = delete;
	TUObjectArrayWrapper& operator=(TUObjectArrayWrapper&&) = delete;
	TUObjectArrayWrapper& operator=(const TUObjectArrayWrapper&) = delete;

	inline void InitManually(void* GObjectsAddressParameter)
	{
		GObjectsAddress = GObjectsAddressParameter;
	}

	inline class TUObjectArray* operator->()
	{
		return reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}

	inline TUObjectArray& operator*() const
	{
		return *reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}

	inline operator const void* ()
	{
		return GObjectsAddress;
	}

	inline class TUObjectArray* GetTypedPtr()
	{
		return reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}
};

// Only the global object array is needed; all object fields are read at probed
// offsets via the raw pointers GObjects hands back.
class UObject
{
public:
	static inline TUObjectArrayWrapper GObjects;
};

}
