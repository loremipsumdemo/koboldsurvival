#pragma once

// OpenGL 3.3 core entry points. SDL only gives us GL 1.1 prototypes, so the
// rest are looked up at runtime after the context is created.

#include <SDL3/SDL_opengl.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace wm {


#ifndef APIENTRY
#  ifdef _WIN32
#    define APIENTRY __stdcall
#  else
#    define APIENTRY
#  endif
#endif

// Same underlying types as the GL headers, so repeating the typedef is allowed.
typedef char GLchar;
typedef std::ptrdiff_t GLsizeiptr;

#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#  define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_STREAM_DRAW
#  define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_VERTEX_SHADER
#  define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#  define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#  define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#  define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#  define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_R8
#  define GL_R8 0x8229
#endif
#ifndef GL_RED
#  define GL_RED 0x1903
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#  define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#  define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE1
#  define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#  define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#  define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_TIME_ELAPSED
#  define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#  define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#  define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#  define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#  define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_WRAP_R
#  define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_RGB8
#  define GL_RGB8 0x8051
#endif

typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum type);
typedef void   (APIENTRY *PFN_glShaderSource)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void   (APIENTRY *PFN_glCompileShader)(GLuint shader);
typedef void   (APIENTRY *PFN_glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFN_glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY *PFN_glDeleteShader)(GLuint shader);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void   (APIENTRY *PFN_glAttachShader)(GLuint program, GLuint shader);
typedef void   (APIENTRY *PFN_glLinkProgram)(GLuint program);
typedef void   (APIENTRY *PFN_glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFN_glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY *PFN_glDeleteProgram)(GLuint program);
typedef void   (APIENTRY *PFN_glUseProgram)(GLuint program);
typedef GLint  (APIENTRY *PFN_glGetUniformLocation)(GLuint program, const GLchar* name);
typedef void   (APIENTRY *PFN_glUniform1i)(GLint location, GLint v0);
typedef void   (APIENTRY *PFN_glUniform1f)(GLint location, GLfloat v0);
typedef void   (APIENTRY *PFN_glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void   (APIENTRY *PFN_glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void   (APIENTRY *PFN_glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void   (APIENTRY *PFN_glGenVertexArrays)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRY *PFN_glBindVertexArray)(GLuint array);
typedef void   (APIENTRY *PFN_glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRY *PFN_glGenBuffers)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRY *PFN_glBindBuffer)(GLenum target, GLuint buffer);
typedef void   (APIENTRY *PFN_glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void   (APIENTRY *PFN_glDeleteBuffers)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint index);
typedef void   (APIENTRY *PFN_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void   (APIENTRY *PFN_glActiveTexture)(GLenum texture);
typedef void   (APIENTRY *PFN_glGenerateMipmap)(GLenum target);
typedef void   (APIENTRY *PFN_glGenFramebuffers)(GLsizei n, GLuint* framebuffers);
typedef void   (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
typedef void   (APIENTRY *PFN_glBindFramebuffer)(GLenum target, GLuint framebuffer);
typedef void   (APIENTRY *PFN_glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum target);
typedef void   (APIENTRY *PFN_glGenQueries)(GLsizei n, GLuint* ids);
typedef void   (APIENTRY *PFN_glDeleteQueries)(GLsizei n, const GLuint* ids);
typedef void   (APIENTRY *PFN_glBeginQuery)(GLenum target, GLuint id);
typedef void   (APIENTRY *PFN_glEndQuery)(GLenum target);
typedef void   (APIENTRY *PFN_glGetQueryObjectiv)(GLuint id, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFN_glGetQueryObjectui64v)(GLuint id, GLenum pname, std::uint64_t* params);

extern PFN_glCreateShader wm_glCreateShader;
extern PFN_glShaderSource wm_glShaderSource;
extern PFN_glCompileShader wm_glCompileShader;
extern PFN_glGetShaderiv wm_glGetShaderiv;
extern PFN_glGetShaderInfoLog wm_glGetShaderInfoLog;
extern PFN_glDeleteShader wm_glDeleteShader;
extern PFN_glCreateProgram wm_glCreateProgram;
extern PFN_glAttachShader wm_glAttachShader;
extern PFN_glLinkProgram wm_glLinkProgram;
extern PFN_glGetProgramiv wm_glGetProgramiv;
extern PFN_glGetProgramInfoLog wm_glGetProgramInfoLog;
extern PFN_glDeleteProgram wm_glDeleteProgram;
extern PFN_glUseProgram wm_glUseProgram;
extern PFN_glGetUniformLocation wm_glGetUniformLocation;
extern PFN_glUniform1i wm_glUniform1i;
extern PFN_glUniform1f wm_glUniform1f;
extern PFN_glUniform3f wm_glUniform3f;
extern PFN_glUniform4f wm_glUniform4f;
extern PFN_glUniformMatrix4fv wm_glUniformMatrix4fv;
extern PFN_glGenVertexArrays wm_glGenVertexArrays;
extern PFN_glBindVertexArray wm_glBindVertexArray;
extern PFN_glDeleteVertexArrays wm_glDeleteVertexArrays;
extern PFN_glGenBuffers wm_glGenBuffers;
extern PFN_glBindBuffer wm_glBindBuffer;
extern PFN_glBufferData wm_glBufferData;
extern PFN_glDeleteBuffers wm_glDeleteBuffers;
extern PFN_glEnableVertexAttribArray wm_glEnableVertexAttribArray;
extern PFN_glVertexAttribPointer wm_glVertexAttribPointer;
extern PFN_glActiveTexture wm_glActiveTexture;
extern PFN_glGenerateMipmap wm_glGenerateMipmap;
extern PFN_glGenFramebuffers wm_glGenFramebuffers;
extern PFN_glDeleteFramebuffers wm_glDeleteFramebuffers;
extern PFN_glBindFramebuffer wm_glBindFramebuffer;
extern PFN_glFramebufferTexture2D wm_glFramebufferTexture2D;
extern PFN_glCheckFramebufferStatus wm_glCheckFramebufferStatus;
extern PFN_glGenQueries wm_glGenQueries;
extern PFN_glDeleteQueries wm_glDeleteQueries;
extern PFN_glBeginQuery wm_glBeginQuery;
extern PFN_glEndQuery wm_glEndQuery;
extern PFN_glGetQueryObjectiv wm_glGetQueryObjectiv;
extern PFN_glGetQueryObjectui64v wm_glGetQueryObjectui64v;

#define glCreateShader wm_glCreateShader
#define glShaderSource wm_glShaderSource
#define glCompileShader wm_glCompileShader
#define glGetShaderiv wm_glGetShaderiv
#define glGetShaderInfoLog wm_glGetShaderInfoLog
#define glDeleteShader wm_glDeleteShader
#define glCreateProgram wm_glCreateProgram
#define glAttachShader wm_glAttachShader
#define glLinkProgram wm_glLinkProgram
#define glGetProgramiv wm_glGetProgramiv
#define glGetProgramInfoLog wm_glGetProgramInfoLog
#define glDeleteProgram wm_glDeleteProgram
#define glUseProgram wm_glUseProgram
#define glGetUniformLocation wm_glGetUniformLocation
#define glUniform1i wm_glUniform1i
#define glUniform1f wm_glUniform1f
#define glUniform3f wm_glUniform3f
#define glUniform4f wm_glUniform4f
#define glUniformMatrix4fv wm_glUniformMatrix4fv
#define glGenVertexArrays wm_glGenVertexArrays
#define glBindVertexArray wm_glBindVertexArray
#define glDeleteVertexArrays wm_glDeleteVertexArrays
#define glGenBuffers wm_glGenBuffers
#define glBindBuffer wm_glBindBuffer
#define glBufferData wm_glBufferData
#define glDeleteBuffers wm_glDeleteBuffers
#define glEnableVertexAttribArray wm_glEnableVertexAttribArray
#define glVertexAttribPointer wm_glVertexAttribPointer
#define glActiveTexture wm_glActiveTexture
#define glGenerateMipmap wm_glGenerateMipmap
#define glGenFramebuffers wm_glGenFramebuffers
#define glDeleteFramebuffers wm_glDeleteFramebuffers
#define glBindFramebuffer wm_glBindFramebuffer
#define glFramebufferTexture2D wm_glFramebufferTexture2D
#define glCheckFramebufferStatus wm_glCheckFramebufferStatus
#define glGenQueries wm_glGenQueries
#define glDeleteQueries wm_glDeleteQueries
#define glBeginQuery wm_glBeginQuery
#define glEndQuery wm_glEndQuery
#define glGetQueryObjectiv wm_glGetQueryObjectiv
#define glGetQueryObjectui64v wm_glGetQueryObjectui64v

// Loads every function above. Call once, after the GL context exists.
bool LoadGL(std::string& err);

// Compiles and links a vertex and fragment shader. Returns 0 and fills err on failure.
GLuint BuildProgram(const char* vertexSrc, const char* fragmentSrc, std::string& err);

}  // namespace wm
