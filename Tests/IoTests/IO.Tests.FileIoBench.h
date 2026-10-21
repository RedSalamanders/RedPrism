#pragma once

// argv is the complete IoTests command line: --bench-file <mode> <width> <height> [layers].
int RunFileIoBench(int argc, wchar_t** argv);
