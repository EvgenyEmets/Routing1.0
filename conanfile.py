from conans import ConanFile, tools

class RoutingConan(ConanFile):
    name = "Routing"
    version = "0.1"
    settings = None
    description = "Routing app"
    url = "None"
    license = "None"
    author = "None"
    topics = None

    def package(self):
        self.copy("*")

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
