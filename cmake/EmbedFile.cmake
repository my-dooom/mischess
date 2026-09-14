# embed_file(<input> <symbol> <output.c> <output.h>)
#
# Turns a binary file into a C array so it can be compiled into the
# executable, which is what makes the game a single portable file with no
# assets directory beside it.
function(embed_file input symbol out_c out_h)
    file(READ ${input} hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR byte_len "${hex_len} / 2")
    # "89504e47" -> "0x89,0x50,0x4e,0x47,"
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    # a line break every 16 bytes keeps the file readable and compilers happy
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" bytes "${bytes}")

    file(WRITE ${out_c}
        "// generated from ${input} by cmake/EmbedFile.cmake -- do not edit\n"
        "#include \"${symbol}.h\"\n"
        "const unsigned char ${symbol}[] = {\n${bytes}\n};\n"
        "const unsigned int ${symbol}_len = ${byte_len};\n")
    file(WRITE ${out_h}
        "// generated from ${input} by cmake/EmbedFile.cmake -- do not edit\n"
        "#ifndef ${symbol}_H\n#define ${symbol}_H\n"
        "extern const unsigned char ${symbol}[];\n"
        "extern const unsigned int ${symbol}_len;\n"
        "#endif\n")
endfunction()
