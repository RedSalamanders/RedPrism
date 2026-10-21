#pragma once

// Event-gated concurrent readers; returns the number of failed checks. No wall-clock assertions.
int RunRasterConcurrencyTests(int rounds);
