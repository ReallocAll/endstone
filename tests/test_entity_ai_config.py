import importlib
import os
import stat
from pathlib import Path

import pytest
import tomlkit

DEFAULT_MOB_AI = {
    "enabled": False,
    "idle": {
        "enabled": True,
        "player-range-blocks": 32,
        "disable-random-stroll": True,
        "disable-random-look-around": True,
    },
    "target-search": {"enabled": True, "player-range-blocks": 32, "interval-ticks": 20},
    "villagers": {"prevent-sleep": False},
}


@pytest.fixture
def bootstrap(tmp_path):
    cls = importlib.import_module("endstone.cli.base").Bootstrap
    return cls(str(tmp_path), no_confirm=True, remote="", interactive=False)


def read_config(bootstrap):
    return tomlkit.parse(bootstrap.config_path.read_text(encoding="utf-8"))


def file_state(path):
    info = path.stat()
    return (
        path.read_bytes(),
        info.st_dev,
        info.st_ino,
        info.st_mode,
        info.st_uid,
        info.st_gid,
        info.st_nlink,
        info.st_mtime_ns,
    )


@pytest.mark.parametrize("original", [None, "", "# My server\n[commands]\nlog = false # Keep private\n"])
def test_prepare_adds_disabled_recommendations(bootstrap, original):
    if original is not None:
        bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert "entity-ai" not in config
    assert bootstrap.plugin_path.is_dir()
    assert (bootstrap.server_path / "packetlimitconfig.json").is_file()
    if original:
        assert config["commands"]["log"] is False
        assert original in bootstrap.config_path.read_text(encoding="utf-8")


@pytest.mark.parametrize(
    "original",
    [
        "[mob-ai]\n",
        "[mob-ai]\nenabled = true\n",
        "[mob-ai.idle]\n# No idle settings yet\n",
        "mob-ai = {}\n",
        "[mob-ai]\nidle = {}\ntarget-search = {}\nvillagers = {}\n",
    ],
)
def test_prepare_fills_missing_or_empty_global_tables(bootstrap, original):
    bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    ai = read_config(bootstrap)["mob-ai"]
    assert ai.unwrap() == {**DEFAULT_MOB_AI, "enabled": "enabled = true" in original}
    if "# No idle settings yet" in original:
        assert "# No idle settings yet" in bootstrap.config_path.read_text(encoding="utf-8")


def test_prepare_preserves_overrides_and_independent_global_values(bootstrap):
    original = (
        "# Main config\n[commands]\nlog = false # Admin choice\n\n"
        "[mob-ai]\nenabled = true # Already enabled\n\n"
        "[mob-ai.idle]\nenabled = false\nplayer-range-blocks = 16\ndisable-random-stroll = false\n\n"
        "[mob-ai.target-search]\nenabled = true\nplayer-range-blocks = 48\ninterval-ticks = 40\n\n"
        "[mob-ai.villagers]\nprevent-sleep = true\n\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:cow', 'minecraft:pig']\n"
        "[mob-ai.overrides.idle]\nplayer-range-blocks = 8 # Keep this\n\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:skeleton']\n"
        "[mob-ai.overrides.target-search]\ninterval-ticks = 80\n"
    )
    bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == {
        "enabled": True,
        "idle": {
            "enabled": False,
            "player-range-blocks": 16,
            "disable-random-stroll": False,
            "disable-random-look-around": True,
        },
        "target-search": {"enabled": True, "player-range-blocks": 48, "interval-ticks": 40},
        "villagers": {"prevent-sleep": True},
        "overrides": [
            {"types": ["minecraft:cow", "minecraft:pig"], "idle": {"player-range-blocks": 8}},
            {"types": ["minecraft:skeleton"], "target-search": {"interval-ticks": 80}},
        ],
    }
    text = bootstrap.config_path.read_text(encoding="utf-8")
    assert "# Main config" in text
    assert "log = false # Admin choice" in text
    assert "enabled = true # Already enabled" in text
    assert "player-range-blocks = 8 # Keep this" in text


@pytest.mark.parametrize("value", ["false", "17", "'invalid'", "[]", "[{name = 'invalid'}]"])
def test_prepare_preserves_non_table_mob_ai(bootstrap, value):
    bootstrap.config_path.write_text(f"mob-ai = {value} # Keep invalid value\n", encoding="utf-8")

    bootstrap._prepare()

    assert read_config(bootstrap)["mob-ai"] == tomlkit.parse(f"value = {value}")["value"]
    assert f"mob-ai = {value} # Keep invalid value" in bootstrap.config_path.read_text(encoding="utf-8")


@pytest.mark.parametrize("value", ["false", "17", "'invalid'", "[]", "[{name = 'invalid'}]", "1.5"])
def test_prepare_preserves_invalid_feature_values(bootstrap, value):
    bootstrap.config_path.write_text(
        f"[mob-ai]\nenabled = 'invalid'\nidle = {value} # Keep invalid feature\n\n"
        "[mob-ai.target-search]\nenabled = 'invalid'\nplayer-range-blocks = 'invalid'\ninterval-ticks = 'invalid'\n\n"
        "[mob-ai.villagers]\nprevent-sleep = 'invalid'\n",
        encoding="utf-8",
    )

    bootstrap._prepare()

    ai = read_config(bootstrap)["mob-ai"]
    assert ai["enabled"] == "invalid"
    assert ai["idle"] == tomlkit.parse(f"value = {value}")["value"]
    assert ai["target-search"].unwrap() == {
        "enabled": "invalid",
        "player-range-blocks": "invalid",
        "interval-ticks": "invalid",
    }
    assert ai["villagers"]["prevent-sleep"] == "invalid"
    assert f"idle = {value} # Keep invalid feature" in bootstrap.config_path.read_text(encoding="utf-8")


@pytest.mark.parametrize("original", [None, "# Main config\n[commands]\nlog = false # Keep private\n"])
def test_prepare_preserves_ignored_legacy_section_and_file(bootstrap, original):
    old_main = (
        "# Old main preferences\n[entity-ai]\nenabled = true # Old switch\nconfig-version = 7\n\n"
        "[entity-ai.policies.animals]\ntypes = ['minecraft:sheep']\nautomatic = true\nactivation-range = 16\n"
    )
    bootstrap.config_path.write_text((original or "") + old_main, encoding="utf-8")
    legacy = bootstrap.server_path / "entity-ai.toml"
    old = (
        "# Legacy preferences\nconfig-version = 1 # Original version\n\n"
        "[policies.common-mobs]\nname = 'Legacy' # Original name\n"
        "types = ['minecraft:sheep', 'minecraft:chicken']\nautomatic = true\nactivation-range = 16\n\n"
        "[policies.zombies]\ntypes = ['minecraft:zombie']\nsearch-interval-ticks = 40\n"
    )
    legacy.write_text(old, encoding="utf-8")
    legacy_bytes = legacy.read_bytes()

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert config["entity-ai"].unwrap() == tomlkit.parse(old_main)["entity-ai"].unwrap()
    text = bootstrap.config_path.read_text(encoding="utf-8")
    assert old_main in text
    assert "# Legacy preferences" not in text
    assert legacy.read_bytes() == legacy_bytes
    if original:
        assert read_config(bootstrap)["commands"]["log"] is False
        assert "log = false # Keep private" in text


@pytest.mark.parametrize("legacy_text", ["", "config-version = 1\n[policies]\n"])
def test_prepare_ignores_empty_legacy_files(bootstrap, legacy_text):
    legacy = bootstrap.server_path / "entity-ai.toml"
    legacy.write_text(legacy_text, encoding="utf-8")
    before = legacy.read_bytes()

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert "entity-ai" not in config
    assert legacy.read_bytes() == before


def test_prepare_preserves_unknown_fields_and_invalid_overrides(bootstrap):
    bootstrap.config_path.write_text(
        "[mob-ai]\nenabled = true\nunknown-option = 'keep'\noverrides = false # Invalid, not repaired\n"
        "idle = {broken = false} # Keep invalid field\n",
        encoding="utf-8",
    )
    legacy = bootstrap.server_path / "entity-ai.toml"
    old = "config-version = 'broken'\npolicies = false # Invalid, not repaired\n"
    legacy.write_text(old, encoding="utf-8")

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == {
        **DEFAULT_MOB_AI,
        "enabled": True,
        "idle": {**DEFAULT_MOB_AI["idle"], "broken": False},
        "unknown-option": "keep",
        "overrides": False,
    }
    assert "entity-ai" not in config
    assert "# Invalid, not repaired" in bootstrap.config_path.read_text(encoding="utf-8")
    assert "# Keep invalid field" in bootstrap.config_path.read_text(encoding="utf-8")
    assert legacy.read_text(encoding="utf-8") == old


@pytest.mark.parametrize("main_exists", [False, True])
def test_prepare_preserves_main_values_and_is_idempotent_with_legacy_file(bootstrap, main_exists):
    if main_exists:
        bootstrap.config_path.write_text(
            "# Main switch\n[commands]\nlog = false\n\n[mob-ai]\nenabled = true # Keep enabled\n\n"
            "[mob-ai.target-search]\nenabled = false\n\n"
            "[[mob-ai.overrides]]\ntypes = ['minecraft:zombie']\n"
            "[mob-ai.overrides.target-search]\ninterval-ticks = 40\n",
            encoding="utf-8",
        )
    legacy = bootstrap.server_path / "entity-ai.toml"
    legacy.write_text(
        "# Legacy switch\nenabled = true # Keep enabled\nconfig-version = 1\n"
        "[policies.zombies]\ntypes = ['minecraft:zombie']\nsearch-interval-ticks = 40\n",
        encoding="utf-8",
    )
    legacy_before = legacy.read_bytes()

    bootstrap._prepare()

    config = read_config(bootstrap)
    expected = dict(DEFAULT_MOB_AI)
    if main_exists:
        expected.update(
            {
                "enabled": True,
                "target-search": {"enabled": False, "player-range-blocks": 32, "interval-ticks": 20},
                "overrides": [{"types": ["minecraft:zombie"], "target-search": {"interval-ticks": 40}}],
            },
        )
    assert config["mob-ai"].unwrap() == expected
    assert "entity-ai" not in config
    text = bootstrap.config_path.read_text(encoding="utf-8")
    assert "# Legacy switch" not in text
    if main_exists:
        assert config["commands"]["log"] is False
        assert "# Main switch" in text
        assert "enabled = true # Keep enabled" in text
    prepared = bootstrap.config_path.read_bytes()
    timestamp = 1_600_000_000_000_000_000
    os.utime(bootstrap.config_path, ns=(timestamp, timestamp))

    bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == prepared
    assert bootstrap.config_path.stat().st_mtime_ns == timestamp
    assert legacy.read_bytes() == legacy_before


@pytest.mark.parametrize("section", ["[mob-ai]\nenabled = false\n", "mob-ai = false\n"])
def test_prepare_ignores_malformed_legacy_and_preserves_world_files(bootstrap, section):
    bootstrap.config_path.write_text(section, encoding="utf-8")
    legacy = bootstrap.server_path / "entity-ai.toml"
    legacy.write_text("not = [valid TOML", encoding="utf-8")
    old = legacy.read_bytes()
    world = bootstrap.server_path / "worlds" / "Bedrock level"
    world.mkdir(parents=True)
    world_data = {
        "endstone-world-id": b"5e915730-296e-4568-8555-c96f31d10c7d\n",
        "entity-ai-bindings.json": b'{"version":1,"world":"5e915730-296e-4568-8555-c96f31d10c7d","groups":[]}\n',
    }
    for name, data in world_data.items():
        (world / name).write_bytes(data)

    bootstrap._prepare()

    config = read_config(bootstrap)
    if section.startswith("[mob-ai]"):
        assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    else:
        assert config["mob-ai"] is False
    assert "entity-ai" not in config
    assert legacy.read_bytes() == old
    assert {path.name: path.read_bytes() for path in world.iterdir()} == world_data


@pytest.mark.parametrize(
    "legacy_text",
    [
        "not = [valid TOML",
        "enabled = false\n",
        "enabled = 1\n",
        "enabled = 1.0\n",
        "enabled = 'true'\n",
        "[enabled]\nvalue = true\n",
    ],
)
@pytest.mark.parametrize("main_exists", [False, True])
def test_prepare_ignores_unusable_legacy_without_importing(bootstrap, legacy_text, main_exists):
    original = "# Keep main\n[commands]\nlog = false\n"
    if main_exists:
        bootstrap.config_path.write_text(original, encoding="utf-8")
    legacy = bootstrap.server_path / "entity-ai.toml"
    legacy.write_text(legacy_text, encoding="utf-8")
    legacy_before = legacy.read_bytes()

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert "entity-ai" not in config
    if main_exists:
        assert config["commands"]["log"] is False
        assert original in bootstrap.config_path.read_text(encoding="utf-8")
    assert legacy.read_bytes() == legacy_before


def test_prepare_rejects_invalid_main_without_rewriting(bootstrap):
    bootstrap.config_path.write_bytes(b"# Keep main\r\nmob-ai = [broken\r\n")
    before = bootstrap.config_path.read_bytes()

    with pytest.raises(ValueError):
        bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == before


def test_prepare_does_not_rewrite_unchanged_config(bootstrap):
    bootstrap._prepare()
    before = bootstrap.config_path.read_bytes()
    timestamp = 1_600_000_000_000_000_000
    os.utime(bootstrap.config_path, ns=(timestamp, timestamp))

    bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == before
    assert bootstrap.config_path.stat().st_mtime_ns == timestamp


@pytest.mark.parametrize("main_exists", [False, True])
def test_prepare_replace_failure_preserves_main_and_cleans_tempfile(bootstrap, monkeypatch, main_exists):
    if main_exists:
        bootstrap.config_path.write_bytes(b"# Keep main\r\n[commands]\r\nlog = false\r\n")
    before = bootstrap.config_path.read_bytes() if main_exists else None
    legacy = bootstrap.server_path / "entity-ai.toml"
    legacy.write_text("config-version = 1\n", encoding="utf-8")
    legacy_before = legacy.read_bytes()
    attempts = []

    def reject_replace(src, dst):
        source, destination = Path(src), Path(dst)
        assert source.parent == destination.parent == bootstrap.server_path
        config = tomlkit.parse(source.read_text(encoding="utf-8"))
        assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
        assert "entity-ai" not in config
        attempts.append(config["mob-ai"]["enabled"])
        raise OSError("replace denied")

    monkeypatch.setattr(os, "replace", reject_replace)

    with pytest.raises(OSError, match="replace denied"):
        bootstrap._prepare()

    assert attempts == [False]
    assert bootstrap.config_path.exists() == main_exists
    if main_exists:
        assert bootstrap.config_path.read_bytes() == before
    assert legacy.read_bytes() == legacy_before
    assert {path.name for path in bootstrap.server_path.iterdir()} == (
        {"plugins", "entity-ai.toml", "endstone.toml"} if main_exists else {"plugins", "entity-ai.toml"}
    )


@pytest.mark.parametrize("changed", [False, True])
def test_prepare_preserves_relative_symlink_and_updates_its_target(bootstrap, monkeypatch, changed):
    if changed:
        bootstrap.config_path.write_text("# Keep target\n[commands]\nlog = false\n", encoding="utf-8")
    else:
        bootstrap._prepare()
    storage = bootstrap.server_path / "config-storage"
    storage.mkdir()
    target = storage / "server.toml"
    bootstrap.config_path.rename(target)
    bootstrap.config_path.symlink_to(Path("config-storage") / target.name)
    link_before = bootstrap.config_path.lstat()
    target_before = target.stat()
    content_before = target.read_bytes()
    replacements = []
    real_replace = os.replace

    def replace(source, destination):
        assert Path(source).parent == target.parent
        assert destination == target
        replacements.append(destination)
        real_replace(source, destination)

    monkeypatch.setattr(os, "replace", replace)

    bootstrap._prepare()

    assert bootstrap.config_path.is_symlink()
    assert os.readlink(bootstrap.config_path) == str(Path("config-storage") / target.name)
    assert bootstrap.config_path.lstat().st_ino == link_before.st_ino
    assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert replacements == ([target] if changed else [])
    if changed:
        assert target.read_bytes().startswith(content_before)
        assert target.stat().st_ino != target_before.st_ino
    else:
        assert target.read_bytes() == content_before
        assert target.stat().st_ino == target_before.st_ino
        assert target.stat().st_mtime_ns == target_before.st_mtime_ns
    assert {path.name for path in storage.iterdir()} == {target.name}


@pytest.mark.parametrize("parent_exists", [False, True])
def test_prepare_dangling_relative_symlink(bootstrap, parent_exists):
    storage = bootstrap.server_path / "config-storage"
    if parent_exists:
        storage.mkdir()
    target = storage / "server.toml"
    bootstrap.config_path.symlink_to(Path("config-storage") / target.name)
    link_before = bootstrap.config_path.lstat()

    if parent_exists:
        bootstrap._prepare()
        assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI
        if os.name == "posix":
            assert stat.S_IMODE(target.stat().st_mode) == 0o600
    else:
        with pytest.raises(FileNotFoundError):
            bootstrap._prepare()
        assert not target.exists()
        assert not (bootstrap.server_path / "packetlimitconfig.json").exists()
    assert bootstrap.config_path.is_symlink()
    assert os.readlink(bootstrap.config_path) == str(Path("config-storage") / target.name)
    assert bootstrap.config_path.lstat().st_ino == link_before.st_ino


def test_prepare_rejects_cyclic_symlink_without_replacing_it(bootstrap):
    second = bootstrap.server_path / "loop.toml"
    bootstrap.config_path.symlink_to(second.name)
    second.symlink_to(bootstrap.config_path.name)

    with pytest.raises((OSError, RuntimeError)):
        bootstrap._prepare()

    assert os.readlink(bootstrap.config_path) == second.name
    assert os.readlink(second) == bootstrap.config_path.name
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "loop.toml", "plugins"}


def test_prepare_rejects_nonregular_config(bootstrap):
    bootstrap.config_path.mkdir()

    with pytest.raises(OSError, match="must be a regular file"):
        bootstrap._prepare()

    assert bootstrap.config_path.is_dir()
    assert list(bootstrap.config_path.iterdir()) == []
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins"}


@pytest.mark.skipif(os.name != "posix" or os.geteuid() == 0, reason="POSIX parent access requires a non-root user")
def test_prepare_inaccessible_target_parent_preserves_symlink(bootstrap):
    storage = bootstrap.server_path / "config-storage"
    storage.mkdir()
    target = storage / "server.toml"
    target.write_bytes(b"# Keep inaccessible target\n")
    original = file_state(target)
    bootstrap.config_path.symlink_to(Path("config-storage") / target.name)
    link_before = bootstrap.config_path.lstat()
    mode = stat.S_IMODE(storage.stat().st_mode)
    storage.chmod(0)
    try:
        with pytest.raises(PermissionError):
            bootstrap._prepare()
    finally:
        storage.chmod(mode)

    assert file_state(target) == original
    assert bootstrap.config_path.is_symlink()
    assert bootstrap.config_path.lstat().st_ino == link_before.st_ino
    assert not (bootstrap.server_path / "packetlimitconfig.json").exists()
    assert {path.name for path in storage.iterdir()} == {target.name}


def test_prepare_replace_failure_preserves_relative_symlink_and_target(bootstrap, monkeypatch):
    storage = bootstrap.server_path / "config-storage"
    storage.mkdir()
    target = storage / "server.toml"
    target.write_bytes(b"# Keep failed target\n")
    original = file_state(target)
    bootstrap.config_path.symlink_to(Path("config-storage") / target.name)
    link_before = bootstrap.config_path.lstat()
    error = OSError("replace target denied")

    def reject_replace(source, destination):
        assert Path(source).parent == target.parent
        assert destination == target
        assert tomlkit.parse(Path(source).read_text(encoding="utf-8"))["mob-ai"].unwrap() == DEFAULT_MOB_AI
        raise error

    monkeypatch.setattr(os, "replace", reject_replace)

    with pytest.raises(OSError) as raised:
        bootstrap._prepare()

    assert raised.value is error
    assert file_state(target) == original
    assert bootstrap.config_path.is_symlink()
    assert os.readlink(bootstrap.config_path) == str(Path("config-storage") / target.name)
    assert bootstrap.config_path.lstat().st_ino == link_before.st_ino
    assert not (bootstrap.server_path / "packetlimitconfig.json").exists()
    assert {path.name for path in storage.iterdir()} == {target.name}


@pytest.mark.parametrize("changed", [False, True])
def test_prepare_hardlinks_are_unchanged_or_rejected_before_staging(bootstrap, monkeypatch, changed):
    if changed:
        bootstrap.config_path.write_text("# Keep hardlink\n[commands]\nlog = false\n", encoding="utf-8")
    else:
        bootstrap._prepare()
    related = bootstrap.server_path / "related.toml"
    os.link(bootstrap.config_path, related)
    before = bootstrap.config_path.stat()
    content = bootstrap.config_path.read_bytes()

    def unexpected_staging(*args, **kwargs):
        pytest.fail("Hardlinked configuration must not be staged")

    monkeypatch.setattr("endstone.cli.base.tempfile.NamedTemporaryFile", unexpected_staging)
    if changed:
        with pytest.raises(OSError, match="multiple hard links"):
            bootstrap._prepare()
        assert not (bootstrap.server_path / "packetlimitconfig.json").exists()
    else:
        bootstrap._prepare()
    for path in [bootstrap.config_path, related]:
        assert path.read_bytes() == content
        assert path.stat().st_ino == before.st_ino
        assert path.stat().st_nlink == before.st_nlink == 2
        assert path.stat().st_mtime_ns == before.st_mtime_ns
        assert path.stat().st_mode == before.st_mode


@pytest.mark.skipif(os.name != "posix", reason="POSIX ownership and permissions")
@pytest.mark.parametrize("mode", [0o640, 0o6750])
def test_prepare_preserves_posix_ownership_and_mode(bootstrap, monkeypatch, mode):
    bootstrap.config_path.write_text("# Keep permissions\n", encoding="utf-8")
    bootstrap.config_path.chmod(mode)
    before = bootstrap.config_path.stat()

    def unexpected_chown(*args):
        pytest.fail("Matching ownership must not require chown")

    monkeypatch.setattr(os, "chown", unexpected_chown)

    bootstrap._prepare()

    after = bootstrap.config_path.stat()
    assert (after.st_uid, after.st_gid, stat.S_IMODE(after.st_mode)) == (before.st_uid, before.st_gid, mode)
    assert bootstrap.config_path.read_text(encoding="utf-8").startswith("# Keep permissions\n")
    assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI


@pytest.mark.skipif(os.name != "posix", reason="POSIX ownership")
def test_prepare_ownership_failure_keeps_original_and_removes_staging(bootstrap, monkeypatch):
    bootstrap.config_path.write_bytes(b"# Keep ownership\n")
    before = bootstrap.config_path.stat()
    original = file_state(bootstrap.config_path)
    real_stat = Path.stat
    requested = []
    error = PermissionError("ownership denied")

    def target_stat(path, *args, **kwargs):
        info = real_stat(path, *args, **kwargs)
        if path == bootstrap.config_path:
            fields = list(info)
            fields[stat.ST_UID] += 1
            return os.stat_result(fields)
        return info

    def reject_chown(path, uid, gid):
        requested.append((Path(path), uid, gid))
        raise error

    with monkeypatch.context() as patch:
        patch.setattr(Path, "stat", target_stat)
        patch.setattr(os, "chown", reject_chown)
        with pytest.raises(PermissionError) as raised:
            bootstrap._prepare()
    assert raised.value is error
    assert len(requested) == 1
    assert requested[0][1:] == (before.st_uid + 1, before.st_gid)
    assert not requested[0][0].exists()
    assert file_state(bootstrap.config_path) == original
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins"}


@pytest.mark.skipif(os.name != "posix", reason="POSIX metadata verification")
def test_prepare_verifies_metadata_before_replacing(bootstrap, monkeypatch):
    bootstrap.config_path.write_bytes(b"# Keep mode\n")
    bootstrap.config_path.chmod(0o640)
    original = file_state(bootstrap.config_path)
    monkeypatch.setattr(os, "chmod", lambda *args: None)

    with pytest.raises(OSError, match="Unable to preserve configuration file metadata"):
        bootstrap._prepare()

    assert file_state(bootstrap.config_path) == original
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins"}


@pytest.mark.parametrize("stage", ["write", "close", "replace"])
def test_prepare_cleanup_failure_preserves_primary_and_reports_residual(bootstrap, monkeypatch, caplog, stage):
    module = importlib.import_module("endstone.cli.base")
    bootstrap.config_path.write_bytes(b"# Keep on failure\n")
    original = file_state(bootstrap.config_path)
    primary = OSError(f"{stage} denied")
    real_temporary = module.tempfile.NamedTemporaryFile
    real_unlink = Path.unlink
    staged_paths = []

    class FailingTemporary:
        def __init__(self, **kwargs):
            self.file = real_temporary(**kwargs)
            self.name = self.file.name
            staged_paths.append(Path(self.name))

        def __enter__(self):
            self.file.__enter__()
            return self

        def write(self, value):
            if stage == "write":
                self.file.write(value[:10])
                raise primary
            return self.file.write(value)

        def __exit__(self, *args):
            self.file.__exit__(*args)
            if stage == "close":
                raise primary

    def reject_replace(*args):
        raise primary

    def reject_cleanup(path, *args, **kwargs):
        if path in staged_paths:
            raise PermissionError("cleanup denied")
        return real_unlink(path, *args, **kwargs)

    with monkeypatch.context() as patch:
        patch.setattr(module.tempfile, "NamedTemporaryFile", FailingTemporary)
        patch.setattr(os, "replace", reject_replace)
        patch.setattr(Path, "unlink", reject_cleanup)
        with pytest.raises(OSError) as raised:
            bootstrap._prepare()

    assert raised.value is primary
    assert file_state(bootstrap.config_path) == original
    assert len(staged_paths) == 1
    residual = staged_paths[0]
    assert residual.is_file()
    assert residual.read_text(encoding="utf-8").startswith("# Keep on ")
    assert str(residual) in caplog.text
    assert "cleanup denied" in caplog.text
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins", residual.name}


def test_prepare_success_does_not_attempt_cleanup(bootstrap, monkeypatch):
    staged_paths = []
    real_replace = os.replace
    real_unlink = Path.unlink

    def replace(source, destination):
        staged_paths.append(Path(source))
        real_replace(source, destination)

    def unlink(path, *args, **kwargs):
        assert path not in staged_paths, "Successful replacement already consumed the staged file"
        return real_unlink(path, *args, **kwargs)

    with monkeypatch.context() as patch:
        patch.setattr(os, "replace", replace)
        patch.setattr(Path, "unlink", unlink)
        bootstrap._prepare()

    assert len(staged_paths) == 1
    assert not staged_paths[0].exists()
    assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI
