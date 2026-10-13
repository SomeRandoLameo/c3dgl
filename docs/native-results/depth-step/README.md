Depth/culling checkpoint
=======================

Implemented depth functions, depth masks/ranges, face culling and a rotating indexed VBO cube. The complete build check passes, including 15 host tests and C/C++/rust3ds consumers. Saved emulator reports and pixel captures document completed runs; no complete repeated-run gate is claimed without a generated summary. Further emulator work was stopped at the user request.

The reference benchmark now asserts resident compact SHORT geometry; the previous FLOAT baseline did not exercise its GPU cache. Reference Z depth is selected with C3DGL_W_DEPTH=0. The reference-only both-face correctness cell uses its polygon-mode path. Rejected runs are retained locally and excluded from accepted comparisons.
