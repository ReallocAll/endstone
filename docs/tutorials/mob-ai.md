# Mob AI controls

Mob AI controls adjust matching native sleep, random-stroll, random-look-around and new-target-search goals on supported Linux BDS binaries. They default to **off**. Endstone checks native hook addresses and each exact goal instance before applying a control; unsupported platforms, goals or unreliable entity state retain vanilla behavior. Windows native controls are unavailable.

## Configuration and first setup

Settings live in the server working directory's `endstone.toml`. For a new installation, the Python launcher supplies:

```toml
[mob-ai]
enabled = false

[mob-ai.idle]
enabled = true
player-range-blocks = 32
disable-random-stroll = true
disable-random-look-around = true

[mob-ai.target-search]
enabled = true
player-range-blocks = 32
interval-ticks = 20

[mob-ai.villagers]
prevent-sleep = false
```

Set `mob-ai.enabled = true` to activate the controls. Idle and target-search settings are independent: an effective feature `enabled = false` keeps that feature vanilla while the other feature can remain active. Each idle `disable-*` flag controls only its named goal. Set `prevent-sleep = true` to prevent supported `minecraft:villager_v2` sleep goals; `false` keeps vanilla sleep. The master switch disables every mob AI control, including overrides.

The two `player-range-blocks` values are independent nearby-player protections, **not target detection radii**. A qualifying player inside the idle range preserves vanilla idle behavior; one inside the target-search range preserves vanilla search cadence. Ranges are finite positive numbers, use three-dimensional feet distance, include the boundary, and consider nonspectator players in the same dimension. `interval-ticks` is an integer from 1 to 4294967295 and delays only new target searches outside their protection range. Existing-target validation, attacks, retaliation, pathfinding, breeding, grazing, egg laying, work, trading, physics and synchronization are not throttled. The 32-block ranges and 20-tick interval are starting settings, with no performance or maximum reaction-delay guarantee.

The launcher fills missing global fields from the template. It generates no override rules and leaves override fields omitted so they inherit the corresponding global values at runtime. Existing values, comments and invalid data are preserved; invalid settings are rejected rather than repaired.

Launcher preparation updates the whole main configuration even when mob AI controls are disabled. Its [atomic replacement and file metadata behavior](../getting-started/start-your-server.md#configuration-updates) also applies to existing installations. If temporary-file cleanup fails, the warning reports the remaining path. Loading the native runtime directly does not create configuration; a missing `mob-ai` section initially keeps vanilla behavior.

## Per-type overrides

Each `[[mob-ai.overrides]]` rule has a nonempty `types` list of unique exact identifiers, such as `minecraft:cow`. Wildcards and duplicate identifiers are invalid. A rule can contain `idle`, `target-search` and `villagers` subtables. There is no root per-type `enabled` field; use the feature subtable's fields. Omitted fields inherit their corresponding global feature values. Villager `prevent-sleep` control remains limited to `minecraft:villager_v2`.

For cows and pigs, use a 16-block idle protection range:

```toml
[[mob-ai.overrides]]
types = ["minecraft:cow", "minecraft:pig"]

[mob-ai.overrides.idle]
player-range-blocks = 16
```

For skeletons, use a 16-block idle protection range and a separate 48-block search protection range with searches every 40 ticks:

```toml
[[mob-ai.overrides]]
types = ["minecraft:skeleton"]

[mob-ai.overrides.idle]
player-range-blocks = 16

[mob-ai.overrides.target-search]
player-range-blocks = 48
interval-ticks = 40
```

With these settings, a nearby player can preserve vanilla skeleton search cadence while idle controls still apply. The type's native detection range is unchanged. The same type cannot occur in two rules for the same feature, even if those rules set different fields. Separate rules for the same type and different features are allowed. Unknown fields, invalid values and conflicting rules reject the entire configuration, even when the master switch is off.

## Capability boundaries and activity protection

Idle controls require a `minecraft:` Mob that is not a player and has the exact matching goal. The following 21 identifiers are excluded, all in the `minecraft:` namespace: `player`, `armor_stand`, `tripod_camera`, `npc`, `agent`, `villager`, `villager_v2`, `wandering_trader`, `trader_llama`, `ender_dragon`, `wither`, `warden`, `creaking`, `allay`, `copper_golem`, `iron_golem`, `snow_golem`, `guardian`, `elder_guardian`, `elder_guardian_ghost`, `shulker`.

Target-search controls additionally require the Monster category and one of these 12 exact identifiers: `zombie`, `husk`, `drowned`, `zombie_villager`, `zombie_villager_v2`, `skeleton`, `stray`, `wither_skeleton`, `bogged`, `parched`, `creeper`, `witch`. Exact selectors cannot bypass native capability boundaries. Unknown Minecraft Mobs may qualify for matching idle goals but do not gain search control from a Monster category alone.

Spider and cave-spider idle goals can qualify; their day/night targeting and retaliation remain vanilla. Endermen, zombie pigmen, piglins, piglin brutes, pets and guardians retain vanilla target searches. Villager sleep control is independent and only accepts exact `minecraft:villager_v2`.

Numeric-range controls retain vanilla behavior with an existing target, riding, passengers, a qualifying nearby player, or activity during the previous 100 ticks. Hurt and interaction activity is retained only for an entity with a resolved supported idle/search capability. Any of these flags also protects the entity: `LEASHED`, `INLOVE`, `TEMPTED`, `TAMED`, `ANGRY`, `SCARED`, `ONFIRE`, `USINGITEM`, `TRADE_INTEREST`, `IS_IN_UI`, `ADMIRING`, `CASTING`, `CHARGING`, `IGNITED`, `FACING_TARGET_TO_RANGE_ATTACK`, `DELAYED_ATTACK`, `RAM_ATTACK`.

Missing required components, invalid positions or an unresolved relevant player's state preserve vanilla behavior. Removed players and players conclusively in another dimension are ignored; explicit spectators are ignored. Ambiguous default game modes retain vanilla behavior when their world default is spectator or unresolved. These safeguards cover the stated conditions; hidden species-specific behavior and every farm or device have not been established as safe.

## Apply changes, disable and restore

Mob AI settings are loaded once during server startup. After editing `endstone.toml`, restart the server and check the English `[Perf]` result. A successful load reports `Mob AI policies loaded`. Endstone `/reload` keeps the Mob AI settings loaded at startup and does not explicitly reset Mob AI activity or search state. Normal plugin and server-data reload behavior still applies.

If Mob AI initialization rejects syntax errors, unknown fields, invalid values or conflicting overrides, controls stay inactive. Disabled configurations still validate all settings. The launcher may reject malformed TOML before the server starts.

To disable mob AI controls, change the master switch in the existing main configuration and restart the server, keeping the feature settings and overrides:

```toml
[mob-ai]
enabled = false
```

To restore prior behavior, restore the saved configuration and restart the server.

## Coverage and verification limits

The pinned definition reference is Mojang/bedrock-samples **v1.26.50.4**, commit `46ba6ea985fb5a92d79a9419198f10dda14c199d`: 128 definitions, 64 containing `random_stroll`, 55 `random_look_around`, 49 `nearest_attackable_target`, and one `sleep`. Their union is **78 definition types**, including tools, special entities and behaviors that may occur only in component groups. This is a definition inventory, not a claim of 78 supported or runtime-tested species. Practical coverage depends separately on definition presence, exact native goal admission and current-state eligibility.

Native support is limited to Linux on Endstone's supported BDS version and requires the native checks to pass. Automated policy/state tests and compilation do not establish live species behavior. Cow/pig idle and breeding, zombie/skeleton/creeper search, spider day/night, player interaction, protections and exclusions require separate live validation. Unobserved cases remain unverified. Preventing villager sleep changes bedtime positioning and can remove native sleep healing; it does not add health compensation, longer work hours or guaranteed overnight restocking. No performance benefit or compatibility with every farm, breeder or sleep-based transport device is promised.
