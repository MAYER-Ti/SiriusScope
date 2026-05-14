from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class SiriusScopeConan(ConanFile):
    name = "siriusscope"
    version = "0.1"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        # Qt, CMake, Ninja, and MinGW are intentionally provided by the local
        # Qt Installer / QtCreator kit. Conan only generates dependency and
        # toolchain metadata for third-party libraries managed by Conan.
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.cache_variables["CMAKE_POLICY_DEFAULT_CMP0091"] = "NEW"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
