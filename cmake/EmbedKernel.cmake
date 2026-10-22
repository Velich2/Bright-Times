# Turns a kernel source file into a C++ header with a raw string literal.
#   cmake -DIN=<kernel.h> -DOUT=<generated.h> -DNAME=<symbol> -P EmbedKernel.cmake
# MSVC limits a single string literal to ~16 KB, so the source is split into
# chunks that the compiler concatenates.
file(READ "${IN}" content)
string(LENGTH "${content}" len)
set(chunk 8000)
set(body "")
set(pos 0)
while(pos LESS len)
  string(SUBSTRING "${content}" ${pos} ${chunk} piece)
  string(APPEND body "R\"BTKSRC(${piece})BTKSRC\"\n")
  math(EXPR pos "${pos} + ${chunk}")
endwhile()
file(WRITE "${OUT}.tmp" "// Generated from ${IN}. Do not edit.\n#pragma once\nstatic const char ${NAME}[] =\n${body};\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
