=== RC2 (base) vs RC3 (cur) — medians over 3 interleaved runs ===

[micro] engine decision latency, T1-decision (ns/key):
  vn-compose   p50    RC2=   65.0  RC3=   50.0  d=  -15.0 (-23.1%)
  vn-compose   p99    RC2=  117.0  RC3=   96.0  d=  -21.0 (-17.9%)
  vn-compose   p999   RC2=  160.0  RC3=  137.0  d=  -23.0 (-14.4%)
  vn-compose   mean   RC2=   70.5  RC3=   55.2  d=  -15.3 (-21.7%)
  mixed        p50    RC2=   77.0  RC3=   51.0  d=  -26.0 (-33.8%)
  mixed        p99    RC2=  136.0  RC3=  120.0  d=  -16.0 (-11.8%)
  mixed        p999   RC2=  180.0  RC3=  155.0  d=  -25.0 (-13.9%)
  mixed        mean   RC2=   79.9  RC3=   55.4  d=  -24.4 (-30.6%)
  passthrough  p50    RC2=   82.0  RC3=   44.0  d=  -38.0 (-46.3%)
  passthrough  p99    RC2=  134.0  RC3=   87.0  d=  -47.0 (-35.1%)
  passthrough  p999   RC2=  175.0  RC3=  118.0  d=  -57.0 (-32.6%)
  passthrough  mean   RC2=   82.4  RC3=   48.1  d=  -34.3 (-41.6%)
  delete       p50    RC2=   65.0  RC3=   46.0  d=  -19.0 (-29.2%)
  delete       p99    RC2=  139.0  RC3=   86.0  d=  -53.0 (-38.1%)
  delete       p999   RC2=  185.0  RC3=  138.0  d=  -47.0 (-25.4%)
  delete       mean   RC2=   74.9  RC3=   50.8  d=  -24.1 (-32.1%)

[micro] full key pipeline, T2-full (ns/key):

[e2e] shim pipeline (us):
  burst_hot_us   p50   RC2=    9.71  RC3=   10.12  d=   +0.41 (+4.2%)
  burst_hot_us   p99   RC2=   48.90  RC3=   46.37  d=   -2.53 (-5.2%)
  pipeline_us    p50   RC2=   10.00  RC3=   10.37  d=   +0.37 (+3.7%)
  pipeline_us    p99   RC2=   49.14  RC3=   46.91  d=   -2.24 (-4.6%)
  wake_pay_us    p50   RC2=   13.10  RC3=   13.76  d=   +0.66 (+5.0%)
  wake_pay_us    p99   RC2= 3244.07  RC3= 1994.70  d=-1249.37 (-38.5%)
  lag spikes med   RC2=6141  RC3=5260
  throughput med   RC2=13944  RC3=14477
  peak RSS med     RC2=9.07  RC3=9.07

[tone] edit p50 (us) per population[barrier]:
  dbar           [spin   ]  RC2=   0.201  RC3=   0.202  d=  +0.001 (+0.5%)
  dbar           [hybrid ]  RC2=   0.201  RC3=   0.202  d=  +0.001 (+0.5%)
  double-tone    [spin   ]  RC2=   0.286  RC3=   0.281  d=  -0.005 (-1.7%)
  double-tone    [hybrid ]  RC2=   0.285  RC3=   0.280  d=  -0.005 (-1.8%)
  heavy-word     [spin   ]  RC2=   0.270  RC3=   0.267  d=  -0.003 (-1.1%)
  heavy-word     [hybrid ]  RC2=   0.270  RC3=   0.267  d=  -0.003 (-1.1%)
  horn-compound  [spin   ]  RC2=   0.292  RC3=   0.300  d=  +0.008 (+2.7%)
  horn-compound  [hybrid ]  RC2=   0.298  RC3=   0.301  d=  +0.003 (+1.0%)
  mid-word       [spin   ]  RC2=   0.202  RC3=   0.201  d=  -0.001 (-0.5%)
  mid-word       [hybrid ]  RC2=   0.203  RC3=   0.201  d=  -0.002 (-1.0%)
  mixed          [spin   ]  RC2=  10.909  RC3=  10.992  d=  +0.083 (+0.8%)
  mixed          [hybrid ]  RC2=  11.649  RC3=  10.998  d=  -0.651 (-5.6%)
  reposition     [spin   ]  RC2=   0.175  RC3=   0.175  d=  +0.000 (+0.0%)
  reposition     [hybrid ]  RC2=   0.175  RC3=   0.175  d=  +0.000 (+0.0%)
  restore-reissue[spin   ]  RC2=   0.199  RC3=   0.183  d=  -0.016 (-8.0%)
  restore-reissue[hybrid ]  RC2=   0.198  RC3=   0.183  d=  -0.015 (-7.6%)
  tone-append    [spin   ]  RC2=   0.175  RC3=   0.175  d=  +0.000 (+0.0%)
  tone-append    [hybrid ]  RC2=   0.174  RC3=   0.174  d=  +0.000 (+0.0%)

[gate] rc: base=0 cur=0
