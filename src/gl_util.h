#pragma once
// Small OpenGL helpers the renderer, its terrain, models and text all use.
#include <GL/glew.h>

// A linked program, or 0 with the error in log.txt when a shader does not compile or the program does not link. name
// says which program it is for.
GLuint linkProgram(const char* name, const char* vs, const char* fs);

// A 2D texture from pixels, filtered and wrapped as given. It stays bound.
GLuint makeTexture2D(int w, int h, GLint internalFmt, GLenum fmt, const void* pixels, GLint filter, GLint wrap);
