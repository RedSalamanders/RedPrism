#pragma once

// Headless, opt-in measurement of cancellation through real image work, including scratch retirement.
// Prints observations, never asserts an elapsed-time target. No sleeps/timers run in the ordinary test suite.
[[nodiscard]] int RunFilterWorkBenchmark();
