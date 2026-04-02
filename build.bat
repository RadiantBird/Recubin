cl.exe /EHsc src\main.cpp ^
/I . ^
/D GLEW_STATIC ^
/MD ^
/link ^
/NODEFAULTLIB:LIBCMT ^
/LIBPATH:include\libs ^
glfw3.lib glew32s.lib opengl32.lib user32.lib gdi32.lib shell32.lib