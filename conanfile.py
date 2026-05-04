from conan import ConanFile


class SiriusScopeConan(ConanFile):
    name = "siriusscope"
    version = "0.1"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
