#include "render/shader.h"

#include "core/filesystem.h"
#include "core/log.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace k {
namespace {

GLuint compileStage(GLenum type, std::string_view src, std::string_view debugName) {
    GLuint s = glCreateShader(type);
    const char* p = src.data();
    GLint len = static_cast<GLint>(src.size());
    glShaderSource(s, 1, &p, &len);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[4096];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        log::error("Shader '{}' ({}) failed to compile:\n{}", debugName, type == GL_VERTEX_SHADER ? "vert" : "frag", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (program_) glDeleteProgram(program_);
        program_ = o.program_;
        o.program_ = 0;
    }
    return *this;
}

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::load(std::string_view name) {
    std::string base = "shaders/" + std::string(name);
    auto vs = fs::readText(base + ".vert");
    auto fsrc = fs::readText(base + ".frag");
    if (!vs || !fsrc) {
        log::error("Missing shader source for '{}'", name);
        return false;
    }
    return compile(*vs, *fsrc, name);
}

bool Shader::compile(std::string_view vertexSource, std::string_view fragmentSource, std::string_view debugName) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, debugName);
    GLuint fsh = compileStage(GL_FRAGMENT_SHADER, fragmentSource, debugName);
    if (!vs || !fsh) {
        if (vs) glDeleteShader(vs);
        if (fsh) glDeleteShader(fsh);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fsh);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fsh);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[4096];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        log::error("Shader '{}' failed to link:\n{}", debugName, buf);
        glDeleteProgram(prog);
        return false;
    }
    if (program_) glDeleteProgram(program_);
    program_ = prog;
    return true;
}

void Shader::bind() const { glUseProgram(program_); }
void Shader::set(const char* n, float v) const { glUniform1f(glGetUniformLocation(program_, n), v); }
void Shader::set(const char* n, int v) const { glUniform1i(glGetUniformLocation(program_, n), v); }
void Shader::set(const char* n, const Vec3& v) const { glUniform3fv(glGetUniformLocation(program_, n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const Vec4& v) const { glUniform4fv(glGetUniformLocation(program_, n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const Mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, n), 1, GL_FALSE, glm::value_ptr(m));
}

} // namespace k
