### Micro (bench_perf, median of 3 runs × 2M keys, ns)

| workload | tier | stat | RC1 | RC2 | Δ | verdict |
|---|---|---|---:|---:|---:|---|
| vn-compose | T1-decision | mean | 133 | 76 | -42.9% | **faster** |
| vn-compose | T1-decision | p50 | 125 | 70 | -44.0% | **faster** |
| vn-compose | T1-decision | p90 | 183 | 98 | -46.4% | **faster** |
| vn-compose | T1-decision | p99 | 247 | 132 | -46.6% | **faster** |
| vn-compose | T1-decision | p999 | 336 | 160 | -52.4% | **faster** |
| vn-compose | T1+encode | mean | 170 | 110 | -35.0% | **faster** |
| vn-compose | T1+encode | p50 | 157 | 100 | -36.3% | **faster** |
| vn-compose | T1+encode | p90 | 223 | 130 | -41.7% | **faster** |
| vn-compose | T1+encode | p99 | 376 | 281 | -25.3% | **faster** |
| vn-compose | T1+encode | p999 | 499 | 328 | -34.3% | **faster** |
| mixed | T1-decision | mean | 133 | 75 | -43.4% | **faster** |
| mixed | T1-decision | p50 | 127 | 74 | -41.7% | **faster** |
| mixed | T1-decision | p90 | 179 | 101 | -43.6% | **faster** |
| mixed | T1-decision | p99 | 249 | 142 | -43.0% | **faster** |
| mixed | T1-decision | p999 | 366 | 214 | -41.5% | **faster** |
| mixed | T1+encode | mean | 165 | 105 | -36.1% | **faster** |
| mixed | T1+encode | p50 | 159 | 103 | -35.2% | **faster** |
| mixed | T1+encode | p90 | 217 | 131 | -39.6% | **faster** |
| mixed | T1+encode | p99 | 296 | 183 | -38.2% | **faster** |
| mixed | T1+encode | p999 | 416 | 259 | -37.7% | **faster** |
| passthrough | T1-decision | mean | 127 | 76 | -40.0% | **faster** |
| passthrough | T1-decision | p50 | 119 | 74 | -37.8% | **faster** |
| passthrough | T1-decision | p90 | 169 | 98 | -42.0% | **faster** |
| passthrough | T1-decision | p99 | 250 | 139 | -44.4% | **faster** |
| passthrough | T1-decision | p999 | 355 | 197 | -44.5% | **faster** |
| passthrough | T1+encode | mean | 158 | 107 | -32.4% | **faster** |
| passthrough | T1+encode | p50 | 148 | 103 | -30.4% | **faster** |
| passthrough | T1+encode | p90 | 203 | 129 | -36.5% | **faster** |
| passthrough | T1+encode | p99 | 287 | 169 | -41.1% | **faster** |
| passthrough | T1+encode | p999 | 402 | 247 | -38.6% | **faster** |
| delete | T1-decision | mean | 142 | 82 | -42.6% | **faster** |
| delete | T1-decision | p50 | 136 | 78 | -42.6% | **faster** |
| delete | T1-decision | p90 | 186 | 104 | -44.1% | **faster** |
| delete | T1-decision | p99 | 232 | 154 | -33.6% | **faster** |
| delete | T1-decision | p999 | 339 | 193 | -43.1% | **faster** |
| delete | T1+encode | mean | 187 | 123 | -34.3% | **faster** |
| delete | T1+encode | p50 | 173 | 109 | -37.0% | **faster** |
| delete | T1+encode | p90 | 238 | 157 | -34.0% | **faster** |
| delete | T1+encode | p99 | 434 | 333 | -23.3% | **faster** |
| delete | T1+encode | p999 | 481 | 346 | -28.1% | **faster** |

### E2E shim pipeline (e2e_bench, median of 3 runs × 100k keys)

| metric | RC1 | RC2 | Δ | verdict |
|---|---:|---:|---:|---|
| raw_us.p50 | 32.05 | 29.68 | -7.4% | **faster** |
| raw_us.p99 | 106.02 | 111.68 | +5.3% | slower |
| raw_us.p999 | 993.56 | 1035.07 | +4.2% | slower |
| raw_us.max | 3023.67 | 2583.15 | -14.6% | **faster** |
| burst_hot_us.p50 | 30.79 | 27.73 | -9.9% | **faster** |
| burst_hot_us.p99 | 87.87 | 81.79 | -6.9% | **faster** |
| burst_hot_us.p999 | 97.90 | 96.62 | -1.3% | unchanged |
| burst_hot_us.max | 99.99 | 99.98 | -0.0% | unchanged |
| pipeline_us.p50 | 31.86 | 29.29 | -8.1% | **faster** |
| pipeline_us.p99 | 89.25 | 83.75 | -6.2% | **faster** |
| pipeline_us.p999 | 98.33 | 97.12 | -1.2% | unchanged |
| pipeline_us.max | 99.99 | 99.98 | -0.0% | unchanged |
| wake_pay_us.p50 | 44.06 | 41.66 | -5.4% | **faster** |
| wake_pay_us.p99 | 118.93 | 120.00 | +0.9% | unchanged |
| throughput_keys_per_s | 13324.40 | 13261.10 | -0.5% | unchanged |
| peak_rss_mb_total | 8.44 | 8.44 | +0.0% | unchanged |
| pipeline_footprint_mb | 5.96 | 5.96 | +0.0% | unchanged |
| lag_spikes_keys | 1321.00 | 1440.00 | +9.0% | slower |

### Tone-population edit latency (bench_tone_latency, p50 µs)

| population | barrier | RC1 | RC2 | Δ | verdict |
|---|---|---:|---:|---:|---|
| dbar | hybrid | 0.230 | 0.237 | +3.0% | unchanged |
| dbar | spin | 0.231 | 0.235 | +1.7% | unchanged |
| double-tone | hybrid | 0.325 | 0.311 | -4.3% | unchanged |
| double-tone | spin | 0.328 | 0.313 | -4.6% | unchanged |
| heavy-word | hybrid | 0.395 | 0.296 | -25.1% | **faster** |
| heavy-word | spin | 0.392 | 0.294 | -25.0% | **faster** |
| horn-compound | hybrid | 0.414 | 0.356 | -14.0% | **faster** |
| horn-compound | spin | 0.415 | 0.356 | -14.2% | **faster** |
| mid-word | hybrid | 0.243 | 0.233 | -4.1% | unchanged |
| mid-word | spin | 0.244 | 0.234 | -4.1% | unchanged |
| mixed | hybrid | 7.061 | 7.254 | +2.7% | unchanged |
| mixed | spin | 7.114 | 6.820 | -4.1% | unchanged |
| reposition | hybrid | 0.198 | 0.206 | +4.0% | unchanged |
| reposition | spin | 0.197 | 0.207 | +5.1% | slower |
| restore-reissue | hybrid | 0.219 | 0.219 | +0.0% | unchanged |
| restore-reissue | spin | 0.221 | 0.222 | +0.5% | unchanged |
| tone-append | hybrid | 0.200 | 0.210 | +5.0% | unchanged |
| tone-append | spin | 0.197 | 0.208 | +5.6% | slower |

### Real-world typing benchmark (bench_real_world_typing, 50k keys × 3 runs, engine decision ns)

| workload | stat | RC1 | RC2 | Δ | verdict |
|---|---|---:|---:|---:|---|
| burst_200wpm | mean | 172 | 80 | -53.5% | **faster** |
| burst_200wpm | p50 | 161 | 75 | -53.4% | **faster** |
| burst_200wpm | p90 | 252 | 99 | -60.7% | **faster** |
| burst_200wpm | p95 | 283 | 123 | -56.5% | **faster** |
| burst_200wpm | p99 | 375 | 172 | -54.1% | **faster** |
| burst_200wpm | p99.9 | 456 | 191 | -58.1% | **faster** |
| normal_60wpm | mean | 177 | 79 | -55.4% | **faster** |
| normal_60wpm | p50 | 164 | 73 | -55.5% | **faster** |
| normal_60wpm | p90 | 251 | 97 | -61.4% | **faster** |
| normal_60wpm | p95 | 282 | 118 | -58.2% | **faster** |
| normal_60wpm | p99 | 361 | 170 | -52.9% | **faster** |
| normal_60wpm | p99.9 | 465 | 189 | -59.4% | **faster** |
| slow_30wpm | mean | 146 | 79 | -45.9% | **faster** |
| slow_30wpm | p50 | 142 | 73 | -48.6% | **faster** |
| slow_30wpm | p90 | 204 | 96 | -52.9% | **faster** |
| slow_30wpm | p95 | 224 | 122 | -45.5% | **faster** |
| slow_30wpm | p99 | 251 | 173 | -31.1% | **faster** |
| slow_30wpm | p99.9 | 312 | 194 | -37.8% | **faster** |
| burst_pause | mean | 134 | 79 | -41.0% | **faster** |
| burst_pause | p50 | 131 | 74 | -43.5% | **faster** |
| burst_pause | p90 | 185 | 96 | -48.1% | **faster** |
| burst_pause | p95 | 206 | 119 | -42.2% | **faster** |
| burst_pause | p99 | 236 | 171 | -27.5% | **faster** |
| burst_pause | p99.9 | 346 | 188 | -45.7% | **faster** |
| single_keys | mean | 104 | 75 | -27.9% | **faster** |
| single_keys | p50 | 99 | 67 | -32.3% | **faster** |
| single_keys | p90 | 141 | 91 | -35.5% | **faster** |
| single_keys | p95 | 157 | 99 | -36.9% | **faster** |
| single_keys | p99 | 187 | 117 | -37.4% | **faster** |
| single_keys | p99.9 | 255 | 186 | -27.1% | **faster** |
| mixed | mean | 129 | 90 | -30.2% | **faster** |
| mixed | p50 | 115 | 79 | -31.3% | **faster** |
| mixed | p90 | 186 | 134 | -28.0% | **faster** |
| mixed | p95 | 208 | 166 | -20.2% | **faster** |
| mixed | p99 | 260 | 210 | -19.2% | **faster** |
| mixed | p99.9 | 602 | 437 | -27.4% | **faster** |
