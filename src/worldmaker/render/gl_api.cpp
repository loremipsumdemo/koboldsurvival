#include "render/gl_api.hpp"

#include <SDL3/SDL.h>

namespace wm {


PFN_glCreateShader wm_glCreateShader = nullptr;
PFN_glShaderSource wm_glShaderSource = nullptr;
PFN_glCompileShader wm_glCompileShader = nullptr;
PFN_glGetShaderiv wm_glGetShaderiv = nullptr;
PFN_glGetShaderInfoLog wm_glGetShaderInfoLog = nullptr;
PFN_glDeleteShader wm_glDeleteShader = nullptr;
PFN_glCreateProgram wm_glCreateProgram = nullptr;
PFN_glAttachShader wm_glAttachShader = nullptr;
PFN_glLinkProgram wm_glLinkProgram = nullptr;
PFN_glGetProgramiv wm_glGetProgramiv = nullptr;
PFN_glGetProgramInfoLog wm_glGetProgramInfoLog = nullptr;
PFN_glDeleteProgram wm_glDeleteProgram = nullptr;
PFN_glUseProgram wm_glUseProgram = nullptr;
PFN_glGetUniformLocation wm_glGetUniformLocation = nullptr;
PFN_glUniform1i wm_glUniform1i = nullptr;
PFN_glUniform1f wm_glUniform1f = nullptr;
PFN_glUniform3f wm_glUniform3f = nullptr;
PFN_glUniform4f wm_glUniform4f = nullptr;
PFN_glUniformMatrix4fv wm_glUniformMatrix4fv = nullptr;
PFN_glGenVertexArrays wm_glGenVertexArrays = nullptr;
PFN_glBindVertexArray wm_glBindVertexArray = nullptr;
PFN_glDeleteVertexArrays wm_glDeleteVertexArrays = nullptr;
PFN_glGenBuffers wm_glGenBuffers = nullptr;
PFN_glBindBuffer wm_glBindBuffer = nullptr;
PFN_glBufferData wm_glBufferData = nullptr;
PFN_glDeleteBuffers wm_glDeleteBuffers = nullptr;
PFN_glEnableVertexAttribArray wm_glEnableVertexAttribArray = nullptr;
PFN_glVertexAttribPointer wm_glVertexAttribPointer = nullptr;
PFN_glActiveTexture wm_glActiveTexture = nullptr;
PFN_glGenerateMipmap wm_glGenerateMipmap = nullptr;
PFN_glGenFramebuffers wm_glGenFramebuffers = nullptr;
PFN_glDeleteFramebuffers wm_glDeleteFramebuffers = nullptr;
PFN_glBindFramebuffer wm_glBindFramebuffer = nullptr;
PFN_glFramebufferTexture2D wm_glFramebufferTexture2D = nullptr;
PFN_glCheckFramebufferStatus wm_glCheckFramebufferStatus = nullptr;
PFN_glGenQueries wm_glGenQueries = nullptr;
PFN_glDeleteQueries wm_glDeleteQueries = nullptr;
PFN_glBeginQuery wm_glBeginQuery = nullptr;
PFN_glEndQuery wm_glEndQuery = nullptr;
PFN_glGetQueryObjectiv wm_glGetQueryObjectiv = nullptr;
PFN_glGetQueryObjectui64v wm_glGetQueryObjectui64v = nullptr;

namespace {

template <typename Fn>
bool LoadProc(Fn& fn, const char* name, std::string& err) {
    fn = reinterpret_cast<Fn>(SDL_GL_GetProcAddress(name));
    if (!fn) {
        err = std::string("missing OpenGL 3.3 function: ") + name;
        return false;
    }
    return true;
}

GLuint CompileStage(GLenum type, const char* src, std::string& err) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? static_cast<std::size_t>(len) : 1u, '\0');
        glGetShaderInfoLog(shader, len, nullptr, &log[0]);
        err = "shader compile error:\n" + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

bool LoadGL(std::string& err) {
    return LoadProc(wm_glCreateShader, "glCreateShader", err)
        && LoadProc(wm_glShaderSource, "glShaderSource", err)
        && LoadProc(wm_glCompileShader, "glCompileShader", err)
        && LoadProc(wm_glGetShaderiv, "glGetShaderiv", err)
        && LoadProc(wm_glGetShaderInfoLog, "glGetShaderInfoLog", err)
        && LoadProc(wm_glDeleteShader, "glDeleteShader", err)
        && LoadProc(wm_glCreateProgram, "glCreateProgram", err)
        && LoadProc(wm_glAttachShader, "glAttachShader", err)
        && LoadProc(wm_glLinkProgram, "glLinkProgram", err)
        && LoadProc(wm_glGetProgramiv, "glGetProgramiv", err)
        && LoadProc(wm_glGetProgramInfoLog, "glGetProgramInfoLog", err)
        && LoadProc(wm_glDeleteProgram, "glDeleteProgram", err)
        && LoadProc(wm_glUseProgram, "glUseProgram", err)
        && LoadProc(wm_glGetUniformLocation, "glGetUniformLocation", err)
        && LoadProc(wm_glUniform1i, "glUniform1i", err)
        && LoadProc(wm_glUniform1f, "glUniform1f", err)
        && LoadProc(wm_glUniform3f, "glUniform3f", err)
        && LoadProc(wm_glUniform4f, "glUniform4f", err)
        && LoadProc(wm_glUniformMatrix4fv, "glUniformMatrix4fv", err)
        && LoadProc(wm_glGenVertexArrays, "glGenVertexArrays", err)
        && LoadProc(wm_glBindVertexArray, "glBindVertexArray", err)
        && LoadProc(wm_glDeleteVertexArrays, "glDeleteVertexArrays", err)
        && LoadProc(wm_glGenBuffers, "glGenBuffers", err)
        && LoadProc(wm_glBindBuffer, "glBindBuffer", err)
        && LoadProc(wm_glBufferData, "glBufferData", err)
        && LoadProc(wm_glDeleteBuffers, "glDeleteBuffers", err)
        && LoadProc(wm_glEnableVertexAttribArray, "glEnableVertexAttribArray", err)
        && LoadProc(wm_glVertexAttribPointer, "glVertexAttribPointer", err)
        && LoadProc(wm_glActiveTexture, "glActiveTexture", err)
        && LoadProc(wm_glGenerateMipmap, "glGenerateMipmap", err)
        && LoadProc(wm_glGenFramebuffers, "glGenFramebuffers", err)
        && LoadProc(wm_glDeleteFramebuffers, "glDeleteFramebuffers", err)
        && LoadProc(wm_glBindFramebuffer, "glBindFramebuffer", err)
        && LoadProc(wm_glFramebufferTexture2D, "glFramebufferTexture2D", err)
        && LoadProc(wm_glCheckFramebufferStatus, "glCheckFramebufferStatus", err)
        && LoadProc(wm_glGenQueries, "glGenQueries", err)
        && LoadProc(wm_glDeleteQueries, "glDeleteQueries", err)
        && LoadProc(wm_glBeginQuery, "glBeginQuery", err)
        && LoadProc(wm_glEndQuery, "glEndQuery", err)
        && LoadProc(wm_glGetQueryObjectiv, "glGetQueryObjectiv", err)
        && LoadProc(wm_glGetQueryObjectui64v, "glGetQueryObjectui64v", err);
}

GLuint BuildProgram(const char* vertexSrc, const char* fragmentSrc, std::string& err) {
    const GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSrc, err);
    if (!vs) return 0;
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc, err);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? static_cast<std::size_t>(len) : 1u, '\0');
        glGetProgramInfoLog(program, len, nullptr, &log[0]);
        err = "program link error:\n" + log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace wm
