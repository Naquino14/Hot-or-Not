cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(hot-or-not LANGUAGES C)

target_sources(app PRIVATE 
    src/main.c
    src/smtp_client.c
    src/tls/hon_tls.c
    src/hon_sntp.c
)

# Get cert PEM filename from secrets config
file(STRINGS "smtp.secrets.conf" CONFIG_LINE REGEX "SMTP_CA_PEM_FILENAME")

# extract just the filename part
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" CA_PEM_FILENAME "${CONFIG_LINE}")

# append .inc
set(CA_PEM_INC_FILENAME "${CA_PEM_FILENAME}.inc")

# make it a compile definition to include it down the line
add_compile_definitions(app PRIVATE CA_PEM_INC_FILENAME="${CA_PEM_INC_FILENAME}")

generate_inc_file_for_target(app
    src/tls/${CA_PEM_FILENAME}
    ${ZEPHYR_BINARY_DIR}/include/generated/${CA_PEM_INC_FILENAME}
)

target_link_libraries(app PRIVATE conn_mgr)