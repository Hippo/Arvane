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

## Worldgen Benchmarks

World generation throughput benchmarks live in `benchmarks/world_benchmarks.cpp` and run through the shared `arv_benchmarks` executable. The legacy `BM_GenerateChunkColumn` benchmark still covers the old inline YAML compatibility generator. Pack-resolved benchmarks use `arvane_home/packs/arvane_base`, resolve `arvane:worldgen/dimension/overworld`, and keep pack loading, YAML parsing, registry construction, and dimension resolution outside the timed loop.

Useful filters:

```sh
./build/benchmarks/arv_benchmarks --benchmark_filter='BM_GenerateChunkColumn|BM_Worldgen.*'
./build/benchmarks/arv_benchmarks --benchmark_filter='BM_WorldgenResolved'
./build/benchmarks/arv_benchmarks --benchmark_filter='BM_WorldgenResolvedRegion16'
./build/benchmarks/arv_benchmarks --benchmark_filter='BM_WorldgenTerrainProfileOnly'
```

Recommended comparison command:

```sh
./build/benchmarks/arv_benchmarks --benchmark_filter='BM_WorldgenResolved(SingleChunk|Ocean|FeatureHeavyForest|Region16|Spiral)' --benchmark_repetitions=3 --benchmark_report_aggregates_only=false --benchmark_min_time=0.1s
```

The resolved benchmarks report user counters for chunks/sec, sections/sec, ms/chunk, p50/p90/p95/p99/max chunk latency inside region-style runs, section classifications, per-stage average/p99/max timing, feature and structure candidates, origin-profile cache/reject counts, carver/ravine/tunnel counters, and direct/lattice noise sample counts. `BM_WorldgenResolvedRegion16JobSystem` uses the actual `JobSystem` background lane and should be read with real time, not CPU time, because the main benchmark thread is mostly waiting for workers. Its counters also report queue wait percentiles, worker run time, completed jobs, and peak queued jobs for the benchmark workload.
