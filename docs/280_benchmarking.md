## Benchmarking

Arvane uses Google Benchmark for performance benchmarks.

GoogleTest and GoogleMock are reserved for correctness tests. Do not use unit test elapsed time as a performance measurement.

Benchmarks live under `benchmarks/` and should be built as separate executables from `tests/`.

The main local benchmark executable is `arv_benchmarks`.

Typical local workflow:

```sh
cmake -S . -B build -DARVANE_BUILD_BENCHMARKS=ON
cmake --build build --target arv_benchmarks
./build/benchmarks/arv_benchmarks
```

Use Google Benchmark filters when investigating one area:

```sh
./build/benchmarks/arv_benchmarks --benchmark_filter=BM_NoisyTerrainSectionMesh
```

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

## Stable Scene

`BM_StableCameraPathMeshing` defines the current stable CPU-side scene benchmark:

* fixed 9 x 9 chunk-section terrain field
* deterministic noisy terrain contents
* fixed eight-step camera/chunk path
* 3 x 3 visible meshing workload around each path point

Use it to compare meshing throughput, snapshot overhead, and chunk-access changes before and after performance work.
