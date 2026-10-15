import os
import shutil

from endstone.cli.base import Bootstrap


def test_runtime_keeps_external_tools_on_path(tmp_path, monkeypatch):
    tools = tmp_path / "compiler-tools"
    tools.mkdir()
    executable = tools / ("test-compiler.exe" if os.name == "nt" else "test-compiler")
    executable.write_bytes(b"")
    executable.chmod(0o755)
    monkeypatch.setenv("PATH", str(tools))
    bootstrap = Bootstrap(str(tmp_path), no_confirm=True, remote="", interactive=False)

    environment = bootstrap._endstone_runtime_env

    assert shutil.which(executable.name, path=environment["PATH"]) == str(executable)
