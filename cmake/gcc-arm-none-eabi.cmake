set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB CUBECLT_GCC_CANDIDATES
    "C:/ST/STM32CubeCLT*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc.exe"
    "C:/ST/STM32CubeCLT*/GNU-tools-for-STM32/*/bin/arm-none-eabi-gcc.exe"
    "C:/ProgramData/STM32CubeCLT*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc.exe"
    "C:/ProgramData/STM32CubeCLT*/GNU-tools-for-STM32/*/bin/arm-none-eabi-gcc.exe"
    "C:/Program Files/STMicroelectronics/STM32CubeCLT*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc.exe"
    "C:/Program Files/STMicroelectronics/STM32CubeCLT*/GNU-tools-for-STM32/*/bin/arm-none-eabi-gcc.exe"
    "C:/Program Files (x86)/STMicroelectronics/STM32CubeCLT*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc.exe"
    "C:/Program Files (x86)/STMicroelectronics/STM32CubeCLT*/GNU-tools-for-STM32/*/bin/arm-none-eabi-gcc.exe"
)

list(SORT CUBECLT_GCC_CANDIDATES ORDER DESCENDING)
list(LENGTH CUBECLT_GCC_CANDIDATES CUBECLT_GCC_CANDIDATE_COUNT)
if(CUBECLT_GCC_CANDIDATE_COUNT GREATER 0)
    list(GET CUBECLT_GCC_CANDIDATES 0 CUBECLT_GCC)
endif()

if(CUBECLT_GCC)
    get_filename_component(TOOLCHAIN_BIN_DIR "${CUBECLT_GCC}" DIRECTORY)
elseif(EXISTS "${PROJECT_ROOT}/.toolchain/arm-gcc/.content/bin/arm-none-eabi-gcc.exe")
    set(TOOLCHAIN_BIN_DIR "${PROJECT_ROOT}/.toolchain/arm-gcc/.content/bin")
else()
    find_program(CUBECLT_GCC_IN_PATH arm-none-eabi-gcc)
    if(CUBECLT_GCC_IN_PATH)
        get_filename_component(TOOLCHAIN_BIN_DIR "${CUBECLT_GCC_IN_PATH}" DIRECTORY)
    else()
        message(FATAL_ERROR
            "arm-none-eabi-gcc was not found. Install STM32CubeCLT, or add GNU Arm Embedded GCC to PATH. "
            "Expected CubeCLT layout: C:/ProgramData/STM32CubeCLT_*/GNU-tools-for-STM32/bin/arm-none-eabi-gcc.exe")
    endif()
endif()

set(CMAKE_C_COMPILER                "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER              "${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER              "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-g++.exe")
set(CMAKE_LINKER                    "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-g++.exe")
set(CMAKE_OBJCOPY                   "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-objcopy.exe")
set(CMAKE_SIZE                      "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-size.exe")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(TOOLCHAIN_LINK_LIBRARIES "m")
