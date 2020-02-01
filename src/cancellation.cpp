std::atomic_bool CancellationRequested;

void Cancel() { CancellationRequested = true; }
bool IsCancelled() { return CancellationRequested; }