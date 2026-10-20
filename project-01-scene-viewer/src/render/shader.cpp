#include "shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

bool read_file(const std::string& path, std::string& text) {
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "could not open '%s'\n", path.c_str());
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

GLuint compile(GLenum type, const std::string& path) {
    std::string source;
    if (!read_file(path, source)) {
        return 0;
    }
    const char* text = source.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s: compilation failed:\n%s\n", path.c_str(), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

bool Shader::load(const std::string& vertex_path, const std::string& fragment_path) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_path);
    const GLuint fragment = vertex == 0 ? 0 : compile(GL_FRAGMENT_SHADER, fragment_path);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return false;
    }

    m_id = glCreateProgram();
    glAttachShader(m_id, vertex);
    glAttachShader(m_id, fragment);
    glLinkProgram(m_id);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success = 0;
    glGetProgramiv(m_id, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s + %s: linking failed:\n%s\n", vertex_path.c_str(),
                     fragment_path.c_str(), log);
        destroy();
        return false;
    }
    return true;
}

void Shader::destroy() {
    glDeleteProgram(m_id);
    m_id = 0;
    m_locations.clear();
}

GLint Shader::location(const char* name) {
    const auto found = m_locations.find(name);
    if (found != m_locations.end()) {
        return found->second;
    }
    return m_locations[name] = glGetUniformLocation(m_id, name);
}

void Shader::set(const char* name, int value) {
    glUniform1i(location(name), value);
}

void Shader::set(const char* name, bool value) {
    glUniform1i(location(name), value ? 1 : 0);
}

void Shader::set(const char* name, float value) {
    glUniform1f(location(name), value);
}

void Shader::set(const char* name, const glm::vec3& value) {
    glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat3& value) {
    glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat4& value) {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}
