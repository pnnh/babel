vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/llama.cpp
    REF b${VERSION}
    SHA512 460ab3217f6e837f84f1f58535752f86273dd2db36cf198beb3a20e04d9f9cac42ae31f075ca3ea60da0c9f91ba5c5f18cc538448f14bf68d71ef3a0e8414ff9
    HEAD_REF master
    PATCHES
        cmake-config.diff
)

# Note: LLAMA_BUILD_TOOLS is intentionally NOT included in vcpkg_check_features
# so that we can force it ON below to always build the mtmd multimodal library.
vcpkg_check_features(OUT_FEATURE_OPTIONS options
    FEATURES
        download    LLAMA_CURL
        metal       GGML_METAL
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
        -DGGML_CCACHE=OFF
        -DLLAMA_ALL_WARNINGS=OFF
        -DLLAMA_BUILD_TESTS=OFF
        -DLLAMA_BUILD_EXAMPLES=OFF
        # Always enable tools so the mtmd multimodal library (and headers) are
        # built and installed.  CLI tool executables are cleaned up below unless
        # the "tools" feature is explicitly requested.
        -DLLAMA_BUILD_TOOLS=ON
        -DLLAMA_BUILD_SERVER=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_Git=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/llama" DO_NOT_DELETE_PARENT_CONFIG_PATH)
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/ggml" PACKAGE_NAME "ggml")
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(INSTALL "${SOURCE_PATH}/gguf-py/gguf" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}/gguf-py")
if(EXISTS "${CURRENT_PACKAGES_DIR}/bin/convert_hf_to_gguf.py")
    file(RENAME "${CURRENT_PACKAGES_DIR}/bin/convert_hf_to_gguf.py" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/convert-hf-to-gguf.py")
    file(REMOVE "${CURRENT_PACKAGES_DIR}/debug/bin/convert_hf_to_gguf.py")
elseif(EXISTS "${SOURCE_PATH}/convert_hf_to_gguf.py")
    file(INSTALL "${SOURCE_PATH}/convert_hf_to_gguf.py" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}" RENAME "convert-hf-to-gguf.py")
endif()

if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(
        TOOL_NAMES
            llama-batched-bench
            llama-bench
            llama-cli
            llama-cvector-generator
            llama-export-lora
            llama-gguf-split
            llama-imatrix
            llama-mtmd-cli
            llama-perplexity
            llama-quantize
            llama-run
            llama-tokenize
            llama-tts
        AUTO_CLEAN
    )
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_clean_executables_in_bin(FILE_NAMES none)

set(gguf-py-license "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/gguf-py LICENSE")
file(COPY_FILE "${SOURCE_PATH}/gguf-py/LICENSE" "${gguf-py-license}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${gguf-py-license}")
