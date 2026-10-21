#pragma once

// Deterministic allocation and cancellation failures at every reached history preparation checkpoint.
[[nodiscard]] int RunHistoryPreparationFaultTests();
