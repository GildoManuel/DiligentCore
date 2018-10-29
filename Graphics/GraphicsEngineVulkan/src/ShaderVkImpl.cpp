/*     Copyright 2015-2018 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include <array>
#include "pch.h"

#include "ShaderVkImpl.h"
#include "RenderDeviceVkImpl.h"
#include "DataBlobImpl.h"
#include "GLSLSourceBuilder.h"
#include "SPIRVUtils.h"

namespace Diligent
{

ShaderVkImpl::ShaderVkImpl(IReferenceCounters* pRefCounters, RenderDeviceVkImpl* pRenderDeviceVk, const ShaderCreationAttribs& CreationAttribs) : 
    TShaderBase       (pRefCounters, pRenderDeviceVk, CreationAttribs.Desc),
    m_StaticResLayout (*this, pRenderDeviceVk->GetLogicalDevice()),
    m_StaticResCache  (ShaderResourceCacheVk::DbgCacheContentType::StaticShaderResources),
    m_StaticVarsMgr   (*this),
    m_EntryPoint      (CreationAttribs.EntryPoint)
{
    if (CreationAttribs.Source != nullptr || CreationAttribs.FilePath != nullptr)
    {
        DEV_CHECK_ERR(CreationAttribs.ByteCode == nullptr, "'ByteCode' must be null when shader is created from source code or a file");
        DEV_CHECK_ERR(CreationAttribs.ByteCodeSize == 0, "'ByteCodeSize' must be 0 when shader is created from source code or a file");

        if (CreationAttribs.SourceLanguage == SHADER_SOURCE_LANGUAGE_HLSL)
        {
            m_SPIRV = HLSLtoSPIRV(CreationAttribs, CreationAttribs.ppCompilerOutput);
        }
        else
        {
            auto GLSLSource = BuildGLSLSourceString(CreationAttribs, TargetGLSLCompiler::glslang, "#define TARGET_API_VULKAN 1\n");
            m_SPIRV = GLSLtoSPIRV(m_Desc.ShaderType, GLSLSource.c_str(), static_cast<int>(GLSLSource.length()), CreationAttribs.ppCompilerOutput);
        }
    
        if (m_SPIRV.empty())
        {
            LOG_ERROR_AND_THROW("Failed to compile shader");
        }
    }
    else if (CreationAttribs.ByteCode != nullptr)
    {
        DEV_CHECK_ERR(CreationAttribs.ByteCodeSize != 0, "ByteCodeSize must not be 0");
        DEV_CHECK_ERR(CreationAttribs.ByteCodeSize % 4 == 0, "Byte code size (", CreationAttribs.ByteCodeSize, ") is not multiple of 4");
        m_SPIRV.resize(CreationAttribs.ByteCodeSize/4);
        memcpy(m_SPIRV.data(), CreationAttribs.ByteCode, CreationAttribs.ByteCodeSize);
    }
    else 
    {
        LOG_ERROR_AND_THROW("Shader source must be provided through one of the 'Source', 'FilePath' or 'ByteCode' members");
    }

    // We cannot create shader module here because resource bindings are assigned when
    // pipeline state is created
    
    // Load shader resources
    auto& Allocator = GetRawAllocator();
    auto* pRawMem = ALLOCATE(Allocator, "Allocator for ShaderResources", sizeof(SPIRVShaderResources));
    auto* pResources = new (pRawMem) SPIRVShaderResources(Allocator, pRenderDeviceVk, m_SPIRV, m_Desc, CreationAttribs.UseCombinedTextureSamplers ? CreationAttribs.CombinedSamplerSuffix : nullptr);
    m_pShaderResources.reset(pResources, STDDeleterRawMem<SPIRVShaderResources>(Allocator));

    m_StaticResLayout.InitializeStaticResourceLayout(m_pShaderResources, GetRawAllocator(), m_StaticResCache);
    // m_StaticResLayout only contains static resources, so reference all of them
    m_StaticVarsMgr.Initialize(m_StaticResLayout, GetRawAllocator(), nullptr, 0, m_StaticResCache);
}

ShaderVkImpl::~ShaderVkImpl()
{
    m_StaticVarsMgr.Destroy(GetRawAllocator());
}

#ifdef DEVELOPMENT
void ShaderVkImpl::DvpVerifyStaticResourceBindings()
{
    m_StaticResLayout.dvpVerifyBindings(m_StaticResCache);
}
#endif

}
