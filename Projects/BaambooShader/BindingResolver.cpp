#include "BindingResolver.h"

#include <slang.h>

#include <cassert>

namespace baamboo::shader
{


bool ResolveBindings(slang::ProgramLayout* pLayout, eTarget target, render::ShaderBindingLayout& out, std::string& error)
{
	out = {};

	auto pReflection = pLayout->getGlobalParamsVarLayout();
	if (pReflection == nullptr)
	{
		error = "[Slang Layout Error] -- No global params in reflection!";
		return false;
	}

	// Global Type
	auto pType = pReflection->getTypeLayout();
	if (pType == nullptr)
	{
		error = "[Slang Layout Error] -- No type layout!";
		return false;
	}
	if (pType->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
	{
		error = "[Invalid Engine Usage] -- Implicit constant buffer(loose uniform buffer) is not allowed!";
		return false;
	}
	else if (pType->getKind() == slang::TypeReflection::Kind::Struct) // "Global" Struct
	{
		for (u32 i = 0u; i < pType->getFieldCount(); ++i)
		{
			auto pGlobalField = pType->getFieldByIndex(i);
			assert(pGlobalField);

			const auto kind = pGlobalField->getTypeLayout()->getKind();
			if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				if (target == eTarget::Dxil)
				{
					const u32 reg   = u32(pGlobalField->getOffset(slang::ParameterCategory::ConstantBuffer));
					const u32 space = u32(pGlobalField->getBindingSpace(slang::ParameterCategory::ConstantBuffer));
					if (reg == render::kSlangDrawIDRegister && space == render::kSlangDrawIDSpace)
						continue;

					// DXIL has no PushConstantBuffer category; the push block is a plain cbuffer at b0 space100.
					if (!(reg == render::kSlangPushRegister && space == render::kSlangPushSpace))
					{
						error = "[Invalid Engine Usage] -- ConstantBuffer is allowed only for Push Constant";
						return false;
					}
				}
				else // eTarget::Spirv
				{
					bool bPush = false;
					for (u32 c = 0u; c < pGlobalField->getCategoryCount(); ++c)
					{
						if (pGlobalField->getCategoryByIndex(c) == slang::ParameterCategory::PushConstantBuffer && 
							pGlobalField->getOffset(slang::ParameterCategory::PushConstantBuffer) == render::kSlangPushBlockIndex)
						{
							bPush = true;
							break;
						}
					}

					if (!bPush)
					{
						error = "[Invalid Engine Usage] -- ConstantBuffer is allowed only for Push Constant";
						return false;
					}
				}

				auto pBlockType = pGlobalField->getTypeLayout(); // ConstantBuffer< T >
				assert(pBlockType);
				auto pElementVar = pBlockType->getElementVarLayout(); // T
				assert(pElementVar);
				auto pElementType = pElementVar->getTypeLayout(); // T( ... )
				assert(pElementType);

				for (u32 f = 0u; f < pElementType->getFieldCount(); ++f)
				{
					auto pField = pElementType->getFieldByIndex(f);
					assert(pField);

					out.pushOffsets[pField->getName()] = u32(pField->getOffset());
				}
				
				out.pushBlockSize = u32(pElementType->getSize());
			}
		}
	}

	return true;
}


} // namespace baamboo::shader
