# MXM integration tests

These tests exercise the built MXM application and cross subsystem boundaries. They are intended to catch failures that compile checks and isolated unit tests cannot.

Integration tests must be registered with CTest and carry the `integration` label. Tests should be deterministic, non-interactive, and runnable on GitHub-hosted Linux and Windows runners without network access during execution.

Plugin-host integration tests belong here. A plugin-format implementation should test the real discovery and hosting path where practical: discover a test plugin, instantiate it, negotiate ports, process audio/events, exercise parameters, serialize and restore state, destroy and recreate the instance, and verify deterministic output. Keep test plugins small and redistributable. Third-party compatibility tests may supplement the deterministic fixture but should not replace it.

GUI tests should automate lifecycle behavior that can be checked reliably in CI, such as editor creation, resize, close, and reopen. Visual correctness and human input behavior remain smoke-test concerns when a hosted runner cannot validate them reliably.

Do not add sleeps as synchronization, depend on user configuration, write outside the build tree, or require physical audio/MIDI devices. Runtime artifacts that help diagnose failures should be written under the integration test build directory so CI can collect them.
