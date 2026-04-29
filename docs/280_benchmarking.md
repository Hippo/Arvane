## Benchmarking

Arvane uses Google Benchmark for performance benchmarks.

GoogleTest and GoogleMock are reserved for correctness tests. Do not use unit test elapsed time as a performance measurement.

Benchmarks live under `benchmarks/` and should be built as separate executables from `tests/`.

Benchmarks should focus on stable engine hot paths:
- chunk section meshing
- lighting propagation
- world generation
- chunk snapshots
- block/state registry lookup
- region file serialization
- protocol serialization
- core containers and allocators

Benchmark inputs must be deterministic. Avoid benchmarking rendering/GPU behavior in Google Benchmark unless the benchmark is explicitly isolated and documented.

Performance benchmarks should not assert exact timings in normal CI. They are used for comparison, regression investigation, and local profiling.