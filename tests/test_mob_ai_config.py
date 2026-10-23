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
    "villagers": {"sleep-failure-skip-night": False},
}


@pytest.fixture
def bootstrap(tmp_path):
    cls = importlib.import_module("endstone.cli.base").Bootstrap
    return cls(str(tmp_path), no_confirm=True, remote="", interactive=False)


def read_config(bootstrap):
    return tomlkit.parse(bootstrap.config_path.read_text(encoding="utf-8"))


@pytest.mark.parametrize("original", [None, "# My server\n[commands]\nlog = false # Keep private\n"])
def test_prepare_adds_disabled_recommendations(bootstrap, original):
    if original is not None:
        bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    config = read_config(bootstrap)
    assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert bootstrap.plugin_path.is_dir()
    assert (bootstrap.server_path / "packetlimitconfig.json").is_file()
    if original is not None:
        assert config["commands"]["log"] is False
        assert original in bootstrap.config_path.read_text(encoding="utf-8")
    elif os.name == "posix":
        assert stat.S_IMODE(bootstrap.config_path.stat().st_mode) == 0o600


def test_prepare_preserves_overrides_and_independent_global_values(bootstrap):
    original = (
        "# Main config\n[commands]\nlog = false # Admin choice\n\n"
        "[mob-ai]\nenabled = true # Already enabled\n\n"
        "[mob-ai.idle]\nenabled = false\nplayer-range-blocks = 16\ndisable-random-stroll = false\n\n"
        "[mob-ai.target-search]\nenabled = true\nplayer-range-blocks = 48\ninterval-ticks = 40\n\n"
        "[mob-ai.villagers]\nsleep-failure-skip-night = true # Keep night policy\n\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:cow', 'minecraft:pig']\n"
        "[mob-ai.overrides.idle]\nplayer-range-blocks = 8 # Keep this\n\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:skeleton']\n"
        "[mob-ai.overrides.target-search]\ninterval-ticks = 80\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:villager_v2']\n"
        "[mob-ai.overrides.villagers]\nsleep-failure-skip-night = false\n"
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
        "villagers": {"sleep-failure-skip-night": True},
        "overrides": [
            {"types": ["minecraft:cow", "minecraft:pig"], "idle": {"player-range-blocks": 8}},
            {"types": ["minecraft:skeleton"], "target-search": {"interval-ticks": 80}},
            {
                "types": ["minecraft:villager_v2"],
                "villagers": {"sleep-failure-skip-night": False},
            },
        ],
    }
    text = bootstrap.config_path.read_text(encoding="utf-8")
    assert "# Main config" in text
    assert "log = false # Admin choice" in text
    assert "enabled = true # Already enabled" in text
    assert "player-range-blocks = 8 # Keep this" in text
    assert "sleep-failure-skip-night = true # Keep night policy" in text
    prepared = bootstrap.config_path.read_bytes()
    timestamp = 1_600_000_000_000_000_000
    os.utime(bootstrap.config_path, ns=(timestamp, timestamp))

    bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == prepared
    assert bootstrap.config_path.stat().st_mtime_ns == timestamp


@pytest.mark.parametrize(
    ("original", "expected"),
    [
        ("mob-ai = false # Keep invalid root\n", False),
        (
            (
                "[mob-ai]\nenabled = 'invalid'\nidle = false # Keep invalid feature\n"
                "unknown-option = 'keep'\noverrides = false # Keep invalid overrides\n"
            ),
            {
                **DEFAULT_MOB_AI,
                "enabled": "invalid",
                "idle": False,
                "unknown-option": "keep",
                "overrides": False,
            },
        ),
    ],
)
def test_prepare_preserves_invalid_user_values(bootstrap, original, expected):
    bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    assert read_config(bootstrap).unwrap()["mob-ai"] == expected
    assert original in bootstrap.config_path.read_text(encoding="utf-8")


def test_prepare_rejects_invalid_main_without_rewriting(bootstrap):
    bootstrap.config_path.write_bytes(b"# Keep main\r\nmob-ai = [broken\r\n")
    before = bootstrap.config_path.read_bytes()

    with pytest.raises(ValueError):
        bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == before


def test_prepare_replace_failure_preserves_main_and_cleans_tempfile(bootstrap, monkeypatch):
    original = b"# Keep main\r\n[commands]\r\nlog = false\r\n"
    bootstrap.config_path.write_bytes(original)

    def reject_replace(src, dst):
        source, destination = Path(src), Path(dst)
        assert source.parent == destination.parent == bootstrap.server_path
        assert destination == bootstrap.config_path
        assert destination.read_bytes() == original
        config = tomlkit.parse(source.read_text(encoding="utf-8"))
        assert config["commands"]["log"] is False
        assert config["mob-ai"].unwrap() == DEFAULT_MOB_AI
        raise OSError("replace denied")

    monkeypatch.setattr(os, "replace", reject_replace)

    with pytest.raises(OSError, match="replace denied"):
        bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == original
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"plugins", "endstone.toml"}


def test_prepare_preserves_relative_symlink_and_updates_its_target(bootstrap):
    storage = bootstrap.server_path / "config-storage"
    storage.mkdir()
    target = storage / "server.toml"
    original = "# Keep target\n[commands]\nlog = false\n"
    target.write_text(original, encoding="utf-8")
    relative_target = Path("config-storage") / target.name
    bootstrap.config_path.symlink_to(relative_target)
    link_before = bootstrap.config_path.lstat()
    target_before = target.stat()

    bootstrap._prepare()

    assert bootstrap.config_path.is_symlink()
    assert os.readlink(bootstrap.config_path) == str(relative_target)
    assert bootstrap.config_path.lstat().st_ino == link_before.st_ino
    assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI
    assert target.read_text(encoding="utf-8").startswith(original)
    assert target.stat().st_ino != target_before.st_ino
    assert {path.name for path in storage.iterdir()} == {target.name}


def test_prepare_hardlinks_are_rejected_without_modification(bootstrap):
    original = b"# Keep hardlink\n[commands]\nlog = false\n"
    bootstrap.config_path.write_bytes(original)
    related = bootstrap.server_path / "related.toml"
    os.link(bootstrap.config_path, related)
    before = bootstrap.config_path.stat()

    with pytest.raises(OSError, match="multiple hard links"):
        bootstrap._prepare()

    for path in [bootstrap.config_path, related]:
        assert path.read_bytes() == original
        assert path.stat().st_ino == before.st_ino
        assert path.stat().st_nlink == 2


@pytest.mark.skipif(os.name != "posix", reason="POSIX ownership and permissions")
def test_prepare_preserves_posix_ownership_and_mode(bootstrap):
    bootstrap.config_path.write_text("# Keep permissions\n", encoding="utf-8")
    bootstrap.config_path.chmod(0o6750)
    before = bootstrap.config_path.stat()

    bootstrap._prepare()

    after = bootstrap.config_path.stat()
    assert (after.st_uid, after.st_gid, stat.S_IMODE(after.st_mode)) == (before.st_uid, before.st_gid, 0o6750)
    assert bootstrap.config_path.read_text(encoding="utf-8").startswith("# Keep permissions\n")
    assert read_config(bootstrap)["mob-ai"].unwrap() == DEFAULT_MOB_AI


@pytest.mark.skipif(os.name != "posix", reason="POSIX metadata verification")
def test_prepare_verifies_metadata_before_replacing(bootstrap, monkeypatch):
    original = b"# Keep mode\n"
    bootstrap.config_path.write_bytes(original)
    bootstrap.config_path.chmod(0o640)
    before = bootstrap.config_path.stat()
    monkeypatch.setattr(os, "chmod", lambda *args: None)

    with pytest.raises(OSError, match="Unable to preserve configuration file metadata"):
        bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == original
    after = bootstrap.config_path.stat()
    assert (after.st_ino, after.st_mode, after.st_uid, after.st_gid, after.st_mtime_ns) == (
        before.st_ino,
        before.st_mode,
        before.st_uid,
        before.st_gid,
        before.st_mtime_ns,
    )
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins"}


def test_prepare_cleanup_failure_preserves_primary_and_reports_residual(bootstrap, monkeypatch, caplog):
    original = b"# Keep on failure\n"
    bootstrap.config_path.write_bytes(original)
    primary = OSError("replace denied")
    staged_paths = []
    real_unlink = Path.unlink

    def reject_replace(source, destination):
        staged_paths.append(Path(source))
        raise primary

    def reject_cleanup(path, *args, **kwargs):
        if path in staged_paths:
            raise PermissionError("cleanup denied")
        return real_unlink(path, *args, **kwargs)

    with monkeypatch.context() as patch:
        patch.setattr(os, "replace", reject_replace)
        patch.setattr(Path, "unlink", reject_cleanup)
        with pytest.raises(OSError) as raised:
            bootstrap._prepare()

    assert raised.value is primary
    assert bootstrap.config_path.read_bytes() == original
    residual = staged_paths[0]
    assert residual.is_file()
    assert residual.read_text(encoding="utf-8").startswith("# Keep on failure\n")
    assert str(residual) in caplog.text
    assert "cleanup denied" in caplog.text
    assert {path.name for path in bootstrap.server_path.iterdir()} == {"endstone.toml", "plugins", residual.name}
