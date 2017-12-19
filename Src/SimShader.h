/*================================================================
Filename: SimShader.h
Date: 2017.12.6
Created by AirGuanZ
================================================================*/
#ifndef __SIMSHADER_H__
#define __SIMSHADER_H__

#include <tuple>
#include <utility>

#include "SimShaderFatalError.h"
#include "SimShaderReleaseCOMObjects.h"
#include "SimShaderStage.h"
#include "SimShaderUniforms.h"

namespace _SimShaderAux
{
    template<typename Func, typename Tu, size_t FI>
    inline void _DoForTupleElements(Func func, Tu &tu, std::index_sequence<FI>)
    {
        func(std::get<FI>(tu));
    }

    template<typename Func, typename Tu, size_t FI, size_t...OI>
    inline void _DoForTupleElements(Func func, Tu &tu, std::index_sequence<FI, OI...>)
    {
        func(std::get<FI>(tu));
        _DoForTupleElements<Func, Tu, OI...>(func, tu, std::index_sequence<OI...>());
    }

    template<typename Func, typename Tu>
    inline void DoForTupleElements(Func func, Tu &tu)
    {
        _DoForTupleElements<Func, Tu>(func, tu, std::make_index_sequence<std::tuple_size<Tu>::value>());
    }

    template<int v>
    inline constexpr int _FindInNumListAux(void)
    {
        return v < 0 ? v : (v + 1);
    }

    template<typename NumType, NumType Dst>
    inline constexpr int FindInNumList(void)
    {
        return -1;
    }

    template<typename NumType, NumType Dst, NumType First, NumType...Others>
    inline constexpr int FindInNumList(void)
    {
        return Dst == First ? 0 : _FindInNumListAux<FindInNumList<NumType, Dst, Others...>()>();
    }

    template<typename NumType>
    inline constexpr bool IsRepeated(void)
    {
        return false;
    }

    template<typename NumType, NumType First, NumType...Others>
    inline constexpr bool IsRepeated(void)
    {
        return (FindInNumList<NumType, First, Others...>() >= 0) || IsRepeated<NumType, Others...>();
    }

    struct _ShaderStagePtrInitializer
    {
        template<typename T>
        void operator()(T *&ptr)
        {
            ptr = nullptr;
        }
    };

    struct _ShaderStageDeleter
    {
        template<typename T>
        void operator()(T *&ptr)
        {
            SafeDeleteObjects(ptr);
        }
    };

    struct _ShaderStageBinder
    {
        template<typename T>
        void operator()(T *pStage)
        {
            assert(pStage != nullptr);
            pStage->BindShader(DC_);
        }
        ID3D11DeviceContext *DC_;
    };

    struct _ShaderStageAvailableRec
    {
        template<typename T>
        void operator()(const T *ptr)
        {
            available = available && (ptr != nullptr);
        }

        bool available = true;
    };

    struct _ShaderStageUnbinder
    {
        template<typename T>
        void operator()(T *pStage)
        {
            assert(pStage != nullptr);
            pStage->UnbindShader(DC_);
        }
        ID3D11DeviceContext *DC_;
    };

    template<typename TStages, size_t...I>
    inline auto _CreateShaderUniformManagerAux(const TStages &stages, std::index_sequence<I...>)
    {
        return new _ShaderUniformManager<std::remove_pointer_t<std::tuple_element_t<I, TStages>>::Stage...>
                        ((*std::get<I>(stages))...);
    }

    template<ShaderStageSelector...StageSelectors>
    class _Shader
    {
    public:
        _Shader(void)
        {
            static_assert((IsRepeated<ShaderStageSelector, StageSelectors...>() == false), "Shader stage repeated");
            static_assert((FindInNumList<ShaderStageSelector, SS_VS, StageSelectors...>() != -1), "Vertex shader not found");
            static_assert((FindInNumList<ShaderStageSelector, SS_PS, StageSelectors...>() != -1), "Pixel shader not found");

            _ShaderStagePtrInitializer setter;
            DoForTupleElements(setter, stages_);
        }

        ~_Shader(void)
        {
            DestroyAllStages();
        }

        template<ShaderStageSelector StageSelector>
        void InitStage(ID3D11Device *dev, const std::string &src,
                       const std::string &target = _ShaderStage<StageSelector>::StageSpec::DefaultCompileTarget(),
                       const std::string &entry = "main")
        {
            auto &pStage = std::get<FindInNumList<ShaderStageSelector, StageSelector, StageSelectors...>()>(stages_);
            SafeDeleteObjects(pStage);
            pStage = new _ShaderStage<StageSelector>(dev, src, target, entry);
        }

        template<ShaderStageSelector StageSelector>
        void InitStage(ID3D11Device *dev, ID3D10Blob *shaderByteCode)
        {
            auto &pStage = std::get<FindInNumList<ShaderStageSelector, StageSelector, StageSelectors...>()>(stages_);
            SafeDeleteObjects(pStage);
            pStage = new _ShaderStage<StageSelector>(dev, shaderByteCode);
        }

        void DestroyAllStages(void)
        {
            _ShaderStageDeleter deleter;
            DoForTupleElements(deleter, stages_);
        }

        bool IsAllStagesAvailable(void) const
        {
            _ShaderStageAvailableRec rec;
            DoForTupleElements(rec, stages_);
            return rec.available;
        }

        template<ShaderStageSelector StageSelector>
        _ShaderStage<StageSelector> *GetStage(void)
        {
            return std::get<FindInNumList<ShaderStageSelector, StageSelector, StageSelectors...>()>(stages_);
        }

        template<ShaderStageSelector StageSelector>
        _ConstantBufferManager<StageSelector> *CreateConstantBufferManager(void)
        {
            return GetStage<StageSelector>()->CreateConstantBufferManager();
        }

        template<ShaderStageSelector StageSelector>
        _ShaderResourceManager<StageSelector> *CreateShaderResourceManager(void)
        {
            return GetStage<StageSelector>()->CreateShaderResourceManager();
        }

        template<ShaderStageSelector StageSelector>
        _ShaderSamplerManager<StageSelector> *CreateShaderSamplerManager(void)
        {
            return GetStage<StageSelector>()->CreateShaderSamplerManager();
        }

        _ShaderUniformManager<StageSelectors...> *CreateUniformManager(void)
        {
            return _CreateShaderUniformManagerAux(stages_,
                std::make_index_sequence<std::tuple_size_v<
                    std::tuple<_ShaderStage<StageSelectors>*...>>>());
        }

        const void *GetShaderByteCodeWithInputSignature(void)
        {
            return GetStage<SS_VS>()->GetShaderByteCode();
        }

        UINT GetShaderByteCodeSizeWithInputSignature(void)
        {
            return GetStage<SS_VS>()->GetShaderByteCodeSize();
        }

        void BindStages(ID3D11DeviceContext *DC)
        {
            _ShaderStageBinder binder = { DC };
            DoForTupleElements(binder, stages_);
        }

        void UnbindStages(ID3D11DeviceContext *DC)
        {
            _ShaderStageUnbinder unbinder = { DC };
            DoForTupleElements(unbinder, stages_);
        }

    private:
        std::tuple<_ShaderStage<StageSelectors>*...> stages_;
    };
}

namespace SimShader
{
    using Error = _SimShaderAux::SimShaderError;

    using ShaderStageSelector = _SimShaderAux::ShaderStageSelector;

    template<ShaderStageSelector StageSelector, typename BufferType, bool Dynamic = true>
    using ConstantBufferObject = _SimShaderAux::_ConstantBufferObject<BufferType, StageSelector, Dynamic>;

    template<ShaderStageSelector StageSelector>
    using ShaderResourceObject = _SimShaderAux::_ShaderResourceObject<StageSelector>;

    template<ShaderStageSelector StageSelector>
    using ShaderSamplerObject = _SimShaderAux::_ShaderSamplerObject<StageSelector>;

    template<ShaderStageSelector StageSelector>
    using ConstantBufferManager = _SimShaderAux::_ConstantBufferManager<StageSelector>;

    template<ShaderStageSelector StageSelector>
    using ShaderResourceManager = _SimShaderAux::_ShaderResourceManager<StageSelector>;

    template<ShaderStageSelector StageSelector>
    using ShaderSamplerManager = _SimShaderAux::_ShaderSamplerManager<StageSelector>;

    template<ShaderStageSelector StageSelector>
    using ShaderStage = _SimShaderAux::_ShaderStage<StageSelector>;

    template<ShaderStageSelector...StageSelectors>
    using Shader = _SimShaderAux::_Shader<StageSelectors...>;
}

using _SimShaderAux::SS_VS;
using _SimShaderAux::SS_PS;

#endif //__SIMSHADER_H__
