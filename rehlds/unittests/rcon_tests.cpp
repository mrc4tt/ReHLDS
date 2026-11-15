#include "precompiled.h"
#include "rehlds_tests_shared.h"
#include "cppunitlite/TestHarness.h"

// The RCON password is shown as *** in the console and logs; the challenge and the command stay as sent
TEST(RconMaskPassword, Rcon, 1000)
{
	char out[512];
	SV_RconMaskPassword("rcon 1234567 \"secret pw\" status", out, sizeof(out));
	ZSTR_EQUAL("quoted password", "rcon 1234567 \"***\" status", out);
	SV_RconMaskPassword("rcon 1234567 secret kick \"Player\"", out, sizeof(out));
	ZSTR_EQUAL("bare password", "rcon 1234567 *** kick \"Player\"", out);
	SV_RconMaskPassword("rcon 1234567", out, sizeof(out));
	ZSTR_EQUAL("no password", "rcon 1234567", out);
	SV_RconMaskPassword("rcon 1234567 \"unterminated", out, sizeof(out));
	ZSTR_EQUAL("unterminated quote", "rcon 1234567 \"***\"", out);
	SV_RconMaskPassword("", out, sizeof(out));
	ZSTR_EQUAL("empty", "", out);
	char small[8];
	SV_RconMaskPassword("rcon 1234567 \"x\" status", small, sizeof(small));
	ZSTR_EQUAL("cut to the buffer", "rcon 12", small);
}
