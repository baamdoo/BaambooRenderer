#pragma once
#include "ShaderCompiler.h"

#include <string>

namespace slang { struct ShaderReflection; typedef ShaderReflection ProgramLayout; }

namespace baamboo::shader
{

bool ResolveBindings(slang::ProgramLayout* pLayout, eTarget target, render::ShaderBindingLayout& out, std::string& error);

} // namespace baamboo::shader
