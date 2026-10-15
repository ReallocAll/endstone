import os
import shutil
import sys
import sysconfig
from pathlib import Path
from types import SimpleNamespace
from zipfile import ZipFile

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


def test_installer_reuses_dependencies_from_current_python_path(tmp_path, monkeypatch):
    from endstone.plugin.plugin_loader import PythonPluginLoader

    prefix = tmp_path / "plugins"
    packages = Path(sysconfig.get_path("purelib", vars={"base": str(prefix), "platbase": str(prefix)}))
    dependency = packages / "prefix_only_dependency-1.0.dist-info"
    dependency.mkdir(parents=True)
    (dependency / "METADATA").write_text("Metadata-Version: 2.1\nName: prefix-only-dependency\nVersion: 1.0\n")
    monkeypatch.syspath_prepend(packages)
    monkeypatch.delenv("PYTHONPATH", raising=False)
    monkeypatch.setenv("PIP_NO_INDEX", "1")
    executable = Path(sys.prefix) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    monkeypatch.setattr(sys, "executable", str(executable))
    wheel = tmp_path / "endstone_prefix_test-1.0-py3-none-any.whl"
    metadata = "endstone_prefix_test-1.0.dist-info"
    with ZipFile(wheel, "w") as archive:
        archive.writestr(
            metadata + "/METADATA",
            "Metadata-Version: 2.1\nName: endstone-prefix-test\nVersion: 1.0\n"
            "Requires-Dist: prefix-only-dependency==1.0\n",
        )
        archive.writestr(metadata + "/WHEEL", "Wheel-Version: 1.0\nRoot-Is-Purelib: true\nTag: py3-none-any\n")
        archive.writestr(metadata + "/RECORD", "")
    loader = SimpleNamespace(_prefix=str(prefix))

    PythonPluginLoader.load_plugin(loader, str(wheel))

    assert (packages / metadata / "METADATA").is_file()
