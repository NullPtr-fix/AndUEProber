#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "UE/UEGameProfile.hpp"
#include "UE/UEOffsets.hpp"

class IGameProfileEx
{
public:
    virtual ~IGameProfileEx() = default;

    virtual uintptr_t PublicGetGUObjectArrayPtr() const = 0;

    virtual std::string PublicGetNameByID(int32_t id) const = 0;

    virtual uintptr_t PublicGetDecryptFNameAddr() const = 0;

    virtual void SetProbedOffsets(const UE_Offsets& offsets) = 0;

    virtual bool HasProbedOffsets() const = 0;

    virtual IGameProfile* AsGameProfile() = 0;
};

template <typename TProfile>
class GameProfileEx : public TProfile, public IGameProfileEx
{
    UE_Offsets m_ProbedOffsets{};
    bool       m_HasProbedOffsets = false;

public:
    GameProfileEx() = default;

    template<typename T, typename = void>
    struct has_report_name_resolver : std::false_type {};
    template<typename T>
    struct has_report_name_resolver<T, std::void_t<decltype(std::declval<const T&>().ReportNameResolverAddr())>> : std::true_type {};

    uintptr_t PublicGetGUObjectArrayPtr() const override
    {
        return this->GetGUObjectArrayPtr();
    }

    std::string PublicGetNameByID(int32_t id) const override
    {
        return this->GetNameByID(id);
    }

    uintptr_t PublicGetDecryptFNameAddr() const override
    {
        if constexpr (has_report_name_resolver<TProfile>::value)
        {
            return this->ReportNameResolverAddr();
        }
        return 0;
    }

    void SetProbedOffsets(const UE_Offsets& offsets) override
    {
        m_ProbedOffsets    = offsets;
        m_HasProbedOffsets = true;
    }

    bool HasProbedOffsets() const override
    {
        return m_HasProbedOffsets;
    }

    IGameProfile* AsGameProfile() override
    {
        return static_cast<IGameProfile*>(this);
    }

    UE_Offsets* GetOffsets() const override
    {
        if (m_HasProbedOffsets)
        {
            return const_cast<UE_Offsets*>(&m_ProbedOffsets);
        }
        return TProfile::GetOffsets();
    }
};
