import glob
import os
import shutil
import subprocess
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        ext_fullpath = os.path.abspath(self.get_ext_fullpath(ext.name))
        extdir = os.path.dirname(ext_fullpath)
        os.makedirs(extdir, exist_ok=True)

        cmake_args = [
            f"-DPython3_EXECUTABLE={sys.executable}",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DQUANTENGINE_BUILD_TESTS=OFF",
            "-DQUANTENGINE_BUILD_BENCHMARKS=OFF",
            "-DQUANTENGINE_BUILD_CLI=OFF",
            "-DQUANTENGINE_BUILD_PYTHON=ON",
        ]

        build_temp = os.path.abspath(self.build_temp)
        os.makedirs(build_temp, exist_ok=True)

        subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=build_temp)
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", "Release", "--target", "quantengine", "-j"],
            cwd=build_temp,
        )

        built_sos = glob.glob(os.path.join(build_temp, "python", "quantengine*.so"))
        if not built_sos:
            built_sos = glob.glob(os.path.join(build_temp, "**", "quantengine*.so"), recursive=True)

        if not built_sos:
            raise RuntimeError(f"Failed to find compiled quantengine shared object in {build_temp}")

        shutil.copyfile(built_sos[0], ext_fullpath)

        pyi_source = os.path.join(ext.sourcedir, "bindings", "python", "quantengine.pyi")
        if os.path.exists(pyi_source):
            shutil.copyfile(pyi_source, os.path.join(extdir, "quantengine.pyi"))


setup(
    ext_modules=[CMakeExtension("quantengine")],
    cmdclass={"build_ext": CMakeBuild},
)
