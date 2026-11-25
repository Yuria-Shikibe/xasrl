// Run from the repository root with the same GCC selected by xmake:
// & C:/msys64/ucrt64/bin/x86_64-w64-mingw32-g++ -std=c++26 -freflection -O2 -Isrc benchmarks/enum_contains.cpp -o build/enum_contains_bench.exe -lstdc++exp
// & ./build/enum_contains_bench.exe
// This standalone optimized benchmark does not change the xmake debug configuration.
// Seven samples, 16 passes over 65,536 queries each; report median ns/query.
// sampled_hits is the percentage drawn from declared values; other queries are
// uniform over the bounding interval plus 32 on either side (and may also hit).
// Each candidate is checked against the original before timing. The checksum
// and compiler barrier keep queries observable. Run without concurrent builds.
// Width 0 means ranges only; other widths pack short runs/holes into bitmaps.
#include "srl/enum.hpp"
#include "srl/number_to_string.hpp"
#include <chrono>
#include <print>
#include <random>
#include <vector>
#include <array>
#include <stdexcept>

enum class continuous : int {
    v0 = -64, v1 = -63, v2 = -62, v3 = -61, v4 = -60, v5 = -59,
    v6 = -58, v7 = -57, v8 = -56, v9 = -55, v10 = -54, v11 = -53,
    v12 = -52, v13 = -51, v14 = -50, v15 = -49, v16 = -48, v17 = -47,
    v18 = -46, v19 = -45, v20 = -44, v21 = -43, v22 = -42, v23 = -41,
    v24 = -40, v25 = -39, v26 = -38, v27 = -37, v28 = -36, v29 = -35,
    v30 = -34, v31 = -33, v32 = -32, v33 = -31, v34 = -30, v35 = -29,
    v36 = -28, v37 = -27, v38 = -26, v39 = -25, v40 = -24, v41 = -23,
    v42 = -22, v43 = -21, v44 = -20, v45 = -19, v46 = -18, v47 = -17,
    v48 = -16, v49 = -15, v50 = -14, v51 = -13, v52 = -12, v53 = -11,
    v54 = -10, v55 = -9, v56 = -8, v57 = -7, v58 = -6, v59 = -5,
    v60 = -4, v61 = -3, v62 = -2, v63 = -1, v64 = 0, v65 = 1,
    v66 = 2, v67 = 3, v68 = 4, v69 = 5, v70 = 6, v71 = 7,
    v72 = 8, v73 = 9, v74 = 10, v75 = 11, v76 = 12, v77 = 13,
    v78 = 14, v79 = 15, v80 = 16, v81 = 17, v82 = 18, v83 = 19,
    v84 = 20, v85 = 21, v86 = 22, v87 = 23, v88 = 24, v89 = 25,
    v90 = 26, v91 = 27, v92 = 28, v93 = 29, v94 = 30, v95 = 31,
    v96 = 32, v97 = 33, v98 = 34, v99 = 35, v100 = 36, v101 = 37,
    v102 = 38, v103 = 39, v104 = 40, v105 = 41, v106 = 42, v107 = 43,
    v108 = 44, v109 = 45, v110 = 46, v111 = 47, v112 = 48, v113 = 49,
    v114 = 50, v115 = 51, v116 = 52, v117 = 53, v118 = 54, v119 = 55,
    v120 = 56, v121 = 57, v122 = 58, v123 = 59, v124 = 60, v125 = 61,
    v126 = 62, v127 = 63, v128 = 64, v129 = 65, v130 = 66, v131 = 67,
    v132 = 68, v133 = 69, v134 = 70, v135 = 71, v136 = 72, v137 = 73,
    v138 = 74, v139 = 75, v140 = 76, v141 = 77, v142 = 78, v143 = 79,
    v144 = 80, v145 = 81, v146 = 82, v147 = 83, v148 = 84, v149 = 85,
    v150 = 86, v151 = 87, v152 = 88, v153 = 89, v154 = 90, v155 = 91,
    v156 = 92, v157 = 93, v158 = 94, v159 = 95, v160 = 96, v161 = 97,
    v162 = 98, v163 = 99, v164 = 100, v165 = 101, v166 = 102, v167 = 103,
    v168 = 104, v169 = 105, v170 = 106, v171 = 107, v172 = 108, v173 = 109,
    v174 = 110, v175 = 111, v176 = 112, v177 = 113, v178 = 114, v179 = 115,
    v180 = 116, v181 = 117, v182 = 118, v183 = 119, v184 = 120, v185 = 121,
    v186 = 122, v187 = 123, v188 = 124, v189 = 125, v190 = 126, v191 = 127,
    v192 = 128, v193 = 129, v194 = 130, v195 = 131, v196 = 132, v197 = 133,
    v198 = 134, v199 = 135, v200 = 136, v201 = 137, v202 = 138, v203 = 139,
    v204 = 140, v205 = 141, v206 = 142, v207 = 143, v208 = 144, v209 = 145,
    v210 = 146, v211 = 147, v212 = 148, v213 = 149, v214 = 150, v215 = 151,
    v216 = 152, v217 = 153, v218 = 154, v219 = 155, v220 = 156, v221 = 157,
    v222 = 158, v223 = 159, v224 = 160, v225 = 161, v226 = 162, v227 = 163,
    v228 = 164, v229 = 165, v230 = 166, v231 = 167, v232 = 168, v233 = 169,
    v234 = 170, v235 = 171, v236 = 172, v237 = 173, v238 = 174, v239 = 175,
    v240 = 176, v241 = 177, v242 = 178, v243 = 179, v244 = 180, v245 = 181,
    v246 = 182, v247 = 183, v248 = 184, v249 = 185, v250 = 186, v251 = 187,
    v252 = 188, v253 = 189, v254 = 190, v255 = 191,
};
enum class clustered : int {
    v0 = 0, v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5,
    v6 = 6, v7 = 7, v8 = 8, v9 = 9, v10 = 10, v11 = 11,
    v12 = 12, v13 = 13, v14 = 14, v15 = 15, v16 = 16, v17 = 17,
    v18 = 18, v19 = 19, v20 = 20, v21 = 21, v22 = 22, v23 = 23,
    v24 = 256, v25 = 257, v26 = 258, v27 = 259, v28 = 260, v29 = 261,
    v30 = 262, v31 = 263, v32 = 264, v33 = 265, v34 = 266, v35 = 267,
    v36 = 268, v37 = 269, v38 = 270, v39 = 271, v40 = 272, v41 = 273,
    v42 = 274, v43 = 275, v44 = 276, v45 = 277, v46 = 278, v47 = 279,
    v48 = 512, v49 = 513, v50 = 514, v51 = 515, v52 = 516, v53 = 517,
    v54 = 518, v55 = 519, v56 = 520, v57 = 521, v58 = 522, v59 = 523,
    v60 = 524, v61 = 525, v62 = 526, v63 = 527, v64 = 528, v65 = 529,
    v66 = 530, v67 = 531, v68 = 532, v69 = 533, v70 = 534, v71 = 535,
    v72 = 768, v73 = 769, v74 = 770, v75 = 771, v76 = 772, v77 = 773,
    v78 = 774, v79 = 775, v80 = 776, v81 = 777, v82 = 778, v83 = 779,
    v84 = 780, v85 = 781, v86 = 782, v87 = 783, v88 = 784, v89 = 785,
    v90 = 786, v91 = 787, v92 = 788, v93 = 789, v94 = 790, v95 = 791,
};
enum class dense : int {
    v0 = 1, v1 = 2, v2 = 3, v3 = 4, v4 = 6, v5 = 7,
    v6 = 8, v7 = 9, v8 = 11, v9 = 12, v10 = 13, v11 = 14,
    v12 = 16, v13 = 17, v14 = 18, v15 = 19, v16 = 21, v17 = 22,
    v18 = 23, v19 = 24, v20 = 26, v21 = 27, v22 = 28, v23 = 29,
    v24 = 31, v25 = 32, v26 = 33, v27 = 34, v28 = 36, v29 = 37,
    v30 = 38, v31 = 39, v32 = 41, v33 = 42, v34 = 43, v35 = 44,
    v36 = 46, v37 = 47, v38 = 48, v39 = 49, v40 = 51, v41 = 52,
    v42 = 53, v43 = 54, v44 = 56, v45 = 57, v46 = 58, v47 = 59,
    v48 = 61, v49 = 62, v50 = 63, v51 = 64, v52 = 66, v53 = 67,
    v54 = 68, v55 = 69, v56 = 71, v57 = 72, v58 = 73, v59 = 74,
    v60 = 76, v61 = 77, v62 = 78, v63 = 79, v64 = 81, v65 = 82,
    v66 = 83, v67 = 84, v68 = 86, v69 = 87, v70 = 88, v71 = 89,
    v72 = 91, v73 = 92, v74 = 93, v75 = 94, v76 = 96, v77 = 97,
    v78 = 98, v79 = 99, v80 = 101, v81 = 102, v82 = 103, v83 = 104,
    v84 = 106, v85 = 107, v86 = 108, v87 = 109, v88 = 111, v89 = 112,
    v90 = 113, v91 = 114, v92 = 116, v93 = 117, v94 = 118, v95 = 119,
    v96 = 121, v97 = 122, v98 = 123, v99 = 124, v100 = 126, v101 = 127,
    v102 = 128, v103 = 129, v104 = 131, v105 = 132, v106 = 133, v107 = 134,
    v108 = 136, v109 = 137, v110 = 138, v111 = 139, v112 = 141, v113 = 142,
    v114 = 143, v115 = 144, v116 = 146, v117 = 147, v118 = 148, v119 = 149,
    v120 = 151, v121 = 152, v122 = 153, v123 = 154, v124 = 156, v125 = 157,
    v126 = 158, v127 = 159, v128 = 161, v129 = 162, v130 = 163, v131 = 164,
    v132 = 166, v133 = 167, v134 = 168, v135 = 169, v136 = 171, v137 = 172,
    v138 = 173, v139 = 174, v140 = 176, v141 = 177, v142 = 178, v143 = 179,
    v144 = 181, v145 = 182, v146 = 183, v147 = 184, v148 = 186, v149 = 187,
    v150 = 188, v151 = 189, v152 = 191, v153 = 192, v154 = 193, v155 = 194,
    v156 = 196, v157 = 197, v158 = 198, v159 = 199, v160 = 201, v161 = 202,
    v162 = 203, v163 = 204, v164 = 206, v165 = 207, v166 = 208, v167 = 209,
    v168 = 211, v169 = 212, v170 = 213, v171 = 214, v172 = 216, v173 = 217,
    v174 = 218, v175 = 219, v176 = 221, v177 = 222, v178 = 223, v179 = 224,
    v180 = 226, v181 = 227, v182 = 228, v183 = 229, v184 = 231, v185 = 232,
    v186 = 233, v187 = 234, v188 = 236, v189 = 237, v190 = 238, v191 = 239,
    v192 = 241, v193 = 242, v194 = 243, v195 = 244, v196 = 246, v197 = 247,
    v198 = 248, v199 = 249, v200 = 251, v201 = 252, v202 = 253, v203 = 254,
};
enum class sparse : int {
    v0 = 0, v1 = 997, v2 = 1994, v3 = 2991, v4 = 3988, v5 = 4985,
    v6 = 5982, v7 = 6979, v8 = 7976, v9 = 8973, v10 = 9970, v11 = 10967,
    v12 = 11964, v13 = 12961, v14 = 13958, v15 = 14955, v16 = 15952, v17 = 16949,
    v18 = 17946, v19 = 18943, v20 = 19940, v21 = 20937, v22 = 21934, v23 = 22931,
    v24 = 23928, v25 = 24925, v26 = 25922, v27 = 26919, v28 = 27916, v29 = 28913,
    v30 = 29910, v31 = 30907, v32 = 31904, v33 = 32901, v34 = 33898, v35 = 34895,
    v36 = 35892, v37 = 36889, v38 = 37886, v39 = 38883, v40 = 39880, v41 = 40877,
    v42 = 41874, v43 = 42871, v44 = 43868, v45 = 44865, v46 = 45862, v47 = 46859,
    v48 = 47856, v49 = 48853, v50 = 49850, v51 = 50847, v52 = 51844, v53 = 52841,
    v54 = 53838, v55 = 54835, v56 = 55832, v57 = 56829, v58 = 57826, v59 = 58823,
    v60 = 59820, v61 = 60817, v62 = 61814, v63 = 62811,
};
enum class mixed : int {
    v0 = -80, v1 = -79, v2 = -78, v3 = -77, v4 = -76, v5 = -75,
    v6 = -74, v7 = -73, v8 = -72, v9 = -71, v10 = -70, v11 = -69,
    v12 = -68, v13 = -67, v14 = -66, v15 = -65, v16 = -64, v17 = -63,
    v18 = -62, v19 = -61, v20 = -60, v21 = -59, v22 = -58, v23 = -57,
    v24 = -56, v25 = -55, v26 = -54, v27 = -53, v28 = -52, v29 = -51,
    v30 = -50, v31 = -49, v32 = -48, v33 = -47, v34 = -46, v35 = -45,
    v36 = -44, v37 = -43, v38 = -42, v39 = -41, v40 = -40, v41 = -39,
    v42 = -38, v43 = -37, v44 = -36, v45 = -35, v46 = -34, v47 = -33,
    v48 = -32, v49 = -31, v50 = -30, v51 = -29, v52 = -28, v53 = -27,
    v54 = -26, v55 = -25, v56 = -24, v57 = -23, v58 = -22, v59 = -21,
    v60 = -20, v61 = -19, v62 = -18, v63 = -17, v64 = -16, v65 = -15,
    v66 = -14, v67 = -13, v68 = -12, v69 = -11, v70 = -10, v71 = -9,
    v72 = -8, v73 = -7, v74 = -6, v75 = -5, v76 = -4, v77 = -3,
    v78 = -2, v79 = -1, v80 = 0, v81 = 1, v82 = 2, v83 = 3,
    v84 = 4, v85 = 5, v86 = 6, v87 = 7, v88 = 8, v89 = 9,
    v90 = 10, v91 = 11, v92 = 12, v93 = 13, v94 = 14, v95 = 15,
    v96 = 16, v97 = 17, v98 = 18, v99 = 19, v100 = 100, v101 = 103,
    v102 = 106, v103 = 109, v104 = 112, v105 = 115, v106 = 118, v107 = 121,
    v108 = 124, v109 = 127, v110 = 130, v111 = 133, v112 = 136, v113 = 139,
    v114 = 142, v115 = 145, v116 = 148, v117 = 151, v118 = 154, v119 = 157,
    v120 = 1000, v121 = 1001, v122 = 1002, v123 = 1003, v124 = 1004, v125 = 1005,
    v126 = 1006, v127 = 1007, v128 = 1008, v129 = 1009, v130 = 1010, v131 = 1011,
    v132 = 1012, v133 = 1013, v134 = 1014, v135 = 1015, v136 = 1016, v137 = 1017,
    v138 = 1018, v139 = 1019, v140 = 1020, v141 = 1021, v142 = 1022, v143 = 1023,
    v144 = 1024, v145 = 1025, v146 = 1026, v147 = 1027, v148 = 1028, v149 = 1029,
    v150 = 1030, v151 = 1031, v152 = 1032, v153 = 1033, v154 = 1034, v155 = 1035,
    v156 = 1036, v157 = 1037, v158 = 1038, v159 = 1039, v160 = 1040, v161 = 1041,
    v162 = 1042, v163 = 1043, v164 = 1044, v165 = 1045, v166 = 1046, v167 = 1047,
    v168 = 1048, v169 = 1049, v170 = 1050, v171 = 1051, v172 = 1052, v173 = 1053,
    v174 = 1054, v175 = 1055, v176 = 1056, v177 = 1057, v178 = 1058, v179 = 1059,
    v180 = 1060, v181 = 1061, v182 = 1062, v183 = 1063, v184 = 1064, v185 = 1065,
    v186 = 1066, v187 = 1067, v188 = 1068, v189 = 1069, v190 = 1070, v191 = 1071,
    v192 = 1072, v193 = 1073, v194 = 1074, v195 = 1075, v196 = 1076, v197 = 1077,
    v198 = 1078, v199 = 1079, v200 = 1080, v201 = 1081, v202 = 1082, v203 = 1083,
    v204 = 1084, v205 = 1085, v206 = 1086, v207 = 1087, v208 = 1088, v209 = 1089,
    v210 = 1090, v211 = 1091, v212 = 1092, v213 = 1093, v214 = 1094, v215 = 1095,
    v216 = 1096, v217 = 1097, v218 = 1098, v219 = 1099,
};

template<typename E>
bool original(int value) {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    template for (constexpr auto e: enumerators) {
        if (value == std::to_underlying(std::meta::extract<E>(std::meta::constant_of(e)))) { return true; }
    }
    return false;
}

template<typename E, unsigned Width, bool Binary>
bool partitioned(int value) {
    static constexpr auto parts = std::define_static_array(xasrl::detail::make_enum_partitions(^^E, Width));
    const auto key = xasrl::detail::enum_order_key(static_cast<std::uint64_t>(value),
        std::meta::is_signed_type(std::meta::underlying_type(^^E)));
    if constexpr (Binary) {
        std::size_t first = 0, last = parts.size();
        while (first < last) {
            const auto mid = first + (last - first) / 2;
            if (key > parts[mid].last) { first = mid + 1; }
            else { last = mid; }
        }
        return first != parts.size() && parts[first].contains(key);
    } else {
        template for (constexpr auto part: parts) {
            if (part.contains(key)) { return true; }
        }
        return false;
    }
}

template<auto Function>
void measure(const char *name, const std::vector<int> &input) {
    std::array<double, 7> samples{};
    std::uint64_t checksum = 0;
    for (auto &sample: samples) {
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t count = 0;
        for (int repeat = 0; repeat < 16; ++repeat) {
            for (int value: input) {
                // Keep each query observable without an indirect function call.
                asm volatile("" : "+r"(value));
                count += Function(value);
            }
        }
        sample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count()
            / (input.size() * 16);
        checksum += count;
    }
    std::ranges::sort(samples);
    const auto checksum_text = xasrl::number_to_string(checksum);
    std::println("  {:<14} {:7.3f} ns/query ({})", name, samples[3], checksum_text.view());
}


template<typename E>
void run(const char *name) {
    static constexpr auto parts = std::define_static_array(xasrl::detail::make_enum_partitions(^^E));
    std::vector<int> declared;
    template for (constexpr auto e: std::define_static_array(std::meta::enumerators_of(^^E))) {
        declared.push_back(std::to_underlying(std::meta::extract<E>(std::meta::constant_of(e))));
    }
    std::mt19937 rng(12345);
    const auto bounds = std::ranges::minmax(declared);
    std::uniform_int_distribution<int> domain(bounds.min - 32, bounds.max + 32);
    std::vector<int> input(65536);
    for (int hit_percent: {0, 50, 100}) {
        for (auto &value: input) {
            value = rng() % 100 < static_cast<unsigned>(hit_percent) ? declared[rng() % declared.size()] : domain(rng);
        }
        for (int value: input) {
            const bool expected = original<E>(value);
            if (partitioned<E, 0, false>(value) != expected ||
                partitioned<E, 16, false>(value) != expected ||
                partitioned<E, 32, false>(value) != expected ||
                partitioned<E, 64, false>(value) != expected ||
                partitioned<E, 64, true>(value) != expected ||
                xasrl::enum_contains<E>(value) != expected) {
                throw std::runtime_error("enum membership mismatch");
            }
        }
        constexpr std::array counts{
            xasrl::detail::make_enum_partitions(^^E, 0).size(),
            xasrl::detail::make_enum_partitions(^^E, 16).size(),
            xasrl::detail::make_enum_partitions(^^E, 32).size(),
        };
        const auto hits = xasrl::number_to_string(hit_percent);
        const auto ranges = xasrl::number_to_string(counts[0]);
        const auto bits16 = xasrl::number_to_string(counts[1]);
        const auto bits32 = xasrl::number_to_string(counts[2]);
        const auto bits64 = xasrl::number_to_string(parts.size());
        std::println("{} sampled_hits={} partitions={}/{}/{}/{}", name, hits.view(),
            ranges.view(), bits16.view(), bits32.view(), bits64.view());
        measure<original<E>>("original", input);
        measure<static_cast<bool (*)(std::underlying_type_t<E>)>(&xasrl::enum_contains<E>)>("srl-current", input);
        measure<partitioned<E, 0, false>>("ranges-linear", input);
        measure<partitioned<E, 16, false>>("bits16-linear", input);
        measure<partitioned<E, 32, false>>("bits32-linear", input);
        measure<partitioned<E, 64, false>>("bits64-linear", input);
        measure<partitioned<E, 64, true>>("bits64-binary", input);
    }
}

enum class [[=xasrl::tag::bit_flag{.mask = 0x83}]] flags8 : std::uint8_t { a = 1, b = 2, c = 128 };
enum class [[=xasrl::tag::bit_flag{.mask = 0x8000000000000003ull}]] flags64 : std::uint64_t { a = 1 };

template<typename E, bool Original>
bool flags_check(int input) {
    using underlying = std::underlying_type_t<E>;
    const auto value = static_cast<underlying>(input);
    if constexpr (Original) {
        return (static_cast<std::uint64_t>(value) & ~xasrl::detail::make_enum_policy(^^E).mask) == 0;
    } else {
        return xasrl::enum_contains<E>(value);
    }
}

int main() {
    run<continuous>("continuous");
    run<clustered>("clustered");
    run<dense>("dense");
    run<sparse>("sparse");
    run<mixed>("mixed");
    std::mt19937 rng(12345);
    std::vector<int> input(65536);
    for (auto &v: input) { v = rng() % 256; }
    measure<flags_check<flags8, true>>("flags8-before", input);
    measure<flags_check<flags8, false>>("flags8-after", input);
    measure<flags_check<flags64, true>>("flags64-before", input);
    measure<flags_check<flags64, false>>("flags64-after", input);
}
