#pragma once

#include <Windows.h>

// Called on the existing dedicated MTA worker. The caller owns owner and is responsible for
// saving/restoring the user's clipboard around this suite.
[[nodiscard]] int RunClipboardBindingTransferTests(HWND owner);
