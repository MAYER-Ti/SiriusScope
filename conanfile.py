from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class SiriusScopeConan(ConanFile):
    name = "siriusscope"
    version = "0.1"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    
    default_options = {
        "qt/*:qtdeclarative": True,
        "qt/*:qtshadertools": True,
        "qt/*:gui": True,
        "qt/*:widgets": True,
        "qt/*:opengl": "desktop",
        "qt/*:with_x11": True,
    }

    def requirements(self):
        self.requires("qt/6.10.1")

    def build_requirements(self):
        self.tool_requires("cmake/3.31.10")
        self.tool_requires("ninja/1.12.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.cache_variables["CMAKE_POLICY_DEFAULT_CMP0091"] = "NEW"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
