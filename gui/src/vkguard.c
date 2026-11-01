#include "vkguard.h"

#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>

int chiaki_guarded_call(ChiakiGuardedFunc fn, void *ctx, unsigned long *exception_code)
{
	*exception_code = 0;
	__try
	{
		return fn(ctx);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		*exception_code = GetExceptionCode();
		return 0;
	}
}
#else
int chiaki_guarded_call(ChiakiGuardedFunc fn, void *ctx, unsigned long *exception_code)
{
	*exception_code = 0;
	return fn(ctx);
}
#endif
