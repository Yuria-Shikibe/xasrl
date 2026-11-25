add_rules("mode.debug", "mode.release")

set_plat("mingw")

add_requires("gtest")
add_requires("benchmark v1.9.5", {debug = false})
add_requires("bitsery v5.2.5", "cereal v1.3.2")

target("srl")
    set_kind("object")
    add_headerfiles("src/(srl/**.hpp)")
    add_includedirs("src", {public = true})
    set_languages("c++latest", {public = true})
    -- GCC 16 enables the C++26 reflection implementation explicitly.
    add_cxxflags("-freflection", {public = true, force = true})
    -- GCC 16 provides std::print terminal support in libstdc++exp.
    add_syslinks("stdc++exp", {public = true})

target("srl.example")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("srl")

target("srl.tests")
    set_default(false)
    set_kind("binary")
    add_defines("SRL_VALIDATE_ANNOTATION_TARGETS=1")
    add_files("tests/*.cpp")
    add_includedirs("tests")
    add_packages("gtest")
    add_deps("srl")

target("srl.benchmarks")
    set_default(false)
    set_kind("binary")
    -- Optimize this target even if the existing project configuration is debug.
    set_optimize("fastest")
    add_defines("NDEBUG")
    add_files("benchmarks/serialization.cpp")
    add_packages("benchmark", "bitsery", "cereal")
    add_deps("srl")
