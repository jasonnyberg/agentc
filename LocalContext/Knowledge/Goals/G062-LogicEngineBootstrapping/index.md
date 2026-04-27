# G062: Logic Engine Bootstrapping

## Goal
Promote Mini-Kanren logic engine capabilities to first-class bootstrap imports in the Edict VM, removing the need for manual runtime FFI resolution in client code.

## Status
- **CANCELLED** (Verified existing FFI import mechanism is sufficient; infrastructure verified and stable.)

## Next Steps
1. Extend `EdictVM::createBootstrapCuratedCartographer` or similar to include the Kanren runtime FFI.
2. Update `EdictVM::runStartupBootstrapPrelude` to include the logic FFI binding.
3. Verify that `logic!` is available immediately upon VM startup.
