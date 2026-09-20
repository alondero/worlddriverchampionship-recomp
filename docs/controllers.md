# Controllers

Controller mapping is not established yet. The first port pass should record:

- the N64 button and analog ranges expected by the ROM;
- keyboard and gamepad defaults;
- dead-zone, saturation, and quantisation rules;
- which thread samples input and which boundary consumes it; and
- a host test for the mapping plus a bounded in-game acceptance scenario.

Do not copy another game's control assumptions. Keep SDL/controller-profile
handling in the host module and the game's semantic actions in a small adapter.
