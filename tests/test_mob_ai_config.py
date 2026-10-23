import importlib
import os

import pytest
import tomlkit


@pytest.fixture
def bootstrap(tmp_path):
    cls = importlib.import_module("endstone.cli.base").Bootstrap
    return cls(str(tmp_path), no_confirm=True, remote="", interactive=False)


def read_config(bootstrap):
    return tomlkit.parse(bootstrap.config_path.read_text(encoding="utf-8"))


def test_prepare_adds_disabled_defaults(bootstrap):
    bootstrap._prepare()

    config = read_config(bootstrap)["mob-ai"]
    assert config["enabled"] is False
    assert config["villagers"]["sleep-failure-skip-night"] is False


def test_prepare_preserves_user_values_comments_and_repeated_setup(bootstrap):
    original = (
        "# Admin config\n[mob-ai]\nenabled = true\nunknown = 'keep'\n"
        "[mob-ai.idle]\nenabled = false\n"
        "[[mob-ai.overrides]]\ntypes = ['minecraft:cow']\n"
        "[mob-ai.overrides.idle]\nplayer-range-blocks = 8\n"
    )
    bootstrap.config_path.write_text(original, encoding="utf-8")

    bootstrap._prepare()

    config = read_config(bootstrap)["mob-ai"]
    assert config["enabled"] is True
    assert config["idle"]["enabled"] is False
    assert config["idle"]["player-range-blocks"] == 32
    assert config["unknown"] == "keep"
    assert config["overrides"].unwrap() == [{"types": ["minecraft:cow"], "idle": {"player-range-blocks": 8}}]
    assert "# Admin config" in bootstrap.config_path.read_text(encoding="utf-8")
    prepared = bootstrap.config_path.read_bytes()
    timestamp = 1_600_000_000_000_000_000
    os.utime(bootstrap.config_path, ns=(timestamp, timestamp))

    bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == prepared
    assert bootstrap.config_path.stat().st_mtime_ns == timestamp


def test_prepare_replace_failure_preserves_main_and_cleans_tempfile(bootstrap, monkeypatch):
    original = b"# Keep main\n[commands]\nlog = false\n"
    bootstrap.config_path.write_bytes(original)
    temporary_paths = []

    def reject_replace(source, destination):
        temporary_paths.append(source)
        raise OSError("replace denied")

    monkeypatch.setattr(os, "replace", reject_replace)

    with pytest.raises(OSError, match="replace denied"):
        bootstrap._prepare()

    assert bootstrap.config_path.read_bytes() == original
    assert not os.path.exists(temporary_paths[0])
