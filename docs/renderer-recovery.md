# Renderer reconnection

kavtor 0.23.1 rebuilds all four M/E background and key compositions after a
renderer connection is established. Source preparation and per-M/E SuperSource
instances must be acknowledged before their routes are submitted. Downstream
M/Es are rebuilt first, with neutral background transforms and CUT routes.
Logical source selections do not change and no transition is executed. A
key-only preview retains the program background and applies the next key state.

Confirmed recovery restores key preparation flags and releases mixer operation.
If a recovery batch is rejected, kavtor reports an error and does not loop
retries. Check the output, correct the underlying error and use ARM to retry.
Ordinary source/output preparation keeps its existing confirmation semantics.

This is not rollback for a partially accepted take, nor a guarantee that a
renderer restart preserves clip position, a paused transition or black output.
Those operations require separate reconciliation policies.

The regression disconnects/reconnects with selections on two M/Es, an active
key and key-only preview. It checks the actual submitted routes, retained
logical selections, rejected recovery and successful explicit ARM retry.

A real Caspar restart test passed with casparMIX 0.16.0: PGM retained input 0
and PST input 1; renderer routes 10 → 1 and 9 → 2 were rebuilt automatically.
The test harness separately restored four clip positions after restarting;
automatic clip-position persistence is not claimed.
