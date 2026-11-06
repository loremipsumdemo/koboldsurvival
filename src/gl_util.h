#pragma once
#include <GL/glew.h>

GLuint linkProgram(const char* name, const char* vs, const char* fs);

GLuint makeTexture2D(int w, int h, GLint internalFmt, GLenum fmt, const void* pixels, GLint filter, GLint wrap);
