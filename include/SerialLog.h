// Debug-only serial logging.
//
// Wrap every Serial call in DEBUG_SERIAL(...). Debug builds define APP_SERIAL_LOGGING (see platformio.ini) and run
// the statement; Release builds compile it to nothing, so logging costs no flash or time there. The do/while wrapper
// makes the macro behave as a single statement, so it is safe in an unbraced if/else.
#pragma once

#ifdef APP_SERIAL_LOGGING
#define DEBUG_SERIAL(statement) do { statement; } while (0)
#else
#define DEBUG_SERIAL(statement) do { } while (0)
#endif
