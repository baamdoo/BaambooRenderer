#pragma once

namespace baamboo
{

namespace math
{

template< typename T >
inline T AlignUp(T size, T alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

}

} // namespace baamboo