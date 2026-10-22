add_rules("mode.debug", "mode.release")

set_plat("mingw")

add_requires("gtest")

local function configure_cpp26_target()
    set_languages("c++latest")
    add_includedirs("src")
    -- GCC 16 enables the C++26 reflection implementation explicitly.
    add_cxxflags("-freflection", {force = true})
    -- GCC 16 provides std::print terminal support in libstdc++exp.
    add_syslinks("stdc++exp")
end

target("srl")
    set_kind("binary")
    add_files("src/main.cpp")
    configure_cpp26_target()

target("srl.tests")
    set_default(false)
    set_kind("binary")
    add_files("tests/*.cpp")
    add_includedirs("tests")
    add_packages("gtest")
    configure_cpp26_target()
